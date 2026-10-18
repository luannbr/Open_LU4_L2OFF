#include "Shared/src/TimeAnalysis.h"

#include "Runtime.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kThreadSlotCount = kTimeAnalysisThreadSlotCount;
constexpr std::size_t kJobCapacity = 1000;

std::int64_t TickDifference(std::uint32_t current, std::int64_t begin) noexcept {
    const std::uint64_t difference = static_cast<std::uint64_t>(current) - static_cast<std::uint64_t>(begin);
    return runtime::Int64FromBits(difference);
}

void CopyTargetJobName(char16_t (&destination)[64], const wchar_t* source) noexcept {
    if (!source) {
        destination[0] = 0;
        return;
    }

    std::size_t output = 0;
    for (std::size_t input = 0; source[input] != L'\0'; ++input) {
        const std::uint32_t codePoint = static_cast<std::uint32_t>(source[input]);
        if (codePoint <= 0xffffU) {
            if (output + 1 >= std::size(destination)) {
                destination[0] = 0;
                return;
            }
            destination[output++] = static_cast<char16_t>(codePoint);
            continue;
        }

        if (codePoint > 0x10ffffU || output + 2 >= std::size(destination)) {
            destination[0] = 0;
            return;
        }
        const std::uint32_t scalar = codePoint - 0x10000U;
        destination[output++] = static_cast<char16_t>(0xd800U + (scalar >> 10U));
        destination[output++] = static_cast<char16_t>(0xdc00U + (scalar & 0x3ffU));
    }
    destination[output] = 0;
}

} // namespace

int g_nJobID = 0;
const wchar_t* g_IDTable[kJobCapacity]{};

#if defined(L2_BUILD_L2SERVER)
// The target declares these as Win64 long values. Use an explicit 32-bit
// representation on LP64 hosts while retaining the source identifiers
// kept by the same-source TotalChecker map.
std::int32_t g_nActionCountStartTime = 0;
std::int32_t g_nUserActionStartCount = 0;
std::int32_t g_nUserActionTickCount = 0;
std::int32_t g_nNpcActionStartCount = 0;
std::int32_t g_nNpcActionTickCount = 0;
std::int32_t g_nUserMoveToLocationCount = 0;
std::int32_t g_nNpcMoveToLocationCount = 0;
volatile std::int32_t g_TotalClientPacketTime = 0;
volatile std::int32_t g_ClientPacketTime[209]{};

void ResetClientPacketTime() {
    runtime::raw_atomic::StoreI32Relaxed(&g_TotalClientPacketTime, 0);
    for (volatile std::int32_t& value : g_ClientPacketTime) {
        runtime::raw_atomic::ExchangeI32(&value, 0);
    }
}

void IncreaseClientPacketTime(int packetNo, std::int32_t elapsed) {
    const std::int32_t units = elapsed / 10000;
    runtime::raw_atomic::AddI32(&g_ClientPacketTime[packetNo], units);
    runtime::raw_atomic::AddI32(&g_TotalClientPacketTime, units);
}
#endif

// Current dynamic initializers 0x0058975c..0x00589846 establish this exact
// registration order and the matching globals at 0x02816850 onward.
CTimer g_Timer;
#if defined(L2_BUILD_L2SERVER)
// Current initializers 0x00db721c..0x00db72e4 establish this exact order.
// Keep only the section marker private: its source identifier is not
// recoverable, whereas every following JOB name is present in current data.
L2_REGISTER_PRIVATE_NAMED_JOB(g_SocketJobSection, L"________________SOCKET________________");
L2_REGISTER_JOB(JOB_DBSOCKET__TOTAL);
L2_REGISTER_JOB(JOB_COMMSOCKET__TOTAL);
L2_REGISTER_JOB(JOB_AUTHSOCKET__TOTAL);
L2_REGISTER_JOB(JOB_USERSOCKET__TOTAL);
L2_REGISTER_JOB(JOB_NPCSOCKET__TOTAL);
L2_REGISTER_JOB(JOB_AsyncSend);
L2_REGISTER_JOB(JOB_AsyncSendConstBuffer);
L2_REGISTER_JOB(JOB_PREMIUMSOCKET__TOTAL);
L2_REGISTER_JOB(JOB_GOODSINVENTORYSOCKET__TOTAL);
L2_REGISTER_JOB(JOB_NPSOCKET__TOTAL);

