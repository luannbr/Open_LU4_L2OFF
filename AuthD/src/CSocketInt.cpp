#include "IOServer.h"

#include "Account.h"
#include "AccountDB.h"
#include "Config.h"
#include "Log.h"
#include "PAServerMgr.h"
#include "SocketServerEx.h"
#include "Util.h"

#include <cctype>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(_WIN32)
#  include <WinSock2.h>
#else
#  include <arpa/inet.h>
#endif

namespace {

const char* getToken(char* token, const char* input);
void KickAccount(CSocketInt* socket, const char* packet);
void ChangeLimitUser(CSocketInt* socket, const char* packet);
void GetLoginUserCount(CSocketInt* socket, const char* packet);
void KickAccountByaccount(CSocketInt* socket, const char* packet);
void SetGMOnlyMode(CSocketInt* socket, const char* packet);
void GetAccountInfo(CSocketInt* socket, const char* packet);
void SetGMIP(CSocketInt* socket, const char* packet);
void SetGMAccessLimit(CSocketInt* socket, const char* packet);
void SetFreeServer(CSocketInt* socket, const char* packet);
void OCHKRequest(CSocketInt* socket, const char* packet);
void CheckPassword(CSocketInt* socket, const char* packet);
void KickAccountDirectToServer(CSocketInt* socket, const char* packet);
void DeliveryPremiumInfoNoti(CSocketInt* socket, const char* packet);
void OCHKExternalRequest(CSocketInt* socket, const char* packet);

// L2Authd.exe .data 0x00519004. The order is read directly from all fourteen
// function pointers, instead of reconstructed from handler names.
CSocketInt::PacketFunction interactivePacketTable[14] = {
    KickAccount,
    ChangeLimitUser,
    GetLoginUserCount,
    KickAccountByaccount,
    SetGMOnlyMode,
    GetAccountInfo,
    SetGMIP,
    SetGMAccessLimit,
    SetFreeServer,
    OCHKRequest,
    CheckPassword,
    KickAccountDirectToServer,
    DeliveryPremiumInfoNoti,
    OCHKExternalRequest,
};

} // namespace

// L2Authd.exe 0x0042C9F0, Src/csocketint.cpp:569-575.
CSocketInt::CSocketInt(std::uint32_t socket)
    : CIOSocket(socket) {
    m_socket = socket;
    addr.s_addr = 0;
    host = nullptr;
    mode = ISM_READ_LEN;
    packetTable = interactivePacketTable;
}

// L2Authd.exe 0x0042CA50, Src/csocketint.cpp:578-579.
CSocketInt::~CSocketInt() = default;

// L2Authd.exe 0x0042CA70, Src/csocketint.cpp:582-584.
CSocketInt* CSocketInt::Allocate(std::uint32_t socket) {
    void* const storage =
        MemoryObject::operator new(sizeof(CSocketInt));
    return storage == nullptr
        ? nullptr
        : ::new (storage) CSocketInt(socket);
}

// L2Authd.exe 0x0042CAF0, Src/csocketint.cpp:624-627.
void CSocketInt::OnCreate() {
    AddRef();
    OnRead();
}

// L2Authd.exe 0x0042CB10, Src/csocketint.cpp:592-608.
void CSocketInt::OnRead() {
    char* buffer = m_pReadBuf->m_buffer;
    int readIndex = 0;
    int remaining = static_cast<int>(m_pReadBuf->m_size);

    while (remaining > 0) {
        if (readIndex < 255 && buffer[readIndex] != '\n') {
            ++readIndex;
            --remaining;
        } else {
            buffer[readIndex] = '\0';
            m_nTimeout = 60;
            Process(buffer);
            buffer += readIndex + 1;
            readIndex = 0;
            --remaining;
        }
    }
    Read(static_cast<std::uint32_t>(readIndex));
}

// L2Authd.exe 0x0042CBC0, Src/csocketint.cpp:586-589.
void CSocketInt::OnClose() {
    mode = ISM_CLOSE;
    const std::uint32_t peerAddress = ntohl(addr.s_addr);
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "*close connection from %s, %x(%x)",
    //     IP(),
    //     m_socket,
    //     runtime::PointerLow32(this))
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=network event=socket_closed peer=admin address=%u.%u.%u.%u socket=%u reason=unspecified",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U, m_socket)
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[admin] Connection from %u.%u.%u.%u closed on socket %u. Cause not provided.",
    //     (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
    //     (peerAddress >> 8U) & 255U, peerAddress & 255U, m_socket)
    L2_LOG_INFO(Log,
        // Noise: The generic absence-of-cause phrase adds no diagnostic context.
        // "[admin] Connection from %u.%u.%u.%u closed on socket %u. Cause not provided.",
        "[admin] Connection from %u.%u.%u.%u closed on socket %u.",
        (peerAddress >> 24U) & 255U, (peerAddress >> 16U) & 255U,
        (peerAddress >> 8U) & 255U, peerAddress & 255U, m_socket);
}

// L2Authd.exe 0x0042CC00, Src/csocketint.cpp:630-632.
void CSocketInt::SetAddress(in_addr address) {
    addr = address;
}

// L2Authd.exe 0x0042CC20, Src/csocketint.cpp:635-645. On x86 va_list is a
// pointer passed by value, so the target formats twice from the same starting
// argument. Fresh va_copy instances keep that behavior on every host ABI.
#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
void CSocketInt::Send(const char* format, ...) {
    if (mode == ISM_CLOSE) {
        return;
    }

    CIOBuffer* const buffer = CIOBuffer::Alloc();
    std::va_list arguments;
    va_start(arguments, format);

    std::va_list firstArguments;
    va_copy(firstArguments, arguments);
    const int firstLength = std::vsprintf(
        buffer->m_buffer,
        format,
        firstArguments);
    va_end(firstArguments);

    int length = 0;
    if (firstLength > 0) {
        std::va_list secondArguments;
        va_copy(secondArguments, arguments);
        length = std::vsprintf(
            buffer->m_buffer,
            format,
            secondArguments);
        va_end(secondArguments);
    }
    va_end(arguments);

    buffer->m_size = static_cast<std::uint32_t>(length);
    Write(buffer);
}
#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

