#include "NPSystem.h"
#include "CConfig.h"
#include "IOCPMessageServer.h"
#include "NPConst.h"
#include "NPSocket.h"
#include "UserSocket.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/NativeSocketTransport.h"

#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

int CurrentLastError() noexcept {
#if defined(_WIN32)
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

void CloseNativeSocket(std::uint64_t socket) noexcept {
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(socket));
#else
    ::close(static_cast<int>(socket));
#endif
}

std::int32_t CurrentProcessId() noexcept {
#if defined(_WIN32)
    return static_cast<std::int32_t>(::GetCurrentProcessId());
#else
    return static_cast<std::int32_t>(::getpid());
#endif
}

void CopyTruncated(char* destination, std::size_t destinationSize, const char* source) noexcept {
    std::size_t index = 0;
    while (index + 1 < destinationSize && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

int CompareCaseInsensitive(const char* left, const char* right) noexcept {
#if defined(_WIN32)
    return ::_stricmp(left, right);
#else
    return ::strcasecmp(left, right);
#endif
}

void ConvertGradeSkillName(const char* source, wchar_t (&destination)[1024]) noexcept {
#if defined(_WIN32)
    std::size_t converted = 0;
    ::mbstowcs_s(&converted, destination, std::size(destination), source, static_cast<std::size_t>(-1));
#else
    const std::size_t converted = std::mbstowcs(destination, source, std::size(destination) - 1);
    if (converted == static_cast<std::size_t>(-1)) {
        destination[0] = L'\0';
    } else {
        destination[converted] = L'\0';
    }
#endif
}

void AppendCriticalMailText(char*& destination, std::uint64_t& remaining, const char* format, ...) noexcept {
    if (remaining == 0) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(destination, static_cast<std::size_t>(remaining), format, arguments);
    va_end(arguments);
    if (written < 0 || static_cast<std::uint64_t>(written) >= remaining) {
        destination += remaining - 1;
        remaining = 1;
        *destination = '\0';
        return;
    }
    destination += written;
    remaining -= static_cast<std::uint64_t>(written);
}

} // namespace

namespace NPSystem {

CNPD g_NPD;
RequestTransactionMap g_requestTransactionMap;
ResponseTransactionMap g_responseTransactionMap;
CYieldLock g_requestTransactionMapSect;
CYieldLock g_responseTransactionMapSect;
ResponseHandler g_ResponseHandler[COMMAND_MAX] = {
    &CNPD::Response_Dummy,
    &CNPD::Response_Dummy,
    &CNPD::Response_AuthLoginTokenForServer,
    &CNPD::Response_AuthRequestTokenForClient,
    &CNPD::Notify_PresenceUserInfo,
    &CNPD::Response_PresenceLogout,
    &CNPD::Notify_PresenceUserLogout,
    &CNPD::Response_ListGrades,
    &CNPD::Notify_GradeChanged,
};

CNPD::CNPD() = default;

CNPD::~CNPD() { CloseNP(); }

void CNPD::SendCriticalMailToTC(CNPSocket* socket, bool closeSocket, const CSTSResponse* response) {
    L2_GUARD_FUNCTION();
    if (!socket) {
        return;
    }

    auto* const message = new CNPSendCriticalMailIOCPMessage();
    if (!message) {
        return;
    }

    char* destination = message->m_szMessage;
    std::uint64_t remaining = 0x7ff;
    // AppendCriticalMailText(
    //     destination,
    //     remaining,
    //     "NPResponse Parsing Error--------------------\r\n\r\n")
    AppendCriticalMailText(destination, remaining, "NP response parsing failed.\r\n\r\n");
    // AppendCriticalMailText(
    //     destination,
    //     remaining,
    //     "[Header Info]\r\n\r\n")
    AppendCriticalMailText(
        destination, remaining, "Header details:\r\nUser connection close requested: %s.\r\n", closeSocket ? "yes" : "no"
    );

    unsigned int accountId = 0;
    if (response) {
        const int command = static_cast<int>(response->m_nCommand);
        const char* commandName = "Unknown";
        if (command >= 0 && command < COMMAND_MAX) {
            // commandName = g_szCommandNames[command][0]
            commandName = g_szCommandNames[command][1];
        }
        if (!response->m_isResponse) {
            // AppendCriticalMailText(
            //     destination,
            //     remaining,
            //     "message, command=%s(%d)\r\n\r\n",
            //     commandName,
            //     command)
            AppendCriticalMailText(destination, remaining, "Notification: %s (command %d).\r\n\r\n", commandName, command);
        } else {
            bool requestFound = false;
            if (response->m_isTransaction) {
                L2_GUARD(g_requestTransactionMapSect);
                const auto request = g_requestTransactionMap.find(static_cast<std::int32_t>(response->m_nTRID));
                if (request != g_requestTransactionMap.end()) {
                    accountId = request->second.m_nAccountID;
                    g_requestTransactionMap.erase(request);
                    requestFound = true;
                }
            }
            // AppendCriticalMailText(
            //     destination,
            //     remaining,
            //     "response, close_socket=%d account_id=%d "
            //     "command=%s(%d) trid=%d response_code=%d "
            //     "string=%s\r\n\r\n",
            //     requestFound ? 1 : 0,
            //     accountId,
            //     commandName,
            //     accountId,
            //     response->m_nTRID,
            //     response->m_nResponseCode,
            //     response->m_pResponseString)
            AppendCriticalMailText(
                destination, remaining,
                "Response: %s (command %d), status %d, transaction %u.\r\n"
                "Transaction header present: %s; matching request: %s; account: %u.\r\n\r\n",
                commandName, command, response->m_nResponseCode, static_cast<unsigned>(response->m_nTRID),
                response->m_isTransaction ? "yes" : "no", requestFound ? "yes" : "no", accountId
            );
        }
    }
    // AppendCriticalMailText(
    //     destination,
    //     remaining,
    //     "[Packet History]\r\n\r\n")
    AppendCriticalMailText(destination, remaining, "Recent sent commands (storage order):\r\n");
    socket->GeneratePacketHistory(destination, remaining);
    message->m_isNeedToCloseSocket = closeSocket;
    message->m_nAccountID = accountId;
    message->Post(0);
}

void CNPD::CloseNP() {
    L2_GUARD_FUNCTION();
    if (m_isConnected && m_spNPSocket) {
        m_isConnected = false;
        m_spNPSocket->GracefulClose();
    }
}

bool CNPD::ConnectNP() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    const SOCKET created = ::socket(AF_INET, SOCK_STREAM, 0);
    if (created == INVALID_SOCKET) {
#else
    const int created = ::socket(AF_INET, SOCK_STREAM, 0);
    if (created == -1) {
#endif
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(e=%d) fail to connect",
        //     L"NPSystem.cpp",
        //     0x31,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot create a TCP socket for %s:%u: socket error %d.",
        //     g_szNPAddress, static_cast<unsigned>(static_cast<std::uint16_t>(g_nNPPort)), CurrentLastError())
        L2_LOG_ERROR(
            Log, "[NP] Cannot create a TCP socket for %s:%u: socket error %d.", g_szNPAddress,
            static_cast<unsigned>(static_cast<std::uint16_t>(g_nNPPort)), CurrentLastError()
        );
        return false;
    }
    const std::uint64_t nativeSocket = static_cast<std::uint64_t>(created);

    sockaddr_in localAddress{};
    localAddress.sin_family = AF_INET;
    if (::bind(created, reinterpret_cast<const sockaddr*>(&localAddress), sizeof(localAddress)) != 0) {
        const int error = CurrentLastError();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(e=%d) fail to bind",
        //     L"NPSystem.cpp",
        //     0x3c,
        //     error)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot bind the local socket for %s:%u: socket error %d.",
        //     g_szNPAddress, static_cast<unsigned>(static_cast<std::uint16_t>(g_nNPPort)), error)
        L2_LOG_ERROR(
            Log, "[NP] Cannot bind the local socket for %s:%u: socket error %d.", g_szNPAddress,
            static_cast<unsigned>(static_cast<std::uint16_t>(g_nNPPort)), error
        );
        CloseNativeSocket(nativeSocket);
        return false;
    }

    std::uint32_t networkAddress = 0;
    if (g_szNPAddress[0] < '1' || g_szNPAddress[0] > '8') {
        const hostent* const host = ::gethostbyname(g_szNPAddress);
        if (!host) {
// Log.Add(
//     LOG_ERROR,
//     L"[%s][%d]NPError(e=%d) fail to gethostbyname()",
//     L"NPSystem.cpp",
//     0x4c,
//     CurrentLastError())
#if defined(_WIN32)
            const int resolverError = CurrentLastError();
#else
            const int resolverError = h_errno;
#endif
            // Log.Add(LOG_ERROR,
            //     "[NP] Cannot resolve host %s: resolver error %d.",
            //     g_szNPAddress, resolverError)
            L2_LOG_ERROR(Log, "[NP] Cannot resolve host %s: resolver error %d.", g_szNPAddress, resolverError);
            return false;
        }
        std::memcpy(&networkAddress, host->h_addr_list[0], static_cast<std::size_t>(host->h_length));
    } else {
        networkAddress = ::inet_addr(g_szNPAddress);
    }

    sockaddr_in remoteAddress{};
    remoteAddress.sin_family = AF_INET;
    remoteAddress.sin_addr.s_addr = networkAddress;
    remoteAddress.sin_port = htons(static_cast<std::uint16_t>(g_nNPPort));
    if (::connect(created, reinterpret_cast<const sockaddr*>(&remoteAddress), sizeof(remoteAddress)) != 0) {
        const int error = CurrentLastError();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(e=%d) fail to connect()",
        //     L"NPSystem.cpp",
        //     0x5a,
        //     error)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot connect to %s:%u: socket error %d.",
        //     g_szNPAddress, static_cast<unsigned>(static_cast<std::uint16_t>(g_nNPPort)), error)
        L2_LOG_ERROR(
            Log, "[NP] Cannot connect to %s:%u: socket error %d.", g_szNPAddress,
            static_cast<unsigned>(static_cast<std::uint16_t>(g_nNPPort)), error
        );
        CloseNativeSocket(nativeSocket);
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d][NO_ERROR]NPError success to connect()",
    //     L"NPSystem.cpp",
    //     0x5f)
    // Log.Add(LOG_NORMAL,
    //     "[NP] TCP connection established to %s:%u. Initializing the NP transport.",
    //     g_szNPAddress, static_cast<unsigned>(static_cast<std::uint16_t>(g_nNPPort)))
    L2_LOG_INFO(
        Log, "[NP] TCP connection established to %s:%u; initializing the NP transport.", g_szNPAddress,
        static_cast<unsigned>(static_cast<std::uint16_t>(g_nNPPort))
    );

    CNPSocket* const socket = new CNPSocket(nativeSocket);
    m_spNPSocket.SetObject(socket, ObjectRefType::FunctionChain);
    m_isConnected = true;

    const bool bound = portable::BindNativeSocketTransport(
        socket, nativeSocket,
        [](void* owner, const std::uint8_t* data, std::size_t length) {
            return portable::FeedSocketRead(static_cast<CNPSocket*>(owner), data, length);
        },
        [](void* owner, int error) { portable::FeedSocketTerminal(static_cast<CNPSocket*>(owner), error); }
    );
    if (!bound) {
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot attach the TCP connection to the portable transport for %s:%u. Closing the native socket.",
        //     g_szNPAddress, static_cast<unsigned>(static_cast<std::uint16_t>(g_nNPPort)))
        L2_LOG_ERROR(
            Log, "[NP] Cannot attach the TCP connection to the portable transport for %s:%u; closing the native socket.",
            g_szNPAddress, static_cast<unsigned>(static_cast<std::uint16_t>(g_nNPPort))
        );
        CloseNativeSocket(nativeSocket);
    }
    socket->Initialize();
    return true;
}

bool CNPD::SendConnectServer() {
    L2_GUARD_FUNCTION();
    if (!IsValid()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(e=%d) fail to SendConnectServer(), "
        //     L"IsValid() failed",
        //     L"NPSystem.cpp",
        //     0xde,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot send /Sts/Connect: the NP connection is unavailable.")
        L2_LOG_ERROR(Log, "[NP] Cannot send /Sts/Connect: the NP connection is unavailable.");
        return false;
    }

    g_szClientAddr[0] = '\0';
    sockaddr_in localAddress{};
#if defined(_WIN32)
    int addressLength = sizeof(localAddress);
    ::getsockname(
        static_cast<SOCKET>(m_spNPSocket->m_hSocket.load(std::memory_order_relaxed)),
        reinterpret_cast<sockaddr*>(&localAddress), &addressLength
    );
#else
    socklen_t addressLength = sizeof(localAddress);
    ::getsockname(
        static_cast<int>(m_spNPSocket->m_hSocket.load(std::memory_order_relaxed)),
        reinterpret_cast<sockaddr*>(&localAddress), &addressLength
    );
#endif
    const auto* const address = reinterpret_cast<const unsigned char*>(&localAddress.sin_addr.s_addr);
    std::snprintf(
        g_szClientAddr, sizeof(g_szClientAddr), "%d.%d.%d.%d", static_cast<int>(address[0]), static_cast<int>(address[1]),
        static_cast<int>(address[2]), static_cast<int>(address[3])
    );

    CSTSRequest request;
    if (!request.Init(METHOD_POST, COMMAND_STS_CONNECT)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(e=%d) fail to init /Sts/Connect",
        //     L"NPSystem.cpp",
        //     0xec,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot initialize the request for /Sts/Connect, request not sent.")
        L2_LOG_ERROR(Log, "[NP] Cannot initialize the request for /Sts/Connect; request not sent.");
        return false;
    }

    const bool bodyStarted = request.StartBody();
    const bool elementStarted = request.StartElement("Connect");
    const bool connectionType = request.AddElementIntValue("ConnType", 0x191);
    const bool build = request.AddElementIntValue("Build", 4000);
    const bool program = request.AddElementIntValue("Program", g_nProgramId);
    const bool clientAddress = request.AddElementStringValue("Address", g_szClientAddr);
    const bool process = request.AddElementIntValue("Process", CurrentProcessId());
    const bool elementEnded = request.EndElement();
    const bool bodyEnded = request.EndBody();
    if (!(bodyStarted & elementStarted & connectionType & build & program & clientAddress & process & elementEnded &
            bodyEnded)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(e=%d) fail to init /Sts/Connect",
        //     L"NPSystem.cpp",
        //     0xfe,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot serialize the request for /Sts/Connect, request not sent.")
        L2_LOG_ERROR(Log, "[NP] Cannot serialize the request for /Sts/Connect; request not sent.");
        return false;
    }

    CIOBuffer* buffer = request.GeneratePakcet();
    if (!buffer) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(e=%d) fail to alloc iobuffer "
        //     L"/Sts/Connect",
        //     L"NPSystem.cpp",
        //     0x105,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot create the outgoing buffer for /Sts/Connect, request not sent.")
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot obtain the outgoing buffer for /Sts/Connect, request not sent.")
        L2_LOG_ERROR(Log, "[NP] Cannot obtain the outgoing buffer for /Sts/Connect; request not sent.");
        return false;
    }

    m_spNPSocket->SendBuffer(request.m_nCommand, static_cast<int>(request.m_nTransactionID), buffer);
    return true;
}

bool CNPD::SendPing() {
    L2_GUARD_FUNCTION();
    if (!IsValid()) {
        // Noise: The 30-second ping timer also runs without an NP connection. Connection failures are logged where they
        // occur. Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(e=%d) fail to IsValid() /Sts/Ping",
        //     L"NPSystem.cpp",
        //     0x115,
        //     CurrentLastError())
        return false;
    }

    CSTSRequest request;
    if (!request.Init(METHOD_POST, COMMAND_STS_PING)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(e=%d) fail to init /Sts/Ping",
        //     L"NPSystem.cpp",
        //     0x11c,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot initialize the request for /Sts/Ping, request not sent.")
        L2_LOG_ERROR(Log, "[NP] Cannot initialize the request for /Sts/Ping; request not sent.");
        return false;
    }

    const bool bodyStarted = request.StartBody();
    const bool bodyEnded = request.EndBody();
    if (!(bodyStarted & bodyEnded)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(e=%d) fail to generate /Sts/Ping",
        //     L"NPSystem.cpp",
        //     0x126,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot serialize the request for /Sts/Ping, request not sent.")
        L2_LOG_ERROR(Log, "[NP] Cannot serialize the request for /Sts/Ping; request not sent.");
        return false;
    }

    CIOBuffer* buffer = request.GeneratePakcet();
    if (!buffer) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(e=%d) fail to alloc /Sts/Ping",
        //     L"NPSystem.cpp",
        //     0x12d,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot create the outgoing buffer for /Sts/Ping, request not sent.")
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot obtain the outgoing buffer for /Sts/Ping, request not sent.")
        L2_LOG_ERROR(Log, "[NP] Cannot obtain the outgoing buffer for /Sts/Ping; request not sent.");
        return false;
    }

    m_spNPSocket->SendBuffer(request.m_nCommand, static_cast<int>(request.m_nTransactionID), buffer);
    return true;
}

