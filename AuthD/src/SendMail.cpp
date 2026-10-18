#include "SendMail.h"

#include "Shared/src/Runtime/BoundedFormat.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Config.h"
#include "Exception.h"
#include "Log.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <WinSock2.h>
#include <Windows.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

SendMail Mailservice;

namespace {

constexpr std::uint32_t kInvalidSocket = UINT32_C(0xffffffff);
constexpr int kMailSocketTimeoutMilliseconds = 5000;

void ReportMailSocketFailure(const char* operation, const char* outcome) {
#if defined(_WIN32)
    const int error = ::WSAGetLastError();
    L2_LOG_ERROR(Log, "[legacy mail] %s failed: WinSock error %d. %s", operation, error, outcome);
    ::WSASetLastError(error);
#else
    const int error = errno;
    L2_LOG_ERROR(Log, "[legacy mail] %s failed: errno %d. %s", operation, error, outcome);
    errno = error;
#endif
}

int SendOnce(
    std::uint32_t socket,
    const char* data,
    int length) {
#if defined(_WIN32)
    return ::send(
        static_cast<SOCKET>(socket),
        data,
        length,
        0);
#else
    return static_cast<int>(::send(
        static_cast<int>(socket),
        data,
        static_cast<std::size_t>(length),
        0));
#endif
}

int ReceiveOnce(
    std::uint32_t socket,
    char* data,
    int capacity) {
#if defined(_WIN32)
    return ::recv(
        static_cast<SOCKET>(socket),
        data,
        capacity,
        0);
#else
    return static_cast<int>(::recv(
        static_cast<int>(socket),
        data,
        static_cast<std::size_t>(capacity),
        0));
#endif
}

void CloseNativeSocket(std::uint32_t socket) {
    linger immediateClose{};
    immediateClose.l_onoff = 1;
    immediateClose.l_linger = 0;
#if defined(_WIN32)
    ::setsockopt(
        static_cast<SOCKET>(socket),
        SOL_SOCKET,
        SO_LINGER,
        reinterpret_cast<const char*>(&immediateClose),
        sizeof(immediateClose));
    ::closesocket(static_cast<SOCKET>(socket));
#else
    ::setsockopt(
        static_cast<int>(socket),
        SOL_SOCKET,
        SO_LINGER,
        &immediateClose,
        sizeof(immediateClose));
    ::close(static_cast<int>(socket));
#endif
}

bool SetSocketTimeouts(std::uint32_t socket) {
#if defined(_WIN32)
    const int timeout = kMailSocketTimeoutMilliseconds;
    return ::setsockopt(
               static_cast<SOCKET>(socket),
               SOL_SOCKET,
               SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout),
               sizeof(timeout)) != SOCKET_ERROR &&
           ::setsockopt(
               static_cast<SOCKET>(socket),
               SOL_SOCKET,
               SO_SNDTIMEO,
               reinterpret_cast<const char*>(&timeout),
               sizeof(timeout)) != SOCKET_ERROR;
#else
    timeval timeout{};
    timeout.tv_sec =
        kMailSocketTimeoutMilliseconds / 1000;
    timeout.tv_usec =
        (kMailSocketTimeoutMilliseconds % 1000) * 1000;
    return ::setsockopt(
               static_cast<int>(socket),
               SOL_SOCKET,
               SO_RCVTIMEO,
               &timeout,
               sizeof(timeout)) != -1 &&
           ::setsockopt(
               static_cast<int>(socket),
               SOL_SOCKET,
               SO_SNDTIMEO,
               &timeout,
               sizeof(timeout)) != -1;
#endif
}

} // namespace

// L2Authd.exe 0x0047B510, Src/sendmail.cpp:11-13.
SendMail::SendMail() {
    mSock = kInvalidSocket;
}

// L2Authd.exe 0x0047B530, Src/sendmail.cpp:16-17.
SendMail::~SendMail() = default;

