#include "Shared/src/SystemMail.h"
#include "Shared/src/Runtime/Utf16Format.h"

#if defined(L2_BUILD_L2SERVER)
#include "L2Server/src/SecureINIFileReader.h"
#endif

#include "L2Core/src/inc/BaseConverter.h"
#include "L2Core/src/inc/FileUtils.h"
#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/SystemMailTransport.h"
#include "Shared/src/Config.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "TextRuntime.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace {

constexpr char kDefaultAddress[] = "Lineage2Crash@ncsoft.com";
constexpr char kDefaultReceiverMultiString[] = "Lineage2Crash@ncsoft.com\0";
constexpr char kData[] = "DATA\r\n";
constexpr char kEndData[] = "\r\n.\r\n";
constexpr char kAttachmentBoundary[] = "\r\n--newchunkstart\r\n";
constexpr char kQuit[] = "QUIT\r\n";
constexpr char kAttachmentHeader[] = "Content-Type: application/dmp; name=\"minidump.dmp\"\r\n"
                                     "Content-Transfer-Encoding: base64\r\n"
                                     "Content-Disposition: attachment; filename=minidump.dmp\r\n\r\n";

std::array<char, 0x800> g_szMailTo{};
std::array<char, 0x100> subjectStr{};
std::array<char, 0x100> mailStr{};
std::array<char, 0x100> rcptStr{};