// L2Authd.exe 0x0042CCA0, Src/csocketint.cpp:619-621.
const char* CSocketInt::IP() {
    return ::inet_ntoa(addr);
}

// L2Authd.exe 0x0042CCC0, Src/csocketint.cpp:610-617.
void CSocketInt::Process(char* packet) {
    int packetNumber;
    std::sscanf(packet, "%d", &packetNumber);
    if (packetNumber >= 0 && packetNumber < 14) {
        packetTable[packetNumber](this, packet);
    }
}

// L2Authd.exe 0x0042CD10, Src/csocketint.cpp:648-659.
void CSocketInt::SendBuffer(const char* buffer, int length) {
    if (mode == ISM_CLOSE || length <= 0 || length > 8192) {
        return;
    }

    CIOBuffer* const ioBuffer = CIOBuffer::Alloc();
    std::memcpy(ioBuffer->m_buffer, buffer, length);
    ioBuffer->m_size = static_cast<std::uint32_t>(length);
    Write(ioBuffer);
}

// L2Authd.exe 0x0042CD80, Src/csocketint.cpp:218-239.
void OCHKRequestCallback(
    int result,
    CSocketInt* socket,
    const char* gameAccount) {
    if (result == 1) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=admin event=verification_rejected operation=ochk peer=pa socket=%u reason_code=%d reply=201",
        //     socket->m_socket, result)
        // Log.AddLog(LOG_NORMAL,
        //     "[admin] PA rejected OCHK verification on socket %u (reason %d). Reply: 201.",
        //     socket->m_socket, result)
        L2_LOG_INFO(Log,
            "[admin] PA rejected OCHK verification on socket %u (reason %d). Reply: 201.",
            socket->m_socket, result);
        socket->Send("-OCHK\t201\tError\t%s\r\n", gameAccount);
        return;
    }
    if (result != 0) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=verification_failed operation=ochk peer=pa socket=%u reason_code=%d reply=004",
        //     socket->m_socket, result)
        // Log.AddLog(LOG_WARN,
        //     "[admin] PA verification failed for OCHK on socket %u (reason %d). Reply: 004.",
        //     socket->m_socket, result)
        L2_LOG_WARN(Log,
            "[admin] PA verification failed for OCHK on socket %u (reason %d). Reply: 004.",
            socket->m_socket, result);
        socket->Send("-OCHK\t004\t\n");
        return;
    }

    CAccount accountInfo;
    result = accountInfo.LoadAccount(gameAccount);
    if (result != 0) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=verification_failed operation=ochk peer=pa phase=account_load socket=%u reason_code=%d action=send_legacy_positive_reply",
        //     socket->m_socket, result)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Account loading failed after PA verification on socket %u (reason %d). The existing path still sends a positive +OCHK reply.",
        //     socket->m_socket, result)
        // L2_LOG_WARN(Log,
        //     "[admin] Account loading failed after PA verification on socket %u (reason %d). The existing path still sends a positive +OCHK reply.",
        //     socket->m_socket, result)
        L2_LOG_WARN(Log,
            "[admin] Local account validation after PA verification returned reason %d on socket %u. The existing path still sends a positive +OCHK reply.",
            result, socket->m_socket);
    }
    const std::int32_t loginFlag =
        accountInfo.block_flag == 0 && accountInfo.block_flag2 == 0
        ? accountInfo.login_flag
        : static_cast<std::int32_t>(
              static_cast<std::uint32_t>(accountInfo.login_flag) |
              UINT32_C(0x80000000));
    socket->Send(
        "+OCHK\t%d\t%s\t%d\r\n",
        loginFlag,
        gameAccount,
        accountInfo.uid);
}

namespace {

// L2Authd.exe 0x0042CE60, Src/csocketint.cpp:30-43.
const char* getToken(char* token, const char* input) {
    int index = 0;
    while (true) {
        if (index > 254) {
            *token = '\0';
            return nullptr;
        }

        *token = *input++;
        if (*token == '\0') {
            return nullptr;
        }
        if (*token == '\t' || *token == '\r') {
            *token = '\0';
            return input;
        }
        ++token;
        ++index;
    }
}

// L2Authd.exe 0x0042CEE0, Src/csocketint.cpp:46-73.
void KickAccount(CSocketInt* socket, const char* packet) {
    int uid = 0;
    char name[256];
    packet = getToken(name, packet);
    if (packet == nullptr) {
        socket->Send("0");
        // Log.AddLog(LOG_DEBUG, "KickAccount() has been failed..")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=admin event=command_rejected operation=kick_account socket=%u reason=invalid_format reply=0",
        //     socket->m_socket)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[admin] Invalid kick command on socket %u. Reply: 0.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Invalid kick command on socket %u. Reply: 0.",
            socket->m_socket);
        return;
    }

    packet = getToken(name, packet);
    const int length = static_cast<int>(std::strlen(name));
    for (int index = 0; index < length; ++index) {
        if (std::isdigit(
                static_cast<unsigned char>(name[index])) == 0) {
            socket->Send("0");
            // Log.AddLog(
            //     LOG_DEBUG,
            //     "KickAccount(%s) has been failed..",
            //     name)
            // Log.AddLog(
            //     LOG_WARN,
            //     "component=admin event=command_rejected operation=kick_account socket=%u field=account_id reason=non_numeric reply=0",
            //     socket->m_socket)
            // Log.AddLog(
            //     LOG_WARN,
            //     "[admin] Invalid kick command on socket %u: account ID is not numeric. Reply: 0.",
            //     socket->m_socket)
            L2_LOG_WARN(Log,
                "[admin] Invalid kick command on socket %u: account ID is not numeric. Reply: 0.",
                socket->m_socket);
            return;
        }
    }

