#include "MailReport.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/SystemMailTransport.h"
#include "Log.h"
#include "Util.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>

#if defined(_WIN32)
#  include <WinSock2.h>
#  include <Windows.h>
#else
#  include <netdb.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
#endif

const char* REPLY_SERVICE_READY = "220";
const char* REPLY_MAIL_ACTION_OK = "250";
const char* REPLY_ENTER_EMAIL = "354";

CMailReport* CMailReport::sInstance = nullptr;

namespace {

constexpr unsigned int kInvalidSocket = UINT32_C(0xffffffff);
constexpr const char* kMimeBoundary = "__boundary__ID__c86j224s";
constexpr int kHostNotFound = 11001;
constexpr int kNoDataRecord = 11004;

unsigned long LastSocketError() noexcept {
    return static_cast<unsigned long>(
        runtime::LastSystemMailSocketError());
}

unsigned long LastSystemError() noexcept {
#if defined(_WIN32)
    return static_cast<unsigned long>(::GetLastError());
#else
    return static_cast<unsigned long>(errno);
#endif
}

unsigned long LastHostLookupError() noexcept {
#if defined(_WIN32)
    return static_cast<unsigned long>(::WSAGetLastError());
#else
    if (h_errno == HOST_NOT_FOUND) {
        return kHostNotFound;
    }
#  if defined(NO_DATA)
    if (h_errno == NO_DATA) {
        return kNoDataRecord;
    }
#  elif defined(NO_ADDRESS)
    if (h_errno == NO_ADDRESS) {
        return kNoDataRecord;
    }
#  endif
    return static_cast<unsigned long>(h_errno);
#endif
}


// Diagnostic classification only: never echo SMTP commands or server reply text.
int MailReplyCode(const char* reply) noexcept {
    if (reply == nullptr || reply[0] < '0' || reply[0] > '9' ||
        reply[1] < '0' || reply[1] > '9' ||
        reply[2] < '0' || reply[2] > '9') {
        return -1;
    }
    return (reply[0] - '0') * 100 + (reply[1] - '0') * 10 + (reply[2] - '0');
}
const char* MailCommandStage(const char* packet) noexcept {
    if (packet == nullptr) return "none";
    if (std::strncmp(packet, "HELO", 4) == 0) return "helo";
    if (std::strncmp(packet, "MAIL FROM:", 10) == 0) return "mail_from";
    if (std::strncmp(packet, "RCPT TO:", 8) == 0) return "rcpt_to";
    if (std::strncmp(packet, "DATA", 4) == 0) return "data_command";
    if (std::strcmp(packet, "\r\n.\r\n") == 0) return "data_terminator";
    if (std::strncmp(packet, "QUIT", 4) == 0) return "quit";
    return "unknown";
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
    if (::localtime_s(&local, &now) != 0) {
        return {};
    }
#else
    if (::localtime_r(&now, &local) == nullptr) {
        return {};
    }
#endif
    return {
        local.tm_year + 1900,
        local.tm_mon + 1,
        local.tm_mday,
        local.tm_hour,
        local.tm_min,
        local.tm_sec};
}

} // namespace

// L2Authd.exe 0x00466FD0, Src/mailreport.cpp:27-39.
CMailReport::CMailReport()
    : mServerAddr{},
      mSock(kInvalidSocket),
      mSocketTimeout(5000),
      mSender{},
      mReceiver{},
      mLocalIPAddr{},
      mLocalHostname{} {
    // The selected constructor does not initialize mAppName. reportMail is a
    // static-storage object, so L2AuthD.cpp's instance is zeroed before this
    // constructor exactly as in the target process.
    if (sInstance == nullptr) {
        sInstance = this;
    }
}

// L2Authd.exe 0x004670E0, Src/mailreport.cpp:43-45.
CMailReport::~CMailReport() {
    sInstance = nullptr;
}

// L2Authd.exe 0x00467120, Src/mailreport.cpp:49-51.
CMailReport* CMailReport::Instance() {
    return sInstance;
}

