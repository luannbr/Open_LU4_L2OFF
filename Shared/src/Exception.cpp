#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "Shared/src/Exception.h"
#include "Shared/src/Runtime/ConfigDirectory.h"
#include "Shared/src/Runtime/DirectLog.h"
#include "Shared/src/Runtime/OperatorLog.h"
#include "Shared/src/Runtime/SourceFile.h"

#include "L2Core/src/inc/FileUtils.h"
#include "L2Core/src/inc/Guard.h"
#include "L2Core/src/inc/GuardProfile.h"
#include "L2Core/src/inc/MiniDump.h"
#include "Shared/src/Runtime/ThreadQuiescence.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WindowsAnsi.h"
#include "Runtime.h"
#include "Shared/src/Config.h"
#include "Shared/src/IOC.h"
#include "Shared/src/LockStatusStack.h"
#include "Shared/src/SystemMail.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "TextRuntime.h"

#if defined(L2_BUILD_L2SERVER)
#include "../../L2Server/src/Default.h"
#include "../../L2Server/src/External/GameGuardServerLibCompat.h"
#include "../../L2Server/src/External/NCGuardServerLibCompat.h"
#include "../../L2Server/src/L2Time.h"
#include "../../L2Server/src/ThreadLocalWorldMap.h"
#endif

#if defined(L2_BUILD_CACHED)
#include "CAccount.h"
#include "Shared/src/DBConn.h"
#include "UserDb.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <DbgHelp.h>
#include <Windows.h>
#elif defined(__APPLE__)
#include <execinfo.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <sys/sysctl.h>
#include <sys/ucontext.h>
#include <unistd.h>
#elif defined(__unix__)
#include <execinfo.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

namespace {

// Operator diagnostics share the existing per-thread report sequence. The
// optional local copy changes destinations, not the recorded frames or mail policy.
void WriteRecordedGuardStack(int threadId, LogType logType, bool sendMail, bool copyErrorLocally) {
    if (threadId < ThreadID::MAX) {
        static thread_local int totalCall = 0;
        ++totalCall;

        int order = 0;
        const int depth = GuardHelper::getCallStackDepth(threadId);
        if (copyErrorLocally) {
            // runtime::log::WriteErrorWithLocalCopy(Log,
            //     "[call stack] Report %d for worker %d: %d frames.",
            //     totalCall, threadId, depth)
            L2_LOG_ERROR_WITH_LOCAL_COPY(
                Log, "[call stack] Report %d for worker %d: %d frames.", totalCall, threadId, depth
            );
        } else {
            // Log.Add(logType,
            //     "[call stack] Report %d for worker %d: %d frames.",
            //     totalCall, threadId, depth)
            L2_LOG(Log, logType, "[call stack] Report %d for worker %d: %d frames.", totalCall, threadId, depth);
        }
        for (int index = 0; index < depth; ++index) {
            ++order;
            const wchar_t* const frameName = GuardHelper::getCallStack(threadId, index);
            const std::string frame = WideToUtf8(frameName ? frameName : L"unknown frame");
            if (copyErrorLocally) {
                // runtime::log::WriteErrorWithLocalCopy(Log,
                //     "[call stack] Report %d, worker %d, frame %d/%d: %s",
                //     totalCall, threadId, order, depth, frame.c_str())
                L2_LOG_ERROR_WITH_LOCAL_COPY(
                    Log, "[call stack] Report %d, worker %d, frame %d/%d: %s", totalCall, threadId, order, depth,
                    frame.c_str()
                );
            } else {
                // Log.Add(logType,
                //     "[call stack] Report %d, worker %d, frame %d/%d: %s",
                //     totalCall, threadId, order, depth, frame.c_str())
                L2_LOG(
                    Log, logType, "[call stack] Report %d, worker %d, frame %d/%d: %s", totalCall, threadId, order, depth,
                    frame.c_str()
                );
            }
        }
        if (sendMail) {
            SystemMail::sendCriticalCallstack(mailServer);
        }
    }
}

} // namespace

void runtime::log::ReportCallStackWithLocalCopy() {
    WriteRecordedGuardStack(CThreadManager::GetThreadID(), LOG_ERROR, false, true);
}

void DontMakeMiniDump() {
    // Current L2Server.exe 0x00726ae0, size 12. Preserve both stores and
    // their original Shared -> L2Core owner edge.
    g_bExceptionMakeMiniDump = false;
    EXCEPTION::CMiniDump::DontMakeDumpFile();
}

// The current L2Server Exception.cpp creation is recovered leaf by
// leaf in the product-specific section below. Do not compile the older
// L2NPC/CacheD exception bodies into that product merely because all three
// builds share this source path.
#if !defined(L2_BUILD_L2SERVER)
#if defined(L2_BUILD_CACHED)
constexpr std::size_t kProcessThreadCapacity = ThreadID::PORTABLE_CAPACITY;
#else
constexpr std::size_t kProcessThreadCapacity = ThreadID::MAX;
#endif

std::uint32_t g_dwGuardStartTick[kProcessThreadCapacity]{};
int g_nSkillActorDBID[kProcessThreadCapacity]{};
int g_nSkillID[kProcessThreadCapacity]{};
int g_nSkillTargetCount[kProcessThreadCapacity]{};
int g_nLeaveWorldPostCount[kProcessThreadCapacity]{};
int g_nLeaveWorldPostDBID[kProcessThreadCapacity][32]{};
#if defined(L2_BUILD_CACHED)
// Original identifier is not kept by the available PDB. Current
// Cached.exe fixes the owner and extent: 22 dwords at 0x02a2ea60, written by
// RequestLoadDroidNPCAppearance and read by PrintCallStack at 0x004c99e4.
std::int32_t g_nDroidNPCCharMapLoop[kProcessThreadCapacity]{};
#endif
std::int32_t server_up_time = 0;
#endif

ServerContext g_ServerContext;

// Exact Exception.obj symbol ?g_CrashReasonString@@3PAPEB_WA. Current
// L2Server.exe 0x015d2e90 is a writable 75-pointer table. Every target
// pointer/string was verified against the current image.
const wchar_t* g_CrashReasonString[CrashInfo::MAX_CRASH_REASON]{
    L"FORCE_CRASH",
    L"LOW_MEMORY",
    L"LOCK_MISMATCH",
    L"LOW_IO_BUFFER",
    L"DB_SOCKET_IS_CLOSED",
    L"AUTH_SOCKET_IS_CLOSED",
    L"TRYING_TO_UNLOCK_THE_UNLOCKED_LOCK",
    L"QUEUE_IS_EMPTY",
    L"NEVER_CALL_OPERATOR_DELETE",
    L"QUEUE_IS_FULL",
    L"THREADPIPE_OVERFLOW",
    L"INVALID_THREAD_NO",
    L"INVALID_CLASS",
    L"INVALID_INDEX",
    L"BY_QUIT_MENU",
    L"BY_SHUTDOWN_MENU",
    L"BY_CRASH_REPORT",
    L"BY_EXIT_MENU",
    L"BY_DESTROY_EVENT",
    L"BY_CRASH_COMMAND",
    L"BY_DBLIST_ERROR",
    L"BY_DEADLOCK_CHECKER",
    L"BY_SERVER_STOPPER_REQUEST",
    L"BY_SHUTDOWN_MESSAGE",
    L"INVALID_SQL_POOL",
    L"NETWORK_DISCONNECT_FROM_DB_TO_CACHED",
    L"THROW_EXCEPTION",
    L"LOCK_STACK_IS_NOT_EMPTY",
    L"CLOSE_IO_THREAD",
    L"IOCP_CRASH_MESSAGE",
    L"PARSING_ERROR_EXPDATA_FILE",
    L"METERED_SECTION_HANDLE_IS_NULL",
    L"PROCEEDED_OBJECT_IS_OVERFLOW",
    L"SHARED_FACTORY_IS_EMPTY",
    L"SHARED_FACTORY_INDEX_IS_INVALID",
    L"SHARED_FACTORY_FREE_INDEX_IS_INVALID",
    L"COBJECT_IS_NULL",
    L"LOAD_FAILURE_FOR_L2SERVER_INI",
    L"CACHE_VERSION_FAILED",
    L"CACHE_VERSION_IS_DIFFERENT",
    L"CACHEDSCRIPT_IS_NOT_UPDATED",
    L"GEOLOADING_IS_FAILED",
    L"INSPECTOR_DATA_IS_INVALID",
    L"NEW_INSPECTOR_DATA_IS_INVALID",
    L"CONTAINER_INDEX_OVERFLOWED",
    L"UNITTEST_TOO_MANY_USERS",
    L"GAMEGUARD_LOADING_IS_FAILED",
    L"SHARED_PLATFORM_INFO_IS_INVALID",
    L"ITEM_CREATION_IS_FAILED",
    L"Z_DIVIDED_NUM_OVERFLOWED",
    L"Z_DIVIDED_NUM_IS_INVALID",
    L"NPC_M_DATA_IS_NULL",
    L"NPC_AI_LOADING_FAILED",
    L"SERVER_TYPE_MISMATCH",
    L"SERVER_PACKET_SIZE_IS_INVALID",
    L"SERVER_UNKNOWN_PROTOCOL",
    L"CHECK_SIZE_INT",
    L"SERVER_SOCKET_CLOSE",
    L"SERVER_BOOT_TIME_IS_OVER_49_DAYS",
    L"LEX_YACC_FAILED",
    L"MISMATCH_NPC_PROTOCOL",
    L"MISMATCH_SHARED_FACTORY_VERSION",
    L"PARSING_ERROR_STATUEDATA_FILE",
    L"MISMATCH_NPC_HEADER_VERSION",
    L"MISMATCH_SHARED_DATA_SIZE",
    L"PARSING_ERROR_HAIREXDATA_FILE",
    L"PARSING_ERROR_FACEEXDATA_FILE",
    L"PARSING_ERROR_COLOREXDATA_FILE",
    L"PARSING_ERROR_DBJOB_FILE",
    L"NP_SOCKET_ERROR",
    L"INVALID_PARAMETER_ERROR",
    L"PARSING_ERROR_EVENTDATA_FILE",
    L"INVALID_SCRIPT_INTEGRITY",
    L"CACHED_DB_VERSION_NOT_MATCHED",
    L"SQL_FUNCTION_SEQUENCE_ERROR",
};

#if defined(L2_BUILD_L2SERVER)
// Current L2Server.exe Exception.obj time32 storage 0x04d26fe8. Process
// startup at 0x0080a97f passes the storage directly to the target _time32
// body. Report helpers consume the same signed 32-bit carrier.
std::int32_t server_up_time = 0;
// Current L2Server.exe Exception.obj daylight carrier 0x04d26fec. Process
// startup and BuilderCmd_DaylightSaving both pass it to _get_daylight.
std::int32_t g_nDaylightSaving = 0;
// Current L2Server.exe Exception.obj byte 0x015d2e5b. The startup setter
// writes it even though this product's folded ExceptionMailing() terminal
// returns true unconditionally.
bool g_bExceptionMailing = false;
// Current Exception.obj storage 0x04d26fe7 is BSS-zeroed. CConfig publishes
// the normalized Setting/minidump value at 0x005c9d45.
bool g_bExceptionMakeMiniDump = false;
// Current initialized Exception.obj dword 0x015d30f0 is one. CConfig
// replaces it with Setting/minidump_type at 0x005c9d69.
int g_nExceptionMiniDumpType = 1;
// Current Exception.obj storage 0x04d26f18, written by
// L2Exception::CreateExceptionSystem at 0x00729ae0.
std::int32_t CExceptionInit::g_timeStart = 0;
LastAction g_LastAction[16];
char16_t g_sNpcServerConnectionLog[20000]{};
#endif

#if !defined(L2_BUILD_L2SERVER)
LastAction g_LastAction[8];
CrashInfo g_CrashInfo;
bool g_bInvalidParameter = false;
bool g_IsBySystemManager = false;
bool bForceCrash = false;
// Current image byte 0x006affc0 is initialized to one. There are no target
// writes to it, so this is not a live socket-state flag in this build.
bool bCanConnectCacheServer = true;
const wchar_t* g_sInvalidParamExpr = nullptr;
wchar_t g_CrashFunctionName[1024]{};
wchar_t g_CrashFileName[1024]{};
wchar_t g_CrashLineNo[16]{};
wchar_t g_szCrashLogPath[260]{};
// Semantic name for current image storage 0x019200d0. The adjacent PDB
// predates this second path buffer. SystemMail's CrashMailMessage reads it
// directly as the LinError.txt body path.
wchar_t g_szLinErrorPath[0x104]{};
// Current image dword 0x006b0230 starts at one. CreateMiniDumpFile in this
// build still passes MiniDumpNormal unconditionally and does not consume the
// value, but the process-visible configuration state keeps that initializer.
int g_nExceptionMiniDumpType = 1;
bool g_bExceptionMailing = false;
int ExceptionTerminating = 0;
CIOCriticalSection g_ThreadSuspendLock;
// Exception.obj globals. The current image reads mailServer at 0x018b1810.
// the adjacent slot and donor symbol identify the dormant devMailServer.
char* devMailServer = nullptr;
char* mailServer = nullptr;
#endif

namespace {

// Copy narrow compiler text such as __FUNCSIG__ into a wide report buffer.
void FormatCrashText(wchar_t* target, std::size_t capacity, const wchar_t* prefix, const char* text) {
    std::size_t index = 0;
    for (; prefix[index] != L'\0' && index + 1 < capacity; ++index) {
        target[index] = prefix[index];
    }
    for (const char* current = text; current != nullptr && *current != '\0' && index + 1 < capacity; ++current) {
        target[index++] = static_cast<unsigned char>(*current);
    }
    target[index] = L'\0';
}

} // namespace

void RecordCrashLocation(const std::source_location location) {
    FormatCrashText(g_CrashFunctionName, std::size(g_CrashFunctionName), L"Function : ", location.function_name());
    FormatCrashText(
        g_CrashFileName, std::size(g_CrashFileName), L"File : ", ::source_location::FileName(location.file_name())
    );
    std::swprintf(g_CrashLineNo, std::size(g_CrashLineNo), L", Line : %d ", static_cast<int>(location.line()));
}

void ForceCrash(const CrashInfo::ReasonType reason, const std::source_location location) {
    RecordCrashLocation(location);
    g_CrashInfo.SetReason(reason);
    bForceCrash = true;
    CrashImpl();
}

void ForceCrash(
    const CrashInfo::ReasonType reason, const wchar_t* const functionName, const wchar_t* const fileName, const int line
) {
    std::swprintf(g_CrashFunctionName, std::size(g_CrashFunctionName), L"Function : %ls", functionName);
    std::swprintf(g_CrashFileName, std::size(g_CrashFileName), L"File : %ls", fileName);
    std::swprintf(g_CrashLineNo, std::size(g_CrashLineNo), L", Line : %d ", line);
    g_CrashInfo.SetReason(reason);
    bForceCrash = true;
    CrashImpl();
}

ServerContext::ServerContext() noexcept { Init(); }

void ServerContext::Init() noexcept {
    const auto clear = [](std::atomic<std::int32_t>& value) { value.store(0, std::memory_order_relaxed); };
    clear(User);
    clear(Npc);
    clear(Item);
    clear(UserSocket);
    clear(InZone);
    clear(Trade);
    clear(Auction);
    clear(Bridge);
    clear(AirShip);
    clear(NotReleasedUser);
    clear(NotReleasedNpc);
    clear(NotReleasedItem);
    clear(NotReleasedUserSocket);
    clear(NotReleasedDoor);
    clear(NotReleasedInZone);
    clear(NotReleasedTrade);
    clear(NotReleasedAuction);
    clear(NotReleasedBridge);
    clear(NotReleasedAirShip);
    clear(ResidenceDecoVisible);
    clear(AirShipSteering);
    clear(AirShipPart);
    clear(BattleCamp);
    clear(Chair);
    clear(ControlTower);
    clear(Door);
    clear(EventController);
    clear(EventMatch);
    clear(FortressFlagpole);
    clear(ObserverRemains);
    clear(SignBoard);
    clear(Statue);
    clear(TeamBattleFlag);
    clear(HolyThing);
    clear(OwnThing);
    // Current Exception.obj initializer stores these final two relaxed lanes
    // in this non-field-order sequence.
    clear(MaxSerializerTime);
    clear(EtcStaticObject);
}

LastAction::LastAction() noexcept : m_SkillID(0), m_AlteredSkillID(0), m_ItemClassID(0), m_PlayerClassID(0), m_Name{} {}

#if !defined(L2_BUILD_L2SERVER)
#if defined(L2_BUILD_L2NPC)
extern int g_NPCClassID[12][18];
#endif

ScriptContext g_ScriptContext[8][1000]{};
wchar_t* g_ScriptNPC[8]{};
RunScriptCallStatus g_RunScriptCallStatus[8]{};
HandlerInfo g_lastcalledEventHandler[8][1000];


namespace {

const wchar_t* const LIN_ERROR_TXT = L"LinError.txt";

// The target's callers allocate 0x104 UTF-16 elements.
constexpr std::size_t kExceptionPathCapacity = 0x104;
constexpr std::uint32_t kTargetModulePathQueryCount = 0x208;

void PrintCallStack(void* LogFile, int nThreadID, const wchar_t* pszThreadName);
void PrintGuardInfo(void* LogFile);

// Exact file-local PDB symbol. Current L2NPC byte 0x006affc2 and CacheD byte
// 0x007e2282 are initialized to one and cleared by violation before the
// intentional fault.
bool xfirst = true;

#if defined(_WIN32)
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
#else
std::atomic_flag g_portableCrashReportActive = ATOMIC_FLAG_INIT;
#endif

void CopyExceptionPath(wchar_t destination[], std::wstring_view source, std::size_t capacity = kExceptionPathCapacity) {
    if (!destination || capacity == 0) {
        return;
    }

    const std::size_t length = source.size() < capacity - 1 ? source.size() : capacity - 1;
    if (length != 0) {
        std::wmemcpy(destination, source.data(), length);
    }
    destination[length] = L'\0';
}

#if !defined(_WIN32)
std::string ExecutablePath() {
#if defined(__APPLE__)
    std::uint32_t capacity = 0;
    _NSGetExecutablePath(nullptr, &capacity);
    if (capacity != 0) {
        std::string path(capacity, '\0');
        if (_NSGetExecutablePath(path.data(), &capacity) == 0) {
            path.resize(std::char_traits<char>::length(path.c_str()));
            return path;
        }
    }
#elif defined(__linux__)
    std::array<char, PATH_MAX + 1> path{};
    const ssize_t length = ::readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (length > 0) {
        return std::string(path.data(), static_cast<std::size_t>(length));
    }
#endif

    return (std::filesystem::current_path() / "L2NPC").string();
}

void* InvalidFileHandle() { return reinterpret_cast<void*>(static_cast<std::intptr_t>(-1)); }
#endif

wchar_t* TimeString(std::int32_t value) {
    // _wctime32, used by the target, returns one shared 26-character buffer.
    // Keep that process-wide overwrite behavior on non-MSVC hosts as well.
    static std::array<wchar_t, 32> buffer{};
    std::tm* const local = runtime::crt::LocalTime32(value);
    if (!local) {
        return nullptr;
    }
    const std::size_t length = std::wcsftime(buffer.data(), buffer.size(), L"%a %b %d %H:%M:%S %Y\n", local);
    if (length == 0) {
        return nullptr;
    }
    return buffer.data();
}

// POSIX has no SYSTEM_INFO or MEMORYSTATUSEX. This private carrier represents
// only those two native records at the portability boundary. CPU structure is
// owned by the recovered CPUInfo class below.
struct PlatformSystemStatus {
    const wchar_t* architecture = L"UnknownArchitecture";
    unsigned int processorCount = 0;
    std::uint64_t pageSize = 0;
    const wchar_t* numa = L"no supported";
    std::uint64_t minimumAddress = 0;
    std::uint64_t maximumAddress = std::numeric_limits<std::uintptr_t>::max();
    std::uint64_t availablePhysical = 0;
    std::uint64_t totalPhysical = 0;
    std::uint64_t availablePageFile = 0;
    std::uint64_t totalPageFile = 0;
};

#if !defined(_WIN32)
bool ReadUnsignedFile(const std::filesystem::path& path, std::uint64_t& value) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    input >> value;
    return !input.fail();
}

std::uint64_t ParseCacheSize(const std::string& value) {
    if (value.empty()) {
        return 0;
    }
    char* end = nullptr;
    const unsigned long long base = std::strtoull(value.c_str(), &end, 10);
    if (end == value.c_str()) {
        return 0;
    }
    std::uint64_t multiplier = 1;
    if (*end == 'K' || *end == 'k') {
        multiplier = 1024;
    } else if (*end == 'M' || *end == 'm') {
        multiplier = 1024 * 1024;
    } else if (*end == 'G' || *end == 'g') {
        multiplier = UINT64_C(1024) * 1024 * 1024;
    }
    return static_cast<std::uint64_t>(base) * multiplier;
}

wchar_t* HostArchitecture() {
#if defined(__x86_64__) || defined(_M_X64)
    static wchar_t value[] = L"x64";
#elif defined(__i386__) || defined(_M_IX86)
    static wchar_t value[] = L"x86";
#elif defined(__aarch64__) || defined(__arm64__)
    static wchar_t value[] = L"ARM64";
#elif defined(__arm__)
    static wchar_t value[] = L"ARM";
#else
    static wchar_t value[] = L"UnknownArchitecture";
#endif
    return value;
}
#endif

PlatformSystemStatus CollectPlatformSystemStatus() {
    PlatformSystemStatus result;

#if defined(_WIN32)
    SYSTEM_INFO systemInfo;
    ::GetSystemInfo(&systemInfo);
    result.architecture = L2Exception::GetProcessorArchitecture(systemInfo);
    result.processorCount = systemInfo.dwNumberOfProcessors;
    result.pageSize = systemInfo.dwPageSize;
    result.minimumAddress = reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    result.maximumAddress = reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);

    ULONG highestNumaNode = 0;
    if (::GetNumaHighestNodeNumber(&highestNumaNode)) {
        result.numa = highestNumaNode == 0 ? L"OFF" : L"ON";
    }

#else
    result.architecture = HostArchitecture();
    long processors = ::sysconf(_SC_NPROCESSORS_ONLN);
    if (processors < 1) {
        processors = static_cast<long>(std::thread::hardware_concurrency());
    }
    result.processorCount = processors > 0 ? static_cast<unsigned int>(processors) : 0U;
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pageSize > 0) {
        result.pageSize = static_cast<std::uint64_t>(pageSize);
        result.minimumAddress = result.pageSize;
    }

#if defined(__linux__)
    if (std::filesystem::exists("/sys/devices/system/node/node1")) {
        result.numa = L"ON";
    } else if (std::filesystem::exists("/sys/devices/system/node/node0")) {
        result.numa = L"OFF";
    }
#endif
#endif

    return result;
}

void CollectPlatformMemoryStatus(PlatformSystemStatus& result) {
#if defined(_WIN32)
    // Current 0x0042dbc8 initializes only dwLength, ignores the BOOL return
    // and consumes every reported field after the call.
    MEMORYSTATUSEX memory;
    memory.dwLength = sizeof(memory);
    ::GlobalMemoryStatusEx(&memory);
    result.availablePhysical = memory.ullAvailPhys;
    result.totalPhysical = memory.ullTotalPhys;
    result.availablePageFile = memory.ullAvailPageFile;
    result.totalPageFile = memory.ullTotalPageFile;
#elif defined(__APPLE__)
    const auto readSysctl = [](const char* name, auto& value) {
        std::size_t size = sizeof(value);
        return ::sysctlbyname(name, &value, &size, nullptr, 0) == 0;
    };

    readSysctl("hw.memsize", result.totalPhysical);
    vm_statistics64_data_t statistics{};
    mach_msg_type_number_t statisticsCount = HOST_VM_INFO64_COUNT;
    if (::host_statistics64(
            ::mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&statistics), &statisticsCount
        ) == KERN_SUCCESS) {
        result.availablePhysical =
            (static_cast<std::uint64_t>(statistics.free_count) + static_cast<std::uint64_t>(statistics.inactive_count)) *
            result.pageSize;
    }

    struct xsw_usage swap{};
    if (readSysctl("vm.swapusage", swap)) {
        result.totalPageFile = swap.xsu_total;
        result.availablePageFile = swap.xsu_avail;
    }
#elif defined(__linux__)
    struct sysinfo memory{};
    if (::sysinfo(&memory) == 0) {
        const std::uint64_t unit = memory.mem_unit;
        result.totalPhysical = static_cast<std::uint64_t>(memory.totalram) * unit;
        result.availablePhysical =
            (static_cast<std::uint64_t>(memory.freeram) + static_cast<std::uint64_t>(memory.bufferram)) * unit;
        result.totalPageFile = static_cast<std::uint64_t>(memory.totalswap) * unit;
        result.availablePageFile = static_cast<std::uint64_t>(memory.freeswap) * unit;
    }
#else
    const long physicalPages = ::sysconf(_SC_PHYS_PAGES);
    const long availablePages = ::sysconf(_SC_AVPHYS_PAGES);
    if (physicalPages > 0) {
        result.totalPhysical = static_cast<std::uint64_t>(physicalPages) * result.pageSize;
    }
    if (availablePages > 0) {
        result.availablePhysical = static_cast<std::uint64_t>(availablePages) * result.pageSize;
    }
#endif
}

std::uint64_t CurrentNativeThreadId() {
#if defined(_WIN32)
    return ::GetCurrentThreadId();
#elif defined(__APPLE__)
    std::uint64_t threadId = 0;
    if (::pthread_threadid_np(nullptr, &threadId) == 0) {
        return threadId;
    }
    return 0;
#elif defined(__linux__)
    return static_cast<std::uint64_t>(::syscall(SYS_gettid));
#else
    return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

} // namespace
#endif // !defined(L2_BUILD_L2SERVER)

// The Windows crash report of every product uses these helpers.
#if !defined(L2_BUILD_L2SERVER) || defined(_WIN32)
namespace {

// Portable addition. ReportCppExceptionAsCrash raises this code, so a fatal
// C++ exception gets the normal crash report.
constexpr std::uint32_t kFatalCppExceptionCode = 0xe0000002U;

// Exact PDB-local name. Current L2NPC.exe 0x0042b468 and Cached.exe
// 0x004c973c. The last entry is a portable addition.
const char* GetExceptionDescription(std::uint32_t exceptionCode) {
    struct ExceptionName {
        std::uint32_t code;
        const char* description;
    };
    constexpr std::array<ExceptionName, 25> exceptionNames{{
        {0x40010005U, "a Control-C"},
        {0x40010008U, "a Control-Break"},
        {0x80000002U, "a Datatype Misalignment"},
        {0x80000003U, "a Breakpoint"},
        {0xc0000005U, "an Access Violation"},
        {0xc0000006U, "an In Page Error"},
        {0xc0000017U, "a No Memory"},
        {0xc000001dU, "an Illegal Instruction"},
        {0xc0000025U, "a Noncontinuable Exception"},
        {0xc0000026U, "an Invalid Disposition"},
        {0xc000008cU, "a Array Bounds Exceeded"},
        {0xc000008dU, "a Float Denormal Operand"},
        {0xc000008eU, "a Float Divide by Zero"},
        {0xc000008fU, "a Float Inexact Result"},
        {0xc0000090U, "a Float Invalid Operation"},
        {0xc0000091U, "a Float Overflow"},
        {0xc0000092U, "a Float Stack Check"},
        {0xc0000093U, "a Float Underflow"},
        {0xc0000094U, "an Integer Divide by Zero"},
        {0xc0000095U, "an Integer Overflow"},
        {0xc0000096U, "a Privileged Instruction"},
        {0xc00000fdU, "a Stack Overflow"},
        {0xc0000142U, "a DLL Initialization Failed"},
        {0xe06d7363U, "a Microsoft C++ Exception"},
        {kFatalCppExceptionCode, "a Fatal C++ Exception"},
    }};
    const auto match =
        std::find_if(exceptionNames.begin(), exceptionNames.end(), [exceptionCode](const ExceptionName& candidate) {
            return candidate.code == exceptionCode;
        });
    return match != exceptionNames.end() ? match->description : "Unknown exception type";
}

#if defined(_WIN32)
bool ReadCrashMemory(const void* source, void* destination, std::size_t size) {
    SIZE_T bytesRead = 0;
    return ::ReadProcessMemory(::GetCurrentProcess(), source, destination, size, &bytesRead) != FALSE && bytesRead == size;
}

std::wstring ModuleBaseName(const void* address) {
    HMODULE module = nullptr;
    if (::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCWSTR>(address), &module
        ) == FALSE) {
        return {};
    }
    std::array<wchar_t, MAX_PATH> path{};
    const DWORD length = ::GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    const std::wstring_view name(path.data(), length);
    const std::size_t slash = name.find_last_of(L"\\/");
    return std::wstring(slash == std::wstring_view::npos ? name : name.substr(slash + 1));
}