class MailInputFile {
public:
    explicit MailInputFile(const wchar_t* path) {
        if (!path) {
            return;
        }
#if defined(_WIN32)
        handle_ = ::CreateFileW(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
#else
        const std::string nativePath = WideToUtf8(path);
        file_ = std::fopen(nativePath.c_str(), "rb");
#endif
    }

    // The target closes only after EOF/ReadFile failure. A socket-send
    // failure returns with this handle still open.
    ~MailInputFile() = default;

    void Close() {
#if defined(_WIN32)
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (file_) {
            std::fclose(file_);
            file_ = nullptr;
        }
#endif
    }

    bool IsOpen() const {
#if defined(_WIN32)
        return handle_ != INVALID_HANDLE_VALUE;
#else
        return file_ != nullptr;
#endif
    }

    std::size_t Read(void* destination, std::size_t capacity) {
        if (!IsOpen() || !destination || capacity == 0) {
            return 0;
        }
#if defined(_WIN32)
        DWORD read = 0;
        if (!::ReadFile(handle_, destination, static_cast<DWORD>(capacity), &read, nullptr)) {
            return 0;
        }
        return static_cast<std::size_t>(read);
#else
        return std::fread(destination, 1, capacity, file_);
#endif
    }

private:
#if defined(_WIN32)
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    std::FILE* file_ = nullptr;
#endif
};

bool IsInvalidOutputFile(void* file) {
#if defined(_WIN32)
    return static_cast<HANDLE>(file) == INVALID_HANDLE_VALUE;
#else
    return file == reinterpret_cast<void*>(static_cast<std::intptr_t>(-1));
#endif
}

void CloseOutputFile(void* file) {
#if defined(_WIN32)
    ::CloseHandle(static_cast<HANDLE>(file));
#else
    std::fclose(static_cast<std::FILE*>(file));
#endif
}

void SetOutputEndOfFile(void* file) {
#if defined(_WIN32)
    ::SetEndOfFile(static_cast<HANDLE>(file));
#else
    // CreateFileWithModuleName opens the portable file with "wb", so its
    // current position and end already coincide here.
    std::fflush(static_cast<std::FILE*>(file));
#endif
}

#if defined(L2_BUILD_CACHED)
// Diagnostic-file boundary only: SQL and driver text must not pass through
// hprintf's fixed buffer or the recovered ANSI code page.
void WriteDatabaseMailReport(void* file, std::string_view text) {
    if (!file || IsInvalidOutputFile(file)) {
        // Log.Add(LOG_ERROR, "[mail] Cannot write the database report: invalid file handle.")
        L2_LOG_ERROR(Log, "[mail] Cannot write the database report: invalid file handle.");
        return;
    }
    std::size_t completed = 0;
    while (completed < text.size()) {
        const std::size_t remaining = text.size() - completed;
        constexpr std::size_t chunkLimit = 1024U * 1024U;
        const std::size_t requested = remaining > chunkLimit ? chunkLimit : remaining;
#if defined(_WIN32)
        DWORD written = 0;
        if (!::WriteFile(
                static_cast<HANDLE>(file), text.data() + completed, static_cast<DWORD>(requested), &written, nullptr
            )) {
            const DWORD error = ::GetLastError();
            // Log.Add(LOG_ERROR,
            //     "[mail] Database report write failed after %zu of %zu bytes (Win32 error %lu). The report may be
            //     incomplete.", completed, text.size(), static_cast<unsigned long>(error))
            L2_LOG_ERROR(
                Log,
                "[mail] Database report write failed after %zu of %zu bytes (Win32 error %lu). The report may be "
                "incomplete.",
                completed, text.size(), static_cast<unsigned long>(error)
            );
            return;
        }
        if (written == 0) {
            // Log.Add(LOG_ERROR,
            //     "[mail] Database report write made no progress after %zu of %zu bytes. The report may be incomplete.",
            //     completed, text.size())
            L2_LOG_ERROR(
                Log, "[mail] Database report write made no progress after %zu of %zu bytes. The report may be incomplete.",
                completed, text.size()
            );
            return;
        }
#else
        const std::size_t written = std::fwrite(text.data() + completed, 1, requested, static_cast<std::FILE*>(file));
        if (written != requested) {
            const int error = errno;
            // Log.Add(LOG_ERROR,
            //     "[mail] Database report write stopped after %zu of %zu bytes (errno snapshot %d). The report may be
            //     incomplete.", completed + written, text.size(), error)
            L2_LOG_ERROR(
                Log,
                "[mail] Database report write stopped after %zu of %zu bytes (errno snapshot %d). The report may be "
                "incomplete.",
                completed + written, text.size(), error
            );
            return;
        }
#endif
        completed += written;
    }
#if !defined(_WIN32)
    if (std::fflush(static_cast<std::FILE*>(file)) != 0) {
        const int error = errno;
        // Log.Add(LOG_ERROR,
        //     "[mail] Could not flush the database report (errno %d). The report may be incomplete.", error)
        L2_LOG_ERROR(Log, "[mail] Could not flush the database report (errno %d). The report may be incomplete.", error);
    }
#endif
}
#endif

const char* EffectiveReceivers() {
    // The original Shared source has two country branches. Current CacheD
    // keeps the g_nCountryCode read at 0x0059a831/0x0059a8ac even though
    // this build resolves both branches to the same address selection.
    if (g_nCountryCode == 0) {
        return g_szMailTo[0] == '\0' ? kDefaultReceiverMultiString : g_szMailTo.data();
    }
    return g_szMailTo[0] == '\0' ? kDefaultReceiverMultiString : g_szMailTo.data();
}

// Current CacheD.exe 0x005999fc. Current L2NPC.exe 0x0053d834.
bool sendFile(runtime::SmtpSocket& socket, const wchar_t* path) {
    MailInputFile file(path);
    if (!file.IsOpen()) {
        // This is the shipping behavior: a missing body file does not abort
        // the SMTP transaction.
        return true;
    }

    std::array<unsigned char, 0x5000> buffer{};
    for (;;) {
        const std::size_t read = file.Read(buffer.data(), buffer.size());
        if (read == 0) {
            file.Close();
            return true;
        }
        if (socket.Send(buffer.data(), read) < 1) {
            return false;
        }
    }
}

#if !defined(L2_BUILD_CACHED)
// Current L2NPC.exe 0x0053d8ec. CacheD does not create this helper and
// its 0x0059a20c sendMail body never sends the supplied crash attachment.
bool sendBinaryFile(runtime::SmtpSocket& socket, const wchar_t* path) {
    MailInputFile file(path);
    if (!file.IsOpen()) {
        return true;
    }
    if (socket.Send(kAttachmentHeader, std::strlen(kAttachmentHeader)) < 1) {
        return false;
    }

    std::array<char, 0x3000> input{};
    std::array<char, 0x4400> output{};
    for (;;) {
        const std::size_t read = file.Read(input.data(), input.size());
        if (read == 0) {
            file.Close();
            return true;
        }
        int encoded = 0;
        if (!Base64::Encode(input.data(), static_cast<int>(read), output.data(), &encoded)) {
            return false;
        }
        if (socket.Send(output.data(), static_cast<std::size_t>(encoded)) < 1) {
            return false;
        }
    }
}
#endif

const char* PrintServerType() {
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x0059a0bc passes this exact case-sensitive text to
    // the recovered title formatter.
    return "CacheD";
#else
    return "NPCSERVER";
#endif
}

// Current CacheD.exe 0x0059a0bc. Current L2NPC.exe 0x0053dde8.
void makeMailTitle(runtime::SmtpSocket& socket, MailType mailType, bool hasAttachment) {
    std::array<unsigned char, 4> address{};
    socket.GetLocalIPv4(address);
    // Current GetComputerNameA result and stack buffer are both unchecked.
    std::array<char, 16> computerName;
    runtime::GetHostComputerName(computerName.data(), computerName.size());

    const int value = static_cast<int>(mailType);
    if (value < 0 || value >= MAIL_MAX) {
        std::snprintf(subjectStr.data(), subjectStr.size(), "Subject: No Title \r\n\r\n");
        return;
    }

    const std::string title = GetTitleBody(mailType);
    // const char* const format = hasAttachment
    //     ? "Subject: %s [%d][%s](%d.%d.%d.%d) %s \r\n"
    //       "MIME-Version: 1.0\r\n"
    //       "Content-Type: multipart/mixed. Boundary=newchunkstart\r\n\r\n"
    //       "--newchunkstart\r\n"
    //       "Content-Type: text/plain.charset=utf-8\r\n\r\n"
    //     : "Subject: %s [%d][%s](%d.%d.%d.%d) %s \r\n\r\n".
    const char* const format = hasAttachment ? "Subject: %s [%d][%s](%d.%d.%d.%d) %s \r\n"
                                               "MIME-Version: 1.0\r\n"
                                               "Content-Type: multipart/mixed; boundary=newchunkstart\r\n\r\n"
                                               "--newchunkstart\r\n"
                                               "Content-Type: text/plain;charset=utf-8\r\n\r\n"
        : mailType == MAIL_CANNOT_FIND_SP    ? "Subject: %s [%d][%s](%d.%d.%d.%d) %s \r\n"
                                               "MIME-Version: 1.0\r\n"
                                               "Content-Type: text/plain;charset=utf-8\r\n\r\n"
                                             : "Subject: %s [%d][%s](%d.%d.%d.%d) %s \r\n\r\n";
    std::snprintf(
        subjectStr.data(), subjectStr.size(), format, PrintServerType(), g_nServerId, computerName.data(), address[0],
        address[1], address[2], address[3], title.c_str()
    );
}

// Diagnostic-only observation. The recovered SMTP flow does not validate
// replies or retry short reads. Keep that protocol behavior while making its
// delivery uncertainty explicit. Never print reply text, addresses or content.
void LogMailFailure(MailType type, const char* phase, int code, bool hasCode = true) {
#if defined(_WIN32)
    constexpr const char* domain = "win32_last_error";
#else
    constexpr const char* domain = "errno";
#endif
    char nativeCode[16] = "unavailable";
    if (hasCode) {
        std::snprintf(nativeCode, sizeof(nativeCode), "%d", code);
    }
    // Log.Add(LOG_ERROR,
    //     "component=mail event=transport_failed mail_type=%d phase=%s port=25 native_code=%s error_domain=%s
    //     delivery=unverified", static_cast<int>(type), phase, nativeCode, hasCode ? domain : "unavailable")
    // Log.Add(LOG_ERROR,
    //     "[mail] Report type %d failed at %s on port 25 (native error %s, domain %s). Delivery is not confirmed.",
    //     static_cast<int>(type), phase, nativeCode, hasCode ? domain : "unavailable")
    L2_LOG_ERROR(
        Log, "[mail] Report type %d failed at %s on port 25 (native error %s, domain %s). Delivery is not confirmed.",
        static_cast<int>(type), phase, nativeCode, hasCode ? domain : "unavailable"
    );
}

unsigned LogMailReply(MailType type, const char* phase, const char* response, int received, int expected) {
    int code = 0;
    if (received >= 3 && response[0] >= '0' && response[0] <= '9' && response[1] >= '0' && response[1] <= '9' &&
        response[2] >= '0' && response[2] <= '9') {
        code = (response[0] - '0') * 100 + (response[1] - '0') * 10 + response[2] - '0';
    }
    if (code == expected || (std::strcmp(phase, "recipient") == 0 && code == 251)) {
        return 0;
    }
    // Log.Add(code >= 400 ? LOG_ERROR : LOG_WARN,
    //     "component=mail event=reply_unverified mail_type=%d phase=%s receive_result=%d reply_prefix_code=%d
    //     expected_code=%d response=redacted action=continue_legacy_sequence", static_cast<int>(type), phase, received,
    //     code, expected)
    // Log.Add(code >= 400 ? LOG_ERROR : LOG_WARN,
    //     "[mail] Report type %d has an unexpected reply at %s: receive result %d, prefix %d, expected %d. The existing SMTP
    //     sequence continues.", static_cast<int>(type), phase, received, code, expected)
    L2_LOG(
        Log, code >= 400 ? LOG_ERROR : LOG_WARN,
        "[mail] Report type %d has an unexpected reply at %s: receive result %d, prefix %d, expected %d. The existing SMTP "
        "sequence continues.",
        static_cast<int>(type), phase, received, code, expected
    );
    return 1;
}

// Current CacheD.exe 0x0059a20c. Current L2NPC.exe 0x0053df20.
int sendMail(
    const char* host,
    const char* from,
    const char* receivers,
    const wchar_t* bodyFile,
    MailType mailType,
    const wchar_t* attachment
) {
    if (!host || !runtime::SystemMailNetworkEnabled()) {
        return 0;
    }

    // Log.Add(
    //     LOG_LOCAL,
    //     L"sendmail called")
    // Log.Add(LOG_LOCAL, "component=mail event=transport_started mail_type=%d port=25", static_cast<int>(mailType))
    // Log.Add(LOG_LOCAL, "[mail] Starting report type %d on SMTP port 25.", static_cast<int>(mailType))
    L2_LOG(Log, LOG_LOCAL, "[mail] Starting report type %d on SMTP port 25.", static_cast<int>(mailType));
    unsigned replyIssues = 0;
    runtime::SmtpSocket socket;
    // if (socket.Initialize() != 0) {
    const int initializeResult = socket.Initialize();
    if (initializeResult != 0) {
        // Log.Add(
        //     LOG_LOCAL,
        //     "WsaStartup failed")
        // Log.Add(LOG_ERROR, "component=mail event=transport_failed mail_type=%d phase=initialize return_code=%d
        // delivery=unverified",
        //     static_cast<int>(mailType), initializeResult)
        // Log.Add(LOG_ERROR, "[mail] Could not initialize SMTP for report type %d (return code %d). Delivery is not
        // confirmed.",
        //     static_cast<int>(mailType), initializeResult)
        L2_LOG_ERROR(
            Log, "[mail] Could not initialize SMTP for report type %d (return code %d). Delivery is not confirmed.",
            static_cast<int>(mailType), initializeResult
        );
        return 0;
    }
    if (!socket.Open()) {
        const int openError = socket.LastError();
        // Current CacheD/L2NPC call WSACleanup before this particular log.
        // all later failures log first and use the common close epilogue.
        socket.Close();
        // Log.Add(
        //     LOG_LOCAL,
        //     L"socket set failed")
        LogMailFailure(mailType, "open", openError);
        return 0;
    }
    if (!socket.SetReceiveTimeout(5000)) {
        // Log.Add(
        //     LOG_LOCAL,
        //     L"setsockopt failed - rcvtime0")
        LogMailFailure(mailType, "set_receive_timeout", socket.LastError());
        return 0;
    }
    if (!socket.SetSendTimeout(5000)) {
        // Log.Add(
        //     LOG_LOCAL,
        //     L"setsockopt failed - sndtime0")
        LogMailFailure(mailType, "set_send_timeout", socket.LastError());
        return 0;
    }
    if (!socket.BindAny()) {
        // Log.Add(
        //     LOG_LOCAL,
        //     L"bind failed")
        LogMailFailure(mailType, "bind", socket.LastError());
        return 0;
    }

    const runtime::SmtpConnectResult connectResult = socket.Connect(host, 25);
    if (connectResult == runtime::SmtpConnectResult::ResolveFailed) {
        // Log.Add(
        //     LOG_LOCAL,
        //     L"gethostbyname failed")
        LogMailFailure(mailType, "resolve", 0, false);
        return 0;
    }
    if (connectResult != runtime::SmtpConnectResult::Connected) {
        // Log.Add(
        //     LOG_LOCAL,
        //     L"connect failed - sndtime0")
        LogMailFailure(mailType, "connect", socket.LastError());
        return 0;
    }

    std::array<char, 0x800> response{};
    // Noise: numbered pre-receive markers do not describe the SMTP reply or an outcome.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"received sendmail 1")

    // socket.Receive(
    //     response.data(),
    //     response.size())
    const int greetingReceived = socket.Receive(response.data(), response.size());
    replyIssues += LogMailReply(mailType, "greeting", response.data(), greetingReceived, 220);

    std::snprintf(mailStr.data(), mailStr.size(), "HELO %s\r\n", host);
    if (socket.Send(mailStr.data(), std::strlen(mailStr.data())) < 1) {
        // Sensitive data: report the stage and available error code without SMTP commands or message content.
        // Log.Add(
        //     LOG_LOCAL,
        //     L"helo send failed. error code [%d]",
        //     socket.LastError())
        LogMailFailure(mailType, "helo", socket.LastError());
        return 0;
    }

    // Noise: numbered pre-receive markers do not describe the SMTP reply or an outcome.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"received sendmail 2")

    // socket.Receive(
    //     response.data(),
    //     response.size())
    const int heloReceived = socket.Receive(response.data(), response.size());
    replyIssues += LogMailReply(mailType, "helo", response.data(), heloReceived, 250);
    std::snprintf(mailStr.data(), mailStr.size(), "MAIL From: <%s>\r\n", from);
    if (socket.Send(mailStr.data(), std::strlen(mailStr.data())) < 1) {
        // Sensitive data: report the stage and available error code without SMTP commands or message content.
        // Log.Add(
        //     LOG_LOCAL,
        //     L"mailstr send failed [%s]",
        //     mailStr.data())
        LogMailFailure(mailType, "mail_from", socket.LastError());
        return 0;
    }

    // Noise: numbered pre-receive markers do not describe the SMTP reply or an outcome.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"received sendmail 3")

    // socket.Receive(
    //     response.data(),
    //     response.size())
    const int mail_fromReceived = socket.Receive(response.data(), response.size());
    replyIssues += LogMailReply(mailType, "mail_from", response.data(), mail_fromReceived, 250);
    for (const char* receiver = receivers; *receiver != '\0'; receiver += std::strlen(receiver) + 1) {
        std::snprintf(rcptStr.data(), rcptStr.size(), "RCPT To: <%s>\r\n", receiver);
        if (socket.Send(rcptStr.data(), std::strlen(rcptStr.data())) < 1) {
            // Sensitive data: report the stage and available error code without SMTP commands or message content.
            // Log.Add(
            //     LOG_LOCAL,
            //     L"rcpt failed [%s]",
            //     rcptStr.data())
            LogMailFailure(mailType, "recipient", socket.LastError());
            return 0;
        }
        // socket.Receive(
        //     response.data(),
        //     response.size())
        const int recipientReceived = socket.Receive(response.data(), response.size());
        replyIssues += LogMailReply(mailType, "recipient", response.data(), recipientReceived, 250);
    }

    if (socket.Send(kData, std::strlen(kData)) < 1) {
        // Sensitive data: report the stage and available error code without SMTP commands or message content.
        // Log.Add(
        //     LOG_LOCAL,
        //     L"send datastr failed [%s]",
        //     kData)
        LogMailFailure(mailType, "data", socket.LastError());
        return 0;
    }
    // Noise: numbered pre-receive markers do not describe the SMTP reply or an outcome.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"received sendmail 4")

    // socket.Receive(
    //     response.data(),
    //     response.size())
    const int dataReceived = socket.Receive(response.data(), response.size());
    replyIssues += LogMailReply(mailType, "data", response.data(), dataReceived, 354);

    makeMailTitle(socket, mailType, attachment != nullptr);
    // Noise: this internal preparation step has no independent operator outcome.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"made title")

    if (socket.Send(subjectStr.data(), std::strlen(subjectStr.data())) < 1) {
        // Sensitive data: report the stage and available error code without SMTP commands or message content.
        // Log.Add(
        //     LOG_LOCAL,
        //     L"subjectStr send failed [%s]",
        //     subjectStr.data())
        LogMailFailure(mailType, "headers", socket.LastError());
        return 0;
    }
    if (!sendFile(socket, bodyFile)) {
        // Sensitive data: report the stage and available error code without SMTP commands or message content.
        // Log.Add(
        //     LOG_LOCAL,
        //     L"sendFile failed [%s]",
        //     bodyFile)
        LogMailFailure(mailType, "body", 0, false);
        return 0;
    }

#if !defined(L2_BUILD_CACHED)
    if (attachment) {
        // Noise: this internal preparation step has no independent operator outcome.
        // Log.Add(
        //     LOG_LOCAL,
        //     L"attaching file")

        if (socket.Send(kAttachmentBoundary, std::strlen(kAttachmentBoundary)) < 1) {
            // Sensitive data: report the stage and available error code without SMTP commands or message content.
            // Log.Add(
            //     LOG_LOCAL,
            //     L"attaching file failed")
            LogMailFailure(mailType, "attachment_boundary", socket.LastError());
            return 0;
        }
        if (!sendBinaryFile(socket, attachment)) {
            // Sensitive data: report the stage and available error code without SMTP commands or message content.
            // Log.Add(
            //     LOG_LOCAL,
            //     L"send binary failed")
            LogMailFailure(mailType, "attachment_payload", 0, false);
            return 0;
        }
    }
#endif

    if (socket.Send(kEndData, std::strlen(kEndData)) < 1) {
        LogMailFailure(mailType, "end_data", socket.LastError());
        return 0;
    }
    // socket.Receive(
    //     response.data(),
    //     response.size())
    const int end_dataReceived = socket.Receive(response.data(), response.size());
    replyIssues += LogMailReply(mailType, "end_data", response.data(), end_dataReceived, 250);
    if (socket.Send(kQuit, std::strlen(kQuit)) < 1) {
        LogMailFailure(mailType, "quit", socket.LastError());
        return 0;
    }
    // socket.Receive(
    //     response.data(),
    //     response.size())
    const int quitReceived = socket.Receive(response.data(), response.size());
    replyIssues += LogMailReply(mailType, "quit", response.data(), quitReceived, 221);
    // The recovered flow ignores SMTP replies and short positive sends. Completion does not prove delivery.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"all sendmail done")
    // Log.Add(replyIssues == 0 ? LOG_LOCAL : LOG_WARN,
    //     "component=mail event=transport_sequence_completed mail_type=%d reply_issues=%u delivery=unverified",
    //     static_cast<int>(mailType), replyIssues)
    // Log.Add(replyIssues == 0 ? LOG_LOCAL : LOG_WARN,
    //     "[mail] SMTP sequence finished for report type %d with %u reply issues. Delivery is not confirmed.",
    //     static_cast<int>(mailType), replyIssues)
    L2_LOG(
        Log, replyIssues == 0 ? LOG_LOCAL : LOG_WARN,
        "[mail] SMTP sequence finished for report type %d with %u reply issues. Delivery is not confirmed.",
        static_cast<int>(mailType), replyIssues
    );
    return 1;
}

} // namespace

