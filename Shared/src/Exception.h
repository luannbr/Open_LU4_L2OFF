#pragma once

#include "Shared/src/Log.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <source_location>
#include <string>
#include <type_traits>

struct _EXCEPTION_POINTERS;
struct _CONTEXT;
struct _SYSTEM_INFO;
#if defined(_WIN32)
struct _SYSTEM_LOGICAL_PROCESSOR_INFORMATION;
struct _RTL_CRITICAL_SECTION;
#endif

// Each product initializes this Windows time32 value during process startup.
// Current L2Server.exe publishes it at 0x0080a97f into Exception.obj storage
// 0x04d26fe8. The public name survives in the older PDB.
extern std::int32_t server_up_time;
#if defined(L2_BUILD_L2SERVER)
// Current Exception.obj storage 0x04d26fec. Startup and the daylight-saving
// builder callback pass this exact signed dword to the CRT daylight query.
extern std::int32_t g_nDaylightSaving;
// Current L2Server.exe 0x00726638, original Shared/Exception.cpp owner.
void SetExceptionMailing();
#endif

// Exception.obj/PDB classes. Current L2NPC constructs eight 0x50-byte
// LastAction records. Current L2Server constructs sixteen at 0x04d32120.
// The product-selected extent is part of the original shared-source build
// structure instead of a single cross-product layout.
class ServerContext {
public:
    ServerContext() noexcept;

    void Init() noexcept;

    std::atomic<std::int32_t> User;
    std::atomic<std::int32_t> Npc;
    std::atomic<std::int32_t> Item;
    std::atomic<std::int32_t> UserSocket;
    std::atomic<std::int32_t> InZone;
    std::atomic<std::int32_t> Trade;
    std::atomic<std::int32_t> Auction;
    std::atomic<std::int32_t> Bridge;
    std::atomic<std::int32_t> AirShip;
    std::atomic<std::int32_t> NotReleasedUser;
    std::atomic<std::int32_t> NotReleasedNpc;
    std::atomic<std::int32_t> NotReleasedItem;
    std::atomic<std::int32_t> NotReleasedUserSocket;
    std::atomic<std::int32_t> NotReleasedDoor;
    std::atomic<std::int32_t> NotReleasedInZone;
    std::atomic<std::int32_t> NotReleasedTrade;
    std::atomic<std::int32_t> NotReleasedAuction;
    std::atomic<std::int32_t> NotReleasedBridge;
    std::atomic<std::int32_t> NotReleasedAirShip;
    std::atomic<std::int32_t> ResidenceDecoVisible;
    std::atomic<std::int32_t> AirShipSteering;
    std::atomic<std::int32_t> AirShipPart;
    std::atomic<std::int32_t> BattleCamp;
    std::atomic<std::int32_t> Chair;
    std::atomic<std::int32_t> ControlTower;
    std::atomic<std::int32_t> Door;
    std::atomic<std::int32_t> EventController;
    std::atomic<std::int32_t> EventMatch;
    std::atomic<std::int32_t> FortressFlagpole;
    std::atomic<std::int32_t> ObserverRemains;
    std::atomic<std::int32_t> SignBoard;
    std::atomic<std::int32_t> Statue;
    std::atomic<std::int32_t> TeamBattleFlag;
    std::atomic<std::int32_t> HolyThing;
    std::atomic<std::int32_t> OwnThing;
    std::atomic<std::int32_t> EtcStaticObject;
    std::atomic<std::int32_t> MaxSerializerTime;
};

class LastAction {
public:
    LastAction() noexcept;

    volatile std::int32_t m_SkillID;
    volatile std::int32_t m_AlteredSkillID;
    volatile std::int32_t m_ItemClassID;
    volatile std::int32_t m_PlayerClassID;
    char16_t m_Name[32];
};


extern ServerContext g_ServerContext;
#if defined(L2_BUILD_L2SERVER)
extern char16_t g_sNpcServerConnectionLog[20000];
extern LastAction g_LastAction[16];
#else
extern LastAction g_LastAction[8];
#endif
#if defined(L2_BUILD_CACHED)
// Current Cached.exe storage 0x02a2ea60: one counter per logical thread.
// RequestLoadDroidNPCAppearance resets/increments the selected element while
// its account character map is scanned. Shared/Exception.cpp prints that
// live value in the matching guard report.
extern std::int32_t g_nDroidNPCCharMapLoop[];
#endif
extern char* devMailServer;

int GetElapsedTime();
wchar_t* GetElapsedTimeString();
wchar_t* GetCurrentTimeString();
wchar_t* GetServerUpTimeString();
int GetL2ProtocolVersion();

