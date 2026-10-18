#pragma once

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <string>

// Both current product images create the same source-level JOB as an
// inline id followed by 64 UTF-16 code units. Current L2NPC.exe proves the
// body at 0x0054153c. Current L2Server.exe proves it at
// 0x00aec540.
class JOB {
public:
    explicit JOB(const wchar_t* name);

    int GetID() const noexcept { return m_nID; }

    const wchar_t* GetName() const noexcept;

private:
    int m_nID;
    char16_t m_Name[64];
};

// NASC64_2015 keeps DECLARE_AUTO_TIMER, DECLARE_AUTO_TIMEREX and
// REGISTER_JOB as historical identifiers. TotalChecker2015 keeps the
// resulting JOB globals. Their source definitions are unavailable, so the
// portable family states each storage/name decision and only
// models the policies closed in the current L2Server image.
#define L2_TIME_ANALYSIS_WIDEN_IMPL(Value) L##Value
#define L2_TIME_ANALYSIS_WIDEN(Value) L2_TIME_ANALYSIS_WIDEN_IMPL(Value)
#define L2_DECLARE_JOB(Name) extern JOB Name
#define L2_REGISTER_JOB(Name) JOB Name(L2_TIME_ANALYSIS_WIDEN(#Name))
#define L2_REGISTER_PRIVATE_NAMED_JOB(Name, DisplayName) static JOB Name(DisplayName)

#if defined(L2_BUILD_L2SERVER)
inline constexpr std::size_t kTimeAnalysisThreadSlotCount = 32;
inline constexpr std::size_t kTimeAnalysisListenThreadSlot = 27;
#else
inline constexpr std::size_t kTimeAnalysisThreadSlotCount = 12;
inline constexpr std::size_t kTimeAnalysisListenThreadSlot = 11;
#endif

class CTimer {
public:
    CTimer();

    void Reset();
    void TurnOn();
    void TurnOff();
    void BeginJob(const JOB& job);
    void EndJob(const JOB& job);
    void Report(LogType logType);

private:
    std::int64_t m_nBeginTime[kTimeAnalysisThreadSlotCount];
    std::int64_t m_nAccumulateTime[kTimeAnalysisThreadSlotCount];
    std::uint32_t m_nResetTick = 0;
    std::vector<std::int64_t> m_EachJobAccumulateTimes[kTimeAnalysisThreadSlotCount];
    std::vector<std::int64_t> m_EachJobBeginTimes[kTimeAnalysisThreadSlotCount];
    std::vector<std::int64_t> m_EachJobMaxTime[kTimeAnalysisThreadSlotCount];
};

// Current Shared/TimeAnalysis.h NPC counters. The current L2Server image
// fixes a two-slot base vtable (Report/Accumulate), 40,000 sixteen-byte
// records and no virtual destructor. NpcBroadCastStat adds only the
// two-argument Accumulate slot. Both derived singletons keep the base
// physical extent.
class CNpcStat {
public:
    struct NPCSTAT {
        std::int32_t npcId{};
        std::int32_t alignment04{};
        volatile std::int64_t cnt{};

        NPCSTAT() noexcept = default;

        NPCSTAT(const NPCSTAT& other) noexcept :
            npcId(other.npcId), alignment04(other.alignment04), cnt(runtime::raw_atomic::LoadI64(&other.cnt)) {}

        NPCSTAT& operator=(const NPCSTAT& other) noexcept {
            npcId = other.npcId;
            alignment04 = other.alignment04;
            runtime::raw_atomic::ExchangeI64(&cnt, runtime::raw_atomic::LoadI64(&other.cnt));
            return *this;
        }
    };

    struct CntPred {
        bool operator()(const NPCSTAT& left, const NPCSTAT& right) const noexcept {
            return runtime::raw_atomic::LoadI64(&left.cnt) > runtime::raw_atomic::LoadI64(&right.cnt);
        }
    };

    virtual void Report(LogType logType);
    virtual void Accumulate(int npcId);

    void Reset() noexcept {
        for (int index = 0; index < 40000; ++index) {
            stat_[index].npcId = index;
            runtime::raw_atomic::ExchangeI64(&stat_[index].cnt, 0);
        }
    }

protected:
    CNpcStat() = default;