bool IsSystemModule(const std::wstring& name) {
    return _wcsicmp(name.c_str(), L"ntdll.dll") == 0 || _wcsicmp(name.c_str(), L"KERNELBASE.dll") == 0 ||
        _wcsicmp(name.c_str(), L"kernel32.dll") == 0;
}

// Portable addition. OutputDebugStringA raises 0x40010006 to give text to a
// debugger. RecordExceptionInfo continues execution for it, so it is not a
// failure. Log the text and the module that wrote it.
void LogDebugOutputException(const EXCEPTION_RECORD& record) {
    std::string text;
    if (record.NumberParameters >= 2 && record.ExceptionInformation[1] != 0) {
        const auto* const source = reinterpret_cast<const char*>(record.ExceptionInformation[1]);
        const std::size_t limit = std::min<std::size_t>(static_cast<std::size_t>(record.ExceptionInformation[0]), 512);
        text.assign(source, ::strnlen(source, limit));
    }
    for (char& character : text) {
        if (static_cast<unsigned char>(character) < 0x20) {
            character = ' ';
        }
    }
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }

    // Skip this handler, then the system frames of the exception dispatcher
    // and OutputDebugStringA. The next frames show who wrote the text, for
    // example a C runtime and the module that called it.
    std::array<void*, 32> frames{};
    const USHORT frameCount =
        ::RtlCaptureStackBackTrace(0, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
    std::string callers;
    int callerCount = 0;
    bool systemFramesSeen = false;
    for (USHORT index = 0; index < frameCount && callerCount < 4; ++index) {
        const std::wstring module = ModuleBaseName(frames[index]);
        const bool system = IsSystemModule(module);
        if (!systemFramesSeen) {
            systemFramesSeen = system;
            continue;
        }
        if (system) {
            continue;
        }
        HMODULE base = nullptr;
        ::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCWSTR>(frames[index]), &base
        );
        char offset[32]{};
        std::snprintf(
            offset, sizeof(offset), "+0x%llx",
            static_cast<unsigned long long>(
                reinterpret_cast<std::uintptr_t>(frames[index]) - reinterpret_cast<std::uintptr_t>(base)
            )
        );
        callers += (callerCount == 0 ? "" : " < ") + WideToUtf8(module) + offset;
        ++callerCount;
    }
    L2_LOG(
        Log, LOG_LOCAL, "[debug output] \"%s\" from %s", text.c_str(), callers.empty() ? "an unknown module" : callers.c_str()
    );
}

// The original installs RecordExceptionInfo also as a vectored handler. It
// runs before every catch block. The portable code reports normal errors with
// C++ exceptions and catches them, so C++ throws pass here. An uncaught throw
// still reaches the unhandled exception filter or WindowsAbortHandler.
ExceptionFilterResult WINAPI VectoredExceptionInfo(_EXCEPTION_POINTERS* exceptionPointers) {
    const std::uint32_t exceptionCode = exceptionPointers->ExceptionRecord->ExceptionCode;
    // 0xe06d7363 is an MSVC throw. 0x20474343 is a MinGW throw (libgcc SEH unwinder).
    if (exceptionCode == 0xe06d7363U || exceptionCode == 0x20474343U) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return RecordExceptionInfo(exceptionPointers);
}

// Writes the message of the current C++ exception. Then RecordExceptionInfo
// writes LinError.txt and the minidump and ends the process. MSVC keeps the
// frames of the throw on the stack until a catch block ends, so the minidump
// can also show where the exception started.
[[noreturn]] void ReportCppExceptionAsCrash(const char* action) noexcept {
    if (const std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& exception) {
            L2_LOG_DIRECT(stderr, runtime::logging::Level::Error, "[crash report] %s: %s", action, exception.what());
        } catch (...) {
            L2_LOG_DIRECT(stderr, runtime::logging::Level::Error, "[crash report] %s of an unknown type.", action);
        }
    } else {
        L2_LOG_DIRECT(stderr, runtime::logging::Level::Error, "[crash report] %s.", action);
    }
    ::RaiseException(kFatalCppExceptionCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    std::_Exit(EXIT_FAILURE);
}

// Without this handler abort ends the process with no crash report. MSVC
// keeps a std::terminate handler for each thread, but one SIGABRT handler
// for the process. The default std::terminate calls abort, so this also
// covers a throw that leaves a noexcept function or a std::thread body.
void WindowsAbortHandler(int) {
    ReportCppExceptionAsCrash(std::current_exception() ? "Uncaught C++ exception" : "abort() was called");
}
#endif

} // namespace
#endif // !defined(L2_BUILD_L2SERVER) || defined(_WIN32)

void ReportCaughtExceptionAsCrash() {
#if defined(_WIN32)
    if (std::current_exception()) {
        ReportCppExceptionAsCrash("Stopping the server after a C++ exception");
    }
#endif
}

#if !defined(L2_BUILD_L2SERVER)
namespace {

#if !defined(_WIN32)
void GeneratePortableFatalReport() noexcept {
    try {
        L2Exception::GenerateExceptionReport(nullptr);
        SystemMail::sendCrashMail(mailServer, g_bExceptionMakeMiniDump ? g_szCrashLogPath : nullptr);
    } catch (...) {
        // A fatal-report path cannot propagate another C++ exception.
    }
}

[[noreturn]] void PortableTerminateHandler() noexcept {
    if (!g_portableCrashReportActive.test_and_set(std::memory_order_acq_rel)) {
        GeneratePortableFatalReport();
    }
    ::_exit(EXIT_FAILURE);
}

void PortableSignalHandler(int signalNumber, siginfo_t*, void*) {
    if (!g_portableCrashReportActive.test_and_set(std::memory_order_acq_rel)) {
        GeneratePortableFatalReport();
    }

    struct sigaction defaultAction{};
    defaultAction.sa_handler = SIG_DFL;
    sigemptyset(&defaultAction.sa_mask);
    ::sigaction(signalNumber, &defaultAction, nullptr);
    ::raise(signalNumber);
    ::_exit(128 + signalNumber);
}

void InstallPortableExceptionHandlers() {
    runtime::InstallThreadQuiescenceTransport();
    std::set_terminate(&PortableTerminateHandler);

    struct sigaction action{};
    action.sa_sigaction = &PortableSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    constexpr std::array<int, 5> fatalSignals{
        SIGABRT,
        SIGBUS,
        SIGFPE,
        SIGILL,
        SIGSEGV,
    };
    for (const int signalNumber : fatalSignals) {
        ::sigaction(signalNumber, &action, nullptr);
    }
}
#endif

} // namespace

const wchar_t* GetCountryName() {
    // Shared Exception.cpp source name corroborated by NASC64_2015. Current
    // L2NPC.exe 0x0042b3b4 and Cached.exe 0x004c9688 reload g_nCountryCode
    // for every comparison.
    if (g_nCountryCode > 7) {
        if (g_nCountryCode != 8) {
            if (g_nCountryCode == 9) {
                return L"EUROPE";
            }
            if (g_nCountryCode == 10) {
                return L"GERMANY";
            }
            if (g_nCountryCode != 11) {
                if (g_nCountryCode == 12) {
                    return L"POLAND";
                }
                if (g_nCountryCode != 13) {
                    return L"UNKNOWN";
                }
                return L"TURKEY";
            }
        }
        return L"RUSSIA";
    }
    if (g_nCountryCode == 7) {
        return L"INDONESIA";
    }
    if (g_nCountryCode == 0) {
        return L"KOREA";
    }
    if (g_nCountryCode == 1) {
        return L"USA";
    }
    if (g_nCountryCode == 2) {
        return L"JAPAN";
    }
    if (g_nCountryCode == 3) {
        return L"TAIWAN";
    }
    if (g_nCountryCode == 4) {
        return L"CHINA";
    }
    if (g_nCountryCode == 5) {
        return L"THAILAND";
    }
    if (g_nCountryCode != 6) {
        return L"UNKNOWN";
    }
    return L"PHILIPPINES";
}

int GetL2ProtocolVersion() {
    // Exact PDB owner (Exception.cpp:14-20) and current L2NPC.exe
    // 0x0045cbcc. Current Cached.exe emits it at 0x004c94a8.
    // The shipping body leaves both locals uninitialized before the
    // guaranteed-success parse of its fixed revision literal.
    char sTmp[80];
    int nVersion;
#if defined(_MSC_VER)
    ::sscanf_s("$Revision: #162 $", "%s #%d", sTmp, static_cast<unsigned>(sizeof(sTmp)), &nVersion);
#else
    std::sscanf("$Revision: #162 $", "%79s #%d", sTmp, &nVersion);
#endif
    return nVersion;
}

std::uint32_t CountSetBits(std::uint64_t bitMask) {
    // Current L2NPC.exe 0x0042bf94 and Cached.exe 0x004ca264 test all 64 bits
    // from the high bit down.
    std::uint32_t count = 0;
    std::uint64_t bit = UINT64_C(0x8000000000000000);
    for (int index = 0; index < 64; ++index) {
        count += (bitMask & bit) != 0 ? 1U : 0U;
        bit >>= 1U;
    }
    return count;
}

void CrashImpl() {
    // Exact PDB name and current L2NPC.exe 0x0042b354:
    // xor eax,eax, then mov byte ptr [rax],1.
#if defined(_WIN32)
    *reinterpret_cast<volatile unsigned char*>(0) = 1;
#else
    // Preserve the target's access-violation boundary instead of converting
    // it into a recoverable C++ exception.
    ::raise(SIGSEGV);
#endif
}

void L2Exception::OnException() {
    // Current Cached.exe 0x004ccb3c and the matching VS2015 Shared donor.
    // Raw instructions fix reason 26 and the ordering: publish all crash
    // metadata before the null write.
    ForceCrash(CrashInfo::THROW_EXCEPTION);
}

char* UnicodeToMultibyte(const wchar_t* pszStr) {
    // Exact PDB name and current L2NPC.exe 0x0042b668.
    if (!pszStr) {
        return nullptr;
    }

    const int size = portable::WideCharToWindowsAnsi(pszStr, -1, nullptr, 0);

    char* const result = static_cast<char*>(std::malloc(static_cast<std::size_t>(size + 1)));
    if (!result) {
        return nullptr;
    }
    portable::WideCharToWindowsAnsi(pszStr, -1, result, size);
    result[size - 1] = '\0';
    return result;
}

#endif // !defined(L2_BUILD_L2SERVER)

namespace {

// ELOG text is already encoded by its product owner. Keep the bytes and the
// existing single write/flush policy. Report failures outside ELOG itself.
class ElogFileUnwindGuard {
public:
    explicit ElogFileUnwindGuard(void* file) noexcept : file_(file) {}
    ElogFileUnwindGuard(const ElogFileUnwindGuard&) = delete;
    ElogFileUnwindGuard& operator=(const ElogFileUnwindGuard&) = delete;
    void Release() noexcept { file_ = nullptr; }
    ~ElogFileUnwindGuard() {
        if (!file_) {
            return;
        }
        const runtime::logging::StorageErrorState preserveErrors;
#if defined(_WIN32)
        const BOOL closed = ::CloseHandle(static_cast<HANDLE>(file_));
        const DWORD error = closed ? 0 : ::GetLastError();
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Error,
            "[logging] ELOG interrupted before normal close; its record may be incomplete. Cleanup close %s (Win32 error "
            "%lu).",
            closed ? "succeeded" : "failed", static_cast<unsigned long>(error)
        );
#else
        errno = 0;
        const int closed = std::fclose(static_cast<std::FILE*>(file_));
        const int error = errno;
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Error,
            "[logging] ELOG interrupted before normal close; its record may be incomplete. Cleanup close %s (errno %d).",
            closed == 0 ? "succeeded" : "failed", error
        );
#endif
    }

private:
    void* file_;
};

void WriteElogBytes(void* file, const char* bytes, std::size_t size) {
    const runtime::logging::StorageErrorState preserveErrors;
#if defined(_WIN32)
    DWORD written = 0;
    const DWORD requested = static_cast<DWORD>(size);
    const BOOL success = ::WriteFile(static_cast<HANDLE>(file), bytes, requested, &written, nullptr);
    const DWORD error = success ? 0 : ::GetLastError();
    if (!success || written != requested) {
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Error,
            "[logging] ELOG write failed: %lu of %lu bytes written (Win32 error %lu).", static_cast<unsigned long>(written),
            static_cast<unsigned long>(requested), static_cast<unsigned long>(error)
        );
    }
#else
    errno = 0;
    const std::size_t written = std::fwrite(bytes, 1, size, static_cast<std::FILE*>(file));
    const int writeError = errno;
    errno = 0;
    const int flushed = std::fflush(static_cast<std::FILE*>(file));
    const int flushError = errno;
    if (written != size || flushed != 0) {
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Error,
            "[logging] ELOG write failed: %zu of %zu bytes written (errno %d); flush %s (errno %d).", written, size,
            writeError, flushed == 0 ? "succeeded" : "failed", flushError
        );
    }
#endif
}

} // namespace

#if defined(L2_BUILD_L2SERVER)
namespace {

void WriteL2ServerElogHostWide(void* file, const wchar_t* text) {
    char* const converted = UnicodeToMultibyte(text);
    const std::size_t byteCount = std::strlen(converted);
#if defined(_WIN32)
    // DWORD written = 0
    // ::WriteFile(
    //     static_cast<HANDLE>(file),
    //     converted,
    //     static_cast<DWORD>(byteCount),
    //     &written,
    //     nullptr)
    WriteElogBytes(file, converted, byteCount);
    // Current L2Server.exe ELOG leaks both conversion allocations.
#else
    // std::fwrite(
    //     converted,
    //     1,
    //     byteCount,
    //     static_cast<std::FILE*>(file))
    // std::fflush(
    //     static_cast<std::FILE*>(file))
    WriteElogBytes(file, converted, byteCount);
    std::free(converted);
#endif
}

void WriteL2ServerElogTargetWide(void* file, const char16_t* text) {
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    WriteL2ServerElogHostWide(file, reinterpret_cast<const wchar_t*>(text));
#else
    const std::string converted =
        portable::WindowsAnsiFromUtf16(std::u16string_view(text, std::char_traits<char16_t>::length(text)));
    // std::fwrite(
    //     converted.data(),
    //     1,
    //     converted.size(),
    //     static_cast<std::FILE*>(file))
    // std::fflush(
    //     static_cast<std::FILE*>(file))
    WriteElogBytes(file, converted.data(), converted.size());
#endif
}

void InsertL2ServerElogCarriageReturns(char16_t* message, std::int32_t& length) {
    // Current L2Server.exe 0x0072884c..0x007288b6 passes the remaining
    // character count to memchr and the resulting byte count to memcpy even
    // though the carrier is UTF-16. Preserve that malformed target contract.
    std::int64_t scan = 0;
    std::int64_t limit = length;
    if (length <= 0) {
        return;
    }

    do {
        char16_t* const newline =
            static_cast<char16_t*>(std::memchr(message + scan, '\n', static_cast<std::size_t>(limit - scan)));
        if (!newline) {
            return;
        }

        if (static_cast<std::uint32_t>(length) < 0xa000U) {
            ++length;
        }
        const std::int64_t newlineIndex = newline - message;
        limit = length;
        std::memmove(newline + 1, newline, static_cast<std::size_t>(limit - newlineIndex - 1));
        *newline = u'\r';
        scan = newlineIndex + 2;
    } while (scan < limit);
}

std::uint64_t L2ServerElogThreadId() {
#if defined(_WIN32)
    return ::GetCurrentThreadId();
#elif defined(__APPLE__)
    std::uint64_t threadId = 0;
    if (::pthread_threadid_np(nullptr, &threadId) == 0) {
        return threadId;
    }
    return 0;
#elif defined(__linux__)
    return static_cast<std::uint64_t>(::syscall(SYS_gettid));
#else
    return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

} // namespace

void WriteStackDetails(void* file, _CONTEXT* context) {
    // Current L2Server.exe 0x00727b9c.
    hprintf(file, L"[Callstack]\r\n");
    hprintf(file, L"\tAddress\t\tFrame\r\n");

#if defined(_WIN32)
    STACKFRAME64 frame{};
    while (::StackWalk64(
               IMAGE_FILE_MACHINE_AMD64, ::GetCurrentProcess(), ::GetCurrentThread(), &frame, context, nullptr,
               ::SymFunctionTableAccess64, ::SymGetModuleBase64, nullptr
           ) != FALSE) {
        if (frame.AddrReturn.Offset == 0) {
            break;
        }
        hprintf(
            file, L"\t0x%016I64X\t0x%016I64X \r\n", static_cast<unsigned long long>(frame.AddrPC.Offset),
            static_cast<unsigned long long>(frame.AddrReturn.Offset)
        );
    }
#elif defined(__APPLE__) || defined(__unix__)
    (void)context;
    std::array<void*, 128> frames{};
    const int frameCount = ::backtrace(frames.data(), static_cast<int>(frames.size()));
    for (int index = 0; index + 1 < frameCount; ++index) {
        hprintf(
            file, L"\t0x%016llX\t0x%016llX \r\n",
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(frames[static_cast<std::size_t>(index)])),
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(frames[static_cast<std::size_t>(index + 1)]))
        );
    }
#else
    (void)context;
#endif
}

void WriteStackWalk(void* file, _EXCEPTION_POINTERS* exceptionPointers) {
    // Current L2Server.exe 0x00728924.
#if defined(_WIN32)
    ::SymSetOptions(SYMOPT_DEFERRED_LOADS);
    HANDLE const process = ::GetCurrentProcess();
    if (::SymInitialize(process, nullptr, TRUE) != FALSE) {
        CONTEXT context = *exceptionPointers->ContextRecord;
        WriteStackDetails(file, &context);
        ::SymCleanup(process);
        hprintf(file, L"\r\n");
    }
#else
    (void)exceptionPointers;
    WriteStackDetails(file, nullptr);
    hprintf(file, L"\r\n");
#endif
}

void ELOG(const wchar_t* format, ...) {
    // Current L2Server.exe 0x007286ac.
    if (ExceptionTerminating != 0) {
        return;
    }

    CLogCritical theCritical;

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
#if defined(_WIN32)
    SYSTEMTIME local{};
    ::GetLocalTime(&local);
    year = local.wYear;
    month = local.wMonth;
    day = local.wDay;
    hour = local.wHour;
    minute = local.wMinute;
    second = local.wSecond;
#else
    const std::time_t now = static_cast<std::time_t>(runtime::crt::Time32Now());
    std::tm local{};
    ::localtime_r(&now, &local);
    year = local.tm_year + 1900;
    month = local.tm_mon + 1;
    day = local.tm_mday;
    hour = local.tm_hour;
    minute = local.tm_min;
    second = local.tm_sec;
#endif

    void* file = nullptr;
#if defined(_WIN32)
    file = ::CreateFileW(
        g_szLinErrorPath, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (static_cast<HANDLE>(file) == INVALID_HANDLE_VALUE) {
        file = nullptr;
    } else {
        // ::SetFilePointer(
        //     static_cast<HANDLE>(file),
        //     0,
        //     nullptr,
        //     FILE_END)
        ::SetLastError(NO_ERROR);
        const DWORD position = ::SetFilePointer(static_cast<HANDLE>(file), 0, nullptr, FILE_END);
        const DWORD seekError = ::GetLastError();
        if (position == INVALID_SET_FILE_POINTER && seekError != NO_ERROR) {
            L2_LOG_DIRECT(
                stderr, runtime::logging::Level::Error,
                "[logging] Cannot seek to the end of ELOG (Win32 error %lu). Writing continues at the current position and "
                "may overwrite existing text.",
                static_cast<unsigned long>(seekError)
            );
        }
    }
#else
    file = std::fopen(WideToUtf8(g_szLinErrorPath).c_str(), "ab");
#endif

    if (!file) {
        const runtime::logging::StorageErrorState errors;
#if defined(_WIN32)
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Error,
            "[logging] Cannot open ELOG file (Win32 error %lu); this exception record was not saved.",
            static_cast<unsigned long>(errors.savedWindowsError)
        );
#else
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Error,
            "[logging] Cannot open ELOG file (errno %d); this exception record was not saved.", errors.savedErrno
        );
#endif
    }
    if (file) {
        ElogFileUnwindGuard fileOnException(file);
        std::array<wchar_t, 0x5000> hostMessage;
        int prefixLength = 0;
#if defined(_MSC_VER)
        prefixLength = ::_snwprintf_s(
            hostMessage.data(), hostMessage.size(), hostMessage.size() - 1,
            L"[(%d) %04d/%02d/%02d "
            L"%02d:%02d:%02d]: ",
            static_cast<int>(L2ServerElogThreadId()), year, month, day, hour, minute, second
        );
#else
        prefixLength = std::swprintf(
            hostMessage.data(), hostMessage.size(),
            L"[(%d) %04d/%02d/%02d "
            L"%02d:%02d:%02d]: ",
            static_cast<int>(L2ServerElogThreadId()), year, month, day, hour, minute, second
        );
#endif

        if (prefixLength < 0) {
            L2_LOG_DIRECT(
                stderr, runtime::logging::Level::Error,
                "[logging] Cannot format the ELOG header; this exception record was not written."
            );
        }
        if (prefixLength >= 0) {
            WriteL2ServerElogHostWide(file, hostMessage.data());

#if !defined(_MSC_VER)
            // Translate before va_start: allocation failure must not bypass va_end.
            const std::wstring translated = WindowsWideFormatToHost(format);
#endif
            va_list arguments;
            va_start(arguments, format);
#if defined(_MSC_VER)
            std::int32_t length =
                ::_vsnwprintf_s(hostMessage.data(), hostMessage.size(), hostMessage.size() - 1, format, arguments);
            va_end(arguments);
            if (length >= 0) {
                auto* const targetMessage = reinterpret_cast<char16_t*>(hostMessage.data());
                InsertL2ServerElogCarriageReturns(targetMessage, length);
                WriteL2ServerElogTargetWide(file, targetMessage);
            } else {
                L2_LOG_DIRECT(
                    stderr, runtime::logging::Level::Error,
                    "[logging] Cannot format ELOG text within %zu wide characters; the body was not written.",
                    hostMessage.size()
                );
            }
#else
            // const std::wstring translated =
            //     WindowsWideFormatToHost(
            //         format)
            const int hostLength = std::vswprintf(hostMessage.data(), hostMessage.size(), translated.c_str(), arguments);
            va_end(arguments);
            if (hostLength >= 0) {
                const std::u16string converted =
                    WideToUtf16(std::wstring_view(hostMessage.data(), static_cast<std::size_t>(hostLength)));
                if (converted.size() < hostMessage.size()) {
                    std::array<char16_t, 0x5000> targetMessage;
                    std::copy(converted.begin(), converted.end(), targetMessage.begin());
                    targetMessage[converted.size()] = u'\0';
                    std::int32_t length = static_cast<std::int32_t>(converted.size());
                    InsertL2ServerElogCarriageReturns(targetMessage.data(), length);
                    WriteL2ServerElogTargetWide(file, targetMessage.data());
                } else {
                    L2_LOG_DIRECT(
                        stderr, runtime::logging::Level::Error,
                        "[logging] ELOG text needs %zu UTF-16 units (capacity %zu); the body was not written.",
                        converted.size() + 1, hostMessage.size()
                    );
                }
            } else {
                L2_LOG_DIRECT(
                    stderr, runtime::logging::Level::Error,
                    "[logging] Cannot format ELOG text within %zu wide characters; the body was not written.",
                    hostMessage.size()
                );
            }
#endif
        }

        fileOnException.Release();
#if defined(_WIN32)
        // ::CloseHandle(
        //     static_cast<HANDLE>(file))
        if (!::CloseHandle(static_cast<HANDLE>(file))) {
            L2_LOG_DIRECT(
                stderr, runtime::logging::Level::Error, "[logging] Cannot close ELOG file (Win32 error %lu).",
                static_cast<unsigned long>(::GetLastError())
            );
        }
#else
        // std::fclose(
        //     static_cast<std::FILE*>(file))
        if (std::fclose(static_cast<std::FILE*>(file)) != 0) {
            L2_LOG_DIRECT(stderr, runtime::logging::Level::Error, "[logging] Cannot close ELOG file (errno %d).", errno);
        }
#endif
    }
}
#endif

#if !defined(L2_BUILD_L2SERVER)
namespace {

void WriteElogHostWide(void* file, const wchar_t* text) {
    char* const converted = UnicodeToMultibyte(text);
    const std::size_t byteCount = std::strlen(converted);
#if defined(_WIN32)
    // DWORD written = 0
    // ::WriteFile(
    //     static_cast<HANDLE>(file),
    //     converted,
    //     static_cast<DWORD>(byteCount),
    //     &written,
    //     nullptr)
    WriteElogBytes(file, converted, byteCount);
    // Current ELOG leaks both UnicodeToMultibyte allocations.
#else
    // std::fwrite(
    //     converted,
    //     1,
    //     byteCount,
    //     static_cast<std::FILE*>(file))
    // std::fflush(
    //     static_cast<std::FILE*>(file))
    WriteElogBytes(file, converted, byteCount);
    std::free(converted);
#endif
}

void WriteElogTargetWide(void* file, const char16_t* text) {
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    WriteElogHostWide(file, reinterpret_cast<const wchar_t*>(text));
#else
    const std::string converted =
        portable::WindowsAnsiFromUtf16(std::u16string_view(text, std::char_traits<char16_t>::length(text)));
    // std::fwrite(
    //     converted.data(),
    //     1,
    //     converted.size(),
    //     static_cast<std::FILE*>(file))
    // std::fflush(
    //     static_cast<std::FILE*>(file))
    WriteElogBytes(file, converted.data(), converted.size());
#endif
}

void InsertElogCarriageReturns(char16_t* message, std::int32_t& length) {
    // Current L2NPC.exe 0x0042d0f4..0x0042d15c passes a byte count to
    // memcpy even though the source and destination are UTF-16 pointers.
    // The linked VS2010 memcpy detects this right-overlap and copies
    // backwards. Preserve both the byte count and the resulting malformed
    // tail instead of replacing the operation with a logical string insert.
    std::int64_t scan = 0;
    std::int64_t limit = length;
    if (length <= 0) {
        return;
    }

    do {
        char16_t* newline = nullptr;
        for (std::int64_t index = scan; index < limit; ++index) {
            if (message[index] == u'\n') {
                newline = message + index;
                break;
            }
        }
        if (!newline) {
            return;
        }

        if (static_cast<std::uint32_t>(length) < 0xa000U) {
            ++length;
        }
        const std::int64_t newlineIndex = newline - message;
        limit = length;
        std::memmove(newline + 1, newline, static_cast<std::size_t>(limit - newlineIndex - 1));
        *newline = u'\r';
        scan = newlineIndex + 2;
    } while (scan < limit);
}

} // namespace

#if defined(_WIN32)
_RTL_CRITICAL_SECTION
CLogCritical::m_CriticalSection{};
#else
std::recursive_mutex CLogCritical::m_CriticalSection;
#endif

CLogCritical::CLogCritical() {
    // Log.Add(
    //     LOG_NORMAL,
    //     "Lock CLogCritical")
    // Noise: routine lock entry is logged before taking the lock and does not prove success.
#if defined(_WIN32)
    ::EnterCriticalSection(&m_CriticalSection);
#else
    m_CriticalSection.lock();
#endif
}

CLogCritical::~CLogCritical() {
    // Exact PDB method. Current L2NPC.exe 0x0042bf34.
    // Log.Add(
    //     LOG_NORMAL,
    //     "Unlock CLogCritical")
    // Noise: routine lock exit is logged before release and does not prove success.
#if defined(_WIN32)
    ::LeaveCriticalSection(&m_CriticalSection);
#else
    m_CriticalSection.unlock();
#endif
}

void CLogCritical::Initialize() {
    // Noise: Routine initialization of the crash-log lock reports no diagnostic result.
    // Log.Add(
    //     LOG_NORMAL,
    //     "Init CLogCritical")
#if defined(_WIN32)
    ::InitializeCriticalSection(&m_CriticalSection);
#endif
}

void CLogCritical::Deinitialize() {
    // Log.Add(
    //     LOG_NORMAL,
    //     "Deinit CLogCritical")
    // Noise: routine crash-lock teardown with no diagnostic result.
#if defined(_WIN32)
    ::DeleteCriticalSection(&m_CriticalSection);
#endif
}

#if defined(_WIN32)
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI DummySetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER) {
    // Current L2NPC.exe 0x00409db8.
    return nullptr;
}