// Current L2Server dynamic initializers 0x00db72f8..0x00db7960. JOB ids
// are allocated monotonically by constructor order, so the complete segment
// is kept: publishing JOB_WORLD_MOVETO alone would give it the wrong id
// and index the wrong TimeAnalysis lane.
L2_REGISTER_JOB(JOB_CHANNELCHATTING__TOTAL);
L2_REGISTER_JOB(JOB_CAPTCHASOCKET_TOTAL);
L2_REGISTER_PRIVATE_NAMED_JOB(g_NewActionJobSection, L"_______________NEWACTION_________________________");
L2_REGISTER_JOB(JOB_NEWACTION__MOVE_ONSTART);
L2_REGISTER_JOB(JOB_NEWACTION__MOVE_ONTICK);
L2_REGISTER_JOB(JOB_NEWACTION__MOVE_SETMOVING);
L2_REGISTER_JOB(JOB_NEWACTION__MOVE_ONEND);
L2_REGISTER_JOB(JOB_NEWACTION__MOVETOTARGET_ONSTART);
L2_REGISTER_JOB(JOB_NEWACTION__MOVETOTARGET_ONTICK);
L2_REGISTER_JOB(JOB_NEWACTION__MOVETOTARGET_SETMOVING);
L2_REGISTER_JOB(JOB_NEWACTION__MOVETOTARGET_ONEND);
L2_REGISTER_JOB(JOB_NEWACTION__MOVETOTARGET_SETDISTANCE);
L2_REGISTER_JOB(JOB_NEWACTION__MOVETOTARGET_SETVALIDPENDINGACTION);
L2_REGISTER_JOB(JOB_NEWACTION__ATTACK_ONSTART);
L2_REGISTER_JOB(JOB_NEWACTION__ATTACK_ONTICK);
L2_REGISTER_JOB(JOB_NEWACTION__SKILL_ONSTART);
L2_REGISTER_JOB(JOB_NEWACTION__SKILL_ONTICK);
L2_REGISTER_JOB(JOB_NEWACTION__SKILL_ONEND);
L2_REGISTER_PRIVATE_NAMED_JOB(g_TimerJobSection, L"_______________TIMER_____________________________");
L2_REGISTER_JOB(JOB_GENERAL_TIMER);
L2_REGISTER_JOB(JOB_CCREATURE__TIMEREXPIRED);
L2_REGISTER_JOB(JOB_USER_TIMEREXPIRED);
L2_REGISTER_JOB(JOB_CNPC_TIMEREXPIRED);
L2_REGISTER_PRIVATE_NAMED_JOB(g_BroadcastJobSection, L"____________BROADCAST____________________________");
L2_REGISTER_JOB(JOB_BROADCAST_CHARINFO);
L2_REGISTER_JOB(JOB_BROADCAST_CHARINFO_NEIGHBOR);
L2_REGISTER_JOB(JOB_BROADCAST_CHARINFO_INSIGHT);
L2_REGISTER_JOB(JOB_BROADCAST_NPCINFO);
L2_REGISTER_JOB(JOB_BROADCAST_NPCINFO_NEIGHBOR);
L2_REGISTER_JOB(JOB_BROADCAST_NPCINFO_INSIGHT);
L2_REGISTER_JOB(JOB_BroadcastToNeighbor);
L2_REGISTER_JOB(JOB_BroadcastToNeighborDist);
L2_REGISTER_JOB(JOB_BroadcastToNeighborDistBetweenZ);
L2_REGISTER_JOB(JOB_BroadcastToNeighborRect);
L2_REGISTER_JOB(JOB_BroadcastToTargettingNeighbor);
L2_REGISTER_JOB(JOB_BroadcastToNeighborExceptSelf);
L2_REGISTER_JOB(JOB_BroadcastToNeighborExceptSelfWithSenderVisibility);
L2_REGISTER_JOB(JOB_BroadcastToNeighborRectExceptSelf);
L2_REGISTER_JOB(JOB_BroadcastToNeighborIndex);
L2_REGISTER_PRIVATE_NAMED_JOB(g_UnitcastJobSection, L"_____________Unitcast___________________________");
L2_REGISTER_JOB(JOB_SEND_EXUSERINFO);
L2_REGISTER_JOB(JOB_SEND_EXUSERINFOINVENWEIGHT);
L2_REGISTER_JOB(JOB_SEND_EXUSERINFOEQUIPSLOT);
L2_REGISTER_JOB(JOB_SEND_EXUSERINFCUBICINFO);
L2_REGISTER_JOB(JOB_SEND_EXUSERINFOABNORMALVISUALEFFECT);
L2_REGISTER_PRIVATE_NAMED_JOB(g_GeoWorldJobSection, L"_____________GEO_WORLD___________________________");
L2_REGISTER_JOB(JOB_CGEODATA_MOVESTRAIGHT);
L2_REGISTER_JOB(JOB_CGEODATA_MOVESTRAIGHT_FLOATING);
L2_REGISTER_JOB(JOB_LOOKNEIGHBOR);
L2_REGISTER_JOB(JOB_WORLD_MOVETO);
L2_REGISTER_JOB(JOB_WORLD_MOVETOZONE);
L2_REGISTER_JOB(JOB_SYNC_ALLMAP);
L2_REGISTER_PRIVATE_NAMED_JOB(g_ContainerJobSection, L"_____________CONTAINER___________________________");
L2_REGISTER_JOB(JOB_FOR_ALL_OBJECT_IN_CONTAINER_INDEX);
L2_REGISTER_JOB(JOB_FOR_ALL_OBJECT_IN_CONTAINER__INSTANT);
L2_REGISTER_JOB(JOB_FOR_ALL_OBJECT_IN_CONTAINER_INDEX__INSTANT);
L2_REGISTER_JOB(JOB_FOR_ALL_CREATURE_IN_CONTAINERINDEX);
L2_REGISTER_JOB(JOB_FOR_ALL_ITEM_IN_CONTAINERINDEX);
L2_REGISTER_PRIVATE_NAMED_JOB(g_ForAllJobSection, L"____________FOR_ALL______________________________");
L2_REGISTER_JOB(JOB_FOR_ALL_NEIGHBOR_CREATURE_DIST);
L2_REGISTER_JOB(JOB_FOR_ALL_NEIGHBOR_CREATURE_DIST_Z_LIMIT);
L2_REGISTER_JOB(JOB_FOR_ALL_NEIGHBOR_CREATURE_RECT);
L2_REGISTER_JOB(JOB_FOR_ALL_NEIGHBOR_USER_DIST);
L2_REGISTER_JOB(JOB_FOR_ALL_NEIGHBOR_USER_DIST_Z_LIMIT);
L2_REGISTER_JOB(JOB_FOR_ALL_NEIGHBOR_USER_RECT);
L2_REGISTER_JOB(JOB_FOR_ALL_NEIGHBOR_USER_RANGE);
L2_REGISTER_JOB(JOB_FOR_ALL_NEIGHBOR_CREATURE_RANGE);
L2_REGISTER_JOB(JOB_FOR_ALL_NEIGHBOR_ITEM_RECT);
L2_REGISTER_JOB(JOB_FOR_ALL_NEIGHBOR_CONTAINER);
L2_REGISTER_PRIVATE_NAMED_JOB(g_EtcJobSection, L"_________________ETC_____________________________");
L2_REGISTER_JOB(JOB_PATH_FIND_FIND);
L2_REGISTER_JOB(JOB_CREATURE_LOCK);
L2_REGISTER_JOB(JOB_USER_INSIGHT);
L2_REGISTER_JOB(JOB_USER_OUTOFSIGHT);
L2_REGISTER_JOB(JOB_WORLD_COLLISION_WORLD);
L2_REGISTER_JOB(JOB_USER_USEITEM);
L2_REGISTER_JOB(JOB_NPC_MOVEPACKET);
L2_REGISTER_PRIVATE_NAMED_JOB(g_ShipActionJobSection, L"_______________SHIPACTION_________________________");
L2_REGISTER_JOB(JOB_SHIPACTION__MOVE_ONSTART);
L2_REGISTER_JOB(JOB_SHIPACTION__MOVE_CALC);
L2_REGISTER_JOB(JOB_SHIPACTION__MOVE_ONTICK);
L2_REGISTER_JOB(JOB_SHIPACTION__MOVE_SETMOVING);
L2_REGISTER_JOB(JOB_SHIPACTION__MOVE_ONEND);
#else
JOB SCRIPT_ENGINE__RUN_NPC_SCRIPT(L"SCRIPT_ENGINE__RUN_NPC_SCRIPT");
JOB SERVERSOCKET__ATTACK_PACKET(L"SERVERSOCKET__ATTACK_PACKET");
JOB SERVERSOCKET__TALK_PACKET(L"SERVERSOCKET__TALK_PACKET");
JOB NPC__DO_NOTHING(L"NPC__DO_NOTHING");
JOB NPC__MOVE_AROUND(L"NPC__MOVE_AROUND");
JOB NPC__PATTACK(L"NPC__PATTACK");
JOB JOB_GENERAL_TIMER(L"JOB_GENERAL_TIMER");
JOB NPC__TIMER_EXPIRED_DBSAVING(L"NPC__TIMER_EXPIRED_DBSAVING");
JOB NPC__TIMER_EXPIRED(L"NPC__TIMER_EXPIRED");
JOB NPC__TIMER_EXPIRED_1(L"NPC__TIMER_EXPIRED_1");
JOB NPC__TIMER_EXPIRED_2(L"NPC__TIMER_EXPIRED_2");
JOB JOB_CREATURE_LOCK(L"JOB_CREATURE_LOCK");
#endif