// L2Authd.exe 0x0047B540, Src/sendmail.cpp:20-29.
void SendMail::SocketClose() {
    const std::uint32_t temporarySocket =
        runtime::raw_atomic::ExchangeU32(
            &mSock,
            kInvalidSocket);
    if (temporarySocket != kInvalidSocket) {
#if defined(_WIN32)
        linger immediateClose{};
        immediateClose.l_onoff = 1;
        immediateClose.l_linger = 0;
        ::setsockopt(
            static_cast<SOCKET>(temporarySocket),
            SOL_SOCKET,
            SO_LINGER,
            reinterpret_cast<const char*>(&immediateClose),
            sizeof(immediateClose));
        ::closesocket(
            static_cast<SOCKET>(temporarySocket));
#else
        CloseNativeSocket(temporarySocket);
#endif
    }
}

// L2Authd.exe 0x0047B5A0, Src/sendmail.cpp:32-51.
int SendMail::Init() {
    const int length = static_cast<int>(
        std::strlen(config.mailServer));
    if (length < 1 || length > 128) {
        L2_LOG_ERROR(Log,
            "[legacy mail] Cannot initialize the relay: mailServer length is %d bytes; expected 1..128. Initialization returns zero.",
            length);
        return 0;
    }

    hostent* const host =
        ::gethostbyname(config.mailServer);
    if (host == nullptr ||
        host->h_addr_list == nullptr ||
        host->h_addr_list[0] == nullptr) {
        L2_LOG_ERROR(Log,
            "[legacy mail] Cannot initialize relay '%s': hostname lookup returned no usable address. Initialization returns zero.",
            config.mailServer);
        return 0;
    }

    std::array<char, 128> mailAddress{};
    in_addr resolvedAddress{};
    std::memcpy(
        &resolvedAddress,
        host->h_addr_list[0],
        sizeof(resolvedAddress));
    std::strncpy(
        mailAddress.data(),
        ::inet_ntoa(resolvedAddress),
        20);

    mAddr.sin_family = AF_INET;
    mAddr.sin_port = htons(25);
    mAddr.sin_addr.s_addr =
        ::inet_addr(mailAddress.data());
    return 1;
}

// L2Authd.exe 0x0047B6A0, Src/sendmail.cpp:54-73.
char* SendMail::PrintErrorMessage(
    int errorCode,
    char* errorMessage) {
#if defined(_WIN32)
    char* buffer = nullptr;
    const unsigned long result = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<unsigned long>(errorCode),
        0,
        reinterpret_cast<char*>(&buffer),
        0,
        nullptr);
    if (result == 0 || result + 14 > 1024) {
        errorMessage[0] = '\0';
    } else {
        const std::size_t length = std::strlen(buffer);
        if (length >= 2) {
            buffer[length - 2] = '\0';
        }
        std::snprintf(
            errorMessage,
            1024,
            "%s(0x%x)",
            buffer,
            errorCode);
    }
    if (buffer != nullptr) {
        ::LocalFree(buffer);
    }
#else
    const char* const buffer = std::strerror(errorCode);
    if (buffer == nullptr ||
        std::strlen(buffer) + 14 > 1024) {
        errorMessage[0] = '\0';
    } else {
        std::snprintf(
            errorMessage,
            1024,
            "%s(0x%x)",
            buffer,
            errorCode);
    }
#endif
    return errorMessage;
}