    uid = std::atoi(name);
    // accountdb.KickAccount(uid, 11, true, true)
    const bool kickProcessed = accountdb.KickAccount(uid, 11, true, true);
    socket->Send("1");
    // Log.AddLog(
    //     LOG_DEBUG,
    //     "%d has been kicked in KickAccount()",
    //     uid)
    // Log.AddLog(
    //     kickProcessed ? LOG_NORMAL : LOG_WARN,
    //     "component=admin event=kick_processed operation=kick_account socket=%u account_id=%d result=%s reply=1 delivery=unverified",
    //     socket->m_socket, uid, kickProcessed ? "accepted" : "account_not_found")
    // Log.AddLog(
    //     kickProcessed ? LOG_NORMAL : LOG_WARN,
    //     "[admin] Kick requested on socket %u for account %d: %s. Reply: 1. Disconnection is not confirmed.",
    //     socket->m_socket, uid, kickProcessed ? "accepted" : "account not found")
    L2_LOG(Log, kickProcessed ? LOG_NORMAL : LOG_WARN,
        "[admin] Kick requested on socket %u for account %d: %s. Reply: 1; disconnection is not confirmed.",
        socket->m_socket, uid, kickProcessed ? "accepted" : "account not found");
}

// L2Authd.exe 0x0042D050, Src/csocketint.cpp:77-80.
void GetLoginUserCount(
    CSocketInt* socket,
    [[maybe_unused]] const char* packet) {
    socket->Send("%d", accountdb.GetUserNum());
}

// L2Authd.exe 0x0042D080, Src/csocketint.cpp:83-113.
void KickAccountByaccount(CSocketInt* socket, const char* packet) {
    [[maybe_unused]] int uid = 0;
    char name[256];
    std::memset(name, 0, 32);

    packet = getToken(name, packet);
    if (packet == nullptr) {
        socket->SendBuffer("0,incorrect format", 19);
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "incorrect format in KickAccountByaccount()")
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=admin event=command_rejected operation=kick_account_by_name socket=%u reason=invalid_format reply=0",
        //     socket->m_socket)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[admin] Invalid kick-by-name command on socket %u. Reply: 0.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Invalid kick-by-name command on socket %u. Reply: 0.",
            socket->m_socket);
        return;
    }

    packet = getToken(name, packet);
    StdAccount(name);
    if (!CheckAccount(name)) {
        socket->Send("0,InvalidAccount");
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "Invalid Account(%s) in KickAccountByaccount()",
        //     name)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=admin event=command_rejected operation=kick_account_by_name socket=%u field=account reason=invalid_value reply=0",
        //     socket->m_socket)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[admin] Invalid account name in kick command on socket %u. Reply: 0.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Invalid account name in kick command on socket %u. Reply: 0.",
            socket->m_socket);
        return;
    }

    if (accountdb.KickAccountByAccount(name, 11, true)) {
        socket->Send("1");
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "%s has been kicked in KickAccountByAccount()",
        //     name)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "component=admin event=kick_processed operation=kick_account_by_name socket=%u account=\"%.14s\" result=accepted reply=1 delivery=unverified",
        //     socket->m_socket, name)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "[admin] Kick requested on socket %u for account \"%.14s\" was accepted. Reply: 1. Disconnection is not confirmed.",
        //     socket->m_socket, name)
        L2_LOG_INFO(Log,
            "[admin] Kick requested on socket %u for account \"%.14s\" was accepted. Reply: 1; disconnection is not confirmed.",
            socket->m_socket, name);
    } else {
        socket->Send("0,InvalidAccount");
        // Log.AddLog(
        //     LOG_DEBUG,
        //     "Kicking %s has been failed in KickAccountByAccount()",
        //     name)
        // Log.AddLog(
        //     LOG_WARN,
        //     "component=admin event=kick_processed operation=kick_account_by_name socket=%u account=\"%.14s\" result=failure reason=account_lookup_or_kick_failed reply=0",
        //     socket->m_socket, name)
        // Log.AddLog(
        //     LOG_WARN,
        //     "[admin] Could not kick account on socket %u (name \"%.14s\"): account lookup or kick failed. Reply: 0.",
        //     socket->m_socket, name)
        L2_LOG_WARN(Log,
            "[admin] Could not kick account on socket %u (name \"%.14s\"): account lookup or kick failed. Reply: 0.",
            socket->m_socket, name);
    }
}

// L2Authd.exe 0x0042D1F0, Src/csocketint.cpp:116-139.
void SetGMOnlyMode(CSocketInt* socket, const char* packet) {
    char buffer[256];
    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=set_gm_only socket=%u reason=invalid_format reply=0",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Invalid GM-only mode command on socket %u. Reply: 0.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Invalid GM-only mode command on socket %u. Reply: 0.",
            socket->m_socket);
        socket->Send("0,Incorrect Format");
        return;
    }

    std::memset(buffer, 0, 32);
    packet = getToken(buffer, packet);
    if (buffer[0] == '0') {
        config.GMCheckMode = false;
        // Log.AddLog(LOG_WARN, "GM ONLY MODE OFF")
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "component=config event=setting_changed source=admin socket=%u option=GMCheckMode value=false reply=1",
        //     socket->m_socket)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "[admin] Socket %u disabled GMCheckMode. Reply: 1.",
        //     socket->m_socket)
        L2_LOG_INFO(Log,
            "[admin] Socket %u disabled GMCheckMode. Reply: 1.",
            socket->m_socket);
        socket->Send("1");
    } else {
        config.GMCheckMode = true;
        // Log.AddLog(LOG_WARN, "GM ONLY MODE ON")
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "component=config event=setting_changed source=admin socket=%u option=GMCheckMode value=true reply=1",
        //     socket->m_socket)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "[admin] Socket %u enabled GMCheckMode. Reply: 1.",
        //     socket->m_socket)
        L2_LOG_INFO(Log,
            "[admin] Socket %u enabled GMCheckMode. Reply: 1.",
            socket->m_socket);
        socket->Send("1");
    }
}