// Current CacheD.exe 0x00599e08. Current L2NPC.exe 0x0053db34.
std::string GetTitleBody(MailType mailType) {
    switch (mailType) {
        case MAIL_CRASH:
            return "is crashed";
        case MAIL_UNITTEST_FAILED:
            return "failed UnitTest";
        case MAIL_UNITTEST_SUCCESS:
            return "succeeded UnitTest";
        case MAIL_CONVERT_INT64_FAILED:
            return "failed to convert INT64";
        case MAIL_CANNOT_FIND_SP:
            return "cannot find stored procedure";
        case MAIL_CRITICAL_CALLSTACK:
            return "critical callstack";
        case MAIL_NOT_SET_THREAD_LOCAL_WORLD_ID:
            return "thread local world id is not set";
        case MAIL_SERVER_START_COMPLETED:
            return "server start completed";
        case MAIL_NOT_SET_DB_LIST:
            return "raid world is not included in DBList at l2cached.ini";
        case RESET_PROFILE:
            return "reset profile";
        case REPORT_PROFILE:
            return "report profile";
        case MAIL_L2SERVER_CONFIG:
            return "l2server.ini setting";
        case MAIL_NP_CRITICAL_MESSAGE:
            return "np critical message";
        case MAIL_TIMER_CRITICAL_MESSAGE:
            return "timer critical message";
        default:
            return "No Title";
    }
}