class CrashInfo {
public:
    // The old PDB ends at 65. The current 2018 image's 600-byte pointer
    // table and 300-byte reason array add ten entries and are authoritative.
    enum ReasonType : int {
        FORCE_CRASH = 0,
        LOW_MEMORY = 1,
        LOCK_MISMATCH = 2,
        LOW_IO_BUFFER = 3,
        DB_SOCKET_IS_CLOSED = 4,
        AUTH_SOCKET_IS_CLOSED = 5,
        TRYING_TO_UNLOCK_THE_UNLOCKED_LOCK = 6,
        QUEUE_IS_EMPTY = 7,
        NEVER_CALL_OPERATOR_DELETE = 8,
        QUEUE_IS_FULL = 9,
        THREADPIPE_OVERFLOW = 10,
        INVALID_THREAD_NO = 11,
        INVALID_CLASS = 12,
        INVALID_INDEX = 13,
        BY_QUIT_MENU = 14,
        BY_SHUTDOWN_MENU = 15,
        BY_CRASH_REPORT = 16,
        BY_EXIT_MENU = 17,
        BY_DESTROY_EVENT = 18,
        BY_CRASH_COMMAND = 19,
        BY_DBLIST_ERROR = 20,
        BY_DEADLOCK_CHECKER = 21,
        BY_SERVER_STOPPER_REQUEST = 22,
        BY_SHUTDOWN_MESSAGE = 23,
        INVALID_SQL_POOL = 24,
        NETWORK_DISCONNECT_FROM_DB_TO_CACHED = 25,
        THROW_EXCEPTION = 26,
        LOCK_STACK_IS_NOT_EMPTY = 27,
        CLOSE_IO_THREAD = 28,
        IOCP_CRASH_MESSAGE = 29,
        PARSING_ERROR_EXPDATA_FILE = 30,
        METERED_SECTION_HANDLE_IS_NULL = 31,
        PROCEEDED_OBJECT_IS_OVERFLOW = 32,
        SHARED_FACTORY_IS_EMPTY = 33,
        SHARED_FACTORY_INDEX_IS_INVALID = 34,
        SHARED_FACTORY_FREE_INDEX_IS_INVALID = 35,
        COBJECT_IS_NULL = 36,
        LOAD_FAILURE_FOR_L2SERVER_INI = 37,
        CACHE_VERSION_FAILED = 38,
        CACHE_VERSION_IS_DIFFERENT = 39,
        CACHEDSCRIPT_IS_NOT_UPDATED = 40,
        GEOLOADING_IS_FAILED = 41,
        INSPECTOR_DATA_IS_INVALID = 42,
        NEW_INSPECTOR_DATA_IS_INVALID = 43,
        CONTAINER_INDEX_OVERFLOWED = 44,
        UNITTEST_TOO_MANY_USERS = 45,
        GAMEGUARD_LOADING_IS_FAILED = 46,
        SHARED_PLATFORM_INFO_IS_INVALID = 47,
        ITEM_CREATION_IS_FAILED = 48,
        Z_DIVIDED_NUM_OVERFLOWED = 49,
        Z_DIVIDED_NUM_IS_INVALID = 50,
        NPC_M_DATA_IS_NULL = 51,
        NPC_AI_LOADING_FAILED = 52,
        SERVER_TYPE_MISMATCH = 53,
        SERVER_PACKET_SIZE_IS_INVALID = 54,
        SERVER_UNKNOWN_PROTOCOL = 55,
        CHECK_SIZE_INT = 56,
        SERVER_SOCKET_CLOSE = 57,
        SERVER_BOOT_TIME_IS_OVER_49_DAYS = 58,
        LEX_YACC_FAILED = 59,
        MISMATCH_NPC_PROTOCOL = 60,
        MISMATCH_SHARED_FACTORY_VERSION = 61,
        PARSING_ERROR_STATUEDATA_FILE = 62,
        MISMATCH_NPC_HEADER_VERSION = 63,
        MISMATCH_SHARED_DATA_SIZE = 64,
        PARSING_ERROR_HAIREXDATA_FILE = 65,
        PARSING_ERROR_FACEEXDATA_FILE = 66,
        PARSING_ERROR_COLOREXDATA_FILE = 67,
        PARSING_ERROR_DBJOB_FILE = 68,
        NP_SOCKET_ERROR = 69,
        INVALID_PARAMETER_ERROR = 70,
        PARSING_ERROR_EVENTDATA_FILE = 71,
        INVALID_SCRIPT_INTEGRITY = 72,
        CACHED_DB_VERSION_NOT_MATCHED = 73,
        SQL_FUNCTION_SEQUENCE_ERROR = 74,
        MAX_CRASH_REASON = 75,
    };

