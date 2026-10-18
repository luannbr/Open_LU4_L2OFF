#include "MailReport.h"

#include "FileLog.h"
#include "Platform/MailReportSocket.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/SystemMailTransport.h"
#include "Shared/src/Runtime/WindowsAnsi.h"
#include "Shared/src/TextRuntime.h"
#include "Base64.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>

const char* REPLY_SERVICE_READY = "220";
const char* REPLY_MAIL_ACTION_OK = "250";
const char* REPLY_ENTER_EMAIL = "354";

CMailReport* CMailReport::sInstance = nullptr;

namespace {

constexpr std::uint32_t kInvalidSocket =
    UINT32_C(0xffffffff);
constexpr std::uint32_t kPortableSocketToken = 1;
constexpr char kMimeBoundary[] =
    "__boundary__ID__c86j224s";

template <std::size_t Capacity>
void CopyTargetString(
    std::array<char, Capacity>& destination,
    const char* source) noexcept {
    if (!source) return;
    std::size_t index = 0;
    while (index + 1U < Capacity &&
           source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

void CopyTargetString(
    char* destination,
    std::size_t capacity,
    const char* source) noexcept {
    if (!destination || capacity == 0) return;
    destination[0] = '\0';
    if (!source) return;
    const std::size_t length = std::min(
        std::strlen(source),
        capacity - 1U);
    std::memcpy(destination, source, length);
    destination[length] = '\0';
}

void AppendTargetString(
    char* destination,
    std::size_t capacity,
    const char* source) noexcept {
    if (!destination || !source || capacity == 0) return;
    std::size_t used = 0;
    while (used < capacity && destination[used] != '\0') {
        ++used;
    }
    if (used == capacity) return;
    CopyTargetString(
        destination + used,
        capacity - used,
        source);
}

struct LocalSystemTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

LocalSystemTime ReadLocalSystemTime() noexcept {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    if (::localtime_s(&local, &now) != 0) return {};
#else
    if (::localtime_r(&now, &local) == nullptr) return {};
#endif
    return {
        local.tm_year + 1900,
        local.tm_mon + 1,
        local.tm_mday,
        local.tm_hour,
        local.tm_min,
        local.tm_sec};
}

std::array<char, 16> FormatIPv4(
    in_addr address) noexcept {
    std::array<unsigned char, 4> octets{};
    std::memcpy(
        octets.data(),
        &address.s_addr,
        octets.size());
    std::array<char, 16> result{};
    std::snprintf(
        result.data(),
        result.size(),
        "%u.%u.%u.%u",
        static_cast<unsigned int>(octets[0]),
        static_cast<unsigned int>(octets[1]),
        static_cast<unsigned int>(octets[2]),
        static_cast<unsigned int>(octets[3]));
    return result;
}

// std::u16string WideLogString(const char* value) {
    // std::u16string result
    // if (!value) return result.
    // while (*value != '\0') {
        // result.push_back(static_cast<char16_t>(
            // static_cast<unsigned char>(*value++)))
    // }
    // return result
// }
std::u16string WideLogString(const char* value) {
    if (!value) return {};
#if defined(_WIN32)
    return portable::Utf16FromWindowsAnsi(value);
#else
    return Utf8ToUtf16(value);
#endif
}

const char16_t* MailCommandPhase(const char* command) noexcept {
    if (!command) return u"unavailable";
    if (std::strncmp(command, "HELO", 4) == 0) return u"helo";
    if (std::strncmp(command, "MAIL", 4) == 0) return u"mail_from";
    if (std::strncmp(command, "RCPT", 4) == 0) return u"recipient";
    if (std::strncmp(command, "DATA", 4) == 0) return u"data";
    if (std::strncmp(command, "QUIT", 4) == 0) return u"quit";
    if (std::strncmp(command, "\r\n.\r\n", 5) == 0) return u"end_data";
    return u"unrecognized";
}

int ReadMailReplyPrefix(const char* reply, std::size_t capacity) noexcept {
    if (!reply || capacity < 3) return -1;
    int code = 0;
    for (std::size_t index = 0; index < 3; ++index) {
        if (reply[index] < '0' || reply[index] > '9') return -1;
        code = code * 10 + reply[index] - '0';
    }
    return code;
}

const char16_t* MailSocketErrorDomain() noexcept {
#if defined(_WIN32)
    return u"win32_last_error";
#else
    return u"errno";
#endif
}

void LogMailIOFailure(const char16_t* operation, int result, int code, int completed, int requested) {
    if (result == -1) {
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=io_failed operation=%s return_code=%d error_domain=%s native_code=%d completed_bytes=%d requested_bytes=%d",
        //     operation, result, MailSocketErrorDomain(), code, completed, requested)
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] %s returned %d (%s error %d) after %d of %d bytes.",
        //     operation, result, MailSocketErrorDomain(), code, completed, requested)
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] %s returned %d (%s error %d) after %d of %d bytes.",
            operation, result, MailSocketErrorDomain(), code, completed, requested);
    } else {
        // A zero result can mean EOF, a closed owner or no progress. Errno is stale.
        // CFileLog::g_pDefaultFileLog->Log(LOG_WARN,
        //     u"component=mail event=io_stopped operation=%s return_code=%d reason=no_progress_or_closed_socket completed_bytes=%d requested_bytes=%d",
        //     operation, result, completed, requested)
        // CFileLog::g_pDefaultFileLog->Log(LOG_WARN,
        //     u"[mail] %s stopped with result %d after %d of %d bytes: no progress or a closed socket.",
        //     operation, result, completed, requested)
        L2_LOG_WARN(*CFileLog::g_pDefaultFileLog,
            u"[mail] %s stopped with result %d after %d of %d bytes: no progress or a closed socket.",
            operation, result, completed, requested);
    }
}