// Current CacheD.exe 0x0040ec64.
wchar_t* CommonMailMessage::GetFileName() { return m_pFileName; }

// Current CacheD.exe 0x0059a774.
int CommonMailMessage::SendMail() {
    std::array<wchar_t, 260> fileName{};
    std::wcsncpy(fileName.data(), L"Unknown", fileName.size() - 1);
    void* const file = L2Exception::CreateFileWithModuleName(fileName.data(), GetFileName());
    if (IsInvalidOutputFile(file)) {
        std::array<char16_t, 0x400> error{};
        FormatUtf16To(error.data(), error.size(), u"Error creating %ls", GetFileName());
        // Log.Add(LOG_ERROR, "component=mail event=body_file_create_failed mail_type=%d file=\"%s\"",
        //     static_cast<int>(GetMailType()), WideToUtf8(GetFileName()).c_str())
        // Log.Add(LOG_ERROR, "[mail] Could not create the body file for report type %d (file \"%s\").",
        //     static_cast<int>(GetMailType()), WideToUtf8(GetFileName()).c_str())
        L2_LOG_ERROR(
            Log, "[mail] Could not create the body file for report type %d (file \"%s\").", static_cast<int>(GetMailType()),
            WideToUtf8(GetFileName()).c_str()
        );
        return 0;
    }

    CreateContents(file);
    CloseOutputFile(file);
    return sendMail(GetHost(), kDefaultAddress, EffectiveReceivers(), fileName.data(), GetMailType(), nullptr);
}

