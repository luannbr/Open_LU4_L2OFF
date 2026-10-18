#include "Shared/src/ThreadManager.h"

#include "L2Core/src/inc/FileUtils.h"
#include "Shared/src/Runtime/OperatorLog.h"

namespace {

// The PE .tls image initializes g_tlThreadID to zero. Every named target
// thread then overwrites it at its entry point.
thread_local int g_tlThreadID = ThreadID::IO_0;

void RegisterLogThreadRole(int threadId) noexcept {
    static constexpr const char* names[] = {
        "io-0",
        "io-1",
        "io-2",
        "io-3",
        "io-4",
        "io-5",
        "io-6",
        "io-7",
#if defined(L2_BUILD_L2SERVER)
        "io-8",
        "io-9",
        "io-10",
        "io-11",
        "io-12",
        "io-13",
        "io-14",
        "io-15",
        "captcha",
        "goods-inventory",
        "relay",
        "raid",
        "premium",
        "community",
        "petition",
        "gip",
        "log",
        "perfmon",
        "main",
        "listen",
        "channel-chat",
        "connect",
        "account",
        "olympiad",
#elif defined(L2_BUILD_CACHED)
        "io-8",
        "io-9",
        "io-10",
        "io-11",
        "io-12",
        "io-13",
        "io-14",
        "io-15",
        "file-server",
        "gip",
        "log",
        "perfmon",
        "main",
        "listen",
#else
        "log",
        "perfmon",
        "main",
        "listen",
#endif
    };
    static_assert(std::size(names) == ThreadID::MAX);
    runtime::logging::SetThreadName(threadId >= 0 && threadId < ThreadID::MAX ? names[threadId] : "");
}

} // namespace

std::array<std::atomic<void*>, ThreadID::MAX> CThreadManager::g_hThreadHandle{};
std::atomic<int> CThreadManager::g_nThreadQuantity{0};

#if defined(L2_BUILD_L2SERVER)
// Current L2Server.exe ThreadManager.obj 0x20d87240..0x20d8894f. The PE image
// and its only initializer establish an all-zero initial state.
int g_nSkillActorDBID[ThreadID::MAX]{};
int g_nSkillID[ThreadID::MAX]{};
int g_nSkillTargetCount[ThreadID::MAX]{};
int g_nLeaveWorldPostCount[ThreadID::MAX]{};
int g_nLeaveWorldPostDBID[ThreadID::MAX][32]{};
int g_nUserGarbageEnterTime[ThreadID::MAX]{};
int g_nUserGarbageUserDBID[ThreadID::MAX]{};
int g_nUserGarbageRefCount[ThreadID::MAX]{};
l2server46_20160218_CheckBuffInfo g_CheckBuffInfo[ThreadID::MAX];
// Current L2Server.exe root 0x00db6fcc zeroes the complete physical
// g_CheckBuffInfo@0x20d88750 array as 32 consecutive 0x10-byte objects.
#endif

void l2server46_20160218_CheckBuffInfo::Clear() {
    // Current L2Server.exe 0x00aeaa50.
    m_nUserDBID = 0;
    m_nAbnormalType = 0;
    m_nInfectedSkillId = 0;
    m_nLoopCount = 0;
}

void l2server46_20160218_CheckBuffInfo::WriteCheckBuffInfo(
    int userDBID, int abnormalType, int infectedSkillId, int loopCount
) {
    // Current L2Server.exe 0x00aeaa5c. The store order is observable in the
    // target: fields +0/+4/+c are written before field +8.
    m_nUserDBID = userDBID;
    m_nAbnormalType = abnormalType;
    m_nLoopCount = loopCount;
    m_nInfectedSkillId = infectedSkillId;
}

void l2server46_20160218_CheckBuffInfo::ReportCheckBuffInfo(void* file) const {
    // Current L2Server.exe 0x00aeaa70.
    hprintf(
        file,
        L"\tCheckBuffInfo userDBID[%d] abnormalType[%d] "
        L"infectedSkillId[%d], loopCount[%d]",
        m_nUserDBID, m_nAbnormalType, m_nInfectedSkillId, m_nLoopCount
    );
    hprintf(file, L"\r\n");
}

int CThreadManager::GetMainThreadID() { return ThreadID::MAIN; }

void CThreadManager::SetThisThreadAsMainThread() {
    // Current Cached.exe 0x0059acbc writes logical thread ID 20 to TLS.
    g_tlThreadID = ThreadID::MAIN;
    RegisterLogThreadRole(g_tlThreadID);
}

void CThreadManager::SetThisThreadAsWorldThread() {
    g_tlThreadID = ThreadID::IO_0;
    RegisterLogThreadRole(g_tlThreadID);
}

void CThreadManager::SetThisThreadAsListenThread() {
    g_tlThreadID = ThreadID::LISTEN;
    RegisterLogThreadRole(g_tlThreadID);
}

bool CThreadManager::IsListenThread() {
    // Current Cached.exe 0x0059ad00 compares the TLS value with 21.
    return g_tlThreadID == ThreadID::LISTEN;
}

bool CThreadManager::IsMainThread() {
    // Current Cached.exe 0x0059acdc compares the TLS value with 20.
    return g_tlThreadID == ThreadID::MAIN;
}

bool CThreadManager::IsWorldThread() { return g_tlThreadID == ThreadID::IO_0; }

int CThreadManager::GetThreadCount() {
    // Current Cached.exe 0x0059ad20 returns the process thread-count scalar.
    return g_nThreadQuantity.load(std::memory_order_relaxed);
}

void CThreadManager::SetThreadCount(int count) {
    // Current Cached.exe 0x0059ad28 stores the process thread-count scalar.
    g_nThreadQuantity.store(count, std::memory_order_relaxed);
}

void CThreadManager::RegisterThisThread(int threadId) {
    // Current Cached.exe 0x0059ad30 writes the supplied logical ID to TLS.
    g_tlThreadID = threadId;
    RegisterLogThreadRole(g_tlThreadID);
}

void CThreadManager::SetThread(int threadId, void* handle) {
    // Current Cached.exe 0x0059ad50 stores the handle at index threadId.
    g_hThreadHandle[static_cast<std::size_t>(threadId)].store(handle, std::memory_order_relaxed);
}

void* CThreadManager::GetThread(int threadId) {
    // Current Cached.exe 0x0059ad60 loads the handle at index threadId.
    return g_hThreadHandle[static_cast<std::size_t>(threadId)].load(std::memory_order_relaxed);
}

int CThreadManager::GetThreadID() {
    // Current Cached.exe 0x0059ad70 returns the current TLS logical ID.
    return g_tlThreadID;
}