    CrashInfo();
    ~CrashInfo();

    void SetReason(ReasonType reason);
    void Print(void* file);

private:
    std::int32_t m_ReasonList[MAX_CRASH_REASON]{};
};


extern CrashInfo g_CrashInfo;
extern bool g_bInvalidParameter;
extern bool g_IsBySystemManager;
extern bool bForceCrash;
extern bool bCanConnectCacheServer;
extern const wchar_t* g_sInvalidParamExpr;
extern wchar_t g_CrashFunctionName[1024];
extern wchar_t g_CrashFileName[1024];
extern wchar_t g_CrashLineNo[16];
extern wchar_t g_szCrashLogPath[260];
#if defined(L2_BUILD_L2SERVER)
// Exception.obj buffer written by current L2Server.exe
// L2Exception::CreateFileWithModuleName at 0x00727fa4. It is distinct from
// g_szCrashLogPath, which carries the completed LinError report path.
extern wchar_t g_wszDumpFilename[260];
#endif
// Current L2Server.exe LinError.txt buffer at 0x04d31e60 (0x104 UTF-16
// elements). Its semantic name is recovered from the current producer and
// consumers because the adjacent PDB predates this storage.
extern wchar_t g_szLinErrorPath[0x104];
extern int g_nExceptionMiniDumpType;
extern bool g_bExceptionMailing;
// Current L2Server.exe Exception.obj report-termination dword 0x04d26fe0.
// This is distinct from ioc.cpp's g_bTerminating.
extern int ExceptionTerminating;


// shared/Exception.h. These exact records are consumed by both
// shared/Exception.cpp and l2npc/ScriptEngine.cpp. The PDB section
// contributions place all three backing arrays in Exception.obj. Current
// L2NPC.exe has no CRT initializer for g_ScriptContext: every dormant record
// starts as zero-filled BSS, and RunNPCScript publishes the -1 sentinels only
// while entering or clearing a live record.
struct ScriptContext {
    bool bSet = false; // +0x00
    wchar_t* pszHandlerName = nullptr; // +0x08
    int nEventID = 0; // +0x10
    wchar_t* pszEventName = nullptr; // +0x18
    int nPC = 0; // +0x20
    int nSP = 0; // +0x24
    char optr = 0; // +0x28
    wchar_t* pszFuncName = nullptr; // +0x30
};


struct RunScriptCallStatus {
    int nRecursiveCount = 0;
    int nLastErrno = 0;

    RunScriptCallStatus() = default;
};


// Exception.obj/PDB HandlerInfo. RunNPCScript keeps the last 1,000 nested
// calls for each of the eight IO threads. Windows wchar_t is two bytes, so a
// UTF-16 array keeps the target's 0x48-byte record on every host.
struct HandlerInfo {
    HandlerInfo() noexcept : m_lastEventID(0) {}

    int m_lastEventID; // +0x00
    char16_t m_wszNpcName[32]; // +0x04
    std::uint32_t m_callTime; // +0x44
};


extern ScriptContext g_ScriptContext[8][1000];
extern wchar_t* g_ScriptNPC[8];
extern RunScriptCallStatus g_RunScriptCallStatus[8];
extern HandlerInfo g_lastcalledEventHandler[8][1000];

// Exact PDB owner and current L2NPC.exe 0x0042c530.
void ReportScriptContext(LogType logType);


// Donor-PDB-complete CBuildDate. Exact current constructors are L2NPC
// 0x0042dd90 and L2Server 0x00729120. Product build times remain separate.
// Its final member is 1024 target WCHARs, represented as UTF-16 on hosts
// where native wchar_t is 32-bit so the recovered object remains 0x818.
class CBuildDate {
public:
    static CBuildDate& inst();

    const wchar_t* BuildDate() const;
    const wchar_t* BuildTime() const;
    const wchar_t* BuildDateTime() const;
    const wchar_t* BuildArchive() const;

private:
    using ArchiveChar = std::conditional_t<sizeof(wchar_t) == 2, wchar_t, char16_t>;

    CBuildDate();

    const wchar_t* buildDate;
    const wchar_t* buildTime;
    const wchar_t* buildDateTime;
    ArchiveChar buildArchive[1024];
};