BOOL PreventSetUnhandledExceptionFilter() {
    // Corroborating source name from the shared 2015 Exception.cpp and the
    // exact current L2NPC body at 0x0042c2f0. Current Cached.exe emits the
    // same Shared owner at unnamed body 0x004ca588.
    HMODULE const kernel32 = ::LoadLibraryW(L"kernel32.dll");
    if (!kernel32) {
        return FALSE;
    }

    FARPROC const entry = ::GetProcAddress(kernel32, "SetUnhandledExceptionFilter");
    if (!entry) {
        return FALSE;
    }

    std::array<unsigned char, 9> patch{};
    patch[0] = 0xe9;
    const std::int64_t relative = static_cast<std::int64_t>(
        reinterpret_cast<std::intptr_t>(&DummySetUnhandledExceptionFilter) - reinterpret_cast<std::intptr_t>(entry) - 5
    );
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));

    SIZE_T written = 0;
    return ::WriteProcessMemory(
        ::GetCurrentProcess(), reinterpret_cast<LPVOID>(entry), patch.data(), patch.size(), &written
    );
}
#endif

std::int32_t CExceptionInit::g_timeStart = 0;

void InitializeExceptionRuntime() {
    // Current L2NPC.exe 0x0042e6d0 and Cached.exe 0x004cca1c. Both call this
    // sequence directly from WinMain. Neither emits a pointer to a
    // process-global CExceptionInit object.
    CLogCritical::Initialize();
    L2Exception::MakeFileNameWithModuleName(g_szLinErrorPath, LIN_ERROR_TXT);
    CExceptionInit::g_timeStart = runtime::crt::Time32Now();
    CExceptionInit exceptionInit;
#if defined(_WIN32)
    ::_set_invalid_parameter_handler(&myInvalidParameterHandler);
#else
    InstallPortableExceptionHandlers();
#endif
}

CPUInfo::CPUInfo() : m_htEnabled(false), m_logicalProcessorCount(0), m_numaNodeCount(0), m_processorCoreCount(0) {
    // The current caller initializes exactly these first 16 bytes before
    // CPUInfo::Init. The three CpuCache records are filled from the Windows
    // processor-information records.
}

#if defined(_WIN32)
bool CPUInfo::CollectCPUInformation(_SYSTEM_LOGICAL_PROCESSOR_INFORMATION*& buffer, unsigned long& returnLength) {
    // Exact PDB owner. Current L2NPC.exe 0x0042ccc8 and Cached.exe
    // 0x004caf58.
    bool completed = false;
    do {
        if (!::GetLogicalProcessorInformation(buffer, &returnLength)) {
            const DWORD firstError = ::GetLastError();
            if (firstError != ERROR_INSUFFICIENT_BUFFER) {
                // The shipping body builds and discards this 128-WCHAR
                // diagnostic, including a second live GetLastError call.
                std::array<wchar_t, 128> errorMessage{};
                std::swprintf(errorMessage.data(), errorMessage.size(), L"GetLastError(%d)\n", ::GetLastError());
                return false;
            }
            if (buffer) {
                std::free(buffer);
            }
            buffer = static_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(std::malloc(returnLength));
            if (!buffer) {
                return false;
            }
        } else {
            completed = true;
        }
    } while (!completed);
    return true;
}
#endif

void CPUInfo::Init() {
    // Exact PDB owner. Windows path follows current L2NPC.exe 0x0042cd7c and
    // Cached.exe 0x004cb020.
#if defined(_WIN32)
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION* buffer = nullptr;
    unsigned long returnLength = 0;
    if (!CollectCPUInformation(buffer, returnLength)) {
        return;
    }

    const unsigned char* cursor = reinterpret_cast<unsigned char*>(buffer);
    const unsigned char* const end = cursor + returnLength;
    while (cursor + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= end) {
        const auto& information = *reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(cursor);
        switch (information.Relationship) {
            case RelationProcessorCore:
                ++m_processorCoreCount;
                m_htEnabled = information.ProcessorCore.Flags != 0;
                m_logicalProcessorCount += CountSetBits(static_cast<std::uint64_t>(information.ProcessorMask));
                break;
            case RelationNumaNode:
                ++m_numaNodeCount;
                break;
            case RelationCache:
                if (information.Cache.Level > 0 && information.Cache.Level < 4) {
                    CpuCache& cache = m_cache[information.Cache.Level - 1];
                    cache.m_LineSize = information.Cache.LineSize;
                    cache.m_TotalSize = static_cast<int>(information.Cache.Size);
                }
                break;
            default:
                break;
        }
        cursor += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    }
    std::free(buffer);
#else
    // A portable host may correctly expose no L2/L3 sysfs or sysctl
    // entry. Windows normally supplies all cache relationships, while the
    // portability boundary must not print indeterminate values for missing
    // host records.
    std::fill(std::begin(m_cache), std::end(m_cache), CpuCache{0, 0});

    const auto toInt = [](std::uint64_t value) {
        return value > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max()
                                                                                   : static_cast<int>(value);
    };

    long logicalProcessors = ::sysconf(_SC_NPROCESSORS_ONLN);
    if (logicalProcessors < 1) {
        logicalProcessors = static_cast<long>(std::thread::hardware_concurrency());
    }
    const std::uint32_t logicalCount = logicalProcessors > 0 ? static_cast<std::uint32_t>(logicalProcessors) : 0U;
    m_logicalProcessorCount += logicalCount;

#if defined(__APPLE__)
    const auto readSysctl = [](const char* name, auto& value) {
        std::size_t size = sizeof(value);
        return ::sysctlbyname(name, &value, &size, nullptr, 0) == 0;
    };

    std::uint32_t physicalCount = 0;
    if (!readSysctl("hw.physicalcpu", physicalCount)) {
        physicalCount = logicalCount;
    }
    m_processorCoreCount += physicalCount;
    m_htEnabled = physicalCount != 0 && logicalCount > physicalCount;

    std::uint64_t cacheLine = 0;
    readSysctl("hw.cachelinesize", cacheLine);
    std::array<std::uint64_t, 3> cacheSize{};
    readSysctl("hw.l1dcachesize", cacheSize[0]);
    readSysctl("hw.l2cachesize", cacheSize[1]);
    readSysctl("hw.l3cachesize", cacheSize[2]);
    for (std::size_t index = 0; index < cacheSize.size(); ++index) {
        m_cache[index].m_LineSize = toInt(cacheLine);
        m_cache[index].m_TotalSize = toInt(cacheSize[index]);
    }
#elif defined(__linux__)
    std::set<std::pair<std::uint64_t, std::uint64_t>> physicalCores;
    for (std::uint32_t cpu = 0; cpu < logicalCount; ++cpu) {
        const std::filesystem::path root =
            std::filesystem::path("/sys/devices/system/cpu") / ("cpu" + std::to_string(cpu)) / "topology";
        std::uint64_t package = 0;
        std::uint64_t core = 0;
        if (ReadUnsignedFile(root / "physical_package_id", package) && ReadUnsignedFile(root / "core_id", core)) {
            physicalCores.emplace(package, core);
        }
    }
    const std::uint32_t physicalCount =
        physicalCores.empty() ? logicalCount : static_cast<std::uint32_t>(physicalCores.size());
    m_processorCoreCount += physicalCount;
    m_htEnabled = physicalCount != 0 && logicalCount > physicalCount;

    const std::filesystem::path nodeRoot = "/sys/devices/system/node";
    std::error_code nodeError;
    for (const auto& entry : std::filesystem::directory_iterator(nodeRoot, nodeError)) {
        const std::string name = entry.path().filename().string();
        if (name.size() > 4 && name.compare(0, 4, "node") == 0 &&
            std::all_of(name.begin() + 4, name.end(), [](unsigned char value) { return value >= '0' && value <= '9'; })) {
            ++m_numaNodeCount;
        }
    }

    const std::filesystem::path cacheRoot = "/sys/devices/system/cpu/cpu0/cache";
    for (int index = 0; index < 16; ++index) {
        const std::filesystem::path root = cacheRoot / ("index" + std::to_string(index));
        std::uint64_t level = 0;
        if (!ReadUnsignedFile(root / "level", level) || level < 1 || level > 3) {
            continue;
        }
        std::uint64_t line = 0;
        ReadUnsignedFile(root / "coherency_line_size", line);
        std::ifstream sizeInput(root / "size");
        std::string size;
        sizeInput >> size;
        CpuCache& cache = m_cache[static_cast<std::size_t>(level - 1)];
        cache.m_LineSize = toInt(line);
        cache.m_TotalSize = toInt(ParseCacheSize(size));
    }
#else
    m_processorCoreCount += logicalCount;
#endif
#endif
}

wchar_t* CPUInfo::PrintHyperThreadResult() {
    static wchar_t disabled[] = L"false";
    static wchar_t enabled[] = L"true";
    return m_htEnabled ? enabled : disabled;
}

int CPUInfo::GetNUMANodeCount() { return static_cast<int>(m_numaNodeCount); }

int CPUInfo::GetProcessorCoreCount() { return static_cast<int>(m_processorCoreCount); }

int CPUInfo::GetLogicalProcessorCount() { return static_cast<int>(m_logicalProcessorCount); }

std::wstring CPUInfo::PrintCacheLineSize() {
    // Current L2NPC.exe 0x0042da6c and Cached.exe 0x004cbcf8 use a
    // 256-WCHAR temporary.
    std::array<wchar_t, 256> buffer{};
    std::swprintf(
        buffer.data(), buffer.size(), L"CacheLine %d/%d/%d", m_cache[0].m_LineSize, m_cache[1].m_LineSize,
        m_cache[2].m_LineSize
    );
    return buffer.data();
}

std::wstring CPUInfo::PrintCacheTotalSize() {
    // Current L2NPC.exe 0x0042db08 and Cached.exe 0x004cbdb4 use a
    // 512-WCHAR temporary.
    std::array<wchar_t, 512> buffer{};
    std::swprintf(
        buffer.data(), buffer.size(), L"TotalCacheSize %dk/%dk/%dk", m_cache[0].m_TotalSize / 1024,
        m_cache[1].m_TotalSize / 1024, m_cache[2].m_TotalSize / 1024
    );
    return buffer.data();
}

wchar_t* L2Exception::GetProcessorArchitecture(_SYSTEM_INFO& systemInfo) {
    // Exact PDB name. Current L2NPC.exe 0x0042bf60 and Cached.exe
    // 0x004ca230.
    static wchar_t x86[] = L"x86";
    static wchar_t x64[] = L"x64";
    static wchar_t ia32OnWin64[] = L"IA32 on Win64";
    static wchar_t unknown[] = L"UnknownArchitecture";
#if defined(_WIN32)
    switch (systemInfo.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_INTEL:
            return x86;
        case PROCESSOR_ARCHITECTURE_AMD64:
            return x64;
        case PROCESSOR_ARCHITECTURE_IA32_ON_WIN64:
            return ia32OnWin64;
        default:
            return unknown;
    }
#else
    return HostArchitecture();
#endif
}

void L2Exception::SuspendThreadExceptMe() {
    // Current Cached.exe 0x004ca0e8 and the PDB lines fix
    // ..\Shared\Exception.cpp:0x558/0x560.
    {
        L2_GUARD(g_ThreadSuspendLock);
        for (int threadId = 0; threadId < CThreadManager::GetThreadCount(); ++threadId) {
            if (threadId != CThreadManager::GetThreadID()) {
#if defined(_WIN32)
                ::SuspendThread(static_cast<HANDLE>(CThreadManager::GetThread(threadId)));
#else
                runtime::SuspendNativeThread(CThreadManager::GetThread(threadId));
#endif
            }
        }
    }

    if (!CThreadManager::IsListenThread()) {
#if defined(_WIN32)
        ::SuspendThread(static_cast<HANDLE>(CThreadManager::GetThread(ThreadID::LISTEN)));
#else
        runtime::SuspendNativeThread(CThreadManager::GetThread(ThreadID::LISTEN));
#endif
    }
    if (!CThreadManager::IsMainThread()) {
#if defined(_WIN32)
        ::SuspendThread(static_cast<HANDLE>(CThreadManager::GetThread(ThreadID::MAIN)));
#else
        // The current EXE never publishes a MAIN-slot handle: its matching
        // SuspendThread(NULL) fails. Keep the same lookup/no-op boundary.
        runtime::SuspendNativeThread(CThreadManager::GetThread(ThreadID::MAIN));
#endif
    }
}

void L2Exception::ResumeThreadExceptMe() {
    // Current L2NPC.exe 0x0042be90 and Cached.exe 0x004ca18c resume the
    // fixed roles first, then take the shared suspend lock around the live
    // worker-thread traversal at Exception.cpp:0x593/0x59b.
    if (!CThreadManager::IsMainThread()) {
#if defined(_WIN32)
        ::ResumeThread(static_cast<HANDLE>(CThreadManager::GetThread(ThreadID::MAIN)));
#else
        runtime::ResumeNativeThread(CThreadManager::GetThread(ThreadID::MAIN));
#endif
    }
    if (!CThreadManager::IsListenThread()) {
#if defined(_WIN32)
        ::ResumeThread(static_cast<HANDLE>(CThreadManager::GetThread(ThreadID::LISTEN)));
#else
        runtime::ResumeNativeThread(CThreadManager::GetThread(ThreadID::LISTEN));
#endif
    }

    {
        L2_GUARD(g_ThreadSuspendLock);
        for (int threadId = 0; threadId < CThreadManager::GetThreadCount(); ++threadId) {
            if (threadId != CThreadManager::GetThreadID()) {
#if defined(_WIN32)
                ::ResumeThread(static_cast<HANDLE>(CThreadManager::GetThread(threadId)));
#else
                runtime::ResumeNativeThread(CThreadManager::GetThread(threadId));
#endif
            }
        }
    }
}

int GetElapsedTime() {
    // Current Cached.exe Shared/Exception.cpp body 0x004c962c reads the
    // current 32-bit CRT time, subtracts server_up_time through difftime and
    // truncates the double result to a signed dword.
    const std::int32_t now = runtime::crt::Time32Now();
    return runtime::Win64CvttDoubleToInt32(
        std::difftime(static_cast<std::time_t>(now), static_cast<std::time_t>(server_up_time))
    );
}

wchar_t* GetElapsedTimeString() {
    // Current L2NPC.exe 0x0042c810 uses floating division followed by the
    // x64 truncating conversion, formats into wchar_t[256], then calls the
    // CRT wide-string duplicator regardless of the formatter's return.
    // Current Cached.exe unnamed body 0x004ca950 emits the same Shared
    // operation and is reached by both CachedStatusWnd refresh paths.
    const int elapsed = GetElapsedTime();
    const int days = runtime::Win64CvttDoubleToInt32(static_cast<double>(elapsed) / 86400.0);
    double remainder = static_cast<double>(elapsed) - static_cast<double>(days * 86400);
    const int hours = runtime::Win64CvttDoubleToInt32(remainder / 3600.0);
    remainder -= static_cast<double>(hours * 3600);
    const int minutes = runtime::Win64CvttDoubleToInt32(remainder / 60.0);
    const int seconds = runtime::Win64CvttDoubleToInt32(remainder - static_cast<double>(minutes * 60));

    std::array<wchar_t, 0x100> buffer{};
    const int length =
        std::swprintf(buffer.data(), buffer.size(), L"%d dd %d:%d:%d hh:mm:ss", days, hours, minutes, seconds);
    if (length < 0) {
        // VS2010's secure formatter clears the destination on this path.
        // _wcsdup still receives that cleared carrier.
        buffer[0] = L'\0';
    }

    const std::size_t bytes = (std::wcslen(buffer.data()) + 1) * sizeof(wchar_t);
    wchar_t* const result = static_cast<wchar_t*>(std::malloc(bytes));
    if (!result) {
        return nullptr;
    }
    std::memcpy(result, buffer.data(), bytes);
    return result;
}

wchar_t* GetCurrentTimeString() {
    // Current Cached.exe unnamed Shared/Exception.cpp body 0x004c9654.
#if defined(_MSC_VER)
    __time32_t current_time = 0;
    ::_time32(&current_time);
    return ::_wctime32(&current_time);
#else
    return TimeString(runtime::crt::Time32Now());
#endif
}

wchar_t* GetServerUpTimeString() {
    // Current Cached.exe unnamed Shared/Exception.cpp body 0x004c967c.
#if defined(_MSC_VER)
    return ::_wctime32(reinterpret_cast<const __time32_t*>(&server_up_time));
#else
    return TimeString(server_up_time);
#endif
}

CrashInfo::CrashInfo() = default;

CrashInfo::~CrashInfo() = default;

void CrashInfo::SetReason(ReasonType reason) { m_ReasonList[static_cast<int>(reason)] = 1; }

void CrashInfo::Print(void* file) {
    // Current L2NPC.exe 0x0042b35c and Cached.exe 0x004c95d4 walk the
    // 75-entry array and its parallel pointer table in lockstep, printing
    // every non-zero reason.
    for (std::size_t index = 0; index < std::size(m_ReasonList); ++index) {
        if (m_ReasonList[index] == 0) {
            continue;
        }
        hprintf(file, L"\tReason : %s \r\n", g_CrashReasonString[index]);
    }
}
#endif

CBuildDate::CBuildDate() {
    L2_GUARD_FUNCTION();

    // These are constructor-body stores in the current image, after the
    // guard frame, instead of C++ member initializers.
    buildDate = L"May 30 2018";
#if defined(L2_BUILD_CACHED)
    // Current Cached.exe CBuildDate::CBuildDate at 0x004cc078.
    buildTime = L"10:05:53";
    buildDateTime = L"May 30 2018_10:05:53";
#elif defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe CBuildDate::CBuildDate at 0x00729120.
    buildTime = L"10:07:45";
    buildDateTime = L"May 30 2018_10:07:45";
#else
    // Current L2NPC.exe CBuildDate::CBuildDate at 0x0042dd90.
    buildTime = L"10:06:06";
    buildDateTime = L"May 30 2018_10:06:06";
#endif
    std::fill_n(buildArchive, std::size(buildArchive), ArchiveChar{});

    // The target constructs a real temporary UTF-16 std::wstring, searches
    // its first and final slash, then creates a second substring before
    // the secure copy. Keep those allocation/failure boundaries instead of
    // folding the fixed marker into a constexpr view.
    const std::u16string marker = u"$Archive: /Programming/Work/Server/Main/Shared/Exception.cpp $";
    const std::size_t first = marker.find(u'/');
    const std::size_t last = marker.rfind(u'/');
    const std::u16string archive = first < last ? marker.substr(first, last - first) : marker;
    const std::size_t length = archive.size();
    for (std::size_t index = 0; index < length; ++index) {
        buildArchive[index] = static_cast<ArchiveChar>(archive[index]);
    }
    buildArchive[length] = 0;
}

CBuildDate& CBuildDate::inst() {
    static CBuildDate instance;
    return instance;
}

const wchar_t* CBuildDate::BuildDate() const { return buildDate; }

const wchar_t* CBuildDate::BuildTime() const { return buildTime; }

const wchar_t* CBuildDate::BuildDateTime() const { return buildDateTime; }

const wchar_t* CBuildDate::BuildArchive() const {
    if constexpr (sizeof(wchar_t) == 2) {
        return reinterpret_cast<const wchar_t*>(buildArchive);
    } else {
        static thread_local std::wstring wide;
        std::size_t length = 0;
        while (length < std::size(buildArchive) && buildArchive[length] != 0) {
            ++length;
        }
        wide = Utf16ToWide(std::u16string_view(reinterpret_cast<const char16_t*>(buildArchive), length));
        return wide.data();
    }
}

void L2Exception::GenerateExceptionReportBody(void* file, _EXCEPTION_POINTERS* exceptionPointers) {
    // Current L2NPC.exe 0x0042b920, Cached.exe 0x004c9c00 and
    // L2Server.exe 0x00726d4c. All three shipping functions have the same
    // 1154-byte control-flow/call contract.
    if (!exceptionPointers) {
        return;
    }

#if defined(_WIN32)
    const EXCEPTION_RECORD* const record = exceptionPointers->ExceptionRecord;
    const CONTEXT* const context = exceptionPointers->ContextRecord;

    hprintf(file, L"[Crash Info]\r\n");

    // The shipping source passes sizeof(WCHAR[MAX_PATH]) to an API whose
    // final argument is a character count, preserving its historical
    // 260-element/520-count mismatch.
    std::array<wchar_t, 260> modulePath;
    const wchar_t* moduleName = L"Unknown";
    MEMORY_BASIC_INFORMATION memoryInfo;
    if (::VirtualQuery(
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(context->Rip)), &memoryInfo, sizeof(memoryInfo)
        ) != 0 &&
        ::GetModuleFileNameW(
            static_cast<HMODULE>(memoryInfo.AllocationBase), modulePath.data(), static_cast<DWORD>(sizeof(modulePath))
        ) != 0) {
        wchar_t* const separator = std::wcsrchr(modulePath.data(), L'\\');
        moduleName = separator ? separator + 1 : modulePath.data();
    }

    hprintf(
        file,
        L"\t%S in module %s at "
        L"%04x:0x%016I64X.\r\n",
        GetExceptionDescription(record->ExceptionCode), moduleName, static_cast<unsigned>(context->SegCs),
        static_cast<unsigned long long>(context->Rip)
    );

    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        const wchar_t* const operation = record->ExceptionInformation[0] == 0 ? L"Read from" : L"Write to";
        hprintf(
            file,
            L"\t%s location 0x%016I64X "
            L"caused an access violation.\r\n",
            operation, static_cast<unsigned long long>(record->ExceptionInformation[1])
        );
    }

    hprintf(file, L"\r\n");
    hprintf(file, L"\tRegisters:\r\n");
    hprintf(
        file,
        L"\tRAX=0x%016I64X CS=%04x "
        L"RIP=0x%016I64X EFLGS=0x%016I64X\r\n",
        static_cast<unsigned long long>(context->Rax), static_cast<unsigned>(context->SegCs),
        static_cast<unsigned long long>(context->Rip), static_cast<unsigned long long>(context->EFlags)
    );
    hprintf(
        file,
        L"\tRBX=0x%016I64X SS=%04x "
        L"RSP=0x%016I64X RBP  =0x%016I64X\r\n",
        static_cast<unsigned long long>(context->Rbx), static_cast<unsigned>(context->SegSs),
        static_cast<unsigned long long>(context->Rsp), static_cast<unsigned long long>(context->Rbp)
    );
    hprintf(
        file,
        L"\tRCX=0x%016I64X DS=%04x "
        L"RSI=0x%016I64X FS=%04x\r\n",
        static_cast<unsigned long long>(context->Rcx), static_cast<unsigned>(context->SegDs),
        static_cast<unsigned long long>(context->Rsi), static_cast<unsigned>(context->SegFs)
    );
    hprintf(
        file,
        L"\tRDX=0x%016I64X ES=%04x "
        L"RDI=0x%016I64X GS=%04x\r\n",
        static_cast<unsigned long long>(context->Rdx), static_cast<unsigned>(context->SegEs),
        static_cast<unsigned long long>(context->Rdi), static_cast<unsigned>(context->SegGs)
    );
    hprintf(
        file, L"\tR08=0x%016I64X R12=0x%016I64X\r\n", static_cast<unsigned long long>(context->R8),
        static_cast<unsigned long long>(context->R12)
    );
    hprintf(
        file, L"\tR09=0x%016I64X R13=0x%016I64X\r\n", static_cast<unsigned long long>(context->R9),
        static_cast<unsigned long long>(context->R13)
    );
    hprintf(
        file, L"\tR10=0x%016I64X R14=0x%016I64X\r\n", static_cast<unsigned long long>(context->R10),
        static_cast<unsigned long long>(context->R14)
    );
    hprintf(
        file, L"\tR11=0x%016I64X R15=0x%016I64X\r\n", static_cast<unsigned long long>(context->R11),
        static_cast<unsigned long long>(context->R15)
    );

    hprintf(file, L"\r\n");
    hprintf(file, L"\tBytes at CS:RIP:\r\n");
    hprintf(file, L"\t");
    const auto instruction = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(context->Rip));
#if defined(_MSC_VER)
    __try {
        for (int codebyte = 0; codebyte < 16; ++codebyte) {
            hprintf(file, L"%02x ", static_cast<unsigned>(instruction[codebyte]));
        }

        hprintf(file, L"\r\n\r\n");
        hprintf(file, L"\tStackdump\r\n");

        static wchar_t buffer[1000]{};
        auto* pStack = reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(context->Rsp));
        std::uint32_t* const end = pStack + 0xa0;
        int Count = 0;
        wchar_t* output = buffer;
        while (pStack < end) {
            if ((Count & 7) == 0) {
                output += ::wsprintfW(output, L"\t%08p: ", pStack);
            }

            ++Count;
            const char* const separator = (Count & 7) == 0 || pStack + 1 == end ? "\r\n" : " ";
            output += ::wsprintfW(output, L"%08x%S", *pStack, separator);
            ++pStack;

            if (output > buffer + 950) {
                hprintf(file, L"%s", buffer);
                buffer[0] = L'\0';
                output = buffer;
            }
        }
        hprintf(file, L"%s", buffer);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        hprintf(
            file,
            L"Exception encountered during "
            L"stack dump.\r\n"
        );
    }
#else
    for (int index = 0; index < 16; ++index) {
        std::uint8_t byte = 0;
        if (ReadCrashMemory(instruction + index, &byte, sizeof(byte))) {
            hprintf(file, L"%02x ", static_cast<unsigned>(byte));
        } else {
            hprintf(file, L"?? ");
        }
    }

    hprintf(file, L"\r\n\r\n");
    hprintf(file, L"\tStackdump\r\n");
    constexpr std::size_t kStackDwordCount = 0x280 / sizeof(std::uint32_t);
    std::array<std::uint32_t, kStackDwordCount> stackValues{};
    const auto stack = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(context->Rsp));
    bool stackReadable = true;
    for (std::size_t index = 0; index < stackValues.size(); ++index) {
        if (!ReadCrashMemory(stack + index * sizeof(std::uint32_t), &stackValues[index], sizeof(stackValues[index]))) {
            stackReadable = false;
            break;
        }
    }
    if (!stackReadable) {
        hprintf(
            file,
            L"Exception encountered during "
            L"stack dump.\r\n"
        );
    } else {
        for (std::size_t index = 0; index < stackValues.size(); ++index) {
            if (index % 8 == 0) {
                hprintf(file, L"\t%08p: ", stack + index * sizeof(std::uint32_t));
            }
            const char* const separator = index % 8 == 7 || index + 1 == stackValues.size() ? "\r\n" : " ";
            hprintf(file, L"%08x%S", static_cast<unsigned>(stackValues[index]), separator);
        }
    }
#endif
    hprintf(file, L"\r\n");
#else
    // `_EXCEPTION_POINTERS` is a Win32 carrier. Native POSIX reports obtain
    // their instruction frames in WriteStackWalk instead.
    hprintf(file, L"[Crash Info]\r\n\r\n");
#endif
}

#if !defined(L2_BUILD_L2SERVER)

void WriteStackDetails(void* file, _CONTEXT* context) {
    // Exact PDB name. Current L2NPC.exe 0x0042c5ec and Cached.exe
    // 0x004ca7e0.
    hprintf(file, L"[Callstack]\r\n");
    hprintf(file, L"\tAddress\t\tFrame\r\n");

#if defined(_WIN32)
    STACKFRAME64 frame{};
    while (::StackWalk64(
               IMAGE_FILE_MACHINE_AMD64, ::GetCurrentProcess(), ::GetCurrentThread(), &frame, context, nullptr,
               ::SymFunctionTableAccess64, ::SymGetModuleBase64, nullptr
           ) != FALSE) {
        if (frame.AddrReturn.Offset == 0) {
            break;
        }
        hprintf(
            file, L"\t0x%016I64X\t0x%016I64X \r\n", static_cast<unsigned long long>(frame.AddrPC.Offset),
            static_cast<unsigned long long>(frame.AddrReturn.Offset)
        );
    }
#elif defined(__APPLE__) || defined(__unix__)
    std::array<void*, 128> frames{};
    const int frameCount = ::backtrace(frames.data(), static_cast<int>(frames.size()));
    for (int index = 0; index + 1 < frameCount; ++index) {
        hprintf(
            file, L"\t0x%016llX\t0x%016llX \r\n",
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(frames[static_cast<std::size_t>(index)])),
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(frames[static_cast<std::size_t>(index + 1)]))
        );
    }
#else
#endif
}

void WriteStackWalk(void* file, _EXCEPTION_POINTERS* exceptionPointers) {
    // Exact PDB name. Current L2NPC.exe 0x0042d1cc and Cached.exe
    // 0x004cb490.
#if defined(_WIN32)
    ::SymSetOptions(SYMOPT_DEFERRED_LOADS);
    HANDLE const process = ::GetCurrentProcess();
    if (::SymInitialize(process, nullptr, TRUE) != FALSE) {
        CONTEXT context = *exceptionPointers->ContextRecord;
        WriteStackDetails(file, &context);
        ::SymCleanup(process);
        hprintf(file, L"\r\n");
    }
#else
    WriteStackDetails(file, nullptr);
    hprintf(file, L"\r\n");
#endif
}

