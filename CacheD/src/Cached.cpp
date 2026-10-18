#include "Shared/src/Runtime/ConfigDirectory.h"
#include "Cached.h"

#include "AddData.h"
#include "AllianceDb.h"
#include "CAccount.h"
#include "CDBJobDeleteQuest.h"
#include "CReadFile.h"
#include "CWatchList.h"
#include "CacheDProcessLifecycle.h"
#include "CacheSocket.h"
#include "CachedStatusWnd.h"
#include "ConsoleCommands.h"
#include "DBConn.h"
#include "DatabaseStartup.h"
#include "FileServerManager.h"
#include "Item.h"
#include "L2Core/src/inc/Guard.h"
#include "L2Core/src/inc/GuardProfile.h"
#include "NewCharDB.h"
#include "NewbieDb.h"
#include "NpcDbSocket.h"
#include "NpcServer.h"
#include "PledgeDb.h"
#include "PostgreSQLDeployment.h"
#include "ProcessInstanceLock.h"
#include "Server.h"
#include "SevenSignQuestDB.h"
#include "Shared/src/CategoryDataContent.h"
#include "Shared/src/CategoryDataDB.h"
#include "Shared/src/DXPacket.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GIP.h"
#include "Shared/src/L2Time.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/PledgeRanking.h"
#include "Shared/src/Runtime/NativeThreadHandle.h"
#include "Shared/src/Runtime/ConsoleInput.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/Runtime/SerialKey.h"
#include "Shared/src/Runtime/SocketTransport.h"
#include "Shared/src/Runtime/SystemMailTransport.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WaitThreadRuntime.h"
#include "Shared/src/QsCheck.h"
#include "Shared/src/QueryPacket.h"
#include "Shared/src/SystemMail.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/LogStorage.h"
#include "Shared/src/PerfMon.h"
#include "TimeAttackDB.h"
#include "UserDb.h"
#include "WareHouse.h"
#include "AdminServer.h"
#include "AdminSocket.h"
#include "Config.h"
#include "Runtime.h"
#include "TextRuntime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {

CacheDConfiguration g_configuration;
CacheDEntryConfiguration g_entryConfiguration;
bool g_configurationLoaded = false;
cached::portable::CacheDProcessLifecycle g_processLifecycle;
std::atomic<int> g_listenThreadResult{0};
std::atomic<bool> g_consoleReady{false};
std::atomic<bool> g_listenThreadDone{false};
std::mutex g_consoleCommandMutex;

#if !defined(_WIN32)
std::filesystem::path ExecutablePathForLogRollover() {
#if defined(__APPLE__)
    std::uint32_t length = 0;
    ::_NSGetExecutablePath(nullptr, &length);
    if (length != 0) {
        std::vector<char> path(length, '\0');
        if (::_NSGetExecutablePath(path.data(), &length) == 0) {
            std::error_code error;
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(path.data(), error);
            return error ? std::filesystem::path(path.data()) : canonical;
        }
    }
#else
    std::error_code error;
    const std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error && !path.empty())
        return path;
#endif
    return {};
}

std::tm* LocalTime32ForLogRollover(std::int32_t timestamp) noexcept {
    if (timestamp < 0) {
        errno = EINVAL;
        return nullptr;
    }
    std::time_t native = static_cast<std::time_t>(timestamp);
    return std::localtime(&native);
}
#endif

void InstallMissingProcedureReporter(database::PostgreSQLRuntimeConfig& configuration) {
    configuration.missingProcedureReporter = [](const database::MissingProcedureReport& report) {
        const std::wstring sqlState = Utf8ToWide(report.sqlState);
        const std::wstring errorMessage = Utf8ToWide(report.errorMessage);
        const std::wstring catalogName = Utf8ToWide(report.catalogName);
        SystemMail::sendCannotFindSPMail(
            mailServer, report.query == nullptr ? L"" : report.query, sqlState.c_str(), report.nativeError,
            errorMessage.c_str(), catalogName.c_str()
        );
    };
}

void SetListenThreadResult(int result) {
    int expected = 0;
    g_listenThreadResult.compare_exchange_strong(expected, result, std::memory_order_acq_rel, std::memory_order_acquire);
}

bool EqualsAsciiInsensitive(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        char first = left[index];
        char second = right[index];
        if (first >= 'A' && first <= 'Z')
            first += 'a' - 'A';
        if (second >= 'A' && second <= 'Z')
            second += 'a' - 'A';
        if (first != second)
            return false;
    }
    return true;
}

bool ProfileBoolean(const runtime::Config& source, const char* section, const char* key, bool defaultValue) {
    return EqualsAsciiInsensitive(source.Get(section, key, defaultValue ? "true" : "false"), "true");
}

char* AllocateMailServerSetting(const std::string& value) {
    // Current entry 0x0045a794 allocates a process-lifetime 1000-byte CRT
    // buffer for each host and converts the profile text into that storage.
    constexpr std::size_t capacity = 1000;
    auto* storage = static_cast<char*>(std::malloc(capacity));
    if (!storage)
        return nullptr;
    std::memset(storage, 0, capacity);
    const std::size_t copied = std::min(value.size(), capacity - 1);
    std::memcpy(storage, value.data(), copied);
    return storage;
}

std::uint64_t UnsignedProfileInteger(int value) { return static_cast<std::uint32_t>(value); }

CacheDServerType ParseServerType(std::string_view value) {
    if (EqualsAsciiInsensitive(value, "dev")) {
        return CacheDServerType::Dev;
    }
    if (EqualsAsciiInsensitive(value, "live")) {
        return CacheDServerType::Live;
    }
    if (EqualsAsciiInsensitive(value, "test")) {
        return CacheDServerType::Test;
    }
    if (EqualsAsciiInsensitive(value, "movie")) {
        return CacheDServerType::Movie;
    }
    if (EqualsAsciiInsensitive(value, "broadcast")) {
        return CacheDServerType::Broadcast;
    }
    if (EqualsAsciiInsensitive(value, "worldserver")) {
        return CacheDServerType::WorldServer;
    }
    // Current 0x00450ba8 maps every unknown spelling back to live.
    return CacheDServerType::Live;
}

std::vector<std::string> SplitExact(const std::string& value, char delimiter) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(delimiter, start);
        const std::string token = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!token.empty())
            result.push_back(token);
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return result;
}

std::vector<int> ParseLateSavingItems(const std::string& value) {
    // Current Cached.exe 0x004a136c tokenizes the mutable UTF-16 INI buffer
    // with `U+003B`, skips empty fields and applies `_wtoi` to every token.
    std::vector<int> result;
    for (const std::string& token : SplitExact(value, ';')) {
        result.push_back(std::atoi(token.c_str()));
    }
    return result;
}

#if !defined(_WIN32)
std::string ReadSerialFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (bytes.size() >= 2 && bytes[0] == 0xff && bytes[1] == 0xfe) {
        std::string result;
        result.reserve(bytes.size() / 2);
        for (std::size_t offset = 2; offset + 1 < bytes.size(); offset += 2) {
            const std::uint16_t value = static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8U));
            if (value == 0)
                break;
            if (value < 0x80)
                result.push_back(static_cast<char>(value));
        }
        return result;
    }
    std::size_t start = bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3 : 0;
    return std::string(reinterpret_cast<const char*>(bytes.data() + start), bytes.size() - start);
}
#endif

bool HasCommandSubstring(int argc, char** argv, std::string_view value) {
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]).find(value) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

bool WaitForBackgroundThreadInterval(std::chrono::milliseconds interval) {
    const portable::WaitThreadRuntimeServices runtime = portable::GetWaitThreadRuntimeServices();
    if (runtime.waitForStop) {
        return runtime.waitForStop(interval);
    }
    std::this_thread::sleep_for(interval);
    return runtime.stopRequested && runtime.stopRequested();
}

// Current Cached.cpp 0x0045435c. The crash report's fixed logical-thread
// label is "PerfMonThread". No stronger function symbol survives the damaged
// PDB, so that target spelling is kept for this semantic recovery.
void PerfMonThread_common() {
    for (;;) {
        l2npc::g_PerfMon.GetPerfMon();
        if (WaitForBackgroundThreadInterval(
                std::chrono::milliseconds(static_cast<std::uint32_t>(l2npc::g_PerfMon.m_nInterval))
            )) {
            return;
        }
    }
}

// Current Cached.cpp 0x0045510c.
void PerfMonThread_mailing() {
    try {
        PerfMonThread_common();
    } catch (...) {
        runtime::process::HandleUnhandledSharedThreadException("Cached.cpp", 0x4e1, "Thu May 10 05:00:26 2018");
    }
}

// Current Cached.cpp 0x0045515c.
std::uint32_t PerfMonThread(void*) {
    constexpr int threadId = ThreadID::PERFMON;
    CThreadManager::RegisterThisThread(threadId);
    InitializeCurrentIOBufferPool(0x80);
    if (ExceptionMailing()) {
        PerfMonThread_mailing();
    } else {
        PerfMonThread_common();
    }
    // The target loop is process-lifetime. This is reached only by the
    // portable cooperative-stop transport.
    CThreadManager::SetThread(threadId, nullptr);
    return 0;
}

// Current Cached.cpp 0x0045437c. The manager's +0x5c interval is initialized
// to 1000 at 0x004d0d30 and has no other writer in the current image.
void FileServerThread_common() {
    for (;;) {
        runtime::process::SetCurrentDatabaseId(GetCacheDConfiguration().worldId);
        g_fileServerManager.TimerExpired();
        if (WaitForBackgroundThreadInterval(std::chrono::milliseconds(1000))) {
            return;
        }
    }
}

// Current Cached.cpp 0x004551f4.
void FileServerThread_mailing() {
    try {
        FileServerThread_common();
    } catch (...) {
        runtime::process::HandleUnhandledSharedThreadException("Cached.cpp", 0x513, "Thu May 10 05:00:26 2018");
    }
}

} // namespace

// Current Cached.cpp 0x00455244. "FileServerThread" is also the exact label
// selected by the current crash-summary body for logical thread 0x10.
std::uint32_t FileServerThread(void*) {
    constexpr int threadId = ThreadID::FILE_SERVER;
    CThreadManager::RegisterThisThread(threadId);
    InitializeCurrentIOBufferPool(0x80);
    if (ExceptionMailing()) {
        FileServerThread_mailing();
    } else {
        FileServerThread_common();
    }
    CThreadManager::SetThread(threadId, nullptr);
    return 0;
}

namespace {

#if defined(_WIN32)
HINSTANCE g_cachedInstance = nullptr;

std::int64_t SerialKeyBox(HWND__* window, std::uint32_t message, std::uint64_t wParam, std::int64_t lParam) {
    // Current Cached.cpp 0x0045419c. The target accepts only IDOK, reads at
    // most 255 UTF-16 code units from edit control 0x3f3, writes the fixed
    // REG_BINARY SerialKey value and always returns FALSE to the dialog host.
    if (message == WM_COMMAND && static_cast<std::uint16_t>(wParam) == IDOK) {
        std::array<wchar_t, 256> serialKey{};
        ::SendDlgItemMessageW(
            static_cast<HWND>(window), 0x3f3, WM_GETTEXT, 0xff, reinterpret_cast<LPARAM>(serialKey.data())
        );
        serialKey[0xff] = L'\0';
        SaveStrToReg(serialKey.data(), L"SerialKey", L"Software\\PROJECT_L2\\L2CACHED");
        ::EndDialog(static_cast<HWND>(window), 0);
    }
    return 0;
}
#endif

// The current type name is absent from the usable PDB tail. Its out-of-line
// destructor is 0x004544a4. The two constructor bodies are inlined at
// 0x00459d8e and 0x00459e36. The three fields and unwind actions prove an
// RAII elapsed-log owner, so keep that behavior under exceptions instead of
// replacing it with manually paired messages.
class StartupElapsedLogScope {
public:
    StartupElapsedLogScope(int logType, const wchar_t* description) :
        m_logType(logType), m_description(description), m_begin(runtime::GetTickCount()) {
        const std::string text = WideToUtf8(m_description);
        // Log.Add(m_logType == 2 ? LOG_ERROR : LOG_NORMAL, "[NO_ERROR][begin] %s", text.c_str())
        // Log.Add(LOG_NORMAL, "component=startup event=stage_begin stage=\"%s\"", text.c_str())
        // Log.Add(LOG_NORMAL, "[startup] %.800s", text.c_str())
        // Previous: L2_LOG_INFO(Log, "[startup] %.800s", text.c_str())
        L2_LOG_INFO(Log, "[startup] Beginning stage \"%.800s\".", text.c_str());
    }