JOB::JOB(const wchar_t* name) : m_nID(g_nJobID++) {
    // Current L2NPC.exe 0x0054153c and current L2Server.exe 0x00aec540 both
    // use wcscpy_s(..., 0x40, ...).
    CopyTargetJobName(m_Name, name);
    g_IDTable[m_nID] = name;
}

const wchar_t* JOB::GetName() const noexcept {
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    return reinterpret_cast<const wchar_t*>(m_Name);
#else
    thread_local std::array<wchar_t, 64> hostName{};
    std::fill(hostName.begin(), hostName.end(), wchar_t{});

    std::size_t output = 0;
    for (std::size_t input = 0; input < std::size(m_Name) && m_Name[input] != 0 && output + 1 < hostName.size(); ++input) {
        const std::uint32_t first = m_Name[input];
        if (first >= 0xd800U && first <= 0xdbffU && input + 1 < std::size(m_Name)) {
            const std::uint32_t second = m_Name[input + 1];
            if (second >= 0xdc00U && second <= 0xdfffU) {
                hostName[output++] = static_cast<wchar_t>(0x10000U + ((first - 0xd800U) << 10U) + (second - 0xdc00U));
                ++input;
                continue;
            }
        }
        hostName[output++] = static_cast<wchar_t>(first);
    }
    hostName[output] = 0;
    return hostName.data();
#endif
}