// Current CacheD.exe 0x0059a898.
int CrashMailMessage::SendMail() {
    return sendMail(GetHost(), kDefaultAddress, EffectiveReceivers(), g_szLinErrorPath, MAIL_CRASH, m_file);
}

#if defined(L2_BUILD_CACHED)
// Current CacheD.exe 0x00599b48. The matching NASC64_2015 PDB supplies the
// original class/field names. Current instructions fix CacheD's extra
// L2Protocol and CatalogName lines and their exact order.
// void CannotFindSPMailMessage::CreateContents(
//     void* file) {
//     // Retain the recovered field/layout without emitting the raw driver text.
//     (void)m_errMsg.
//     hprintf(
//         file,
//         L"Build Date : %s\r\n",
//         CBuildDate::inst().BuildDateTime())
//     hprintf(
//         file,
//         L"L2Protocol : %d\r\n",
//         GetL2ProtocolVersion())
//     hprintf(
//         file,
//         L"CatalogName : %s\r\n",
//         m_catalogName)
//     // Sensitive data: the procedure argument is a full SQL call, and the driver message may echo values. Retain metadata
//     in the diagnostic file/mail body.
//     // hprintf(
//     //     file,
//     //     L"Cannot find stored procedure[%s].\r\n",
//     //     m_sName)
//     // hprintf(
//     //     file,
//     //     L"State:%s, Err:%d, Msg:%s\r\n",
//     //     m_SQLState,
//     //     m_native,
//     //     m_errMsg)
//     char state[6]{}
//     if (m_SQLState != nullptr) {
//         for (std::size_t i = 0, i < 5 && m_SQLState[i] != 0, ++i) {
//             const auto value = static_cast<unsigned long>(m_SQLState[i])
//             state[i] = value <= 0x7f ? static_cast<char>(value) : '?'
//         }
//     }
//     const bool validState = runtime::logging::IsSqlState(state) && m_SQLState[5] == 0
//     const std::wstring safeState = Utf8ToWide(validState ? state : "none")
//     const std::wstring reason = Utf8ToWide(runtime::logging::SqlStateReason(validState ? state : ""))
//     const std::wstring statementKind = Utf8ToWide(runtime::logging::WideSqlStatementKind(m_sName))
//     // hprintf(file,
//     //     L"component=database event=missing_procedure statement_kind=%s sqlstate=%s native_code=%d reason=%s
//     sql=redacted details=redacted\r\n",
//     //     statementKind.c_str(), safeState.c_str(), m_native, reason.c_str())
//     hprintf(file,
//         L"[database] Stored procedure was not found (%s statement). SQLSTATE %s, native error %d (%s).\r\n",
//         statementKind.c_str(), safeState.c_str(), m_native, reason.c_str())
//     // Keep the existing mail-file encoding. Split only to respect hprintf's
//     // fixed buffer, without changing the statement or interpolating parameters.
//     if (m_sName != nullptr) {
//         const std::string statement = WideToUtf8(m_sName)
//         runtime::logging::ForEachSqlFragment(statement,
//             [file](std::uint64_t sequence, std::size_t part, std::size_t total, std::string_view fragment) {
//                 const std::wstring text = Utf8ToWide(fragment)
//                 if (total == 1) {
//                     hprintf(file, L"SQL: \"%s\"\r\n", text.c_str())
//                 } else {
//                     hprintf(file, L"SQL %llu, part %zu/%zu: \"%s\"\r\n",
//                         static_cast<unsigned long long>(sequence), part, total, text.c_str())
//                 }
//             })
//     }
//     hprintf(file, L"--- call stack ---\r\n")
//     GuardHelper::printCallStack(
//         file,
//         CThreadManager::GetThreadID())
// }
void CannotFindSPMailMessage::CreateContents(void* file) {
    // The report is UTF-8 throughout, including Unicode stack labels. Keep
    // SQL and driver messages intact, without formatting or fragment limits.
    const auto utf8 = [](const wchar_t* value) { return value ? WideToUtf8(value) : std::string("(not provided)"); };
    std::string report = "Build Date : " + utf8(CBuildDate::inst().BuildDateTime()) + "\r\n";
    report += "L2Protocol : " + std::to_string(GetL2ProtocolVersion()) + "\r\n";
    report += "CatalogName : " + utf8(m_catalogName) + "\r\n";
    report += "[database] Stored procedure was not found. SQLSTATE " + utf8(m_SQLState);
    report += ", native error " + std::to_string(m_native) + ".\r\n";
    report += "Driver message:\r\n" + utf8(m_errMsg) + "\r\n";
    report += "SQL:\r\n" + utf8(m_sName) + "\r\n";
    report += "--- call stack ---\r\n";
    const int thread = CThreadManager::GetThreadID();
    for (int depth = 0; depth < GuardHelper::getCallStackDepth(thread); ++depth) {
        report += "[" + std::to_string(depth) + "] ";
        report += utf8(GuardHelper::getCallStack(thread, depth)) + "\r\n";
    }
    WriteDatabaseMailReport(file, report);
}
#endif