bool CNPD::SendAuthLoginTokenForServer(int accountId, const char* loginToken) {
    L2_GUARD_FUNCTION();
    if (!loginToken || *loginToken == '\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) invalid argument(szLoginToken)",
        //     L"NPSystem.cpp",
        //     0x142,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_WARN,
        //     "[NP] Cannot send /Auth/LoginToken for account %d: login token is missing.",
        //     accountId)
        L2_LOG_WARN(Log, "[NP] Cannot send /Auth/LoginToken for account %d: login token is missing.", accountId);
        return false;
    }
    if (!IsValid()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to CNPD::IsValid() ",
        //     L"NPSystem.cpp",
        //     0x148,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_WARN,
        //     "[NP] Cannot send /Auth/LoginToken for account %d: the NP connection is unavailable.",
        //     accountId)
        L2_LOG_WARN(Log, "[NP] Cannot send /Auth/LoginToken for account %d: the NP connection is unavailable.", accountId);
        return false;
    }

    CSTSRequest request;
    if (!request.Init(METHOD_POST, COMMAND_AUTH_LOGINTOKENFORSERVER)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to send request(AuthToken)",
        //     L"NPSystem.cpp",
        //     0x14f,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot initialize the request for /Auth/LoginToken (account %d). Request not sent.",
        //     accountId)
        L2_LOG_ERROR(
            Log, "[NP] Cannot initialize the request for /Auth/LoginToken (account %d); request not sent.", accountId
        );
        return false;
    }

    const bool transactionHeader = request.SetHeaderAsTransaction(accountId);
    const bool bodyStarted = request.StartBody();
    const bool elementStarted = request.StartElement("Request");
    const bool appId = request.AddElementStringValue("AppId", g_pServerAppKey);
    const bool provider = request.AddElementStringValue("AuthProviderCode", " ");
    const bool token = request.AddElementStringValue("AuthnToken", loginToken);
    const bool clientAddress = request.AddElementStringValue("ClientNetAddress", g_szClientAddr);
    const bool elementEnded = request.EndElement();
    const bool bodyEnded = request.EndBody();
    if (!(transactionHeader & bodyStarted & elementStarted & appId & provider & token & clientAddress & elementEnded &
            bodyEnded)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to generate request(AuthToken)",
        //     L"NPSystem.cpp",
        //     0x160,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot serialize the request for /Auth/LoginToken (account %d). Request not sent.",
        //     accountId)
        L2_LOG_ERROR(
            Log, "[NP] Cannot serialize the request for /Auth/LoginToken (account %d); request not sent.", accountId
        );
        return false;
    }

    CIOBuffer* buffer = request.GeneratePakcet();
    if (!buffer) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to alloc buffer(AuthToken)",
        //     L"NPSystem.cpp",
        //     0x167,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot create the outgoing buffer for /Auth/LoginToken (account %d). Request not sent.",
        //     accountId)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot obtain the outgoing buffer for /Auth/LoginToken (account %d). Request not sent.",
        //     accountId)
        L2_LOG_ERROR(
            Log, "[NP] Cannot obtain the outgoing buffer for /Auth/LoginToken (account %d); request not sent.", accountId
        );
        return false;
    }

    m_spNPSocket->SendBuffer(request.m_nCommand, static_cast<int>(request.m_nTransactionID), buffer);
    return true;
}