void L2Exception::PrintObjectReport(void* file) {
    // Current L2NPC.exe 0x0042bfbc and Cached.exe 0x004cb978 have distinct
    // product rows beneath the shared heading.
    hprintf(file, L"[Object Report]\r\n");
#if defined(L2_BUILD_CACHED)
    // MSVC evaluates the hprintf arguments right-to-left: the account map is
    // counted before the leading CUserDb map in the shipping body.
    const int loadedAccountCount = g_accounts.GetSize();
    const int loadedUserCount = g_userDb.m_usersByWorldAndId.GetSize();
    hprintf(file, L"\tloaded user[%d], loaded account[%d]\r\n", loadedUserCount, loadedAccountCount);
#else
    hprintf(
        file, L"\tuser[%d], npc[%d]\r\n", g_ServerContext.User.load(std::memory_order_relaxed),
        g_ServerContext.Npc.load(std::memory_order_relaxed)
    );
#endif
}

void L2Exception::PrintCrashSummary(void* file) {
    // Current L2NPC.exe 0x0042d890 and Cached.exe 0x004cba90.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"in generate crash summary")
    // Noise: the report file itself immediately receives the crash-summary section.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"in generate crash summary")
    hprintf(file, L"[Crash Summary]\r\n");

    const int threadId = CThreadManager::GetThreadID();
#if defined(L2_BUILD_CACHED)
    if (threadId == ThreadID::FILE_SERVER) {
        hprintf(file, L"\tFileServerThread crashed\r\n");
    } else if (threadId == ThreadID::LOG) {
        hprintf(file, L"\tLogThread crashed\r\n");
    }
#endif
    // Log.Add(
    //     LOG_LOCAL,
    //     L"reported crashed thread [%d]",
    //     threadId)
    // Noise: the crashed worker is recorded in the report body below.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"reported crashed thread [%d]",
    //     threadId)
    hprintf(file, L"\tCrashed Thread[%d]\r\n", threadId);
    // Log.Add(
    //     LOG_LOCAL,
    //     L"by system manager? [%d]",
    //     static_cast<int>(
    //         g_IsBySystemManager))
    // Noise: the report body records whether the system manager requested it.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"by system manager? [%d]",
    //     static_cast<int>(
    //         g_IsBySystemManager))

    if (g_bInvalidParameter) {
        hprintf(file, L"\tInvalid parameter checked[%s]\r\n", g_sInvalidParamExpr);
    }
    if (g_IsBySystemManager) {
        hprintf(file, L"\tServer is closed by system manager.\r\n");
    }
    if (!bCanConnectCacheServer) {
        hprintf(file, L"\tCan't connect to cache server \r\n");
    }

    g_CrashInfo.Print(file);
    if (bForceCrash) {
        hprintf(file, L"\tWas forced crash by\r\n");
        hprintf(file, L"\t\t%s\r\n", g_CrashFunctionName);
        hprintf(file, L"\t\t%s%s\r\n", g_CrashFileName, g_CrashLineNo);
    }

    auto& deadlockCrashInfo = Singleton<CIODeadlockChecker>::inst().m_DeadlockCrashInfo;
    if (deadlockCrashInfo.ForcedCrashByDeadlockChecker()) {
        deadlockCrashInfo.PrintLog(file);
#if defined(L2_BUILD_CACHED)
        CSqlPoolManager::Instance().WriteUnhandledPoolCrashLog();
#endif
    }
}

void L2Exception::PrintIOBuffer(void* file) {
    // Current L2NPC.exe 0x0042ce48 and Cached.exe 0x004cb0ec.
    hprintf(file, L"[IOBuffer Pool]\r\n");
    const int pendingWrite = g_nSocketPendingWrite.load(std::memory_order_relaxed);
    // MSVC evaluates the target call arguments right-to-left here: total is
    // observed before free/count. Keep the order explicit on every host.
    const int totalBufferCount = GetIOBufferPoolTotalCount();
    const int freeBufferCount = GetIOBufferPoolCount();
    hprintf(
        file,
        L"\tIOBufferPool - %d / %d,  "
        L"PendingWrite %d bytes "
        L"(%d buffer count) \r\n",
        freeBufferCount, totalBufferCount, pendingWrite, pendingWrite / 0x5000
    );
    hprintf(
        file,
        L"\tDetail IO Buffer Pool Size "
        L"for each Thread\r\n"
    );

    for (int threadId = 0;
        // Current 0x0042ce48 reloads the live thread count before the first
        // iteration and after every printed row.
        threadId < CThreadManager::GetThreadCount(); ++threadId) {
        // The pool pointer and its total field are observed first. The target
        // then reloads the pointer and reads the instantaneous free count.
        const int threadTotal = GetIOBufferPoolThreadTotalCount(threadId);
        const int threadFree = GetIOBufferPoolThreadCount(threadId);
        hprintf(
            file,
            L"\t(%02d) : Free(%d) / "
            L"Total(%d)\r\n",
            threadId, threadFree, threadTotal
        );
    }
}

void L2Exception::PrintHardwareStatus(void* file) {
    // Current L2NPC.exe 0x0042dbc8 and Cached.exe 0x004cbe94. CPUInfo remains
    // the exact recovered owner. PlatformSystemStatus replaces only
    // SYSTEM_INFO and MEMORYSTATUSEX on non-Windows hosts.
    hprintf(file, L"[Hardware Status]\r\n");

    CPUInfo cpu;
    cpu.Init();
    PlatformSystemStatus hardware = CollectPlatformSystemStatus();

    hprintf(
        file,
        L"\tCPU\t\t\t: %s, %d processor(s), "
        L"PageSize:%d, NUMA[%s] SMT[%s]\r\n",
        hardware.architecture, static_cast<int>(hardware.processorCount), static_cast<int>(hardware.pageSize), hardware.numa,
        cpu.PrintHyperThreadResult()
    );

    const std::wstring totalCacheSize = cpu.PrintCacheTotalSize();
    const std::wstring cacheLine = cpu.PrintCacheLineSize();
    hprintf(file, L"\tCache(L1/L2/L3)\t: %s, %s\r\n", cacheLine.c_str(), totalCacheSize.c_str());
    hprintf(
        file,
        L"\tAccessible address\t: "
        L"0x%016I64X ~ 0x%016I64X\r\n",
        static_cast<unsigned long long>(hardware.minimumAddress), static_cast<unsigned long long>(hardware.maximumAddress)
    );
    CollectPlatformMemoryStatus(hardware);
    hprintf(
        file,
        L"\tPhysical Memory\t: Free [%d MB] / "
        L"Total [%d MB] \r\n",
        static_cast<std::int32_t>(hardware.availablePhysical >> 20U),
        static_cast<std::int32_t>(hardware.totalPhysical >> 20U)
    );
    hprintf(
        file,
        L"\tPageFile Memory\t: Free [%d MB] / "
        L"Total [%d MB] \r\n",
        static_cast<std::int32_t>(hardware.availablePageFile >> 20U),
        static_cast<std::int32_t>(hardware.totalPageFile >> 20U)
    );
}

void L2Exception::PrintServerInfo(void* file) {
    // Current L2NPC.exe 0x0042df6c and Cached.exe 0x004cc27c.
    hprintf(file, L"[Server Info]\r\n");
    hprintf(file, L"\tCountry\t\t\t: %s [%d]\r\n", GetCountryName(), g_nWorldId);

    std::array<wchar_t, 0x104> binaryPath{};
#if defined(_WIN32)
    if (::GetModuleFileNameW(nullptr, binaryPath.data(), 0x104) == 0) {
        ::wcscpy_s(binaryPath.data(), binaryPath.size(), L"Unknown Module");
    }
#else
    const std::wstring hostBinaryPath = Utf8ToWide(ExecutablePath());
    CopyExceptionPath(binaryPath.data(), hostBinaryPath, binaryPath.size());
#endif
    hprintf(file, L"\tBinary Path\t\t: %s\r\n", binaryPath.data());

    std::array<wchar_t, 200> account{};
#if defined(_WIN32)
    DWORD accountLength = 200;
    if (!::GetUserNameW(account.data(), &accountLength)) {
        ::wcscpy_s(account.data(), account.size(), L"Unknown User");
    }
#else
    struct passwd password{};
    struct passwd* passwordResult = nullptr;
    std::array<char, 4096> passwordBuffer{};
    const bool accountRead =
        ::getpwuid_r(::geteuid(), &password, passwordBuffer.data(), passwordBuffer.size(), &passwordResult) == 0 &&
        passwordResult && passwordResult->pw_name;
    const std::wstring hostAccount = accountRead ? Utf8ToWide(passwordResult->pw_name) : std::wstring{};
    if (!accountRead || hostAccount.size() >= account.size()) {
        CopyExceptionPath(account.data(), L"Unknown User", account.size());
    } else {
        CopyExceptionPath(account.data(), hostAccount, account.size());
    }
#endif
    hprintf(file, L"\tProcess Account\t: %s\r\n", account.data());
    hprintf(file, L"\tL2Protocol\t\t: %d\r\n", GetL2ProtocolVersion());

#if defined(_WIN32)
    const wchar_t* const contentsVersion = reinterpret_cast<const wchar_t*>(g_BinaryInfo.updateVersion);
#else
    const std::wstring contentsVersionStorage = FixedUtf16ToWide(g_BinaryInfo.updateVersion);
    const wchar_t* const contentsVersion = contentsVersionStorage.c_str();
#endif
    hprintf(file, L"\tContents Version\t: %s\r\n", contentsVersion);

    hprintf(file, L"\tBuild Date\t\t: %s\r\n", CBuildDate::inst().BuildDateTime());
    hprintf(file, L"\tBuild Archive\t\t: %s\r\n", CBuildDate::inst().BuildArchive());
    hprintf(file, L"\tBuild Compiler\t: %d (Version) \r\n", 0x098a051b);
    hprintf(file, L"\tCurrent Thread ID\t: %d\r\n", static_cast<std::int32_t>(CurrentNativeThreadId()));
    hprintf(file, L"\tServer Up Time\t: %s\r\n", GetServerUpTimeString());
    hprintf(file, L"\tCurrent    Time\t: %s\r\n", GetCurrentTimeString());
    wchar_t* const elapsed = GetElapsedTimeString();
    hprintf(file, L"\tElapsed    Time\t: %s\r\n", elapsed);
    std::free(elapsed);
}

void L2Exception::GenerateExceptionReportFront(void* file) {
    // Current L2NPC.exe 0x0042e1c4 and Cached.exe 0x004cc4f0. The two final
    // subsystems do not emit sections in either executable. The targets log
    // their phases and write only the same separating CRLF.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate crash summary")
    // Noise: per-section progress floods the last useful records during a crash.
    // the final report outcome identifies success or failure.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate crash summary")
    PrintCrashSummary(file);
    hprintf(file, L"\r\n");

    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate hardware status")
    // Noise: per-section crash-report progress. See the final outcome record.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate hardware status")
    PrintHardwareStatus(file);
    hprintf(file, L"\r\n");

    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate server info")
    // Noise: per-section crash-report progress. See the final outcome record.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate server info")
    PrintServerInfo(file);
    hprintf(file, L"\r\n");

    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate io buffer")
    // Noise: per-section crash-report progress. See the final outcome record.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate io buffer")
    PrintIOBuffer(file);
    hprintf(file, L"\r\n");

    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate object report")
    // Noise: per-section crash-report progress. See the final outcome record.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate object report")
    PrintObjectReport(file);
    hprintf(file, L"\r\n");

    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate tlm")
    // Noise: this product writes no TLM section here. Only the separator follows.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate tlm")
    hprintf(file, L"\r\n");

    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate connection log")
    // Noise: this product writes no connection section here. Only the separator follows.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate connection log")
    hprintf(file, L"\r\n");
}

void L2Exception::GenerateExceptionReportBack(void* file) {
    // Current L2NPC.exe 0x0042d670 and Cached.exe 0x004cae7c. Only L2NPC's
    // larger body emits script-context frames. CacheD proceeds directly from
    // thread suspension to its process-specific guard-thread roster.
    SuspendThreadExceptMe();

#if defined(L2_BUILD_L2NPC)
    int reportThreadId = CThreadManager::GetThreadID();
    if (Singleton<CIODeadlockChecker>::inst().m_DeadlockCrashInfo.ForcedCrashByDeadlockChecker()) {
        reportThreadId = Singleton<CIODeadlockChecker>::inst().m_DeadlockCrashInfo.GetThreadID();
    }

    bool printedContext = false;
    for (std::size_t index = 0; index < 1000; ++index) {
        const ScriptContext& context = g_ScriptContext[reportThreadId][index];
        if (!context.bSet) {
            break;
        }
        if (index == 0) {
            hprintf(
                file,
                L"\r\nscript context[%s]"
                L"[Recursive Count : %d]"
                L"[Last Error : %d]\r\n",
                g_ScriptNPC[reportThreadId], g_RunScriptCallStatus[reportThreadId].nRecursiveCount,
                g_RunScriptCallStatus[reportThreadId].nLastErrno
            );
            printedContext = true;
        }
        for (std::size_t depth = 0; depth < index; ++depth) {
            hprintf(file, L"    ");
        }
        hprintf(
            file,
            L"%s [Event Name : %s]"
            L"[Event ID : %d][PC : %d]"
            L"[SP: %d][OPTR: %d]"
            L"[Func Name : %s]\r\n",
            context.pszHandlerName, context.pszEventName, context.nEventID, context.nPC, context.nSP,
            static_cast<int>(context.optr), context.pszFuncName
        );
    }
    if (printedContext) {
        // The target passes its process-wide empty wide string here.
        hprintf(file, L"");
    }
#endif

    PrintGuardInfo(file);
    hprintf(file, L"\r\n");
}

void L2Exception::MakeFileNameWithModuleName(wchar_t szFileFullName[], const wchar_t* pPostFix) {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    // Current EXE replaces only the module basename after the final
    // backslash. Its PDB-local `fileName` copy is kept even though the
    // optimized result is not later consumed.
    const DWORD length = ::GetModuleFileNameW(nullptr, szFileFullName, static_cast<DWORD>(kTargetModulePathQueryCount));
    if (length == 0) {
        szFileFullName[0] = L'\0';
    }
    wchar_t* basename = std::wcsrchr(szFileFullName, L'\\');
    basename = basename ? basename + 1 : szFileFullName;

    std::array<wchar_t, kExceptionPathCapacity> fileName{};
    CopyExceptionPath(fileName.data(), L"Unknown");
    CopyExceptionPath(fileName.data(), basename);
    if (wchar_t* const extension = std::wcsrchr(fileName.data(), L'.')) {
        *extension = L'\0';
    }

    // lstrcpyW is unbounded and does not accept a null
    // postfix. Both properties are visible in the shipping body.
    ::lstrcpyW(basename, pPostFix);
#else
    std::wstring modulePath = Utf8ToWide(ExecutablePath());
    const std::size_t separator = modulePath.find_last_of(L'/');
    if (separator == std::wstring::npos) {
        modulePath.clear();
    } else {
        modulePath.resize(separator + 1);
    }
    modulePath.append(pPostFix);
    CopyExceptionPath(szFileFullName, modulePath);
#endif
}

void* L2Exception::CreateFileWithModuleName(wchar_t szFileFullName[], wchar_t* pPostFix) {
    L2_GUARD_FUNCTION();

    MakeFileNameWithModuleName(szFileFullName, pPostFix);
    CopyExceptionPath(g_szCrashLogPath, szFileFullName);

#if defined(_WIN32)
    return ::CreateFileW(
        szFileFullName, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr
    );
#else
    if (!szFileFullName) {
        return InvalidFileHandle();
    }
    const std::string nativePath = WideToUtf8(szFileFullName);
    std::FILE* file = std::fopen(nativePath.c_str(), "wb");
    return file ? static_cast<void*>(file) : InvalidFileHandle();
#endif
}

#endif // !defined(L2_BUILD_L2SERVER)
#if defined(L2_BUILD_L2SERVER)
void L2Exception::CreateMiniDumpFile(_EXCEPTION_POINTERS* ep, int nType) {
    L2_GUARD_FUNCTION();

    // Current L2Server.exe 0x0072808c accepts nType but always requests
    // MiniDumpNormal. The exception pointers remain part of the target's
    // MINIDUMP_EXCEPTION_INFORMATION record.
    (void)nType;

    const std::int32_t now = runtime::crt::Time32Now();
    std::tm* local = nullptr;
#if defined(_MSC_VER)
    local = ::_localtime32(reinterpret_cast<const __time32_t*>(&now));
#else
    const std::time_t hostNow = static_cast<std::time_t>(now);
    local = std::localtime(&hostNow);
#endif

    std::array<wchar_t, 0x104> fileName{};
    std::swprintf(
        fileName.data(), fileName.size(), L"minidump_%d%02d%02d_%02d%02d%02d.dmp", local->tm_year + 1900, local->tm_mon + 1,
        local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec
    );

    std::array<wchar_t, 0x104> path{};
    std::wmemcpy(path.data(), L"Unknown", std::size(L"Unknown"));
    void* const file = CreateFileWithModuleName(path.data(), fileName.data());

#if defined(_WIN32)
    HANDLE const handle = static_cast<HANDLE>(file);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION information{};
    information.ThreadId = ::GetCurrentThreadId();
    information.ExceptionPointers = ep;
    information.ClientPointers = TRUE;

    ::MiniDumpWriteDump(
        ::GetCurrentProcess(), ::GetCurrentProcessId(), handle, MiniDumpNormal, &information, nullptr, nullptr
    );
    ::CloseHandle(handle);
#else
    if (file == reinterpret_cast<void*>(static_cast<std::intptr_t>(-1))) {
        return;
    }

    // DbgHelp MINIDUMP is a Windows boundary. Emit the native instruction
    // stack into the same target-named file without introducing a separate
    // crash-report protocol.
    (void)ep;
#if defined(__APPLE__) || defined(__unix__)
    std::array<void*, 128> frames{};
    const int frameCount = ::backtrace(frames.data(), static_cast<int>(frames.size()));
    std::fflush(static_cast<std::FILE*>(file));
    ::backtrace_symbols_fd(frames.data(), frameCount, ::fileno(static_cast<std::FILE*>(file)));
#endif
    std::fclose(static_cast<std::FILE*>(file));
#endif
}

#endif // defined(L2_BUILD_L2SERVER)
#if !defined(L2_BUILD_L2SERVER)
void L2Exception::CreateMiniDumpFile(_EXCEPTION_POINTERS* ep, int nType) {
    L2_GUARD_FUNCTION();

    // The current executable accepts this configuration value but emits
    // MiniDumpNormal unconditionally.

    const std::int32_t now = runtime::crt::Time32Now();
    std::tm* const local = runtime::crt::LocalTime32(now);
    std::array<wchar_t, kExceptionPathCapacity> fileName;
    std::swprintf(
        fileName.data(), fileName.size(), L"minidump_%d%02d%02d_%02d%02d%02d.dmp", local->tm_year + 1900, local->tm_mon + 1,
        local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec
    );

    std::array<wchar_t, kExceptionPathCapacity> path{};
    CopyExceptionPath(path.data(), L"Unknown");
    void* const file = CreateFileWithModuleName(path.data(), fileName.data());

#if defined(_WIN32)
    HANDLE const handle = static_cast<HANDLE>(file);
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION information{};
    information.ThreadId = ::GetCurrentThreadId();
    information.ExceptionPointers = ep;
    information.ClientPointers = TRUE;

    ::MiniDumpWriteDump(
        ::GetCurrentProcess(), ::GetCurrentProcessId(), handle, MiniDumpNormal, &information, nullptr, nullptr
    );
    ::CloseHandle(handle);
#else
    if (file == InvalidFileHandle()) {
        return;
    }

    // DbgHelp MINIDUMP is unavailable on POSIX. Keep the target
    // filename/lifecycle while recording a native backtrace at the same
    // external boundary.
    std::FILE* const output = static_cast<std::FILE*>(file);
    // std::fprintf(
    //     output,
    //     "L2NPC portable crash report\n"
    //     "timestamp=%04d-%02d-%02d %02d:%02d:%02d\n"
    //     "exception_pointers=%p\n"
    //     "minidump_type=%d (shipping image emits MiniDumpNormal)\n"
    //     "errno=%d\n"
    //     "backtrace:\n",
    //     local->tm_year + 1900,
    //     local->tm_mon + 1,
    //     local->tm_mday,
    //     local->tm_hour,
    //     local->tm_min,
    //     local->tm_sec,
    //     static_cast<void*>(ep),
    //     nType,
    //     errno)
#if defined(L2_BUILD_CACHED)
    constexpr const char* reportProduct = "CacheD";
#else
    constexpr const char* reportProduct = "L2NPC";
#endif
    const int headerWritten = std::fprintf(
        output,
        "%s portable crash report\n"
        "timestamp=%04d-%02d-%02d %02d:%02d:%02d\n"
        "exception_pointers=%p\n"
        "minidump_type=%d (shipping image emits MiniDumpNormal)\n"
        "errno=%d\n"
        "backtrace:\n",
        reportProduct, local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, local->tm_hour, local->tm_min,
        local->tm_sec, static_cast<void*>(ep), nType, errno
    );
    // std::fflush(output)
    const int headerFlushed = std::fflush(output);
    if (headerWritten < 0 || headerFlushed != 0) {
        const int savedErrno = errno;
        constexpr char warning[] =
            "ERROR [Exception.cpp] Cannot write or flush the native crash-report header; report output may be incomplete.\n";
        // Avoid re-entering CLog or taking a CRT stream lock in the fallback.
        (void)::write(STDERR_FILENO, warning, sizeof(warning) - 1);
        errno = savedErrno;
    }

#if defined(__APPLE__) || defined(__unix__)
    std::array<void*, 128> frames{};
    const int frameCount = ::backtrace(frames.data(), static_cast<int>(frames.size()));
    ::backtrace_symbols_fd(frames.data(), frameCount, ::fileno(output));
#endif
    // std::fclose(output)
    if (std::fclose(output) != 0) {
        const int savedErrno = errno;
        constexpr char warning[] =
            "ERROR [Exception.cpp] Cannot close or flush the native crash report; report completion is unconfirmed.\n";
        (void)::write(STDERR_FILENO, warning, sizeof(warning) - 1);
        errno = savedErrno;
    }
#endif
}

void L2Exception::GenerateExceptionReport(_EXCEPTION_POINTERS* exceptionPointers) {
    // Exact PDB name. Current L2NPC.exe 0x0042e2f0 and Cached.exe
    // 0x004cc61c.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"Creating Minidump file")
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"Creating Minidump file")
    L2_LOG(
        Log, LOG_LOCAL, u"[crash report] Starting diagnostics; minidump enabled: %s.",
        g_bExceptionMakeMiniDump ? L"yes" : L"no"
    );
    if (g_bExceptionMakeMiniDump) {
        CreateMiniDumpFile(exceptionPointers, g_nExceptionMiniDumpType);
    }

    // Log.Add(
    //     LOG_LOCAL,
    //     L"Path completed [%s]",
    //     g_szLinErrorPath)
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"Path completed [%s]",
    //     g_szLinErrorPath)
    L2_LOG(Log, LOG_LOCAL, u"[crash report] Writing diagnostics to \"%s\".", g_szLinErrorPath);

    void* file = nullptr;
#if defined(_WIN32)
    file = ::CreateFileW(
        g_szLinErrorPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr
    );
    const bool open = static_cast<HANDLE>(file) != INVALID_HANDLE_VALUE;
    const unsigned error = open ? 0U : ::GetLastError();
#else
    const std::string nativePath = WideToUtf8(g_szLinErrorPath);
    file = std::fopen(nativePath.c_str(), "wb");
    const bool open = file != nullptr;
    const unsigned error = open ? 0U : static_cast<unsigned>(errno);
#endif

    if (!open) {
        // Log.Add(
        //     LOG_LOCAL,
        //     L"Error handle GenerateExceptionReport "
        //     L"[%s] error code [%d]",
        //     g_szLinErrorPath,
        //     error)
        // Previous: L2_LOG(Log,
        //     LOG_LOCAL,
        //     L"Error handle GenerateExceptionReport "
        //     L"[%s] error code [%d]",
        //     g_szLinErrorPath,
        //     error)
        L2_LOG(
            Log, LOG_LOCAL, u"[crash report] Cannot open \"%s\" for writing (OS error %u); report generation stopped.",
            g_szLinErrorPath, error
        );
        return;
    }

#if defined(_WIN32)
    ::SetEndOfFile(static_cast<HANDLE>(file));
#endif
    // Log.Add(
    //     LOG_LOCAL,
    //     L"trying exception")
    // Noise: internal report-stage marker. The start and final outcome are enough.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"trying exception")
    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate front")
    // Noise: internal report-stage marker. The final outcome identifies completion.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate front")
    GenerateExceptionReportFront(file);
    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate body")
    // Noise: internal report-stage marker. The final outcome identifies completion.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate body")
    GenerateExceptionReportBody(file, exceptionPointers);
    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate back")
    // Noise: internal report-stage marker. The final outcome identifies completion.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate back")
    GenerateExceptionReportBack(file);
    WriteStackWalk(file, exceptionPointers);
    hprintf(file, L"\r\n\r\n");

#if defined(_WIN32)
    const bool closed = ::CloseHandle(static_cast<HANDLE>(file)) != FALSE;
#else
    const bool closed = std::fclose(static_cast<std::FILE*>(file)) == 0;
#endif
    // Log.Add(
    //     LOG_LOCAL,
    //     closed
    //     ? L"finaly close success"
    //     : L"finaly close failure")
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     closed
    //     ? L"finaly close success"
    //     : L"finaly close failure")
    L2_LOG(
        Log, LOG_LOCAL,
        closed ? L"[crash report] Diagnostics saved to \"%s\"."
               : L"[crash report] Diagnostics were written to \"%s\", but closing the file failed.",
        g_szLinErrorPath
    );
}

CExceptionInit::CExceptionInit() {
    // Exact PDB name. Current L2NPC.exe 0x0042e598 and the instruction-
    // identical Cached.exe owner at 0x004cc8c4.
#if defined(_WIN32)
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    ::GetVersionExW(&version);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (version.dwMajorVersion >= 6 && version.dwMinorVersion > 0) {
        // ::AddVectoredExceptionHandler(1, &RecordExceptionInfo)
        ::AddVectoredExceptionHandler(1, &VectoredExceptionInfo);
    }
    previousFilter = ::SetUnhandledExceptionFilter(&RecordExceptionInfo);
    std::signal(SIGABRT, &WindowsAbortHandler);

    PreventSetUnhandledExceptionFilter();
#endif
}

bool ExceptionMailing() {
    // Current L2NPC.exe 0x00516b64 is exactly `mov al, 1` followed by `ret`. It does not
    // read the nearby-PDB global g_bExceptionMailing in this build.
    return true;
}