CTimer::CTimer() {
    // Current L2NPC.exe 0x00541700 and current L2Server.exe 0x00aec734 both
    // reserve and create all 1000 job slots in each of the three vector
    // banks. Their product builds differ only in the proven slot count and
    // fixed LISTEN slot selected above.
    for (std::size_t thread = 0; thread < kThreadSlotCount; ++thread) {
        m_nBeginTime[thread] = 0;
        m_nAccumulateTime[thread] = 0;
        auto& accumulated = m_EachJobAccumulateTimes[thread];
        auto& begun = m_EachJobBeginTimes[thread];
        auto& maximum = m_EachJobMaxTime[thread];
        accumulated.reserve(kJobCapacity);
        begun.reserve(kJobCapacity);
        maximum.reserve(kJobCapacity);
        for (std::size_t job = 0; job < kJobCapacity; ++job) {
            accumulated.push_back(0);
            begun.push_back(0);
            maximum.push_back(0);
        }
        std::fill(accumulated.begin(), accumulated.end(), 0);
        std::fill(begun.begin(), begun.end(), 0);
        std::fill(maximum.begin(), maximum.end(), 0);
    }
    m_nBeginTime[kTimeAnalysisListenThreadSlot] = 0;
    m_nAccumulateTime[kTimeAnalysisListenThreadSlot] = 0;
}