// L2Authd.exe 0x0042D2E0, Src/csocketint.cpp:142-157.
void ChangeLimitUser(CSocketInt* socket, const char* packet) {
    char buffer[256];
    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=set_socket_limit socket=%u reason=invalid_format action=ignore_without_reply",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Invalid socket limit command on socket %u. Ignoring the command without replying.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Invalid socket limit command on socket %u. Ignoring the command without replying.",
            socket->m_socket);
        return;
    }

    getToken(buffer, packet);
    const int limitUser = std::atoi(buffer);
    if (limitUser > 0 && limitUser < 12000) {
        config.SocketLimit = limitUser;
        // Log.AddLog(
        //     LOG_WARN,
        //     "Change socketlimit, %d",
        //     config.SocketLimit)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "component=config event=setting_changed source=admin socket=%u option=SocketLimit value=%d reply=1",
        //     socket->m_socket, config.SocketLimit)
        // Log.AddLog(
        //     LOG_NORMAL,
        //     "[admin] Socket %u changed SocketLimit to %d. Reply: 1.",
        //     socket->m_socket, config.SocketLimit)
        L2_LOG_INFO(Log,
            "[admin] Socket %u changed SocketLimit to %d. Reply: 1.",
            socket->m_socket, config.SocketLimit);
        socket->Send("1");
    } else {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=set_socket_limit socket=%u reason=outside_range requested_value=%d min_value=1 max_value=11999 action=ignore_without_reply",
        //     socket->m_socket, limitUser)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Socket limit rejected on socket %u: requested %d, allowed range 1..11999. Ignoring the command without replying.",
        //     socket->m_socket, limitUser)
        L2_LOG_WARN(Log,
            "[admin] Socket limit rejected on socket %u: requested %d, allowed range 1..11999. Ignoring the command without replying.",
            socket->m_socket, limitUser);
    }
}

// These two opcode slots are genuine five-byte push/ret bodies in the
// selected EXE, not portable placeholders.
// L2Authd.exe 0x0042D3A0, Src/csocketint.cpp:160-161.
void GetAccountInfo(
    [[maybe_unused]] CSocketInt* socket,
    [[maybe_unused]] const char* packet) {}

// L2Authd.exe 0x0042D3B0, Src/csocketint.cpp:164-185.
void SetGMIP(CSocketInt* socket, const char* packet) {
    char buffer[256];
    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=set_gm_ip socket=%u reason=invalid_format reply=0",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Invalid GM IP command on socket %u. Reply: 0.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Invalid GM IP command on socket %u. Reply: 0.",
            socket->m_socket);
        socket->Send("0");
        return;
    }

    getToken(buffer, packet);
    buffer[16] = '\0';
    in_addr ip{};
    ip.s_addr = ::inet_addr(buffer);
    if (ip.s_addr == INADDR_NONE) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=set_gm_ip socket=%u reason=invalid_ipv4 reply=0",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] GM IP command on socket %u contains an invalid IPv4 address. Reply: 0.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] GM IP command on socket %u contains an invalid IPv4 address. Reply: 0.",
            socket->m_socket);
        socket->Send("0");
        return;
    }

    config.GMIP = ip;
    const auto* const octets =
        reinterpret_cast<const unsigned char*>(&ip.s_addr);
    // Log.AddLog(
    //     LOG_WARN,
    //     "Change GMIP, %d.%d.%d.%d",
    //     octets[0],
    //     octets[1],
    //     octets[2],
    //     octets[3])
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=config event=setting_changed source=admin socket=%u option=GMIP value=%u.%u.%u.%u reply=1",
    //     socket->m_socket, static_cast<unsigned int>(octets[0]),
    //     static_cast<unsigned int>(octets[1]), static_cast<unsigned int>(octets[2]),
    //     static_cast<unsigned int>(octets[3]))
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[admin] Socket %u changed GMIP to %u.%u.%u.%u. Reply: 1.",
    //     socket->m_socket, static_cast<unsigned int>(octets[0]),
    //     static_cast<unsigned int>(octets[1]), static_cast<unsigned int>(octets[2]),
    //     static_cast<unsigned int>(octets[3]))
    L2_LOG_INFO(Log,
        "[admin] Socket %u changed GMIP to %u.%u.%u.%u. Reply: 1.",
        socket->m_socket, static_cast<unsigned int>(octets[0]),
        static_cast<unsigned int>(octets[1]), static_cast<unsigned int>(octets[2]),
        static_cast<unsigned int>(octets[3]));
    socket->Send("1");
}

// L2Authd.exe 0x0042D4D0, Src/csocketint.cpp:189-191.
// void SetGMAccessLimit(
//     [[maybe_unused]] CSocketInt* socket,
//     [[maybe_unused]] const char* packet) {}
void SetGMAccessLimit(
    CSocketInt* socket,
    [[maybe_unused]] const char* packet) {
    L2_LOG_WARN(Log,
        "[admin] Ignored SetGMAccessLimit on socket %u: this command has no action. Settings are unchanged; no reply is sent.",
        socket->m_socket);
}