void ELOG(const wchar_t* format, ...) {
    // Exact PDB name. Current L2NPC.exe 0x0042cf54 and Cached.exe
    // 0x004cb1f8.
    if (ExceptionTerminating != 0) {
        return;
    }

    CLogCritical theCritical;

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
#if defined(_WIN32)
    SYSTEMTIME local{};
    ::GetLocalTime(&local);
    year = local.wYear;
    month = local.wMonth;
    day = local.wDay;
    hour = local.wHour;
    minute = local.wMinute;
    second = local.wSecond;
#else
    const std::time_t now = static_cast<std::time_t>(runtime::crt::Time32Now());
    const std::tm local = runtime::crt::LocalTimeOrZero(now);
    year = local.tm_year + 1900;
    month = local.tm_mon + 1;
    day = local.tm_mday;
    hour = local.tm_hour;
    minute = local.tm_min;
    second = local.tm_sec;
#endif

    void* file = nullptr;
#if defined(_WIN32)
    file = ::CreateFileW(
        g_szLinErrorPath, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (static_cast<HANDLE>(file) == INVALID_HANDLE_VALUE) {
        file = nullptr;
    } else {
        // ::SetFilePointer(
        //     static_cast<HANDLE>(file),
        //     0,
        //     nullptr,
        //     FILE_END)
        ::SetLastError(NO_ERROR);
        const DWORD position = ::SetFilePointer(static_cast<HANDLE>(file), 0, nullptr, FILE_END);
        const DWORD seekError = ::GetLastError();
        if (position == INVALID_SET_FILE_POINTER && seekError != NO_ERROR) {
            L2_LOG_DIRECT(
                stderr, runtime::logging::Level::Error,
                "[logging] Cannot seek to the end of ELOG (Win32 error %lu). Writing continues at the current position and "
                "may overwrite existing text.",
                static_cast<unsigned long>(seekError)
            );
        }
    }
#else
    file = std::fopen(WideToUtf8(g_szLinErrorPath).c_str(), "ab");
#endif

    if (!file) {
        const runtime::logging::StorageErrorState errors;
#if defined(_WIN32)
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Error,
            "[logging] Cannot open ELOG file (Win32 error %lu); this exception record was not saved.",
            static_cast<unsigned long>(errors.savedWindowsError)
        );
#else
        L2_LOG_DIRECT(
            stderr, runtime::logging::Level::Error,
            "[logging] Cannot open ELOG file (errno %d); this exception record was not saved.", errors.savedErrno
        );
#endif
    }
    if (file) {
        ElogFileUnwindGuard fileOnException(file);
        std::array<wchar_t, 0x5000> hostMessage;
        int prefixLength = 0;
#if defined(_MSC_VER)
        prefixLength = ::_snwprintf_s(
            hostMessage.data(), hostMessage.size(), hostMessage.size() - 1,
            L"[(%d) %04d/%02d/%02d "
            L"%02d:%02d:%02d]: ",
            static_cast<int>(CurrentNativeThreadId()), year, month, day, hour, minute, second
        );
#else
        prefixLength = std::swprintf(
            hostMessage.data(), hostMessage.size(),
            L"[(%d) %04d/%02d/%02d "
            L"%02d:%02d:%02d]: ",
            static_cast<int>(CurrentNativeThreadId()), year, month, day, hour, minute, second
        );
#endif

        if (prefixLength < 0) {
            L2_LOG_DIRECT(
                stderr, runtime::logging::Level::Error,
                "[logging] Cannot format the ELOG header; this exception record was not written."
            );
        }
        if (prefixLength >= 0) {
            WriteElogHostWide(file, hostMessage.data());

#if !defined(_MSC_VER)
            // Translate before va_start: allocation failure must not bypass va_end.
            const std::wstring translated = WindowsWideFormatToHost(format);
#endif
            va_list arguments;
            va_start(arguments, format);
#if defined(_MSC_VER)
            std::int32_t length =
                ::_vsnwprintf_s(hostMessage.data(), hostMessage.size(), hostMessage.size() - 1, format, arguments);
            va_end(arguments);
            if (length >= 0) {
                auto* const targetMessage = reinterpret_cast<char16_t*>(hostMessage.data());
                InsertElogCarriageReturns(targetMessage, length);
                WriteElogTargetWide(file, targetMessage);
            } else {
                L2_LOG_DIRECT(
                    stderr, runtime::logging::Level::Error,
                    "[logging] Cannot format ELOG text within %zu wide characters; the body was not written.",
                    hostMessage.size()
                );
            }
#else
            // const std::wstring translated =
            //     WindowsWideFormatToHost(
            //         format)
            const int hostLength = std::vswprintf(hostMessage.data(), hostMessage.size(), translated.c_str(), arguments);
            va_end(arguments);
            if (hostLength >= 0) {
                const std::u16string converted =
                    WideToUtf16(std::wstring_view(hostMessage.data(), static_cast<std::size_t>(hostLength)));
                if (converted.size() < hostMessage.size()) {
                    std::array<char16_t, 0x5000> targetMessage;
                    std::copy(converted.begin(), converted.end(), targetMessage.begin());
                    targetMessage[converted.size()] = u'\0';
                    std::int32_t length = static_cast<std::int32_t>(converted.size());
                    InsertElogCarriageReturns(targetMessage.data(), length);
                    WriteElogTargetWide(file, targetMessage.data());
                } else {
                    L2_LOG_DIRECT(
                        stderr, runtime::logging::Level::Error,
                        "[logging] ELOG text needs %zu UTF-16 units (capacity %zu); the body was not written.",
                        converted.size() + 1, hostMessage.size()
                    );
                }
            } else {
                L2_LOG_DIRECT(
                    stderr, runtime::logging::Level::Error,
                    "[logging] Cannot format ELOG text within %zu wide characters; the body was not written.",
                    hostMessage.size()
                );
            }
#endif
        }

        fileOnException.Release();
#if defined(_WIN32)
        // ::CloseHandle(
        //     static_cast<HANDLE>(
        //         file))
        if (!::CloseHandle(static_cast<HANDLE>(file))) {
            L2_LOG_DIRECT(
                stderr, runtime::logging::Level::Error, "[logging] Cannot close ELOG file (Win32 error %lu).",
                static_cast<unsigned long>(::GetLastError())
            );
        }
#else
        // std::fclose(
        //     static_cast<std::FILE*>(
        //         file))
        if (std::fclose(static_cast<std::FILE*>(file)) != 0) {
            L2_LOG_DIRECT(stderr, runtime::logging::Level::Error, "[logging] Cannot close ELOG file (errno %d).", errno);
        }
#endif
    }
}

ExceptionFilterResult RecordExceptionInfo(_EXCEPTION_POINTERS* exceptionPointers) {
    // Exact PDB name. Current L2NPC.exe 0x0042e484 and instruction-identical
    // Cached.exe owner 0x004cc7b0.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"[%s][%d] RecordExceptionInfo called",
    //     L"..\\Shared\\Exception.cpp",
    //     0x813)
#if defined(_WIN32)
    // Noise: OutputDebugStringA raises 0x40010006 and it is not a failure.
    // The original also logged "RecordExceptionInfo called" for it. Log the
    // debug text and its module instead, then continue as before.
    if (exceptionPointers->ExceptionRecord->ExceptionCode == 0x40010006U) {
        LogDebugOutputException(*exceptionPointers->ExceptionRecord);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
#endif
    L2_LOG(Log, LOG_LOCAL, u"[%s][%d] RecordExceptionInfo called", __WFILE__, __LINE__);

#if defined(_WIN32)
    const std::uint32_t exceptionCode = exceptionPointers->ExceptionRecord->ExceptionCode;

    static int BeenHere = 0;
    if (BeenHere != 0) {
        return previousFilter ? previousFilter(exceptionPointers) : EXCEPTION_CONTINUE_SEARCH;
    }
    BeenHere = 1;

    if (ExceptionTerminating == 0 && exceptionCode != 0xe0000001U) {
        L2Exception::GenerateExceptionReport(exceptionPointers);
        SystemMail::sendCrashMail(mailServer, g_bExceptionMakeMiniDump ? g_szCrashLogPath : nullptr);
    } else {
        // Log.Add(
        //     LOG_LOCAL,
        //     L"[%s][%d] RecordExceptionInfo "
        //     L"failed. Exception code [%d]",
        //     L"..\\Shared\\Exception.cpp",
        //     0x83e,
        //     static_cast<int>(
        //         exceptionCode))
        L2_LOG(
            Log, LOG_LOCAL,
            u"[%s][%d] RecordExceptionInfo "
            u"failed. Exception code [%d]",
            __WFILE__, __LINE__, static_cast<int>(exceptionCode)
        );
        if (ExceptionTerminating != 0) {
            // Log.Add(
            //     LOG_LOCAL,
            //     L"[%s][%d] "
            //     L"ExceptionTerminating is true.",
            //     L"..\\Shared\\Exception.cpp",
            //     0x842,
            //     static_cast<int>(
            //         exceptionCode))
            L2_LOG(
                Log, LOG_LOCAL,
                u"[%s][%d] "
                u"ExceptionTerminating is true.",
                __WFILE__, __LINE__, static_cast<int>(exceptionCode)
            );
        }
    }
    ::ExitProcess(0);
    return EXCEPTION_CONTINUE_SEARCH;
#else
    return 0;
#endif
}

int exception_filter(_EXCEPTION_POINTERS* exceptionPointers) {
    // Exact PDB name. Current L2NPC.exe 0x0042e608.
    CLogCritical theCritical;
    if (exceptionPointers) {
        L2Exception::GenerateExceptionReport(exceptionPointers);
    }
    return 1;
}

void violation() {
    // Exact PDB name and current L2NPC.exe 0x0042e678 / Cached.exe 0x004cc9c4.
    // The Windows bodies fault inside their local SEH scope.
    if (!xfirst) {
        return;
    }
    xfirst = false;

#if defined(_MSC_VER)
    __try {
        ELOG(L"Intentional Exception\n");
        CrashImpl();
    } __except (exception_filter(GetExceptionInformation())) {
        ELOG(L" %S:%d(%S) exception\n", L2_SOURCE_FILE, __LINE__, __TIMESTAMP__);
        send_exception(false);
    }
#else
    // Windows SEH has no portable counterpart. Preserve its observable
    // sequence without turning this caught diagnostic into a fatal POSIX
    // signal: write the intentional-fault marker, create the same report,
    // append the target's handler marker, and send without exiting.
    ELOG(L"Intentional Exception\n");
    L2Exception::GenerateExceptionReport(nullptr);
    ELOG(L" %S:%d(%S) exception\n", L2_SOURCE_FILE, __LINE__, __TIMESTAMP__);
    send_exception(false);
#endif
}

void myInvalidParameterHandler(
    const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned line, std::uint64_t reserved
) {
    // Exact PDB name. Current L2NPC.exe 0x0042c390 and Cached.exe 0x004ca640
    // ignore every diagnostic argument except expression, record the Shared
    // source site, then cross the intentional-fault boundary.

    g_sInvalidParamExpr = expression;
    g_bInvalidParameter = true;
    RecordCrashLocation();

    g_CrashInfo.SetReason(CrashInfo::INVALID_PARAMETER_ERROR);
    CrashImpl();
    bForceCrash = true;
}

void send_exception(bool exitProcess) {
    // Exact PDB name. Current L2NPC.exe 0x0042c400 and Cached.exe
    // 0x004ca6b0.
    if (ExceptionTerminating != 0) {
        return;
    }
    // Portable addition. A catch block that stops the server has no exception
    // pointers for a report. On Windows, report the active C++ exception first.
    if (exitProcess) {
        ReportCaughtExceptionAsCrash();
    }

    {
        CLogCritical theCritical;
        SystemMail::sendCrashMail(mailServer, nullptr);
    }

    if (exitProcess) {
        ExceptionTerminating = 1;
#if defined(_WIN32)
        ::ExitProcess(0);
#else
        std::_Exit(0);
#endif
    }
}

namespace {

void PrintThreadInfo(void* LogFile, int nThreadID, const wchar_t* pszThreadName) {
    if (nThreadID < ThreadID::MAX) {
        const std::uint32_t startTick = g_dwGuardStartTick[nThreadID];
        const wchar_t* const state = nThreadID == CThreadManager::GetThreadID() ? L"ahehe" : L"good";
        const std::uint32_t elapsed = runtime::GetTickCount() - startTick;
        hprintf(
            LogFile, L"\t%s\t[%02d][start: %d][%u ms] (%s):  ", pszThreadName, nThreadID, static_cast<int>(startTick),
            elapsed, state
        );
    }
}

void PrintSkillInfo(void* LogFile, int nThreadID) {
    if (nThreadID >= 0 && nThreadID < ThreadID::MAX) {
        hprintf(
            LogFile,
            L"\tSkillInfo actor dbid:%u, skill_id:%d "
            L"target_cnt:%d\r\n",
            static_cast<unsigned>(g_nSkillActorDBID[nThreadID]), g_nSkillID[nThreadID], g_nSkillTargetCount[nThreadID]
        );
    }
}

void PrintPostInfo(void* LogFile, int nThreadID) {
    if (nThreadID >= 0 && nThreadID < ThreadID::MAX) {
        const int postCount = g_nLeaveWorldPostCount[nThreadID];
        hprintf(LogFile, L"\tPostInfo count: %d post dbids: ", postCount);
        for (int index = 0; index < postCount && index < 32; ++index) {
            hprintf(LogFile, L"%d ", g_nLeaveWorldPostDBID[nThreadID][index]);
        }
        hprintf(LogFile, L"\r\n");
    }
}

void PrintLockStack(void* LogFile, int nThreadID) {
    hprintf(LogFile, L"\t\tLock Stack : ");
    const CLockStatusStack& stack = g_LockStack[nThreadID];
    for (int index = 0; index < stack.mt_nStackTop; ++index) {
        const auto& node = stack.mt_Stack[index];
        hprintf(
            LogFile,
            L"%s(line[%d], lock object pointer "
            L"[0x%I64x] Entered:%d)",
            node.desc, static_cast<int>(node.line), static_cast<unsigned long long>(node.addr),
            static_cast<int>(node.entered)
        );
        if (index != stack.mt_nStackTop - 1) {
            hprintf(LogFile, L" -> ");
        }
    }
    hprintf(LogFile, L"\r\n");
}

void PrintCallStack(void* LogFile, int nThreadID, const wchar_t* pszThreadName) {
    // The four helpers are corroborated by the shared source donor and the
    // adjacent PDB's seven-line call-site map. They are fully inlined into
    // current L2NPC.exe 0x0042b700. Cached.exe 0x004c99e4 adds its live
    // DroidNPC character-map loop counter between PostInfo and guard frames.
    PrintThreadInfo(LogFile, nThreadID, pszThreadName);
    PrintSkillInfo(LogFile, nThreadID);
    PrintPostInfo(LogFile, nThreadID);
#if defined(L2_BUILD_CACHED)
    if (nThreadID >= 0 && nThreadID < ThreadID::MAX) {
        hprintf(LogFile, L"\tDroidNPC CharMap Loop[%d]", g_nDroidNPCCharMapLoop[nThreadID]);
        hprintf(LogFile, L"\r\n");
    }
#endif
    GuardHelper::printCallStackWithElapsedTime(LogFile, nThreadID);
    PrintLockStack(LogFile, nThreadID);
#if defined(L2_BUILD_L2NPC)
    hprintf(LogFile, L"NPC Class ID [%d] EventID [%d] \r\n", g_NPCClassID[nThreadID][0], g_NPCClassID[nThreadID][1]);
#endif
    hprintf(LogFile, L"\r\n");
}

void PrintGuardInfo(void* LogFile) {
    // Corroborated source helper. It is fully inlined into current L2NPC.exe
    // 0x0042d670 and Cached.exe 0x004cae7c. The two products have distinct
    // fixed-role suffixes after their live IO-thread traversal.
    hprintf(LogFile, L"[Guard Info]\r\n");
    for (int threadId = 0; threadId < CThreadManager::GetThreadCount(); ++threadId) {
        PrintCallStack(LogFile, threadId, L"IOThread");
    }
#if defined(L2_BUILD_CACHED)
    PrintCallStack(LogFile, ThreadID::FILE_SERVER, L"FileServerThread");
    PrintCallStack(LogFile, ThreadID::GIP, L"GIPThread");
#endif
    PrintCallStack(LogFile, ThreadID::LOG, L"LogThread");
    PrintCallStack(LogFile, ThreadID::PERFMON, L"PerfMonThread");
    PrintCallStack(LogFile, ThreadID::MAIN, L"MainThread");
    PrintCallStack(LogFile, ThreadID::LISTEN, L"ListenThread");
}

} // namespace

void ReportCallStack(int threadId, LogType logType, bool sendMail) {
    // // Exact current L2NPC.exe 0x0042bff4 and Cached.exe 0x004ca28c.
    // if (threadId < ThreadID::MAX) {
    //     static thread_local int totalCall = 0.
    //     ++totalCall.
    //
    //     int order = 0
    //     const int depth =
    //         GuardHelper::getCallStackDepth(threadId)
    //     Log.Add(
    //         logType,
    //         L"[CallStack][tid:%d][tick:%d][%d] Begin",
    //         threadId,
    //         totalCall,
    //         0)
    //     for (int index = 0.
    //          index < depth.
    //          ++index) {
    //         ++order.
    //         Log.Add(
    //             logType,
    //             L"[CallStack][tid:%d][tick:%d][%d][%d] %s",
    //             threadId,
    //             totalCall,
    //             order,
    //             index,
    //             GuardHelper::getCallStack(
    //                 threadId,
    //                 index))
    //     }
    //     Log.Add(
    //         logType,
    //         L"[CallStack][tid:%d][tick:%d][%d] End",
    //         threadId,
    //         totalCall,
    //         order + 1)
    //
    //     if (sendMail) {
    //         SystemMail::
    //             sendCriticalCallstack(
    //                 mailServer)
    //     }
    // }
    // Noise: the old End banner is extra with the frame count and numbered rows.
    WriteRecordedGuardStack(threadId, logType, sendMail, false);
}

void ReportCallStack(LogType lType, bool bSendMail) {
    // Exact PDB entry. Current L2NPC.exe 0x0042cf28 and Cached.exe 0x004cb1cc
    // forward the current logical thread to their three-argument workers.
    ReportCallStack(CThreadManager::GetThreadID(), lType, bSendMail);
}

void ReportCallStackNew(wchar_t* contentsName, LogType logType, bool sendMail) {
    // Exact PDB name and current L2NPC.exe 0x0042c110 / Cached.exe 0x004ca3a8.
    // Its tick counter is a distinct TLS local from the older ReportCallStack
    // overload above.
    static thread_local int totalCall = 0;
    ++totalCall;

    const int threadId = CThreadManager::GetThreadID();
    int order = 0;
    // Log.Add(
    //     logType,
    //     L"[%s][CallStack][tid:%d][tick:%d][%d] Begin",
    //     contentsName,
    //     threadId,
    //     totalCall,
    //     0)
    // Log.Add(logType,
    //     "[call stack] Named report %d for worker %d: %s.",
    //     totalCall, threadId,
    //     WideToUtf8(contentsName ? contentsName : L"unnamed context").c_str())
    L2_LOG(
        Log, logType, "[call stack] Named report %d for worker %d: %s.", totalCall, threadId,
        WideToUtf8(contentsName ? contentsName : L"unnamed context").c_str()
    );

    const int depth = GuardHelper::getCallStackDepth(threadId);
    for (int index = 0; index < depth; ++index) {
        ++order;
        // Log.Add(
        //     logType,
        //     L"[%s][CallStack][tid:%d][tick:%d]"
        //     L"[%d][%d] %s",
        //     contentsName,
        //     threadId,
        //     totalCall,
        //     order,
        //     index,
        //     GuardHelper::getCallStack(
        //         threadId,
        //         index))
        const wchar_t* const frameName = GuardHelper::getCallStack(threadId, index);
        // Log.Add(logType,
        //     "[call stack] Named report %d, worker %d, frame %d/%d: %s",
        //     totalCall, threadId, order, depth,
        //     WideToUtf8(frameName ? frameName : L"unknown frame").c_str())
        L2_LOG(
            Log, logType, "[call stack] Named report %d, worker %d, frame %d/%d: %s", totalCall, threadId, order, depth,
            WideToUtf8(frameName ? frameName : L"unknown frame").c_str()
        );
    }

    // Log.Add(
    //     logType,
    //     L"[%s][CallStack][tid:%d][tick:%d][%d] End",
    //     contentsName,
    //     threadId,
    //     totalCall,
    //     order + 1)
    // Log.Add(logType,
    //     "[call stack] Named report %d for worker %d complete: %d frames.",
    //     totalCall, threadId, order)
    L2_LOG(Log, logType, "[call stack] Named report %d for worker %d complete: %d frames.", totalCall, threadId, order);
    if (sendMail) {
        SystemMail::sendCriticalCallstack(mailServer);
    }
}


void ReportScriptContext(LogType lType) {
    // Exact PDB owner and current L2NPC.exe 0x0042c530.
    const int threadId = CThreadManager::GetThreadID();
    if (threadId < 0 || threadId >= ThreadID::MAX) {
        // CLog's recursion guard also indexes by the logical worker ID.
        // Use the independent bounded console transport for an invalid ID.
        const int savedErrno = errno;
#if defined(_WIN32)
        const DWORD savedWindowsError = ::GetLastError();
#endif
        std::array<char, 160> message{};
        std::snprintf(
            message.data(), message.size(), "[script context] Invalid worker ID %d; expected 0..%d. AI context unavailable.",
            threadId, static_cast<int>(ThreadID::MAX) - 1
        );
        // runtime::logging::Write("shared", runtime::logging::Level::Error, message.data())
        runtime::logging::Write("shared", runtime::logging::Level::Error, message.data(), __FILE__);
        errno = savedErrno;
#if defined(_WIN32)
        ::SetLastError(savedWindowsError);
#endif
        return;
    }
    if (static_cast<std::size_t>(threadId) >= std::size(g_ScriptContext)) {
        // Log.Add(lType,
        //     L"[script context] No AI context storage for worker %d.",
        //     threadId)
        L2_LOG(Log, lType, u"[script context] No AI context storage for worker %d.", threadId);
        return;
    }
    // Noise: Each frame identifies the worker and depth. An empty context
    // gets an explicit message below instead of a standalone banner.
    // Log.Add(
    //     lType,
    //     L"--- script call context  ---")
    for (int index = 0; index < 1000; ++index) {
        const ScriptContext& context = g_ScriptContext[threadId][index];
        if (!context.bSet) {
            if (index == 0) {
                // Log.Add(lType,
                //     L"[script context] No active AI script on worker %d.",
                //     threadId)
                L2_LOG(Log, lType, u"[script context] No active AI script on worker %d.", threadId);
            }
            return;
        }
        // Log.Add(
        //     lType,
        //     L"[%d] %s [%s][%d][%d][%s]",
        //     index,
        //     context.pszHandlerName,
        //     context.pszEventName,
        //     context.nPC,
        //     context.nSP,
        //     context.pszFuncName)
        // Log.Add(
        //     lType,
        //     L"[script context] Worker %d, frame %d: AI \"%ls\", event \"%ls\" (ID %d), instruction %d, stack index %d,
        //     native function \"%ls\".", threadId, index + 1, context.pszHandlerName && *context.pszHandlerName ?
        //     context.pszHandlerName : L"(unknown)", context.pszEventName && *context.pszEventName ? context.pszEventName :
        //     L"(unknown)", context.nEventID, context.nPC, context.nSP, context.pszFuncName && *context.pszFuncName ?
        //     context.pszFuncName : L"(none)")
        L2_LOG(
            Log, lType,
            u"[script context] Worker %d, frame %d: AI \"%ls\", event \"%ls\" (ID %d), instruction %d, stack index %d, "
            u"native function \"%ls\".",
            threadId, index + 1, context.pszHandlerName && *context.pszHandlerName ? context.pszHandlerName : L"(unknown)",
            context.pszEventName && *context.pszEventName ? context.pszEventName : L"(unknown)", context.nEventID,
            context.nPC, context.nSP, context.pszFuncName && *context.pszFuncName ? context.pszFuncName : L"(none)"
        );
    }
}


void ReportLockStack(int threadId, const wchar_t* context, LogType logType) {
    // Current L2NPC.exe 0x0042c248 and Cached.exe 0x004ca4e0. LTCG folded
    // each caller's LOG_ERROR argument into the body, exactly as in the
    // corroborating shared source. The product-specific ThreadID::MAX keeps
    // CacheD's exact 22-entry bound.
    if (threadId >= ThreadID::MAX) {
        return;
    }

    const CLockStatusStack& stack = g_LockStack[threadId];
    const int count = std::min(CLockStatusStack::kMaximumDepth, stack.mt_nStackTop);
    for (int index = 0; index < count; ++index) {
        const CLockStatusStack::NodeInfo& node = stack.mt_Stack[index];
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][LockStack][%s][%d] "
        //     L"[0x%I64x][%d]",
        //     context,
        //     node.desc,
        //     static_cast<int>(node.line),
        //     node.addr,
        //     static_cast<int>(node.entered))
        // Log.Add(LOG_ERROR,
        //     "[lock stack] Worker %d, entry %d/%d for \"%s\": address 0x%016llx, type %u, entered %u. \"%s\", line %u.",
        //     threadId, index + 1, count,
        //     WideToUtf8(context ? context : L"unnamed context").c_str(),
        //     static_cast<unsigned long long>(node.addr), static_cast<unsigned>(node.lockType),
        //     static_cast<unsigned>(node.entered),
        //     WideToUtf8(node.desc ? node.desc : L"unknown lock site").c_str(),
        //     static_cast<unsigned>(node.line))
        L2_LOG_ERROR(
            Log, "[lock stack] Worker %d, entry %d/%d for \"%s\": address 0x%016llx, type %u, entered %u; \"%s\", line %u.",
            threadId, index + 1, count, WideToUtf8(context ? context : L"unnamed context").c_str(),
            static_cast<unsigned long long>(node.addr), static_cast<unsigned>(node.lockType),
            static_cast<unsigned>(node.entered), WideToUtf8(node.desc ? node.desc : L"unknown lock site").c_str(),
            static_cast<unsigned>(node.line)
        );
    }
}

void ReportLockStack(const wchar_t* context, LogType logType) {
    ReportLockStack(CThreadManager::GetThreadID(), context, logType);
}

void ReportGuardProfileToLog(const GuardProfileInfo& profile) {
    // Current Cached.exe 0x004ca73c.
    if (profile.nTotalCallCount <= 0) {
        return;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"\t %I64d \t %I64d \t %I64d "
    //     L"\t %I64d \t %s",
    //     profile.nTotalClock,
    //     profile.nTotalCallCount,
    //     profile.nTotalClock /
    //         profile.nTotalCallCount,
    //     profile.nAccTotalClock,
    //     profile.pszGuardName)
    // Previous: L2_LOG_INFO(Log,
    //     L"\t %I64d \t %I64d \t %I64d "
    //     L"\t %I64d \t %s",
    //     profile.nTotalClock,
    //     profile.nTotalCallCount,
    //     profile.nTotalClock / profile.nTotalCallCount,
    //     profile.nAccTotalClock,
    //     profile.pszGuardName)
    L2_LOG_INFO(
        Log,
        u"[guard profile] \"%s\": self time %I64d ns over %I64d calls (%I64d ns per call); including nested guards %I64d "
        u"ns.",
        profile.pszGuardName, profile.nTotalClock, profile.nTotalCallCount, profile.nTotalClock / profile.nTotalCallCount,
        profile.nAccTotalClock
    );
}

void ReportGuardProfile() {
    // Current Cached.exe 0x004ca790.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"\t Only in guard \t Count "
    //     L"\t only guard / count "
    //     L"\t with lower guard \t GuardName")
    // Previous: L2_LOG_INFO(Log,
    //     L"\t Self time (ns) \t Calls "
    //     L"\t Self time per call (ns) "
    //     L"\t Time including nested guards (ns) \t Guard name")
    L2_LOG_INFO(Log, u"[guard profile] Reporting self and nested execution time by guard.");
    const std::int32_t count = GuardProfileHelper::RegisteredProfileCount();
    for (std::int32_t index = 0; index < count; ++index) {
        const GuardProfileInfo profile = GuardProfileHelper::Snapshot(index);
        ReportGuardProfileToLog(profile);
    }
}
#endif

#if defined(L2_BUILD_L2SERVER)
// Closed current-L2Server Exception.cpp boundary. Broader report sections
// remain outside this product creation until their current-image
// dependencies are closed in the original owners.
void ReportLockStack(const int threadId, const wchar_t* const context, LogType) {
    // Current L2Server.exe 0x007285f8. The signed comparison deliberately
    // lets a negative index reach the original failure boundary. Valid
    // stacks are capped at the current 40000-node physical extent, and the
    // log type is folded to LOG_ERROR by the shipped body.
    if (threadId >= ThreadID::MAX) {
        return;
    }

    const CLockStatusStack& stack = g_LockStack[threadId];
    const int count = std::min(CLockStatusStack::kMaximumDepth, stack.mt_nStackTop);
    for (int index = 0; index < count; ++index) {
        const CLockStatusStack::NodeInfo& node = stack.mt_Stack[index];
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][LockStack][%s][%d] [0x%I64x][%d]",
        //     context,
        //     node.desc,
        //     static_cast<int>(node.line),
        //     node.addr,
        //     static_cast<int>(node.entered))
        // Log.Add(LOG_ERROR,
        //     "[lock stack] Worker %d, entry %d/%d for \"%s\": address 0x%016llx, type %u, entered %u. \"%s\", line %u.",
        //     threadId, index + 1, count,
        //     WideToUtf8(context ? context : L"unnamed context").c_str(),
        //     static_cast<unsigned long long>(node.addr), static_cast<unsigned>(node.lockType),
        //     static_cast<unsigned>(node.entered),
        //     WideToUtf8(node.desc ? node.desc : L"unknown lock site").c_str(),
        //     static_cast<unsigned>(node.line))
        L2_LOG_ERROR(
            Log, "[lock stack] Worker %d, entry %d/%d for \"%s\": address 0x%016llx, type %u, entered %u; \"%s\", line %u.",
            threadId, index + 1, count, WideToUtf8(context ? context : L"unnamed context").c_str(),
            static_cast<unsigned long long>(node.addr), static_cast<unsigned>(node.lockType),
            static_cast<unsigned>(node.entered), WideToUtf8(node.desc ? node.desc : L"unknown lock site").c_str(),
            static_cast<unsigned>(node.line)
        );
    }
}

void ReportLockStack(const wchar_t* const context, const LogType logType) {
    ReportLockStack(CThreadManager::GetThreadID(), context, logType);
}

// Current L2Server.exe Exception.obj storage 0x04d32620. Suspend and resume
// serialize only the live IO-thread traversal. The fixed MAIN/LISTEN roles
// remain outside this critical section in the exact target order. The
// process-global CRT sequence constructs this owner before g_CrashInfo.
CIOCriticalSection g_ThreadSuspendLock;