// L2Authd.exe 0x0047B740, Src/sendmail.cpp:76-118.
int SendMail::ConnectMailServer() {
#if defined(_WIN32)
    const SOCKET socket = ::socket(AF_INET, SOCK_STREAM, 0);
    mSock = static_cast<std::uint32_t>(socket);
#else
    const int socket = ::socket(AF_INET, SOCK_STREAM, 0);
    mSock = socket == -1
        ? kInvalidSocket
        : static_cast<std::uint32_t>(socket);
#endif
    if (mSock == kInvalidSocket) {
        ReportMailSocketFailure("Creating the SMTP socket", "Connection setup returns zero.");
        return 0;
    }

    // The selected target returns immediately on either setsockopt failure.
    // it does not close the descriptor on these two paths.
    if (!SetSocketTimeouts(mSock)) {
        ReportMailSocketFailure("Setting SMTP socket timeouts",
            "Connection setup returns zero; the socket remains open on this path.");
        return 0;
    }

    std::array<char, 1024> messageBuffer{};
    int result = 0;
    if (::connect(
#if defined(_WIN32)
            static_cast<SOCKET>(mSock),
#else
            static_cast<int>(mSock),
#endif
            reinterpret_cast<const sockaddr*>(&mAddr),
            sizeof(mAddr)) != -1 &&
        ReceiveOnce(
            mSock,
            messageBuffer.data(),
            static_cast<int>(messageBuffer.size())) != -1) {
        result = 1;
    }

    if (result == 0) {
        ReportMailSocketFailure("Connecting to the SMTP relay or reading its greeting",
            "Closing the socket; connection setup returns zero.");
        SocketClose();
    } else if (std::strncmp(
                   messageBuffer.data(),
                   "220",
                   3) != 0) {
        char replyCode[12] = "unavailable";
        if (messageBuffer[0] >= '0' && messageBuffer[0] <= '9' &&
            messageBuffer[1] >= '0' && messageBuffer[1] <= '9' &&
            messageBuffer[2] >= '0' && messageBuffer[2] <= '9') {
            std::memcpy(replyCode, messageBuffer.data(), 3);
            replyCode[3] = '\0';
        }
        L2_LOG_ERROR(Log,
            "[legacy mail] Relay greeting status: %s; expected 220. Closing the socket; connection setup returns zero.",
            replyCode);
        SocketClose();
        result = 0;
    }
    return result;
}