// L2Authd.exe 0x00467130, Src/mailreport.cpp:137-235.
bool CMailReport::Initialize(
    const char* server,
    const short port,
    const char* sender,
    const char* receiver,
    const char* applicationName,
    const char* productVersion,
    const char* buildNumber) {
    int length = server == nullptr
        ? 0
        : static_cast<int>(StrLen(server));
    if (length <= 0 || length >= 64) {
        // Log.AddLog(LOG_ERROR,
        //     "component=mail client=report event=config_rejected field=server bytes=%d min_bytes=1 max_bytes=63 action=return_false",
        //     length)
        // Log.AddLog(LOG_ERROR,
        //     "[mail] Invalid server name length: %d bytes. Expected 1..63. Initialization failed.",
        //     length)
        L2_LOG_ERROR(Log,
            "[mail] Invalid server name length: %d bytes; expected 1..63. Initialization failed.",
            length);
        return false;
    }

    length = sender == nullptr
        ? 0
        : static_cast<int>(StrLen(sender));
    if (length <= 0 || length >= 64) {
        // Log.AddLog(LOG_ERROR,
        //     "component=mail client=report event=config_rejected field=sender bytes=%d min_bytes=1 max_bytes=63 action=return_false",
        //     length)
        // Log.AddLog(LOG_ERROR,
        //     "[mail] Invalid sender address length: %d bytes. Expected 1..63. Initialization failed.",
        //     length)
        L2_LOG_ERROR(Log,
            "[mail] Invalid sender address length: %d bytes; expected 1..63. Initialization failed.",
            length);
        return false;
    }

    length = receiver == nullptr
        ? 0
        : static_cast<int>(StrLen(receiver));
    if (length <= 0 || length >= 64) {
        // Log.AddLog(LOG_ERROR,
        //     "component=mail client=report event=config_rejected field=receiver bytes=%d min_bytes=1 max_bytes=63 action=return_false",
        //     length)
        // Log.AddLog(LOG_ERROR,
        //     "[mail] Invalid recipient address length: %d bytes. Expected 1..63. Initialization failed.",
        //     length)
        L2_LOG_ERROR(Log,
            "[mail] Invalid recipient address length: %d bytes; expected 1..63. Initialization failed.",
            length);
        return false;
    }

    if (productVersion != nullptr) {
        m_productVersion = productVersion;
    }
    if (buildNumber != nullptr) {
        m_buildNumber = buildNumber;
    }
    if (!GetLocalAddress()) {
        return false;
    }

    hostent* remoteHost = nullptr;
    in_addr serverAddress{};
    if (std::isalpha(
            static_cast<unsigned char>(server[0])) != 0) {
        remoteHost = ::gethostbyname(server);
    } else {
        serverAddress.s_addr = ::inet_addr(server);
        if (serverAddress.s_addr == INADDR_NONE) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "ReportMail - The IPv4 address entered must be a legal address.")
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=mail client=report event=config_rejected field=server reason=invalid_ipv4 host_preview=\"%.63s\" action=return_false",
            //     server)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[mail] Invalid IPv4 address for the mail server (preview \"%.63s\"). Initialization failed.",
            //     server)
            L2_LOG_ERROR(Log,
                "[mail] Invalid IPv4 address for the mail server (preview \"%.63s\"). Initialization failed.",
                server);
            return false;
        }
        remoteHost = ::gethostbyaddr(
            reinterpret_cast<const char*>(&serverAddress),
            sizeof(serverAddress),
            AF_INET);
    }

    if (remoteHost == nullptr) {
        const unsigned long error = LastHostLookupError();
        if (error == kHostNotFound) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "ReportMail - Host not found.")
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=mail client=report event=resolve_failed host_preview=\"%.63s\" lookup=%s reason=host_not_found error_domain=host_lookup_compatible code=%lu action=return_false",
            //     server, std::isalpha(static_cast<unsigned char>(server[0])) != 0 ? "forward" : "reverse", error)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[mail] Mail server \"%.63s\" was not found by %s lookup (resolver error %lu). Initialization failed.",
            //     server, std::isalpha(static_cast<unsigned char>(server[0])) != 0 ? "forward" : "reverse", error)
            L2_LOG_ERROR(Log,
                "[mail] Mail server \"%.63s\" was not found by %s lookup (resolver error %lu). Initialization failed.",
                server, std::isalpha(static_cast<unsigned char>(server[0])) != 0 ? "forward" : "reverse", error);
        } else if (error == kNoDataRecord) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "ReportMail - No data record found.")
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=mail client=report event=resolve_failed host_preview=\"%.63s\" lookup=%s reason=no_data_record error_domain=host_lookup_compatible code=%lu action=return_false",
            //     server, std::isalpha(static_cast<unsigned char>(server[0])) != 0 ? "forward" : "reverse", error)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[mail] Mail server \"%.63s\" has no data record for %s lookup (resolver error %lu). Initialization failed.",
            //     server, std::isalpha(static_cast<unsigned char>(server[0])) != 0 ? "forward" : "reverse", error)
            L2_LOG_ERROR(Log,
                "[mail] Mail server \"%.63s\" has no data record for %s lookup (resolver error %lu). Initialization failed.",
                server, std::isalpha(static_cast<unsigned char>(server[0])) != 0 ? "forward" : "reverse", error);
        } else {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "ReportMail - Function failed with error: %ld",
            //     static_cast<long>(error))
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=mail client=report event=resolve_failed host_preview=\"%.63s\" lookup=%s reason=lookup_failed error_domain=host_lookup_compatible code=%lu action=return_false",
            //     server, std::isalpha(static_cast<unsigned char>(server[0])) != 0 ? "forward" : "reverse", error)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[mail] Could not resolve mail server \"%.63s\" with %s lookup (resolver error %lu). Initialization failed.",
            //     server, std::isalpha(static_cast<unsigned char>(server[0])) != 0 ? "forward" : "reverse", error)
            L2_LOG_ERROR(Log,
                "[mail] Could not resolve mail server \"%.63s\" with %s lookup (resolver error %lu). Initialization failed.",
                server, std::isalpha(static_cast<unsigned char>(server[0])) != 0 ? "forward" : "reverse", error);
        }
        return false;
    }

    std::memcpy(
        &serverAddress,
        remoteHost->h_addr_list[0],
        sizeof(serverAddress));
    mServerAddr.sin_family = AF_INET;
    mServerAddr.sin_port = htons(
        static_cast<unsigned short>(port));
    mServerAddr.sin_addr = serverAddress;
    StrCopy(mSender, sender, sizeof(mSender));
    StrCopy(mReceiver, receiver, sizeof(mReceiver));

    length = applicationName == nullptr
        ? 0
        : static_cast<int>(StrLen(applicationName));
    if (length > 0 && length < 32) {
        StrCopy(
            mAppName,
            applicationName,
            sizeof(mAppName));
    }
    return true;
}