void LogMailInputFailure(const char16_t* phase, const char16_t* field, std::size_t length, std::size_t maximum) {
    // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
    //     u"component=mail event=input_rejected phase=%s field=%s length_bytes=%llu min_bytes=1 max_bytes=%llu value=redacted",
    //     phase, field, static_cast<unsigned long long>(length), static_cast<unsigned long long>(maximum))
    // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
    //     u"[mail] %s rejected: %s length is %llu bytes. Expected 1..%llu.",
    //     phase, field, static_cast<unsigned long long>(length), static_cast<unsigned long long>(maximum))
    L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        u"[mail] %s rejected: %s length is %llu bytes; expected 1..%llu.",
        phase, field, static_cast<unsigned long long>(length), static_cast<unsigned long long>(maximum));
}

} // namespace

// AuthGateD.exe 0x00412B40, mailreport.cpp.
CMailReport::CMailReport()
    : mServerAddr{},
      mSock(kInvalidSocket),
      mSocketTimeout(5000),
      m_szProductVersion{},
      m_szAppVer{},
      mSender{},
      mReceiver{},
      mLocalIPAddr{},
      mLocalHostname{} {
    // The target constructor does not touch mAppName. The
    // process-global g_reportMail is zero-initialized before this runs.
    if (!sInstance) sInstance = this;
}

// AuthGateD.exe 0x00412BF0, mailreport.cpp.
CMailReport::~CMailReport() {
    authgated::platform::CloseMailSocket(this);
    sInstance = nullptr;
}

// AuthGateD.exe 0x00410C80, mailreport.cpp.
CMailReport* CMailReport::Instance() {
    return sInstance;
}

// AuthGateD.exe 0x00411320, mailreport.cpp.
bool CMailReport::Initialize(
    const char* server,
    std::int16_t port,
    const char* sender,
    const char* receiver,
    const char* appName,
    const char* productVersion,
    const char* appVersion) {
    const std::size_t serverLength = server
        ? std::strlen(server)
        : 0;
    if (serverLength == 0 || serverLength > 63) {
        LogMailInputFailure(u"Initialize", u"server", serverLength, 63);
        return false;
    }
    const std::size_t senderLength = sender
        ? std::strlen(sender)
        : 0;
    if (senderLength == 0 || senderLength > 63) {
        LogMailInputFailure(u"Initialize", u"sender", senderLength, 63);
        return false;
    }
    const std::size_t receiverLength = receiver
        ? std::strlen(receiver)
        : 0;
    if (receiverLength == 0 || receiverLength > 63) {
        LogMailInputFailure(u"Initialize", u"receiver", receiverLength, 63);
        return false;
    }

    if (productVersion) {
        CopyTargetString(
            m_szProductVersion,
            productVersion);
    }
    if (appVersion && appVersion[0] != '\0') {
        CopyTargetString(m_szAppVer, appVersion);
    }
    if (!GetLocalAddress()) return false;

    const authgated::platform::MailHostLookupResult lookup =
        authgated::platform::ResolveMailServerIPv4(server);
    switch (lookup.status) {
    case authgated::platform::MailHostLookupStatus::Resolved:
        break;
    case authgated::platform::MailHostLookupStatus::InvalidAddress:
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - The IPv4 address entered must be a legal address.")
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=initialization_failed phase=resolve_server reason=invalid_ipv4")
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Cannot initialize the relay: invalid IPv4 address.")
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Cannot initialize the relay: invalid IPv4 address.");
        return false;
    case authgated::platform::MailHostLookupStatus::HostNotFound:
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Host not found.")
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=initialization_failed phase=resolve_server reason=host_not_found resolver_code=%d",
        //     lookup.nativeError)
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Cannot initialize the relay: host not found (resolver error %d).",
        //     lookup.nativeError)
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Cannot initialize the relay: host not found (resolver error %d).",
            lookup.nativeError);
        return false;
    case authgated::platform::MailHostLookupStatus::NoData:
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - No data record found.")
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=initialization_failed phase=resolve_server reason=no_ipv4_record resolver_code=%d",
        //     lookup.nativeError)
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Cannot initialize the relay: no IPv4 record (resolver error %d).",
        //     lookup.nativeError)
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Cannot initialize the relay: no IPv4 record (resolver error %d).",
            lookup.nativeError);
        return false;
    case authgated::platform::MailHostLookupStatus::Failed:
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Function failed with error: %ld",
            // static_cast<long>(lookup.nativeError))
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=initialization_failed phase=resolve_server reason=lookup_failed resolver_code=%d",
        //     lookup.nativeError)
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Cannot initialize the relay: address lookup failed (resolver error %d).",
        //     lookup.nativeError)
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Cannot initialize the relay: address lookup failed (resolver error %d).",
            lookup.nativeError);
        return false;
    }

    mServerAddr.sin_family = AF_INET;
    mServerAddr.sin_port = htons(
        static_cast<std::uint16_t>(port));
    mServerAddr.sin_addr.s_addr =
        lookup.networkAddress;
    CopyTargetString(mSender, sender);
    CopyTargetString(mReceiver, receiver);

    const std::size_t appNameLength = appName
        ? std::strlen(appName)
        : 0;
    if (appNameLength != 0 && appNameLength < 32) {
        CopyTargetString(mAppName, appName);
    }
    return true;
}