// L2Authd.exe 0x0047B900, Src/sendmail.cpp:121-161.
int SendMail::SendMessageA(
    char* receiver,
    char* mailTitle,
    char* message) {
    int result = 0;
    const char* diagnosticStage = "validation";
    try {
        int length = static_cast<int>(
            std::strlen(mailTitle));
        if (length < 1 || length > 1024) {
            // Log.AddLog(LOG_ERROR,
            //     "component=mail client=legacy event=send_rejected field=subject bytes=%d min_bytes=1 max_bytes=1024 action=return_zero",
            //     length)
            // Log.AddLog(LOG_ERROR,
            //     "[legacy mail] Invalid subject length: %d bytes. Expected 1..1024. Returning zero.",
            //     length)
            L2_LOG_ERROR(Log,
                "[legacy mail] Invalid subject length: %d bytes; expected 1..1024. Returning zero.",
                length);
            return 0;
        }
        length = static_cast<int>(
            std::strlen(message));
        if (length < 1 || length > 1024) {
            // Log.AddLog(LOG_ERROR,
            //     "component=mail client=legacy event=send_rejected field=body bytes=%d min_bytes=1 max_bytes=1024 action=return_zero",
            //     length)
            // Log.AddLog(LOG_ERROR,
            //     "[legacy mail] Invalid body length: %d bytes. Expected 1..1024. Returning zero.",
            //     length)
            L2_LOG_ERROR(Log,
                "[legacy mail] Invalid body length: %d bytes; expected 1..1024. Returning zero.",
                length);
            return 0;
        }

        if (std::strchr(config.mailFrom, '@') != nullptr &&
            std::strchr(receiver, '@') != nullptr &&
            std::strlen(config.mailFrom) > 4 &&
            std::strlen(config.mailFrom) < 1024) {
            length = static_cast<int>(
                std::strlen(receiver));
            if (length > 4 && length < 1024) {
                std::strcpy(mReceiver, receiver);
                mReceiver[length] = '\0';
                diagnosticStage = "connect";
                if (ConnectMailServer() == 0) {
                    // Log.AddLog(LOG_ERROR,
                    //     "component=mail client=legacy event=send_failed stage=connect action=return_zero")
                    // Log.AddLog(LOG_ERROR,
                    //     "[legacy mail] Could not connect to the relay. Returning zero.")
                    L2_LOG_ERROR(Log,
                        "[legacy mail] Could not connect to the relay. Returning zero.");
                    return 0;
                }

                diagnosticStage = "helo";
                result = PacketHelo();
                if (result != 0) {
                    diagnosticStage = "mail_from";
                    result = PacketMailFrom();
                }
                if (result != 0) {
                    diagnosticStage = "rcpt_to";
                    result = PacketRcptTo();
                }
                if (result != 0) {
                    diagnosticStage = "data";
                    result = PacketData(
                        mailTitle,
                        message);
                }
                if (result != 0) {
                    diagnosticStage = "quit";
                    result = PacketQuit();
                }
                if (result == 0) {
                    // Log.AddLog(
                    //     LOG_ERROR,
                    //     "mailsend error , %s",
                    //     message)
                    // Log.AddLog(
                    //     LOG_ERROR,
                    //     "component=mail client=legacy event=send_failed stage=%s relay_acceptance=%s body=redacted action=return_zero",
                    //     diagnosticStage, std::strcmp(diagnosticStage, "quit") == 0 ? "confirmed" : "unconfirmed")
                    // Log.AddLog(
                    //     LOG_ERROR,
                    //     "[legacy mail] SMTP step %s failed. Relay acceptance is %s. Returning zero.",
                    //     diagnosticStage, std::strcmp(diagnosticStage, "quit") == 0 ? "confirmed" : "unconfirmed")
                    L2_LOG_ERROR(Log,
                        "[legacy mail] SMTP step %s failed; relay acceptance is %s. Returning zero.",
                        diagnosticStage, std::strcmp(diagnosticStage, "quit") == 0 ? "confirmed" : "unconfirmed");
                } else {
                    // Log.AddLog(LOG_NORMAL,
                    //     "component=mail client=legacy event=smtp_transaction_completed relay_acceptance=confirmed recipient_acceptance=at_least_one quit_reply=not_read delivery=unconfirmed")
                    // Log.AddLog(LOG_NORMAL,
                    //     "[legacy mail] Relay accepted the message for at least one recipient. QUIT reply was not read, final delivery is not confirmed.")
                    // L2_LOG_INFO(Log,
                    //     "[legacy mail] Relay accepted the message for at least one recipient. QUIT reply was not read, final delivery is not confirmed.")
                    // Noise: relay acceptance already states the observed outcome, without claiming mailbox delivery.
                    L2_LOG_INFO(Log,
                        "[legacy mail] Relay accepted the message for at least one recipient.");
                }
            } else {
                // Log.AddLog(LOG_ERROR,
                //     "component=mail client=legacy event=send_rejected field=receiver bytes=%d min_bytes=5 max_bytes=1023 action=return_zero",
                //     length)
                // Log.AddLog(LOG_ERROR,
                //     "[legacy mail] Invalid recipient address length: %d bytes. Expected 5..1023. Returning zero.",
                //     length)
                L2_LOG_ERROR(Log,
                    "[legacy mail] Invalid recipient address length: %d bytes; expected 5..1023. Returning zero.",
                    length);
            }
        } else {
            // Log.AddLog(LOG_ERROR,
            //     "component=mail client=legacy event=send_rejected field=envelope reason=missing_at_sign_or_sender_length action=return_zero")
            // Log.AddLog(LOG_ERROR,
            //     "[legacy mail] Invalid mail envelope: sender or recipient lacks an @ sign, or sender length is outside 5..1023 bytes. Returning zero.")
            L2_LOG_ERROR(Log,
                "[legacy mail] Invalid mail envelope: sender or recipient lacks an @ sign, or sender length is outside 5..1023 bytes. Returning zero.");
        }
    } catch (...) {
        // Log.AddLog(LOG_ERROR,
        //     "component=mail client=legacy event=send_exception stage=%s result_code=%d action=invoke_exception_handler",
        //     diagnosticStage, result)
        // Log.AddLog(LOG_ERROR,
        //     "[legacy mail] Exception during %s (last result %d). Invoking the exception handler.",
        //     diagnosticStage, result)
        // Noise: ELOG immediately below reports the same stage and result
        // before entering crash handling. A second record adds no context.
        // L2_LOG_ERROR(Log,
        //     "[legacy mail] Exception during %s (last result %d). Invoking the exception handler.",
        //     diagnosticStage, result)
        // ELOG(
        //     "*%s:%d(%s) exception\n",
        //     "SendMail.cpp",
        //     159,
        //     "Wed Oct 23 17:02:11 2019")
        ELOG(
            "[SendMail.cpp] Exception during legacy mail %s (last result %d). Entering exception handling.\n",
            diagnosticStage, result);
        send_exception(true);
    }
    return result;
}