    ~StartupElapsedLogScope() {
        const std::string text = WideToUtf8(m_description);
        const int elapsed = static_cast<int>(static_cast<std::uint32_t>(runtime::GetTickCount() - m_begin));
        // Log.Add(
        //     m_logType == 2 ? LOG_ERROR : LOG_NORMAL, "[NO_ERROR][end] %s [%d ms]", text.c_str(), elapsed
        // )
        // Log.Add(LOG_NORMAL, "component=startup event=stage_end stage=\"%s\" elapsed_ms=%d",
        //     text.c_str(), elapsed)
        // Log.Add(LOG_NORMAL, "[startup] Leaving stage \"%.800s\" after %d ms.",
        //     text.c_str(), elapsed)
        L2_LOG_INFO(Log, "[startup] Leaving stage \"%.800s\" after %d ms.",
            text.c_str(), elapsed);
    }

    StartupElapsedLogScope(const StartupElapsedLogScope&) = delete;
    StartupElapsedLogScope& operator=(const StartupElapsedLogScope&) = delete;

private:
    [[maybe_unused]] int m_logType;
    const wchar_t* m_description;
    std::uint32_t m_begin;
};

// Current Cached.cpp keeps this sequence inline in ListenThread_common
// 0x00459928. It is extracted only so the recovery gate can remain before
// every destructive call until the complete later startup continuation is
// closed. It is production code, not a test path.
void RunDatabaseJobAccountItemStartupPhase(const CacheDConfiguration& config) {
    g_readFile.ExecuteDBJob();
    CDBJobDeleteQuest::ExecuteOneTimeQuestDeletion();
    CAccount::Init();
    CAccount::ClearUpFromAddedService3ChangeName();
    CItem::Init();

    if (config.ghostDataClearOnStart) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_CleanUpGhostData}";
        connection.Prepare(statement);
        connection.ExecutePrepared(statement);
    }

    {
        StartupElapsedLogScope elapsed(2, L"[post] Clean up posts of deleted sender characters...");
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_CleanUpDeletedPost}";
        connection.Prepare(statement);
        connection.ExecutePrepared(statement);
    }

    {
        StartupElapsedLogScope elapsed(2, L"[post] Clean up posts of old post...");
        int deletedPostCount = 0;
        DBConn connection(runtime::process::CurrentDatabaseId());
        connection.Bind(&deletedPostCount);
        constexpr const wchar_t* statement = L"{Call lin_DeleteOldPost}";
        connection.Prepare(statement);
        if (connection.ExecutePrepared(statement) && connection.Fetch()) {
            // Log.Add(LOG_ERROR, "[NO_ERROR] old post deleted count = [%d]", deletedPostCount)
            // Log.Add(LOG_NORMAL, "component=mail_cleanup event=old_posts_deleted count=%d", deletedPostCount)
            // Log.Add(LOG_NORMAL, "[mail cleanup] Deleted %d old posts.", deletedPostCount)
            L2_LOG_INFO(Log, "[mail cleanup] Deleted %d old posts.", deletedPostCount);
        }
    }

    if (config.moveCommission) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_MoveCommission}";
        connection.Prepare(statement);
        connection.ExecutePrepared(statement);
    }

    CWareHouse::SetCheckSumOnDBForServerCrash();
    g_allianceDb.Init();
    CPledgeDb::TransferPledgeMasterBatch();
    CPledgeDb::ChangeNameByMergeBatch();
    CNewbieDb::LoadNewbieDataAll();
    g_newCharDB.LoadNewCharList();
    SevenSignQuestDB& sevenSignQuest = SevenSignQuestDB::Instance();
    sevenSignQuest.Load();
    TimeAttackDB::Instance().Load(sevenSignQuest.GetRoundNumber());
    CPledgeRanking::Load();
    g_readFile.MakeItemDataTable();
    g_userDb.LoadCommissionUser();
    CPledgeJoinDB::Instance().Init();
    CPacketStat<695, 234>::Inst().Init(L"ServerToCacheD");
    CPacketStat<506, 198>::Inst().Init(L"CacheDToServer");
}

class SocketSubsystemSession {
public:
    int Initialize() {
        const int result = portable::InitializeSocketSubsystem();
        active_ = result == 0;
        return result;
    }

    void Cleanup() {
        if (!active_)
            return;
        portable::CleanupSocketSubsystem();
        active_ = false;
    }

    ~SocketSubsystemSession() { Cleanup(); }

private:
    bool active_ = false;
};

void ReportBackgroundWorker(const char* role, bool started) noexcept {
    const int savedErrno = errno;
#if defined(_WIN32)
    const DWORD savedLastError = ::GetLastError();
#endif
    char message[256]{};
    if (started) {
        // std::snprintf(message, sizeof(message), "component=startup event=worker_started role=%s", role)
        std::snprintf(message, sizeof(message), "[startup] Started worker %s.", role);
    } else {
        // std::snprintf(message, sizeof(message),
        //     "component=startup event=worker_start_failed role=%s reason=thread_creation_or_registration_exception action=continue_startup", role)
        std::snprintf(message, sizeof(message),
            "[startup] Could not start worker %s: thread creation or registration threw an exception. Startup continues.", role);
    }
    try {
        // Log.Add(started ? LOG_NORMAL : LOG_ERROR, "%s", message)
        L2_LOG(Log, started ? LOG_NORMAL : LOG_ERROR, "%s", message);
    } catch (...) {
        // A diagnostic failure must not enter the worker creation catch path or escape it.
        // runtime::logging::Write("cached", started ? runtime::logging::Level::Info : runtime::logging::Level::Error, message)
        runtime::logging::Write("cached", started ? runtime::logging::Level::Info : runtime::logging::Level::Error, message, __FILE__);
    }
    errno = savedErrno;
#if defined(_WIN32)
    ::SetLastError(savedLastError);
#endif
}

class CacheDBackgroundThreads {
public:
    ~CacheDBackgroundThreads() {
        g_processLifecycle.RequestStop();
        Join();
    }

    bool StartPerfMon() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (perfMon_.joinable())
            return false;
        try {
            perfMon_ = std::thread([] { PerfMonThread(nullptr); });
            CThreadManager::SetThread(ThreadID::PERFMON, runtime::NativeThreadHandle(perfMon_));
            ReportBackgroundWorker("perfmon", true);
            return true;
        } catch (...) {
            // The target does not check _beginthreadex's result and keeps
            // startup moving with a null logical-thread handle.
            CThreadManager::SetThread(ThreadID::PERFMON, nullptr);
            ReportBackgroundWorker("perfmon", false);
            return false;
        }
    }

    bool StartFileServer() {
        std::lock_guard<std::mutex> lock(mutex_);
        // The current process handle starts as INVALID_HANDLE_VALUE. Once
        // _beginthreadex has been attempted it becomes either a real handle
        // or zero, and command 0x8018 never retries either state.
        if (fileServerStartAttempted_)
            return false;
        fileServerStartAttempted_ = true;
        try {
            fileServer_ = std::thread([] { FileServerThread(nullptr); });
            CThreadManager::SetThread(ThreadID::FILE_SERVER, runtime::NativeThreadHandle(fileServer_));
            ReportBackgroundWorker("file_server", true);
            return true;
        } catch (...) {
            CThreadManager::SetThread(ThreadID::FILE_SERVER, nullptr);
            ReportBackgroundWorker("file_server", false);
            return false;
        }
    }

    void Join() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (perfMon_.joinable())
            perfMon_.join();
        if (fileServer_.joinable())
            fileServer_.join();
    }

private:
    std::mutex mutex_;
    bool fileServerStartAttempted_ = false;
    std::thread perfMon_;
    std::thread fileServer_;
};

CacheDBackgroundThreads g_backgroundThreads;

bool InitializeListenThreadPersistence(const CacheDConfiguration& config) {
    if (config.serialKeyValidationEnabled) {
        // Current Windows Cached.exe loads this exact REG_BINARY value through
        // Shared/Utils.cpp. POSIX has no registry, so only that transport is
        // replaced by the explicit local file. The validator remains the
        // exact current algorithm. This enclosing policy gate is a documented
        // portable extension and does not generate or accept arbitrary keys.
        std::string serialKey;
#if defined(_WIN32)
        wchar_t serialKeyBuffer[256]{};
        if (LoadStrFromReg(serialKeyBuffer, L"SerialKey", L"Software\\PROJECT_L2\\L2CACHED")) {
            serialKey = WideToUtf8(serialKeyBuffer);
        }
#else
        serialKey = ReadSerialFile(runtime::ConfigFile("./SerialKey.txt").string());
#endif
        if (serialKey.empty()) {
#if defined(_WIN32)
            if (config.userInterface == runtime::UserInterface::Gui) {
                ::DialogBoxParamW(
                    g_cachedInstance, MAKEINTRESOURCEW(0x8e), nullptr, reinterpret_cast<DLGPROC>(&SerialKeyBox), 0
                );
                ::MessageBoxW(nullptr, L"Execute CacheD Again", L"Validation", 0);
                ::ExitProcess(0);
            }
            L2_LOG_ERROR(Log,
                "[startup] Serial key is missing from the CacheD registry settings. Startup aborted (exit code 3).");
            SetListenThreadResult(3);
            return false;
#else
            // Log.Add(LOG_ERROR, "SerialKey.txt is required for portable CacheD validation")
            // Log.Add(
            //     LOG_ERROR,
            //     "component=startup event=serial_key_unavailable path=\"%s\" reason=missing_unreadable_or_empty action=abort_startup result=3",
            //     runtime::ConfigFile("./SerialKey.txt").string().c_str())
            // Log.Add(
            //     LOG_ERROR,
            //     "[startup] Serial key \"%s\" is missing, unreadable or empty. Startup aborted (exit code 3).",
            //     runtime::ConfigFile("./SerialKey.txt").string().c_str())
            L2_LOG_ERROR(Log,
                "[startup] Serial key \"%s\" is missing, unreadable or empty. Startup aborted (exit code 3).",
                runtime::ConfigFile("./SerialKey.txt").string().c_str());
            SetListenThreadResult(3);
            return false;
#endif
        }
        const runtime::SerialKeyHostIdentity identity =
            runtime::ReadSerialKeyHostIdentity();
        if (!runtime::ValidateSerialKey(serialKey, identity)) {
            // Log.Add(LOG_ERROR, "Serial Key is Invalid or Expired")
            // Log.Add(
            //     LOG_ERROR,
            //     "component=startup event=serial_key_rejected reason=invalid_or_expired action=abort_startup delay_ms=3000 result=4")
            // Log.Add(
            //     LOG_ERROR,
            //     "[startup] Serial key is invalid or expired. Exiting in 3 seconds (exit code 4).")
            L2_LOG_ERROR(Log,
                "[startup] Serial key is invalid or expired. Exiting in 3 seconds (exit code 4).");
            std::this_thread::sleep_for(std::chrono::seconds(3));
            SetListenThreadResult(4);
            return false;
        }
    } else {
        // Log.Add(LOG_WARN, "[portable] serial-key validation is disabled by configuration")
        // Log.Add(
        //     LOG_NORMAL,
        //     "component=config event=serial_key_validation_disabled source=configuration")
        // Log.Add(
        //     LOG_NORMAL,
        //     "[config] Serial-key validation is disabled by configuration.")
        L2_LOG_INFO(Log,
            "[config] Serial-key validation is disabled by configuration.");
    }

    CacheDDatabasePlan databasePlan;
    std::string databaseError;
    if (!LoadCacheDDatabasePlan(runtime::ConfigFile("./l2cache.ini").string(), config.worldId, databasePlan, databaseError, false)) {
        // Log.Add(LOG_ERROR, "[DBConnection] %s", databaseError.c_str())
        // Log.Add(LOG_ERROR,
        //     "component=startup event=persistence_configuration_failed stage=database_plan world_id=%d detail_preview=\"%.512s\" action=abort_startup result=6",
        //     config.worldId, databaseError.c_str())
        // Log.Add(LOG_ERROR,
        //     "[database] Invalid database plan for world %d: %.512s. Startup aborted (exit code 6).",
        //     config.worldId, databaseError.c_str())
        // Log.Add(LOG_ERROR,
        //     "[database] Startup aborted for world %d (exit code 6). Invalid database plan: %.512s",
        //     config.worldId, databaseError.c_str())
        L2_LOG_ERROR(Log,
            "[database] Startup aborted for world %d (exit code 6); invalid database plan: %.512s",
            config.worldId, databaseError.c_str());
        SetListenThreadResult(6);
        return false;
    }

    database::PostgreSQLRuntimeConfig postgres;
    if (!BuildPostgreSQLRuntimeConfiguration(
            runtime::ConfigFile("./etc/cached-postgresql.ini").string(), config, databasePlan, postgres, databaseError, false
        )) {
        // Log.Add(LOG_ERROR, "[DBConnection] %s", databaseError.c_str())
        // Log.Add(LOG_ERROR,
        //     "component=startup event=persistence_configuration_failed stage=postgresql_configuration world_id=%d detail_preview=\"%.512s\" action=abort_startup result=6",
        //     config.worldId, databaseError.c_str())
        // Log.Add(LOG_ERROR,
        //     "[database] Invalid PostgreSQL configuration for world %d: %.512s. Startup aborted (exit code 6).",
        //     config.worldId, databaseError.c_str())
        // Log.Add(LOG_ERROR,
        //     "[database] Startup aborted for world %d (exit code 6). Invalid PostgreSQL configuration: %.512s",
        //     config.worldId, databaseError.c_str())
        L2_LOG_ERROR(Log,
            "[database] Startup aborted for world %d (exit code 6); invalid PostgreSQL configuration: %.512s",
            config.worldId, databaseError.c_str());
        SetListenThreadResult(6);
        return false;
    }
    if (!ApplyCacheDPostgreSQLMigrations(
            runtime::ConfigFile("./etc/cached-postgresql.ini").string(), postgres, databaseError)) {
        // Log.Add(LOG_ERROR, "[migrations] %s", databaseError.c_str())
        // Log.Add(LOG_ERROR, "component=migrations event=failed %s", databaseError.c_str())
        // Log.Add(
        //     LOG_ERROR,
        //     "component=migrations event=failed world_id=%d detail_preview=\"%.512s\" action=abort_startup result=6",
        //     config.worldId, databaseError.c_str())
        // Log.Add(
        //     LOG_ERROR,
        //     "[migrations] Failed for world %d: %.512s. Startup aborted (exit code 6).",
        //     config.worldId, databaseError.c_str())
        // Log.Add(
        //     LOG_ERROR,
        //     "[migrations] Startup aborted for world %d (exit code 6): %.512s",
        //     config.worldId, databaseError.c_str())
        L2_LOG_ERROR(Log,
            "[migrations] Startup aborted for world %d (exit code 6): %.512s",
            config.worldId, databaseError.c_str());
        SetListenThreadResult(6);
        return false;
    }
    // Current DBConn::Error 0x004a5d14 reaches
    // SystemMail::sendCannotFindSPMail at 0x0059a9d0. This callback reconnects
    // that same product edge across the portable libpq/static-library split.
    InstallMissingProcedureReporter(postgres);
    postgres.slowQueryObserver = [](const std::string& statement, std::uint32_t durationMilliseconds) {
        const std::wstring targetStatement = Utf8ToWide(statement);
        g_qsCheck.AddSlowQs(targetStatement.c_str(), durationMilliseconds);
    };
    postgres.successfulQueryObserver = [](const std::string& statement, std::uint32_t durationMilliseconds) {
        const std::wstring targetStatement = Utf8ToWide(statement);
        g_qsCheck.AddValue(targetStatement.c_str(), durationMilliseconds);
    };
    if (!GetSqlPoolManager().ConfigurePostgreSQL(std::move(postgres))) {
        SetListenThreadResult(6);
        return false;
    }

    // Current 0x00459c5a..0x00459c79: timer first, SQL-pool Init second,
    // then the DB-version check only on a development server. Live/test/
    // movie/broadcast/worldserver do not execute that gate here.
    g_qsCheckManager.StartTimer();
    GetSqlPoolManager().Init();
    if (config.serverType == CacheDServerType::Dev && !CheckDatabaseVersion(config.worldId)) {
        SetListenThreadResult(6);
        return false;
    }

    RunDatabaseJobAccountItemStartupPhase(config);
    return true;
}