void CTimer::Reset() {
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00aec58c.
#else
    // Current L2NPC.exe 0x00541588.
#endif
    for (int thread = 0; thread < CThreadManager::GetThreadCount(); ++thread) {
        m_nAccumulateTime[thread] = 0;
        for (int job = 0; job < g_nJobID; ++job) {
            runtime::CheckedVectorAt(m_EachJobAccumulateTimes[thread], job) = 0;
            runtime::CheckedVectorAt(m_EachJobMaxTime[thread], job) = 0;
        }
    }

    m_nResetTick = runtime::GetTickCount();
    g_ObjectInShortTime.store(0, std::memory_order_relaxed);
    // Log.Add(
    //     LOG_NORMAL,
    //     L"Timer Reset")
    // Log.Add(LOG_NORMAL,
    //     "[timing] Resetting profiling totals.")
    L2_LOG_INFO(Log, "[timing] Resetting profiling totals.");

    m_nAccumulateTime[ThreadID::LISTEN] = 0;
    for (int job = 0; job < g_nJobID; ++job) {
        runtime::CheckedVectorAt(m_EachJobAccumulateTimes[ThreadID::LISTEN], job) = 0;
        runtime::CheckedVectorAt(m_EachJobMaxTime[ThreadID::LISTEN], job) = 0;
    }

#if defined(L2_BUILD_L2SERVER)
    const std::uint32_t actionCountStartTime = runtime::GetTickCount();
    static_assert(sizeof(g_nActionCountStartTime) == sizeof(actionCountStartTime));
    std::memcpy(&g_nActionCountStartTime, &actionCountStartTime, sizeof(g_nActionCountStartTime));
    g_nUserActionStartCount = 0;
    g_nNpcActionStartCount = 0;
    g_nUserActionTickCount = 0;
    g_nNpcActionTickCount = 0;
    g_nUserMoveToLocationCount = 0;
    g_nNpcMoveToLocationCount = 0;
#endif
}

void CTimer::TurnOn() {
    // Current L2NPC.exe 0x005414d8 / current L2Server.exe 0x00aec484.
    const int thread = CThreadManager::GetThreadID();
    m_nBeginTime[thread] = runtime::GetTickCount();
}

void CTimer::TurnOff() {
    // Current L2NPC.exe 0x00541508 / current L2Server.exe 0x00aec4b4.
    const int thread = CThreadManager::GetThreadID();
    m_nAccumulateTime[thread] =
        runtime::AddInt64Wrapping(m_nAccumulateTime[thread], TickDifference(runtime::GetTickCount(), m_nBeginTime[thread]));
}

void CTimer::BeginJob(const JOB& job) {
    const int thread = CThreadManager::GetThreadID();
    runtime::CheckedVectorAt(m_EachJobBeginTimes[thread], job.GetID()) = runtime::GetTickCount();
}

void CTimer::EndJob(const JOB& job) {
    // Current L2NPC.exe 0x004556a8 and current L2Server.exe 0x00463160.
    // The product-conditioned layout above places the third vector bank at
    // the exact current offset in each image.
    const int thread = CThreadManager::GetThreadID();
    const int jobId = job.GetID();
    const std::int64_t elapsed = TickDifference(runtime::GetTickCount(), runtime::CheckedVectorAt(m_EachJobBeginTimes[thread], jobId));

    std::int64_t& maximum = runtime::CheckedVectorAt(m_EachJobMaxTime[thread], jobId);
    if (maximum < elapsed) {
        maximum = elapsed;
    }
    if (elapsed > 5000) {
#if defined(L2_BUILD_L2SERVER)
        // LogIOError(
        //     L"[%s][%d] ThreadID[%d] Job[%s] "
        //     L"Elasped time[%I64d]",
        //     L"C:\\Work\\Lineage2Build\\Server\\"
        //     L"Main\\Shared\\TimeAnalysis.h",
        //     0x10b,
        //     thread,
        //     job.GetName(),
        //     elapsed)
        // LogIOError(
        //     "[timing] Job \"%s\" on worker %d took %lld ms. Exceeds the 5000 ms limit.",
        //     WideToUtf8(job.GetName()).c_str(), thread, static_cast<long long>(elapsed))
        L2_LOG_IO_ERROR(
            "[timing] Job \"%s\" on worker %d took %lld ms; exceeds the 5000 ms limit.", WideToUtf8(job.GetName()).c_str(),
            thread, static_cast<long long>(elapsed)
        );
#else
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ThreadID[%d] Job[%s] "
        //     L"Elasped time[%I64d]",
        //     L"c:\\work\\lineage2build\\server\\"
        //     L"main\\shared\\TimeAnalysis.h",
        //     0x10b,
        //     thread,
        //     job.GetName(),
        //     elapsed)
        // Log.Add(LOG_ERROR,
        //     "[timing] Job \"%s\" on worker %d took %lld ms. Exceeds the 5000 ms limit.",
        //     WideToUtf8(job.GetName()).c_str(), thread, static_cast<long long>(elapsed))
        L2_LOG_ERROR(
            Log, "[timing] Job \"%s\" on worker %d took %lld ms; exceeds the 5000 ms limit.",
            WideToUtf8(job.GetName()).c_str(), thread, static_cast<long long>(elapsed)
        );
#endif
    }
    std::int64_t& accumulated = runtime::CheckedVectorAt(m_EachJobAccumulateTimes[thread], jobId);
    accumulated = runtime::AddInt64Wrapping(accumulated, elapsed);
}