CrashInfo g_CrashInfo;
bool g_bInvalidParameter = false;
bool g_IsBySystemManager = false;
bool bForceCrash = false;
// Current initialized Exception.obj byte 0x015d2e5a is one. Unlike the three
// adjacent crash-state flags above, this is not BSS-zeroed.
bool bCanConnectCacheServer = true;
const wchar_t* g_sInvalidParamExpr = nullptr;
wchar_t g_CrashFunctionName[1024]{};
wchar_t g_CrashFileName[1024]{};
wchar_t g_CrashLineNo[16]{};

// Current L2Server.exe Exception.obj storage 0x04d31e60. The exception
// startup path fills this 0x104-WCHAR buffer with the module-local
// LinError.txt path. ELOG appends to that same live path.
wchar_t g_szLinErrorPath[0x104]{};

// Current L2Server.exe Exception.obj dword 0x04d26fe0. This exception-report
// termination state is independent of ioc.cpp's process exit flag.
int ExceptionTerminating = 0;

// Current L2Server.exe Exception.obj storage 0x04d26f60: one guard-entry
// timestamp for each of the 32 logical server threads.
std::uint32_t g_dwGuardStartTick[ThreadID::MAX]{};

// Current L2Server.exe Exception.obj storage 0x04d28010. The exact donor
// symbol is g_wszDumpFilename. CreateFileWithModuleName publishes its final
// path here instead of in g_szCrashLogPath.
wchar_t g_wszDumpFilename[260]{};

// Exception.obj storage at current L2Server.exe 0x04d26f50. CConfig's
// LoadSettingSection publishes its allocated 1000-byte buffer at 0x005c92dd
// before publishing the configured ANSI MailServer value.
char* mailServer = nullptr;

void send_exception(bool exitProcess) {
    // Current L2Server.exe 0x00727a50. Unlike the sibling product body, this
    // product forwards the live minidump path, then always tears down the
    // embedded GameGuard server library before the optional process exit.
    if (ExceptionTerminating != 0) {
        return;
    }
    // Portable addition. A catch block that stops the server has no exception
    // pointers for a report. On Windows, report the active C++ exception first.
    if (exitProcess) {
        ReportCaughtExceptionAsCrash();
    }

    {
        CLogCritical theCritical;
        SystemMail::sendCrashMail(mailServer, g_bExceptionMakeMiniDump ? g_wszDumpFilename : nullptr);
    }

    CleanupGameguardAuth();
    if (exitProcess) {
        ExceptionTerminating = 1;
#if defined(_WIN32)
        ::ExitProcess(0);
#else
        std::_Exit(0);
#endif
    }
}

namespace {

// Current initialized Exception.obj byte 0x015d2e5e. violation clears it
// before raising its one intentional exception.
bool xfirst = true;

#if defined(_WIN32)
// Current Exception.obj BSS pointer 0x04d26f10.
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
#else
std::atomic_flag g_portableCrashReportActive = ATOMIC_FLAG_INIT;

void GeneratePortableFatalReport() noexcept {
    try {
        L2Exception::GenerateExceptionReport(nullptr);
        SystemMail::sendCrashMail(mailServer, g_bExceptionMakeMiniDump ? g_wszDumpFilename : nullptr);
    } catch (...) {
    }
}

[[noreturn]] void PortableTerminateHandler() noexcept {
    if (!g_portableCrashReportActive.test_and_set(std::memory_order_acq_rel)) {
        GeneratePortableFatalReport();
    }
    ::_exit(EXIT_FAILURE);
}

void PortableSignalHandler(int signalNumber, siginfo_t* signalInfo, void* machineContext) {
    const int interruptedErrno = errno;
    std::uintptr_t programCounter = 0;
#if defined(__APPLE__) && defined(__aarch64__)
    if (machineContext != nullptr) {
        const auto* context = static_cast<const ucontext_t*>(machineContext);
        programCounter = static_cast<std::uintptr_t>(context->uc_mcontext->__ss.__pc);
    }
#elif defined(__APPLE__) && defined(__x86_64__)
    if (machineContext != nullptr) {
        const auto* context = static_cast<const ucontext_t*>(machineContext);
        programCounter = static_cast<std::uintptr_t>(context->uc_mcontext->__ss.__rip);
    }
#endif
    // std::fprintf(
    //     stderr,
    //     "[PORTABLE_SIGNAL] signal=%d address=%p pc=0x%llx\n",
    //     signalNumber,
    //     signalInfo != nullptr ? signalInfo->si_addr : nullptr,
    //     static_cast<unsigned long long>(programCounter))
    // std::fflush(stderr)
    // Avoid CRT stream locks and allocation in the first signal breadcrumb.
    // The full report below has a separate, more demanding lifetime boundary.
    char signalLine[160];
    std::size_t used = 0;
    const auto append = [&](const char* text) {
        while (*text && used < sizeof(signalLine)) {
            signalLine[used++] = *text++;
        }
    };
    const auto appendNumber = [&](std::uint64_t value, unsigned base) {
        char digits[64];
        std::size_t count = 0;
        do {
            digits[count++] = "0123456789abcdef"[value % base];
            value /= base;
        } while (value != 0);
        while (count != 0 && used < sizeof(signalLine)) {
            signalLine[used++] = digits[--count];
        }
    };
    append("[PORTABLE_SIGNAL] signal=");
    const auto signedSignal = static_cast<std::int64_t>(signalNumber);
    if (signedSignal < 0) {
        append("-");
    }
    appendNumber(static_cast<std::uint64_t>(signedSignal < 0 ? -signedSignal : signedSignal), 10);
    append(" address=0x");
    appendNumber(signalInfo ? reinterpret_cast<std::uintptr_t>(signalInfo->si_addr) : 0, 16);
    append(" pc=0x");
    appendNumber(programCounter, 16);
    append("\n");
    (void)::write(STDERR_FILENO, signalLine, used);
    // A failed stderr write must not replace the interrupted code's error
    // with EBADF/EPIPE/etc. before the full report checks process state.
    errno = interruptedErrno;
    if (!g_portableCrashReportActive.test_and_set(std::memory_order_acq_rel)) {
        GeneratePortableFatalReport();
    }

    struct sigaction defaultAction{};
    defaultAction.sa_handler = SIG_DFL;
    sigemptyset(&defaultAction.sa_mask);
    ::sigaction(signalNumber, &defaultAction, nullptr);
    ::raise(signalNumber);
    ::_exit(128 + signalNumber);
}

void InstallPortableExceptionHandlers() {
    runtime::InstallThreadQuiescenceTransport();
    std::set_terminate(&PortableTerminateHandler);

    struct sigaction action{};
    action.sa_sigaction = &PortableSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    constexpr std::array<int, 5> fatalSignals{
        SIGABRT,
        SIGBUS,
        SIGFPE,
        SIGILL,
        SIGSEGV,
    };
    for (const int signalNumber : fatalSignals) {
        ::sigaction(signalNumber, &action, nullptr);
    }
}
#endif

} // namespace

#if defined(_WIN32)
_RTL_CRITICAL_SECTION
CLogCritical::m_CriticalSection{};
#else
std::recursive_mutex CLogCritical::m_CriticalSection;
#endif

CLogCritical::CLogCritical() {
    // Current L2Server.exe inlines this exact owner body in the exception
    // paths at 0x00727a50 and 0x007299d8.
    // Log.Add(
    //     LOG_NORMAL,
    //     "Lock CLogCritical")
    // Noise: routine lock entry is logged before taking the lock and does not prove success.
#if defined(_WIN32)
    ::EnterCriticalSection(&m_CriticalSection);
#else
    m_CriticalSection.lock();
#endif
}

CLogCritical::~CLogCritical() {
    // Current L2Server.exe 0x007263f0.
    // Log.Add(
    //     LOG_NORMAL,
    //     "Unlock CLogCritical")
    // Noise: routine lock exit is logged before release and does not prove success.
#if defined(_WIN32)
    ::LeaveCriticalSection(&m_CriticalSection);
#else
    m_CriticalSection.unlock();
#endif
}

void CLogCritical::Initialize() {
    // Current L2Server.exe inlines this exact owner body at 0x00729aa0.
    // Noise: Routine initialization of the crash-log lock reports no diagnostic result.
    // Log.Add(
    //     LOG_NORMAL,
    //     "Init CLogCritical")
#if defined(_WIN32)
    ::InitializeCriticalSection(&m_CriticalSection);
#endif
}

char* UnicodeToMultibyte(const wchar_t* pszStr) {
    // Current L2Server.exe 0x00726358.
    if (!pszStr) {
        return nullptr;
    }

    const int size = portable::WideCharToWindowsAnsi(pszStr, -1, nullptr, 0);
    char* const result = static_cast<char*>(std::malloc(static_cast<std::size_t>(size + 1)));
    if (!result) {
        return nullptr;
    }
    portable::WideCharToWindowsAnsi(pszStr, -1, result, size);
    result[size - 1] = '\0';
    return result;
}

#if defined(_WIN32)
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI DummySetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER) {
    // Current L2Server.exe 0x0092f498.
    return nullptr;
}

BOOL PreventSetUnhandledExceptionFilter() {
    // Current L2Server.exe 0x00727940. The target writes a nine-byte patch:
    // one near-jump opcode and the complete 64-bit relative displacement.
    HMODULE const kernel32 = ::LoadLibraryW(L"kernel32.dll");
    if (!kernel32) {
        return FALSE;
    }

    FARPROC const entry = ::GetProcAddress(kernel32, "SetUnhandledExceptionFilter");
    if (!entry) {
        return FALSE;
    }

    std::array<unsigned char, 9> patch{};
    patch[0] = 0xe9;
    const std::int64_t relative = static_cast<std::int64_t>(
        reinterpret_cast<std::intptr_t>(&DummySetUnhandledExceptionFilter) - reinterpret_cast<std::intptr_t>(entry) - 5
    );
    std::memcpy(patch.data() + 1, &relative, sizeof(relative));

    SIZE_T written = 0;
    return ::WriteProcessMemory(
        ::GetCurrentProcess(), reinterpret_cast<LPVOID>(entry), patch.data(), patch.size(), &written
    );
}
#endif

#if !defined(_MSC_VER)
namespace {

wchar_t* L2ServerTimeString(std::int32_t value) {
    // Portable implementation of the target _wctime32 boundary. Both
    // public callers share this one overwrite-on-call result buffer, just as
    // the Windows CRT entry point does.
    static std::array<wchar_t, 32> buffer{};
    if (value < 0) {
        return nullptr;
    }
    const std::time_t hostTime = static_cast<std::time_t>(value);
    std::tm* const local = std::localtime(&hostTime);
    if (!local || std::wcsftime(buffer.data(), buffer.size(), L"%a %b %d %H:%M:%S %Y\n", local) == 0) {
        return nullptr;
    }
    return buffer.data();
}

} // namespace
#endif

const wchar_t* GetCountryName() {
    // Current L2Server.exe 0x00726584 reads config.obj g_nCountryCode once
    // and returns one of these exact target literals. Codes 8 and 11 share
    // RUSSIA in the machine branch graph.
    switch (g_nCountryCode) {
        case 0:
            return L"KOREA";
        case 1:
            return L"USA";
        case 2:
            return L"JAPAN";
        case 3:
            return L"TAIWAN";
        case 4:
            return L"CHINA";
        case 5:
            return L"THAILAND";
        case 6:
            return L"PHILIPPINES";
        case 7:
            return L"INDONESIA";
        case 8:
        case 11:
            return L"RUSSIA";
        case 9:
            return L"EUROPE";
        case 10:
            return L"GERMANY";
        case 12:
            return L"POLAND";
        case 13:
            return L"TURKEY";
        default:
            return L"UNKNOWN";
    }
}

int GetL2ProtocolVersion() {
    // Current L2Server.exe 0x00726318 parses the exact embedded revision
    // literal through the CRT sscanf_s contract and returns the integer local.
    char temporary[80];
    int version;
#if defined(_MSC_VER)
    ::sscanf_s("$Revision: #162 $", "%s #%d", temporary, static_cast<unsigned>(sizeof(temporary)), &version);
#else
    std::sscanf("$Revision: #162 $", "%79s #%d", temporary, &version);
#endif
    return version;
}

int GetElapsedTime() {
    // Current L2Server.exe 0x00726528: _time32, _difftime32 against the
    // Exception.obj startup value, then CVTTSD2SI truncation.
    const std::int32_t now = runtime::crt::Time32Now();
    const double elapsed = std::difftime(static_cast<std::time_t>(now), static_cast<std::time_t>(server_up_time));
    constexpr double minimum = static_cast<double>(std::numeric_limits<std::int32_t>::min());
    constexpr double maximumExclusive = static_cast<double>(std::numeric_limits<std::int32_t>::max()) + 1.0;
    if (elapsed != elapsed || elapsed < minimum || elapsed >= maximumExclusive) {
        return std::numeric_limits<std::int32_t>::min();
    }
    return static_cast<int>(elapsed);
}

wchar_t* GetCurrentTimeString() {
    // Current L2Server.exe 0x00726550 keeps the time32 local alive through
    // the CRT's shared _wctime32-buffer call.
#if defined(_MSC_VER)
    __time32_t currentTime = 0;
    ::_time32(&currentTime);
    return ::_wctime32(&currentTime);
#else
    const std::int32_t currentTime = runtime::crt::Time32Now();
    return L2ServerTimeString(currentTime);
#endif
}

wchar_t* GetServerUpTimeString() {
    // Current L2Server.exe 0x00726578 is the direct _wctime32 tail call on
    // server_up_time.
#if defined(_MSC_VER)
    return ::_wctime32(reinterpret_cast<const __time32_t*>(&server_up_time));
#else
    return L2ServerTimeString(server_up_time);
#endif
}

wchar_t* GetElapsedTimeString() {
    // Current L2Server.exe 0x00727d84. All four divisions are double/CVTT,
    // followed by the exact target format and a CRT-owned duplicate.
    const auto truncate = [](double value) {
        constexpr double minimum = static_cast<double>(std::numeric_limits<std::int32_t>::min());
        constexpr double maximumExclusive = static_cast<double>(std::numeric_limits<std::int32_t>::max()) + 1.0;
        if (value != value || value < minimum || value >= maximumExclusive) {
            return std::numeric_limits<std::int32_t>::min();
        }
        return static_cast<int>(value);
    };

    const int elapsed = GetElapsedTime();
    const int days = truncate(static_cast<double>(elapsed) / 86400.0);
    double remainder = static_cast<double>(elapsed) - static_cast<double>(days * 86400);
    const int hours = truncate(remainder / 3600.0);
    remainder -= static_cast<double>(hours * 3600);
    const int minutes = truncate(remainder / 60.0);
    const int seconds = truncate(remainder - static_cast<double>(minutes * 60));

    std::array<wchar_t, 0x100> buffer{};
    const int length =
        std::swprintf(buffer.data(), buffer.size(), L"%d dd %d:%d:%d hh:mm:ss", days, hours, minutes, seconds);
    if (length < 0) {
        buffer[0] = L'\0';
    }

    const std::size_t bytes = (std::wcslen(buffer.data()) + 1) * sizeof(wchar_t);
    wchar_t* const result = static_cast<wchar_t*>(std::malloc(bytes));
    if (!result) {
        return nullptr;
    }
    std::memcpy(result, buffer.data(), bytes);
    return result;
}

CPUInfo::CPUInfo() : m_htEnabled(false), m_logicalProcessorCount(0), m_numaNodeCount(0), m_processorCoreCount(0) {
    // Current L2Server.exe PrintHardwareStatus@0x00728f58 inlines these
    // exact first-0x10-byte stores. The three cache records are populated by
    // Init and are not constructor-initialized in the target.
}

#if defined(_WIN32)
bool CPUInfo::CollectCPUInformation(_SYSTEM_LOGICAL_PROCESSOR_INFORMATION*& buffer, unsigned long& returnLength) {
    // Current L2Server.exe 0x0072835c.
    bool completed = false;
    do {
        if (!::GetLogicalProcessorInformation(buffer, &returnLength)) {
            const DWORD firstError = ::GetLastError();
            if (firstError != ERROR_INSUFFICIENT_BUFFER) {
                // The target builds and discards this diagnostic using a
                // second live GetLastError observation.
                std::array<wchar_t, 128> errorMessage{};
                std::swprintf(errorMessage.data(), errorMessage.size(), L"GetLastError(%d)\n", ::GetLastError());
                return false;
            }
            if (buffer != nullptr) {
                std::free(buffer);
            }
            buffer = static_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(std::malloc(returnLength));
            if (buffer == nullptr) {
                return false;
            }
        } else {
            completed = true;
        }
    } while (!completed);
    return true;
}
#endif

void CPUInfo::Init() {
    // Current L2Server.exe 0x00728410.
#if defined(_WIN32)
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION* buffer = nullptr;
    unsigned long returnLength = 0;
    if (!CollectCPUInformation(buffer, returnLength)) {
        return;
    }

    const unsigned char* cursor = reinterpret_cast<unsigned char*>(buffer);
    const unsigned char* const end = cursor + returnLength;
    while (cursor + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= end) {
        const auto& information = *reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(cursor);
        switch (information.Relationship) {
            case RelationProcessorCore:
                ++m_processorCoreCount;
                m_htEnabled = information.ProcessorCore.Flags != 0;
                m_logicalProcessorCount += CountSetBits(static_cast<std::uint64_t>(information.ProcessorMask));
                break;
            case RelationNumaNode:
                ++m_numaNodeCount;
                break;
            case RelationCache:
                if (information.Cache.Level > 0 && information.Cache.Level < 4) {
                    CpuCache& cache = m_cache[information.Cache.Level - 1];
                    cache.m_LineSize = information.Cache.LineSize;
                    cache.m_TotalSize = static_cast<int>(information.Cache.Size);
                }
                break;
            default:
                break;
        }
        cursor += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
    }
    std::free(buffer);
#else
    // Host implementation of the Windows structure boundary. Missing host
    // cache records remain deterministic zero records instead of exposing
    // the target constructor's uninitialized cache lanes.
    std::fill(std::begin(m_cache), std::end(m_cache), CpuCache{0, 0});

    const auto toInt = [](std::uint64_t value) {
        return value > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max()
                                                                                   : static_cast<int>(value);
    };

    long logicalProcessors = ::sysconf(_SC_NPROCESSORS_ONLN);
    if (logicalProcessors < 1) {
        logicalProcessors = static_cast<long>(std::thread::hardware_concurrency());
    }
    const std::uint32_t logicalCount = logicalProcessors > 0 ? static_cast<std::uint32_t>(logicalProcessors) : 0U;
    m_logicalProcessorCount += logicalCount;

#if defined(__APPLE__)
    const auto readSysctl = [](const char* name, auto& value) {
        std::size_t size = sizeof(value);
        return ::sysctlbyname(name, &value, &size, nullptr, 0) == 0;
    };

    std::uint32_t physicalCount = 0;
    if (!readSysctl("hw.physicalcpu", physicalCount)) {
        physicalCount = logicalCount;
    }
    m_processorCoreCount += physicalCount;
    m_htEnabled = physicalCount != 0 && logicalCount > physicalCount;

    std::uint64_t cacheLine = 0;
    readSysctl("hw.cachelinesize", cacheLine);
    std::array<std::uint64_t, 3> cacheSize{};
    readSysctl("hw.l1dcachesize", cacheSize[0]);
    readSysctl("hw.l2cachesize", cacheSize[1]);
    readSysctl("hw.l3cachesize", cacheSize[2]);
    for (std::size_t index = 0; index < cacheSize.size(); ++index) {
        m_cache[index].m_LineSize = toInt(cacheLine);
        m_cache[index].m_TotalSize = toInt(cacheSize[index]);
    }
#elif defined(__linux__)
    const auto readUnsignedFile = [](const std::filesystem::path& path, std::uint64_t& value) {
        std::ifstream input(path);
        if (!input) {
            return false;
        }
        input >> value;
        return !input.fail();
    };
    const auto parseCacheSize = [](const std::string& value) {
        if (value.empty()) {
            return std::uint64_t{0};
        }
        char* end = nullptr;
        const unsigned long long base = std::strtoull(value.c_str(), &end, 10);
        if (end == value.c_str()) {
            return std::uint64_t{0};
        }
        std::uint64_t multiplier = 1;
        if (*end == 'K' || *end == 'k') {
            multiplier = 1024;
        } else if (*end == 'M' || *end == 'm') {
            multiplier = UINT64_C(1024) * 1024;
        } else if (*end == 'G' || *end == 'g') {
            multiplier = UINT64_C(1024) * 1024 * 1024;
        }
        return static_cast<std::uint64_t>(base) * multiplier;
    };

    std::set<std::pair<std::uint64_t, std::uint64_t>> physicalCores;
    for (std::uint32_t cpu = 0; cpu < logicalCount; ++cpu) {
        const std::filesystem::path root =
            std::filesystem::path("/sys/devices/system/cpu") / ("cpu" + std::to_string(cpu)) / "topology";
        std::uint64_t package = 0;
        std::uint64_t core = 0;
        if (readUnsignedFile(root / "physical_package_id", package) && readUnsignedFile(root / "core_id", core)) {
            physicalCores.emplace(package, core);
        }
    }
    const std::uint32_t physicalCount =
        physicalCores.empty() ? logicalCount : static_cast<std::uint32_t>(physicalCores.size());
    m_processorCoreCount += physicalCount;
    m_htEnabled = physicalCount != 0 && logicalCount > physicalCount;

    const std::filesystem::path nodeRoot = "/sys/devices/system/node";
    std::error_code nodeError;
    for (const auto& entry : std::filesystem::directory_iterator(nodeRoot, nodeError)) {
        const std::string name = entry.path().filename().string();
        if (name.size() > 4 && name.compare(0, 4, "node") == 0 &&
            std::all_of(name.begin() + 4, name.end(), [](unsigned char value) { return value >= '0' && value <= '9'; })) {
            ++m_numaNodeCount;
        }
    }

    const std::filesystem::path cacheRoot = "/sys/devices/system/cpu/cpu0/cache";
    for (int index = 0; index < 16; ++index) {
        const std::filesystem::path root = cacheRoot / ("index" + std::to_string(index));
        std::uint64_t level = 0;
        if (!readUnsignedFile(root / "level", level) || level < 1 || level > 3) {
            continue;
        }
        std::uint64_t line = 0;
        readUnsignedFile(root / "coherency_line_size", line);
        std::ifstream sizeInput(root / "size");
        std::string size;
        sizeInput >> size;
        CpuCache& cache = m_cache[static_cast<std::size_t>(level - 1)];
        cache.m_LineSize = toInt(line);
        cache.m_TotalSize = toInt(parseCacheSize(size));
    }
#else
    m_processorCoreCount += logicalCount;
#endif
#endif
}

wchar_t* CPUInfo::PrintHyperThreadResult() {
    // Inlined by current PrintHardwareStatus@0x00728f58.
    static wchar_t disabled[] = L"OFF";
    static wchar_t enabled[] = L"ON";
    return m_htEnabled ? enabled : disabled;
}

std::wstring CPUInfo::PrintCacheLineSize() {
    // Current L2Server.exe 0x00728dfc.
    std::array<wchar_t, 256> buffer{};
    std::swprintf(
        buffer.data(), buffer.size(), L"CacheLine %d/%d/%d", m_cache[0].m_LineSize, m_cache[1].m_LineSize,
        m_cache[2].m_LineSize
    );
    return buffer.data();
}

std::wstring CPUInfo::PrintCacheTotalSize() {
    // Current L2Server.exe 0x00728e98.
    std::array<wchar_t, 512> buffer{};
    std::swprintf(
        buffer.data(), buffer.size(), L"TotalCacheSize %dk/%dk/%dk", m_cache[0].m_TotalSize / 1024,
        m_cache[1].m_TotalSize / 1024, m_cache[2].m_TotalSize / 1024
    );
    return buffer.data();
}

void L2Exception::PrintHardwareStatus(void* file) {
    // Current L2Server.exe 0x00728f58.
    hprintf(file, L"[Hardware Status]\r\n");

    CPUInfo cpu;
    cpu.Init();

    const wchar_t* architecture = L"UnknownArchitecture";
    std::uint32_t processorCount = 0;
    std::uint64_t pageSize = 0;
    const wchar_t* numa = L"no supported";
    std::uint64_t minimumAddress = 0;
    std::uint64_t maximumAddress = std::numeric_limits<std::uintptr_t>::max();

#if defined(_WIN32)
    SYSTEM_INFO systemInfo;
    ::GetSystemInfo(&systemInfo);
    architecture = GetProcessorArchitecture(systemInfo);
    processorCount = systemInfo.dwNumberOfProcessors;
    pageSize = systemInfo.dwPageSize;
    minimumAddress = reinterpret_cast<std::uintptr_t>(systemInfo.lpMinimumApplicationAddress);
    maximumAddress = reinterpret_cast<std::uintptr_t>(systemInfo.lpMaximumApplicationAddress);

    ULONG highestNumaNode = 0;
    if (::GetNumaHighestNodeNumber(&highestNumaNode)) {
        numa = highestNumaNode == 0 ? L"OFF" : L"ON";
    }
#else
#if defined(__x86_64__) || defined(_M_X64)
    architecture = L"x64";
#elif defined(__i386__) || defined(_M_IX86)
    architecture = L"x86";
#endif
    long hostProcessorCount = ::sysconf(_SC_NPROCESSORS_ONLN);
    if (hostProcessorCount < 1) {
        hostProcessorCount = static_cast<long>(std::thread::hardware_concurrency());
    }
    if (hostProcessorCount > 0) {
        processorCount = static_cast<std::uint32_t>(hostProcessorCount);
    }
    const long hostPageSize = ::sysconf(_SC_PAGESIZE);
    if (hostPageSize > 0) {
        pageSize = static_cast<std::uint64_t>(hostPageSize);
        minimumAddress = pageSize;
    }
#if defined(__linux__)
    if (std::filesystem::exists("/sys/devices/system/node/node1")) {
        numa = L"ON";
    } else if (std::filesystem::exists("/sys/devices/system/node/node0")) {
        numa = L"OFF";
    }
#endif
#endif

    hprintf(
        file,
        L"\tCPU\t\t\t: %s, %d processor(s), "
        L"PageSize:%d, NUMA[%s] SMT[%s]\r\n",
        architecture, static_cast<int>(processorCount), static_cast<int>(pageSize), numa, cpu.PrintHyperThreadResult()
    );

    const std::wstring totalCacheSize = cpu.PrintCacheTotalSize();
    const std::wstring cacheLine = cpu.PrintCacheLineSize();
    hprintf(file, L"\tCache(L1/L2/L3)\t: %s, %s\r\n", cacheLine.c_str(), totalCacheSize.c_str());
    hprintf(
        file,
        L"\tAccessible address\t: "
        L"0x%016I64X ~ 0x%016I64X\r\n",
        static_cast<unsigned long long>(minimumAddress), static_cast<unsigned long long>(maximumAddress)
    );

    std::uint64_t availablePhysical = 0;
    std::uint64_t totalPhysical = 0;
    std::uint64_t availablePageFile = 0;
    std::uint64_t totalPageFile = 0;
#if defined(_WIN32)
    MEMORYSTATUSEX memory;
    memory.dwLength = sizeof(memory);
    ::GlobalMemoryStatusEx(&memory);
    availablePhysical = memory.ullAvailPhys;
    totalPhysical = memory.ullTotalPhys;
    availablePageFile = memory.ullAvailPageFile;
    totalPageFile = memory.ullTotalPageFile;
#elif defined(__APPLE__)
    const auto readSysctl = [](const char* name, auto& value) {
        std::size_t size = sizeof(value);
        return ::sysctlbyname(name, &value, &size, nullptr, 0) == 0;
    };
    readSysctl("hw.memsize", totalPhysical);
    vm_statistics64_data_t statistics{};
    mach_msg_type_number_t statisticsCount = HOST_VM_INFO64_COUNT;
    if (::host_statistics64(
            ::mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&statistics), &statisticsCount
        ) == KERN_SUCCESS) {
        availablePhysical =
            (static_cast<std::uint64_t>(statistics.free_count) + static_cast<std::uint64_t>(statistics.inactive_count)) *
            pageSize;
    }
    struct xsw_usage swap{};
    if (readSysctl("vm.swapusage", swap)) {
        totalPageFile = swap.xsu_total;
        availablePageFile = swap.xsu_avail;
    }
#elif defined(__linux__)
    struct sysinfo memory{};
    if (::sysinfo(&memory) == 0) {
        const std::uint64_t unit = memory.mem_unit;
        totalPhysical = static_cast<std::uint64_t>(memory.totalram) * unit;
        availablePhysical =
            (static_cast<std::uint64_t>(memory.freeram) + static_cast<std::uint64_t>(memory.bufferram)) * unit;
        totalPageFile = static_cast<std::uint64_t>(memory.totalswap) * unit;
        availablePageFile = static_cast<std::uint64_t>(memory.freeswap) * unit;
    }