// L2Authd.exe 0x004673D0, Src/mailreport.cpp:327-412.
bool CMailReport::SendMail(
    const char* subject,
    const char* message) {
    int length = subject == nullptr
        ? 0
        : static_cast<int>(StrLen(subject));
    if (length <= 0 || length >= 1024) {
        // Log.AddLog(LOG_ERROR,
        //     "component=mail client=report event=send_rejected field=subject bytes=%d min_bytes=1 max_bytes=1023 action=return_false",
        //     length)
        // Log.AddLog(LOG_ERROR,
        //     "[mail] Invalid subject length: %d bytes. Expected 1..1023. Sending rejected.",
        //     length)
        L2_LOG_ERROR(Log,
            "[mail] Invalid subject length: %d bytes; expected 1..1023. Sending rejected.",
            length);
        return false;
    }

    length = message == nullptr
        ? 0
        : static_cast<int>(StrLen(message));
    if (length <= 0 || length > 4096) {
        // Log.AddLog(LOG_ERROR,
        //     "component=mail client=report event=send_rejected field=body bytes=%d min_bytes=1 max_bytes=4096 action=return_false",
        //     length)
        // Log.AddLog(LOG_ERROR,
        //     "[mail] Invalid body length: %d bytes. Expected 1..4096. Sending rejected.",
        //     length)
        L2_LOG_ERROR(Log,
            "[mail] Invalid body length: %d bytes; expected 1..4096. Sending rejected.",
            length);
        return false;
    }

    length = static_cast<int>(StrLen(mSender));
    if (length <= 0 || length >= 64) {
        // Log.AddLog(LOG_ERROR,
        //     "component=mail client=report event=send_rejected field=sender bytes=%d min_bytes=1 max_bytes=63 action=return_false",
        //     length)
        // Log.AddLog(LOG_ERROR,
        //     "[mail] Invalid sender address length: %d bytes. Expected 1..63. Sending rejected.",
        //     length)
        L2_LOG_ERROR(Log,
            "[mail] Invalid sender address length: %d bytes; expected 1..63. Sending rejected.",
            length);
        return false;
    }
    length = static_cast<int>(StrLen(mReceiver));
    if (length <= 0 || length >= 64) {
        // Log.AddLog(LOG_ERROR,
        //     "component=mail client=report event=send_rejected field=receiver bytes=%d min_bytes=1 max_bytes=63 action=return_false",
        //     length)
        // Log.AddLog(LOG_ERROR,
        //     "[mail] Invalid recipient address length: %d bytes. Expected 1..63. Sending rejected.",
        //     length)
        L2_LOG_ERROR(Log,
            "[mail] Invalid recipient address length: %d bytes; expected 1..63. Sending rejected.",
            length);
        return false;
    }

    // Noise: Routine SMTP step announcements add no outcome. Failures identify the stage and completion is reported once.
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "ReportMail - SendMail() - ConnectMailServer")
    if (!ConnectMailServer()) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - Failed to perform ConnectMailServer() !!!")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=mail client=report event=send_failed stage=connect relay_acceptance=unconfirmed action=return_false")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[mail] SMTP failed during connection setup. Relay acceptance is not confirmed, returning failure.")
        // Noise: ConnectMailServer already reports the failed setup operation and its transport or resolver result.
        // L2_LOG_ERROR(Log,
        //     "[mail] SMTP failed during connection setup. Relay acceptance is not confirmed, returning failure.")
        return false;
    }

    // Noise: Routine SMTP step announcements add no outcome. Failures identify the stage and completion is reported once.
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "ReportMail - SendMail() - PacketHelo")
    if (!PacketHelo()) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - Failed to perform PacketHelo() !!!")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=mail client=report event=send_failed stage=helo relay_acceptance=unconfirmed action=return_false")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[mail] SMTP failed during HELO. Relay acceptance is not confirmed, returning failure.")
        L2_LOG_ERROR(Log,
            "[mail] SMTP failed during HELO. Relay acceptance is not confirmed; returning failure.");
        return false;
    }

    // Noise: Routine SMTP step announcements add no outcome. Failures identify the stage and completion is reported once.
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "ReportMail - SendMail() - PacketMailFrom")
    if (!PacketMailFrom()) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - Failed to perform PacketMailFrom() !!!")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=mail client=report event=send_failed stage=mail_from relay_acceptance=unconfirmed action=return_false")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[mail] SMTP failed during MAIL FROM. Relay acceptance is not confirmed, returning failure.")
        L2_LOG_ERROR(Log,
            "[mail] SMTP failed during MAIL FROM. Relay acceptance is not confirmed; returning failure.");
        return false;
    }

    // Noise: Routine SMTP step announcements add no outcome. Failures identify the stage and completion is reported once.
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "ReportMail - SendMail() - PacketRcptTo")
    if (!PacketRcptTo()) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - Failed to perform PacketRcptTo() !!!")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=mail client=report event=send_failed stage=rcpt_to relay_acceptance=unconfirmed action=return_false")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[mail] SMTP failed during RCPT TO. Relay acceptance is not confirmed, returning failure.")
        L2_LOG_ERROR(Log,
            "[mail] SMTP failed during RCPT TO. Relay acceptance is not confirmed; returning failure.");
        return false;
    }

    // Noise: Routine SMTP step announcements add no outcome. Failures identify the stage and completion is reported once.
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "ReportMail - SendMail() - PacketData")
    if (!PacketData(subject, message)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - Failed to perform PacketData() !!!")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=mail client=report event=send_failed stage=data relay_acceptance=unconfirmed action=return_false")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[mail] SMTP failed during DATA. Relay acceptance is not confirmed, returning failure.")
        // Noise: PacketData already identifies the failed DATA part and whether relay acceptance was confirmed.
        // L2_LOG_ERROR(Log,
        //     "[mail] SMTP failed during DATA. Relay acceptance is not confirmed, returning failure.")
        return false;
    }

    // Noise: Routine SMTP step announcements add no outcome. Failures identify the stage and completion is reported once.
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "ReportMail - SendMail() - PacketQuit")
    if (!PacketQuit()) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - Failed to perform PacketQuit() !!!")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=mail client=report event=send_failed stage=quit relay_acceptance=confirmed action=return_false")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[mail] QUIT failed after the relay accepted the message. Returning failure, final delivery is not confirmed.")
        // L2_LOG_ERROR(Log,
        //     "[mail] QUIT failed after the relay accepted the message. Returning failure, final delivery is not confirmed.")
        L2_LOG_ERROR(Log,
            "[mail] QUIT failed after the relay accepted the message. SendMail returns failure.");
        return false;
    }

    // Log.AddLog(
    //     LOG_NORMAL,
    //     "ReportMail - SendMail() - finished")
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "component=mail client=report event=smtp_transaction_completed relay_acceptance=confirmed quit_reply=not_read delivery=unconfirmed attachments=%zu",
    //     mAttachments.size())
    // Log.AddLog(
    //     LOG_NORMAL,
    //     "[mail] Relay accepted the message with %zu attachments. QUIT reply was not read. Final delivery is not confirmed.",
    //     mAttachments.size())
    // L2_LOG_INFO(Log,
    //     "[mail] Relay accepted the message with %zu attachments. QUIT reply was not read. Final delivery is not confirmed.",
    //     mAttachments.size())
    // Noise: relay acceptance already states the observed outcome, without claiming mailbox delivery.
    // L2_LOG_INFO(Log,
    //     "[mail] Relay accepted the message with %zu attachments.",
    //     mAttachments.size())
    L2_LOG_INFO(Log,
        "[mail] Relay accepted the message with %llu attachments.",
        static_cast<unsigned long long>(mAttachments.size()));
    return true;
}

// L2Authd.exe 0x00467640, Src/mailreport.cpp:876-881.
void CMailReport::AddAttachment(const char* filename) {
    if (filename != nullptr) {
        mAttachments.insert(
            mAttachments.end(),
            std::string(filename));
    }
}

// L2Authd.exe 0x004676F0, Src/mailreport.cpp:884-886.
void CMailReport::ClearAttachments() {
    mAttachments.clear();
}