    NPCSTAT stat_[40000]{};
};

class NpcDieStat : public CNpcStat {
public:
    static NpcDieStat& Inst() {
        static NpcDieStat instance;
        return instance;
    }

private:
    NpcDieStat() = default;
};

class NpcBroadCastStat : public CNpcStat {
public:
    static NpcBroadCastStat& Inst() {
        static NpcBroadCastStat instance;
        return instance;
    }

    using CNpcStat::Accumulate;

    virtual void Accumulate(int npcId, int count);

private:
    NpcBroadCastStat() = default;
};


// The original header uses this eight-byte RAII carrier at every profiled
// job scope. Its constructor and End method were inlined in the current
// image. The shared destructor thunk survives at 0x00455768.
class CAutoStartTimer {
public:
    explicit CAutoStartTimer(const JOB& job) noexcept;
    ~CAutoStartTimer();

    void End();

    CAutoStartTimer(const CAutoStartTimer&) = delete;
    CAutoStartTimer& operator=(const CAutoStartTimer&) = delete;

private:
    const JOB& m_job;
};

#define L2_DECLARE_AUTO_TIMER(Variable, JobName) CAutoStartTimer Variable(JobName)

// Current L2Server keeps this source-level owner in Shared/TimeAnalysis.h.
// The standalone destructor at 0x005d75dc and all six inlined consumers
// fix the four public fields and their order. The matching
// TotalChecker PDB supplies only the original class/method spelling.
class LagAlert {
public:
    LagAlert(std::int64_t maxLag, const char* source, int line) noexcept :
        base(static_cast<std::uint64_t>(runtime::GetTickCount())), maxLag(maxLag), source(source), line(line) {}

    ~LagAlert() {
        const std::uint32_t current = runtime::GetTickCount();
        const std::uint64_t elapsedBits = static_cast<std::uint64_t>(current) - static_cast<std::uint64_t>(base);
        const std::int64_t elapsed = runtime::Int64FromBits(elapsedBits);
        if (maxLag < elapsed) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PERF] [%S][%d] LAG PEAK!!!: %ums",
            //     source,
            //     line,
            //     static_cast<int>(elapsed))
            // Log.Add(LOG_ERROR,
            //     "[timing] Slow operation \"%s\": %lld ms exceeds the %lld ms limit (shipping marker %d).",
            //     source, static_cast<long long>(elapsed), static_cast<long long>(maxLag), line)
            L2_LOG_ERROR(
                Log, "[timing] Slow operation \"%s\": %lld ms exceeds the %lld ms limit (shipping marker %d).", source,
                static_cast<long long>(elapsed), static_cast<long long>(maxLag), line
            );
        }
    }

    std::int64_t base;
    std::int64_t maxLag;
    const char* source;
    int line;
};


std::wstring Int64ToString(std::int64_t value);

extern int g_nJobID;
extern const wchar_t* g_IDTable[1000];
extern CTimer g_Timer;

#if defined(L2_BUILD_L2SERVER)
// Current L2Server TimeAnalysis globals. The original Win64 `long` is
// represented because host `long` is eight bytes on LP64.
extern std::int32_t g_nActionCountStartTime;
extern std::int32_t g_nUserActionStartCount;
extern std::int32_t g_nUserActionTickCount;
extern std::int32_t g_nNpcActionStartCount;
extern std::int32_t g_nNpcActionTickCount;
extern std::int32_t g_nUserMoveToLocationCount;
extern std::int32_t g_nNpcMoveToLocationCount;

extern volatile std::int32_t g_TotalClientPacketTime;
extern volatile std::int32_t g_ClientPacketTime[209];
void ResetClientPacketTime();
void IncreaseClientPacketTime(int packetNo, std::int32_t elapsed);