// AuthGateD.exe 0x00412300, mailreport.cpp.
bool CMailReport::SendMail(
    const char* subject,
    const char* body) {
    const std::size_t subjectLength = subject
        ? std::strlen(subject)
        : 0;
    if (subjectLength == 0 || subjectLength >= 1024) {
        LogMailInputFailure(u"SendMail", u"subject", subjectLength, 1023);
        return false;
    }
    const std::size_t bodyLength = body
        ? std::strlen(body)
        : 0;
    if (bodyLength == 0 || bodyLength > 4096) {
        LogMailInputFailure(u"SendMail", u"body", bodyLength, 4096);
        return false;
    }
    const std::size_t senderLength =
        std::strlen(mSender.data());
    if (senderLength == 0 || senderLength >= 64) {
        LogMailInputFailure(u"SendMail", u"sender", senderLength, 63);
        return false;
    }
    const std::size_t receiverLength =
        std::strlen(mReceiver.data());
    if (receiverLength == 0 || receiverLength >= 64) {
        LogMailInputFailure(u"SendMail", u"receiver", receiverLength, 63);
        return false;
    }

    // CFileLog::g_pDefaultFileLog->Log(
        // LOG_NORMAL,
        // u"ReportMail - SendMail() - ConnectMailServer")
    // CFileLog::g_pDefaultFileLog->Log(LOG_NORMAL,
    //     u"component=mail event=transport_started port=%u attachments=%llu",
    //     static_cast<unsigned int>(ntohs(mServerAddr.sin_port)), static_cast<unsigned long long>(mAttachments.size()))
    // CFileLog::g_pDefaultFileLog->Log(LOG_NORMAL,
    //     u"[mail] Starting SMTP on port %u with %llu attachments.",
    //     static_cast<unsigned int>(ntohs(mServerAddr.sin_port)), static_cast<unsigned long long>(mAttachments.size()))
    L2_LOG_INFO(*CFileLog::g_pDefaultFileLog,
        u"[mail] Starting SMTP on port %u with %llu attachments.",
        static_cast<unsigned int>(ntohs(mServerAddr.sin_port)), static_cast<unsigned long long>(mAttachments.size()));
    if (!ConnectMailServer()) {
        // Noise: the failing transport/command/body owner already reports the cause.
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Failed to perform ConnectMailServer() !!!")
        return false;
    }
    // Noise: phase-entry chatter. Failures carry phase and completion is reported once.
    // CFileLog::g_pDefaultFileLog->Log(
        // LOG_NORMAL,
        // u"ReportMail - SendMail() - PacketHelo")
    if (!PacketHelo()) {
        // Noise: the failing transport/command/body owner already reports the cause.
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Failed to perform PacketHelo() !!!")
        return false;
    }
    // Noise: phase-entry chatter. Failures carry phase and completion is reported once.
    // CFileLog::g_pDefaultFileLog->Log(
        // LOG_NORMAL,
        // u"ReportMail - SendMail() - PacketMailFrom")
    if (!PacketMailFrom()) {
        // Noise: the failing transport/command/body owner already reports the cause.
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Failed to perform PacketMailFrom() !!!")
        return false;
    }
    // Noise: phase-entry chatter. Failures carry phase and completion is reported once.
    // CFileLog::g_pDefaultFileLog->Log(
        // LOG_NORMAL,
        // u"ReportMail - SendMail() - PacketRcptTo")
    if (!PacketRcptTo()) {
        // Noise: the failing transport/command/body owner already reports the cause.
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Failed to perform PacketRcptTo() !!!")
        return false;
    }
    // Noise: phase-entry chatter. Failures carry phase and completion is reported once.
    // CFileLog::g_pDefaultFileLog->Log(
        // LOG_NORMAL,
        // u"ReportMail - SendMail() - PacketData")
    if (!PacketData(subject, body)) {
        // Noise: the failing transport/command/body owner already reports the cause.
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Failed to perform PacketData() !!!")
        return false;
    }
    // Noise: phase-entry chatter. Failures carry phase and completion is reported once.
    // CFileLog::g_pDefaultFileLog->Log(
        // LOG_NORMAL,
        // u"ReportMail - SendMail() - PacketQuit")
    if (!PacketQuit()) {
        // Noise: the failing transport/command/body owner already reports the cause.
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Failed to perform PacketQuit() !!!")
        return false;
    }
    // CFileLog::g_pDefaultFileLog->Log(
        // LOG_NORMAL,
        // u"ReportMail - SendMail() - finished")
    // CFileLog::g_pDefaultFileLog->Log(LOG_NORMAL,
    //     u"component=mail event=transport_sequence_completed smtp_acceptance=prefix_matched quit_reply=unchecked delivery=unverified")
    // CFileLog::g_pDefaultFileLog->Log(LOG_NORMAL,
    //     u"[mail] SMTP exchange completed with matching reply prefixes. QUIT reply was not read, final delivery is not confirmed.")
    // L2_LOG_INFO(*CFileLog::g_pDefaultFileLog,
    //     u"[mail] SMTP exchange completed with matching reply prefixes. QUIT reply was not read, final delivery is not confirmed.")
    // Noise: the observed SMTP reply is enough context. This does not claim mailbox delivery.
    L2_LOG_INFO(*CFileLog::g_pDefaultFileLog,
        u"[mail] SMTP server returned 250 after the message body.");
    return true;
}