// L2Authd.exe 0x00467710, Src/mailreport.cpp:239-256.
bool CMailReport::GetLocalAddress() {
    char localHostname[24]{};
    if (::gethostname(
            localHostname,
            sizeof(localHostname)) != 0) {
#if defined(_WIN32)
        const int hostnameError = ::WSAGetLastError();
        const char* const hostnameErrorDomain = "winsock";
#else
        const int hostnameError = errno;
        const char* const hostnameErrorDomain = "errno";
#endif
        // Log.AddLog(LOG_ERROR,
        //     "component=mail client=report event=local_address_failed stage=gethostname error_domain=%s code=%d action=return_false",
        //     hostnameErrorDomain, hostnameError)
        // Log.AddLog(LOG_ERROR,
        //     "[mail] Could not read the local hostname (%s error %d). Connection setup failed.",
        //     hostnameErrorDomain, hostnameError)
        L2_LOG_ERROR(Log,
            "[mail] Could not read the local hostname (%s error %d). Connection setup failed.",
            hostnameErrorDomain, hostnameError);
        return false;
    }

    hostent* const host = ::gethostbyname(localHostname);
    if (host == nullptr) {
        // Log.AddLog(LOG_ERROR,
        //     "component=mail client=report event=local_address_failed stage=resolve_hostname error_domain=host_lookup_compatible code=%lu action=return_false",
        //     LastHostLookupError())
        // Log.AddLog(LOG_ERROR,
        //     "[mail] Could not resolve the local hostname (resolver error %lu). Connection setup failed.",
        //     LastHostLookupError())
        L2_LOG_ERROR(Log,
            "[mail] Could not resolve the local hostname (resolver error %lu). Connection setup failed.",
            LastHostLookupError());
        return false;
    }

    StrCopy(
        mLocalHostname,
        host->h_name,
        sizeof(mLocalHostname));
    std::memcpy(
        &mLocalIPAddr,
        host->h_addr_list[0],
        sizeof(mLocalIPAddr));
    return true;
}

// L2Authd.exe 0x004677B0, Src/mailreport.cpp:311-321.
void CMailReport::SocketClose() {
    const unsigned int temporarySocket =
        runtime::raw_atomic::ExchangeU32(
            &mSock,
            kInvalidSocket);
    if (temporarySocket == kInvalidSocket) {
        return;
    }

    runtime::CloseSystemMailSocket(this);
}

// L2Authd.exe 0x00467810, Src/mailreport.cpp:260-307.
bool CMailReport::ConnectMailServer() {
    bool result = false;
    char readyMessage[1024]{};

    if (!runtime::CreateSystemMailSocket(this)) {
        mSock = kInvalidSocket;
        [[maybe_unused]] const unsigned long error =
            LastSocketError();
        // Log.AddLog(LOG_ERROR,
        //     "component=mail client=report event=connection_failed stage=create_socket transport_error_snapshot=%lu action=return_false network_enabled=%s",
        //     error, runtime::SystemMailNetworkEnabled() ? "true" : "false")
        // Log.AddLog(LOG_ERROR,
        //     "[mail] Could not create a socket (last transport error %lu). Mail networking is %s. Connection setup failed.",
        //     error, runtime::SystemMailNetworkEnabled() ? "enabled" : "disabled")
        L2_LOG_ERROR(Log,
            "[mail] Could not create a socket (last transport error %lu). Mail networking is %s; connection setup failed.",
            error, runtime::SystemMailNetworkEnabled() ? "enabled" : "disabled");
        return false;
    }
    mSock = runtime::SystemMailSocketToken(this);

    if (!runtime::SetSystemMailReceiveTimeout(
            this,
            mSocketTimeout)) {
        [[maybe_unused]] const unsigned long error =
            LastSocketError();
        // Log.AddLog(LOG_ERROR,
        //     "component=mail client=report event=connection_failed stage=set_receive_timeout transport_error_snapshot=%lu action=return_false socket_cleanup=not_performed",
        //     error)
        // Log.AddLog(LOG_ERROR,
        //     "[mail] Could not set the receive timeout (last transport error %lu). Returning failure without socket cleanup.",
        //     error)
        L2_LOG_ERROR(Log,
            "[mail] Could not set the receive timeout (last transport error %lu). Returning failure without socket cleanup.",
            error);
        return false;
    }
    if (!runtime::SetSystemMailSendTimeout(
            this,
            mSocketTimeout)) {
        [[maybe_unused]] const unsigned long error =
            LastSocketError();
        // Log.AddLog(LOG_ERROR,
        //     "component=mail client=report event=connection_failed stage=set_send_timeout transport_error_snapshot=%lu action=return_false socket_cleanup=not_performed",
        //     error)
        // Log.AddLog(LOG_ERROR,
        //     "[mail] Could not set the send timeout (last transport error %lu). Returning failure without socket cleanup.",
        //     error)
        L2_LOG_ERROR(Log,
            "[mail] Could not set the send timeout (last transport error %lu). Returning failure without socket cleanup.",
            error);
        return false;
    }

    if (!runtime::ConnectSystemMailSocket(
            this,
            mServerAddr.sin_addr.s_addr,
            ntohs(mServerAddr.sin_port))) {
        [[maybe_unused]] const unsigned long error =
            LastSocketError();
        // Log.AddLog(LOG_ERROR,
        //     "component=mail client=report event=connection_failed stage=connect transport_error_snapshot=%lu action=return_false",
        //     error)
        // Log.AddLog(LOG_ERROR,
        //     "[mail] Could not connect to the relay (last transport error %lu). Returning failure.",
        //     error)
        L2_LOG_ERROR(Log,
            "[mail] Could not connect to the relay (last transport error %lu). Returning failure.",
            error);
    } else if (SocketRecvLine(
                   mSock,
                   readyMessage,
                   sizeof(readyMessage),
                   0)) {
        result = true;
    } else {
        [[maybe_unused]] const unsigned long error =
            LastSocketError();
    }

    if (result &&
        std::strncmp(
            readyMessage,
            REPLY_SERVICE_READY,
            std::strlen(REPLY_SERVICE_READY)) == 0) {
        return result;
    }

    if (result) {
        // Log.AddLog(LOG_ERROR,
        //     "component=mail client=report event=unexpected_greeting expected_code=220 received_code=%d action=close_socket return_value=true",
        //     MailReplyCode(readyMessage))
        // Log.AddLog(LOG_ERROR,
        //     "[mail] Unexpected SMTP greeting: expected 220, received %d (-1 means no valid code). Closing the socket. This path still returns true.",
        //     MailReplyCode(readyMessage))
        L2_LOG_ERROR(Log,
            "[mail] Unexpected SMTP greeting: expected 220, received %d (-1 means no valid code). Closing the socket; this path still returns true.",
            MailReplyCode(readyMessage));
    }

    // The selected image returns true after a successfully received but
    // mismatched greeting even though it closes the socket here.
    SocketClose();
    return result;
}