void CTimer::Report(LogType logType) {
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe 0x00aecae8.
#else
    // Current L2NPC.exe 0x00541ab0.
#endif
    const int type = static_cast<int>(logType);
    if (!CThreadManager::IsMainThread() || type < LOG_NORMAL || type >= LOG_MAX) {
        return;
    }

    // Noise: The accumulated-time headline below identifies the report without a long separator.
    // Log.Add(
    //     logType,
    //     L"----------------------------------------------------------------------- Time anlysis report
    //     --------------------------------------------------------------------")

    const LogType reportType = logType == LOG_ERROR || logType == LOG_WARN ? LOG_NORMAL : logType;
    std::int64_t accumulatedTotal = 0;
    for (int thread = 0; thread < CThreadManager::GetThreadCount(); ++thread) {
        accumulatedTotal = runtime::AddInt64Wrapping(accumulatedTotal, m_nAccumulateTime[thread]);
    }
    accumulatedTotal = runtime::AddInt64Wrapping(accumulatedTotal, m_nAccumulateTime[ThreadID::LISTEN]);

    // Noise: The accumulated-time headline below already identifies this report.
    // Log.Add(
    //     logType,
    //     L"Time analysis result")
    // Log.Add(
    //     logType,
    //     L"  Total accumulated tick count = %d",
    //     static_cast<int>(accumulatedTotal))
    // Log.Add(reportType,
    //     "[timing] Profiling report: accumulated worker time %lld ms.",
    //     static_cast<long long>(accumulatedTotal))
    L2_LOG(
        Log, reportType, "[timing] Profiling report: accumulated worker time %lld ms.",
        static_cast<long long>(accumulatedTotal)
    );

    const std::uint32_t reportTick = runtime::GetTickCount();
    const int rows = (g_nJobID + 1) / 2;
    std::int64_t registeredTotal = 0;
    for (int first = 0; first < rows; ++first) {
        const int second = rows + first;
        std::int64_t firstTotal = 0;
        std::int64_t secondTotal = 0;
        for (int thread = 0; thread < CThreadManager::GetThreadCount(); ++thread) {
            firstTotal =
                runtime::AddInt64Wrapping(firstTotal, runtime::CheckedVectorAt(m_EachJobAccumulateTimes[thread], first));
            if (second < g_nJobID) {
                secondTotal =
                    runtime::AddInt64Wrapping(secondTotal, runtime::CheckedVectorAt(m_EachJobAccumulateTimes[thread], second));
            }
        }
        firstTotal =
            runtime::AddInt64Wrapping(firstTotal, runtime::CheckedVectorAt(m_EachJobAccumulateTimes[ThreadID::LISTEN], first));
        if (second < g_nJobID) {
            secondTotal = runtime::AddInt64Wrapping(
                secondTotal, runtime::CheckedVectorAt(m_EachJobAccumulateTimes[ThreadID::LISTEN], second)
            );
        }
        registeredTotal =
            runtime::AddInt64Wrapping(runtime::AddInt64Wrapping(registeredTotal, secondTotal), firstTotal);

        // Noise: Omit zero-time rows and the wide two-column table. Emit one nonzero job per readable record.
        // if (second < g_nJobID) {
        //     const std::wstring secondText =
        //         Int64ToString(secondTotal)
        //     const std::wstring firstText =
        //         Int64ToString(firstTotal)
        //     Log.Add(
        //         logType,
        //         L"%50s (%25s)  %50s (%25s) ",
        //         g_IDTable[first],
        //         firstText.c_str(),
        //         g_IDTable[second],
        //         secondText.c_str())
        // } else {
        //     const std::wstring firstText =
        //         Int64ToString(firstTotal)
        //     Log.Add(
        //         logType,
        //         L"%50s (%25s)  ",
        //         g_IDTable[first],
        //         firstText.c_str())
        // }
        if (firstTotal != 0) {
            // Log.Add(reportType, "[timing] Job \"%s\": %lld ms.",
            //     WideToUtf8(g_IDTable[first]).c_str(), static_cast<long long>(firstTotal))
            L2_LOG(
                Log, reportType, "[timing] Job \"%s\": %lld ms.", WideToUtf8(g_IDTable[first]).c_str(),
                static_cast<long long>(firstTotal)
            );
        }
        if (second < g_nJobID && secondTotal != 0) {
            // Log.Add(reportType, "[timing] Job \"%s\": %lld ms.",
            //     WideToUtf8(g_IDTable[second]).c_str(), static_cast<long long>(secondTotal))
            L2_LOG(
                Log, reportType, "[timing] Job \"%s\": %lld ms.", WideToUtf8(g_IDTable[second]).c_str(),
                static_cast<long long>(secondTotal)
            );
        }
    }

    const std::uint32_t elapsedSinceReset = reportTick - m_nResetTick;
    const std::uint32_t shortTimeAverage =
        static_cast<std::uint32_t>(g_ObjectInShortTime.load(std::memory_order_relaxed)) * 100000U / elapsedSinceReset;
    // Log.Add(
    //     logType,
    //     L"  ObjectInTimer[%d]  "
    //     L"ObjectInShortTime Average[%d]",
    //     g_ObjectInTimer.load(
    //         std::memory_order_relaxed),
    //     shortTimeAverage)
    // Log.Add(reportType,
    //     "[timing] Objects in timers: %d. Short-time average: %u.",
    //     g_ObjectInTimer.load(std::memory_order_relaxed), static_cast<unsigned>(shortTimeAverage))
    L2_LOG(
        Log, reportType, "[timing] Objects in timers: %d; short-time average: %u.",
        g_ObjectInTimer.load(std::memory_order_relaxed), static_cast<unsigned>(shortTimeAverage)
    );

    const double registeredPercent =
        registeredTotal == 0 ? 0.0 : static_cast<double>(registeredTotal) / static_cast<double>(accumulatedTotal) * 100.0;
    // Log.Add(
    //     logType,
    //     L"  [%0.0f/%0.0f] Registered Total : "
    //     L"%0.2f%%",
    //     static_cast<double>(registeredTotal),
    //     static_cast<double>(accumulatedTotal),
    //     registeredPercent)
    // Log.Add(reportType,
    //     "[timing] Registered jobs: %.0f ms of %.0f ms accumulated (%.2f%%).",
    //     static_cast<double>(registeredTotal), static_cast<double>(accumulatedTotal), registeredPercent)
    L2_LOG(
        Log, reportType, "[timing] Registered jobs: %.0f ms of %.0f ms accumulated (%.2f%%).",
        static_cast<double>(registeredTotal), static_cast<double>(accumulatedTotal), registeredPercent
    );

#if defined(L2_BUILD_L2SERVER)
    std::uint32_t actionCountStartTime = 0;
    static_assert(sizeof(actionCountStartTime) == sizeof(g_nActionCountStartTime));
    std::memcpy(&actionCountStartTime, &g_nActionCountStartTime, sizeof(actionCountStartTime));
    const double actionSeconds = static_cast<double>(runtime::GetTickCount() - actionCountStartTime) / 1000.0;
    // Log.Add(
    //     logType,
    //     L" Action per SEC: User Start[%4.1f], "
    //     L"Tick[%4.1f] / NPC Start[%4.1f], "
    //     L"Tick[%4.1f]",
    //     static_cast<double>(
    //         g_nUserActionStartCount) /
    //         actionSeconds,
    //     static_cast<double>(
    //         g_nUserActionTickCount) /
    //         actionSeconds,
    //     static_cast<double>(
    //         g_nNpcActionStartCount) /
    //         actionSeconds,
    //     static_cast<double>(
    //         g_nNpcActionTickCount) /
    //         actionSeconds)
    // Log.Add(reportType,
    //     "[timing] Actions per second: users %.1f starts, %.1f ticks. NPCs %.1f starts, %.1f ticks.",
    //     static_cast<double>(g_nUserActionStartCount) / actionSeconds,
    //     static_cast<double>(g_nUserActionTickCount) / actionSeconds,
    //     static_cast<double>(g_nNpcActionStartCount) / actionSeconds,
    //     static_cast<double>(g_nNpcActionTickCount) / actionSeconds)
    L2_LOG(
        Log, reportType, "[timing] Actions per second: users %.1f starts, %.1f ticks; NPCs %.1f starts, %.1f ticks.",
        static_cast<double>(g_nUserActionStartCount) / actionSeconds,
        static_cast<double>(g_nUserActionTickCount) / actionSeconds,
        static_cast<double>(g_nNpcActionStartCount) / actionSeconds,
        static_cast<double>(g_nNpcActionTickCount) / actionSeconds
    );
    // Log.Add(
    //     logType,
    //     L" MoveToLocation per SEC : User "
    //     L"[%4.1f], NPC [%4.1f]",
    //     static_cast<double>(
    //         g_nUserMoveToLocationCount) /
    //         actionSeconds,
    //     static_cast<double>(
    //         g_nNpcMoveToLocationCount) /
    //         actionSeconds)
    // Log.Add(reportType,
    //     "[timing] Move-to-location actions per second: users %.1f, NPCs %.1f.",
    //     static_cast<double>(g_nUserMoveToLocationCount) / actionSeconds,
    //     static_cast<double>(g_nNpcMoveToLocationCount) / actionSeconds)
    L2_LOG(
        Log, reportType, "[timing] Move-to-location actions per second: users %.1f, NPCs %.1f.",
        static_cast<double>(g_nUserMoveToLocationCount) / actionSeconds,
        static_cast<double>(g_nNpcMoveToLocationCount) / actionSeconds
    );
#endif
}