#else
    const long physicalPages = ::sysconf(_SC_PHYS_PAGES);
    const long availablePages = ::sysconf(_SC_AVPHYS_PAGES);
    if (physicalPages > 0) {
        totalPhysical = static_cast<std::uint64_t>(physicalPages) * pageSize;
    }
    if (availablePages > 0) {
        availablePhysical = static_cast<std::uint64_t>(availablePages) * pageSize;
    }
#endif

    hprintf(
        file,
        L"\tPhysical Memory\t: Free [%d MB] / "
        L"Total [%d MB] \r\n",
        static_cast<std::int32_t>(availablePhysical >> 20U), static_cast<std::int32_t>(totalPhysical >> 20U)
    );
    hprintf(
        file,
        L"\tPageFile Memory\t: Free [%d MB] / "
        L"Total [%d MB] \r\n",
        static_cast<std::int32_t>(availablePageFile >> 20U), static_cast<std::int32_t>(totalPageFile >> 20U)
    );
}

void L2Exception::PrintCrashSummary(void* file) {
    // Current L2Server.exe 0x00728be4. Unlike the neighbouring products,
    // this build names every service-thread lane 16..24 and includes the
    // live default.obj guard-history string in both thread diagnostics.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"in generate crash summary")
    // Noise: the report file itself immediately receives the crash-summary section.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"in generate crash summary")
    hprintf(file, L"[Crash Summary]\r\n");

    const int threadId = CThreadManager::GetThreadID();
    if (threadId == ThreadID::CAPTCHA) {
        hprintf(file, L"\tCapchaThread crashed\r\n");
    } else if (threadId == ThreadID::GOODS_INVENTORY) {
        hprintf(file, L"\tGoodsInventoryThread crashed\r\n");
    } else if (threadId == ThreadID::RELAY) {
        hprintf(file, L"\tRelayThread crashed\r\n");
    } else if (threadId == ThreadID::RAID) {
        hprintf(file, L"\tRaidThread crashed\r\n");
    } else if (threadId == ThreadID::PREMIUM) {
        hprintf(file, L"\tPremiumThread crashed\r\n");
    } else if (threadId == ThreadID::L2COMM) {
        hprintf(file, L"\tL2CommThread crashed\r\n");
    } else if (threadId == ThreadID::PETITION) {
        hprintf(file, L"\tPetitionThread crashed\r\n");
    } else if (threadId == ThreadID::GIP) {
        hprintf(file, L"\tGIPThread crashed\r\n");
    } else if (threadId == ThreadID::LOG) {
        hprintf(file, L"\tLogThread crashed\r\n");
    }

    // Log.Add(
    //     LOG_LOCAL,
    //     L"reported crashed thread [%d][%s]",
    //     threadId,
    //     g_GuardHistoryStr)
    // Noise: the report body records the crashed worker and guard history below.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"reported crashed thread [%d][%s]",
    //     threadId,
    //     g_GuardHistoryStr)
    hprintf(file, L"\tCrashed Thread[%d] GuardHistory[%s].\r\n", threadId, g_GuardHistoryStr);
    // Log.Add(
    //     LOG_LOCAL,
    //     L"by system manager? [%d]",
    //     static_cast<int>(g_IsBySystemManager))
    // Noise: the report body records whether the system manager requested it.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"by system manager? [%d]",
    //     static_cast<int>(g_IsBySystemManager))

    if (g_bInvalidParameter) {
        hprintf(file, L"\tInvalid parameter checked[%s]\r\n", g_sInvalidParamExpr);
    }
    if (g_IsBySystemManager) {
        hprintf(file, L"\tServer is closed by system manager.\r\n");
    }
    if (!bCanConnectCacheServer) {
        hprintf(file, L"\tCan't connect to cache server \r\n");
    }

    g_CrashInfo.Print(file);
    if (bForceCrash) {
        hprintf(file, L"\tWas forced crash by\r\n");
        hprintf(file, L"\t\t%s\r\n", g_CrashFunctionName);
        hprintf(file, L"\t\t%s%s\r\n", g_CrashFileName, g_CrashLineNo);
    }

    if (Singleton<CIODeadlockChecker>::inst().m_DeadlockCrashInfo.ForcedCrashByDeadlockChecker()) {
        Singleton<CIODeadlockChecker>::inst().m_DeadlockCrashInfo.PrintLog(file);
    }
}

void L2Exception::PrintIOBuffer(void* file) {
    // Current L2Server.exe 0x007284dc. The read order is fixed by the target:
    // pending bytes, process total/free counters, then per-thread total/free.
    hprintf(file, L"[IOBuffer Pool]\r\n");
    const int pendingWrite = g_nSocketPendingWrite.load(std::memory_order_relaxed);
    const int totalBufferCount = GetIOBufferPoolTotalCount();
    const int freeBufferCount = GetIOBufferPoolCount();
    hprintf(
        file,
        L"\tIOBufferPool - %d / %d,  "
        L"PendingWrite %d bytes "
        L"(%d buffer count) \r\n",
        freeBufferCount, totalBufferCount, pendingWrite, pendingWrite / 0x5000
    );
    hprintf(
        file,
        L"\tDetail IO Buffer Pool Size "
        L"for each Thread\r\n"
    );

    for (int threadId = 0; threadId < CThreadManager::GetThreadCount(); ++threadId) {
        const int threadTotal = GetIOBufferPoolThreadTotalCount(threadId);
        const int threadFree = GetIOBufferPoolThreadCount(threadId);
        hprintf(
            file,
            L"\t(%02d) : Free(%d) / "
            L"Total(%d)\r\n",
            threadId, threadFree, threadTotal
        );
    }
}

void L2Exception::PrintServerInfo(void* file) {
    // Current L2Server.exe 0x007292fc. Native host queries below are the
    // portability boundary for GetModuleFileNameW/GetUserNameW and
    // GetCurrentThreadId. Report order, fallback strings and target values
    // remain identical.
    hprintf(file, L"[Server Info]\r\n");

    const wchar_t* const countryName = GetCountryName();
    const int worldId = g_nWorldId;
    hprintf(file, L"\tCountry\t\t\t: %s [%d]\r\n", countryName, worldId);

    std::array<wchar_t, 0x104> binaryPath{};
#if defined(_WIN32)
    if (::GetModuleFileNameW(nullptr, binaryPath.data(), static_cast<DWORD>(binaryPath.size())) == 0) {
        ::wcscpy_s(binaryPath.data(), binaryPath.size(), L"Unknown Module");
    }
#else
    std::string executablePath;
#if defined(__APPLE__)
    std::uint32_t pathCapacity = 0;
    ::_NSGetExecutablePath(nullptr, &pathCapacity);
    if (pathCapacity != 0) {
        executablePath.resize(pathCapacity);
        if (::_NSGetExecutablePath(executablePath.data(), &pathCapacity) == 0) {
            executablePath.resize(std::char_traits<char>::length(executablePath.c_str()));
        } else {
            executablePath.clear();
        }
    }
#elif defined(__linux__)
    std::array<char, PATH_MAX + 1> path{};
    const ssize_t pathLength = ::readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (pathLength > 0) {
        executablePath.assign(path.data(), static_cast<std::size_t>(pathLength));
    }
#endif
    const std::wstring hostBinaryPath =
        executablePath.empty() ? std::wstring(L"Unknown Module") : Utf8ToWide(executablePath);
    const std::size_t binaryLength = std::min<std::size_t>(hostBinaryPath.size(), binaryPath.size() - 1);
    std::wmemcpy(binaryPath.data(), hostBinaryPath.data(), binaryLength);
    binaryPath[binaryLength] = L'\0';
#endif
    hprintf(file, L"\tBinary Path\t\t: %s\r\n", binaryPath.data());

    std::array<wchar_t, 200> account{};
#if defined(_WIN32)
    DWORD accountLength = static_cast<DWORD>(account.size());
    if (!::GetUserNameW(account.data(), &accountLength)) {
        ::wcscpy_s(account.data(), account.size(), L"Unknown User");
    }
#else
    struct passwd password{};
    struct passwd* passwordResult = nullptr;
    std::array<char, 4096> passwordBuffer{};
    const bool accountRead =
        ::getpwuid_r(::geteuid(), &password, passwordBuffer.data(), passwordBuffer.size(), &passwordResult) == 0 &&
        passwordResult && passwordResult->pw_name;
    const std::wstring hostAccount = accountRead ? Utf8ToWide(passwordResult->pw_name) : std::wstring(L"Unknown User");
    const std::size_t accountCopyLength =
        hostAccount.size() < account.size() ? hostAccount.size() : std::wstring_view(L"Unknown User").size();
    const wchar_t* const accountSource = hostAccount.size() < account.size() ? hostAccount.c_str() : L"Unknown User";
    std::wmemcpy(account.data(), accountSource, accountCopyLength);
    account[accountCopyLength] = L'\0';
#endif
    hprintf(file, L"\tProcess Account\t: %s\r\n", account.data());

    hprintf(file, L"\tL2Protocol\t\t: %d\r\n", GetL2ProtocolVersion());

#if defined(_WIN32)
    const wchar_t* const contentsVersion = reinterpret_cast<const wchar_t*>(g_BinaryInfo.updateVersion);
#else
    std::size_t contentsLength = 0;
    while (contentsLength < std::size(g_BinaryInfo.updateVersion) && g_BinaryInfo.updateVersion[contentsLength] != u'\0') {
        ++contentsLength;
    }
    const std::wstring contentsVersionStorage = Utf16ToWide(std::u16string_view(g_BinaryInfo.updateVersion, contentsLength));
    const wchar_t* const contentsVersion = contentsVersionStorage.c_str();
#endif
    hprintf(file, L"\tContents Version\t: %s\r\n", contentsVersion);

    hprintf(file, L"\tBuild Date\t\t: %s\r\n", CBuildDate::inst().BuildDateTime());
    hprintf(file, L"\tBuild Archive\t\t: %s\r\n", CBuildDate::inst().BuildArchive());
    hprintf(file, L"\tBuild Compiler\t: %d (Version) \r\n", 0x098a051b);

    std::uint32_t nativeThreadId = 0;
#if defined(_WIN32)
    nativeThreadId = ::GetCurrentThreadId();
#elif defined(__APPLE__)
    std::uint64_t hostThreadId = 0;
    if (::pthread_threadid_np(nullptr, &hostThreadId) == 0) {
        nativeThreadId = static_cast<std::uint32_t>(hostThreadId);
    }
#elif defined(__linux__)
    nativeThreadId = static_cast<std::uint32_t>(::syscall(SYS_gettid));
#else
    nativeThreadId = static_cast<std::uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
    hprintf(file, L"\tCurrent Thread ID\t: %d\r\n", static_cast<std::int32_t>(nativeThreadId));

    hprintf(file, L"\tServer Up Time\t: %s\r\n", GetServerUpTimeString());
    hprintf(file, L"\tCurrent    Time\t: %s\r\n", GetCurrentTimeString());
    wchar_t* const elapsedTime = GetElapsedTimeString();
    hprintf(file, L"\tElapsed    Time\t: %s\r\n", elapsedTime);
    std::free(elapsedTime);
}

void L2Exception::PrintObjectReport(void* file) {
    // Current L2Server.exe 0x007273bc. The first row reports
    // active-minus-not-released beside a second live read of each release
    // counter. Bridge is not present in the target format.
    hprintf(file, L"[Object Report]\r\n");
    hprintf(file, L"\t[active : release failed]\r\n");

    const auto load = [](const std::atomic<std::int32_t>& value) { return value.load(std::memory_order_relaxed); };

    const int airShip = load(g_ServerContext.AirShip);
    const int activeAirShip = airShip - load(g_ServerContext.NotReleasedAirShip);
    const int auction = load(g_ServerContext.Auction);
    const int activeAuction = auction - load(g_ServerContext.NotReleasedAuction);
    const int trade = load(g_ServerContext.Trade);
    const int activeTrade = trade - load(g_ServerContext.NotReleasedTrade);
    const int inZone = load(g_ServerContext.InZone);
    const int activeInZone = inZone - load(g_ServerContext.NotReleasedInZone);
    const int userSocket = load(g_ServerContext.UserSocket);
    const int activeUserSocket = userSocket - load(g_ServerContext.NotReleasedUserSocket);
    const int item = load(g_ServerContext.Item);
    const int activeItem = item - load(g_ServerContext.NotReleasedItem);
    const int npc = load(g_ServerContext.Npc);
    const int activeNpc = npc - load(g_ServerContext.NotReleasedNpc);
    const int user = load(g_ServerContext.User);
    const int activeUser = user - load(g_ServerContext.NotReleasedUser);

    const int maxInZoneNpcCount = g_nMaxInZoneNPCCount;
    const int notReleasedAirShip = load(g_ServerContext.NotReleasedAirShip);
    const int notReleasedAuction = load(g_ServerContext.NotReleasedAuction);
    const int notReleasedTrade = load(g_ServerContext.NotReleasedTrade);
    const int notReleasedInZone = load(g_ServerContext.NotReleasedInZone);
    const int notReleasedUserSocket = load(g_ServerContext.NotReleasedUserSocket);
    const int notReleasedItem = load(g_ServerContext.NotReleasedItem);
    const int notReleasedNpc = load(g_ServerContext.NotReleasedNpc);
    const int notReleasedUser = load(g_ServerContext.NotReleasedUser);

    hprintf(
        file,
        L"\tuser[%d:%d], npc[%d:%d], item[%d:%d], "
        L"usersocket[%d:%d], InZone[%d:%d], Trade[%d:%d], "
        L"Auction[%d:%d] AirShip[%d:%d] "
        L"Max InZone NPC[%d]\r\n",
        activeUser, notReleasedUser, activeNpc, notReleasedNpc, activeItem, notReleasedItem, activeUserSocket,
        notReleasedUserSocket, activeInZone, notReleasedInZone, activeTrade, notReleasedTrade, activeAuction,
        notReleasedAuction, activeAirShip, notReleasedAirShip, maxInZoneNpcCount
    );

    hprintf(file, L"[StaticObject Report]\r\n");

    // Current right-to-left load order. HolyThing and OwnThing are physical
    // ServerContext fields but are absent from this report.
    const int etcStaticObject = load(g_ServerContext.EtcStaticObject);
    const int teamBattleFlag = load(g_ServerContext.TeamBattleFlag);
    const int statue = load(g_ServerContext.Statue);
    const int signBoard = load(g_ServerContext.SignBoard);
    const int observerRemains = load(g_ServerContext.ObserverRemains);
    const int fortressFlagpole = load(g_ServerContext.FortressFlagpole);
    const int eventMatch = load(g_ServerContext.EventMatch);
    const int eventController = load(g_ServerContext.EventController);
    const int door = load(g_ServerContext.Door);
    const int controlTower = load(g_ServerContext.ControlTower);
    const int chair = load(g_ServerContext.Chair);
    const int battleCamp = load(g_ServerContext.BattleCamp);
    const int airShipPart = load(g_ServerContext.AirShipPart);
    const int airShipSteering = load(g_ServerContext.AirShipSteering);
    const int residenceDecoVisible = load(g_ServerContext.ResidenceDecoVisible);

    hprintf(
        file,
        L"\tResidenceDecoVisible[%d], AirShipSteering[%d], "
        L"AirShipPart[%d], BattleCamp[%d], Chair[%d], "
        L"ResidenceDecoVisible[%d], Door[%d], "
        L"EventController[%d], EventMatch[%d], "
        L"FortressFlagpole[%d], ObserverRemains[%d], "
        L"SignBoard[%d], Statue[%d], TeamBattleFlag[%d], "
        L"EtcStaticObject[%d]\r\n",
        residenceDecoVisible, airShipSteering, airShipPart, battleCamp, chair, controlTower, door, eventController,
        eventMatch, fortressFlagpole, observerRemains, signBoard, statue, teamBattleFlag, etcStaticObject
    );

    if (g_LastAction[CThreadManager::GetThreadID()].m_SkillID == 0) {
        return;
    }

    hprintf(file, L"\r\n[Skill use info]\r\n");
    if (g_LastAction[CThreadManager::GetThreadID()].m_Name[0] != u'\0') {
#if defined(_WIN32)
        hprintf(
            file, L"\tNpc Name : [%s], Class [%d]",
            reinterpret_cast<const wchar_t*>(g_LastAction[CThreadManager::GetThreadID()].m_Name),
            g_LastAction[CThreadManager::GetThreadID()].m_PlayerClassID
        );
#else
        const LastAction& action = g_LastAction[CThreadManager::GetThreadID()];
        std::size_t length = 0;
        while (length < std::size(action.m_Name) && action.m_Name[length] != u'\0') {
            ++length;
        }
        const std::wstring name = Utf16ToWide(std::u16string_view(action.m_Name, length));
        hprintf(file, L"\tNpc Name : [%s], Class [%d]", name.c_str(), action.m_PlayerClassID);
#endif
    }

    hprintf(
        file,
        L"\tSkill ID [%d], Altered Skill ID : [%d], "
        L"Item Class ID [%d]\r\n\r\n",
        g_LastAction[CThreadManager::GetThreadID()].m_SkillID, g_LastAction[CThreadManager::GetThreadID()].m_AlteredSkillID,
        g_LastAction[CThreadManager::GetThreadID()].m_ItemClassID
    );
}

namespace {

void PrintCallStack(void* file, int threadId, const wchar_t* threadName) {
    // Current L2Server.exe 0x00726898. Unlike the neighbouring products,
    // L2Server appends its UserGC and CheckBuffInfo lanes before the shared
    // guard frames. The target applies the full non-negative
    // bounds check only to those ThreadManager-owned diagnostic arrays.
    if (threadId < ThreadID::MAX) {
        const std::uint32_t startTick = g_dwGuardStartTick[threadId];
        const wchar_t* const state = threadId == CThreadManager::GetThreadID() ? L"ahehe" : L"good";
        hprintf(
            file, L"\t%s\t[%02d][start: %d][%u ms] (%s):  ", threadName, threadId, static_cast<int>(startTick),
            runtime::GetTickCount() - startTick, state
        );
    }

    if (threadId >= 0 && threadId < ThreadID::MAX) {
        hprintf(
            file,
            L"\tSkillInfo actor dbid:%u, skill_id:%d "
            L"target_cnt:%d\r\n",
            static_cast<unsigned>(g_nSkillActorDBID[threadId]), g_nSkillID[threadId], g_nSkillTargetCount[threadId]
        );

        const int postCount = g_nLeaveWorldPostCount[threadId];
        hprintf(file, L"\tPostInfo count: %d post dbids: ", postCount);
        for (int index = 0; index < postCount && index < 32; ++index) {
            hprintf(file, L"%d ", g_nLeaveWorldPostDBID[threadId][index]);
        }
        hprintf(file, L"\r\n");

        hprintf(
            file, L"\tUserGCInfo EnterTime[%d] DBID[%d] RefCnt[%d]", g_nUserGarbageEnterTime[threadId],
            g_nUserGarbageUserDBID[threadId], g_nUserGarbageRefCount[threadId]
        );
        hprintf(file, L"\r\n");
        g_CheckBuffInfo[threadId].ReportCheckBuffInfo(file);
    }

    GuardHelper::printCallStackWithElapsedTime(file, threadId);

    hprintf(file, L"\t\tLock Stack : ");
    const CLockStatusStack& stack = g_LockStack[threadId];
    for (int index = 0; index < stack.mt_nStackTop; ++index) {
        const CLockStatusStack::NodeInfo& node = stack.mt_Stack[index];
        hprintf(
            file,
            L"%s(line[%d], lock object pointer "
            L"[0x%I64x] Entered:%d)",
            node.desc, static_cast<int>(node.line), static_cast<unsigned long long>(node.addr),
            static_cast<int>(node.entered)
        );
        if (index != stack.mt_nStackTop - 1) {
            hprintf(file, L" -> ");
        }
    }
    hprintf(file, L"\r\n");
    hprintf(file, L"\r\n");
}

} // namespace

void L2Exception::SuspendThreadExceptMe() {
    // Current L2Server.exe 0x00727218. Keep the worker traversal locked
    // and the later fixed-role suspension outside that lock.
    {
        L2_GUARD(g_ThreadSuspendLock);
        for (int threadId = 0; threadId < CThreadManager::GetThreadCount(); ++threadId) {
            if (threadId == CThreadManager::GetThreadID()) {
                continue;
            }
#if defined(_WIN32)
            ::SuspendThread(static_cast<HANDLE>(CThreadManager::GetThread(threadId)));
#else
            runtime::SuspendNativeThread(CThreadManager::GetThread(threadId));
#endif
        }
    }

    if (!CThreadManager::IsListenThread()) {
#if defined(_WIN32)
        ::SuspendThread(static_cast<HANDLE>(CThreadManager::GetThread(ThreadID::LISTEN)));
#else
        runtime::SuspendNativeThread(CThreadManager::GetThread(ThreadID::LISTEN));
#endif
    }
    if (!CThreadManager::IsMainThread()) {
#if defined(_WIN32)
        ::SuspendThread(static_cast<HANDLE>(CThreadManager::GetThread(ThreadID::MAIN)));
#else
        runtime::SuspendNativeThread(CThreadManager::GetThread(ThreadID::MAIN));
#endif
    }
}

void L2Exception::ResumeThreadExceptMe() {
    // Current L2Server.exe 0x007272bc resumes the two fixed roles before it
    // enters the serialized live IO-thread traversal.
    if (!CThreadManager::IsMainThread()) {
#if defined(_WIN32)
        ::ResumeThread(static_cast<HANDLE>(CThreadManager::GetThread(ThreadID::MAIN)));
#else
        runtime::ResumeNativeThread(CThreadManager::GetThread(ThreadID::MAIN));
#endif
    }
    if (!CThreadManager::IsListenThread()) {
#if defined(_WIN32)
        ::ResumeThread(static_cast<HANDLE>(CThreadManager::GetThread(ThreadID::LISTEN)));
#else
        runtime::ResumeNativeThread(CThreadManager::GetThread(ThreadID::LISTEN));
#endif
    }

    {
        L2_GUARD(g_ThreadSuspendLock);
        for (int threadId = 0; threadId < CThreadManager::GetThreadCount(); ++threadId) {
            if (threadId == CThreadManager::GetThreadID()) {
                continue;
            }
#if defined(_WIN32)
            ::ResumeThread(static_cast<HANDLE>(CThreadManager::GetThread(threadId)));
#else
            runtime::ResumeNativeThread(CThreadManager::GetThread(threadId));
#endif
        }
    }
}

void L2Exception::GenerateExceptionReportBack(void* file) {
    // Current L2Server.exe 0x00728208. The live IO range is followed by the
    // target's fixed-role suffix 16..27. Indices 28..31 are intentionally
    // absent from this report even though the process arrays have 32 slots.
    SuspendThreadExceptMe();
    hprintf(file, L"[Guard Info]\r\n");

    for (int threadId = 0; threadId < CThreadManager::GetThreadCount(); ++threadId) {
        PrintCallStack(file, threadId, L"IOThread");
    }

    PrintCallStack(file, ThreadID::CAPTCHA, L"CapchaThread");
    PrintCallStack(file, ThreadID::GOODS_INVENTORY, L"GoodsInventoryThread");
    PrintCallStack(file, ThreadID::RELAY, L"RelayThread");
    PrintCallStack(file, ThreadID::RAID, L"RaidThread");
    PrintCallStack(file, ThreadID::PREMIUM, L"PremiumThread");
    PrintCallStack(file, ThreadID::L2COMM, L"L2CommThread");
    PrintCallStack(file, ThreadID::PETITION, L"PetitionThread");
    PrintCallStack(file, ThreadID::GIP, L"GIPThread");
    PrintCallStack(file, ThreadID::LOG, L"LogThread");
    PrintCallStack(file, ThreadID::PERFMON, L"PerfMonThread");
    PrintCallStack(file, ThreadID::MAIN, L"MainThread");
    PrintCallStack(file, ThreadID::LISTEN, L"ListenThread");
    hprintf(file, L"\r\n");
}

void L2Exception::MakeFileNameWithModuleName(wchar_t szFileFullName[], const wchar_t* pPostFix) {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    // Current L2Server.exe 0x00727e74. The optimized body keeps the local
    // basename copy and extension removal even though the result is unused.
    if (::GetModuleFileNameW(nullptr, szFileFullName, 0x208) == 0) {
        szFileFullName[0] = L'\0';
    }
    wchar_t* basename = std::wcsrchr(szFileFullName, L'\\');
    basename = basename ? basename + 1 : szFileFullName;

    std::array<wchar_t, 260> fileName{};
    ::wcscpy_s(fileName.data(), fileName.size(), basename);
    if (wchar_t* const extension = std::wcsrchr(fileName.data(), L'.')) {
        *extension = L'\0';
    }
    ::lstrcpyW(basename, pPostFix);
#else
    // Platform implementation of the same observable contract: replace the
    // executable basename while retaining its containing directory.
    std::string executablePath;
#if defined(__APPLE__)
    std::uint32_t capacity = 0;
    _NSGetExecutablePath(nullptr, &capacity);
    if (capacity != 0) {
        executablePath.resize(capacity);
        if (_NSGetExecutablePath(executablePath.data(), &capacity) != 0) {
            executablePath.clear();
        } else {
            executablePath.resize(std::char_traits<char>::length(executablePath.c_str()));
        }
    }
#elif defined(__linux__)
    std::array<char, PATH_MAX + 1> path{};
    const ssize_t length = ::readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (length > 0) {
        executablePath.assign(path.data(), static_cast<std::size_t>(length));
    }
#endif
    if (executablePath.empty()) {
        executablePath = (std::filesystem::current_path() / "L2Server").string();
    }

    std::wstring modulePath = Utf8ToWide(executablePath);
    const std::size_t separator = modulePath.find_last_of(L'/');
    if (separator == std::wstring::npos) {
        modulePath.clear();
    } else {
        modulePath.resize(separator + 1);
    }
    modulePath.append(pPostFix);

    const std::size_t copyLength = std::min<std::size_t>(modulePath.size(), 259);
    std::wmemcpy(szFileFullName, modulePath.data(), copyLength);
    szFileFullName[copyLength] = L'\0';
#endif
}

void* L2Exception::CreateFileWithModuleName(wchar_t szFileFullName[], wchar_t* pPostFix) {
    L2_GUARD_FUNCTION();

    // Current L2Server.exe 0x00727fa4.
    MakeFileNameWithModuleName(szFileFullName, pPostFix);
#if defined(_WIN32)
    ::wcsncpy_s(g_wszDumpFilename, std::size(g_wszDumpFilename), szFileFullName, std::size(g_wszDumpFilename));
    return ::CreateFileW(
        szFileFullName, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr
    );
#else
    const std::size_t length = std::min<std::size_t>(std::wcslen(szFileFullName), std::size(g_wszDumpFilename) - 1);
    std::wmemcpy(g_wszDumpFilename, szFileFullName, length);
    g_wszDumpFilename[length] = L'\0';

    std::FILE* const file = std::fopen(WideToUtf8(szFileFullName).c_str(), "wb");
    return file ? static_cast<void*>(file) : reinterpret_cast<void*>(static_cast<std::intptr_t>(-1));
#endif
}

std::uint32_t CountSetBits(std::uint64_t bitMask) {
    // Current L2Server.exe 0x00727394 tests every bit, beginning with bit 63.
    std::uint32_t count = 0;
    std::uint64_t bit = UINT64_C(0x8000000000000000);
    for (int index = 0; index < 64; ++index) {
        count += (bitMask & bit) != 0 ? 1U : 0U;
        bit >>= 1U;
    }
    return count;
}

wchar_t* L2Exception::GetProcessorArchitecture(_SYSTEM_INFO& systemInfo) {
    // Current L2Server.exe 0x00727360 returns these four exact labels for
    // PROCESSOR_ARCHITECTURE_INTEL/AMD64/IA32_ON_WIN64/other.
#if defined(_WIN32)
    switch (systemInfo.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_INTEL:
            return const_cast<wchar_t*>(L"x86");
        case PROCESSOR_ARCHITECTURE_AMD64:
            return const_cast<wchar_t*>(L"x64");
        case PROCESSOR_ARCHITECTURE_IA32_ON_WIN64:
            return const_cast<wchar_t*>(L"IA32 on Win64");
        default:
            return const_cast<wchar_t*>(L"UnknownArchitecture");
    }
#else
    // _SYSTEM_INFO is a Win32 carrier. Native builds keep the existing
    // Exception.cpp portability boundary by returning their host label.
    (void)systemInfo;
#if defined(__x86_64__) || defined(_M_X64)
    return const_cast<wchar_t*>(L"x64");
#elif defined(__i386__) || defined(_M_IX86)
    return const_cast<wchar_t*>(L"x86");
#else
    return const_cast<wchar_t*>(L"UnknownArchitecture");
#endif
#endif
}