// Current Cached.cpp 0x00459928. The exact literal survives in the guard
// stack even though the damaged PDB no longer supplies a function record.
void ListenThread_common() {
    // Current 0x00459944: this is the first call in the shipping body.
    RenameOldLog();

    const CacheDConfiguration& config = GetCacheDConfiguration();
    runtime::process::SetCurrentDatabaseId(config.worldId);

    SocketSubsystemSession socketSubsystem;
    const int socketResult = socketSubsystem.Initialize();
    if (socketResult != 0) {
        // Log.Add(LOG_ERROR, "WSAStartup error 0x%x", socketResult)
        // Log.Add(
        //     LOG_ERROR,
        //     "component=network event=subsystem_initialization_failed subsystem=winsock_compatible code=%d action=abort_startup result=7",
        //     socketResult)
        // Log.Add(
        //     LOG_ERROR,
        //     "[network] Could not initialize the socket subsystem (Winsock-compatible error %d). Startup aborted (exit code 7).",
        //     socketResult)
        L2_LOG_ERROR(Log,
            "[network] Could not initialize the socket subsystem (Winsock-compatible error %d). Startup aborted (exit code 7).",
            socketResult);
        SetListenThreadResult(7);
        return;
    }

    const int ioThreadCount = GetIOThreadCount();
    if (!InitializeListenThreadPersistence(config)) {
        return;
    }

    // Current 0x0045a151..0x0045a1c0: workers first, then the three fixed
    // INADDR_ANY listeners in NPC/L2Server/admin order.
    CreateIOThread(ioThreadCount);
    g_NpcServer.Run(12008, &CNpcDbSocket::Allocate, false, 0, 0);
    g_gameDbServer.Run(12006, &CSocket::Allocate, false, 0, 0);
    g_adminServer.Run(12012, &CAdminSocket::Allocate, false, 0, 0);

    runtime::log::ConfigureLog(
        Log,
        runtime::log::LogOptions{
            config.logDAddress, static_cast<int>(config.logDPort), config.worldId, "log", config.logFileDurationMinutes}
    );
    // CIOSocketEx::OnCreate arms the first WSARecv in the Windows target.
    // The portable native reader is held until CacheD's IO
    // workers exist, so release it at this equivalent post-CreateIOThread
    // boundary before waking the recovered LogD connector.
    runtime::log::StartLogReceiving(Log);
    Log.ConnectToLogD();
    GIP.Init();

    if (l2npc::g_PerfMon.Init()) {
        g_backgroundThreads.StartPerfMon();
    }
    if (g_fileServerManager.Initialize()) {
        // Log.Add(LOG_ERROR, L"[FileServer] FileServer Setting Load Success!")
        // Log.Add(LOG_NORMAL, "component=file_server event=configuration_loaded")
        // Log.Add(
        //     LOG_DEBUG,
        //     "component=file_server event=worker_start_requested source=startup")
        // Log.Add(
        //     LOG_DEBUG,
        //     "[FileServer] Requesting the worker to start.")
        // Previous: L2_LOG_DEBUG(Log,
        //     "[FileServer] Requesting the worker to start.")
        L2_LOG_DEBUG(Log,
            "[file server] Requesting the worker to start.");
        g_backgroundThreads.StartFileServer();
    } else {
        // Log.Add(LOG_ERROR, L"[FileServer] File Server is not Ready! Check Config")
        // Log.Add(LOG_ERROR, "component=file_server event=initialization_failed, check FileServer configuration")
        // Log.Add(
        //     LOG_WARN,
        //     "component=file_server event=startup_degraded reason=initialization_failed worker_start=skipped action=continue_startup")
        // Log.Add(
        //     LOG_WARN,
        //     "[FileServer] Initialization failed, the worker will not start. Server startup continues.")
        // Previous: L2_LOG_WARN(Log,
        //     "[FileServer] Initialization failed, the worker will not start. Server startup continues.")
        L2_LOG_WARN(Log,
            "[file server] Initialization failed; the worker will not start. Server startup continues.");
    }

    if (config.useServerStartMail) {
        SystemMail::sendServerStartCompleted(mailServer);
    }

    CCategoryDataDB::Inst().Load();
    CategoryContent::CategoryContentInit();
    CategoryContent::SetupCategoryDB();
    // Log.Add(LOG_NORMAL, L"Script load finished!!")
    // Log.Add(LOG_NORMAL, "component=startup event=script_load_completed")
    // Log.Add(LOG_NORMAL, "[startup] Script loading completed.")
    L2_LOG_INFO(Log, "[startup] Script loading completed.");

    ServerAutoStarter serverAutoStarter;
    serverAutoStarter.Do();
    g_consoleReady.store(true, std::memory_order_release);
    WaitThread(nullptr);

    {
        // Let a running command finish before listener resources stop.
        const std::lock_guard<std::mutex> guard(g_consoleCommandMutex);
        g_consoleReady.store(false, std::memory_order_release);
    }

    // WaitThread is process-lifetime in Cached.exe. These operations are the
    // portable console-host equivalent of process teardown after it returns.
    // they do not extend any recovered target class API.
    g_processLifecycle.RequestStop();
    g_NpcServer.Stop();
    g_gameDbServer.Stop();
    g_adminServer.Stop();
    runtime::StopGameInfoPipeline(GIP);
    portable::StopCacheDIOThreadRuntime();
    g_backgroundThreads.Join();
    runtime::log::StopLogTransport(Log);

    // Current 0x0045a30b calls WSACleanup before the local
    // ServerAutoStarter destructor closes its two process handles.
    socketSubsystem.Cleanup();
}

// Current Cached.cpp 0x0045a398. SEH metadata supplies the mailing boundary.
// the visible source body is exactly the common call.
void ListenThread_mailing() { ListenThread_common(); }

// Current Cached.cpp 0x0045a3e8.
std::uint32_t ListenThread(void*) {
    constexpr int threadId = ThreadID::LISTEN;
    CThreadManager::RegisterThisThread(threadId);
    InitializeCurrentIOBufferPool(0x80);
    if (ExceptionMailing()) {
        ListenThread_mailing();
    } else {
        ListenThread_common();
    }
    return 0;
}

bool StartListenThreadRuntime() {
    g_listenThreadResult.store(0, std::memory_order_release);
    g_consoleReady.store(false, std::memory_order_release);
    g_listenThreadDone.store(false, std::memory_order_release);
    if (!g_processLifecycle.StartListener([] {
            ListenThread(nullptr);
            g_listenThreadDone.store(true, std::memory_order_release);
        })) {
        CThreadManager::SetThread(ThreadID::LISTEN, nullptr);
        return false;
    }
    CThreadManager::SetThread(ThreadID::LISTEN, g_processLifecycle.ListenerNativeHandle());
    return true;
}

void JoinListenThreadRuntime() {
    g_processLifecycle.JoinListener();
    CThreadManager::SetThread(ThreadID::LISTEN, nullptr);
}

// Called only by the Windows GUI entry point.
[[maybe_unused]] void TestPacketTableRegistration() {
    // Current Cached.cpp 0x00455ff8 calls all six template owners in this
    // exact order and discards each boolean result.
    // FixedSizeCallbackMap<235>::TestRegistration: 0x00455ba4,
    // SHA-256 04e1e4f89ac1a2b15c1a7f12b976d2ceab56a0cf191d55f4ad2fd7fe8328abc5.
    gPacketTableFromServer.TestRegistration();
    // FixedSizeCallbackMap<461>::TestRegistration: 0x00455c14,
    // SHA-256 56bf039c6572af22ce196cc45f0c82682760d51a319d0b8439572cc0d1af6221.
    gPacketTableFromServerEx.TestRegistration();
    // FixedSizeCallbackMap<116>::TestRegistration: 0x00455c84,
    // SHA-256 314bc5a25dbdbb06dd61719366f525d7ffb48f14dd2e01e37e744692b1484223.
    g_DxPacketTable.TestRegistration();
    // FixedSizeCallbackMap<4>::TestRegistration: 0x00455cf0,
    // SHA-256 17b726e825fe6b252466da8cd561e16a093eabb0de2192d1ff4124041ad97c02.
    g_QueryPacketTable.TestRegistration();
    // FixedSizeCallbackMap<19>::TestRegistration: 0x00455d58,
    // SHA-256 c8adcfa47a5146207c2ffe506b5b4ab117bd39da783aa45a590e9f5e4ee8cdd6.
    gNpcDbPacketTable.TestRegistration();
    // FixedSizeCallbackMap<191>::TestRegistration: 0x00455dc4,
    // SHA-256 f61018d07dbcfd8ad1ebf6d4c770ac196f08bbabd6d290774ecfa3046eea7e83.
    gPacketTableFromAdmin.TestRegistration();
}

} // namespace

int g_MonRaceTaxRate = 0;
int g_nCountryCode = 0;
bool g_bUseDBSecurity = false;
int g_nDBSecurityPenaltyLevel = 0;
std::uint32_t g_bSaveQsData = 0;
bool g_bServerToCacheDPacketStatMonitor = false;
bool g_bCacheDToServerPacketStatMonitor = false;
bool g_bExceptionMakeMiniDump = false;
BinaryInfo g_BinaryInfo{};
int g_nServerId = 1;
int g_nWorldId = 1;