// Exact PDB class from Exception.cpp. Current L2NPC.exe constructs this
// 40-byte record in L2Exception::PrintHardwareStatus and fills it at
// 0x0042cd7c.
class CPUInfo {
public:
    struct CpuCache {
        int m_LineSize;
        int m_TotalSize;
    };

    CPUInfo();

    void Init();
    wchar_t* PrintHyperThreadResult();
    int GetNUMANodeCount();
    int GetProcessorCoreCount();
    int GetLogicalProcessorCount();
    std::wstring PrintCacheLineSize();
    std::wstring PrintCacheTotalSize();

private:
#if defined(_WIN32)
    bool CollectCPUInformation(_SYSTEM_LOGICAL_PROCESSOR_INFORMATION*& buffer, unsigned long& returnLength);
#endif

    bool m_htEnabled; // +0x00
    std::uint32_t m_logicalProcessorCount; // +0x04
    std::uint32_t m_numaNodeCount; // +0x08
    std::uint32_t m_processorCoreCount; // +0x0c
    CpuCache m_cache[3]; // +0x10
};


// Exact PDB global. Current L2NPC.exe 0x0042bf94 and current L2Server.exe
// 0x00727394.
std::uint32_t CountSetBits(std::uint64_t bitMask);

// Exact PDB class from Exception.cpp. In the shipping image this is a
// process-wide recursive Win32 critical section. Portable hosts keep the
// same RAII lock lifetime with std::recursive_mutex.
class CLogCritical {
public:
    CLogCritical();
    ~CLogCritical();

    static void Initialize();
    static void Deinitialize();

private:
#if defined(_WIN32)
    static _RTL_CRITICAL_SECTION m_CriticalSection;
#else
    static std::recursive_mutex m_CriticalSection;
#endif
};


// Exact PDB global. Current L2NPC.exe 0x0042b354 writes one byte
// through a null pointer.
void CrashImpl();

// The original server expanded one crash macro at each forced crash. It wrote
// "Function : " __FUNCSIG__, "File : " __FILE__ and ", Line : " __LINE__ into
// the crash report buffers, set the reason and called CrashImpl. ForceCrash
// does the same with the location of its caller.
void ForceCrash(CrashInfo::ReasonType reason, std::source_location location = std::source_location::current());

// Write only the crash location part of the report: function, file and line.
void RecordCrashLocation(std::source_location location = std::source_location::current());

// The same crash for a site that already has its function text and location.
void ForceCrash(CrashInfo::ReasonType reason, const wchar_t* functionName, const wchar_t* fileName, int line);

// Exact Shared/Exception.cpp owner. Current L2Server.exe 0x00726ae0 clears
// both exception-report and L2Core minidump emission before forced shutdown.
void DontMakeMiniDump();

// Exact PDB global. Current L2NPC.exe 0x0042b668 and current L2Server.exe
// 0x00726358.
char* UnicodeToMultibyte(const wchar_t* pszStr);

// Current build guard strings and CBuildDate's archive marker place this
// class in ..\Shared\Exception.cpp. All methods are static in the target.
class L2Exception {
public:
    // Exact PDB name. Current L2Server.exe 0x00729aa0.
    static void CreateExceptionSystem();

    // Exact PDB name. Current Cached.exe 0x004ccb3c and current L2Server.exe
    // 0x00729bc0. This is the deliberate DirectCommandBox crash path, not the
    // unhandled-exception callback.
    static void OnException();

    // Exact PDB names. Current L2Server.exe 0x00727218/0x007272bc.
    static void SuspendThreadExceptMe();
    static void ResumeThreadExceptMe();

    // Exact PDB name. Current L2NPC.exe 0x0042bf60 and current L2Server.exe
    // 0x00727360.
    static wchar_t* GetProcessorArchitecture(_SYSTEM_INFO& systemInfo);

    // Exact PDB name. Current L2Server.exe 0x00727698.
    static void PrintNPCServerConnectionLog(void* file);

    // Current L2NPC.exe 0x0042b920.
    static void GenerateExceptionReportBody(void* file, _EXCEPTION_POINTERS* exceptionPointers);

    // Current L2NPC.exe 0x0042d890. Current L2Server.exe 0x00728be4.
    static void PrintCrashSummary(void* file);

    // Current L2NPC.exe 0x0042bfbc. Current L2Server.exe 0x007273bc.
    static void PrintObjectReport(void* file);

    // Current L2NPC.exe 0x0042ce48.
    static void PrintIOBuffer(void* file);

    // Current L2NPC.exe 0x0042dbc8.
    static void PrintHardwareStatus(void* file);