// L2Authd.exe 0x004679E0, Src/mailreport.cpp:416-427.
bool CMailReport::PacketHelo() {
    const char* command = "HELO\r\n";
    unsigned long lastError = 0;
    const bool result = SendPacket(
        command,
        REPLY_MAIL_ACTION_OK,
        &lastError);
    if (!result) {
        SocketClose();
    }
    return result;
}

// L2Authd.exe 0x00467A30, Src/mailreport.cpp:431-445.
bool CMailReport::PacketMailFrom() {
    const char* commandFormat = "MAIL FROM: <%s>\r\n";
    unsigned long lastError = 0;
    char command[512]{};
    StrPrintf(
        command,
        sizeof(command),
        commandFormat,
        mSender);
    const bool result = SendPacket(
        command,
        REPLY_MAIL_ACTION_OK,
        &lastError);
    if (!result) {
        SocketClose();
    }
    return result;
}

// L2Authd.exe 0x00467AF0, Src/mailreport.cpp:449-465.
bool CMailReport::PacketRcptTo() {
    const char* commandFormat = "RCPT TO: <%s>\r\n";
    unsigned long lastError = 0;
    char command[512]{};
    StrPrintf(
        command,
        sizeof(command),
        commandFormat,
        mReceiver);
    const bool result = SendPacket(
        command,
        REPLY_MAIL_ACTION_OK,
        &lastError);
    if (!result) {
        SocketClose();
    }
    return result;
}