bool CNPD::SendAuthRequestTokenForClient(int accountId, const char* userGuid) {
    L2_GUARD_FUNCTION();
    if (!userGuid || *userGuid == '\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) invalid argument(szUserGUID)",
        //     L"NPSystem.cpp",
        //     0x1b5,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_WARN,
        //     "[NP] Cannot send /Auth/RequestToken for account %d: user GUID is missing.",
        //     accountId)
        L2_LOG_WARN(Log, "[NP] Cannot send /Auth/RequestToken for account %d: user GUID is missing.", accountId);
        return false;
    }
    if (!IsValid()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to IsValid()",
        //     L"NPSystem.cpp",
        //     0x1bb,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_WARN,
        //     "[NP] Cannot send /Auth/RequestToken for account %d: the NP connection is unavailable.",
        //     accountId)
        L2_LOG_WARN(Log, "[NP] Cannot send /Auth/RequestToken for account %d: the NP connection is unavailable.", accountId);
        return false;
    }

    CSTSRequest request;
    if (!request.Init(METHOD_POST, COMMAND_AUTH_REQUESTTOKENFORCLIENT)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to IsValid()",
        //     L"NPSystem.cpp",
        //     0x1c2,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot initialize the request for /Auth/RequestToken (account %d). Request not sent.",
        //     accountId)
        L2_LOG_ERROR(
            Log, "[NP] Cannot initialize the request for /Auth/RequestToken (account %d); request not sent.", accountId
        );
        return false;
    }

    const bool transactionHeader = request.SetHeaderAsTransaction(accountId);
    const bool bodyStarted = request.StartBody();
    const bool elementStarted = request.StartElement("Request");
    const bool userId = request.AddElementStringValue("UserId", userGuid);
    const bool appId = request.AddElementStringValue("AppId", g_pClientAppKey);
    const bool elementEnded = request.EndElement();
    const bool bodyEnded = request.EndBody();
    if (!(transactionHeader & bodyStarted & elementStarted & userId & appId & elementEnded & bodyEnded)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to generate "
        //     L"/Auth/RequestToken",
        //     L"NPSystem.cpp",
        //     0x1d1,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot serialize the request for /Auth/RequestToken (account %d). Request not sent.",
        //     accountId)
        L2_LOG_ERROR(
            Log, "[NP] Cannot serialize the request for /Auth/RequestToken (account %d); request not sent.", accountId
        );
        return false;
    }

    CIOBuffer* buffer = request.GeneratePakcet();
    if (!buffer) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to generate "
        //     L"/Auth/RequestToken",
        //     L"NPSystem.cpp",
        //     0x1d9,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot create the outgoing buffer for /Auth/RequestToken (account %d). Request not sent.",
        //     accountId)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot obtain the outgoing buffer for /Auth/RequestToken (account %d). Request not sent.",
        //     accountId)
        L2_LOG_ERROR(
            Log, "[NP] Cannot obtain the outgoing buffer for /Auth/RequestToken (account %d); request not sent.", accountId
        );
        return false;
    }

    m_spNPSocket->SendBuffer(request.m_nCommand, static_cast<int>(request.m_nTransactionID), buffer);
    return true;
}