    // Current L2NPC.exe 0x0042df6c. Current L2Server.exe 0x007292fc.
    static void PrintServerInfo(void* file);

    // Current L2Server.exe 0x00729554.
    static void GenerateExceptionReportFront(void* file);

    // Current L2Server.exe 0x00728208.
    static void GenerateExceptionReportBack(void* file);

    // Current L2NPC.exe 0x0042e2f0.
    static void GenerateExceptionReport(_EXCEPTION_POINTERS* exceptionPointers);

    // Current L2Server.exe 0x00727e74.
    static void MakeFileNameWithModuleName(wchar_t szFileFullName[], const wchar_t* pPostFix);

    // Current L2Server.exe 0x00727fa4. The opaque return value is a Win32 HANDLE
    // in the target and an open std::FILE* at the POSIX portability boundary.
    static void* CreateFileWithModuleName(wchar_t szFileFullName[], wchar_t* pPostFix);

    // Current L2NPC.exe 0x0042cb4c. Current L2Server.exe 0x0072808c.
    static void CreateMiniDumpFile(_EXCEPTION_POINTERS* ep, int nType);
};

// Exact PDB class. The current L2NPC constructor at 0x0042e598 and CacheD
// owner at 0x004cc8c4 install the exception filters and do not access object
// storage.
class CExceptionInit {
public:
    static std::int32_t g_timeStart;

    CExceptionInit();
    // The adjacent PDB still contains a restoring destructor, but the
    // shipping image's initializer at 0x0042e6d0 registers no atexit
    // callback. The current class therefore has a trivial destructor.
    ~CExceptionInit() = default;
};


// Semantic name for current L2NPC.exe 0x0042e6d0 and Cached.exe 0x004cca1c.
// the original source name is not present in either image. WinMain calls this
// sequence (before argument inspection in CacheD). It is not a CRT
// initializer.
void InitializeExceptionRuntime();

// Exact PDB names. Current L2NPC.exe 0x0042e484/0x0042e608.
#if defined(_WIN32)
using ExceptionFilterResult = long;
#else
using ExceptionFilterResult = std::int32_t;
#endif

ExceptionFilterResult RecordExceptionInfo(_EXCEPTION_POINTERS* exceptionPointers);
// Exact PDB name. Current L2Server.exe 0x00729968.
void SetUpExceptionFilter();
int exception_filter(_EXCEPTION_POINTERS* exceptionPointers);

// Exact PDB global. Current L2NPC.exe 0x0042e678 and Cached.exe 0x004cc9c4.
// Each target raises and catches one access violation to capture
// the failing IOCP path.
void violation();

void myInvalidParameterHandler(
    const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned line, std::uint64_t reserved
);

void send_exception(bool exitProcess);
// Portable addition. Call it in a catch block that stops the server. On
// Windows it writes LinError.txt and the minidump for the caught C++
// exception and ends the process. Elsewhere it does nothing.
void ReportCaughtExceptionAsCrash();
bool ExceptionMailing();
void ELOG(const wchar_t* format, ...);

// Exact PDB names. Current L2NPC.exe 0x0042c5ec/0x0042d1cc and current
// L2Server.exe 0x00727b9c/0x00728924.
void WriteStackDetails(void* file, _CONTEXT* context);
void WriteStackWalk(void* file, _EXCEPTION_POINTERS* exceptionPointers);

// Exception.obj owns this process-sized guard timestamp array. The remaining
// per-thread report arrays are declared by their true ThreadManager.h owner.
extern std::uint32_t g_dwGuardStartTick[];

// Exact PDB overloads. Current L2Server.exe 0x007285cc/0x007276ec.
void ReportCallStack(LogType logType, bool sendMail);
void ReportCallStack(int threadId, LogType logType, bool sendMail);
namespace runtime::log {
// One guard-stack report, retaining its local-file copy when LogD is connected.
void ReportCallStackWithLocalCopy();
} // namespace runtime::log

// Exact PDB name. Current L2Server.exe 0x00727808.
void ReportCallStackNew(wchar_t* contentsName, LogType logType, bool sendMail);

// Shared Exception.cpp source name, corroborated by NASC64_2015. Current
// L2NPC.exe 0x0042c248 emits one diagnostic record per held lock.
void ReportLockStack(int threadId, const wchar_t* context, LogType logType);
void ReportLockStack(const wchar_t* context, LogType logType);

// Exact PDB global. Current L2Server.exe 0x00727b4c. Its const-reference row
// helper is file-local in the original Exception.cpp owner at 0x00727af8.
void ReportGuardProfile();