void CNpcStat::Report(LogType logType) {
    L2_GUARD_FUNCTION();

    std::vector<NPCSTAT, std::allocator<NPCSTAT>> sorted;
    std::copy(std::begin(stat_), std::end(stat_), std::back_inserter(sorted));
    std::stable_sort(sorted.begin(), sorted.end(), CntPred{});

    const int reportCount = std::min<int>(20, static_cast<int>(sorted.size()));
    for (int rank = 0; rank < reportCount; ++rank) {
        const NPCSTAT& entry = sorted[static_cast<std::size_t>(rank)];
        // Log.Add(
        //     logType,
        //     L"[SNIS][%d] %d:%I64d",
        //     rank,
        //     entry.npcId,
        //     runtime::raw_atomic::LoadI64(&entry.cnt))
        // Log.Add(logType == LOG_ERROR || logType == LOG_WARN ? LOG_NORMAL : logType,
        //     "[NPC statistics] Rank %d: NPC %d, count %lld.",
        //     rank + 1, entry.npcId, static_cast<long long>(runtime::raw_atomic::LoadI64(&entry.cnt)))
        L2_LOG(
            Log, logType == LOG_ERROR || logType == LOG_WARN ? LOG_NORMAL : logType,
            "[NPC statistics] Rank %d: NPC %d, count %lld.", rank + 1, entry.npcId,
            static_cast<long long>(runtime::raw_atomic::LoadI64(&entry.cnt))
        );
    }
}