// L2Authd.exe 0x0047BB50, Src/sendmail.cpp:164-196.
int SendMail::PacketHelo() {
    std::array<char, 1024> messageBuffer{};
    if (mSock == kInvalidSocket) {
        return 0;
    }

    std::strcpy(
        messageBuffer.data(),
        "HELO mail.ncsoft.net\r\n");
    int result = 0;
    if (SendOnce(
            mSock,
            messageBuffer.data(),
            static_cast<int>(
                std::strlen(messageBuffer.data()))) != -1) {
        messageBuffer.fill('\0');
        if (ReceiveOnce(
                mSock,
                messageBuffer.data(),
                static_cast<int>(messageBuffer.size())) != -1) {
            result = 1;
        }
    }

    if (result == 0) {
        ReportMailSocketFailure("Sending SMTP HELO or receiving its reply",
            "Closing the socket; HELO returns zero.");
        SocketClose();
    } else if (std::strncmp(
                   messageBuffer.data(),
                   "250",
                   3) != 0) {
        SocketClose();
        result = 0;
    }
    return result;
}

// L2Authd.exe 0x0047BCC0, Src/sendmail.cpp:199-230.
int SendMail::PacketMailFrom() {
    std::array<char, 1024> messageBuffer{};
    if (mSock == kInvalidSocket) {
        return 0;
    }

    // Source bug: the configured sender was formatted without a bound.
    const int length = runtime::BoundedFormat(
        messageBuffer.data(),
        messageBuffer.size(),
        "MAIL From:<%s>\r\n",
        config.mailFrom);
    int result = 0;
    if (length >= 0 && SendOnce(
            mSock,
            messageBuffer.data(),
            length) != -1) {
        messageBuffer.fill('\0');
        if (ReceiveOnce(
                mSock,
                messageBuffer.data(),
                static_cast<int>(messageBuffer.size())) != -1) {
            result = 1;
        }
    }

    if (result == 0) {
        ReportMailSocketFailure("Sending SMTP MAIL FROM or receiving its reply",
            "Closing the socket; MAIL FROM returns zero.");
        SocketClose();
    } else if (std::strncmp(
                   messageBuffer.data(),
                   "250",
                   3) != 0) {
        SocketClose();
        result = 0;
    }
    return result;
}