// AuthGateD.exe 0x00412CB0, mailreport.cpp.
void CMailReport::AddAttachment(const char* path) {
    if (path) mAttachments.emplace_back(path);
}

// AuthGateD.exe 0x00410C90, mailreport.cpp.
void CMailReport::SocketClose() {
    const std::uint32_t socket =
        runtime::raw_atomic::ExchangeU32(
            &mSock,
            kInvalidSocket);
    if (socket != kInvalidSocket) {
        authgated::platform::CloseMailSocket(this);
    }
}

// AuthGateD.exe 0x00410CE0, mailreport.cpp.
bool CMailReport::SocketSend(
    [[maybe_unused]] std::uint32_t socket,
    const char* data,
    int length,
    [[maybe_unused]] int flags) {
    int sent = 0;
    while (sent < length) {
        const int result =
            authgated::platform::SendMailSocket(
                this,
                data + sent,
                static_cast<std::size_t>(length - sent));
        if (result == -1 || result == 0) {
            const int error =
                authgated::platform::LastMailSocketError();
            // CFileLog::g_pDefaultFileLog->Log(
                // LOG_ERROR,
                // u"ReportMail - Failed to send, nRet=%d, dwLastError=%d",
                // result,
                // error)
            LogMailIOFailure(u"send", result, error, sent, length);
            return false;
        }
        sent += result;
    }
    return true;
}

// AuthGateD.exe 0x00410D50, mailreport.cpp.
bool CMailReport::SocketRecvLine(
    [[maybe_unused]] std::uint32_t socket,
    char* data,
    int length,
    [[maybe_unused]] int flags) {
    int received = 0;
    for (;;) {
        const int result =
            authgated::platform::ReceiveMailSocket(
                this,
                data + received,
                static_cast<std::size_t>(length - received));
        if (result == -1 || result == 0) {
            const int error =
                authgated::platform::LastMailSocketError();
            // CFileLog::g_pDefaultFileLog->Log(
                // LOG_ERROR,
                // u"ReportMail - Failed to recv, nRet=%d, dwLastError=%d",
                // result,
                // error)
            LogMailIOFailure(u"receive", result, error, received, length);
            return false;
        }
        received += result;
        if (received >= length) {
            data[length - 1] = '\0';
            return true;
        }
        if (std::strstr(data, "\r\n")) return true;
    }
}

// AuthGateD.exe 0x00410E80, mailreport.cpp.
bool CMailReport::GetLocalAddress() {
    std::uint32_t address = 0;
    if (!authgated::platform::ResolveLocalMailAddress(
            mLocalHostname.data(),
            mLocalHostname.size(),
            address)) {
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=initialization_failed phase=resolve_local_address reason=no_local_ipv4")
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Cannot initialize SMTP: no local IPv4 address.")
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Cannot initialize SMTP: no local IPv4 address.");
        return false;
    }
    mLocalIPAddr.s_addr = address;
    return true;
}