// L2Authd.exe 0x0042D4E0, Src/csocketint.cpp:194-215.
void SetFreeServer(CSocketInt* socket, const char* packet) {
    char buffer[256];
    packet = getToken(buffer, packet);
    if (packet != nullptr) {
        packet = getToken(buffer, packet);
        if (buffer[0] == '0') {
            config.FreeServer = 1;
            // Log.AddLog(
            //     LOG_WARN,
            //     "Config FreeServer is chaged to false")
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "component=config event=setting_changed source=admin socket=%u option=FreeServer value=1 reply=1",
            //     socket->m_socket)
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "[admin] Socket %u changed FreeServer to 1. Reply: 1.",
            //     socket->m_socket)
            L2_LOG_INFO(Log,
                "[admin] Socket %u changed FreeServer to 1. Reply: 1.",
                socket->m_socket);
            socket->Send("1");
            return;
        }
        if (buffer[0] == '1') {
            config.FreeServer = 2;
            // Log.AddLog(
            //     LOG_WARN,
            //     "Config FreeServer is changed to true")
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "component=config event=setting_changed source=admin socket=%u option=FreeServer value=2 reply=1",
            //     socket->m_socket)
            // Log.AddLog(
            //     LOG_NORMAL,
            //     "[admin] Socket %u changed FreeServer to 2. Reply: 1.",
            //     socket->m_socket)
            L2_LOG_INFO(Log,
                "[admin] Socket %u changed FreeServer to 2. Reply: 1.",
                socket->m_socket);
            socket->Send("1");
            return;
        }
    }
    // Log.AddLog(LOG_WARN,
    //     "component=admin event=command_rejected operation=set_free_server socket=%u reason=invalid_format_or_value reply=0",
    //     socket->m_socket)
    // Log.AddLog(LOG_WARN,
    //     "[admin] Invalid free-server mode command or value on socket %u. Reply: 0.",
    //     socket->m_socket)
    L2_LOG_WARN(Log,
        "[admin] Invalid free-server mode command or value on socket %u. Reply: 0.",
        socket->m_socket);
    socket->Send("0");
}

// L2Authd.exe 0x0042D5E0, Src/csocketint.cpp:242-310.
void OCHKRequest(CSocketInt* socket, const char* packet) {
    // PDB stack offsets prove this exact adjacency. The target copies 33 bytes
    // from the 20-byte password field into PreLoginUser::playPassword, so the
    // first 13 account bytes are part of the observed legacy result.
    struct RequestFields {
        char clientKey[64];
        char password[20];
        char account[16];
    } fields;

    char buffer[256];
    std::memset(buffer, 0, sizeof(buffer));
    std::memset(fields.account, 0, sizeof(fields.account));
    std::memset(fields.password, 0, sizeof(fields.password));
    std::memset(fields.clientKey, 0, sizeof(fields.clientKey));

    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=ochk socket=%u reason=invalid_arguments field=command reply=004",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Invalid OCHK command on socket %u. Reply: 004.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Invalid OCHK command on socket %u. Reply: 004.",
            socket->m_socket);
        socket->Send("-OCHK\t004\t\n");
        return;
    }

    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=ochk socket=%u reason=invalid_arguments field=account reply=003",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] OCHK command on socket %u has an invalid account argument. Reply: 003.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] OCHK command on socket %u has an invalid account argument. Reply: 003.",
            socket->m_socket);
        socket->Send("-OCHK\t003\tError\r\n");
        return;
    }
    std::strncpy(fields.account, buffer, 16);
    fields.account[15] = '\0';

    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=ochk socket=%u reason=invalid_arguments field=password reply=003",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] OCHK command on socket %u has an invalid password argument. Reply: 003.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] OCHK command on socket %u has an invalid password argument. Reply: 003.",
            socket->m_socket);
        socket->Send("-OCHK\t003\tError\r\n");
        return;
    }
    std::strncpy(fields.password, buffer, 20);
    fields.password[19] = '\0';

    getToken(buffer, packet);
    std::strncpy(fields.clientKey, buffer, 63);
    fields.clientKey[63] = '\0';

    if (config.usePAServer) {
        PreLoginUser user;
        std::strncpy(
            user.playAccount,
            fields.account,
            sizeof(user.playAccount));
        std::memcpy(
            user.playPassword,
            reinterpret_cast<const unsigned char*>(&fields) +
                offsetof(RequestFields, password),
            sizeof(user.playPassword));
        const char result = PAServerMgr::Inst()->LoginAccount(
            1,
            nullptr,
            socket,
            &user);
        if (result != 0) {
            // Log.AddLog(LOG_WARN,
            //     "component=admin event=verification_failed operation=ochk peer=pa socket=%u reason_code=%d reply=004",
            //     socket->m_socket, static_cast<int>(result))
            // Log.AddLog(LOG_WARN,
            //     "[admin] Could not submit OCHK verification to PA on socket %u (reason %d). Reply: 004.",
            //     socket->m_socket, static_cast<int>(result))
            L2_LOG_WARN(Log,
                "[admin] Could not submit OCHK verification to PA on socket %u (reason %d). Reply: 004.",
                socket->m_socket, static_cast<int>(result));
            socket->Send("-OCHK\t004\t\n");
        }
        return;
    }

    CAccount accountInfo;
    char result = accountInfo.CheckPassword(
        fields.account,
        fields.password,
        config.newEncrypt);
    if (result == 0) {
        result = accountInfo.LoadAccount(fields.account);
    }
    if (result == 4) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=admin event=verification_rejected operation=ochk peer=local socket=%u reason_code=4 reply=201",
        //     socket->m_socket)
        // Log.AddLog(LOG_NORMAL,
        //     "[admin] Local OCHK verification rejected on socket %u (reason 4). Reply: 201.",
        //     socket->m_socket)
        L2_LOG_INFO(Log,
            "[admin] Local OCHK verification rejected on socket %u (reason 4). Reply: 201.",
            socket->m_socket);
        socket->Send(
            "-OCHK\t201\tError\t%s\r\n",
            fields.clientKey);
        return;
    }
    if (result == 3) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=admin event=verification_rejected operation=ochk peer=local socket=%u reason_code=3 reply=202",
        //     socket->m_socket)
        // Log.AddLog(LOG_NORMAL,
        //     "[admin] Local OCHK verification rejected on socket %u (reason 3). Reply: 202.",
        //     socket->m_socket)
        L2_LOG_INFO(Log,
            "[admin] Local OCHK verification rejected on socket %u (reason 3). Reply: 202.",
            socket->m_socket);
        socket->Send(
            "-OCHK\t202\tError\t%s\r\n",
            fields.clientKey);
        return;
    }

    if (result != 0) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=verification_failed operation=ochk peer=local socket=%u reason_code=%d action=send_legacy_positive_reply",
        //     socket->m_socket, static_cast<int>(result))
        // Log.AddLog(LOG_WARN,
        //     "[admin] Local OCHK verification failed on socket %u (reason %d). The existing path still sends a positive +OCHK reply.",
        //     socket->m_socket, static_cast<int>(result))
        L2_LOG_WARN(Log,
            "[admin] Local OCHK verification failed on socket %u (reason %d). The existing path still sends a positive +OCHK reply.",
            socket->m_socket, static_cast<int>(result));
    }
    const std::int32_t loginFlag =
        accountInfo.block_flag == 0 && accountInfo.block_flag2 == 0
        ? accountInfo.login_flag
        : static_cast<std::int32_t>(
              static_cast<std::uint32_t>(accountInfo.login_flag) |
              UINT32_C(0x80000000));
    socket->Send(
        "+OCHK\t%d\t%s\t%d\r\n",
        loginFlag,
        fields.clientKey,
        accountInfo.uid);
}