bool CNPD::SendPresenceLogout(int accountId, const char* userGuid) {
    L2_GUARD_FUNCTION();
    if (!userGuid || *userGuid == '\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) invalid argument(szUserGUID)",
        //     L"NPSystem.cpp",
        //     0x179,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_WARN,
        //     "[NP] Cannot send /Presence/Logout for account %d: user GUID is missing.",
        //     accountId)
        L2_LOG_WARN(Log, "[NP] Cannot send /Presence/Logout for account %d: user GUID is missing.", accountId);
        return false;
    }
    if (!IsValid()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to IsValid()",
        //     L"NPSystem.cpp",
        //     0x17f,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_WARN,
        //     "[NP] Cannot send /Presence/Logout for account %d: the NP connection is unavailable.",
        //     accountId)
        L2_LOG_WARN(Log, "[NP] Cannot send /Presence/Logout for account %d: the NP connection is unavailable.", accountId);
        return false;
    }

    CSTSRequest request;
    if (!request.Init(METHOD_POST, COMMAND_PRESENCE_LOGOUT)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to init /Presence/Logout",
        //     L"NPSystem.cpp",
        //     0x186,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot initialize the request for /Presence/Logout (account %d). Request not sent.",
        //     accountId)
        L2_LOG_ERROR(
            Log, "[NP] Cannot initialize the request for /Presence/Logout (account %d); request not sent.", accountId
        );
        return false;
    }

    char destination[38]{};
    std::snprintf(destination, sizeof(destination), "$%s", userGuid);
    const bool transactionHeader = request.SetHeaderAsTransaction(accountId);
    const bool destinationHeader = request.SetHeaderAsString(HEADERNAME_TO, destination);
    const bool bodyStarted = request.StartBody();
    const bool elementStarted = request.StartElement("Request");
    const bool userId = request.AddElementStringValue("UserId", userGuid);
    const bool elementEnded = request.EndElement();
    const bool bodyEnded = request.EndBody();
    if (!(transactionHeader & destinationHeader & bodyStarted & elementStarted & userId & elementEnded & bodyEnded)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to generate /Presence/Logout",
        //     L"NPSystem.cpp",
        //     0x198,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot serialize the request for /Presence/Logout (account %d). Request not sent.",
        //     accountId)
        L2_LOG_ERROR(
            Log, "[NP] Cannot serialize the request for /Presence/Logout (account %d); request not sent.", accountId
        );
        return false;
    }

    CIOBuffer* buffer = request.GeneratePakcet();
    if (!buffer) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to alloc buffer /Presence/Logout",
        //     L"NPSystem.cpp",
        //     0x19f,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot create the outgoing buffer for /Presence/Logout (account %d). Request not sent.",
        //     accountId)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot obtain the outgoing buffer for /Presence/Logout (account %d). Request not sent.",
        //     accountId)
        L2_LOG_ERROR(
            Log, "[NP] Cannot obtain the outgoing buffer for /Presence/Logout (account %d); request not sent.", accountId
        );
        return false;
    }

    m_spNPSocket->SendBuffer(request.m_nCommand, static_cast<int>(request.m_nTransactionID), buffer);
    return true;
}

bool CNPD::SendListGrades(int accountId, const char* userGuid) {
    L2_GUARD_FUNCTION();
    if (!userGuid || *userGuid == '\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) invalid argument(szUserGUID)",
        //     L"NPSystem.cpp",
        //     0x2f2,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_WARN,
        //     "[NP] Cannot send /Grade/ListGrades for account %d: user GUID is missing.",
        //     accountId)
        L2_LOG_WARN(Log, "[NP] Cannot send /Grade/ListGrades for account %d: user GUID is missing.", accountId);
        return false;
    }
    if (!IsValid()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to IsValid()",
        //     L"NPSystem.cpp",
        //     0x2f8,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_WARN,
        //     "[NP] Cannot send /Grade/ListGrades for account %d: the NP connection is unavailable.",
        //     accountId)
        L2_LOG_WARN(Log, "[NP] Cannot send /Grade/ListGrades for account %d: the NP connection is unavailable.", accountId);
        return false;
    }

    CSTSRequest request;
    if (!request.Init(METHOD_POST, COMMAND_LIST_GRADE)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to init "
        //     L"/Grade/ListGrades",
        //     L"NPSystem.cpp",
        //     0x2ff,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot initialize the request for /Grade/ListGrades (account %d). Request not sent.",
        //     accountId)
        L2_LOG_ERROR(
            Log, "[NP] Cannot initialize the request for /Grade/ListGrades (account %d); request not sent.", accountId
        );
        return false;
    }

    char destination[38]{};
    std::snprintf(destination, sizeof(destination), "$%s", userGuid);
    const bool transactionHeader = request.SetHeaderAsTransaction(accountId);
    const bool destinationHeader = request.SetHeaderAsString(HEADERNAME_TO, destination);
    const bool bodyStarted = request.StartBody();
    const bool elementStarted = request.StartElement("Request");
    const bool userId = request.AddElementStringValue("UserId", userGuid);
    const bool appGroupCode = request.AddElementStringValue("AppGroupCode", g_pAppGroupCode);
    const bool allGrades = request.AddElementStringValue("AllGrades", "true");
    const bool elementEnded = request.EndElement();
    const bool bodyEnded = request.EndBody();
    if (!(transactionHeader & destinationHeader & bodyStarted & elementStarted & userId & appGroupCode & allGrades &
            elementEnded & bodyEnded)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to generate "
        //     L"/Grade/ListGrades",
        //     L"NPSystem.cpp",
        //     0x313,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot serialize the request for /Grade/ListGrades (account %d). Request not sent.",
        //     accountId)
        L2_LOG_ERROR(
            Log, "[NP] Cannot serialize the request for /Grade/ListGrades (account %d); request not sent.", accountId
        );
        return false;
    }

    CIOBuffer* buffer = request.GeneratePakcet();
    if (!buffer) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to alloc buffer "
        //     L"/Grade/ListGrades",
        //     L"NPSystem.cpp",
        //     0x31a,
        //     accountId,
        //     CurrentLastError())
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot create the outgoing buffer for /Grade/ListGrades (account %d). Request not sent.",
        //     accountId)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot obtain the outgoing buffer for /Grade/ListGrades (account %d). Request not sent.",
        //     accountId)
        L2_LOG_ERROR(
            Log, "[NP] Cannot obtain the outgoing buffer for /Grade/ListGrades (account %d); request not sent.", accountId
        );
        return false;
    }

    m_spNPSocket->SendBuffer(request.m_nCommand, static_cast<int>(request.m_nTransactionID), buffer);
    return true;
}