// AuthGateD.exe 0x00410F30, mailreport.cpp.
bool CMailReport::ConnectMailServer() {
    bool receivedGreeting = false;
    std::array<char, 1024> greeting{};

    mSock = kInvalidSocket;
    if (!authgated::platform::CreateMailSocket(this)) {
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=socket_create_failed network_enabled=%d native_code=unavailable",
        //     runtime::SystemMailNetworkEnabled() ? 1 : 0)
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Could not create a socket. Mail networking is %s. Native error code is unavailable.",
        //     runtime::SystemMailNetworkEnabled() ? u"enabled" : u"disabled")
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Could not create a socket; mail networking is %s. Native error code is unavailable.",
            runtime::SystemMailNetworkEnabled() ? u"enabled" : u"disabled");
        return false;
    }
    mSock = kPortableSocketToken;
    if (!authgated::platform::SetMailReceiveTimeout(
            this,
            mSocketTimeout) ||
        !authgated::platform::SetMailSendTimeout(
            this,
            mSocketTimeout)) {
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=timeout_configuration_failed timeout_ms=%d error_domain=%s native_code=%d",
        //     mSocketTimeout, MailSocketErrorDomain(), authgated::platform::LastMailSocketError())
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Could not set the socket timeout to %d ms (%s error %d).",
        //     mSocketTimeout, MailSocketErrorDomain(), authgated::platform::LastMailSocketError())
        // Original: L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        //     u"[mail] Could not set the socket timeout to %d ms (%s error %d).",
        //     mSocketTimeout, MailSocketErrorDomain(), authgated::platform::LastMailSocketError())
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Could not set the socket timeout to %d ms (%s error %d). Aborting this send attempt; the created socket remains open until a later close or cleanup.",
            mSocketTimeout, MailSocketErrorDomain(), authgated::platform::LastMailSocketError());
        return false;
    }

    // if (!authgated::platform::ConnectMailSocket(
            // this,
            // mServerAddr.sin_addr.s_addr,
            // ntohs(mServerAddr.sin_port)) ||
    const bool connected = authgated::platform::ConnectMailSocket(
        this, mServerAddr.sin_addr.s_addr, ntohs(mServerAddr.sin_port));
    if (!connected ||
        !SocketRecvLine(
            mSock,
            greeting.data(),
            static_cast<int>(greeting.size()),
            0)) {
        [[maybe_unused]] const int error =
            authgated::platform::LastMailSocketError();
        if (!connected) {
            // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
            //     u"component=mail event=connect_failed port=%u error_domain=%s native_code=%d action=close_connection",
            //     static_cast<unsigned int>(ntohs(mServerAddr.sin_port)), MailSocketErrorDomain(), error)
            // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
            //     u"[mail] Could not connect to the relay on port %u (%s error %d). Closing the connection.",
            //     static_cast<unsigned int>(ntohs(mServerAddr.sin_port)), MailSocketErrorDomain(), error)
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[mail] Could not connect to the relay on port %u (%s error %d). Closing the connection.",
                static_cast<unsigned int>(ntohs(mServerAddr.sin_port)), MailSocketErrorDomain(), error);
        }
    } else {
        receivedGreeting = true;
        if (std::strncmp(
                greeting.data(),
                REPLY_SERVICE_READY,
                std::strlen(REPLY_SERVICE_READY)) == 0) {
            return true;
        }
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=reply_mismatch phase=greeting expected_prefix_code=%d actual_prefix_code=%d response=redacted action=close_connection legacy_return=success",
        //     ReadMailReplyPrefix(REPLY_SERVICE_READY, std::strlen(REPLY_SERVICE_READY)),
        //     ReadMailReplyPrefix(greeting.data(), greeting.size()))
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Unexpected SMTP greeting: expected prefix %d, received %d (-1 means no valid code). Closing the connection. This path still returns success.",
        //     ReadMailReplyPrefix(REPLY_SERVICE_READY, std::strlen(REPLY_SERVICE_READY)),
        //     ReadMailReplyPrefix(greeting.data(), greeting.size()))
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Unexpected SMTP greeting: expected prefix %d, received %d (-1 means no valid code). Closing the connection; this path still returns success.",
            ReadMailReplyPrefix(REPLY_SERVICE_READY, std::strlen(REPLY_SERVICE_READY)),
            ReadMailReplyPrefix(greeting.data(), greeting.size()));
    }

    // This appears to be a legacy bug: the shipping image returns true after
    // receiving a mismatched greeting even though it closes the socket. Keep
    // it for EXE parity. The next HELO then reports the actual failure.
    SocketClose();
    return receivedGreeting;
}

// AuthGateD.exe 0x00411050, mailreport.cpp.
bool CMailReport::SendPacket(
    const char* packet,
    const char* expectedReply,
    std::uint32_t* replyCode) {
    std::array<char, 512> reply{};
    if (!packet) {
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - SendPacket(). pszCommand is NULL. %d",
            // 760)
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=command_rejected phase=unavailable reason=null_command")
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Cannot submit a null SMTP command.")
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Cannot submit a null SMTP command.");
        return false;
    }
    if (mSock == kInvalidSocket) {
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - SendPacket(). invalid socket. %d",
            // 766)
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=command_rejected phase=%s reason=closed_socket",
        //     MailCommandPhase(packet))
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Cannot submit SMTP step %s: the socket is closed.",
        //     MailCommandPhase(packet))
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Cannot submit SMTP step %s: the socket is closed.",
            MailCommandPhase(packet));
        return false;
    }
    if (!SocketSend(
            mSock,
            packet,
            static_cast<int>(std::strlen(packet)),
            0)) {
        if (replyCode) {
            *replyCode = static_cast<std::uint32_t>(
                authgated::platform::LastMailSocketError());
        }
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Failed to perform SocketSend() %d",
            // 776)
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=command_failed phase=%s operation=send delivery=unverified",
        //     MailCommandPhase(packet))
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Could not send SMTP step %s. Delivery is not confirmed.",
        //     MailCommandPhase(packet))
        // L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        //     u"[mail] Could not send SMTP step %s. Delivery is not confirmed.",
        //     MailCommandPhase(packet))
        // Noise: the failed step and preceding I/O error describe the observed failure.
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Could not send SMTP step %s.",
            MailCommandPhase(packet));
        return false;
    }
    if (!expectedReply) return true;

    if (!SocketRecvLine(
            mSock,
            reply.data(),
            static_cast<int>(reply.size()),
            0)) {
        if (replyCode) {
            *replyCode = static_cast<std::uint32_t>(
                authgated::platform::LastMailSocketError());
        }
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Failed to perform SocketRecvLine() %d",
            // 788)
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=command_failed phase=%s operation=receive delivery=unverified",
        //     MailCommandPhase(packet))
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Could not read the reply to SMTP step %s. Delivery is not confirmed.",
        //     MailCommandPhase(packet))
        // L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        //     u"[mail] Could not read the reply to SMTP step %s. Delivery is not confirmed.",
        //     MailCommandPhase(packet))
        // Noise: the failed step and preceding I/O error describe the observed failure.
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Could not read the reply to SMTP step %s.",
            MailCommandPhase(packet));
        return false;
    }

    const std::size_t expectedLength = std::min(
        std::strlen(expectedReply),
        reply.size());
    if (std::strncmp(
            reply.data(),
            expectedReply,
            expectedLength) == 0) {
        return true;
    }
    if (replyCode) *replyCode = 0;
    // Noise: the adjacent reply diagnostic includes both prefixes and the command phase.
    // CFileLog::g_pDefaultFileLog->Log(
        // LOG_ERROR,
        // u"ReportMail - mismatched reply code!!!")
    // const int actualReply =
        // (reply[0] - '0') * 100 +
        // (reply[1] - '0') * 10 +
        // (reply[2] - '0')
    // CFileLog::g_pDefaultFileLog->Log(
        // LOG_ERROR,
        // u"ReportMail - check smtp server reply code : %d - %d",
        // std::atoi(expectedReply),
        // actualReply)
    // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
    //     u"component=mail event=reply_mismatch phase=%s expected_prefix_code=%d actual_prefix_code=%d response=redacted action=reject_command",
    //     MailCommandPhase(packet), ReadMailReplyPrefix(expectedReply, expectedLength), ReadMailReplyPrefix(reply.data(), reply.size()))
    // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
    //     u"[mail] SMTP step %s rejected: expected prefix %d, received %d (-1 means no valid code).",
    //     MailCommandPhase(packet), ReadMailReplyPrefix(expectedReply, expectedLength), ReadMailReplyPrefix(reply.data(), reply.size()))
    L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        u"[mail] SMTP step %s rejected: expected prefix %d, received %d (-1 means no valid code).",
        MailCommandPhase(packet), ReadMailReplyPrefix(expectedReply, expectedLength), ReadMailReplyPrefix(reply.data(), reply.size()));
    return false;
}