// Original rollover body kept. The logging overlay below observes errors
// without changing the path, rename policy, fallback or startup continuation.
// void RenameOldLog() {
//     // Current Cached.cpp 0x00454b88. The target obtains the executable path,
//     // replaces its basename with LinError.txt, reads the file's 32-bit mtime
//     // and calls the wide CRT rename without checking its result. The temporary
//     // executable basename stripped of its extension is dead source state in
//     // this shipping body and has no observable consumer.
// #if defined(_WIN32)
//     std::array<wchar_t, 0x104> modulePath{}
//     if (::GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size())) == 0) {
//         modulePath[0] = L'\0'.
//     }
// 
//     wchar_t* fileName = std::wcsrchr(modulePath.data(), L'\\')
//     fileName = fileName == nullptr ? modulePath.data() : fileName + 1
// 
//     std::array<wchar_t, 0x104> executableName{}
//     std::wcsncpy(executableName.data(), L"Unknown", executableName.size() - 1)
//     ::lstrcpyW(executableName.data(), fileName)
//     if (wchar_t* extension = std::wcsrchr(executableName.data(), L'.')) {
//         *extension = L'\0'
//     }
// 
//     ::lstrcpyW(fileName, L"LinError.txt")
//     struct _stat32 status{}.
//     if (::_wstat32(modulePath.data(), &status) != 0) {
//         return.
//     }
// 
//     std::tm* const localTime = ::_localtime32(&status.st_mtime)
//     std::array<wchar_t, 0x104> backupPath{}
//     std::wcsncpy(backupPath.data(), L"Unknown", backupPath.size() - 1)
//     ::swprintf_s(
//         backupPath.data(), backupPath.size(), L"%ls.%04d-%02d-%02d-%02d-%02d-%02d.bak", modulePath.data(),
//         localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday, localTime->tm_hour, localTime->tm_min,
//         localTime->tm_sec
//     )
//     ::_wrename(modulePath.data(), backupPath.data())
// #else
//     const std::filesystem::path executablePath = ExecutablePathForLogRollover()
//     std::error_code error
//     const std::filesystem::path directory =
//         executablePath.empty() ? std::filesystem::current_path(error) : executablePath.parent_path()
//     if (error)
//         return.
// 
//     const std::filesystem::path logPath = directory / "LinError.txt"
//     const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(logPath, error)
//     if (error)
//         return.
// 
//     const auto systemWriteTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
//         writeTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
//     )
//     const std::int32_t timestamp = static_cast<std::int32_t>(std::chrono::system_clock::to_time_t(systemWriteTime))
//     const std::tm* const localTime = LocalTime32ForLogRollover(timestamp)
// 
//     std::array<wchar_t, 0x104> backupPath{}
//     std::wcsncpy(backupPath.data(), L"Unknown", backupPath.size() - 1)
//     const std::wstring logPathWide = logPath.wstring()
//     std::swprintf(
//         backupPath.data(), backupPath.size(), L"%ls.%04d-%02d-%02d-%02d-%02d-%02d.bak", logPathWide.c_str(),
//         localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday, localTime->tm_hour, localTime->tm_min,
//         localTime->tm_sec
//     )
//     error.clear()
//     std::filesystem::rename(logPath, std::filesystem::path(backupPath.data()), error)
// #endif
// }
void RenameOldLog() {
    // Current Cached.cpp 0x00454b88. The target obtains the executable path,
    // replaces its basename with LinError.txt, reads the file's 32-bit mtime
    // and calls the wide CRT rename without checking its result. The temporary
    // executable basename stripped of its extension is dead source state in
    // this shipping body and has no observable consumer.
#if defined(_WIN32)
    std::array<wchar_t, 0x104> modulePath{};
    if (::GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size())) == 0) {
        {
            const runtime::logging::StorageErrorState preserveErrors;
            L2_LOG_WARN(Log,
                "[crash reports] Cannot determine the executable path (Windows error %lu); looking for LinError.txt in the current directory.",
                static_cast<unsigned long>(preserveErrors.savedWindowsError));
        }
        modulePath[0] = L'\0';
    }

    wchar_t* fileName = std::wcsrchr(modulePath.data(), L'\\');
    fileName = fileName == nullptr ? modulePath.data() : fileName + 1;

    std::array<wchar_t, 0x104> executableName{};
    std::wcsncpy(executableName.data(), L"Unknown", executableName.size() - 1);
    ::lstrcpyW(executableName.data(), fileName);
    if (wchar_t* extension = std::wcsrchr(executableName.data(), L'.')) {
        *extension = L'\0';
    }

    ::lstrcpyW(fileName, L"LinError.txt");
    struct _stat32 status{};
    if (::_wstat32(modulePath.data(), &status) != 0) {
        const runtime::logging::StorageErrorState preserveErrors;
        // Noise: No previous crash report is an expected startup state.
        if (preserveErrors.savedErrno != ENOENT) {
            L2_LOG_WARN(
                Log, u"[crash reports] Cannot read metadata for \"%ls\" (errno %d); backup skipped, startup continues.",
                modulePath.data(), preserveErrors.savedErrno
            );
        }
        return;
    }

    std::tm* const localTime = ::_localtime32(&status.st_mtime);
    std::array<wchar_t, 0x104> backupPath{};
    std::wcsncpy(backupPath.data(), L"Unknown", backupPath.size() - 1);
    const int backupFormatResult = ::swprintf_s(
        backupPath.data(), backupPath.size(), L"%ls.%04d-%02d-%02d-%02d-%02d-%02d.bak", modulePath.data(),
        localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday, localTime->tm_hour, localTime->tm_min,
        localTime->tm_sec
    );
    if (backupFormatResult < 0) {
        const runtime::logging::StorageErrorState preserveErrors;
        L2_LOG_WARN(
            Log,
            u"[crash reports] Cannot format the backup filename for \"%ls\" (errno %d); rename will still be attempted.",
            modulePath.data(), preserveErrors.savedErrno
        );
    }
    const int renameResult = ::_wrename(modulePath.data(), backupPath.data());
    if (renameResult != 0) {
        const runtime::logging::StorageErrorState preserveErrors;
        L2_LOG_WARN(
            Log,
            u"[crash reports] Cannot move \"%ls\" to \"%ls\" (errno %d); startup continues without completing this backup.",
            modulePath.data(), backupPath.data(), preserveErrors.savedErrno
        );
    }
#else
    const std::filesystem::path executablePath = ExecutablePathForLogRollover();
    if (executablePath.empty()) {
        const runtime::logging::StorageErrorState preserveErrors;
        L2_LOG_WARN(Log,
            "[crash reports] Executable path is unavailable; looking for LinError.txt in the current directory.");
    }
    std::error_code error;
    const std::filesystem::path directory =
        executablePath.empty() ? std::filesystem::current_path(error) : executablePath.parent_path();
    if (error) {
        const runtime::logging::StorageErrorState preserveErrors;
        L2_LOG_WARN(Log,
            "[crash reports] Cannot determine the current directory (%s error %d); LinError.txt backup skipped, startup continues.",
            error.category().name(), error.value());
        return;
    }

    const std::filesystem::path logPath = directory / "LinError.txt";
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(logPath, error);
    if (error) {
        const runtime::logging::StorageErrorState preserveErrors;
        // Noise: No previous crash report is an expected startup state.
        if (error && error != std::errc::no_such_file_or_directory) {
            L2_LOG_WARN(Log,
                "[crash reports] Cannot read the modification time of \"%s\" (%s error %d); backup skipped, startup continues.",
                logPath.c_str(), error.category().name(), error.value());
        }
        return;
    }

    const auto systemWriteTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        writeTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    const std::int32_t timestamp = static_cast<std::int32_t>(std::chrono::system_clock::to_time_t(systemWriteTime));
    const std::tm* const localTime = LocalTime32ForLogRollover(timestamp);

    std::array<wchar_t, 0x104> backupPath{};
    std::wcsncpy(backupPath.data(), L"Unknown", backupPath.size() - 1);
    const std::wstring logPathWide = logPath.wstring();
    const int backupFormatResult = std::swprintf(
        backupPath.data(), backupPath.size(), L"%ls.%04d-%02d-%02d-%02d-%02d-%02d.bak", logPathWide.c_str(),
        localTime->tm_year + 1900, localTime->tm_mon + 1, localTime->tm_mday, localTime->tm_hour, localTime->tm_min,
        localTime->tm_sec
    );
    if (backupFormatResult < 0) {
        const runtime::logging::StorageErrorState preserveErrors;
        L2_LOG_WARN(Log,
            "[crash reports] Cannot format the backup filename for \"%s\" (errno %d); rename will still be attempted.",
            logPath.c_str(), preserveErrors.savedErrno);
    }
    error.clear();
    std::filesystem::rename(logPath, std::filesystem::path(backupPath.data()), error);
    if (error) {
        const runtime::logging::StorageErrorState preserveErrors;
        L2_LOG_WARN(Log,
            "[crash reports] Cannot move \"%s\" to \"%ls\" (%s error %d); startup continues without completing this backup.",
            logPath.c_str(), backupPath.data(), error.category().name(), error.value());
    }
#endif
}

// Noise: The portable production tree declares this table but has no writer
// or escaping address. Its zero-filled rows are not observed reference counts.
// Retain the complete former report, including its original string formats,
// and revisit it if work-type instrumentation is introduced.
// void ReportClassWorkRefCount() {
//     // Current Cached.cpp 0x00454d10. The 512 x 1024-byte carrier and its
//     // row selection are exact. Bounded incremental formatting avoids the
//     // self-overlapping sprintf call compiled by the original MSVC CRT.
//     L2_GUARD_FUNCTION()
//     // Log.Add(LOG_NORMAL, "============  RefCount Report START ============")
//     L2_LOG_INFO(Log, "============  RefCount Report START ============")
// 
//     using ReportLine = std::array<char, 1024>
//     std::array<ReportLine, 512> reportLines{}
//     auto appendValue = [](ReportLine& line, std::int32_t value) {
//         const std::size_t used = std::strlen(line.data())
//         if (used >= line.size())
//             return.
//         std::snprintf(line.data() + used, line.size() - used, "%15ld", static_cast<long>(value))
//     }
// 
//     for (std::int32_t workType = 0, workType < static_cast<std::int32_t>(kMemoryWorkTypeCount), ++workType) {
//         appendValue(reportLines[0], workType)
//     }
//     // Log.Add(LOG_NORMAL, "[%30s]\t[%d]\t%s", "", 0, reportLines[0].data())
//     L2_LOG_INFO(Log, "[%30s]\t[%d]\t%s", "", 0, reportLines[0].data())
// 
//     for (int classIndex = 0. ClassIndex < g_nClassCount. ++classIndex) {
//         ReportLine& line = reportLines[static_cast<std::size_t>(classIndex + 1)]
//         for (std::size_t workType = 0, workType < kMemoryWorkTypeCount, ++workType) {
//             appendValue(line, g_pClassWorkRefCount[static_cast<std::size_t>(classIndex)][workType])
//         }
//         // Log.Add(
//         //     LOG_NORMAL, "[%30s]\t[%d]\t%s", g_pClassRefName[static_cast<std::size_t>(classIndex)], classIndex, line.data()
//         // )
//         L2_LOG_INFO(Log, "[%30s]\t[%d]\t%s", g_pClassRefName[static_cast<std::size_t>(classIndex)], classIndex, line.data()
//         )
//     }
// 
//     // Log.Add(LOG_NORMAL, "============  RefCount Report END ============")
//     L2_LOG_INFO(Log, "============  RefCount Report END ============")
// }
void ReportClassWorkRefCount() {
    L2_GUARD_FUNCTION();
    L2_LOG_INFO(Log,
        "[memory] Reference counts by work type are not collected by this build.");
}