void CriticalCallstackMessage::CreateContents(void* file) {
    SetOutputEndOfFile(file);
    L2Exception::GenerateExceptionReportFront(file);
    L2Exception::GenerateExceptionReportBack(file);
    L2Exception::ResumeThreadExceptMe();
}

void ProfileResetMessage::CreateContents(void* file) {
#if L2_ENABLE_GUARD_PROFILING
    hprintf(file, L"Guard profile counters reset.\r\n");
#else
    hprintf(file, L"Can't Reset Profile! No guard Profile version.\r\n");
#endif
}

#if !defined(L2_BUILD_CACHED)
void NotSetThreadLocalWorldIDMessage::CreateContents(void* file) {
    hprintf(file, L"Not set thread local world id!!!!.\r\n");
    hprintf(file, L"--- call stack ---\r\n");
    GuardHelper::printCallStack(file, CThreadManager::GetThreadID());
}
#endif

void ProfileReportMessage::CreateContents(void* file) {
#if L2_ENABLE_GUARD_PROFILING
    GuardProfileHelper::ReportGuardProfileByMail(file);
#else
    hprintf(file, L"Can't Report Profile! No guard Profile version.\r\n");
#endif
}

#if defined(L2_BUILD_CACHED) || defined(L2_BUILD_L2SERVER)
void ServerStartCompletedMessage::CreateContents(void* file) {
    hprintf(file, L"version:\r\n");
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe 0x00599c40 parses the fixed revision markers
    // #56/#373 and prints them after the current protocol revision (#162).
    hprintf(file, L"[l2:%d] [db:%d] [admin:%d]\r\n", GetL2ProtocolVersion(), 373, 56);
    hprintf(file, L"\r\n\r\nbuild date ver = %s\r\n", CBuildDate::inst().BuildDateTime());
    hprintf(file, L"build archive = %s\r\n", CBuildDate::inst().BuildArchive());
#endif
}
#endif