void L2Exception::PrintNPCServerConnectionLog(void* file) {
    // Current L2Server.exe 0x00727698. The 20,000-WCHAR buffer is owned by
    // this same Exception.obj and filled by NpcServer.cpp.
    hprintf(file, L"[NPC Server Connection Log]\r\n");

    std::size_t length = 0;
    while (g_sNpcServerConnectionLog[length] != u'\0') {
        ++length;
    }
    if (length == 0) {
        hprintf(file, L"\tno connect\r\n");
        return;
    }

#if defined(_WIN32)
    hprintf(file, L"%s", reinterpret_cast<const wchar_t*>(g_sNpcServerConnectionLog));
#else
    const std::wstring contents = Utf16ToWide(std::u16string_view(g_sNpcServerConnectionLog, length));
    hprintf(file, L"%s", contents.c_str());
#endif
}

namespace {

void ReportThreadLocalWorldMap(void* file) {
    // Current L2Server.exe 0x00726aec. The five globals and each concrete
    // CTLPipeBase specialization are the live ThreadLocalWorldMap owners.
    // their public pipe/index structure is kept by the adjacent PDB and
    // every offset/stride below is confirmed by the current image.
    hprintf(file, L"[ThreadLocalWorldMap Pipe]\r\n");
    hprintf(
        file,
        L"\t(tid): XXX (Last - First = / "
        L"MAX_XXX_CONCURRENCY)\r\n"
    );

    for (int threadId = 0; threadId < CThreadManager::GetThreadCount(); ++threadId) {
        hprintf(file, L"\t(%02d) : ", threadId);

        if (g_ptlCreatureMap) {
            const auto& pipe = g_ptlCreatureMap->m_Pipe[static_cast<std::size_t>(threadId)];
            const std::int32_t differenceLast = pipe.m_nLastIndex.load(std::memory_order_relaxed);
            const std::int32_t differenceFirst = pipe.m_nFirstIndex.load(std::memory_order_relaxed);
            const std::int32_t first = pipe.m_nFirstIndex.load(std::memory_order_relaxed);
            const std::int32_t last = pipe.m_nLastIndex.load(std::memory_order_relaxed);
            hprintf(file, L"Creature(%d - %d = %d / %d) ", last, first, differenceLast - differenceFirst, 600000);
        }

        if (g_ptlUserMap) {
            const auto& pipe = g_ptlUserMap->m_Pipe[static_cast<std::size_t>(threadId)];
            const std::int32_t differenceLast = pipe.m_nLastIndex.load(std::memory_order_relaxed);
            const std::int32_t differenceFirst = pipe.m_nFirstIndex.load(std::memory_order_relaxed);
            const std::int32_t first = pipe.m_nFirstIndex.load(std::memory_order_relaxed);
            const std::int32_t last = pipe.m_nLastIndex.load(std::memory_order_relaxed);
            hprintf(file, L"User(%d - %d = %d / %d) ", last, first, differenceLast - differenceFirst, 50000);
        }

        if (g_ptlItemMap) {
            const auto& pipe = g_ptlItemMap->m_Pipe[static_cast<std::size_t>(threadId)];
            const std::int32_t differenceLast = pipe.m_nLastIndex.load(std::memory_order_relaxed);
            const std::int32_t differenceFirst = pipe.m_nFirstIndex.load(std::memory_order_relaxed);
            const std::int32_t first = pipe.m_nFirstIndex.load(std::memory_order_relaxed);
            const std::int32_t last = pipe.m_nLastIndex.load(std::memory_order_relaxed);
            hprintf(file, L"Item(%d - %d = %d / %d) ", last, first, differenceLast - differenceFirst, 112500);
        }

        if (g_ptlStaticObjectMap) {
            const auto& pipe = g_ptlStaticObjectMap->m_Pipe[static_cast<std::size_t>(threadId)];
            const std::int32_t differenceLast = pipe.m_nLastIndex.load(std::memory_order_relaxed);
            const std::int32_t differenceFirst = pipe.m_nFirstIndex.load(std::memory_order_relaxed);
            const std::int32_t first = pipe.m_nFirstIndex.load(std::memory_order_relaxed);
            const std::int32_t last = pipe.m_nLastIndex.load(std::memory_order_relaxed);
            hprintf(file, L"StaticObject(%d - %d = %d / %d) ", last, first, differenceLast - differenceFirst, 20002);
        }

        if (g_ptlAirShipMap) {
            const auto& pipe = g_ptlAirShipMap->m_Pipe[static_cast<std::size_t>(threadId)];
            const std::int32_t differenceLast = pipe.m_nLastIndex.load(std::memory_order_relaxed);
            const std::int32_t differenceFirst = pipe.m_nFirstIndex.load(std::memory_order_relaxed);
            const std::int32_t first = pipe.m_nFirstIndex.load(std::memory_order_relaxed);
            const std::int32_t last = pipe.m_nLastIndex.load(std::memory_order_relaxed);
            hprintf(file, L"AirShip(%d - %d = %d / %d) ", last, first, differenceLast - differenceFirst, 3000);
        }

        hprintf(file, L"\r\n");
    }
}

} // namespace

void L2Exception::GenerateExceptionReportFront(void* file) {
    // Current L2Server.exe 0x00729554. Every phase is mandatory in this
    // product, including the live TLM and NPC-server connection sections.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate crash summary")
    // Noise: per-section progress floods the last useful records during a crash.
    // the final report outcome identifies success or failure.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate crash summary")
    PrintCrashSummary(file);
    hprintf(file, L"\r\n");

    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate hardware status")
    // Noise: per-section crash-report progress. See the final outcome record.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate hardware status")
    PrintHardwareStatus(file);
    hprintf(file, L"\r\n");

    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate server info")
    // Noise: per-section crash-report progress. See the final outcome record.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate server info")
    PrintServerInfo(file);
    hprintf(file, L"\r\n");

    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate io buffer")
    // Noise: per-section crash-report progress. See the final outcome record.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate io buffer")
    PrintIOBuffer(file);
    hprintf(file, L"\r\n");

    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate object report")
    // Noise: per-section crash-report progress. See the final outcome record.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate object report")
    PrintObjectReport(file);
    hprintf(file, L"\r\n");

    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate tlm")
    // Noise: per-section crash-report progress. See the final outcome record.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate tlm")
    ReportThreadLocalWorldMap(file);
    hprintf(file, L"\r\n");

    // Log.Add(
    //     LOG_LOCAL,
    //     L"generate connection log")
    // Noise: per-section crash-report progress. See the final outcome record.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"generate connection log")
    PrintNPCServerConnectionLog(file);
    hprintf(file, L"\r\n");
}

void ReportGuardProfileToLog(const GuardProfileInfo& profile) {
    // Current L2Server.exe 0x00727af8. The adjacent PDB fixes the original
    // const-reference signature. The current ABI reads the same 0x28-byte
    // owner record directly.
    if (profile.nTotalCallCount <= 0) {
        return;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"\t %I64d \t %I64d \t %I64d "
    //     L"\t %I64d \t %s",
    //     profile.nTotalClock,
    //     profile.nTotalCallCount,
    //     profile.nTotalClock /
    //         profile.nTotalCallCount,
    //     profile.nAccTotalClock,
    //     profile.pszGuardName)
    // Previous: L2_LOG_INFO(Log,
    //     L"\t %I64d \t %I64d \t %I64d "
    //     L"\t %I64d \t %s",
    //     profile.nTotalClock,
    //     profile.nTotalCallCount,
    //     profile.nTotalClock / profile.nTotalCallCount,
    //     profile.nAccTotalClock,
    //     profile.pszGuardName)
    L2_LOG_INFO(
        Log,
        u"[guard profile] \"%s\": self time %I64d ns over %I64d calls (%I64d ns per call); including nested guards %I64d "
        u"ns.",
        profile.pszGuardName, profile.nTotalClock, profile.nTotalCallCount, profile.nTotalClock / profile.nTotalCallCount,
        profile.nAccTotalClock
    );
}

void ReportGuardProfile() {
    // Current L2Server.exe 0x00727b4c reloads the live seed after every row.
    // Log.Add(
    //     LOG_NORMAL,
    //     L"\t Only in guard \t Count "
    //     L"\t only guard / count "
    //     L"\t with lower guard \t GuardName")
    // Previous: L2_LOG_INFO(Log,
    //     L"\t Self time (ns) \t Calls "
    //     L"\t Self time per call (ns) "
    //     L"\t Time including nested guards (ns) \t Guard name")
    L2_LOG_INFO(Log, u"[guard profile] Reporting self and nested execution time by guard.");
    const std::int32_t count = GuardProfileHelper::RegisteredProfileCount();
    for (std::int32_t index = 0; index < count; ++index) {
        const GuardProfileInfo profile = GuardProfileHelper::Snapshot(index);
        ReportGuardProfileToLog(profile);
    }
}

void ReportCallStack(int threadId, LogType logType, bool sendMail) {
    // // Current L2Server.exe 0x007276ec. This function-local TLS counter is
    // // distinct from ReportCallStackNew's counter at 0x00727808.
    // if (threadId < ThreadID::MAX) {
    //     static thread_local int totalCall = 0.
    //     ++totalCall.
    //
    //     int order = 0
    //     const int depth =
    //         GuardHelper::getCallStackDepth(
    //             threadId)
    //     Log.Add(
    //         logType,
    //         L"[CallStack][tid:%d][tick:%d][%d] Begin",
    //         threadId,
    //         totalCall,
    //         0)
    //     for (int index = 0.
    //          index < depth.
    //          ++index) {
    //         ++order.
    //         Log.Add(
    //             logType,
    //             L"[CallStack][tid:%d][tick:%d][%d][%d] %s",
    //             threadId,
    //             totalCall,
    //             order,
    //             index,
    //             GuardHelper::getCallStack(
    //                 threadId,
    //                 index))
    //     }
    //     Log.Add(
    //         logType,
    //         L"[CallStack][tid:%d][tick:%d][%d] End",
    //         threadId,
    //         totalCall,
    //         order + 1)
    //
    //     if (sendMail) {
    //         SystemMail::sendCriticalCallstack(
    //             mailServer)
    //     }
    // }
    // Noise: the old End banner is extra with the frame count and numbered rows.
    WriteRecordedGuardStack(threadId, logType, sendMail, false);
}

void ReportCallStack(LogType logType, bool sendMail) {
    // Current L2Server.exe 0x007285cc is exactly this forwarding overload.
    ReportCallStack(CThreadManager::GetThreadID(), logType, sendMail);
}

void ReportCallStackNew(wchar_t* contentsName, LogType logType, bool sendMail) {
    // Current L2Server.exe 0x00727808. The current logical thread is read
    // before this overload's independent TLS counter is incremented.
    const int threadId = CThreadManager::GetThreadID();
    static thread_local int totalCall = 0;
    ++totalCall;

    int order = 0;
    // Log.Add(
    //     logType,
    //     L"[%s][CallStack][tid:%d][tick:%d][%d] Begin",
    //     contentsName,
    //     threadId,
    //     totalCall,
    //     0)
    // Log.Add(logType,
    //     "[call stack] Named report %d for worker %d: %s.",
    //     totalCall, threadId,
    //     WideToUtf8(contentsName ? contentsName : L"unnamed context").c_str())
    L2_LOG(
        Log, logType, "[call stack] Named report %d for worker %d: %s.", totalCall, threadId,
        WideToUtf8(contentsName ? contentsName : L"unnamed context").c_str()
    );

    const int depth = GuardHelper::getCallStackDepth(threadId);
    for (int index = 0; index < depth; ++index) {
        ++order;
        // Log.Add(
        //     logType,
        //     L"[%s][CallStack][tid:%d][tick:%d][%d][%d] %s",
        //     contentsName,
        //     threadId,
        //     totalCall,
        //     order,
        //     index,
        //     GuardHelper::getCallStack(
        //         threadId,
        //         index))
        const wchar_t* const frameName = GuardHelper::getCallStack(threadId, index);
        // Log.Add(logType,
        //     "[call stack] Named report %d, worker %d, frame %d/%d: %s",
        //     totalCall, threadId, order, depth,
        //     WideToUtf8(frameName ? frameName : L"unknown frame").c_str())
        L2_LOG(
            Log, logType, "[call stack] Named report %d, worker %d, frame %d/%d: %s", totalCall, threadId, order, depth,
            WideToUtf8(frameName ? frameName : L"unknown frame").c_str()
        );
    }
    // Log.Add(
    //     logType,
    //     L"[%s][CallStack][tid:%d][tick:%d][%d] End",
    //     contentsName,
    //     threadId,
    //     totalCall,
    //     order + 1)
    // Log.Add(logType,
    //     "[call stack] Named report %d for worker %d complete: %d frames.",
    //     totalCall, threadId, order)
    L2_LOG(Log, logType, "[call stack] Named report %d for worker %d complete: %d frames.", totalCall, threadId, order);

    if (sendMail) {
        SystemMail::sendCriticalCallstack(mailServer);
    }
}

void L2Exception::GenerateExceptionReport(_EXCEPTION_POINTERS* exceptionPointers) {
    // Current L2Server.exe 0x00729690. The NCGuard termination and the
    // exception-safe report-file finalizer are both mandatory target paths.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"Creating Minidump file")
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"Creating Minidump file")
    L2_LOG(
        Log, LOG_LOCAL, u"[crash report] Starting diagnostics; minidump enabled: %s.",
        g_bExceptionMakeMiniDump ? L"yes" : L"no"
    );
    if (g_bExceptionMakeMiniDump) {
        CreateMiniDumpFile(exceptionPointers, g_nExceptionMiniDumpType);
    }

    // Log.Add(
    //     LOG_LOCAL,
    //     L"Path completed [%s]",
    //     g_szLinErrorPath)
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"Path completed [%s]",
    //     g_szLinErrorPath)
    L2_LOG(Log, LOG_LOCAL, u"[crash report] Writing diagnostics to \"%s\".", g_szLinErrorPath);

    void* file = nullptr;
#if defined(_WIN32)
    file = ::CreateFileW(
        g_szLinErrorPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr
    );
    const bool open = static_cast<HANDLE>(file) != INVALID_HANDLE_VALUE;
    const unsigned error = open ? 0U : ::GetLastError();
#else
    const std::string nativePath = WideToUtf8(g_szLinErrorPath);
    file = std::fopen(nativePath.c_str(), "wb");
    const bool open = file != nullptr;
    const unsigned error = open ? 0U : static_cast<unsigned>(errno);
#endif

    if (!open) {
        // Log.Add(
        //     LOG_LOCAL,
        //     L"Error handle GenerateExceptionReport "
        //     L"[%s] error code [%d]",
        //     g_szLinErrorPath,
        //     error)
        // Previous: L2_LOG(Log,
        //     LOG_LOCAL,
        //     L"Error handle GenerateExceptionReport "
        //     L"[%s] error code [%d]",
        //     g_szLinErrorPath,
        //     error)
        L2_LOG(
            Log, LOG_LOCAL, u"[crash report] Cannot open \"%s\" for writing (OS error %u); report generation stopped.",
            g_szLinErrorPath, error
        );
        return;
    }

#if defined(_WIN32)
    ::SetEndOfFile(static_cast<HANDLE>(file));
#endif
    if (g_bInitedNCGuard) {
        const NCGuardServerResult result = TerminateNCGuardServer();
        if (result != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"End NCGuard Module fail[%d]",
            //     static_cast<int>(result))
            L2_LOG_ERROR(
                Log, "[crash report] NCGuard termination returned error %d. Crash-report generation continues.",
                static_cast<int>(result)
            );
        }
    }

    // Log.Add(
    //     LOG_LOCAL,
    //     L"trying exception")
    // Noise: internal report-stage marker. The start and final outcome are enough.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"trying exception")
    bool raiseExceptionInGenerateExceptionReport = false;

#if defined(_MSC_VER)
    __try {
        raiseExceptionInGenerateExceptionReport = true;
        // Log.Add(LOG_LOCAL, L"generate front")
        // Noise: internal report-stage marker. The final outcome identifies completion.
        // Previous: L2_LOG(Log, LOG_LOCAL, L"generate front")
        GenerateExceptionReportFront(file);
        // Log.Add(LOG_LOCAL, L"generate body")
        // Noise: internal report-stage marker. The final outcome identifies completion.
        // Previous: L2_LOG(Log, LOG_LOCAL, L"generate body")
        GenerateExceptionReportBody(file, exceptionPointers);
        // Log.Add(LOG_LOCAL, L"generate back")
        // Noise: internal report-stage marker. The final outcome identifies completion.
        // Previous: L2_LOG(Log, LOG_LOCAL, L"generate back")
        GenerateExceptionReportBack(file);
        WriteStackWalk(file, exceptionPointers);
        raiseExceptionInGenerateExceptionReport = false;
    } __finally {
        if (raiseExceptionInGenerateExceptionReport) {
            hprintf(
                file,
                L"########### Raise Exception In "
                L"GenerateExceptionReport!!! ############"
            );
        }
        hprintf(file, L"\r\n\r\n");
        const bool closed = ::CloseHandle(static_cast<HANDLE>(file)) != FALSE;
        // Log.Add(
        //     LOG_LOCAL,
        //     closed
        //         ? L"finaly close success"
        //         : L"finaly close failure")
        // Previous: L2_LOG(Log,
        //     LOG_LOCAL,
        //     closed
        //         ? L"finaly close success"
        //         : L"finaly close failure")
        L2_LOG(
            Log, LOG_LOCAL,
            closed && !raiseExceptionInGenerateExceptionReport ? L"[crash report] Diagnostics saved to \"%s\"."
                : closed ? L"[crash report] Incomplete diagnostics saved to \"%s\" after report generation failed."
                         : L"[crash report] Diagnostics were written to \"%s\", but closing the file failed.",
            g_szLinErrorPath
        );
    }
#else
    const auto finalizeReport = [&]() {
        if (raiseExceptionInGenerateExceptionReport) {
            hprintf(
                file,
                L"########### Raise Exception In "
                L"GenerateExceptionReport!!! ############"
            );
        }
        hprintf(file, L"\r\n\r\n");
#if defined(_WIN32)
        const bool closed = ::CloseHandle(static_cast<HANDLE>(file)) != FALSE;
#else
        const bool closed = std::fclose(static_cast<std::FILE*>(file)) == 0;
#endif
        // Log.Add(
        //     LOG_LOCAL,
        //     closed
        //         ? L"finaly close success"
        //         : L"finaly close failure")
        // Previous: L2_LOG(Log,
        //     LOG_LOCAL,
        //     closed
        //         ? L"finaly close success"
        //         : L"finaly close failure")
        L2_LOG(
            Log, LOG_LOCAL,
            closed && !raiseExceptionInGenerateExceptionReport ? L"[crash report] Diagnostics saved to \"%s\"."
                : closed ? L"[crash report] Incomplete diagnostics saved to \"%s\" after report generation failed."
                         : L"[crash report] Diagnostics were written to \"%s\", but closing the file failed.",
            g_szLinErrorPath
        );
    };

    try {
        raiseExceptionInGenerateExceptionReport = true;
        // Log.Add(LOG_LOCAL, L"generate front")
        // Noise: internal report-stage marker. The final outcome identifies completion.
        // Previous: L2_LOG(Log, LOG_LOCAL, L"generate front")
        GenerateExceptionReportFront(file);
        // Log.Add(LOG_LOCAL, L"generate body")
        // Noise: internal report-stage marker. The final outcome identifies completion.
        // Previous: L2_LOG(Log, LOG_LOCAL, L"generate body")
        GenerateExceptionReportBody(file, exceptionPointers);
        // Log.Add(LOG_LOCAL, L"generate back")
        // Noise: internal report-stage marker. The final outcome identifies completion.
        // Previous: L2_LOG(Log, LOG_LOCAL, L"generate back")
        GenerateExceptionReportBack(file);
        WriteStackWalk(file, exceptionPointers);
        raiseExceptionInGenerateExceptionReport = false;
    } catch (...) {
        finalizeReport();
        throw;
    }
    finalizeReport();
#endif
}

ExceptionFilterResult RecordExceptionInfo(_EXCEPTION_POINTERS* exceptionPointers) {
    // Current L2Server.exe 0x00729854.
    // Log.Add(
    //     LOG_LOCAL,
    //     L"[%s][%d] RecordExceptionInfo called",
    //     L"..\\Shared\\Exception.cpp",
    //     0x813)
#if defined(_WIN32)
    // Noise: OutputDebugStringA raises 0x40010006 and it is not a failure.
    // The original also logged "RecordExceptionInfo called" for it. Log the
    // debug text and its module instead, then continue as before.
    if (exceptionPointers->ExceptionRecord->ExceptionCode == 0x40010006U) {
        LogDebugOutputException(*exceptionPointers->ExceptionRecord);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
#endif
    L2_LOG(Log, LOG_LOCAL, u"[%s][%d] RecordExceptionInfo called", __WFILE__, __LINE__);

#if defined(_WIN32)
    const std::uint32_t exceptionCode = exceptionPointers->ExceptionRecord->ExceptionCode;

    static int BeenHere = 0;
    if (BeenHere != 0) {
        return previousFilter ? previousFilter(exceptionPointers) : EXCEPTION_CONTINUE_SEARCH;
    }
    BeenHere = 1;

    if (ExceptionTerminating == 0 && exceptionCode != 0xe0000001U) {
        L2Exception::GenerateExceptionReport(exceptionPointers);
        SystemMail::sendCrashMail(mailServer, g_bExceptionMakeMiniDump ? g_wszDumpFilename : nullptr);
    } else {
        // Log.Add(
        //     LOG_LOCAL,
        //     L"[%s][%d] RecordExceptionInfo "
        //     L"failed. Exception code [%d]",
        //     L"..\\Shared\\Exception.cpp",
        //     0x83e,
        //     static_cast<int>(exceptionCode))
        L2_LOG(
            Log, LOG_LOCAL,
            u"[%s][%d] RecordExceptionInfo "
            u"failed. Exception code [%d]",
            __WFILE__, __LINE__, static_cast<int>(exceptionCode)
        );
        if (ExceptionTerminating != 0) {
            // Log.Add(
            //     LOG_LOCAL,
            //     L"[%s][%d] "
            //     L"ExceptionTerminating is true.",
            //     L"..\\Shared\\Exception.cpp",
            //     0x842)
            L2_LOG(
                Log, LOG_LOCAL,
                u"[%s][%d] "
                u"ExceptionTerminating is true.",
                __WFILE__, __LINE__
            );
        }
    }
    ::ExitProcess(0);
    return EXCEPTION_CONTINUE_SEARCH;
#else
    (void)exceptionPointers;
    return 0;
#endif
}

void SetUpExceptionFilter() {
    // Current L2Server.exe 0x00729968.
#if defined(_WIN32)
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    ::GetVersionExW(&version);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (version.dwMajorVersion >= 6 && version.dwMinorVersion > 0) {
        // ::AddVectoredExceptionHandler(1, &RecordExceptionInfo)
        ::AddVectoredExceptionHandler(1, &VectoredExceptionInfo);
    }
    previousFilter = ::SetUnhandledExceptionFilter(&RecordExceptionInfo);
    std::signal(SIGABRT, &WindowsAbortHandler);
    PreventSetUnhandledExceptionFilter();
#else
    InstallPortableExceptionHandlers();
#endif
}

int exception_filter(_EXCEPTION_POINTERS* exceptionPointers) {
    // Current L2Server.exe 0x007299d8.
    CLogCritical theCritical;
    if (exceptionPointers) {
        L2Exception::GenerateExceptionReport(exceptionPointers);
    }
    return 1;
}

void violation() {
    // Current L2Server.exe 0x00729a48.
    if (!xfirst) {
        return;
    }
    xfirst = false;

#if defined(_MSC_VER)
    __try {
        ELOG(L"Intentional Exception\n");
        CrashImpl();
    } __except (exception_filter(GetExceptionInformation())) {
        ELOG(L" %S:%d(%S) exception\n", L2_SOURCE_FILE, __LINE__, __TIMESTAMP__);
        send_exception(false);
    }
#else
    ELOG(L"Intentional Exception\n");
    L2Exception::GenerateExceptionReport(nullptr);
    ELOG(L" %S:%d(%S) exception\n", L2_SOURCE_FILE, __LINE__, __TIMESTAMP__);
    send_exception(false);
#endif
}

void L2Exception::CreateExceptionSystem() {
    // Current L2Server.exe 0x00729aa0.
    CLogCritical::Initialize();
    MakeFileNameWithModuleName(g_szLinErrorPath, L"LinError.txt");
    CExceptionInit::g_timeStart = runtime::crt::Time32Now();
    SetUpExceptionFilter();
#if defined(_MSC_VER)
    ::_set_invalid_parameter_handler(&myInvalidParameterHandler);
#endif
}

void SetExceptionMailing() {
    // Current L2Server.exe 0x00726638: read at most 98 UTF-16 code units from
    // [Setting]/ExceptionMailing in .\l2server.ini, default to "True", and
    // publish true only for a case-insensitive exact match with "True".
#if defined(_WIN32)
    std::array<wchar_t, 108> temporary{};
    ::GetPrivateProfileStringW(
        L"Setting", L"ExceptionMailing", L"True", temporary.data(), 99,
        runtime::ConfigFile("./l2server.ini").wstring().c_str()
    );
    g_bExceptionMailing = ::_wcsicmp(temporary.data(), L"True") == 0;
#else
    runtime::Config profile;
    static_cast<void>(profile.Load(runtime::ConfigFile("./l2server.ini").string(), false));
    std::string value = profile.Get("Setting", "ExceptionMailing", "True");
    if (value.size() > 98) {
        value.resize(98);
    }
    const auto asciiLower = [](char character) {
        return character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A')) : character;
    };
    constexpr std::string_view expected = "true";
    g_bExceptionMailing = value.size() == expected.size() &&
        std::equal(value.begin(), value.end(), expected.begin(), [&](char left, char right) {
            return asciiLower(left) == right;
        });
#endif
}

namespace {

int LocalExceptionFilter(unsigned int exceptionCode, _EXCEPTION_POINTERS* exceptionPointers) {
    // Current L2Server.exe 0x00729afc. The current PDB fixes the anonymous
    // two-parameter source signature.
    (void)exceptionCode;
    // Log.Add(
    //     LOG_LOCAL,
    //     L"Local Exception Filter guard")
    // Noise: internal crash-handler checkpoint. Report generation logs its start and outcome.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"Local Exception Filter guard")
    CLogCritical theCritical;
    L2Exception::GenerateExceptionReport(exceptionPointers);
    // Log.Add(
    //     LOG_LOCAL,
    //     L"GenerateExceptionReport guard")
    // Noise: internal crash-handler checkpoint. The preceding report outcome is explicit.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"GenerateExceptionReport guard")
    SystemMail::sendCrashMail(mailServer, nullptr);
    // Log.Add(
    //     LOG_LOCAL,
    //     L"sendCrashMail guard")
    // Noise: internal crash-handler checkpoint. Mail delivery has its own diagnostics.
    // Previous: L2_LOG(Log,
    //     LOG_LOCAL,
    //     L"sendCrashMail guard")
    L2Exception::ResumeThreadExceptMe();
    return 1;
}

} // namespace

CrashInfo::CrashInfo() = default;
CrashInfo::~CrashInfo() = default;

void CrashInfo::SetReason(ReasonType reason) { m_ReasonList[static_cast<int>(reason)] = 1; }

void CrashInfo::Print(void* file) {
    // Current L2Server.exe 0x007264d0 advances the 300-byte reason state and
    // the 600-byte g_CrashReasonString table in lockstep for exactly 75
    // entries, printing every non-zero lane.
    for (std::size_t index = 0; index < std::size(m_ReasonList); ++index) {
        if (m_ReasonList[index] == 0) {
            continue;
        }
        hprintf(file, L"\tReason : %s \r\n", g_CrashReasonString[index]);
    }
}

void CrashImpl() {
#if defined(_WIN32)
    *reinterpret_cast<volatile unsigned char*>(0) = 1;
#else
    ::raise(SIGSEGV);
#endif
}

void L2Exception::OnException() {
    // Current L2Server.exe 0x00729bc0. Raw instructions fix reason 26,
    // force-crash publication and the final null write.
    ForceCrash(CrashInfo::THROW_EXCEPTION);
}

bool ExceptionMailing() {
    // Current L2Server.exe 0x004c0008: mov al, 1, then ret.
    return true;
}

void myInvalidParameterHandler(
    const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned line, std::uint64_t reserved
) {
    // Current L2Server.exe 0x007279e0. Raw instructions place the force-crash
    // byte store after the deliberate null write, even though that store is
    // unreachable in the shipping process.

    g_bInvalidParameter = true;
    g_sInvalidParamExpr = expression;
    RecordCrashLocation();
    g_CrashInfo.SetReason(CrashInfo::INVALID_PARAMETER_ERROR);
    CrashImpl();
    bForceCrash = true;
}
#endif