#if defined(_WIN32)
namespace {

std::array<wchar_t, 100> g_cachedTitle{};
std::array<wchar_t, 100> g_cachedWindowClass{};

std::int64_t About(HWND__* window, std::uint32_t message, std::uint64_t wParam, std::int64_t lParam) {
    // Current Cached.cpp 0x00454710.
    L2_GUARD_FUNCTION();

    if (message == WM_INITDIALOG)
        return 1;
    if (message == WM_COMMAND) {
        const std::uint16_t command = static_cast<std::uint16_t>(wParam);
        if (command == IDOK || command == IDCANCEL) {
            ::EndDialog(static_cast<HWND>(window), static_cast<INT_PTR>(command));
            return 1;
        }
    }
    return 0;
}

std::int64_t DirectCommandBox(HWND__* window, std::uint32_t message, std::uint64_t wParam, std::int64_t lParam) {
    // Current Cached.cpp 0x00454958.
    L2_GUARD_FUNCTION();

    if (message == WM_INITDIALOG)
        return 1;
    if (message != WM_COMMAND)
        return 0;

    const std::uint16_t command = static_cast<std::uint16_t>(wParam);
    if (command == IDOK) {
        std::array<wchar_t, 20480> input{};
        ::SendDlgItemMessageW(static_cast<HWND>(window), 0x3f6, WM_GETTEXT, 0x40, reinterpret_cast<LPARAM>(input.data()));
        char16_t* token = nullptr;
        GetLeftString(reinterpret_cast<char16_t*>(input.data()), &token);
        const wchar_t* const text = reinterpret_cast<const wchar_t*>(token);
        if (std::wcscmp(text, L"CriticalCallStack") == 0) {
            SystemMail::sendCriticalCallstack(mailServer);
        } else if (std::wcscmp(text, L"profile") == 0) {
            SystemMail::sendReportProfile(mailServer);
        } else if (std::wcscmp(text, L"Exception") == 0) {
            L2Exception::OnException();
        } else if (std::wcscmp(text, L"Crash") == 0) {
            ForceCrash(CrashInfo::BY_CRASH_COMMAND);
        }
    } else if (command != IDCANCEL) {
        return 0;
    }

    ::EndDialog(static_cast<HWND>(window), static_cast<INT_PTR>(command));
    return 1;
}

double PacketBufferUseRate(std::int64_t byteCount, std::int64_t packetCount) {
    const std::int64_t denominator = std::max<std::int64_t>(packetCount, 1) * 0x5000;
    return static_cast<double>(byteCount) / static_cast<double>(denominator) * 100.0;
}

void ReportPacketBufferUsage() {
    const std::int64_t sendBytes = g_nSocketSendBytes.load(std::memory_order_acquire);
    const std::int64_t receiveBytes = g_nSocketReceiveBytes.load(std::memory_order_acquire);
    const std::int64_t sendCount = g_nSocketSendCount.load(std::memory_order_acquire);
    const std::int64_t receiveCount = g_nSocketReceiveCount.load(std::memory_order_acquire);
    const std::int64_t sendBytesOver7K = g_nSocketSendBytesOver7K.load(std::memory_order_acquire);
    const std::int64_t receiveBytesOver7K = g_nSocketReceiveBytesOver7K.load(std::memory_order_acquire);
    const std::int64_t sendCountOver7K = g_nSocketSendCountOver7K.load(std::memory_order_acquire);
    const std::int64_t receiveCountOver7K = g_nSocketReceiveCountOver7K.load(std::memory_order_acquire);

    // Noise: the previous eight-line table used separator-only records and
    // repeated labels, so one snapshot was hard to identify in interleaved logs.
    // Keep both the shipping calls and the previous active rendering below.
    // publish the same counters as one component-prefixed record instead.
    // Log.Add(LOG_NORMAL, L"--------------------------------------------------------------------------")
    // Previous: L2_LOG_INFO(Log, L"--------------------------------------------------------------------------")
    // Log.Add(LOG_NORMAL, L"Packet Size       : %d", 0x5000)
    // Previous: L2_LOG_INFO(Log, L"Packet Size       : %d", 0x5000)
    // Log.Add(
    //     LOG_NORMAL, L"SendPacket    Sum : %I64d, Count : %I64d, Use Rate : %lf", sendBytes, sendCount,
    //     PacketBufferUseRate(sendBytes, sendCount)
    // )
    // Previous: L2_LOG_INFO(Log, L"SendPacket    Sum : %I64d, Count : %I64d, Use Rate : %lf", sendBytes, sendCount, PacketBufferUseRate(sendBytes, sendCount))
    // Log.Add(
    //     LOG_NORMAL, L"ReceivePacket Sum : %I64d, Count : %I64d, Use Rate : %lf", receiveBytes, receiveCount,
    //     PacketBufferUseRate(receiveBytes, receiveCount)
    // )
    // Previous: L2_LOG_INFO(Log, L"ReceivePacket Sum : %I64d, Count : %I64d, Use Rate : %lf", receiveBytes, receiveCount, PacketBufferUseRate(receiveBytes, receiveCount))
    // Log.Add(LOG_NORMAL, L"Packet Size       : 7k Over")
    // Previous: L2_LOG_INFO(Log, L"Packet Size       : 7k Over")
    // Log.Add(
    //     LOG_NORMAL, L"SendPacket    Sum : %I64d, Count : %I64d, Use Rate : %lf", sendBytesOver7K, sendCountOver7K,
    //     PacketBufferUseRate(sendBytesOver7K, sendCountOver7K)
    // )
    // Previous: L2_LOG_INFO(Log, L"SendPacket    Sum : %I64d, Count : %I64d, Use Rate : %lf", sendBytesOver7K, sendCountOver7K, PacketBufferUseRate(sendBytesOver7K, sendCountOver7K))
    // Log.Add(
    //     LOG_NORMAL, L"ReceivePacket Sum : %I64d, Count : %I64d, Use Rate : %lf", receiveBytesOver7K, receiveCountOver7K,
    //     PacketBufferUseRate(receiveBytesOver7K, receiveCountOver7K)
    // )
    // Previous: L2_LOG_INFO(Log, L"ReceivePacket Sum : %I64d, Count : %I64d, Use Rate : %lf", receiveBytesOver7K, receiveCountOver7K, PacketBufferUseRate(receiveBytesOver7K, receiveCountOver7K))
    // Log.Add(LOG_NORMAL, L"--------------------------------------------------------------------------")
    // Previous: L2_LOG_INFO(Log, L"--------------------------------------------------------------------------")
    L2_LOG_INFO(
        Log,
        u"[packet buffers] Capacity 20480 bytes: sent %I64d bytes in %I64d packets (average use %.2lf%%); "
        u"received %I64d bytes in %I64d packets (average use %.2lf%%). Over 7 KiB: sent %I64d bytes in %I64d packets "
        u"(average use %.2lf%%); received %I64d bytes in %I64d packets (average use %.2lf%%).",
        sendBytes, sendCount, PacketBufferUseRate(sendBytes, sendCount), receiveBytes, receiveCount,
        PacketBufferUseRate(receiveBytes, receiveCount), sendBytesOver7K, sendCountOver7K,
        PacketBufferUseRate(sendBytesOver7K, sendCountOver7K), receiveBytesOver7K, receiveCountOver7K,
        PacketBufferUseRate(receiveBytesOver7K, receiveCountOver7K)
    );
}

LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    // Current Cached.cpp 0x00458a48.
    L2_GUARD_FUNCTION();
    std::array<wchar_t, 100> unusedString{};
    ::LoadStringW(g_cachedInstance, 0x6a, unusedString.data(), static_cast<int>(unusedString.size()));
    HMENU const menu = ::GetMenu(window);

    if (message == WM_DESTROY) {
        g_qsCheck.SlowReport(LOG_ERROR);
        g_IsBySystemManager = true;
        ForceCrash(CrashInfo::BY_DESTROY_EVENT);
        ::PostQuitMessage(0);
        return 0;
    }

    if (message == WM_MOVE) {
        // Shipping quirk: WINDOWINFO::cbSize is never initialized.
        WINDOWINFO windowInfo;
        ::GetWindowInfo(window, &windowInfo);
        if ((windowInfo.dwStyle & 0x21000000U) == 0) {
            HKEY key;
            ::RegCreateKeyW(HKEY_LOCAL_MACHINE, L"Software\\PROJECT_L2\\L2CACHED", &key);
            ::RegSetValueExW(
                key, L"posx", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&windowInfo.rcWindow.left),
                sizeof(windowInfo.rcWindow.left)
            );
            ::RegSetValueExW(
                key, L"posy", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&windowInfo.rcWindow.top),
                sizeof(windowInfo.rcWindow.top)
            );
            const int width = windowInfo.rcWindow.right - windowInfo.rcWindow.left;
            const int height = windowInfo.rcWindow.bottom - windowInfo.rcWindow.top;
            ::RegSetValueExW(key, L"sizex", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&width), sizeof(width));
            ::RegSetValueExW(key, L"sizey", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&height), sizeof(height));
            ::RegCloseKey(key);
        }
        return 0;
    }

    if (message == WM_SIZE) {
        CachedStatusWnd::Init().OnMainWndResized(static_cast<std::uint64_t>(wParam), static_cast<std::int64_t>(lParam));
        return 0;
    }
    if (message == WM_PAINT) {
        Log.Redraw();
        return 0;
    }
    if (message == WM_KEYDOWN) {
        if (wParam == VK_F2 && GetCacheDConfiguration().directCommand) {
            ::DialogBoxParamW(
                g_cachedInstance, MAKEINTRESOURCEW(0x91), window, reinterpret_cast<DLGPROC>(&DirectCommandBox), 0
            );
        }
        return 0;
    }

    if (message == WM_COMMAND) {
        const std::uint16_t command = LOWORD(wParam);
        switch (command) {
        case 0x68:
            ::DialogBoxParamW(g_cachedInstance, MAKEINTRESOURCEW(0x67), window, reinterpret_cast<DLGPROC>(&About), 0);
            return 0;
        case 0x69: {
            GuardHelper exitGuard(L"IDM_EXIT", CThreadManager::GetThreadID());
            g_qsCheck.SlowReport(LOG_ERROR);
            g_IsBySystemManager = true;
            ForceCrash(CrashInfo::BY_EXIT_MENU);
            return 0;
        }
        case 0x85: {
            GuardHelper crashReportGuard(L"crash_report", CThreadManager::GetThreadID());
            g_qsCheck.SlowReport(LOG_ERROR);
            g_IsBySystemManager = true;
            ForceCrash(CrashInfo::BY_CRASH_REPORT);
            return 0;
        }
        case 0x88:
            GuardProfileHelper::ResetGuardProfile();
            return 0;
        case 0x89:
            g_qsCheck.Report(LOG_NORMAL);
            return 0;
        case 0x8a:
            g_qsCheck.SlowReport(LOG_NORMAL);
            return 0;
        case 0x8c:
            ReportGuardProfile();
            return 0;
        case 0x8e:
            g_qsCheck.ShowPacketReport();
            return 0;
        case 0x8f:
            CQsCheck::s_packetReport = !CQsCheck::s_packetReport;
            // Log.Add(
            //     LOG_ERROR, CQsCheck::s_packetReport ? L"packet report is enabled." : L"packet report is disabled."
            // )
            // Previous: L2_LOG_ERROR(Log, CQsCheck::s_packetReport ? L"packet report is enabled." : L"packet report is disabled.")
            L2_LOG_INFO(Log, CQsCheck::s_packetReport ? L"[packet report] Enabled." : L"[packet report] Disabled.");
            return 0;
        case 0x90:
            g_qsCheck.ClearPacketReport();
            return 0;
        case 0x91:
            g_userDb.PrintAllAccountData();
            return 0;
        case 0x92:
            g_userDb.PrintAllAccountWarehouseData();
            return 0;
        case 0x93:
            g_qsCheck.ResetData();
            return 0;
        case 0x94:
            g_bSaveQsData = g_bSaveQsData == 0 ? 1U : 0U;
            ::CheckMenuItem(menu, 0x94, g_bSaveQsData != 0 ? MF_CHECKED : MF_UNCHECKED);
            return 0;
        case 0x96:
            // CPacketStat<695, 234>::Inst().Report(LOG_ERROR)
            CPacketStat<695, 234>::Inst().Report(LOG_NORMAL);
            // CPacketStat<506, 198>::Inst().Report(LOG_ERROR)
            CPacketStat<506, 198>::Inst().Report(LOG_NORMAL);
            return 0;
        case 0x97:
            CPacketStat<695, 234>::Inst().Reset();
            CPacketStat<506, 198>::Inst().Reset();
            return 0;
        case 0x98:
            // CPacketStat<695, 234>::Inst().ReportRank(LOG_ERROR, CPacketStat<695, 234>::PRED_CNT)
            CPacketStat<695, 234>::Inst().ReportRank(LOG_NORMAL, CPacketStat<695, 234>::PRED_CNT);
            // CPacketStat<695, 234>::Inst().ReportRank(LOG_ERROR, CPacketStat<695, 234>::PRED_BYTES)
            CPacketStat<695, 234>::Inst().ReportRank(LOG_NORMAL, CPacketStat<695, 234>::PRED_BYTES);
            // CPacketStat<506, 198>::Inst().ReportRank(LOG_ERROR, CPacketStat<506, 198>::PRED_CNT)
            CPacketStat<506, 198>::Inst().ReportRank(LOG_NORMAL, CPacketStat<506, 198>::PRED_CNT);
            // CPacketStat<506, 198>::Inst().ReportRank(LOG_ERROR, CPacketStat<506, 198>::PRED_BYTES)
            CPacketStat<506, 198>::Inst().ReportRank(LOG_NORMAL, CPacketStat<506, 198>::PRED_BYTES);
            return 0;
        case 0x8006:
            Log.ConnectToLogD();
            return 0;
        case 0x8007:
            g_addData.AddItemData();
            return 0;
        case 0x8008:
            g_addData.AddSkillData();
            return 0;
        case 0x8009:
            g_addData.AddQuestData();
            return 0;
        case 0x800a:
            ReportClassWorkRefCount();
            return 0;
        case 0x800b:
            ReportPacketBufferUsage();
            return 0;
        case 0x800c:
            ReportMemory();
            return 0;
        case 0x800d:
            ::DialogBoxParamW(
                g_cachedInstance, MAKEINTRESOURCEW(0x95), window, reinterpret_cast<DLGPROC>(&QsCheckTimeBox), 0
            );
            return 0;
        case 0x800f:
            l2npc::g_PerfMon.GetPerfMon();
            return 0;
        case 0x8013:
            g_bShowMallocLog = !g_bShowMallocLog;
            ::CheckMenuItem(menu, 0x8013, g_bShowMallocLog ? MF_CHECKED : MF_UNCHECKED);
            return 0;
        case 0x8015:
            g_bShowStatusWindow = !g_bShowStatusWindow;
            ::CheckMenuItem(menu, 0x8015, g_bShowStatusWindow ? MF_CHECKED : MF_UNCHECKED);
            if (g_bShowStatusWindow) {
                CachedStatusWnd::Init().Show();
            } else {
                CachedStatusWnd::Init().Hide();
            }
            return 0;
        case 0x8017:
            g_readFile.MakeItemDataTable();
            return 0;
        case 0x8018:
            if (!g_fileServerManager.Initialize()) {
                // Log.Add(LOG_ERROR, L"[FileServer] File Server is not Ready! Check Config")
                // Log.Add(LOG_ERROR, "component=file_server event=reload_failed, check FileServer configuration")
                // Log.Add(
                //     LOG_WARN,
                //     "component=file_server event=reload_failed configuration_state=possibly_partial action=return_to_message_loop")
                // Log.Add(
                //     LOG_WARN,
                //     "[FileServer] Configuration reload failed and may have applied some settings. Returning to the message loop.")
                // Previous: L2_LOG_WARN(Log,
                //     "[FileServer] Configuration reload failed and may have applied some settings. Returning to the message loop.")
                L2_LOG_WARN(Log,
                    "[file server] Configuration reload failed and may have applied some settings. Returning to the message loop.");
            } else {
                // Log.Add(LOG_ERROR, L"[FileServer] FileServer Setting Reload Success!")
                // Log.Add(LOG_NORMAL, "component=file_server event=configuration_reloaded")
                // Log.Add(
                //     LOG_DEBUG,
                //     "component=file_server event=worker_start_requested source=configuration_reload")
                // Log.Add(
                //     LOG_DEBUG,
                //     "[FileServer] Requesting the worker to start after configuration reload.")
                // Previous: L2_LOG_DEBUG(Log,
                //     "[FileServer] Requesting the worker to start after configuration reload.")
                L2_LOG_DEBUG(Log,
                    "[file server] Requesting the worker to start after configuration reload.");
                g_backgroundThreads.StartFileServer();
            }
            return 0;
        default:
            return ::DefWindowProcW(window, WM_COMMAND, wParam, lParam);
        }
    }

    if (message == WM_SYSCOMMAND) {
        if (wParam == SC_MINIMIZE) {
            ::ShowWindow(window, SW_SHOWMINIMIZED);
            return 0;
        }
        return ::DefWindowProcW(window, WM_SYSCOMMAND, wParam, lParam);
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

ATOM MyRegisterClass(HINSTANCE instance) {
    // Current Cached.cpp 0x00459810.
    L2_GUARD_FUNCTION();
    WNDCLASSEXW windowClass;
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &WndProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = instance;
    windowClass.hIcon = ::LoadIconW(instance, MAKEINTRESOURCEW(0x6b));
    windowClass.hCursor = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<std::uintptr_t>(COLOR_WINDOW + 1));
    windowClass.lpszMenuName = MAKEINTRESOURCEW(0x6d);
    windowClass.lpszClassName = g_cachedWindowClass.data();
    windowClass.hIconSm = ::LoadIconW(instance, MAKEINTRESOURCEW(0x6c));
    return ::RegisterClassExW(&windowClass);
}