// L2Authd.exe 0x0042D930, Src/csocketint.cpp:313-362.
void CheckPassword(CSocketInt* socket, const char* packet) {
    char account[15];
    char password[17];
    char buffer[256];
    std::memset(account, 0, sizeof(account));
    std::memset(password, 0, sizeof(password));

    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=check_rejected operation=admin_check_password socket=%u field=command reason=invalid_format reply=1",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Invalid password-check command on socket %u. Reply: 1.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Invalid password-check command on socket %u. Reply: 1.",
            socket->m_socket);
        socket->Send("%d", 1);
        return;
    }

    buffer[0] = '\0';
    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=check_rejected operation=admin_check_password socket=%u field=account reason=invalid_format reply=1",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Password check on socket %u has an invalid account field format. Reply: 1.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Password check on socket %u has an invalid account field format. Reply: 1.",
            socket->m_socket);
        socket->Send("%d", 1);
        return;
    }
    std::strncpy(account, buffer, 14);
    account[14] = '\0';
    if (!CheckAccount(account)) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=check_rejected operation=admin_check_password socket=%u field=account reason=invalid_value reply=2",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Password check on socket %u has an invalid account name. Reply: 2.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Password check on socket %u has an invalid account name. Reply: 2.",
            socket->m_socket);
        socket->Send("%d", 2);
        return;
    }

    buffer[0] = '\0';
    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=authentication event=check_rejected operation=admin_check_password socket=%u field=password reason=invalid_format reply=1",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Password check on socket %u has an invalid password field format. Reply: 1.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Password check on socket %u has an invalid password field format. Reply: 1.",
            socket->m_socket);
        socket->Send("%d", 1);
        return;
    }
    buffer[16] = '\0';
    std::strncpy(password, buffer, 16);

    CAccount accountInfo;
    char result = accountInfo.CheckPassword(
        account,
        password,
        config.newEncrypt);
    if (result == 0) {
        result = accountInfo.LoadAccount(account);
    }
    if (result != 0) {
        // Log.AddLog(LOG_NORMAL,
        //     "component=authentication event=check_failed operation=admin_check_password socket=%u reason_code=%d reply=3",
        //     socket->m_socket, static_cast<int>(result))
        // Log.AddLog(LOG_NORMAL,
        //     "[admin] Password check failed on socket %u (reason %d). Reply: 3.",
        //     socket->m_socket, static_cast<int>(result))
        L2_LOG_INFO(Log,
            "[admin] Password check failed on socket %u (reason %d). Reply: 3.",
            socket->m_socket, static_cast<int>(result));
    }
    socket->Send("%d", result == 0 ? 0 : 3);
}