void CNpcStat::Accumulate(int npcId) {
    if (static_cast<unsigned int>(npcId) >= 40000U) {
        return;
    }
    runtime::raw_atomic::AddI64(&stat_[npcId].cnt, 1);
}

void NpcBroadCastStat::Accumulate(int npcId, int count) {
    if (static_cast<unsigned int>(npcId) >= 40000U) {
        return;
    }
    runtime::raw_atomic::AddI64(&stat_[npcId].cnt, static_cast<std::int64_t>(count));
}

CAutoStartTimer::CAutoStartTimer(const JOB& job) noexcept : m_job(job) { g_Timer.BeginJob(m_job); }

CAutoStartTimer::~CAutoStartTimer() { End(); }

void CAutoStartTimer::End() { g_Timer.EndJob(m_job); }

std::wstring Int64ToString(std::int64_t value) {
    // Current L2NPC.exe 0x00541970 returns an empty string for zero and
    // prepends a comma after each group of three remaining digits.
    std::wstring result;
    int digits = 0;
    while (value != 0) {
        const std::int64_t quotient = value / 10;
        const int remainder = static_cast<int>(value - quotient * 10);
        result = std::to_wstring(remainder) + result;
        value = quotient;
        ++digits;
        if (digits == 3 && value != 0) {
            result = L"," + result;
            digits = 0;
        }
    }
    return result;
}