// AuthGateD.exe 0x004115B0, mailreport.cpp.
bool CMailReport::PacketHelo() {
    std::uint32_t replyCode = 0;
    const bool result = SendPacket(
        "HELO\r\n",
        REPLY_MAIL_ACTION_OK,
        &replyCode);
    if (!result) SocketClose();
    return result;
}

// AuthGateD.exe 0x004115F0, mailreport.cpp.
bool CMailReport::PacketMailFrom() {
    std::array<char, 512> command{};
    std::snprintf(
        command.data(),
        command.size(),
        "MAIL FROM: <%s>\r\n",
        mSender.data());
    std::uint32_t replyCode = 0;
    const bool result = SendPacket(
        command.data(),
        REPLY_MAIL_ACTION_OK,
        &replyCode);
    if (!result) SocketClose();
    return result;
}

// AuthGateD.exe 0x00411690, mailreport.cpp.
bool CMailReport::PacketRcptTo() {
    std::array<char, 512> command{};
    std::snprintf(
        command.data(),
        command.size(),
        "RCPT TO: <%s>\r\n",
        mReceiver.data());
    std::uint32_t replyCode = 0;
    const bool result = SendPacket(
        command.data(),
        REPLY_MAIL_ACTION_OK,
        &replyCode);
    if (!result) SocketClose();
    return result;
}

// AuthGateD.exe 0x00411730, mailreport.cpp.
bool CMailReport::PacketQuit() {
    std::uint32_t replyCode = 0;
    const bool result = SendPacket(
        "QUIT\r\n",
        nullptr,
        &replyCode);
    SocketClose();
    return result;
}