// L2Authd.exe 0x0047BE20, Src/sendmail.cpp:233-268.
int SendMail::PacketRcptTo() {
    if (mSock == kInvalidSocket) {
        return 0;
    }

    std::array<char, 1024> receiverBuffer{};
    std::strcpy(
        receiverBuffer.data(),
        mReceiver);
    int result = 0;
    unsigned int attemptedRecipients = 0;
    unsigned int acceptedRecipients = 0;
    char* token = std::strtok(
        receiverBuffer.data(),
        ";");
    while (token != nullptr) {
        ++attemptedRecipients;
        std::array<char, 1024> messageBuffer{};
        // Source bug: each configured receiver was formatted without a bound.
        const int length = runtime::BoundedFormat(
            messageBuffer.data(),
            messageBuffer.size(),
            "RCPT TO:<%s>\r\n",
            token);
        if (length >= 0 && SendOnce(
                mSock,
                messageBuffer.data(),
                length) != -1) {
            messageBuffer.fill('\0');
            if (ReceiveOnce(
                    mSock,
                    messageBuffer.data(),
                    static_cast<int>(messageBuffer.size())) != -1 &&
                std::strncmp(
                    messageBuffer.data(),
                    "250",
                    3) == 0) {
                result = 1;
                ++acceptedRecipients;
            }
        }
        token = std::strtok(nullptr, ";");
    }

    if (attemptedRecipients == 0 || acceptedRecipients < attemptedRecipients) {
        L2_LOG(Log, result == 0 ? LOG_ERROR : LOG_WARN,
            "[legacy mail] RCPT TO accepted %u of %u recipients. %s",
            acceptedRecipients, attemptedRecipients,
            result == 0 ? "No recipient was confirmed; closing the socket and returning zero."
                        : "At least one recipient was accepted; the send path continues despite unconfirmed recipients.");
    }
    if (result == 0) {
        SocketClose();
    }
    return result;
}

// L2Authd.exe 0x0047BFE0, Src/sendmail.cpp:271-332.
int SendMail::PacketData(
    char* mailTitle,
    char* message) {
    std::array<char, 1024> messageBuffer{};
    if (mSock == kInvalidSocket) {
        return 0;
    }

    std::strcpy(messageBuffer.data(), "DATA\r\n");
    int result = 0;
    if (SendOnce(
            mSock,
            messageBuffer.data(),
            static_cast<int>(
                std::strlen(messageBuffer.data()))) != -1) {
        messageBuffer.fill('\0');
        if (ReceiveOnce(
                mSock,
                messageBuffer.data(),
                static_cast<int>(messageBuffer.size())) != -1) {
            result = 1;
        }
    }

    if (result != 0) {
        result = 0;
        messageBuffer.fill('\0');
        // Source bug: the title, receiver and body were formatted into this
        // 1024-byte buffer without a bound. The body is now sent directly,
        // with the same bytes on the wire.
        const int length = runtime::BoundedFormat(
            messageBuffer.data(),
            messageBuffer.size(),
            "Subject:%s\r\nTo:%s\r\n\r\n",
            mailTitle,
            mReceiver);
        if (length >= 0 &&
            SendOnce(
                mSock,
                messageBuffer.data(),
                length) != -1) {
            messageBuffer.fill('\0');
            if (SendOnce(
                    mSock,
                    message,
                    static_cast<int>(std::strlen(message))) != -1 &&
                SendOnce(mSock, "\r\n", 2) != -1 &&
                SendOnce(
                    mSock,
                    "\r\n.\r\n",
                    static_cast<int>(
                        std::strlen("\r\n.\r\n"))) != -1) {
                messageBuffer.fill('\0');
                if (ReceiveOnce(
                        mSock,
                        messageBuffer.data(),
                        static_cast<int>(
                            messageBuffer.size())) != -1) {
                    result = 1;
                }
            }
        }
    }

    if (result == 0) {
        ReportMailSocketFailure("SMTP DATA exchange",
            "Relay acceptance is not confirmed. Closing the socket; DATA returns zero.");
        SocketClose();
    } else if (std::strncmp(
                   messageBuffer.data(),
                   "250",
                   3) != 0) {
        SocketClose();
        result = 0;
    }
    return result;
}

// L2Authd.exe 0x0047C2D0, Src/sendmail.cpp:335-355.
int SendMail::PacketQuit() {
    std::array<char, 1024> messageBuffer{};
    if (mSock == kInvalidSocket) {
        return 0;
    }

    std::strcpy(messageBuffer.data(), "QUIT\r\n");
    const int result = SendOnce(
        mSock,
        messageBuffer.data(),
        static_cast<int>(
            std::strlen(messageBuffer.data()))) != -1;
    if (result == 0) {
        ReportMailSocketFailure("Sending SMTP QUIT",
            "Closing the socket; QUIT returns zero. This failure does not establish whether the message was delivered.");
    }
    SocketClose();
    return result;
}