bool InitInstance(HINSTANCE instance, int showCommand) {
    // Current Cached.cpp 0x0045a46c.
    L2_GUARD_FUNCTION();
    server_up_time = runtime::crt::Time32Now();

    int positionX = 150;
    int positionY = 150;
    int width = 640;
    int height = 480;
    g_cachedInstance = instance;
    DWORD valueSize = sizeof(DWORD);
    DWORD valueType = REG_DWORD;
    HKEY key;
    ::RegCreateKeyW(HKEY_LOCAL_MACHINE, L"Software\\PROJECT_L2\\L2CACHED", &key);
    ::RegQueryValueExW(key, L"posx", nullptr, &valueType, reinterpret_cast<BYTE*>(&positionX), &valueSize);
    ::RegQueryValueExW(key, L"posy", nullptr, &valueType, reinterpret_cast<BYTE*>(&positionY), &valueSize);
    ::RegQueryValueExW(key, L"sizex", nullptr, &valueType, reinterpret_cast<BYTE*>(&width), &valueSize);
    ::RegQueryValueExW(key, L"sizey", nullptr, &valueType, reinterpret_cast<BYTE*>(&height), &valueSize);
    ::RegCloseKey(key);

    HWND const window = ::CreateWindowExW(
        0, g_cachedWindowClass.data(), g_cachedTitle.data(), 0x00cf0000, positionX, positionY, width, height, nullptr,
        nullptr, instance, nullptr
    );
    if (!window)
        return false;

    ::CheckMenuItem(::GetMenu(window), 0x94, g_bSaveQsData != 0 ? MF_CHECKED : MF_UNCHECKED);
    Log.Set(reinterpret_cast<HWND__*>(window));
    Log.Enable(true);
    ::ShowWindow(window, showCommand);
    ::UpdateWindow(window);
    CachedStatusWnd::Init().AttachToMainWnd(
        reinterpret_cast<HINSTANCE__*>(instance), reinterpret_cast<HWND__*>(window), showCommand
    );

    // g_readFile.ParseItemData()
    // g_readFile.ParseExpData()
    // g_readFile.ParseDBJobData()
    const bool itemDataLoaded = g_readFile.ParseItemData();
    const bool expDataLoaded = g_readFile.ParseExpData();
    const bool dbJobDataLoaded = g_readFile.ParseDBJobData();
    // Noise: three startup records duplicated one phase. Retain one local marker.
    // Log.Add(LOG_NORMAL, "Cached Server Start")
    // Log.Add(LOG_LOCAL, "Cached Server Start")
    // Log.Add(LOG_ERROR, "[NO_ERROR] Cached Server Start")
    // Log.Add(LOG_LOCAL, "component=startup event=initial_script_phase_finished")
    // Log.Add(LOG_LOCAL, "[startup] Initial script phase finished.")
    // L2_LOG(Log, LOG_LOCAL, "[startup] Initial script phase finished.")
    if (itemDataLoaded && expDataLoaded && dbJobDataLoaded) {
        L2_LOG(Log, LOG_LOCAL, "[startup] Initial script phase finished.");
    } else {
        L2_LOG_ERROR(Log,
            // "[startup] Initial script phase reported failures: item data=%s, experience data=%s, DB job data=%s. Startup continues. See the loader diagnostics.",
            // itemDataLoaded ? "success" : "failure",
            // expDataLoaded ? "success" : "failure",
            // dbJobDataLoaded ? "success" : "failure")
            "[startup] Initial script loaders returned: item data=%s, experience data=%s, DB job data=%s. Startup continues; see the loader diagnostics.",
            itemDataLoaded ? "true" : "false",
            expDataLoaded ? "true" : "false",
            dbJobDataLoaded ? "true" : "false");
    }
    if (g_WatchList.ParseWatchList()) {
        // Log.Add(LOG_NORMAL, L"watch list loaded")
        // Log.Add(LOG_NORMAL, "component=startup event=watch_list_load_finished result=true")
        // Log.Add(LOG_NORMAL, "[startup] Watch list loaded.")
        // L2_LOG_INFO(Log, "[startup] Watch list loaded.")
        L2_LOG_INFO(Log, "[startup] Watch-list loading finished; see any preceding read warnings or errors.");
    }

    // _beginthreadex's failure is not part of InitInstance's
    // return value in the current process.
    StartListenThreadRuntime();
    return true;
}

void MessageLoop_common(HACCEL accelerator) {
    // Current Cached.cpp 0x004545f0. MSG is not initialized.
    // the target also treats GetMessageW(-1) as a nonzero loop result.
    L2_GUARD_FUNCTION();
    MSG message;
    while (::GetMessageW(&message, nullptr, 0, 0) != 0) {
        if (!::TranslateAcceleratorW(message.hwnd, accelerator, &message)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }
}

void MessageLoop_mailing(HACCEL accelerator) {
    // Current 0x004546c0 is the SEH-mailing wrapper around the common body.
    MessageLoop_common(accelerator);
}

int CurrentShowCommand() {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    ::GetStartupInfoW(&startup);
    if ((startup.dwFlags & STARTF_USESHOWWINDOW) != 0) {
        return startup.wShowWindow;
    }
    return SW_SHOWDEFAULT;
}

int RunWindowsProcessUI() {
    HINSTANCE const instance = g_cachedInstance ? g_cachedInstance : ::GetModuleHandleW(nullptr);
    ::LoadStringW(instance, 0x69, g_cachedTitle.data(), g_cachedTitle.size());
    ::LoadStringW(instance, 0x6d, g_cachedWindowClass.data(),  g_cachedWindowClass.size());
    MyRegisterClass(instance);
    if (!InitInstance(instance, CurrentShowCommand())) {
        return 0;
    }

    TestPacketTableRegistration();
    HACCEL const accelerator = ::LoadAcceleratorsW(instance, MAKEINTRESOURCEW(0x6d));
    if (ExceptionMailing()) {
        MessageLoop_mailing(accelerator);
    } else {
        MessageLoop_common(accelerator);
    }
    return -1;
}

} // namespace

std::int64_t QsCheckTimeBox(HWND__* window, std::uint32_t message, std::uint64_t wParam, std::int64_t lParam) {
    // Current Cached.cpp 0x004547e0. The 20,480-wide-character local is the
    // exact source-shaped stack carrier also compiled by adjacent dialog
    // procedures, although WM_GETTEXT limits this input to 64.
    L2_GUARD_FUNCTION();

    if (message == WM_INITDIALOG) {
        return 1;
    }
    if (message != WM_COMMAND) {
        return 0;
    }

    const std::uint16_t command = static_cast<std::uint16_t>(wParam);
    if (command == IDOK) {
        std::array<wchar_t, 20480> input{};
        ::SendDlgItemMessageW(static_cast<HWND>(window), 0x3f4, WM_GETTEXT, 0x40, reinterpret_cast<LPARAM>(input.data()));
        const unsigned int minute = static_cast<unsigned int>(::_wtoi(input.data()));
        if (minute < 60U) {
            g_qsCheckManager.SetReportMinute(static_cast<int>(minute));
            g_qsCheckManager.EnableTimer();
        } else {
            g_qsCheckManager.DisableTimer();
        }
    } else if (command != IDCANCEL) {
        return 0;
    }

    ::EndDialog(static_cast<HWND>(window), static_cast<INT_PTR>(command));
    return 1;
}
#endif