// L2Authd.exe 0x00467BB0, Src/mailreport.cpp:471-731.
bool CMailReport::PacketData(
    const char* subject,
    const char* message) {
    const char* commandData = "DATA\r\n";
    const char* subjectFormat =
        "To: %s\r\nSubject: [%s] %s\r\n";
    const char* textDataFinish = "\r\n.\r\n";
    unsigned long lastError = 0;
    char mailTextBuffer[1024]{};

    if (!SendPacket(
            commandData,
            REPLY_ENTER_EMAIL,
            &lastError)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - Failed to send command-DATA, %d, %d",
        //     483,
        //     static_cast<int>(lastError))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=mail client=report event=data_command_failed transport_or_reply_error=%lu action=close_socket",
        //     lastError)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[mail] DATA command failed (transport or reply error %lu). Closing the socket.",
        //     lastError)
        L2_LOG_ERROR(Log,
            "[mail] DATA command failed (transport or reply error %lu). Closing the socket.",
            lastError);
        SocketClose();
        return false;
    }

    StrPrintf(
        mailTextBuffer,
        sizeof(mailTextBuffer),
        subjectFormat,
        mReceiver,
        mAppName,
        subject);
    StrCat(
        mailTextBuffer,
        "MIME-Version: 1.0\r\n",
        sizeof(mailTextBuffer));
    if (mAttachments.empty()) {
        StrCat(
            mailTextBuffer,
            "Content-type: text/plain; charset=US-ASCII\r\n",
            sizeof(mailTextBuffer));
        StrCat(
            mailTextBuffer,
            "Content-Transfer-Encoding: 7bit\r\n",
            sizeof(mailTextBuffer));
        StrCat(mailTextBuffer, "\r\n", sizeof(mailTextBuffer));
    } else {
        StrCat(
            mailTextBuffer,
            "Content-Type: multipart/mixed; boundary=\"",
            sizeof(mailTextBuffer));
        StrCat(mailTextBuffer, kMimeBoundary, sizeof(mailTextBuffer));
        StrCat(mailTextBuffer, "\"\r\n", sizeof(mailTextBuffer));
        StrCat(mailTextBuffer, "\r\n", sizeof(mailTextBuffer));
        StrCat(mailTextBuffer, "--", sizeof(mailTextBuffer));
        StrCat(mailTextBuffer, kMimeBoundary, sizeof(mailTextBuffer));
        StrCat(mailTextBuffer, "\r\n", sizeof(mailTextBuffer));
        StrCat(
            mailTextBuffer,
            "Content-type: text/plain; charset=US-ASCII\r\n",
            sizeof(mailTextBuffer));
        StrCat(
            mailTextBuffer,
            "Content-Transfer-Encoding: 7bit\r\n",
            sizeof(mailTextBuffer));
        StrCat(mailTextBuffer, "\r\n", sizeof(mailTextBuffer));
    }

    int textLength = static_cast<int>(StrLen(mailTextBuffer));
    if (!SocketSend(
            mSock,
            mailTextBuffer,
            textLength,
            0)) {
        lastError = LastSocketError();
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - Failed to perform SocketSend() %d %d",
        //     549,
        //     static_cast<int>(lastError))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=mail client=report event=data_write_failed stage=headers bytes=%d transport_error_snapshot=%lu action=close_socket",
        //     textLength, lastError)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[mail] Could not send message headers (%d bytes, transport error snapshot %lu). Closing the socket.",
        //     textLength, lastError)
        L2_LOG_ERROR(Log,
            "[mail] Could not send message headers (%d bytes, transport error snapshot %lu). Closing the socket.",
            textLength, lastError);
        SocketClose();
        return false;
    }

    const LocalSystemTime localTime = ReadLocalSystemTime();
    textLength = static_cast<int>(StrPrintf(
        mailTextBuffer,
        sizeof(mailTextBuffer),
        "==============================================\r\n"
        "IP Address     : %s \r\n"
        "Hostname       : %s \r\n"
        "Time           : %.4d/%.2d/%.2d %.2d:%.2d:%.2d \r\n"
        "ProductVersion\t: %s \r\n"
        "BuildNumber    : %s \r\n"
        "==============================================\r\n\r\n\r\n",
        ::inet_ntoa(mLocalIPAddr),
        mLocalHostname,
        localTime.year,
        localTime.month,
        localTime.day,
        localTime.hour,
        localTime.minute,
        localTime.second,
        m_productVersion.c_str(),
        m_buildNumber.c_str()));
    if (!SocketSend(
            mSock,
            mailTextBuffer,
            textLength,
            0)) {
        lastError = LastSocketError();
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - Failed to perform SocketSend() %d %d",
        //     576,
        //     static_cast<int>(lastError))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=mail client=report event=data_write_failed stage=server_metadata bytes=%d transport_error_snapshot=%lu action=close_socket",
        //     textLength, lastError)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[mail] Could not send server metadata (%d bytes, transport error snapshot %lu). Closing the socket.",
        //     textLength, lastError)
        L2_LOG_ERROR(Log,
            "[mail] Could not send server metadata (%d bytes, transport error snapshot %lu). Closing the socket.",
            textLength, lastError);
        SocketClose();
        return false;
    }

    std::memset(mailTextBuffer, 0, sizeof(mailTextBuffer));
    char mailBody[4096]{};
    textLength = static_cast<int>(StrPrintf(
        mailBody,
        sizeof(mailBody),
        "%s\r\n\r\n",
        message));
    if (!SocketSend(mSock, mailBody, textLength, 0)) {
        lastError = LastSocketError();
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - Failed to perform SocketSend() %d %d",
        //     588,
        //     static_cast<int>(lastError))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=mail client=report event=data_write_failed stage=body bytes=%d transport_error_snapshot=%lu action=close_socket",
        //     textLength, lastError)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[mail] Could not send message body (%d bytes, transport error snapshot %lu). Closing the socket.",
        //     textLength, lastError)
        L2_LOG_ERROR(Log,
            "[mail] Could not send message body (%d bytes, transport error snapshot %lu). Closing the socket.",
            textLength, lastError);
        SocketClose();
        return false;
    }

    const int fileBufferSize = 55;
    const int filePathSize = 1024;
    const int sendBufferSize = 10240;
    char* fileBuffer = new (std::nothrow) char[fileBufferSize];
    char* fileName = new (std::nothrow) char[filePathSize];
    char* sendBuffer = new (std::nothrow) char[sendBufferSize];
    unsigned int totalSize = 0;
    if (fileBuffer == nullptr ||
        fileName == nullptr ||
        sendBuffer == nullptr) {
        lastError = LastSystemError();
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - Failed to allocate memory: %ld",
        //     static_cast<long>(lastError))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=mail client=report event=allocation_failed stage=attachments requested_bytes=11319 action=close_socket cleanup=legacy_partial_allocation_retained")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[mail] Could not allocate 11319 bytes for attachment buffers. Closing the socket, any partial allocation is retained.")
        L2_LOG_ERROR(Log,
            "[mail] Could not allocate 11319 bytes for attachment buffers. Closing the socket; any partial allocation is retained.");
        SocketClose();
        return false;
    }

    std::memset(fileBuffer, 0, fileBufferSize);
    std::memset(fileName, 0, filePathSize);
    std::memset(sendBuffer, 0, sendBufferSize);

    for (unsigned int fileId = 0;
         fileId < mAttachments.size();
         ++fileId) {
        std::strcpy(
            fileName,
            mAttachments[fileId].c_str());
        std::snprintf(sendBuffer, sendBufferSize, "--%s\r\n", kMimeBoundary);
        std::strcat(
            sendBuffer,
            "Content-Type: application/x-msdownload; name=\"");
        std::size_t filenameOffset =
            mAttachments[fileId].find_last_of("\\") + 1;
        std::strcat(sendBuffer, fileName + filenameOffset);
        std::strcat(sendBuffer, "\"\r\n");
        std::strcat(
            sendBuffer,
            "Content-Transfer-Encoding: base64\r\n");
        std::strcat(
            sendBuffer,
            "Content-Disposition: attachment; pFileName=\"");
        filenameOffset =
            mAttachments[fileId].find_last_of("\\") + 1;
        std::strcat(sendBuffer, fileName + filenameOffset);
        std::strcat(sendBuffer, "\"\r\n");
        std::strcat(sendBuffer, "\r\n");

        textLength = static_cast<int>(StrLen(sendBuffer));
        if (!SocketSend(mSock, sendBuffer, textLength, 0)) {
            lastError = LastSocketError();
            // Log.AddLog(
            //     LOG_ERROR,
            //     "ReportMail - Failed to perform SocketSend() %d %d",
            //     635,
            //     static_cast<int>(lastError))
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=mail client=report event=data_write_failed stage=attachment_headers bytes=%d transport_error_snapshot=%lu action=close_socket",
            //     textLength, lastError)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[mail] Could not send attachment headers (%d bytes, transport error snapshot %lu). Closing the socket.",
            //     textLength, lastError)
            L2_LOG_ERROR(Log,
                "[mail] Could not send attachment headers (%d bytes, transport error snapshot %lu). Closing the socket.",
                textLength, lastError);
            SocketClose();
            return false;
        }

        std::FILE* const file = std::fopen(fileName, "rb");
        if (file == nullptr) {
            lastError = LastSystemError();
            // Log.AddLog(
            //     LOG_ERROR,
            //     "ReportMail - Failed to open file: %ld, %s",
            //     static_cast<long>(lastError),
            //     fileName)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=mail client=report event=attachment_open_failed path_preview=\"%.512s\" error_domain=errno code=%d action=close_socket",
            //     fileName, errno)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[mail] Could not open attachment (path preview \"%.512s\", errno %d). Closing the socket.",
            //     fileName, errno)
            L2_LOG_ERROR(Log,
                "[mail] Could not open attachment (path preview \"%.512s\", errno %d). Closing the socket.",
                fileName, errno);
            SocketClose();
            return false;
        }

        unsigned int fileSize = 0;
        while (std::feof(file) == 0) {
            fileSize += static_cast<unsigned int>(std::fread(
                fileBuffer,
                1,
                54,
                file));
        }
        totalSize += fileSize;
        if ((totalSize >> 10U) > 0x1400U) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "ReportMail - message too big !!!")
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=mail client=report event=attachment_limit_exceeded total_raw_bytes_u32=%u threshold_kib=5120 comparison=floor_kib_greater action=close_file_and_socket",
            //     totalSize)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[mail] Attachments total %u raw bytes. Size rounded down to KiB exceeds 5120 KiB. Closing the file and socket.",
            //     totalSize)
            L2_LOG_ERROR(Log,
                "[mail] Attachments total %u raw bytes; size rounded down to KiB exceeds 5120 KiB. Closing the file and socket.",
                totalSize);
            std::fclose(file);
            SocketClose();
            return false;
        }

        std::fseek(file, 0, SEEK_SET);
        unsigned int messagePart = 0;
        for (unsigned int i = 0;
             i < fileSize / 54U + 1U;
             ++i) {
            const unsigned int read =
                static_cast<unsigned int>(std::fread(
                    fileBuffer,
                    1,
                    54,
                    file));
            Base64Encode(
                read,
                reinterpret_cast<unsigned char*>(fileBuffer),
                sizeof(mailTextBuffer),
                mailTextBuffer);
            if (messagePart == 0) {
                std::strcpy(sendBuffer, mailTextBuffer);
            } else {
                std::strcat(sendBuffer, mailTextBuffer);
            }
            std::strcat(sendBuffer, "\r\n");
            messagePart += read + 2U;
            if (messagePart >=
                static_cast<unsigned int>(sendBufferSize / 2)) {
                messagePart = 0;
                textLength = static_cast<int>(StrLen(sendBuffer));
                if (!SocketSend(
                        mSock,
                        sendBuffer,
                        textLength,
                        0)) {
                    lastError = LastSocketError();
                    // Log.AddLog(
                    //     LOG_ERROR,
                    //     "ReportMail - Failed to perform SocketSend() %d %d",
                    //     683,
                    //     static_cast<int>(lastError))
                    // Log.AddLog(
                    //     LOG_ERROR,
                    //     "component=mail client=report event=data_write_failed stage=attachment_chunk bytes=%d transport_error_snapshot=%lu action=close_socket",
                    //     textLength, lastError)
                    // Log.AddLog(
                    //     LOG_ERROR,
                    //     "[mail] Could not send attachment chunk (%d bytes, transport error snapshot %lu). Closing the socket.",
                    //     textLength, lastError)
                    L2_LOG_ERROR(Log,
                        "[mail] Could not send attachment chunk (%d bytes, transport error snapshot %lu). Closing the socket.",
                        textLength, lastError);
                    std::fclose(file);
                    SocketClose();
                    return false;
                }
            }
        }

        if (messagePart > 0) {
            textLength = static_cast<int>(StrLen(sendBuffer));
            if (!SocketSend(
                    mSock,
                    sendBuffer,
                    textLength,
                    0)) {
                lastError = LastSocketError();
                // Log.AddLog(
                //     LOG_ERROR,
                //     "ReportMail - Failed to perform SocketSend() %d %d",
                //     696,
                //     static_cast<int>(lastError))
                // Log.AddLog(
                //     LOG_ERROR,
                //     "component=mail client=report event=data_write_failed stage=attachment_tail bytes=%d transport_error_snapshot=%lu action=close_socket",
                //     textLength, lastError)
                // Log.AddLog(
                //     LOG_ERROR,
                //     "[mail] Could not send attachment tail (%d bytes, transport error snapshot %lu). Closing the socket.",
                //     textLength, lastError)
                L2_LOG_ERROR(Log,
                    "[mail] Could not send attachment tail (%d bytes, transport error snapshot %lu). Closing the socket.",
                    textLength, lastError);
                std::fclose(file);
                SocketClose();
                return false;
            }
        }
        std::fclose(file);
    }

    std::memset(fileBuffer, 0, fileBufferSize);
    delete[] fileBuffer;
    std::memset(fileName, 0, filePathSize);
    delete[] fileName;
    std::memset(sendBuffer, 0, sendBufferSize);
    delete[] sendBuffer;
    sendBuffer = nullptr;

    if (!mAttachments.empty()) {
        textLength = static_cast<int>(StrPrintf(
            mailTextBuffer,
            sizeof(mailTextBuffer),
            "\r\n--%s--\r\n",
            kMimeBoundary));
        if (!SocketSend(
                mSock,
                mailTextBuffer,
                textLength,
                0)) {
            lastError = LastSocketError();
            // Log.AddLog(
            //     LOG_ERROR,
            //     "ReportMail - Failed to perform SocketSend() %d %d",
            //     716,
            //     static_cast<int>(lastError))
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=mail client=report event=data_write_failed stage=multipart_terminator bytes=%d transport_error_snapshot=%lu action=close_socket",
            //     textLength, lastError)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[mail] Could not send multipart terminator (%d bytes, transport error snapshot %lu). Closing the socket.",
            //     textLength, lastError)
            L2_LOG_ERROR(Log,
                "[mail] Could not send multipart terminator (%d bytes, transport error snapshot %lu). Closing the socket.",
                textLength, lastError);
            SocketClose();
            return false;
        }
    }

    if (!SendPacket(
            textDataFinish,
            REPLY_MAIL_ACTION_OK,
            &lastError)) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - Failed to perform SendPacket() %d %d",
        //     725,
        //     static_cast<int>(lastError))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=mail client=report event=data_commit_failed transport_or_reply_error=%lu relay_acceptance=unconfirmed action=close_socket",
        //     lastError)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[mail] Could not complete DATA (transport or reply error %lu). Relay acceptance is not confirmed. Closing the socket.",
        //     lastError)
        L2_LOG_ERROR(Log,
            "[mail] Could not complete DATA (transport or reply error %lu). Relay acceptance is not confirmed; closing the socket.",
            lastError);
        SocketClose();
        return false;
    }
    return true;
}