#if defined(L2_BUILD_L2SERVER)
void L2ServerINISettingMessage::CreateContents(void* file) {
    // Current L2Server 0x00addd2c.
    const std::u16string contents = l2server::g_SecureINIFileDB.Print();
    const std::wstring hostContents = Utf16ToWide(contents);
    hprintf(file, L"%s", hostContents.c_str());
}

void NPCriticalMessage::CreateContents(void* file) {
    if (m_npUserGuid && *m_npUserGuid != '\0') {
        hprintf(file, L"NP User GUID: %S", m_npUserGuid);
    }
    if (m_message && *m_message != '\0') {
        hprintf(file, L"%S", m_message);
    }
}
#endif

namespace SystemMail {

// Current CacheD.exe 0x00599d8c.
int SetReceivers(const char* receivers) {
    if (!receivers) {
        return false;
    }

    const std::size_t sourceLength = std::strlen(receivers);
#if defined(_MSC_VER)
    ::strncpy_s(g_szMailTo.data(), g_szMailTo.size(), receivers, g_szMailTo.size());
#else
    if (sourceLength < g_szMailTo.size()) {
        std::memcpy(g_szMailTo.data(), receivers, sourceLength + 1);
    } else {
        // VS2010's constraint failure clears the destination, after which
        // the shipping body still performs its unchecked trailing store.
        g_szMailTo[0] = '\0';
    }
#endif
    ReplaceChar(g_szMailTo.data(), g_szMailTo.size(), ';', '\0');
    ReplaceChar(g_szMailTo.data(), g_szMailTo.size(), ',', '\0');
    g_szMailTo.data()[sourceLength + 1] = '\0';
    return true;
}

#if !defined(L2_BUILD_CACHED)
int sendNotSetThreadLocalWorldID(const char* server) {
    NotSetThreadLocalWorldIDMessage message(server);
    return message.SendMail();
}
#endif

int sendCriticalCallstack(const char* server) {
    // Current CacheD.exe wrapper 0x0059a998.
    CriticalCallstackMessage message(server);
    return message.SendMail();
}

int sendResetProfile(const char* server) {
    // Current CacheD.exe wrapper 0x0059a8f0.
    ProfileResetMessage message(server);
    return message.SendMail();
}

int sendReportProfile(const char* server) {
    // Current CacheD.exe wrapper 0x0059a928.
    ProfileReportMessage message(server);
    return message.SendMail();
}

#if defined(L2_BUILD_CACHED) || defined(L2_BUILD_L2SERVER)
int sendServerStartCompleted(const char* server) {
    // Current CacheD.exe wrapper 0x0059a960.
    ServerStartCompletedMessage message(server);
    return message.SendMail();
}
#endif

#if defined(L2_BUILD_L2SERVER)
int sendL2ServerINISetting(const char* server) {
    // Current L2Server 0x00adea20.
    L2ServerINISettingMessage message(server);
    return message.SendMail();
}

int sendNPCriticalMessage(const char* server, const char* message, const char* npUserGuid) {
    NPCriticalMessage mail(server, message, npUserGuid);
    return mail.SendMail();
}
#endif

#if defined(L2_BUILD_CACHED)
// Current CacheD.exe 0x0059a9d0. The exact public name/signature comes from
// the matching NASC64_2015 Shared/SystemMail PDB.
int sendCannotFindSPMail(
    const char* server,
    const wchar_t* storedProcedure,
    const wchar_t* sqlState,
    int nativeError,
    const wchar_t* errorMessage,
    const wchar_t* catalogName
) {
    CannotFindSPMailMessage message(server, storedProcedure, sqlState, nativeError, errorMessage, catalogName);
    return message.SendMail();
}
#endif

int sendCrashMail(const char* server, const wchar_t* attachmentPath) {
    // Current CacheD.exe wrapper 0x0059aa2c.
    CrashMailMessage message(server, attachmentPath);
    return message.SendMail();
}

} // namespace SystemMail