void InitConfig() {
    L2_GUARD_FUNCTION();
    g_configuration = CacheDConfiguration{};
    g_configurationLoaded = false;
    g_nCountryCode = 0;
    g_bUseDBSecurity = false;
    g_nDBSecurityPenaltyLevel = 0;
    g_bSaveQsData = 0;
    g_bServerToCacheDPacketStatMonitor = false;
    g_bCacheDToServerPacketStatMonitor = false;
    g_bExceptionMakeMiniDump = false;
    g_nServerId = 1;
    g_nWorldId = 1;
    std::fill(std::begin(g_BinaryInfo.updateVersion), std::end(g_BinaryInfo.updateVersion), u'\0');

    runtime::Config source;
    if (!source.Load(runtime::ConfigFile("./l2cache.ini").string())) {
        // Log.Add(LOG_ERROR, "cannot read file \"l2cache.INI\"")
        // Log.Add(
        //     LOG_ERROR,
        //     "component=config event=load_failed path=\"%s\" action=abort_startup result=2",
        //     runtime::ConfigFile("./l2cache.ini").string().c_str())
        // Log.Add(
        //     LOG_ERROR,
        //     "[config] Could not load \"%s\". Startup aborted (exit code 2).",
        //     runtime::ConfigFile("./l2cache.ini").string().c_str())
        L2_LOG_ERROR(Log,
            "[config] Could not load \"%s\". Startup aborted (exit code 2).",
            runtime::ConfigFile("./l2cache.ini").string().c_str());
        return;
    }

    // Apply before resource and database startup. LogD setup comes later.
    // const int debugLogging = source.GetInt("Logging", "Debug", 0)
    // runtime::log::SetDebugLoggingEnabled(debugLogging == 1)
    // if (debugLogging != 0 && debugLogging != 1) {
    //     L2_LOG_WARN(Log,
    //         "[config] Logging.Debug must be 0 or 1. Received %d. Debug logging remains disabled.",
    //         debugLogging)
    // }
    const auto debugLogging = source.Get("Logging", "Debug", "0");
    runtime::log::SetDebugLoggingEnabled(debugLogging == "1");
    if (debugLogging != "0" && debugLogging != "1") {
        L2_LOG_WARN(Log,
            "[config] Logging.Debug must be exactly 0 or 1. Invalid value; debug logging remains disabled.");
    }
    if (!runtime::ConfigureResourceDirectories(source, source.Path())) return;

    CacheDConfiguration recovered;
    recovered.userInterface = runtime::ReadUserInterface(
        source.Get("Setting", "UserInterface", "console"));
    recovered.country = source.GetInt("Setting", "Country", 0);
    recovered.logDAddress = source.Get("LogD", "address", "127.0.0.1");
    recovered.logDPort = static_cast<std::uint16_t>(source.GetInt("LogD", "port", 3999));
    recovered.logFileDurationMinutes = source.GetInt("LogD", "FileDuration", 30);
    if (recovered.logFileDurationMinutes == 0) {
        // The target evaluates 60 % value before its value<1 fallback and
        // therefore faults for an explicit zero. Fail startup deterministically
        // at the platform boundary instead of entering production partially.
        // Log.Add(LOG_ERROR, "[cfg] LogD/FileDuration=0 is invalid in current Cached.exe")
        // Log.Add(
        //     LOG_ERROR,
        //     "component=config event=value_rejected section=LogD field=FileDuration value=0 reason=zero_duration action=abort_startup result=2")
        // Log.Add(
        //     LOG_ERROR,
        //     "[config] LogD FileDuration cannot be zero. Startup aborted (exit code 2).")
        L2_LOG_ERROR(Log,
            "[config] LogD FileDuration cannot be zero. Startup aborted (exit code 2).");
        return;
    }
    if (recovered.logFileDurationMinutes < 1 || 60 % recovered.logFileDurationMinutes != 0) {
        // Log.Add(LOG_WARN,
        //     "component=config event=value_defaulted section=LogD field=FileDuration configured_minutes=%d applied_minutes=30 reason=not_a_positive_divisor_of_60",
        //     recovered.logFileDurationMinutes)
        // Log.Add(LOG_WARN,
        //     "[config] LogD FileDuration is %d minutes. Using 30. The value must be a positive divisor of 60.",
        //     recovered.logFileDurationMinutes)
        L2_LOG_WARN(Log,
            "[config] LogD FileDuration is %d minutes; using 30. The value must be a positive divisor of 60.",
            recovered.logFileDurationMinutes);
        recovered.logFileDurationMinutes = 30;
    }

    recovered.serverType = ParseServerType(source.Get("Setting", "ServerType", "live"));
    if (recovered.serverType == CacheDServerType::Dev) {
        recovered.mailTo = source.Get("Setting", "MailTo", "");
        SystemMail::SetReceivers(recovered.mailTo.c_str());
    }
    const std::string explicitMailServer = source.Get("Setting", "MailServer", "");
    recovered.systemMailNetworkEnabled = ProfileBoolean(source, "Setting", "EnableSystemMail", false) &&
        !explicitMailServer.empty() && !recovered.mailTo.empty();
    recovered.serialKeyValidationEnabled = ProfileBoolean(source, "Portable", "EnableSerialKeyValidation", false);
    ServerAutoStarter::config.m_enabled = ProfileBoolean(source, "ServerAutoStarter", "Enabled", false);
    ServerAutoStarter::config.m_workingFolder.assign(
        Utf8ToUtf16(source.Get("ServerAutoStarter", "WorkingFolder", "")));
    ServerAutoStarter::config.m_filePath.assign(
        Utf8ToUtf16(source.Get("ServerAutoStarter", "FilePath", "")));

    recovered.defaultPriceLimit = UnsignedProfileInteger(source.GetInt("ItemAudit", "DefaultPriceLimit", 2000000));
    recovered.defaultPriceCheck = UnsignedProfileInteger(source.GetInt("ItemAudit", "DefaultPriceCheck", 1000000));
    recovered.priceChangeRatioLimit = source.GetInt("ItemAudit", "ChgRatioLimit", 30);
    recovered.adenaLimit = UnsignedProfileInteger(source.GetInt("ItemAudit", "AdenaLimit", 2000000));
    recovered.adenaCheck = source.GetInt("ItemAudit", "AdenaCheck", 1000000);
    recovered.adenaChangeRatioLimit = source.GetInt("ItemAudit", "AdenaChgRatioLimit", 30);
    recovered.adenaLogLimit = UnsignedProfileInteger(source.GetInt("ItemAudit", "AdenaLogLimit", 500000000));

    recovered.worldId = source.GetInt("World", "WorldId", 1);
    // Confirmed current shipping no-op: the string read remains intentional.
    source.Get("Setting", "IgnoreInvalidServerCacheVersion", "true");
    recovered.sqlExecLimitMilliseconds = source.GetInt("SQL", "SQLExecLimit", 5000);
    recovered.itemClearOnStart = source.GetInt("SQL", "ItemClearOnStart", 0) != 0;
    recovered.ghostDataClearOnStart = source.GetInt("SQL", "GhostDataClearOnStart", 0) != 0;
    recovered.saveQsData = source.GetInt("SQL", "SaveQsData", 0) != 0;
    recovered.loadedWorldDBTimeoutSeconds = source.GetInt("SQL", "WorldDBTimeOut", 60);
    recovered.waitTimeoutCheckMilliseconds =
        static_cast<std::uint32_t>(source.GetInt("SQL", "WaitTimeoutCheckTime", 0)) * 1000U;
    recovered.deleteAccount = source.GetInt("World", "DeleteAccount", 0) != 0;

    recovered.timeLimit = source.GetInt("setting", "timelimit", 0);
    recovered.writeCache = source.GetInt("setting", "writecache", 0) != 0;
    recovered.binaryUpdateVersion =
        source.Get("setting", "binary_update_version", "Please insert binary_update_version in setting section!");
    recovered.lateSavingItems = ParseLateSavingItems(source.Get("setting", "LateSavingItems", ""));
    recovered.lateSaving = source.GetInt("setting", "LateSaving", 0) != 0;
    recovered.lateSavingAmount = source.GetInt("setting", "LateSavingAmount", 100);
    recovered.serverStopperEnabled = ProfileBoolean(source, "setting", "ServerStopperEnabled", false);
    recovered.serverStopperKey = source.GetInt("setting", "ServerStopperKey", 0);
    recovered.adminAddresses = SplitExact(source.Get("setting", "AdminIP", ""), ',');
    // The current UTF-16 tokenizer at 0x004a13b4 likewise skips empty comma
    // fields and appends each remaining spelling without normalization.
    recovered.pledgeNameValueRate =
        static_cast<double>(static_cast<std::uint32_t>(source.GetInt("event", "pledgenamevaluerate", 100))) / 100.0;

    recovered.gipEnabled = ProfileBoolean(source, "GIP", "enabled", false);
    recovered.gipAddress = source.Get("GIP", "address", "127.0.0.1");
    recovered.gipPort = static_cast<std::uint16_t>(source.GetInt("GIP", "port", 3999));
    recovered.gipHeartbeatMilliseconds = static_cast<std::uint32_t>(source.GetInt("GIP", "heartbeat", 10)) * 1000U;

    recovered.qsCheckEnabled = ProfileBoolean(source, "QsCheck", "Enabled", false);
    if (recovered.qsCheckEnabled) {
        recovered.qsCheckResetTime = source.GetInt("QsCheck", "ResetTime", 55);
    }
    recovered.showTestCaseProgress = ProfileBoolean(source, "Setting", "ShowTestCaseProgress", false);
    recovered.directCommand = ProfileBoolean(source, "Setting", "direct_command", false);
    const std::string myOdbcSetting = source.Get("Setting", "MyOdbc", "false");
    if (!EqualsAsciiInsensitive(myOdbcSetting, "false")) {
        recovered.myOdbcSetting = myOdbcSetting;
    }
    recovered.waitingDateForDelete = source.GetInt("Setting", "WaitingDateForDelete", 7);
    recovered.waitingMinuteForDelete = source.GetInt("Setting", "WaitingMinuteForDelete", 60);
    recovered.useServerStartMail = ProfileBoolean(source, "Setting", "UseServerStartMail", false);
    recovered.moveCommission = ProfileBoolean(source, "Setting", "MoveCommission", false);
    recovered.blockPenaltyCount = source.GetInt("Setting", "BlockPenaltyCount", 0);
    recovered.blockPenaltyLevel = source.GetInt("Setting", "BlockPenaltyLevel", 0);
    recovered.postLimitLevel = source.GetInt("Setting", "PostLimitLevel", 0);
    recovered.trainingTimeInit = source.GetInt("Setting", "TrainingTimeInit", 23400);
    recovered.trainingTimeLimit = source.GetInt("Setting", "TrainingTimeLimit", 300);
    recovered.minidump = ProfileBoolean(source, "Setting", "minidump", true);
    recovered.minidumpType = source.GetInt("Setting", "minidump_type", 1);

    // Current InitConfig overwrites the just-loaded flag at 0x004a1f57.
    recovered.useServerStartMail = true;
    recovered.useDBSecurity = ProfileBoolean(source, "DBSecurity", "UseDBSecurity", false);
    recovered.dbSecurityPenaltyLevel = source.GetInt("DBSecurity", "PenaltyLevel", 0);
    recovered.dbSecurityKey = source.Get("DBSecurity", "Key", "123456789!");
    if (recovered.dbSecurityKey.size() > 48) {
        recovered.dbSecurityKey.resize(48);
    }
    recovered.myOdbcEnabled = ProfileBoolean(source, "MyOdbc", "enabled", false);
    recovered.myOdbcWorldDatabase = source.Get("MyOdbc", "world_db", "");

    recovered.activateCharacterAmount = source.GetInt("FreeToPlay", "ActivateCharacterAmount", 0);
    if (recovered.activateCharacterAmount == 0) {
        recovered.activateCharacterAmount = 7;
    }
    recovered.deactivateMinLevel = source.GetInt("FreeToPlay", "DeactivateMinLevel", 200);
    recovered.dailyPostCountForFreeUser = source.GetInt("FreeToPlay", "DailyPostCountForFreeUser", -1);
    recovered.dualBillingSystem = ProfileBoolean(source, "FreeToPlay", "DualBillingSystem", false);
    recovered.serverToCacheDPacketStatMonitor = ProfileBoolean(source, "QsCheck", "ServerToCacheDPacketStatMonitor", false);
    recovered.cacheDToServerPacketStatMonitor = ProfileBoolean(source, "QsCheck", "CacheDToServerPacketStatMonitor", false);

    g_configuration = std::move(recovered);
    g_nCountryCode = g_configuration.country;
    g_bUseDBSecurity = g_configuration.useDBSecurity;
    g_nDBSecurityPenaltyLevel = g_configuration.dbSecurityPenaltyLevel;
    g_bSaveQsData = g_configuration.saveQsData ? 1U : 0U;
    g_bServerToCacheDPacketStatMonitor = g_configuration.serverToCacheDPacketStatMonitor;
    g_bCacheDToServerPacketStatMonitor = g_configuration.cacheDToServerPacketStatMonitor;
    if (g_configuration.qsCheckEnabled) {
        g_qsCheckManager.SetReportMinute(g_configuration.qsCheckResetTime);
        g_qsCheckManager.EnableTimer();
    }
    g_bExceptionMakeMiniDump = g_configuration.minidump;
    g_nServerId = g_configuration.worldId;
    g_nWorldId = g_configuration.worldId;
    const std::u16string binaryUpdateVersion = Utf8ToUtf16(g_configuration.binaryUpdateVersion);
    std::copy_n(
        binaryUpdateVersion.begin(), std::min(binaryUpdateVersion.size(), std::size(g_BinaryInfo.updateVersion) - 1),
        g_BinaryInfo.updateVersion
    );
    g_configurationLoaded = true;
}

void InitEntryConfiguration() {
    runtime::Config secure;
    secure.Load(runtime::ConfigFile("./secure.ini").string(), false);
    g_entryConfiguration.scriptSecureEnabled = ProfileBoolean(secure, "Setting", "ScriptSecureEnabled", true);

    runtime::Config mainConfiguration;
    mainConfiguration.Load(runtime::ConfigFile("./l2cache.ini").string(), false);
    g_entryConfiguration.mailServer = mainConfiguration.Get("Setting", "MailServer", "mail.ncsoft.co.kr");
    g_entryConfiguration.developmentMailServer = mainConfiguration.Get("Setting", "DevMailServer", "dev-mail");
    mailServer = AllocateMailServerSetting(g_entryConfiguration.mailServer);
    devMailServer = AllocateMailServerSetting(g_entryConfiguration.developmentMailServer);
    runtime::ConfigureSystemMailNetwork(g_configuration.systemMailNetworkEnabled);
}