// L2Authd.exe 0x004688E0, Src/mailreport.cpp:735-748.
bool CMailReport::PacketQuit() {
    const char* command = "QUIT\r\n";
    unsigned long lastError = 0;
    const bool result = SendPacket(
        command,
        nullptr,
        &lastError);
    SocketClose();
    return result;
}

// L2Authd.exe 0x00468930, Src/mailreport.cpp:755-807.
bool CMailReport::SendPacket(
    const char* packet,
    const char* expectedReply,
    unsigned long* receivedReply) {
    char reply[512]{};
    if (packet == nullptr) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - SendPacket(). pszCommand is NULL. %d",
        //     760)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=mail client=report event=command_rejected reason=null_command action=return_false")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[mail] Cannot submit a null SMTP command. Returning failure.")
        L2_LOG_ERROR(Log,
            "[mail] Cannot submit a null SMTP command. Returning failure.");
        return false;
    }
    if (mSock == kInvalidSocket) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - SendPacket(). invalid socket. %d",
        //     766)
        // Log.AddLog(
        //     LOG_ERROR,
        //     "component=mail client=report event=command_rejected stage=%s reason=closed_transport action=return_false",
        //     MailCommandStage(packet))
        // Log.AddLog(
        //     LOG_ERROR,
        //     "[mail] Cannot submit SMTP step %s: the transport is closed. Returning failure.",
        //     MailCommandStage(packet))
        L2_LOG_ERROR(Log,
            "[mail] Cannot submit SMTP step %s: the transport is closed. Returning failure.",
            MailCommandStage(packet));
        return false;
    }

    if (!SocketSend(
            mSock,
            packet,
            static_cast<int>(StrLen(packet)),
            0)) {
        if (receivedReply != nullptr) {
            *receivedReply = LastSocketError();
        }
        // Noise: SocketSend reports transport progress/failure and the caller reports the SMTP stage. Omit this duplicate.
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - Failed to perform SocketSend() %d",
        //     776)
        return false;
    }

    if (expectedReply == nullptr) {
        return true;
    }

    if (!SocketRecvLine(
            mSock,
            reply,
            sizeof(reply),
            0)) {
        if (receivedReply != nullptr) {
            *receivedReply = LastSocketError();
        }
        // Noise: SocketRecvLine reports transport progress/failure and the caller reports the SMTP stage. Omit this duplicate.
        // Log.AddLog(
        //     LOG_ERROR,
        //     "ReportMail - Failed to perform SocketRecvLine() %d",
        //     788)
        return false;
    }

    const int expectedLength =
        StrLen(expectedReply) < sizeof(reply)
        ? static_cast<int>(StrLen(expectedReply))
        : static_cast<int>(sizeof(reply));
    if (std::strncmp(
            reply,
            expectedReply,
            expectedLength) == 0) {
        return true;
    }

    if (receivedReply != nullptr) {
        *receivedReply = 0;
    }
    // Noise: The following single reply diagnostic includes the SMTP stage and validated numeric codes.
    // Log.AddLog(
    //     LOG_ERROR,
    //     "ReportMail - mismatched reply code!!!")
    const int actualReply =
        (reply[0] - '0') * 100 +
        (reply[1] - '0') * 10 +
        (reply[2] - '0');
    // Log.AddLog(
    //     LOG_ERROR,
    //     "ReportMail - check smtp server reply code : %d - %d",
    //     std::atoi(expectedReply),
    //     actualReply)
    // Log.AddLog(
    //     LOG_ERROR,
    //     "component=mail client=report event=smtp_reply_rejected stage=%s expected_code=%d received_code=%d invalid_code=-1 action=return_false",
    //     MailCommandStage(packet), MailReplyCode(expectedReply),
    //     MailReplyCode(reply) < 0 ? -1 : actualReply)
    // Log.AddLog(
    //     LOG_ERROR,
    //     "[mail] SMTP step %s rejected: expected reply %d, received %d (-1 means no valid code). Returning failure.",
    //     MailCommandStage(packet), MailReplyCode(expectedReply),
    //     MailReplyCode(reply) < 0 ? -1 : actualReply)
    L2_LOG_ERROR(Log,
        "[mail] SMTP step %s rejected: expected reply %d, received %d (-1 means no valid code). Returning failure.",
        MailCommandStage(packet), MailReplyCode(expectedReply),
        MailReplyCode(reply) < 0 ? -1 : actualReply);
    return false;
}

