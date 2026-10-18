#pragma once

#include <array>
#include <atomic>
#include <cstddef>


// Recovered from Shared/ThreadManager.cpp. The original Shared project
// selected a different ThreadIndices enum for each executable. Keep that
// compile-time product distinction in this one physical header, just as the
// shipping source did.
namespace ThreadID {
enum Enum : int {
    IO_0 = 0,
    IO_1 = 1,
    IO_2 = 2,
    IO_3 = 3,
    IO_4 = 4,
    IO_5 = 5,
    IO_6 = 6,
    IO_7 = 7,
#if defined(L2_BUILD_L2SERVER)
    IO_8 = 8,
    IO_9 = 9,
    IO_10 = 10,
    IO_11 = 11,
    IO_12 = 12,
    IO_13 = 13,
    IO_14 = 14,
    IO_15 = 15,
    CAPTCHA = 16,
    GOODS_INVENTORY = 17,
    RELAY = 18,
    RAID = 19,
    PREMIUM = 20,
    L2COMM = 21,
    PETITION = 22,
    GIP = 23,
    LOG = 24,
    PERFMON = 25,
    MAIN = 26,
    LISTEN = 27,
    CHANNEL_CHATTING = 28,
    CONNECT = 29,
    ACCOUNT = 30,
    OLYMPIAD = 31,
    MAX = 32,
#elif defined(L2_BUILD_CACHED)
    IO_8 = 8,
    IO_9 = 9,
    IO_10 = 10,
    IO_11 = 11,
    IO_12 = 12,
    IO_13 = 13,
    IO_14 = 14,
    IO_15 = 15,
    FILE_SERVER = 16,
    GIP = 17,
    LOG = 18,
    PERFMON = 19,
    MAIN = 20,
    LISTEN = 21,
    MAX = 22,
#else
    LOG = 8,
    PERFMON = 9,
    MAIN = 10,
    LISTEN = 11,
    MAX = 12,
#endif
    // Common transport storage must accommodate the widest selected product
    // even in archive objects compiled without an executable macro. Current
    // L2Server has a 32-entry handle table and a parallel 32-entry logging
    // recursion array.
    PORTABLE_CAPACITY = 32,
};
} // namespace ThreadID

// Exact Shared/ThreadManager.cpp class. TotalChecker's type record fixes the
// private four-int layout and public method qualifiers. Current L2Server.exe
// fixes the three bodies at 0x00aeaa50..0x00aeaab0.
class l2server46_20160218_CheckBuffInfo {
public:
    l2server46_20160218_CheckBuffInfo() : m_nUserDBID(0), m_nAbnormalType(0), m_nInfectedSkillId(0), m_nLoopCount(0) {}

    void Clear();
    void WriteCheckBuffInfo(int userDBID, int abnormalType, int infectedSkillId, int loopCount);
    void ReportCheckBuffInfo(void* file) const;

private:
    int m_nUserDBID;
    int m_nAbnormalType;
    int m_nInfectedSkillId;
    int m_nLoopCount;
};


// ThreadManager.obj process arrays. The selected product's ThreadID::MAX is
// the physical first dimension. Current L2Server.exe fixes it at 32.
extern int g_nSkillActorDBID[];
extern int g_nSkillID[];
extern int g_nSkillTargetCount[];
extern int g_nLeaveWorldPostCount[];
extern int g_nLeaveWorldPostDBID[][32];
extern int g_nUserGarbageEnterTime[];
extern int g_nUserGarbageUserDBID[];
extern int g_nUserGarbageRefCount[];
extern l2server46_20160218_CheckBuffInfo g_CheckBuffInfo[];

class CThreadManager {
public:
    static int GetMainThreadID();
    static void SetThisThreadAsMainThread();
    static void SetThisThreadAsWorldThread();
    static void SetThisThreadAsListenThread();

    static bool IsListenThread();
    static bool IsMainThread();
    static bool IsWorldThread();

    static int GetThreadCount();
    static void SetThreadCount(int count);
    static void RegisterThisThread(int threadId);
    static void SetThread(int threadId, void* handle);
    static void* GetThread(int threadId);
    static int GetThreadID();

private:
    static std::array<std::atomic<void*>, ThreadID::MAX> g_hThreadHandle;
    static std::atomic<int> g_nThreadQuantity;
};