bool CNPD::LoginStep_Init(unsigned int accountId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(accountMapSect);
    if (g_accountLoginStep.find(accountId) != g_accountLoginStep.end()) {
        return false;
    }
    return g_account2NPGuidMap.find(accountId) == g_account2NPGuidMap.end();
}

bool CNPD::LoginStep_SetServerLoginComplete(unsigned int accountId, const char* userGuid) {
    L2_GUARD_FUNCTION();
    if (!userGuid || *userGuid == '\0') {
        return false;
    }

    L2_GUARD(accountMapSect);
    g_accountLoginStep[accountId] = LS_NP_GAMESERVER_LOGIN_COMPLETE;
    g_account2NPGuidMap[accountId] = std::string(userGuid);
    g_npGuid2AccountMap[std::string(userGuid)] = accountId;
    g_NPD.SendListGrades(static_cast<int>(accountId), userGuid);
    return true;
}

bool CNPD::LoginStep_SetClientLoginComplete(unsigned int accountId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(accountMapSect);

    const auto loginStep = g_accountLoginStep.find(accountId);
    if (loginStep == g_accountLoginStep.end()) {
        return false;
    }
    loginStep->second = LS_NP_LOGIN_COMPLETE;

    std::array<char, 0x25> userGuid{};
    const auto guid = g_account2NPGuidMap.find(accountId);
    if (guid == g_account2NPGuidMap.end()) {
        return false;
    }
    std::strncpy(userGuid.data(), guid->second.c_str(), userGuid.size() - 1);
    userGuid.back() = '\0';

    const auto account = accountMap.find(accountId);
    if (account == accountMap.end()) {
        return false;
    }
    CUserSocket* const socket = account->second;
    std::strncpy(socket->m_szNPUserGUIDA.data(), userGuid.data(), socket->m_szNPUserGUIDA.size() - 1);
    socket->m_szNPUserGUIDA.back() = '\0';
    return true;
}

bool CNPD::LoginStep_Logout(unsigned int accountId) {
    L2_GUARD_FUNCTION();
    if (!g_bUseNPLogin) {
        return true;
    }

    bool sendLogout = false;
    char userGuid[38]{};
    {
        L2_GUARD(accountMapSect);

        const auto loginStep = g_accountLoginStep.find(accountId);
        if (loginStep == g_accountLoginStep.end()) {
            // Noise: Disconnect cleanup can run before NP login state exists. There is no map entry to erase.
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] fail g_accountLoginStep.erase(nAccountID=%d)",
            //     L"NPSystem.cpp",
            //     0x4a9,
            //     accountId)
        } else {
            sendLogout = loginStep->second >= LS_NP_GAMESERVER_LOGIN_COMPLETE;
            g_accountLoginStep.erase(loginStep);
        }

        const auto guid = g_account2NPGuidMap.find(accountId);
        if (guid == g_account2NPGuidMap.end()) {
            // Noise: Disconnect cleanup can run before a GUID was assigned. A required logout with no GUID is reported
            // below. Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] fail g_account2NPGuidMap.erase(nAccountID=%d)",
            //     L"NPSystem.cpp",
            //     0x4bc,
            //     accountId)
        } else {
            std::strncpy(userGuid, guid->second.c_str(), sizeof(userGuid) - 1);
            userGuid[sizeof(userGuid) - 1] = '\0';
            g_npGuid2AccountMap.erase(std::string(userGuid));
            g_account2NPGuidMap.erase(guid);
        }
    }

    if (sendLogout) {
        if (*userGuid == '\0') {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] fail to send NP Logout(%d)",
            //     L"NPSystem.cpp",
            //     0x4c9,
            //     accountId)
            // Log.Add(LOG_ERROR,
            //     "[NP] Cannot send /Presence/Logout for account %u: login reached the server-complete stage but its user
            //     GUID is missing.", accountId)
            L2_LOG_ERROR(
                Log,
                "[NP] Cannot send /Presence/Logout for account %u: login reached the server-complete stage but its user "
                "GUID is missing.",
                accountId
            );
        } else {
            g_NPD.SendPresenceLogout(static_cast<int>(accountId), userGuid);
        }
    }
    return true;
}

bool CNPD::LoginStep_IsComplete(unsigned int accountId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(accountMapSect);
    const auto loginStep = g_accountLoginStep.find(accountId);
    return loginStep != g_accountLoginStep.end() && loginStep->second == LS_NP_LOGIN_COMPLETE;
}