bool IsCacheDConfigurationLoaded() { return g_configurationLoaded; }

CacheDConfiguration& GetCacheDConfiguration() { return g_configuration; }

const CacheDEntryConfiguration& GetCacheDEntryConfiguration() { return g_entryConfiguration; }

int CacheDMain(int argc, char** argv) {
    runtime::logging::SetThreadName("main");
    // Current Cached.exe WinMain 0x0045a794 calls the exception-runtime
    // initializer at 0x004cca1c before even inspecting the "-v" substring.
    // The Shared implementation is the same six-call sequence recovered for
    // this process: CLogCritical, LinError.txt, start time, filters and the
    // invalid-parameter handler.
    InitializeExceptionRuntime();

    // Current 0x0045a794 uses strstr(commandLine, "-v"), not an exact option
    // parser. Preserve that observable behavior for the compatibility switch.
    if (HasCommandSubstring(argc, argv, "-v")) {
        std::printf("[db:%d] [admin:%d]\n", 56, 373);
        return -1;
    }

    const bool checkConfigurationOnly = HasCommandSubstring(argc, argv, "--check-config");
    CacheDProcessInstanceLock instanceLock;
    if (!checkConfigurationOnly && !instanceLock.TryAcquire()) {
        // Log.Add(LOG_ERROR, "L2cached.EXE is already running.")
        // Log.Add(
        //     LOG_ERROR,
        //     "component=startup event=instance_lock_failed reason=busy_or_unavailable check=other_cached_instance_and_working_directory_permissions action=exit result=-1")
        // Log.Add(
        //     LOG_ERROR,
        //     "[startup] Could not acquire the instance lock. Check for another CacheD process and working-directory permissions. Exiting with code -1.")
        L2_LOG_ERROR(Log,
            "[startup] Could not acquire the instance lock. Check for another CacheD process and working-directory permissions. Exiting with code -1.");
        return -1;
    }

    InitConfig();
    if (!IsCacheDConfigurationLoaded())
        return 2;

    // Current WinMain 0x0045a794 performs these three operations immediately
    // after InitConfig: publish logical MAIN=0x14, cache the current capped
    // I/O-worker count, and allocate the main thread's 0x80 default buffers.
    CThreadManager::SetThisThreadAsMainThread();
    CThreadManager::SetThreadCount(GetIOThreadCount());
    InitializeCurrentIOBufferPool(0x80);
    InitEntryConfiguration();

    if (checkConfigurationOnly) {
        const CacheDConfiguration& config = GetCacheDConfiguration();
        CacheDDatabasePlan databasePlan;
        std::string databaseError;
        if (!LoadCacheDDatabasePlan(runtime::ConfigFile("./l2cache.ini").string(), config.worldId, databasePlan, databaseError, false)) {
            // Log.Add(LOG_ERROR, "[cfg] DBList: %s", databaseError.c_str())
            // Log.Add(
            //     LOG_ERROR,
            //     "component=config event=check_failed stage=database_plan world_id=%d detail_preview=\"%.512s\" action=exit result=2",
            //     config.worldId, databaseError.c_str())
            // Log.Add(
            //     LOG_ERROR,
            //     "[config] Database plan check failed for world %d: %.512s. Exiting with code 2.",
            //     config.worldId, databaseError.c_str())
            // Log.Add(
            //     LOG_ERROR,
            //     "[config] Database plan check failed for world %d (exit code 2): %.512s",
            //     config.worldId, databaseError.c_str())
            L2_LOG_ERROR(Log,
                "[config] Database plan check failed for world %d (exit code 2): %.512s",
                config.worldId, databaseError.c_str());
            return 2;
        }

        const bool hasPostgreSQLDeployment = PostgreSQLDeploymentExists(runtime::ConfigFile("./etc/cached-postgresql.ini").string());
        if (hasPostgreSQLDeployment) {
            database::PostgreSQLRuntimeConfig postgres;
            if (!BuildPostgreSQLRuntimeConfiguration(
                    runtime::ConfigFile("./etc/cached-postgresql.ini").string(), config, databasePlan, postgres, databaseError, false
                )) {
                // Log.Add(LOG_ERROR, "[cfg] PostgreSQL: %s", databaseError.c_str())
                // Log.Add(
                //     LOG_ERROR,
                //     "component=config event=check_failed stage=postgresql_configuration world_id=%d detail_preview=\"%.512s\" action=exit result=2",
                //     config.worldId, databaseError.c_str())
                // Log.Add(
                //     LOG_ERROR,
                //     "[config] PostgreSQL configuration check failed for world %d: %.512s. Exiting with code 2.",
                //     config.worldId, databaseError.c_str())
                // Log.Add(
                //     LOG_ERROR,
                //     "[config] PostgreSQL configuration check failed for world %d (exit code 2): %.512s",
                //     config.worldId, databaseError.c_str())
                L2_LOG_ERROR(Log,
                    "[config] PostgreSQL configuration check failed for world %d (exit code 2): %.512s",
                    config.worldId, databaseError.c_str());
                return 2;
            }
        }
        // Log.Add(
        //     LOG_NORMAL,
        //     "[cfg] CacheD current contract: world=%d LogD=%s:%u "
        //     "listeners=0.0.0.0:{12008,12006,12012} DBList=%s "
        //     "PostgreSQL=%s serial-key-validation=%s",
        //     config.worldId, config.logDAddress.c_str(), static_cast<unsigned>(config.logDPort),
        //     DescribeCacheDDatabasePlan(databasePlan).c_str(), hasPostgreSQLDeployment ? "configured" : "not-configured",
        //     config.serialKeyValidationEnabled ? "enabled" : "disabled"
        // )
        // Log.Add(
        //     LOG_NORMAL,
        //     "component=config event=check_finished world_id=%d logd_host_preview=\"%.253s\" logd_port=%u npc_listen=0.0.0.0:12008 world_listen=0.0.0.0:12006 admin_listen=0.0.0.0:12012 database_plan=\"%.512s\" postgres_deployment=%s serial_key_validation=%s network_connections=unchecked",
        //     config.worldId, config.logDAddress.c_str(), static_cast<unsigned int>(config.logDPort),
        //     DescribeCacheDDatabasePlan(databasePlan).c_str(), hasPostgreSQLDeployment ? "present" : "absent",
        //     config.serialKeyValidationEnabled ? "enabled" : "disabled")
        // Log.Add(LOG_NORMAL, "[config] Configuration is valid for world %d. Network connections have not been checked.", config.worldId)
        L2_LOG_INFO(Log, "[config] Configuration is valid for world %d. Network connections have not been checked.", config.worldId);
        // Log.Add(LOG_NORMAL, "[config] LogD: \"%.253s\":%u. Listeners: NPC 0.0.0.0:12008, world 0.0.0.0:12006, admin 0.0.0.0:12012.",
        //     config.logDAddress.c_str(), static_cast<unsigned int>(config.logDPort))
        L2_LOG_INFO(Log, "[config] LogD: \"%.253s\":%u. Listeners: NPC 0.0.0.0:12008, world 0.0.0.0:12006, admin 0.0.0.0:12012.",
            config.logDAddress.c_str(), static_cast<unsigned int>(config.logDPort));
        // Log.Add(LOG_NORMAL, "[database] Plan: \"%.512s\". PostgreSQL deployment: %s.",
        //     DescribeCacheDDatabasePlan(databasePlan).c_str(), hasPostgreSQLDeployment ? "present" : "absent")
        L2_LOG_INFO(Log, "[database] Plan: \"%.512s\". PostgreSQL deployment: %s.",
            DescribeCacheDDatabasePlan(databasePlan).c_str(), hasPostgreSQLDeployment ? "present" : "absent");
        // Log.Add(LOG_NORMAL, "[config] Serial-key validation: %s.", config.serialKeyValidationEnabled ? "enabled" : "disabled")
        L2_LOG_INFO(Log, "[config] Serial-key validation: %s.", config.serialKeyValidationEnabled ? "enabled" : "disabled");
        return 0;
    }

    portable::WaitThreadRuntimeServices waitServices;
    waitServices.stopRequested = [] { return g_processLifecycle.StopRequested(); };
    waitServices.waitForStop = [](std::chrono::milliseconds timeout) { return g_processLifecycle.WaitForStopFor(timeout); };
    portable::WaitThreadRuntimeBinding waitBinding(std::move(waitServices));

#if defined(_WIN32)
    if (g_configuration.userInterface == runtime::UserInterface::Gui) {
        ::FreeConsole();
        const int result = RunWindowsProcessUI();
        g_processLifecycle.RequestStop();
        JoinListenThreadRuntime();
        return result;
    }
#endif
    // Current InitInstance 0x0045a46c publishes the process start time and
    // constructs/attaches CachedStatusWnd before the three CReadFile loads.
    // The portable host has no parent HWND, but it keeps the same status
    // owner and diagnostic state.
    server_up_time = runtime::crt::Time32Now();
    CachedStatusWnd::Init();

    // Current InitInstance 0x0045a46c calls these three read-only loaders
    // in this exact order after configuration/UI setup and before launching
    // the listen thread. Their bool results are discarded.
    // Logging observes those results below. They still do not gate startup.
    // g_readFile.ParseItemData()
    // g_readFile.ParseExpData()
    // g_readFile.ParseDBJobData()
    const bool itemDataLoaded = g_readFile.ParseItemData();
    const bool expDataLoaded = g_readFile.ParseExpData();
    const bool dbJobDataLoaded = g_readFile.ParseDBJobData();
    // Noise: three startup records duplicated one phase. Retain one local marker.
    // Log.Add(LOG_NORMAL, "Cached Server Start")
    // Log.Add(LOG_LOCAL, "Cached Server Start")
    // Log.Add(LOG_ERROR, "[NO_ERROR] Cached Server Start")
    // Log.Add(LOG_LOCAL, "component=startup event=initial_script_phase_finished")
    // Log.Add(LOG_LOCAL, "[startup] Initial script phase finished.")
    // L2_LOG(Log, LOG_LOCAL, "[startup] Initial script phase finished.")
    if (itemDataLoaded && expDataLoaded && dbJobDataLoaded) {
        L2_LOG(Log, LOG_LOCAL, "[startup] Initial script phase finished.");
    } else {
        L2_LOG_ERROR(Log,
            "[startup] Initial script loaders returned: item data=%s, experience data=%s, DB job data=%s. Startup continues; see the loader diagnostics.",
            itemDataLoaded ? "true" : "false",
            expDataLoaded ? "true" : "false",
            dbJobDataLoaded ? "true" : "false");
    }
    if (g_WatchList.ParseWatchList()) {
        // Log.Add(LOG_NORMAL, L"watch list loaded")
        // Log.Add(LOG_NORMAL, "component=startup event=watch_list_load_finished result=true")
        // Log.Add(LOG_NORMAL, "[startup] Watch list loaded.")
        // L2_LOG_INFO(Log, "[startup] Watch list loaded.")
        L2_LOG_INFO(Log, "[startup] Watch-list loading finished; see any preceding read warnings or errors.");
    }

    // Current InitInstance creates the LISTEN thread here and returns to the
    // GUI message loop. The portable console host owns the same asynchronous
    // boundary and blocks its main thread until SIGINT/SIGTERM or a startup
    // failure ends LISTEN.
    g_listenThreadResult.store(0, std::memory_order_release);
    g_processLifecycle.InstallSignalHandlers();

    if (!StartListenThreadRuntime()) {
        // Log.Add(LOG_ERROR, "cannot create CacheD listen thread")
        // Log.Add(LOG_ERROR, "component=startup event=listener_thread_failed, startup aborted")
        // Log.Add(
        //     LOG_ERROR,
        //     "component=startup event=listener_thread_failed action=abort_startup result=7")
        // Log.Add(
        //     LOG_ERROR,
        //     "[startup] Could not create the listener thread. Startup aborted (exit code 7).")
        L2_LOG_ERROR(Log,
            "[startup] Could not create the listener thread. Startup aborted (exit code 7).");
        return 7;
    }
    runtime::ConsoleInput console;
    while (!g_listenThreadDone.load(std::memory_order_acquire) && !g_processLifecycle.StopRequested()) {
        if (g_consoleReady.load(std::memory_order_acquire)) {
            console.Poll(
                [](std::string_view line) {
                    const std::lock_guard<std::mutex> guard(g_consoleCommandMutex);
                    if (g_consoleReady.load(std::memory_order_acquire) && !g_processLifecycle.StopRequested()) {
                        cached::console::HandleCommand(line, [] { g_processLifecycle.RequestStop(); });
                    }
                },
                [](std::string_view error) {
                    L2_LOG_WARN(Log, "[console] %.*s", static_cast<int>(error.size()), error.data());
                });
        }
        g_processLifecycle.WaitForStopFor(std::chrono::milliseconds(100));
    }
    JoinListenThreadRuntime();
    return g_listenThreadResult.load(std::memory_order_acquire);
}