// L2Authd.exe 0x00468B90, Src/mailreport.cpp:815-832.
bool CMailReport::SocketSend(
    [[maybe_unused]] unsigned int socket,
    const char* buffer,
    int length,
    [[maybe_unused]] int flags) {
    int sent = 0;
    while (sent < length) {
        const int result =
            runtime::SendSystemMailSocket(
            this,
            buffer + sent,
            static_cast<std::size_t>(length - sent));
        if (result == -1 || result == 0) {
            const unsigned long error = LastSocketError();
            // Log.AddLog(
            //     LOG_ERROR,
            //     "ReportMail - Failed to send, nRet=%d, dwLastError=%d",
            //     result,
            //     static_cast<int>(error))
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=mail client=report event=socket_write_failed result=%d sent_bytes=%d requested_bytes=%d reason=%s transport_error_snapshot=%lu action=return_false",
            //     result, sent, length, result == 0 ? "no_progress_or_closed_transport" : "transport_error", result == 0 ? 0UL : error)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[mail] Write returned %d after %d of %d bytes: %s (transport error snapshot %lu). Returning failure.",
            //     result, sent, length, result == 0 ? "no progress or closed transport" : "transport error", result == 0 ? 0UL : error)
            L2_LOG_ERROR(Log,
                "[mail] Write returned %d after %d of %d bytes: %s (transport error snapshot %lu). Returning failure.",
                result, sent, length, result == 0 ? "no progress or closed transport" : "transport error", result == 0 ? 0UL : error);
            return false;
        }
        sent += result;
    }
    return true;
}

// L2Authd.exe 0x00468C10, Src/mailreport.cpp:840-873.
bool CMailReport::SocketRecvLine(
    [[maybe_unused]] unsigned int socket,
    char* buffer,
    int length,
    [[maybe_unused]] int flags) {
    int received = 0;
    while (true) {
        const int result =
            runtime::ReceiveSystemMailSocket(
            this,
            buffer + received,
            static_cast<std::size_t>(length - received));
        if (result == -1 || result == 0) {
            const unsigned long error = LastSocketError();
            // Log.AddLog(
            //     LOG_ERROR,
            //     "ReportMail - Failed to recv, nRet=%d, dwLastError=%d",
            //     result,
            //     static_cast<int>(error))
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=mail client=report event=socket_read_failed result=%d received_bytes=%d capacity=%d reason=%s transport_error_snapshot=%lu action=return_false",
            //     result, received, length, result == 0 ? "eof_or_closed_transport" : "transport_error", result == 0 ? 0UL : error)
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[mail] Read returned %d after %d bytes into a %d-byte buffer: %s (transport error snapshot %lu). Returning failure.",
            //     result, received, length, result == 0 ? "end of input or closed transport" : "transport error", result == 0 ? 0UL : error)
            L2_LOG_ERROR(Log,
                "[mail] Read returned %d after %d bytes into a %d-byte buffer: %s (transport error snapshot %lu). Returning failure.",
                result, received, length, result == 0 ? "end of input or closed transport" : "transport error", result == 0 ? 0UL : error);
            return false;
        }
        received += result;
        if (received >= length) {
            buffer[length - 1] = '\0';
            return true;
        }
        if (std::strstr(buffer, "\r\n") != nullptr) {
            return true;
        }
    }
}