bool CNPD::Response_Dummy(CNPSocket*, STSRequestTI*, CSTSResponse*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool CNPD::Response_AuthLoginTokenForServer(CNPSocket* socket, STSRequestTI* request, CSTSResponse* response) {
    L2_GUARD_FUNCTION();

    if ((!socket && !request) || !response) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) invalid argument",
        //     L"NPSystem.cpp",
        //     0x1fa,
        //     request->m_nAccountID,
        //     7)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot handle /Auth/LoginToken: invalid context (socket present %d, request present %d, response present
        //     %d), login failure code 7.", socket != nullptr, request != nullptr, response != nullptr)
        L2_LOG_ERROR(
            Log,
            "[NP] Cannot handle /Auth/LoginToken: invalid context (socket present %d, request present %d, response present "
            "%d); login failure code 7.",
            socket != nullptr, request != nullptr, response != nullptr
        );
        CUserSocket::OnLoginFailed(
            nullptr, request->m_nAccountID,
            // 7)
            LEC_NPFAIL_GSLOGIN_ACK_INVALIDARG
        );
        return false;
    }

    if (response->m_nResponseCode != 200) {
        // if (response->m_pResponseString) {
        //     Log.Add(
        //         LOG_ERROR,
        //         L"[%s][%d]NPError(a=%d, e=%d) response error("
        //         L"code:%d, msg:%S)",
        //         L"NPSystem.cpp",
        //         0x204,
        //         request->m_nAccountID,
        //         8,
        //         response->m_nResponseCode,
        //         response->m_pResponseString)
        // }
        // Log.Add(LOG_WARN,
        //     "[NP] /Auth/LoginToken rejected for account %u: NP status %d. Login failure code 8.",
        //     request->m_nAccountID, response->m_nResponseCode)
        L2_LOG_WARN(
            Log, "[NP] /Auth/LoginToken rejected for account %u: NP status %d; login failure code 8.", request->m_nAccountID,
            response->m_nResponseCode
        );
        CUserSocket::OnLoginFailed(
            nullptr, request->m_nAccountID,
            // 8)
            LEC_NPFAIL_GSLOGIN_ACK_INVALIDRESPONSECODE
        );
        return false;
    }

    pugi::xml_document document;
    if (!response->ParseBody(document)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) invalid response body 1",
        //     L"NPSystem.cpp",
        //     0x215,
        //     request->m_nAccountID,
        //     9)
        // Log.Add(LOG_ERROR,
        //     "[NP] /Auth/LoginToken failed for account %u: the response body is not valid XML. Login failure code 9.",
        //     request->m_nAccountID)
        // Log.Add(LOG_ERROR,
        //     "[NP] /Auth/LoginToken failed for account %u: cannot parse the response body as XML. Login failure code 9.",
        //     request->m_nAccountID)
        L2_LOG_ERROR(
            Log, "[NP] /Auth/LoginToken failed for account %u: cannot parse the response body as XML; login failure code 9.",
            request->m_nAccountID
        );
        CUserSocket::OnLoginFailed(
            nullptr, request->m_nAccountID,
            // 9)
            LEC_NPFAIL_GSLOGIN_ACK_INVALIDRESPONSEBODY
        );
        return false;
    }

    const char* const userGuid = document.child("Reply").child_value("UserId");
    if (!userGuid || *userGuid == '\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) invalid response body 2",
        //     L"NPSystem.cpp",
        //     0x21e,
        //     request->m_nAccountID,
        //     9)
        // Log.Add(LOG_ERROR,
        //     "[NP] /Auth/LoginToken failed for account %u: Reply.UserId is missing. Login failure code 9.",
        //     request->m_nAccountID)
        L2_LOG_ERROR(
            Log, "[NP] /Auth/LoginToken failed for account %u: Reply.UserId is missing; login failure code 9.",
            request->m_nAccountID
        );
        CUserSocket::OnLoginFailed(
            nullptr, request->m_nAccountID,
            // 9)
            LEC_NPFAIL_GSLOGIN_ACK_INVALIDRESPONSEBODY
        );
        return false;
    }

    CUserSocket* userSocket = nullptr;
    {
        L2_GUARD(accountMapSect);
        const auto account = accountMap.find(request->m_nAccountID);
        if (account != accountMap.end()) {
            userSocket = account->second;
        }
    }
    if (!userSocket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) invalid user socket",
        //     L"NPSystem.cpp",
        //     0x231,
        //     request->m_nAccountID,
        //     10)
        // Log.Add(LOG_WARN,
        //     "[NP] /Auth/LoginToken failed for account %u: the user connection is no longer registered. Login failure
        //     code 10.", request->m_nAccountID)
        L2_LOG_WARN(
            Log,
            "[NP] /Auth/LoginToken failed for account %u: the user connection is no longer registered; login failure code "
            "10.",
            request->m_nAccountID
        );
        CUserSocket::OnLoginFailed(
            nullptr, request->m_nAccountID,
            // 10)
            LEC_NPFAIL_GSLOGIN_ACK_INVALIDUSERSOCKET
        );
        return false;
    }

    CopyTruncated(userSocket->m_szNPUserGUIDA.data(), userSocket->m_szNPUserGUIDA.size(), userGuid);
    if (!LoginStep_SetServerLoginComplete(request->m_nAccountID, userGuid)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to set login complete",
        //     L"NPSystem.cpp",
        //     0x23a,
        //     request->m_nAccountID,
        //     11)
        // Log.Add(LOG_ERROR,
        //     "[NP] /Auth/LoginToken failed for account %u: cannot complete the server login step. Login failure code 11.",
        //     request->m_nAccountID)
        L2_LOG_ERROR(
            Log,
            "[NP] /Auth/LoginToken failed for account %u: cannot complete the server login step; login failure code 11.",
            request->m_nAccountID
        );
        CUserSocket::OnLoginFailed(
            nullptr, request->m_nAccountID,
            // 11)
            LEC_NPFAIL_GSLOGIN_ACK_FAILTOSETLOGINCOMPLETE
        );
        return false;
    }
    if (!g_NPD.SendAuthRequestTokenForClient(static_cast<int>(request->m_nAccountID), userGuid)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) fail to /Auth/RequestToken",
        //     L"NPSystem.cpp",
        //     0x242,
        //     request->m_nAccountID,
        //     12)
        // Log.Add(LOG_ERROR,
        //     "[NP] /Auth/RequestToken failed for account %u: cannot send the client token request. Login failure code 12.",
        //     request->m_nAccountID)
        L2_LOG_ERROR(
            Log,
            "[NP] /Auth/RequestToken failed for account %u: cannot send the client token request; login failure code 12.",
            request->m_nAccountID
        );
        CUserSocket::OnLoginFailed(
            nullptr, request->m_nAccountID,
            // 12)
            LEC_NPFAIL_GCTOKEN_REQ
        );
    }
    return false;
}