// L2Authd.exe 0x0042DBA0, Src/csocketint.cpp:365-416.
void KickAccountDirectToServer(
    CSocketInt* socket,
    const char* packet) {
    int serverId = 0;
    CAccount accountInfo;
    [[maybe_unused]] char errorMessage = 0;
    char buffer[256];
    char account[15];

    // Noise: Raw command text is unnecessary. Parsed outcome records below identify the operation.
    // Log.AddLog(LOG_DEBUG, "%s", packet)
    std::memset(account, 0, sizeof(account));
    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=kick_on_world socket=%u reason=invalid_format reply=1",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Invalid world kick command on socket %u. Reply: 1.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Invalid world kick command on socket %u. Reply: 1.",
            socket->m_socket);
        socket->Send("%d", 1);
        return;
    }

    buffer[0] = '\0';
    packet = getToken(buffer, packet);
    std::strncpy(account, buffer, 14);
    account[14] = '\0';
    if (!CheckAccount(account)) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=kick_on_world socket=%u reason=invalid_account reply=2",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] World kick command on socket %u has an invalid account name. Reply: 2.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] World kick command on socket %u has an invalid account name. Reply: 2.",
            socket->m_socket);
        socket->Send("%d", 2);
        return;
    }

    const char result = accountInfo.Load(account);
    if (result != 0) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=kick_on_world socket=%u reason=account_load_failed reply=byte_2",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Could not load the account for a world kick on socket %u. Reply: raw byte 0x02.",
        //     socket->m_socket)
        // L2_LOG_WARN(Log,
        //     "[admin] Could not load the account for a world kick on socket %u. Reply: raw byte 0x02.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Account lookup or access validation for a world kick returned reason %d on socket %u. Reply: raw byte 0x02.",
            static_cast<int>(result), socket->m_socket);
        socket->Send("%c", 2);
        return;
    }

    buffer[0] = '\0';
    packet = getToken(buffer, packet);
    buffer[255] = '\0';
    if (!IsValidNumeric(
            buffer,
            static_cast<int>(std::strlen(buffer)))) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=kick_on_world socket=%u reason=non_numeric_server_id reply=4",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] World kick command on socket %u has a non-numeric server ID. Reply: 4.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] World kick command on socket %u has a non-numeric server ID. Reply: 4.",
            socket->m_socket);
        socket->Send("%d", 4);
        return;
    }
    serverId = std::atoi(buffer);
    packet = getToken(buffer, packet);

    if (!g_ServerList.ExistId(serverId)) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=kick_on_world socket=%u reason=server_not_registered reply=4",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] World kick command on socket %u names an unregistered server. Reply: 4.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] World kick command on socket %u names an unregistered server. Reply: 4.",
            socket->m_socket);
        socket->Send("%d", 4);
        return;
    }
    const in_addr serverAddress =
        g_ServerList.GetAt(serverId).inner_addr;
    if (!server.GetServerStatus(serverAddress)) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=kick_on_world socket=%u reason=world_connection_missing reply=5 server_id=%d",
        //     socket->m_socket, serverId)
        // Log.AddLog(LOG_WARN,
        //     "[admin] World kick command on socket %u cannot reach server %d: no world connection. Reply: 5.",
        //     socket->m_socket, serverId)
        L2_LOG_WARN(Log,
            "[admin] World kick command on socket %u cannot reach server %d: no world connection. Reply: 5.",
            socket->m_socket, serverId);
        socket->Send("%d", 5);
        return;
    }

    // SendSocket(
    //     serverAddress,
    //     "cdcs",
    //     1,
    //     accountInfo.uid,
    //     7,
    //     account)
    const int forwarded = SendSocket(
        serverAddress,
        "cdcs",
        1,
        accountInfo.uid,
        7,
        account);
    // Log.AddLog(forwarded == 1 ? LOG_NORMAL : LOG_WARN,
    //     "component=admin event=kick_forward_result operation=kick_on_world socket=%u account_id=%d server_id=%d send_result=%d reply=0 delivery=unverified",
    //     socket->m_socket, accountInfo.uid, serverId, forwarded)
    // Log.AddLog(forwarded == 1 ? LOG_NORMAL : LOG_WARN,
    //     "[admin] World kick requested on socket %u for account %d, server %d (send result %d). Reply: 0. Delivery is not confirmed.",
    //     socket->m_socket, accountInfo.uid, serverId, forwarded)
    // L2_LOG(Log, forwarded == 1 ? LOG_NORMAL : LOG_WARN,
    //     "[admin] World kick requested on socket %u for account %d, server %d (send result %d). Reply: 0. Delivery is not confirmed.",
    //     socket->m_socket, accountInfo.uid, serverId, forwarded)
    L2_LOG(Log, forwarded == 1 ? LOG_NORMAL : LOG_WARN,
        "[admin] World kick requested on socket %u for account %d, server %d (send result %d). Responding with code 0.",
        socket->m_socket, accountInfo.uid, serverId, forwarded);
    socket->Send("%d", 0);
}

// L2Authd.exe 0x0042DED0, Src/csocketint.cpp:420-463.
void DeliveryPremiumInfoNoti(
    CSocketInt* socket,
    const char* packet) {
    int premiumLevel = 0;
    CAccount accountInfo;
    char buffer[256];
    char gameAccount[15];
    std::memset(buffer, 0, sizeof(buffer));
    std::memset(gameAccount, 0, sizeof(gameAccount));

    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=notify_premium_level socket=%u reason=invalid_format reply=1",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Invalid premium level notification on socket %u. Reply: 1.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Invalid premium level notification on socket %u. Reply: 1.",
            socket->m_socket);
        socket->Send("%d", 1);
        return;
    }

    buffer[0] = '\0';
    packet = getToken(buffer, packet);
    std::strncpy(gameAccount, buffer, 15);
    packet = getToken(buffer, packet);
    if (!IsValidNumeric(
            buffer,
            static_cast<int>(std::strlen(buffer)))) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=notify_premium_level socket=%u reason=non_numeric_level reply=1",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Premium level notification on socket %u has a non-numeric level. Reply: 1.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Premium level notification on socket %u has a non-numeric level. Reply: 1.",
            socket->m_socket);
        socket->Send("%d", 1);
        return;
    }
    premiumLevel = std::atoi(buffer);

    if (!CheckAccount(gameAccount)) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=notify_premium_level socket=%u reason=invalid_account reply=2",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Premium level notification on socket %u has an invalid account name. Reply: 2.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Premium level notification on socket %u has an invalid account name. Reply: 2.",
            socket->m_socket);
        socket->Send("%d", 2);
        return;
    }
    const char accountResult = accountInfo.Load(gameAccount);
    if (accountResult != 0) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=notify_premium_level socket=%u reason=account_load_failed reply=2",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Could not load the account for a premium level notification on socket %u. Reply: 2.",
        //     socket->m_socket)
        // L2_LOG_WARN(Log,
        //     "[admin] Could not load the account for a premium level notification on socket %u. Reply: 2.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Account lookup or access validation for a premium level notification returned reason %d on socket %u. Reply: 2.",
            static_cast<int>(accountResult), socket->m_socket);
        socket->Send("%d", 2);
        return;
    }
    if (!accountdb.UpdateUserLevel(
            accountInfo.uid,
            premiumLevel)) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=notify_premium_level socket=%u reason=update_not_completed reply=2",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Premium level update was not completed for the request on socket %u. Reply: 2.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Premium level update was not completed for the request on socket %u. Reply: 2.",
            socket->m_socket);
        socket->Send("%d", 2);
        return;
    }
    // Log.AddLog(LOG_NORMAL,
    //     "component=admin event=premium_level_forwarded socket=%u account_id=%d requested_level=%d reply=0 delivery=unverified",
    //     socket->m_socket, accountInfo.uid, premiumLevel)
    // Log.AddLog(LOG_NORMAL,
    //     "[admin] Premium level update accepted on socket %u for account %d, requested level %d. Reply: 0. Delivery is not confirmed.",
    //     socket->m_socket, accountInfo.uid, premiumLevel)
    // L2_LOG_INFO(Log,
    //     "[admin] Premium level update accepted on socket %u for account %d, requested level %d. Reply: 0. Delivery is not confirmed.",
    //     socket->m_socket, accountInfo.uid, premiumLevel)
    L2_LOG_INFO(Log,
        "[admin] Premium level update accepted on socket %u for account %d, requested level %d. Responding with code 0.",
        socket->m_socket, accountInfo.uid, premiumLevel);
    socket->Send("%d", 0);
}