// Current L2Server initializer 0x00db7230 and the exact UTF-16 literal at
// 0x011ec010 establish this public job and its id (1). The preceding socket
// section marker remains private to TimeAnalysis.cpp.
L2_DECLARE_JOB(JOB_DBSOCKET__TOTAL);
L2_DECLARE_JOB(JOB_COMMSOCKET__TOTAL);
L2_DECLARE_JOB(JOB_AUTHSOCKET__TOTAL);
L2_DECLARE_JOB(JOB_USERSOCKET__TOTAL);
L2_DECLARE_JOB(JOB_NPCSOCKET__TOTAL);
L2_DECLARE_JOB(JOB_AsyncSend);
L2_DECLARE_JOB(JOB_AsyncSendConstBuffer);
L2_DECLARE_JOB(JOB_PREMIUMSOCKET__TOTAL);
L2_DECLARE_JOB(JOB_GOODSINVENTORYSOCKET__TOTAL);
L2_DECLARE_JOB(JOB_NPSOCKET__TOTAL);
L2_DECLARE_JOB(JOB_CHANNELCHATTING__TOTAL);
L2_DECLARE_JOB(JOB_CAPTCHASOCKET_TOTAL);
L2_DECLARE_JOB(JOB_NEWACTION__MOVE_ONSTART);
L2_DECLARE_JOB(JOB_NEWACTION__MOVE_ONTICK);
L2_DECLARE_JOB(JOB_NEWACTION__MOVE_SETMOVING);
L2_DECLARE_JOB(JOB_NEWACTION__MOVE_ONEND);
L2_DECLARE_JOB(JOB_NEWACTION__MOVETOTARGET_ONSTART);
L2_DECLARE_JOB(JOB_NEWACTION__MOVETOTARGET_ONTICK);
L2_DECLARE_JOB(JOB_NEWACTION__MOVETOTARGET_SETMOVING);
L2_DECLARE_JOB(JOB_NEWACTION__MOVETOTARGET_ONEND);
L2_DECLARE_JOB(JOB_NEWACTION__MOVETOTARGET_SETDISTANCE);
L2_DECLARE_JOB(JOB_NEWACTION__MOVETOTARGET_SETVALIDPENDINGACTION);
L2_DECLARE_JOB(JOB_NEWACTION__ATTACK_ONSTART);
L2_DECLARE_JOB(JOB_NEWACTION__ATTACK_ONTICK);
L2_DECLARE_JOB(JOB_NEWACTION__SKILL_ONSTART);
L2_DECLARE_JOB(JOB_NEWACTION__SKILL_ONTICK);
L2_DECLARE_JOB(JOB_NEWACTION__SKILL_ONEND);
L2_DECLARE_JOB(JOB_GENERAL_TIMER);
L2_DECLARE_JOB(JOB_CCREATURE__TIMEREXPIRED);
L2_DECLARE_JOB(JOB_USER_TIMEREXPIRED);
L2_DECLARE_JOB(JOB_CNPC_TIMEREXPIRED);
L2_DECLARE_JOB(JOB_BROADCAST_CHARINFO);
L2_DECLARE_JOB(JOB_BROADCAST_CHARINFO_NEIGHBOR);
L2_DECLARE_JOB(JOB_BROADCAST_CHARINFO_INSIGHT);
L2_DECLARE_JOB(JOB_BROADCAST_NPCINFO);
L2_DECLARE_JOB(JOB_BROADCAST_NPCINFO_NEIGHBOR);
L2_DECLARE_JOB(JOB_BROADCAST_NPCINFO_INSIGHT);
L2_DECLARE_JOB(JOB_BroadcastToNeighbor);
L2_DECLARE_JOB(JOB_BroadcastToNeighborDist);
L2_DECLARE_JOB(JOB_BroadcastToNeighborDistBetweenZ);
L2_DECLARE_JOB(JOB_BroadcastToNeighborRect);
L2_DECLARE_JOB(JOB_BroadcastToTargettingNeighbor);
L2_DECLARE_JOB(JOB_BroadcastToNeighborExceptSelf);
L2_DECLARE_JOB(JOB_BroadcastToNeighborExceptSelfWithSenderVisibility);
L2_DECLARE_JOB(JOB_BroadcastToNeighborRectExceptSelf);
L2_DECLARE_JOB(JOB_BroadcastToNeighborIndex);
L2_DECLARE_JOB(JOB_SEND_EXUSERINFO);
L2_DECLARE_JOB(JOB_SEND_EXUSERINFOINVENWEIGHT);
L2_DECLARE_JOB(JOB_SEND_EXUSERINFOEQUIPSLOT);
L2_DECLARE_JOB(JOB_SEND_EXUSERINFCUBICINFO);
L2_DECLARE_JOB(JOB_SEND_EXUSERINFOABNORMALVISUALEFFECT);
L2_DECLARE_JOB(JOB_CGEODATA_MOVESTRAIGHT);
L2_DECLARE_JOB(JOB_CGEODATA_MOVESTRAIGHT_FLOATING);
L2_DECLARE_JOB(JOB_LOOKNEIGHBOR);
L2_DECLARE_JOB(JOB_WORLD_MOVETO);
L2_DECLARE_JOB(JOB_WORLD_MOVETOZONE);
L2_DECLARE_JOB(JOB_SYNC_ALLMAP);
L2_DECLARE_JOB(JOB_FOR_ALL_OBJECT_IN_CONTAINER_INDEX);
L2_DECLARE_JOB(JOB_FOR_ALL_OBJECT_IN_CONTAINER__INSTANT);
L2_DECLARE_JOB(JOB_FOR_ALL_OBJECT_IN_CONTAINER_INDEX__INSTANT);
L2_DECLARE_JOB(JOB_FOR_ALL_CREATURE_IN_CONTAINERINDEX);
L2_DECLARE_JOB(JOB_FOR_ALL_ITEM_IN_CONTAINERINDEX);
L2_DECLARE_JOB(JOB_FOR_ALL_NEIGHBOR_CREATURE_DIST);
L2_DECLARE_JOB(JOB_FOR_ALL_NEIGHBOR_CREATURE_DIST_Z_LIMIT);
L2_DECLARE_JOB(JOB_FOR_ALL_NEIGHBOR_CREATURE_RECT);
L2_DECLARE_JOB(JOB_FOR_ALL_NEIGHBOR_USER_DIST);
L2_DECLARE_JOB(JOB_FOR_ALL_NEIGHBOR_USER_DIST_Z_LIMIT);
L2_DECLARE_JOB(JOB_FOR_ALL_NEIGHBOR_USER_RECT);
L2_DECLARE_JOB(JOB_FOR_ALL_NEIGHBOR_USER_RANGE);
L2_DECLARE_JOB(JOB_FOR_ALL_NEIGHBOR_CREATURE_RANGE);
L2_DECLARE_JOB(JOB_FOR_ALL_NEIGHBOR_ITEM_RECT);
L2_DECLARE_JOB(JOB_FOR_ALL_NEIGHBOR_CONTAINER);
L2_DECLARE_JOB(JOB_PATH_FIND_FIND);
L2_DECLARE_JOB(JOB_CREATURE_LOCK);
L2_DECLARE_JOB(JOB_USER_INSIGHT);
L2_DECLARE_JOB(JOB_USER_OUTOFSIGHT);
L2_DECLARE_JOB(JOB_WORLD_COLLISION_WORLD);
L2_DECLARE_JOB(JOB_USER_USEITEM);
L2_DECLARE_JOB(JOB_NPC_MOVEPACKET);
L2_DECLARE_JOB(JOB_SHIPACTION__MOVE_ONSTART);
L2_DECLARE_JOB(JOB_SHIPACTION__MOVE_CALC);
L2_DECLARE_JOB(JOB_SHIPACTION__MOVE_ONTICK);
L2_DECLARE_JOB(JOB_SHIPACTION__MOVE_SETMOVING);
L2_DECLARE_JOB(JOB_SHIPACTION__MOVE_ONEND);
#else
extern JOB SCRIPT_ENGINE__RUN_NPC_SCRIPT;
extern JOB SERVERSOCKET__ATTACK_PACKET;
extern JOB SERVERSOCKET__TALK_PACKET;
extern JOB NPC__DO_NOTHING;
extern JOB NPC__MOVE_AROUND;
extern JOB NPC__PATTACK;
extern JOB JOB_GENERAL_TIMER;
extern JOB NPC__TIMER_EXPIRED_DBSAVING;
extern JOB NPC__TIMER_EXPIRED;
extern JOB NPC__TIMER_EXPIRED_1;
extern JOB NPC__TIMER_EXPIRED_2;
extern JOB JOB_CREATURE_LOCK;
#endif