bool CNPD::Response_AuthRequestTokenForClient(CNPSocket* socket, STSRequestTI* request, CSTSResponse* response) {
    L2_GUARD_FUNCTION();

    if (!request || !response || !socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) invalid argument",
        //     L"NPSystem.cpp",
        //     0x255,
        //     request->m_nAccountID,
        //     13)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot handle /Auth/RequestToken: invalid context (socket present %d, request present %d, response
        //     present %d), login failure code 13.", socket != nullptr, request != nullptr, response != nullptr)
        L2_LOG_ERROR(
            Log,
            "[NP] Cannot handle /Auth/RequestToken: invalid context (socket present %d, request present %d, response "
            "present %d); login failure code 13.",
            socket != nullptr, request != nullptr, response != nullptr
        );
        CUserSocket::OnLoginFailed(
            nullptr, request->m_nAccountID,
            // 13)
            LEC_NPFAIL_GCTOKEN_ACK_INVALIDARG
        );
        return false;
    }

    if (response->m_nResponseCode != 200) {
        // if (response->m_pResponseString) {
        //     Log.Add(
        //         LOG_ERROR,
        //         L"[%s][%d]NPError(a=%d, e=%d) response error("
        //         L"code:%d, msg:%S)",
        //         L"NPSystem.cpp",
        //         0x25e,
        //         request->m_nAccountID,
        //         14,
        //         response->m_nResponseCode,
        //         response->m_pResponseString)
        // }
        // Log.Add(LOG_WARN,
        //     "[NP] /Auth/RequestToken rejected for account %u: NP status %d. Login failure code 14.",
        //     request->m_nAccountID, response->m_nResponseCode)
        L2_LOG_WARN(
            Log, "[NP] /Auth/RequestToken rejected for account %u: NP status %d; login failure code 14.",
            request->m_nAccountID, response->m_nResponseCode
        );
        CUserSocket::OnLoginFailed(
            nullptr, request->m_nAccountID,
            // 14)
            LEC_NPFAIL_GCTOKEN_ACK_INVALIDRESPONSECODE
        );
        return false;
    }

    pugi::xml_document document;
    if (!response->ParseBody(document)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) invalid response body 1",
        //     L"NPSystem.cpp",
        //     0x26e,
        //     request->m_nAccountID,
        //     15)
        // Log.Add(LOG_ERROR,
        //     "[NP] /Auth/RequestToken failed for account %u: the response body is not valid XML. Login failure code 15.",
        //     request->m_nAccountID)
        // Log.Add(LOG_ERROR,
        //     "[NP] /Auth/RequestToken failed for account %u: cannot parse the response body as XML. Login failure
        //     code 15.", request->m_nAccountID)
        L2_LOG_ERROR(
            Log,
            "[NP] /Auth/RequestToken failed for account %u: cannot parse the response body as XML; login failure code 15.",
            request->m_nAccountID
        );
        CUserSocket::OnLoginFailed(
            nullptr, request->m_nAccountID,
            // 15)
            LEC_NPFAIL_GCTOKEN_ACK_INVALIDRESPONSEBODY
        );
        return false;
    }

    const char* const authenticationToken = document.child("Reply").child_value("AuthnToken");
    if (!authenticationToken || *authenticationToken == '\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) invalid response body 2",
        //     L"NPSystem.cpp",
        //     0x277,
        //     request->m_nAccountID,
        //     15)
        // Log.Add(LOG_ERROR,
        //     "[NP] /Auth/RequestToken failed for account %u: Reply.AuthnToken is missing. Login failure code 15.",
        //     request->m_nAccountID)
        L2_LOG_ERROR(
            Log, "[NP] /Auth/RequestToken failed for account %u: Reply.AuthnToken is missing; login failure code 15.",
            request->m_nAccountID
        );
        CUserSocket::OnLoginFailed(
            nullptr, request->m_nAccountID,
            // 15)
            LEC_NPFAIL_GCTOKEN_ACK_INVALIDRESPONSEBODY
        );
        return false;
    }

    CUserSocket* userSocket = nullptr;
    {
        L2_GUARD(accountMapSect);
        const auto account = accountMap.find(request->m_nAccountID);
        if (account != accountMap.end()) {
            userSocket = account->second;
        }
    }
    if (!userSocket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) invalid user socket",
        //     L"NPSystem.cpp",
        //     0x28a,
        //     request->m_nAccountID,
        //     16)
        // Log.Add(LOG_WARN,
        //     "[NP] /Auth/RequestToken failed for account %u: the user connection is no longer registered. Login failure
        //     code 16.", request->m_nAccountID)
        L2_LOG_WARN(
            Log,
            "[NP] /Auth/RequestToken failed for account %u: the user connection is no longer registered; login failure code "
            "16.",
            request->m_nAccountID
        );
        CUserSocket::OnLoginFailed(
            nullptr, request->m_nAccountID,
            // 16)
            LEC_NPFAIL_GCTOKEN_ACK_INVALIDUSERSOCKET
        );
        return false;
    }

    if (!LoginStep_SetClientLoginComplete(request->m_nAccountID)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) invalid response body 2",
        //     L"NPSystem.cpp",
        //     0x29a,
        //     request->m_nAccountID,
        //     17)
        // Log.Add(LOG_ERROR,
        //     "[NP] /Auth/RequestToken failed for account %u: cannot complete the client login step. Login failure
        //     code 17.", request->m_nAccountID)
        L2_LOG_ERROR(
            Log,
            "[NP] /Auth/RequestToken failed for account %u: cannot complete the client login step; login failure code 17.",
            request->m_nAccountID
        );
        CUserSocket::OnLoginFailed(
            nullptr, request->m_nAccountID,
            // 17)
            LEC_NPFAIL_LOGINCOMPLETE
        );
        return false;
    }

    CUserSocket::OnLoginSuccess(userSocket, static_cast<int>(std::strlen(authenticationToken)), authenticationToken);
    return false;
}

bool CNPD::Notify_PresenceUserInfo(CNPSocket*, STSRequestTI*, CSTSResponse*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool CNPD::Response_PresenceLogout(CNPSocket*, STSRequestTI*, CSTSResponse*) {
    L2_GUARD_FUNCTION();
    return false;
}

bool CNPD::Notify_PresenceUserLogout(CNPSocket* socket, STSRequestTI* request, CSTSResponse* response) {
    L2_GUARD_FUNCTION();
    // constexpr int errorCode = 0x13.
    constexpr int errorCode = LEC_NPFAIL_NOTIFY_DUPLIECATEDLOGIN;
    if (!response || !socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) invalid argument",
        //     L"NPSystem.cpp",
        //     0x2ce,
        //     request->m_nAccountID,
        //     errorCode)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot handle /Presence/UserLogout: invalid context (socket present %d, request present %d, response
        //     present %d), login failure code 19.", socket != nullptr, request != nullptr, response != nullptr)
        L2_LOG_ERROR(
            Log,
            "[NP] Cannot handle /Presence/UserLogout: invalid context (socket present %d, request present %d, response "
            "present %d); login failure code 19.",
            socket != nullptr, request != nullptr, response != nullptr
        );
    } else {
        constexpr const char* reply = "<Reply/>\r\n";
        socket->Send(
            "STS/1.0 200 Ok\r\ns:%dr\r\nl:%d\r\n\r\n%s", response->m_nTRID, static_cast<int>(std::strlen(reply)), reply
        );
    }
    CUserSocket::OnLoginFailed(nullptr, request->m_nAccountID, errorCode);
    return false;
}