// L2Authd.exe 0x0042E0F0, Src/csocketint.cpp:467-546.
void OCHKExternalRequest(CSocketInt* socket, const char* packet) {
    // PDB offsets: password=-72, ipaddress=-52, account=-36, company=-20.
    // The target's 33-byte memcpy therefore appends the first 13 IP bytes to
    // the 20-byte password. A standard-layout carrier reproduces those bytes
    // without an out-of-bounds read in portable C++.
    struct ExternalRequestFields {
        char password[20];
        char ipAddress[16];
        char account[16];
        char company[16];
    } fields;

    char buffer[256];
    std::memset(buffer, 0, sizeof(buffer));
    std::memset(fields.account, 0, sizeof(fields.account));
    std::memset(fields.password, 0, sizeof(fields.password));
    std::memset(fields.company, 0, sizeof(fields.company));
    std::memset(fields.ipAddress, 0, sizeof(fields.ipAddress));

    if (!config.usePAServer) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=ochk_external socket=%u reason=pa_disabled reply=009",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] External OCHK verification rejected on socket %u: PA is disabled. Reply: 009.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] External OCHK verification rejected on socket %u: PA is disabled. Reply: 009.",
            socket->m_socket);
        socket->Send("-OCHK\t009\t\n");
        return;
    }

    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=ochk_external socket=%u reason=invalid_arguments field=command reply=004",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] Invalid external OCHK command on socket %u. Reply: 004.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] Invalid external OCHK command on socket %u. Reply: 004.",
            socket->m_socket);
        socket->Send("-OCHK\t004\t\n");
        return;
    }
    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=ochk_external socket=%u reason=invalid_arguments field=company reply=003",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] External OCHK command on socket %u has an invalid company argument. Reply: 003.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] External OCHK command on socket %u has an invalid company argument. Reply: 003.",
            socket->m_socket);
        socket->Send("-OCHK\t003\tError\n");
        return;
    }
    std::strncpy(fields.company, buffer, 15);
    fields.company[15] = '\0';

    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=ochk_external socket=%u reason=invalid_arguments field=account reply=003",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] External OCHK command on socket %u has an invalid account argument. Reply: 003.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] External OCHK command on socket %u has an invalid account argument. Reply: 003.",
            socket->m_socket);
        socket->Send("-OCHK\t003\tError\n");
        return;
    }
    std::strncpy(fields.account, buffer, 16);
    fields.account[15] = '\0';

    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=ochk_external socket=%u reason=invalid_arguments field=password reply=003",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] External OCHK command on socket %u has an invalid password argument. Reply: 003.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] External OCHK command on socket %u has an invalid password argument. Reply: 003.",
            socket->m_socket);
        socket->Send("-OCHK\t003\tError\n");
        return;
    }
    std::strncpy(fields.password, buffer, 19);
    fields.password[19] = '\0';

    packet = getToken(buffer, packet);
    if (packet == nullptr) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=command_rejected operation=ochk_external socket=%u reason=invalid_arguments field=ip_address reply=003",
        //     socket->m_socket)
        // Log.AddLog(LOG_WARN,
        //     "[admin] External OCHK command on socket %u has an invalid IP address argument. Reply: 003.",
        //     socket->m_socket)
        L2_LOG_WARN(Log,
            "[admin] External OCHK command on socket %u has an invalid IP address argument. Reply: 003.",
            socket->m_socket);
        socket->Send("-OCHK\t003\tError\n");
        return;
    }
    std::strncpy(fields.ipAddress, buffer, 15);
    fields.ipAddress[15] = '\0';

    PreLoginUser user;
    unsigned char externalToken[1024];
    std::memset(externalToken, 0, sizeof(externalToken));
    std::strncpy(
        reinterpret_cast<char*>(externalToken),
        fields.account,
        65);
    std::memcpy(
        externalToken + 32,
        reinterpret_cast<const unsigned char*>(&fields) +
            offsetof(ExternalRequestFields, password),
        33);

    const int companyNumber = fields.company[0] == '\0'
        ? 0
        : std::atoi(fields.company);
    const int gameId = config.gameId == 8 ? 32 : 27;
    const int affiliatedGameId = config.affiliatedGameId;
    in_addr clientIp{};
    clientIp.s_addr = ::inet_addr(fields.ipAddress);

    // This full-object clear occurs after construction at 0x0042E45C and is
    // part of the shipping function's state contract.
    std::memset(static_cast<void*>(&user), 0, sizeof(user));
    user.externalUser.companyNo = companyNumber;
    user.externalUser.externalToken[0] = '\0';
    std::memcpy(
        user.externalUser.externalToken + 1,
        externalToken,
        1023);
    std::memcpy(
        user.externalUser.externalToken + 65,
        &clientIp,
        sizeof(clientIp));
    user.externalUser.affiliatedGameId = affiliatedGameId;
    user.externalUser.gameId = gameId;

    const char result = PAServerMgr::Inst()->LoginExternalToken(
        1,
        nullptr,
        socket,
        &user);
    if (result != 0) {
        // Log.AddLog(LOG_WARN,
        //     "component=admin event=verification_failed operation=ochk_external peer=pa socket=%u reason_code=%d reply=004",
        //     socket->m_socket, static_cast<int>(result))
        // Log.AddLog(LOG_WARN,
        //     "[admin] Could not submit external OCHK verification to PA on socket %u (reason %d). Reply: 004.",
        //     socket->m_socket, static_cast<int>(result))
        L2_LOG_WARN(Log,
            "[admin] Could not submit external OCHK verification to PA on socket %u (reason %d). Reply: 004.",
            socket->m_socket, static_cast<int>(result));
        socket->Send("-OCHK\t004\t\n");
    }
}

} // namespace