// AuthGateD.exe 0x00411790, mailreport.cpp.
bool CMailReport::PacketData(
    const char* subject,
    const char* body) {
    std::uint32_t replyCode = 0;
    if (!SendPacket(
            "DATA\r\n",
            REPLY_ENTER_EMAIL,
            &replyCode)) {
        // Noise: SendPacket already reports the command phase and failure.
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Failed to send command-DATA, %d, %d",
            // 483,
            // static_cast<int>(replyCode))
        SocketClose();
        return false;
    }

    std::array<char, 1024> text{};
    std::snprintf(
        text.data(),
        text.size(),
        "To: %s\r\nSubject: [%s] %s\r\n",
        mReceiver.data(),
        mAppName.data(),
        subject);
    AppendTargetString(
        text.data(),
        text.size(),
        "MIME-Version: 1.0\r\n");
    if (mAttachments.empty()) {
        AppendTargetString(
            text.data(),
            text.size(),
            "Content-type: text/plain; charset=US-ASCII\r\n");
        AppendTargetString(
            text.data(),
            text.size(),
            "Content-Transfer-Encoding: 7bit\r\n");
        AppendTargetString(text.data(), text.size(), "\r\n");
    } else {
        AppendTargetString(
            text.data(),
            text.size(),
            "Content-Type: multipart/mixed; boundary=\"");
        AppendTargetString(
            text.data(),
            text.size(),
            kMimeBoundary);
        AppendTargetString(text.data(), text.size(), "\"\r\n");
        AppendTargetString(text.data(), text.size(), "\r\n--");
        AppendTargetString(
            text.data(),
            text.size(),
            kMimeBoundary);
        AppendTargetString(text.data(), text.size(), "\r\n");
        AppendTargetString(
            text.data(),
            text.size(),
            "Content-type: text/plain; charset=US-ASCII\r\n");
        AppendTargetString(
            text.data(),
            text.size(),
            "Content-Transfer-Encoding: 7bit\r\n");
        AppendTargetString(text.data(), text.size(), "\r\n");
    }

    if (!SocketSend(
            mSock,
            text.data(),
            static_cast<int>(std::strlen(text.data())),
            0)) {
        // const int error =
            // authgated::platform::LastMailSocketError()
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Failed to perform SocketSend() %d %d",
            // 549,
            // error)
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=body_send_failed phase=headers action=close_connection")
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Could not send message headers. Closing the connection.")
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Could not send message headers. Closing the connection.");
        SocketClose();
        return false;
    }

    const LocalSystemTime local = ReadLocalSystemTime();
    const std::array<char, 16> localAddress =
        FormatIPv4(mLocalIPAddr);
    std::snprintf(
        text.data(),
        text.size(),
        "==============================================\r\n"
        "IP Address : %s \r\n"
        "Hostname   : %s \r\n"
        "Time       : %.4d/%.2d/%.2d %.2d:%.2d:%.2d \r\n"
        "Product Version : %s \r\n"
        "App Version: %s \r\n"
        "==============================================\r\n\r\n\r\n",
        localAddress.data(),
        mLocalHostname.data(),
        local.year,
        local.month,
        local.day,
        local.hour,
        local.minute,
        local.second,
        m_szProductVersion.data(),
        m_szAppVer.data());
    if (!SocketSend(
            mSock,
            text.data(),
            static_cast<int>(std::strlen(text.data())),
            0)) {
        // const int error =
            // authgated::platform::LastMailSocketError()
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Failed to perform SocketSend() %d %d",
            // 576,
            // error)
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=body_send_failed phase=host_metadata action=close_connection")
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Could not send host metadata. Closing the connection.")
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Could not send host metadata. Closing the connection.");
        SocketClose();
        return false;
    }

    std::array<char, 4096> message{};
    std::snprintf(
        message.data(),
        message.size(),
        "%s\r\n\r\n",
        body);
    if (!SocketSend(
            mSock,
            message.data(),
            static_cast<int>(std::strlen(message.data())),
            0)) {
        // const int error =
            // authgated::platform::LastMailSocketError()
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Failed to perform SocketSend() %d %d",
            // 588,
            // error)
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=body_send_failed phase=body action=close_connection")
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Could not send message body. Closing the connection.")
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Could not send message body. Closing the connection.");
        SocketClose();
        return false;
    }

    std::unique_ptr<unsigned char[]> fileBuffer(
        new (std::nothrow) unsigned char[55]);
    std::unique_ptr<char[]> fileName(
        new (std::nothrow) char[1024]);
    std::unique_ptr<char[]> sendBuffer(
        new (std::nothrow) char[10240]);
    if (!fileBuffer || !fileName || !sendBuffer) {
        // const int error = errno
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Failed to allocate memory: %ld",
            // static_cast<long>(error))
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"component=mail event=allocation_failed phase=attachments requested_buffers=3 total_requested_bytes=11319 action=close_connection")
        // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
        //     u"[mail] Could not allocate three attachment buffers totalling 11319 bytes. Closing the connection.")
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[mail] Could not allocate three attachment buffers totalling 11319 bytes. Closing the connection.");
        SocketClose();
        return false;
    }
    std::memset(fileBuffer.get(), 0, 55);
    std::memset(fileName.get(), 0, 1024);
    std::memset(sendBuffer.get(), 0, 10240);

    std::uint32_t totalAttachmentSize = 0;
    for (const std::string& attachment : mAttachments) {
        CopyTargetString(
            fileName.get(),
            1024,
            attachment.c_str());

        std::snprintf(
            sendBuffer.get(),
            10240,
            "--%s\r\n",
            kMimeBoundary);
        AppendTargetString(
            sendBuffer.get(),
            10240,
            "Content-Type: application/x-msdownload; name=\"");
        const std::size_t filenameOffset =
            attachment.find_last_of('\\') + 1U;
        AppendTargetString(
            sendBuffer.get(),
            10240,
            fileName.get() + filenameOffset);
        AppendTargetString(
            sendBuffer.get(),
            10240,
            "\"\r\nContent-Transfer-Encoding: base64\r\n");
        AppendTargetString(
            sendBuffer.get(),
            10240,
            "Content-Disposition: attachment; pFileName=\"");
        AppendTargetString(
            sendBuffer.get(),
            10240,
            fileName.get() + filenameOffset);
        AppendTargetString(sendBuffer.get(), 10240, "\"\r\n\r\n");

        if (!SocketSend(
                mSock,
                sendBuffer.get(),
                static_cast<int>(std::strlen(sendBuffer.get())),
                0)) {
            // const int error =
                // authgated::platform::LastMailSocketError()
            // CFileLog::g_pDefaultFileLog->Log(
                // LOG_ERROR,
                // u"ReportMail - Failed to perform SocketSend() %d %d",
                // 635,
                // error)
            // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
            //     u"component=mail event=body_send_failed phase=attachment_header action=close_connection")
            // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
            //     u"[mail] Could not send attachment header. Closing the connection.")
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[mail] Could not send attachment header. Closing the connection.");
            SocketClose();
            return false;
        }

        std::FILE* const file =
            std::fopen(fileName.get(), "rb");
        if (!file) {
            const int error = errno;
            const std::u16string wideFileName =
                WideLogString(fileName.get());
            // CFileLog::g_pDefaultFileLog->Log(
                // LOG_ERROR,
                // u"ReportMail - Failed to open file: %ld, %s",
                // static_cast<long>(error),
                // wideFileName.c_str())
            // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
            //     u"component=mail event=attachment_open_failed error_domain=errno native_code=%d file=\"%s\" action=close_connection",
            //     error, wideFileName.c_str())
            // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
            //     u"[mail] Could not open attachment (errno %d, file \"%s\"). Closing the connection.",
            //     error, wideFileName.c_str())
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[mail] Could not open attachment (errno %d, file \"%s\"). Closing the connection.",
                error, wideFileName.c_str());
            SocketClose();
            return false;
        }

        std::uint32_t fileSize = 0;
        while (std::feof(file) == 0) {
            fileSize += static_cast<std::uint32_t>(
                std::fread(
                    fileBuffer.get(),
                    1,
                    54,
                    file));
        }
        totalAttachmentSize += fileSize;
        if ((totalAttachmentSize & UINT32_C(0xfffffc00)) >
            UINT32_C(0x500000)) {
            // CFileLog::g_pDefaultFileLog->Log(
                // LOG_ERROR,
                // u"ReportMail - message too big !!!")
            // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
            //     u"component=mail event=attachment_limit_exceeded total_bytes=%u comparison=rounded_down_1k limit_bytes=5242880 action=close_connection",
            //     totalAttachmentSize)
            // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
            //     u"[mail] Attachments total %u bytes. Size rounded down to KiB exceeds the 5120 KiB limit. Closing the connection.",
            //     totalAttachmentSize)
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[mail] Attachments total %u bytes; size rounded down to KiB exceeds the 5120 KiB limit. Closing the connection.",
                totalAttachmentSize);
            std::fclose(file);
            SocketClose();
            return false;
        }

        std::fseek(file, 0, SEEK_SET);
        std::uint32_t messagePart = 0;
        const std::uint32_t blockCount =
            fileSize / 54U + 1U;
        for (std::uint32_t block = 0;
             block < blockCount;
             ++block) {
            const std::uint32_t read =
                static_cast<std::uint32_t>(
                    std::fread(
                        fileBuffer.get(),
                        1,
                        54,
                        file));
            Base64Encode(
                read,
                fileBuffer.get(),
                static_cast<std::uint32_t>(text.size()),
                text.data());
            if (messagePart == 0) {
                CopyTargetString(
                    sendBuffer.get(),
                    10240,
                    text.data());
            } else {
                AppendTargetString(
                    sendBuffer.get(),
                    10240,
                    text.data());
            }
            AppendTargetString(
                sendBuffer.get(),
                10240,
                "\r\n");
            messagePart += read + 2U;
            if (messagePart >= UINT32_C(0x1400)) {
                messagePart = 0;
                if (!SocketSend(
                        mSock,
                        sendBuffer.get(),
                        static_cast<int>(
                            std::strlen(sendBuffer.get())),
                        0)) {
                    // const int error =
                        // authgated::platform::LastMailSocketError()
                    // CFileLog::g_pDefaultFileLog->Log(
                        // LOG_ERROR,
                        // u"ReportMail - Failed to perform SocketSend() %d %d",
                        // 683,
                        // error)
                    // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
                    //     u"component=mail event=body_send_failed phase=attachment_chunk action=close_connection")
                    // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
                    //     u"[mail] Could not send attachment chunk. Closing the connection.")
                    L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                        u"[mail] Could not send attachment chunk. Closing the connection.");
                    std::fclose(file);
                    SocketClose();
                    return false;
                }
            }
        }
        if (messagePart != 0 &&
            !SocketSend(
                mSock,
                sendBuffer.get(),
                static_cast<int>(
                    std::strlen(sendBuffer.get())),
                0)) {
            // const int error =
                // authgated::platform::LastMailSocketError()
            // CFileLog::g_pDefaultFileLog->Log(
                // LOG_ERROR,
                // u"ReportMail - Failed to perform SocketSend() %d %d",
                // 696,
                // error)
            // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
            //     u"component=mail event=body_send_failed phase=attachment_tail action=close_connection")
            // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
            //     u"[mail] Could not send attachment tail. Closing the connection.")
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[mail] Could not send attachment tail. Closing the connection.");
            std::fclose(file);
            SocketClose();
            return false;
        }
        std::fclose(file);
    }

    if (!mAttachments.empty()) {
        std::snprintf(
            text.data(),
            text.size(),
            "\r\n--%s--\r\n",
            kMimeBoundary);
        if (!SocketSend(
                mSock,
                text.data(),
                static_cast<int>(std::strlen(text.data())),
                0)) {
            // const int error =
                // authgated::platform::LastMailSocketError()
            // CFileLog::g_pDefaultFileLog->Log(
                // LOG_ERROR,
                // u"ReportMail - Failed to perform SocketSend() %d %d",
                // 716,
                // error)
            // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
            //     u"component=mail event=body_send_failed phase=multipart_end action=close_connection")
            // CFileLog::g_pDefaultFileLog->Log(LOG_ERROR,
            //     u"[mail] Could not send multipart terminator. Closing the connection.")
            L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
                u"[mail] Could not send multipart terminator. Closing the connection.");
            SocketClose();
            return false;
        }
    }

    if (!SendPacket(
            "\r\n.\r\n",
            REPLY_MAIL_ACTION_OK,
            &replyCode)) {
        // Noise: SendPacket already reports the command phase and failure.
        // CFileLog::g_pDefaultFileLog->Log(
            // LOG_ERROR,
            // u"ReportMail - Failed to perform SendPacket() %d %d",
            // 725,
            // static_cast<int>(replyCode))
        SocketClose();
        return false;
    }
    return true;
}