bool CNPD::Response_ListGrades(CNPSocket* socket, STSRequestTI* request, CSTSResponse* response) {
    L2_GUARD_FUNCTION();
    if (!request || !response || !socket || response->m_nResponseCode != 200) {
        return false;
    }

    CUserSocket* userSocket = nullptr;
    {
        L2_GUARD(accountMapSect);
        const auto account = accountMap.find(request->m_nAccountID);
        if (account != accountMap.end()) {
            userSocket = account->second;
        }
    }
    if (!userSocket) {
        return false;
    }

    pugi::xml_document document;
    if (!response->ParseBody(document)) {
        return false;
    }

    const pugi::xml_node reply = document.child("Reply");
    const char* const gradeListCountValue = reply.child_value("GradeListCount");
    if (!gradeListCountValue || *gradeListCountValue == '\0') {
        return false;
    }

    const int gradeListCount = static_cast<int>(std::strtol(gradeListCountValue, nullptr, 10));
    pugi::xml_node grade = reply.child("GradeList").first_child();
    for (int index = 0; index < gradeListCount; ++index) {
        const char* const gameGradeKey = grade.child_value("GameGradeKey");
        const char* const remainingTimeValue = grade.child_value("RemainingGradeTimePerSecond");
        const char* const activation = grade.child_value("Activation");
        (void)std::strtoll(gameGradeKey, nullptr, 10);
        const int remainingTime = static_cast<int>(std::strtol(remainingTimeValue, nullptr, 10));

        wchar_t skillName[1024]{};
        ConvertGradeSkillName(gameGradeKey, skillName);
        if (CompareCaseInsensitive("true", activation) == 0) {
            const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfoByName(skillName);
            if (!skillInfo) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]NPError(a=%d, e=%d) Invalid Skill! "
                //     L"SkillName[%s]",
                //     L"NPSystem.cpp",
                //     0x393,
                //     request->m_nAccountID,
                //     CurrentLastError(),
                //     skillName)
                // Log.Add(LOG_ERROR,
                //     L"[NP] /Grade/ListGrades for account %u: skill %ls was not found. Entry skipped.",
                //     request->m_nAccountID, skillName)
                L2_LOG_ERROR(
                    Log, u"[NP] /Grade/ListGrades for account %u: skill %ls was not found; entry skipped.",
                    request->m_nAccountID, skillName
                );
            } else if (skillInfo->m_nAbnormalType == static_cast<AbnormalStatusSlotType>(-2)) {
                InfectedSkillType infectedSkill;
                infectedSkill.pSkillInfo = const_cast<CSkillInfo*>(skillInfo);
                infectedSkill.nStartTime = runtime::crt::Time32Now();
                infectedSkill.bSelfCast = true;
                infectedSkill.nSourceCreatureId = 0;
                infectedSkill.nDuration = remainingTime;
                userSocket->PushGradeSkill(infectedSkill);
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]NPError(a=%d, e=%d) Is Not NP Buff! "
                //     L"SkillName[%s]",
                //     L"NPSystem.cpp",
                //     0x38d,
                //     request->m_nAccountID,
                //     CurrentLastError(),
                //     skillName)
                // Log.Add(LOG_ERROR,
                //     L"[NP] /Grade/ListGrades for account %u: skill %ls is not an NP buff. Entry skipped.",
                //     request->m_nAccountID, skillName)
                L2_LOG_ERROR(
                    Log, u"[NP] /Grade/ListGrades for account %u: skill %ls is not an NP buff; entry skipped.",
                    request->m_nAccountID, skillName
                );
            }
        }
        grade = grade.next_sibling();
    }
    return false;
}

bool CNPD::Notify_GradeChanged(CNPSocket* socket, STSRequestTI* request, CSTSResponse* response) {
    L2_GUARD_FUNCTION();
    if (!request || !response || !socket) {
        return false;
    }

    pugi::xml_document document;
    if (!response->ParseBody(document)) {
        return false;
    }

    const pugi::xml_node message = document.child("Message");
    const char* const userGuid = message.child_value("UserId");
    if (!userGuid || *userGuid == '\0') {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) invalid response body",
        //     L"NPSystem.cpp",
        //     0x3ba,
        //     request->m_nAccountID,
        //     0xf)
        // Log.Add(LOG_ERROR,
        //     "[NP] Cannot apply /Grade/NotifyGradeChanged: Message.UserId is missing.")
        L2_LOG_ERROR(Log, "[NP] Cannot apply /Grade/NotifyGradeChanged: Message.UserId is missing.");
        return false;
    }

    const auto guid = g_npGuid2AccountMap.find(std::string(userGuid));
    if (guid == g_npGuid2AccountMap.end()) {
        return false;
    }
    const unsigned int accountId = guid->second;

    CUserSocket* userSocket = nullptr;
    {
        L2_GUARD(accountMapSect);
        const auto account = accountMap.find(accountId);
        if (account != accountMap.end()) {
            userSocket = account->second;
        }
    }
    if (!userSocket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) Invalid UserSocket!",
        //     L"NPSystem.cpp",
        //     0x3d6,
        //     accountId)
        // Log.Add(LOG_WARN,
        //     "[NP] Ignoring /Grade/NotifyGradeChanged for account %u: the user connection is no longer registered.",
        //     accountId)
        L2_LOG_WARN(
            Log, "[NP] Ignoring /Grade/NotifyGradeChanged for account %u: the user connection is no longer registered.",
            accountId
        );
        return false;
    }

    const char* const gameGradeKey = message.child_value("GameGradeKey");
    (void)message.child_value("Activation");
    const char* const remainingTimeValue = message.child_value("RemainingGradeTimePerSecond");
    const char* const reason = message.child_value("Reason");

    wchar_t skillName[1024]{};
    ConvertGradeSkillName(gameGradeKey, skillName);
    const int remainingTime = static_cast<int>(std::strtol(remainingTimeValue, nullptr, 10));
    const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfoByName(skillName);
    InfectedSkillType infectedSkill;
    if (!skillInfo) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) Invalid Skill! "
        //     L"SkillName[%s]",
        //     L"NPSystem.cpp",
        //     0x3fe,
        //     accountId,
        //     CurrentLastError(),
        //     skillName)
        // Log.Add(LOG_ERROR,
        //     L"[NP] /Grade/NotifyGradeChanged for account %u: skill %ls was not found. Update rejected.",
        //     accountId, skillName)
        L2_LOG_ERROR(
            Log, u"[NP] /Grade/NotifyGradeChanged for account %u: skill %ls was not found; update rejected.", accountId,
            skillName
        );
        return false;
    }
    if (skillInfo->m_nAbnormalType != static_cast<AbnormalStatusSlotType>(-2)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]NPError(a=%d, e=%d) Is Not NP Buff! "
        //     L"SkillName[%s]",
        //     L"NPSystem.cpp",
        //     0x3f7,
        //     accountId,
        //     CurrentLastError(),
        //     skillName)
        // Log.Add(LOG_ERROR,
        //     L"[NP] /Grade/NotifyGradeChanged for account %u: skill %ls is not an NP buff. Update rejected.",
        //     accountId, skillName)
        L2_LOG_ERROR(
            Log, u"[NP] /Grade/NotifyGradeChanged for account %u: skill %ls is not an NP buff; update rejected.", accountId,
            skillName
        );
        return false;
    }

    infectedSkill.pSkillInfo = const_cast<CSkillInfo*>(skillInfo);
    infectedSkill.nStartTime = runtime::crt::Time32Now();
    infectedSkill.bSelfCast = true;
    infectedSkill.nSourceCreatureId = 0;
    infectedSkill.nDuration = remainingTime;

    if (CompareCaseInsensitive("Activated", reason) == 0 || CompareCaseInsensitive("ActivatedWithAdding", reason) == 0 ||
        CompareCaseInsensitive("Added", reason) == 0 || CompareCaseInsensitive("Deleted", reason) == 0) {
        userSocket->PushGradeSkill(infectedSkill);
        userSocket->PushGradeSkillToUser(infectedSkill);
    } else if (CompareCaseInsensitive("DeletedAndExhausted", reason) == 0) {
        userSocket->PopGradeSkill(skillInfo);
        userSocket->PopGradeSkillFromUser(skillInfo);
    }
    return false;
}

} // namespace NPSystem
