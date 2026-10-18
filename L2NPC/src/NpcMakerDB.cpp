#include "NpcMakerDB.h"

#include "NPC.h"

#include "L2NPC/src/Config.h"
#include "MakerEventFactory.h"
#include "NPCServer.h"
#include "NpcEvent.h"
#include "ParserSpawn.h"
#include "Shared/src/Runtime/IOThreadRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Runtime.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/ResidenceData.h"
#include "Shared/src/ShareInfo.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "TextRuntime.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string_view>
#include <thread>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace l2npc {
namespace {

#if defined(_WIN32)
constexpr const wchar_t* kWindowsProtectionMutex = L"L2Mutex_Protector_090720";
#else
constexpr const char* kPosixProtectionMutexPath = "/tmp/l2npc_process_L2Mutex_Protector_090720.lock";
#endif

// Diagnostic state is private to the calling thread and the single protection
// object. It never changes retry timing or the recovered mutex return policy.
enum class ProtectionFailure { CreateMutex, WaitMutex, AbandonedMutex, OpenLock, AcquireLock };

struct ProtectionDiagnosticErrorState {
    const int savedErrno = errno;
#if defined(_WIN32)
    const DWORD savedWindowsError = ::GetLastError();
#endif
    ~ProtectionDiagnosticErrorState() {
        errno = savedErrno;
#if defined(_WIN32)
        ::SetLastError(savedWindowsError);
#endif
    }
};

struct ProtectionFailureReports {
    bool active = false;
    ProtectionFailure operation = ProtectionFailure::CreateMutex;
    unsigned long error = 0;
    unsigned long long repeats = 0;
    std::chrono::steady_clock::time_point lastReport;
};
thread_local ProtectionFailureReports protectionFailureReports;

const char* ProtectionFailureName(ProtectionFailure operation) noexcept {
    switch (operation) {
        case ProtectionFailure::CreateMutex:
            return "create/open mutex";
        case ProtectionFailure::WaitMutex:
            return "wait for mutex";
        case ProtectionFailure::AbandonedMutex:
            return "abandoned mutex";
        case ProtectionFailure::OpenLock:
            return "open lock file";
        case ProtectionFailure::AcquireLock:
            return "acquire file lock";
    }
    return "unknown protection operation";
}

void SummarizeProtectionFailures() {
    auto& reports = protectionFailureReports;
    if (reports.repeats == 0) {
        return;
    }
    const auto repeats = reports.repeats;
    reports.repeats = 0;
    // Log.Add(reports.operation == ProtectionFailure::AbandonedMutex ? LOG_WARN : LOG_ERROR,
    //     "[NPC startup] L2Server protection check: %s%llu more '%s' failures on this thread (%s %lu) since the last
    //     report.", repeats == std::numeric_limits<unsigned long long>::max() ? "at least " : "", repeats,
    //     ProtectionFailureName(reports.operation),
    //     reports.operation == ProtectionFailure::OpenLock || reports.operation == ProtectionFailure::AcquireLock
    //         ? "errno" : "Windows status", reports.error)
    L2_LOG(
        Log, reports.operation == ProtectionFailure::AbandonedMutex ? LOG_WARN : LOG_ERROR,
        "[NPC startup] L2Server protection check: %s%llu more '%s' failures on this thread (%s %lu) since the last report.",
        repeats == std::numeric_limits<unsigned long long>::max() ? "at least " : "", repeats,
        ProtectionFailureName(reports.operation),
        reports.operation == ProtectionFailure::OpenLock || reports.operation == ProtectionFailure::AcquireLock
            ? "errno"
            : "Windows status",
        reports.error
    );
}

bool ShouldReportProtectionFailure(ProtectionFailure operation, unsigned long error) {
    const ProtectionDiagnosticErrorState preserveErrors;
    auto& reports = protectionFailureReports;
    const auto now = std::chrono::steady_clock::now();
    if (reports.active && reports.operation == operation && reports.error == error) {
        if (reports.repeats < std::numeric_limits<unsigned long long>::max()) {
            ++reports.repeats;
        }
        if (now - reports.lastReport >= std::chrono::seconds(30)) {
            reports.lastReport = now;
            SummarizeProtectionFailures();
        }
        return false;
    }
    SummarizeProtectionFailures();
    reports.active = true;
    reports.operation = operation;
    reports.error = error;
    reports.lastReport = now;
    return true;
}

void ReportProtectionCheckProgress(bool protectionPassed) {
    const ProtectionDiagnosticErrorState preserveErrors;
    auto& reports = protectionFailureReports;
    if (!reports.active) {
        return;
    }
    SummarizeProtectionFailures();
    reports.active = false;
    // Log.Add(LOG_NORMAL,
    //     "[NPC startup] Protection check now %s after a '%s' failure (%s %lu).",
    //     protectionPassed ? "passes" : "reports a busy lock. Startup is still waiting",
    //     ProtectionFailureName(reports.operation),
    //     reports.operation == ProtectionFailure::OpenLock || reports.operation == ProtectionFailure::AcquireLock
    //         ? "errno" : "Windows status", reports.error)
    L2_LOG_INFO(
        Log, "[NPC startup] Protection check now %s after a '%s' failure (%s %lu).",
        protectionPassed ? "passes" : "reports a busy lock; startup is still waiting",
        ProtectionFailureName(reports.operation),
        reports.operation == ProtectionFailure::OpenLock || reports.operation == ProtectionFailure::AcquireLock
            ? "errno"
            : "Windows status",
        reports.error
    );
}

CNPC* ResolveNpcObjectW(int objectId) { return CNPC::GetObjectW(objectId); }

void DispatchMakerStartEvent(NpcMakerEx& maker) {
    MakerEvent* event = MakerEventFactory::CreateStartEvent(&maker);
    if (!event) {
        return;
    }
    maker.HandleEvent(event);
    L2_RELEASE(event, ::ObjectRefType::CreateDelete);
}

bool InitializeTerritoryRandomPositions(TerritoryInfo& territory, bool sky, int sampleTarget) {
    return territory.InitializeRandomPos(sky, sampleTarget);
}

} // namespace

// NpcMakerDB.cpp global from the shipping image. CNpcMakerDB::Inst at
// current target 0x004e7b98 returns this object unconditionally.
CNpcMakerDB g_NpcMakerDB;

CNpcPosEvent::CNpcPosEvent() = default;

CNpcPosEvent::~CNpcPosEvent() = default;

void CNpcPosEvent::StartTimer() {
    mTick = mTotalMakerSize < 1 ? DEFAULT_TICK : TOTAL_TICK / mTotalMakerSize;
    if (mTick < DEFAULT_TICK) {
        mTick = DEFAULT_TICK;
    }
    mStartTick = static_cast<int>(runtime::GetTickCount());
    AddTimer(1500U, 0);
}

int CNpcPosEvent::GetNpcPosIndex() const { return mNpcPosIndex; }

NpcPosEventController::NpcPosData::NpcPosData(std::vector<CNpcMaker*>& makerList, int enabled) :
    mIsEnabled(enabled), mMakerList(makerList) {}

void NpcPosEventController::AddNpcPosEvent(std::vector<CNpcMaker*>& npcMakers, int enabled) {
    NpcPosData data(npcMakers, enabled);
    m_NpcPosDatas.push_back(data);
    mTotalMakerSize += static_cast<int>(npcMakers.size());
}

void NpcPosEventController::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    if (static_cast<std::int64_t>(mNpcPosIndex) == static_cast<std::int64_t>(m_NpcPosDatas.size())) {
        const std::uint32_t elapsedSeconds = (runtime::GetTickCount() - static_cast<std::uint32_t>(mStartTick)) / 1000U;
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Processing NPCPOS EVENT - %d finished. "
        //     L"ElapsedSec[%d]",
        //     mTotalMakerSize,
        //     static_cast<int>(elapsedSeconds))
        // Log.Add(LOG_NORMAL,
        //     "[NPC maker] Position-event dispatch finished for %d regular maker entries in %u seconds.",
        //     mTotalMakerSize, static_cast<unsigned int>(elapsedSeconds))
        L2_LOG_INFO(
            Log, "[NPC maker] Position-event dispatch finished for %d regular maker entries in %u seconds.", mTotalMakerSize,
            static_cast<unsigned int>(elapsedSeconds)
        );
        L2_RELEASE(this, ::ObjectRefType::CreateDelete);
        return;
    }

    NpcPosData& data = runtime::CheckedVectorAt(m_NpcPosDatas, static_cast<std::size_t>(mNpcPosIndex));
    if (static_cast<std::int64_t>(data.mMakerIndex) == static_cast<std::int64_t>(data.mMakerList.size())) {
        ++mNpcPosIndex;
    } else {
        CNpcMaker* maker = runtime::CheckedVectorAt(data.mMakerList, static_cast<std::size_t>(data.mMakerIndex));
        ++data.mMakerIndex;
        maker->OnNpcPosEvent(data.mIsEnabled != 0);
    }
    AddTimer(static_cast<std::uint32_t>(mTick), 0);
}

NpcPosExEventController::NpcPosDataEx::NpcPosDataEx(std::vector<NpcMakerEx*>& makerList, int enabled) :
    mIsEnabled(enabled), mMakerList(makerList) {}

void NpcPosExEventController::AddNpcPosExEvent(std::vector<NpcMakerEx*>& npcMakers, int enabled) {
    NpcPosDataEx data(npcMakers, enabled);
    m_NpcPosDataExs.push_back(data);
    mTotalMakerSize += static_cast<int>(npcMakers.size());
}

void NpcPosExEventController::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    if (static_cast<std::int64_t>(mNpcPosIndex) == static_cast<std::int64_t>(m_NpcPosDataExs.size())) {
        const std::uint32_t elapsedSeconds = (runtime::GetTickCount() - static_cast<std::uint32_t>(mStartTick)) / 1000U;
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Processing NPCPOS EVENT - %d finished. "
        //     L"ElapsedSec[%d]",
        //     mTotalMakerSize,
        //     static_cast<int>(elapsedSeconds))
        // Log.Add(LOG_NORMAL,
        //     "[NPC maker] Position-event dispatch finished for %d extended maker entries in %u seconds.",
        //     mTotalMakerSize, static_cast<unsigned int>(elapsedSeconds))
        L2_LOG_INFO(
            Log, "[NPC maker] Position-event dispatch finished for %d extended maker entries in %u seconds.",
            mTotalMakerSize, static_cast<unsigned int>(elapsedSeconds)
        );
        L2_RELEASE(this, ::ObjectRefType::CreateDelete);
        return;
    }

    NpcPosDataEx& data = runtime::CheckedVectorAt(m_NpcPosDataExs, static_cast<std::size_t>(mNpcPosIndex));
    if (static_cast<std::int64_t>(data.mMakerIndex) == static_cast<std::int64_t>(data.mMakerList.size())) {
        ++mNpcPosIndex;
    } else {
        NpcMakerEx* maker = runtime::CheckedVectorAt(data.mMakerList, static_cast<std::size_t>(data.mMakerIndex));
        ++data.mMakerIndex;
        maker->OnNpcPosEvent(data.mIsEnabled != 0);
    }
    AddTimer(static_cast<std::uint32_t>(mTick), 0);
}

// Current L2NPC.exe: 0x004f6a24. Container and lock construction is supplied
// by their C++ members in declaration order. These are the explicit writes in
// the guarded source-level constructor body.
CNpcMakerDB::CNpcMakerDB() {
    L2_GUARD_FUNCTION();
    m_NpcMakers.clear();
    m_DomainTerritories.clear();
    m_nMakerProcessed = 0;
    m_nSiegeRelatedProcessed = 0;
    m_nTotalSiegeRelated = 0;
    m_nMakerDoneCount = 0;
    m_nMakerExDoneCount = 0;
    m_MakerFinished = 0;
}

CNpcMakerDB::~CNpcMakerDB() = default;

CNpcMakerDB& CNpcMakerDB::Inst() {
    L2_GUARD_FUNCTION();
    // Current L2NPC.exe 0x004e7b98.
    return g_NpcMakerDB;
}

bool CNpcMakerDB::Load(const wchar_t* fileName) {
    L2_GUARD_FUNCTION();
    int codeUnitCount = 0;
    std::uint16_t* const image = LoadBinaryAbsoluteW(fileName, codeUnitCount);
    if (!image) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] Read from Objects file failed",
        //     L"NpcMakerDB.cpp",
        //     0x6b)
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] Cannot read spawn definitions from '%s'. Loading failed.",
        //     WideToUtf8(fileName).c_str())
        L2_LOG_ERROR(
            Log, "[NPC maker] Cannot read spawn definitions from '%s'; loading failed.", WideToUtf8(fileName).c_str()
        );
        return false;
    }

    CParserForNPCSpawn npcspawnParser;
    if (!npcspawnParser.Create(this, image + 1, codeUnitCount - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] npcspawnParser.create failed",
        //     L"NpcMakerDB.cpp",
        //     0x73)
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] Cannot initialize the spawn parser for '%s'. Loading failed.",
        //     WideToUtf8(fileName).c_str())
        L2_LOG_ERROR(
            Log, "[NPC maker] Cannot initialize the spawn parser for '%s'; loading failed.", WideToUtf8(fileName).c_str()
        );
        return false;
    }
    if (npcspawnParser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing NPC Spawn file failed, "
        //     L"at line[%d]",
        //     L"NpcMakerDB.cpp",
        //     0x7e,
        //     npcspawnParser.m_lexer.yylineno)
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] Parsing '%s' failed at lexer line %d. Loading stopped.",
        //     WideToUtf8(fileName).c_str(), npcspawnParser.m_lexer.yylineno)
        L2_LOG_ERROR(
            Log, "[NPC maker] Parsing '%s' failed at lexer line %d; loading stopped.", WideToUtf8(fileName).c_str(),
            npcspawnParser.m_lexer.yylineno
        );
        return false;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"loading file '%s' finished",
    //     fileName)
    // Log.Add(LOG_NORMAL,
    //     "[NPC maker] Parsed spawn definitions from '%s'.",
    //     WideToUtf8(fileName).c_str())
    // Original: L2_LOG_INFO(Log,
    //     "[NPC maker] Parsed spawn definitions from '%s'.",
    //     WideToUtf8(fileName).c_str())
    if (npcspawnParser.yyerrorcount != 0) {
        L2_LOG_WARN(
            Log,
            "[NPC maker] Parser accepted '%s' after reporting %d errors; the loader still returns success. Check the "
            "preceding parser diagnostics.",
            WideToUtf8(fileName).c_str(), npcspawnParser.yyerrorcount
        );
    } else {
        L2_LOG_INFO(Log, "[NPC maker] Parsed spawn definitions from '%s'.", WideToUtf8(fileName).c_str());
    }
    return true;
}

void CNpcMakerDB::AddSiegeRelatedCount() { runtime::raw_atomic::AddI32(&m_nTotalSiegeRelated, 1); }

bool CNpcMakerDB::IsSiegeRelatedCompleted() const {
    // Current PingForCastleSiege at 0x0051a67d reads +0x248 first and
    // +0x244 second before comparing total <= processed. Keep the two live
    // volatile reads distinct and in that order.
    const std::int32_t total = runtime::raw_atomic::LoadI32Relaxed(&m_nTotalSiegeRelated);
    const std::int32_t processed = runtime::raw_atomic::LoadI32Relaxed(&m_nSiegeRelatedProcessed);
    return total <= processed;
}

int CNpcMakerDB::GetInZoneMapFlag(int inzoneId, int flagId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InZoneMakerLock);
    const auto found = m_InzoneMapFlag.find(std::make_pair(inzoneId, flagId));
    return found == m_InzoneMapFlag.end() ? -1 : found->second;
}

void CNpcMakerDB::SetInZoneMapFlag(int inzoneId, int flagId, int value) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InZoneMakerLock);
    const std::pair<int, int> key{inzoneId, flagId};
    const auto found = m_InzoneMapFlag.find(key);
    if (found != m_InzoneMapFlag.end() && found->second == value) {
        return;
    }

    m_InzoneMapFlag[key] = value;
    // 0x004ecebc supplies the changed key/value as excess varargs to
    // `cdddd`. VAssemble consumes only the four literal dwords.
    g_ns.Send("cdddd", 0xa1, 6, 0, 0, 0, inzoneId, flagId);
}

void CNpcMakerDB::_SetInstantZoneMapFlag(int inzoneId, const std::uint8_t* packet) {
    L2_GUARD_FUNCTION();
    // Exact `d count`, followed by `dd` flag/value records.
    int flagCount = 0;
    packet = ::Disassemble(packet, "d", &flagCount);
    for (int index = 0; index < flagCount; ++index) {
        int flagId = 0;
        int value = 0;
        packet = ::Disassemble(packet, "dd", &flagId, &value);
        m_InzoneMapFlag[std::make_pair(inzoneId, flagId)] = value;
    }
}

void CNpcMakerDB::RegisterNpcPosEvent(const char16_t* eventName, CNpcMaker* maker) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    const std::u16string eventNameKey(eventName);
    // 0x004f2754 appends without rejecting an empty name, null maker or
    // duplicate registration.
    m_NpcMakerReceivingNpcPosEvent[eventNameKey].push_back(maker);
}

int CNpcMakerDB::RegisterNpcPosEvent(const char16_t* eventName, NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    const std::u16string eventNameKey(eventName);
    // 0x004f1b18 appends unconditionally. In particular, the target does
    // not reject an empty key and does not deduplicate a maker.
    mNpcMakerExReceivingNpcPosEvent[eventNameKey].push_back(maker);
    return 0;
}

int CNpcMakerDB::RegisterSiegeEventEx(int eventId, NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    const auto range = mNpcMakerExSiegeEvent.equal_range(eventId);
    const bool registered =
        std::any_of(range.first, range.second, [maker](const auto& entry) { return entry.second == maker; });
    if (!registered) {
        {
            L2_GUARD(m_ExSiegeEventSect);
            mNpcMakerExSiegeEvent.emplace(eventId, maker);
        }
    }
    return 0;
}

int CNpcMakerDB::RegisterAgitSiegeEventEx(int eventId, NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    const auto range = mNpcMakerExAgitSiegeEvent.equal_range(eventId);
    const bool registered =
        std::any_of(range.first, range.second, [maker](const auto& entry) { return entry.second == maker; });
    if (!registered) {
        {
            L2_GUARD(m_ExAgitSiegeEventSect);
            mNpcMakerExAgitSiegeEvent.emplace(eventId, maker);
        }
    }
    return 0;
}

void CNpcMakerDB::RegisterSiegeEvent(CNpcMaker* maker) {
    L2_GUARD_FUNCTION();
    // Current 0x004f25ec uses the same outer CIOCriticalSection as
    // SiegeEvent itself. It does not add a null-maker fast path.
    {
        L2_GUARD(m_NpcMakerSiegeEventSect);
        m_NpcMakersReceivingSiegeEvent[{maker->GetCastleID(), maker->GetSiegeType()}].push_back(maker);
    }
}

void CNpcMakerDB::RegisterAgitAttackEvent(int agitId, CNpcMaker* maker) {
    {
        L2_GUARD(m_AgitAttackEventSect);
        m_NpcMakerReceivingAgitAttackEvent.emplace(agitId, maker);
    }
}

void CNpcMakerDB::RegisterAgitDefendEvent(int agitId, CNpcMaker* maker) {
    {
        L2_GUARD(m_AgitDefendEventSect);
        m_NpcMakerReceivingAgitDefendEvent.emplace(agitId, maker);
    }
}

void CNpcMakerDB::RegisterAgitTB_BattleRoyalEvent(int agitId, CNpcMaker* maker) {
    {
        L2_GUARD(m_AgitTBBattleRoyalEventSect);
        m_NpcMakerReceivingAgitTB_BattleRoyalEvent.emplace(std::make_pair(agitId, maker->GetColor()), maker);
    }
}

void CNpcMakerDB::RegisterAgitTB_FinalEvent(int agitId, CNpcMaker* maker) {
    {
        L2_GUARD(m_AgitTBFinalEventSect);
        m_NpcMakerReceivingAgitTB_FinalEvent.emplace(std::make_pair(agitId, maker->GetColor()), maker);
    }
}

void CNpcMakerDB::RegisterDoorEvent(const char16_t* doorName, CNpcMaker* maker) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_ReceiverLock);
        const std::u16string doorNameKey(doorName);
        // 0x004f28b8 updates the classic maker registry first, then asks
        // L2Server for the current state even on duplicate registrations.
        m_NpcMakerReceivingDoorEvent[doorNameKey].push_back(maker);
    }
    g_ns.Send("cdS", 0x83, 0, doorName);
}

int CNpcMakerDB::RegisterDoorEvent(const char16_t* doorName, int inzoneType, NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    if (inzoneType == 0) {
        L2_GUARD(m_ReceiverLock);
        const std::u16string doorNameKey(doorName);
        mNpcMakerExReceivingDoorEvent[doorNameKey].push_back(maker);
    } else {
        L2_GUARD(m_InZoneDoorLock);
        // Current 0x004f1c6c keeps the script-literal pointer unchanged.
        m_NpcMakerExReceivingInZoneDoorEventDB[maker].push_back(doorName);
    }
    g_ns.Send("cdS", 0x83, 0, doorName);
    return 0;
}

void CNpcMakerDB::RegisterFieldCycleEvent(unsigned int eventId, NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    // 0x004ec5f0 inserts into the multimap without a duplicate scan.
    mNpcMakerExFieldCycleEvent.emplace(eventId, maker);
}

void CNpcMakerDB::RegisterOlympiadFieldEvent(int eventId, NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    // xmap::insert keeps the first value for an existing key.
    m_npcMakerExOlympiadFieldEvent.emplace(eventId, maker);
}

void CNpcMakerDB::RegisterPledgeGameFieldEvent(int eventId, NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    // Unnamed 0x004ebeb8 is identified by its exact diagnostic signature.
    m_npcMakerExPledgeGameFieldEvent.emplace(eventId, maker);
}

void CNpcMakerDB::RegisterFortressEvent(int residenceId, int eventId, int spawn, NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    m_NpcMakerExReceivingFortressEvent[std::make_pair(residenceId, eventId)].emplace_back(maker, spawn);
}

void CNpcMakerDB::RegisterInstantZoneEvent(int inzoneType, int clusterId, int eventId, int spawn, NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    // 0x004f3f24 is the only recovered registration routine here that
    // ignores a null maker.
    if (!maker) {
        return;
    }
    L2_GUARD(m_InZoneMakerLock);
    m_NpcMakerExReceivingInZoneEventDB[std::make_pair(inzoneType, clusterId)].push_back(
        NpcMakerExEventInfo{eventId, spawn, maker}
    );
}

void CNpcMakerDB::RegisterHunterGuildBless(NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    m_npcHunterGuildBlessMakers.push_back(maker);
}

void CNpcMakerDB::RegisterInstantZoneDoorEventMakerEx(int inzoneId, NpcMakerEx* source, NpcMakerEx* instance) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InZoneDoorLock);
    const auto sourceDoors = m_NpcMakerExReceivingInZoneDoorEventDB.find(source);
    if (sourceDoors == m_NpcMakerExReceivingInZoneDoorEventDB.end()) {
        return;
    }
    for (const char16_t* doorName : sourceDoors->second) {
        const std::u16string doorNameKey(doorName);
        m_NpcMakerExReceivingInZoneDoorEvent[inzoneId][doorNameKey].push_back(instance);
    }
}

void CNpcMakerDB::RegisterInstantZoneNotifySubscriber(InstantZoneKey key, NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InZoneMakerLock);
    m_mapInstantZoneNotify[key].insert(maker);
}

void CNpcMakerDB::UnregisterInstantZoneNotifySubscriber(InstantZoneKey key, NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InZoneMakerLock);
    const auto found = m_mapInstantZoneNotify.find(key);
    if (found == m_mapInstantZoneNotify.end()) {
        return;
    }
    found->second.erase(maker);
    // 0x004ec6e8 keeps the empty set entry.
}

void CNpcMakerDB::NotifyToInstantZoneMakerSubscriber(
    InstantZoneKey key, std::uint32_t sourceInzoneId, int eventId, int argument1, int argument2, int argument3
) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InZoneMakerLock);
    const auto found = m_mapInstantZoneNotify.find(key);
    if (found == m_mapInstantZoneNotify.end()) {
        return;
    }

    // Current 0x004eb920 traverses the live pointer-ordered set. A null
    // entry is erased in-place. The non-null path advances the original
    // iterator after the callback and does not re-seek by pointer value.
    auto& registered = found->second;
    auto iterator = registered.begin();
    while (iterator != registered.end()) {
        NpcMakerEx* maker = *iterator;
        if (!maker) {
            iterator = registered.erase(iterator);
            continue;
        }
        const int targetInzoneId = static_cast<int>(maker->GetInZoneID());
        maker->OnInstantZoneEvent(
            key.typeId, key.clusterId, targetInzoneId, eventId, 1, 0,
            sourceInzoneId == static_cast<std::uint32_t>(targetInzoneId) ? 1 : 0, argument1, argument2, argument3
        );
        ++iterator;
    }
}

void CNpcMakerDB::DoorEvent(const char16_t* doorName, int enabled, int inzoneId) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_ReceiverLock);
        const auto regular = m_NpcMakerReceivingDoorEvent.find(doorName);
        if (regular != m_NpcMakerReceivingDoorEvent.end()) {
            // Current 0x004f2d78 re-evaluates both operator[] and size on every
            // pass, so a recursive callback observes appends made to this key.
            for (std::uint32_t index = 0; index < m_NpcMakerReceivingDoorEvent[std::u16string(doorName)].size(); ++index) {
                runtime::CheckedVectorAt(m_NpcMakerReceivingDoorEvent[std::u16string(doorName)], index)->DoorEvent(enabled);
            }
        }
        // Current 0x004f2d78 releases the classic receiver scope
        // before acquiring the scope used by either extended registry.
    }

    if (inzoneId == 0) {
        L2_GUARD(m_ReceiverLock);
        const auto extended = mNpcMakerExReceivingDoorEvent.find(doorName);
        if (extended == mNpcMakerExReceivingDoorEvent.end()) {
            return;
        }
        auto& makers = extended->second;
        for (auto iterator = makers.begin(); iterator != makers.end(); ++iterator) {
            (*iterator)->OnDoorEvent(enabled);
        }
        return;
    }

    L2_GUARD(m_InZoneDoorLock);
    const auto inzone = m_NpcMakerExReceivingInZoneDoorEvent.find(inzoneId);
    if (inzone == m_NpcMakerExReceivingInZoneDoorEvent.end()) {
        return;
    }
    const auto door = inzone->second.find(std::u16string(doorName));
    if (door == inzone->second.end()) {
        return;
    }
    auto& makers = door->second;
    for (auto iterator = makers.begin(); iterator != makers.end(); ++iterator) {
        (*iterator)->OnDoorEvent(enabled);
    }
}

void CNpcMakerDB::AgitEvent(int agitId, std::u16string agitName, int enabled, int variant, bool databaseRespawn) {
    L2_GUARD_FUNCTION();
    bool handled = false;
    if (variant > 0) {
        {
            L2_GUARD(m_AgitAttackEventSect);
            auto iterator = m_NpcMakerReceivingAgitAttackEvent.lower_bound(agitId);
            while (iterator != m_NpcMakerReceivingAgitAttackEvent.end() && iterator->first == agitId) {
                iterator->second->AgitEvent(enabled, databaseRespawn);
                handled = true;
                ++iterator;
            }
        }
    } else if (variant == 0) {
        {
            L2_GUARD(m_AgitDefendEventSect);
            auto iterator = m_NpcMakerReceivingAgitDefendEvent.lower_bound(agitId);
            while (iterator != m_NpcMakerReceivingAgitDefendEvent.end() && iterator->first == agitId) {
                iterator->second->AgitEvent(enabled, databaseRespawn);
                handled = true;
                ++iterator;
            }
        }
    }

    auto iterator = mNpcMakerExAgitSiegeEvent.lower_bound(agitId);
    while (iterator != mNpcMakerExAgitSiegeEvent.end() && iterator->first == agitId) {
        iterator->second->AgitEvent(enabled, databaseRespawn);
        handled = true;
        ++iterator;
    }
    if (!handled) {
        const std::string agitNameWide = Utf16ToUtf8(agitName.data());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid agit[%s]",
        //     L"NpcMakerDB.cpp",
        //     0x3d2,
        //     agitNameWide.c_str())
        // Log.Add(LOG_WARN,
        //     "[NPC maker] No maker handled clan-hall event for '%s' (ID %d, variant %d, %s).",
        //     WideToUtf8(agitNameWide).c_str(), agitId, variant, enabled ? "enabled" : "disabled")
        L2_LOG_WARN(
            Log, "[NPC maker] No maker handled clan-hall event for '%s' (ID %d, variant %d, %s).",
            agitNameWide.c_str(), agitId, variant, enabled ? "enabled" : "disabled"
        );
    }
}

void CNpcMakerDB::SiegeEvent(int residenceId, int event, bool databaseRespawn) {
    L2_GUARD_FUNCTION();
    // Current target 0x004eea00 holds
    // m_NpcMakerSiegeEventSect (+0x518) for the complete dispatch.
    {
        L2_GUARD(m_NpcMakerSiegeEventSect);
        const auto activateSubtype = [this, residenceId, databaseRespawn](int subtype) {
            const auto receiving =
                m_NpcMakersReceivingSiegeEvent.find(std::make_pair(residenceId, static_cast<SiegeDefendType>(subtype)));
            if (receiving == m_NpcMakersReceivingSiegeEvent.end()) {
                return;
            }
            for (std::uint32_t index = 0; index < receiving->second.size(); ++index) {
                runtime::CheckedVectorAt(receiving->second, index)->SiegeEvent(residenceId, 1, databaseRespawn);
            }
            RegisterNpcMakerWorkingSiegeEvent(residenceId, receiving->second);
        };

        switch (event) {
            case 1: {
                ClearNpcMakerWorkingSiegeEvent(residenceId);
                const CResidenceData& residence = CResidenceData::Inst(residenceId);
                const int subtype = residence.m_nResidenceOwnerID != 0 ? 1 : 0;
                activateSubtype(subtype);
                break;
            }
            case 2:
                ClearNpcMakerWorkingSiegeEvent(residenceId);
                break;
            case 6:
                activateSubtype(2);
                break;
            case 7:
                ClearNpcMakerWorkingSiegeEvent(residenceId);
                break;
            default:
                break;
        }

        if (event >= 1 && event <= 10) {
            auto iterator = mNpcMakerExSiegeEvent.lower_bound(residenceId);
            while (iterator != mNpcMakerExSiegeEvent.end() && iterator->first == residenceId) {
                NpcMakerEx* const maker = iterator->second;
                switch (event) {
                    case 1:
                        maker->OnStartSiegeEvent();
                        break;
                    case 2:
                        maker->OnEndSiegeEvent();
                        break;
                    case 3:
                        maker->OnProclaimSiegeEvent();
                        break;
                    case 4:
                        maker->OnDestructCTRLTowerEvent();
                        break;
                    case 5:
                        maker->OnCancelSiegeEvent();
                        break;
                    case 6:
                        maker->OnExtendedStartSiegeEvent();
                        break;
                    case 7:
                        maker->OnDestructSupplyEvent();
                        break;
                    case 8:
                        maker->OnDeclareDominionEvent();
                        break;
                    case 9:
                        maker->OnStartChangeTimeEvent();
                        break;
                    case 10:
                        maker->OnEndChangeTimeEvent();
                        break;
                    default:
                        break;
                }
                ++iterator;
            }
        }
    }
}

void CNpcMakerDB::RegisterNpcMakerWorkingSiegeEvent(int eventId, std::vector<CNpcMaker*>& makers) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_NpcMakersWorkingSiegeEventMapSect);
        for (CNpcMaker* maker : makers) {
            m_NpcMakersWorkingSiegeEventMap.emplace(eventId, maker);
        }
    }
}

void CNpcMakerDB::ClearNpcMakerWorkingSiegeEvent(int eventId) {
    L2_GUARD_FUNCTION();
    std::vector<CNpcMaker*> working;
    {
        L2_GUARD(m_NpcMakersWorkingSiegeEventMapSect);
        {
            const auto range = m_NpcMakersWorkingSiegeEventMap.equal_range(eventId);
            for (auto iterator = range.first; iterator != range.second; ++iterator) {
                working.push_back(iterator->second);
            }
            m_NpcMakersWorkingSiegeEventMap.erase(range.first, range.second);
        }
    }

    // The target calls maker code outside the map lock.
    for (CNpcMaker* maker : working) {
        maker->SiegeEvent(eventId, 2, false);
    }
}

void CNpcMakerDB::TB_StartBatleRoyal(
    int eventId,
    const char16_t* eventName [[maybe_unused]],
    const std::map<int, std::pair<int, int>>& teams,
    bool databaseRespawn
) {
    L2_GUARD_FUNCTION();

    std::vector<CNpcMaker*> spawned;
    {
        L2_GUARD(m_NpcMakersWorkingSiegeEventMapSect);
        {
            const auto range = m_NpcMakersWorkingSiegeEventMap.equal_range(eventId);
            for (auto iterator = range.first; iterator != range.second; ++iterator) {
                spawned.push_back(iterator->second);
            }
            m_NpcMakersWorkingSiegeEventMap.erase(range.first, range.second);
        }
    }

    for (CNpcMaker* maker : spawned) {
        maker->EndTeamBattle();
    }
    spawned.clear();

    for (int color = 0; color < 5; ++color) {
        int npcType = 1;
        int pledgeId = 0;
        const auto team = teams.find(color);
        if (team != teams.end()) {
            npcType = team->second.first;
            pledgeId = team->second.second;
        }
        if (npcType == 0) {
            npcType = 1;
        }

        CNpcMaker* source = nullptr;
        {
            L2_GUARD(m_AgitTBBattleRoyalEventSect);
            {
                const auto range = m_NpcMakerReceivingAgitTB_BattleRoyalEvent.equal_range(std::make_pair(eventId, color));
                for (auto iterator = range.first; iterator != range.second; ++iterator) {
                    CNpcMaker* candidate = iterator->second;
                    if (candidate->GetNpcType() == npcType) {
                        source = candidate;
                        break;
                    }
                }
            }
        }

        if (!source) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"battle royal related npc cannot be spawned"
            //     L"(type[%d], color[%d], pledge[%d])",
            //     npcType,
            //     color,
            //     pledgeId)
            // Log.Add(LOG_ERROR,
            //     "[NPC maker] No battle-royal spawn template for event %d, NPC type %d, team color %d, pledge %d. Skipping
            //     this team.", eventId, npcType, color, pledgeId)
            L2_LOG_ERROR(
                Log,
                "[NPC maker] No battle-royal spawn template for event %d, NPC type %d, team color %d, pledge %d; skipping "
                "this team.",
                eventId, npcType, color, pledgeId
            );
            continue;
        }

        // Shipping 0x004ef26c allocates and copy-constructs the clone on
        // CNpcMakerDB, then overwrites m_nPledgeId before retaining the raw
        // pointer in its local/working vectors.
        CNpcMaker* clone = new CNpcMaker(*source);
        clone->SetPledgeId(pledgeId);
        spawned.push_back(clone);
    }

    for (CNpcMaker* maker : spawned) {
        maker->StartTeamBattle(databaseRespawn);
    }
    {
        L2_GUARD(m_NpcMakersWorkingSiegeEventMapSect);
        for (CNpcMaker* maker : spawned) {
            m_NpcMakersWorkingSiegeEventMap.emplace(eventId, maker);
        }
    }
}

void CNpcMakerDB::TB_StartFinal(
    int eventId,
    const char16_t* eventName [[maybe_unused]],
    const std::map<int, std::pair<int, int>>& teams,
    bool databaseRespawn
) {
    L2_GUARD_FUNCTION();

    std::vector<CNpcMaker*> spawned;
    {
        L2_GUARD(m_NpcMakersWorkingSiegeEventMapSect);
        {
            const auto range = m_NpcMakersWorkingSiegeEventMap.equal_range(eventId);
            for (auto iterator = range.first; iterator != range.second; ++iterator) {
                spawned.push_back(iterator->second);
            }
            m_NpcMakersWorkingSiegeEventMap.erase(range.first, range.second);
        }
    }

    for (CNpcMaker* maker : spawned) {
        maker->EndTeamBattle();
    }
    spawned.clear();

    for (int color = 0; color < 2; ++color) {
        int npcType = 1;
        int pledgeId = 0;
        const auto team = teams.find(color);
        if (team != teams.end()) {
            npcType = team->second.first;
            pledgeId = team->second.second;
        }
        if (npcType == 0) {
            npcType = 1;
        }

        CNpcMaker* source = nullptr;
        {
            L2_GUARD(m_AgitTBFinalEventSect);
            {
                const auto range = m_NpcMakerReceivingAgitTB_FinalEvent.equal_range(std::make_pair(eventId, color));
                for (auto iterator = range.first; iterator != range.second; ++iterator) {
                    CNpcMaker* candidate = iterator->second;
                    if (candidate->GetNpcType() == npcType) {
                        source = candidate;
                        break;
                    }
                }
            }
        }

        if (!source) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"final related npc cannot be spawned"
            //     L"(type[%d], color[%d], pledge[%d])",
            //     npcType,
            //     color,
            //     pledgeId)
            // Log.Add(LOG_ERROR,
            //     "[NPC maker] No final-round spawn template for event %d, NPC type %d, team color %d, pledge %d. Skipping
            //     this team.", eventId, npcType, color, pledgeId)
            L2_LOG_ERROR(
                Log,
                "[NPC maker] No final-round spawn template for event %d, NPC type %d, team color %d, pledge %d; skipping "
                "this team.",
                eventId, npcType, color, pledgeId
            );
            continue;
        }

        // Shipping 0x004ef6d8 has the same direct allocation/pledge-write
        // ownership as the battle-royal path.
        CNpcMaker* clone = new CNpcMaker(*source);
        clone->SetPledgeId(pledgeId);
        spawned.push_back(clone);
    }

    for (CNpcMaker* maker : spawned) {
        maker->StartTeamBattle(databaseRespawn);
    }
    {
        L2_GUARD(m_NpcMakersWorkingSiegeEventMapSect);
        for (CNpcMaker* maker : spawned) {
            m_NpcMakersWorkingSiegeEventMap.emplace(eventId, maker);
        }
    }
}

void CNpcMakerDB::TB_EndBattle(int eventId, const char16_t* eventName [[maybe_unused]]) {
    L2_GUARD_FUNCTION();
    std::vector<CNpcMaker*> working;
    {
        L2_GUARD(m_NpcMakersWorkingSiegeEventMapSect);
        {
            const auto range = m_NpcMakersWorkingSiegeEventMap.equal_range(eventId);
            for (auto iterator = range.first; iterator != range.second; ++iterator) {
                working.push_back(iterator->second);
            }
            m_NpcMakersWorkingSiegeEventMap.erase(range.first, range.second);
        }
    }

    // Current 0x004efb44 also releases +0xd8 before EndTeamBattle.
    for (CNpcMaker* maker : working) {
        maker->EndTeamBattle();
    }
}

bool CNpcMakerDB::GetDoorStatus(const char16_t* doorName) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    const auto found = m_NpcMakerReceivingDoorEvent.find(doorName);
    if (found == m_NpcMakerReceivingDoorEvent.end()) {
        return false;
    }

    bool enabled = false;
    // Current 0x004f301c repeats map operator[] and size for every index and
    // dereferences every stored maker without a null guard.
    for (std::uint32_t index = 0; index < m_NpcMakerReceivingDoorEvent[std::u16string(doorName)].size(); ++index) {
        enabled = runtime::CheckedVectorAt(m_NpcMakerReceivingDoorEvent[std::u16string(doorName)], index)->m_bEnabled;
    }
    return enabled;
}

void CNpcMakerDB::RegisterAsOlympiadOperator(int npcSid) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    if (std::find(m_npcOlympiadManager.begin(), m_npcOlympiadManager.end(), npcSid) != m_npcOlympiadManager.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] npcSID[%d] is already exist",
        //     L"NpcMakerDB.cpp",
        //     0x80b,
        //     npcSid)
        // Log.Add(LOG_WARN,
        //     "[NPC maker] Olympiad manager NPC %d is already registered. Ignoring the duplicate.",
        //     npcSid)
        L2_LOG_WARN(Log, "[NPC maker] Olympiad manager NPC %d is already registered; ignoring the duplicate.", npcSid);
        return;
    }
    m_npcOlympiadManager.push_back(npcSid);
}

void CNpcMakerDB::RegisterAsPledgeGameOperator(int npcSid) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    if (std::find(m_npcPledgeGameManager.begin(), m_npcPledgeGameManager.end(), npcSid) != m_npcPledgeGameManager.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] npcSID[%d] is already exist",
        //     L"NpcMakerDB.cpp",
        //     0x894,
        //     npcSid)
        // Log.Add(LOG_WARN,
        //     "[NPC maker] pledge-game manager NPC %d is already registered. Ignoring the duplicate.",
        //     npcSid)
        L2_LOG_WARN(Log, "[NPC maker] pledge-game manager NPC %d is already registered; ignoring the duplicate.", npcSid);
        return;
    }
    m_npcPledgeGameManager.push_back(npcSid);
}

void CNpcMakerDB::RegisterAsFieldCycleManager(int npcSid) {
    L2_GUARD_FUNCTION();
    // Unlike the three operator registries, the target does not perform a
    // duplicate scan here.
    L2_GUARD(m_ReceiverLock);
    m_npcFieldCycleManager.push_back(npcSid);
}

void CNpcMakerDB::RegisterAsWorldChatOperator(int npcSid) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    if (std::find(m_npcWorldChatManager.begin(), m_npcWorldChatManager.end(), npcSid) != m_npcWorldChatManager.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] npcSID[%d] is already exist",
        //     L"NpcMakerDB.cpp",
        //     0xa0e,
        //     npcSid)
        // Log.Add(LOG_WARN,
        //     "[NPC maker] world-chat manager NPC %d is already registered. Ignoring the duplicate.",
        //     npcSid)
        L2_LOG_WARN(Log, "[NPC maker] world-chat manager NPC %d is already registered; ignoring the duplicate.", npcSid);
        return;
    }
    m_npcWorldChatManager.push_back(npcSid);
}

CNPC* CNpcMakerDB::GetOlympiadOperatorBase() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    if (m_npcOlympiadManager.empty()) {
        return nullptr;
    }
    // The target resolves only front(). A stale first registration masks
    // every later entry instead of being skipped.
    return ResolveNpcObjectW(m_npcOlympiadManager.front());
}

CNPC* CNpcMakerDB::GetPledgeGameOperatorBase() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    if (m_npcPledgeGameManager.empty()) {
        return nullptr;
    }
    return ResolveNpcObjectW(m_npcPledgeGameManager.front());
}

CNPC* CNpcMakerDB::GetWorldChatOperatorBase() const {
    L2_GUARD_FUNCTION();
    if (m_npcWorldChatManager.empty()) {
        return nullptr;
    }
    return ResolveNpcObjectW(m_npcWorldChatManager.front());
}

NpcMakerEx* CNpcMakerDB::GetInstantZoneMakerEx(int inzoneId, const char16_t* makerName, bool checkName) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InZoneMakerLock);
    const auto found = m_NpcMakerExReceivingInZoneEvent.find(inzoneId);
    if (found == m_NpcMakerExReceivingInZoneEvent.end()) {
        const std::wstring makerNameWide = makerName ? Utf16ToWide(makerName) : std::wstring();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] "
        //     L"m_NpcMakerExReceivingInZoneEvent cannot find "
        //     L"NpcMakerExEventInfo. InZone ID [%d] "
        //     L"wsMakerExName[%s]",
        //     L"NpcMakerDB.cpp",
        //     0x6ae,
        //     inzoneId,
        //     makerName
        //         ? makerNameWide.c_str()
        //         : nullptr)
        // Log.Add(LOG_WARN,
        //     "[NPC maker] Cannot find maker '%s' in instance zone %d (%s): the instance zone has no registered maker
        //     list.", makerName ? WideToUtf8(makerNameWide).c_str() : "(unspecified)", inzoneId, checkName ? "name check
        //     enabled" : "name check disabled")
        L2_LOG_WARN(
            Log,
            "[NPC maker] Cannot find maker '%s' in instance zone %d (%s): the instance zone has no registered maker list.",
            makerName ? WideToUtf8(makerNameWide).c_str() : "(unspecified)", inzoneId,
            checkName ? "name check enabled" : "name check disabled"
        );
        return nullptr;
    }

    // Registration order is observable. In particular, duplicate names
    // resolve to the first concrete maker, unlike a key-overwriting map.
    // Current 0x004ed058 copies the registered vector before traversing it.
    const NpcMakerExEventInfoListType eventInfos = found->second;
    for (const NpcMakerExEventInfo& info : eventInfos) {
        NpcMakerEx* maker = info.pMakerEx;
        if (!maker) {
            continue;
        }
        if (!checkName || std::u16string_view(maker->GetName()) == std::u16string_view(makerName)) {
            return maker;
        }
    }
    const std::wstring makerNameWide = makerName ? Utf16ToWide(makerName) : std::wstring();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] "
    //     L"vMakerExEventInfoList cannot find "
    //     L"NpcMakerExEventInfo. InZone ID [%d] "
    //     L"wsMakerExName[%s]",
    //     L"NpcMakerDB.cpp",
    //     0x6a9,
    //     inzoneId,
    //     makerName
    //         ? makerNameWide.c_str()
    //         : nullptr)
    // Log.Add(LOG_WARN,
    //     "[NPC maker] Cannot find maker '%s' in instance zone %d (%s): no live maker matches the lookup.",
    //     makerName ? WideToUtf8(makerNameWide).c_str() : "(unspecified)", inzoneId,
    //     checkName ? "name check enabled" : "name check disabled")
    L2_LOG_WARN(
        Log, "[NPC maker] Cannot find maker '%s' in instance zone %d (%s): no live maker matches the lookup.",
        makerName ? WideToUtf8(makerNameWide).c_str() : "(unspecified)", inzoneId,
        checkName ? "name check enabled" : "name check disabled"
    );
    return nullptr;
}

void CNpcMakerDB::OlympiadFieldEvent(int zeroBasedFieldIndex, int matchType, int state) {
    L2_GUARD_FUNCTION();
    // 0x004eb678 has an asymmetric guard: matchType may be
    // negative, while the field and state must be non-negative and only the
    // upper match-type bound is checked.
    if (zeroBasedFieldIndex < 0 || state < 0 || matchType >= 3) {
        return;
    }

    L2_GUARD(m_ReceiverLock);

    SharedOlympiadInfo* const olympiad = SharedOlympiadInfo::GetInfo();
    sOlympiadFieldInfo* const field =
        zeroBasedFieldIndex < SharedOlympiadInfo::FIELD_COUNT ? &olympiad->arrFieldInfo[zeroBasedFieldIndex] : nullptr;
    if (!field) {
        return;
    }

    const auto subscriber = m_npcMakerExOlympiadFieldEvent.find(field->nInzoneTypeId);
    if (subscriber != m_npcMakerExOlympiadFieldEvent.end() && subscriber->second) {
        NpcMakerEx& maker = *subscriber->second;
        maker.SetInZoneID(static_cast<unsigned int>(field->nInzoneSId));
        maker.OlympiadFieldEvent(zeroBasedFieldIndex + 1, matchType, state);
    }

    if (state != 1) {
        return;
    }
    for (auto iterator = m_npcOlympiadManager.begin(); iterator != m_npcOlympiadManager.end(); ++iterator) {
        CNPC* npc = ResolveNpcObjectW(*iterator);
        if (npc) {
            npc->OnOlympiadGamePrepared(zeroBasedFieldIndex + 1, matchType);
        }
    }
}

void CNpcMakerDB::FieldCycleEvent(int fieldId, int step) {
    L2_GUARD_FUNCTION();
    if (fieldId < 0 || step < 0) {
        return;
    }

    L2_GUARD(m_ReceiverLock);

    for (auto iterator = mNpcMakerExFieldCycleEvent.lower_bound(fieldId); iterator != mNpcMakerExFieldCycleEvent.end();
        ++iterator) {
        iterator->second->FieldCycleEvent(fieldId, step);
    }
}

void CNpcMakerDB::FieldCycleStepExpired(int cycleId, int eventId, int step) {
    L2_GUARD_FUNCTION();
    if (eventId < 0 || step < 0 || m_npcFieldCycleManager.empty()) {
        return;
    }

    L2_GUARD(m_ReceiverLock);
    // The target traverses the live vector while retaining m_ReceiverLock.
    // Repeated SIDs receive repeated events in vector order.
    for (const int npcSid : m_npcFieldCycleManager) {
        CNPC* npc = ResolveNpcObjectW(npcSid);
        if (npc) {
            npc->FieldCycleStepExpired(cycleId, eventId, step);
        }
    }
}

void CNpcMakerDB::FortressEvent(int residenceId, int eventId, int argument0, int argument1, int argument2, char16_t* text) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    const auto subscribers = m_NpcMakerExReceivingFortressEvent.find(std::make_pair(residenceId, eventId));
    if (subscribers == m_NpcMakerExReceivingFortressEvent.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fortress event maker does not "
        //     L"exist in event maker map, fortress[%d] event[%d]",
        //     L"NpcMakerDB.cpp",
        //     0x55c,
        //     residenceId,
        //     eventId)
        // Log.Add(LOG_WARN,
        //     "[NPC maker] No subscribers for fortress %d event %d. Event not dispatched.",
        //     residenceId, eventId)
        L2_LOG_WARN(Log, "[NPC maker] No subscribers for fortress %d event %d; event not dispatched.", residenceId, eventId);
        return;
    }

    // Current 0x004efd44 copies the registered vector and calls that
    // copy before releasing m_ReceiverLock.
    const NpcMakerExEnableListType subscriptions = subscribers->second;
    for (const auto& [maker, spawn] : subscriptions) {
        maker->OnFortressEvent(residenceId, eventId, spawn, argument0, argument1, argument2, text);
    }
}

void CNpcMakerDB::_CreateInstantZoneMakerEx(int inzoneId, NpcMakerExEventInfoListType& templates) {
    L2_GUARD_FUNCTION();
    NpcMakerExEventInfoListType instances;
    for (const NpcMakerExEventInfo& templateInfo : templates) {
        NpcMakerEx* source = templateInfo.pMakerEx;

        NpcMakerEx* instance = source->Copy();
        if (!instance) {
            continue;
        }

        instance->SetInZoneID(static_cast<unsigned int>(inzoneId));
        instance->SetDefineExInZoneID(static_cast<unsigned int>(inzoneId));
        RegisterInstantZoneDoorEventMakerEx(inzoneId, source, instance);
        instances.push_back(NpcMakerExEventInfo{templateInfo.nEventId, templateInfo.nEnable, instance});
    }

    if (m_NpcMakerExReceivingInZoneEvent.find(inzoneId) != m_NpcMakerExReceivingInZoneEvent.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] inzone event maker "
        //     L"instance already exists for inzone id",
        //     L"NpcMakerDB.cpp",
        //     0x655)
        // Log.Add(LOG_WARN,
        //     "[NPC maker] Instance zone %d already has makers. Deleting the old set before publishing %zu replacement
        //     entries.", inzoneId, instances.size())
        L2_LOG_WARN(
            Log,
            "[NPC maker] Instance zone %d already has makers; deleting the old set before publishing %zu replacement "
            "entries.",
            inzoneId, instances.size()
        );
        _DeleteInstantZoneMakerEx(inzoneId);
    }
    m_NpcMakerExReceivingInZoneEvent[inzoneId] = std::move(instances);
}

void CNpcMakerDB::_DeleteInstantZoneMakerEx(int inzoneId) {
    L2_GUARD_FUNCTION();
    const auto instances = m_NpcMakerExReceivingInZoneEvent.find(inzoneId);
    if (instances != m_NpcMakerExReceivingInZoneEvent.end()) {
        const NpcMakerExEventInfoListType deleting = instances->second;
        for (const NpcMakerExEventInfo& info : deleting) {
            NpcMakerEx* maker = info.pMakerEx;
            if (!maker) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] try to delete "
                //     L"invalid inzone maker ex",
                //     L"NpcMakerDB.cpp",
                //     0x67e)
                // Log.Add(LOG_WARN,
                //     "[NPC maker] Null maker entry while deleting instance zone %d. Skipping this entry.",
                //     inzoneId)
                L2_LOG_WARN(
                    Log, "[NPC maker] Null maker entry while deleting instance zone %d; skipping this entry.", inzoneId
                );
                continue;
            }

            const int deleteCount = maker->IncreaseInZoneDelete();
            if (deleteCount == 1) {
                maker->Delete();
            }
        }
        m_NpcMakerExReceivingInZoneEvent.erase(instances);
    }
    {
        L2_GUARD(m_InZoneDoorLock);
        m_NpcMakerExReceivingInZoneDoorEvent.erase(inzoneId);
    }
}

void CNpcMakerDB::InstantZoneEvent(
    int inzoneTypeId, int clusterId, int inzoneId, int eventId, int argument0, const std::uint8_t* packetTail
) {
    L2_GUARD_FUNCTION();
    if (eventId == -1) {
        L2_GUARD(m_InZoneMakerLock);
        const auto templates = m_NpcMakerExReceivingInZoneEventDB.find(std::make_pair(inzoneTypeId, clusterId));
        if (templates == m_NpcMakerExReceivingInZoneEventDB.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] inzone event maker does "
            //     L"not exist in event maker DB, inzone type[%d] "
            //     L"clusterID[%d]",
            //     L"NpcMakerDB.cpp",
            //     0x57d,
            //     inzoneTypeId,
            //     clusterId)
            // Log.Add(LOG_ERROR,
            //     "[NPC maker] Cannot delete makers for instance zone %d: template type %d, cluster %d is not registered.",
            //     inzoneId, inzoneTypeId, clusterId)
            L2_LOG_ERROR(
                Log,
                "[NPC maker] Cannot delete makers for instance zone %d: template type %d, cluster %d is not registered.",
                inzoneId, inzoneTypeId, clusterId
            );
            return;
        }

        const auto instances = m_NpcMakerExReceivingInZoneEvent.find(inzoneId);
        if (instances != m_NpcMakerExReceivingInZoneEvent.end()) {
            const NpcMakerExEventInfoListType eventInfos = instances->second;
            for (const NpcMakerExEventInfo& info : eventInfos) {
                if (info.pMakerEx) {
                    UnregisterInstantZoneNotifySubscriber(InstantZoneKey{inzoneTypeId, clusterId}, info.pMakerEx);
                }
            }
        }
        _DeleteInstantZoneMakerEx(inzoneId);
        return;
    }

    NpcMakerExEventInfoListType eventInfos;
    {
        L2_GUARD(m_InZoneMakerLock);
        const auto templates = m_NpcMakerExReceivingInZoneEventDB.find(std::make_pair(inzoneTypeId, clusterId));
        if (templates == m_NpcMakerExReceivingInZoneEventDB.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] inzone event maker does "
            //     L"not exist in event maker DB, inzone type[%d] "
            //     L"clusterID[%d]",
            //     L"NpcMakerDB.cpp",
            //     0x5a7,
            //     inzoneTypeId,
            //     clusterId)
            // Log.Add(LOG_ERROR,
            //     "[NPC maker] Cannot process instance-zone event %d for zone %d: template type %d, cluster %d is not
            //     registered.", eventId, inzoneId, inzoneTypeId, clusterId)
            L2_LOG_ERROR(
                Log,
                "[NPC maker] Cannot process instance-zone event %d for zone %d: template type %d, cluster %d is not "
                "registered.",
                eventId, inzoneId, inzoneTypeId, clusterId
            );
            return;
        }

        if (eventId == -2) {
            _CreateInstantZoneMakerEx(inzoneId, templates->second);
            _SetInstantZoneMapFlag(inzoneId, packetTail);
        }

        const auto instances = m_NpcMakerExReceivingInZoneEvent.find(inzoneId);
        if (instances == m_NpcMakerExReceivingInZoneEvent.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] inzone maker does not "
            //     L"exist in event maker map, inzone type[%d] "
            //     L"event[%d]",
            //     L"NpcMakerDB.cpp",
            //     0x5c3,
            //     inzoneTypeId,
            //     eventId)
            // Log.Add(LOG_ERROR,
            //     "[NPC maker] No maker instances for zone %d (type %d, cluster %d). Event %d not dispatched.",
            //     inzoneId, inzoneTypeId, clusterId, eventId)
            L2_LOG_ERROR(
                Log, "[NPC maker] No maker instances for zone %d (type %d, cluster %d); event %d not dispatched.", inzoneId,
                inzoneTypeId, clusterId, eventId
            );
            return;
        }

        // Current 0x004f7498 copies the live vector, then drops
        // m_InZoneMakerLock before invoking script callbacks.
        eventInfos = instances->second;
    }

    for (const NpcMakerExEventInfo& info : eventInfos) {
        if (!info.pMakerEx || info.nEventId != eventId) {
            continue;
        }
        info.pMakerEx->OnInstantZoneEvent(inzoneTypeId, clusterId, inzoneId, eventId, info.nEnable, argument0, 0, 0, 0, 0);
    }
}

void CNpcMakerDB::OlympiadInZoneMakerCreate(int inzoneTypeId, int inzoneId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InZoneMakerLock);
    constexpr int clusterId = 0;
    const auto templates = m_NpcMakerExReceivingInZoneEventDB.find(std::make_pair(inzoneTypeId, clusterId));
    if (templates == m_NpcMakerExReceivingInZoneEventDB.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] inzone event maker does not "
        //     L"exist in event maker DB, inzone type[%d] clusterID[%d]",
        //     L"NpcMakerDB.cpp",
        //     0x820,
        //     inzoneTypeId,
        //     clusterId)
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] Cannot create Olympiad makers for instance zone %d: template type %d, cluster %d is not
        //     registered.", inzoneId, inzoneTypeId, clusterId)
        L2_LOG_ERROR(
            Log,
            "[NPC maker] Cannot create Olympiad makers for instance zone %d: template type %d, cluster %d is not "
            "registered.",
            inzoneId, inzoneTypeId, clusterId
        );
        return;
    }

    _CreateInstantZoneMakerEx(inzoneId, templates->second);
}

void CNpcMakerDB::PledgeGameInZoneMakerCreate(int inzoneTypeId, int inzoneId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_InZoneMakerLock);
    constexpr int clusterId = 0;
    const auto templates = m_NpcMakerExReceivingInZoneEventDB.find(std::make_pair(inzoneTypeId, clusterId));
    if (templates == m_NpcMakerExReceivingInZoneEventDB.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] inzone event maker does not "
        //     L"exist in event maker DB, inzone type[%d] clusterID[%d]",
        //     L"NpcMakerDB.cpp",
        //     0x8a9,
        //     inzoneTypeId,
        //     clusterId)
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] Cannot create pledge-game makers for instance zone %d: template type %d, cluster %d is not
        //     registered.", inzoneId, inzoneTypeId, clusterId)
        L2_LOG_ERROR(
            Log,
            "[NPC maker] Cannot create pledge-game makers for instance zone %d: template type %d, cluster %d is not "
            "registered.",
            inzoneId, inzoneTypeId, clusterId
        );
        return;
    }

    _CreateInstantZoneMakerEx(inzoneId, templates->second);
}

void CNpcMakerDB::OnNpcPosEvent(char16_t* eventName, bool enabled) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    const std::u16string windowsName(eventName);
    const auto regular = m_NpcMakerReceivingNpcPosEvent.find(windowsName);
    if (regular != m_NpcMakerReceivingNpcPosEvent.end()) {
        auto* controller = new NpcPosEventController();
        controller->AddNpcPosEvent(regular->second, enabled ? 1 : 0);
        controller->StartTimer();
    }

    const auto extended = mNpcMakerExReceivingNpcPosEvent.find(windowsName);
    if (extended != mNpcMakerExReceivingNpcPosEvent.end()) {
        auto* controller = new NpcPosExEventController();
        controller->AddNpcPosExEvent(extended->second, enabled ? 1 : 0);
        controller->StartTimer();
    }
}

void CNpcMakerDB::OnNpcPosesEvent(const NpcPosEventNameList& despawn, const NpcPosEventNameList& spawn) {
    L2_GUARD_FUNCTION();
    if (despawn.empty() && spawn.empty()) {
        return;
    }

    auto* regularController = new NpcPosEventController();
    auto* extendedController = new NpcPosExEventController();
    for (const NpcPosEventName& name : despawn) {
        {
            // 0x004f441b holds one receiver lock across both registry
            // lookups and controller additions for this name only.
            L2_GUARD(m_ReceiverLock);
            const auto regular = m_NpcMakerReceivingNpcPosEvent.find(name);
            if (regular != m_NpcMakerReceivingNpcPosEvent.end()) {
                regularController->AddNpcPosEvent(regular->second, 0);
            }
            const auto extended = mNpcMakerExReceivingNpcPosEvent.find(name);
            if (extended != mNpcMakerExReceivingNpcPosEvent.end()) {
                extendedController->AddNpcPosExEvent(extended->second, 0);
            }
        }
    }
    for (const NpcPosEventName& name : spawn) {
        {
            // 0x004f44e0 uses the same per-name scope for the spawn list.
            L2_GUARD(m_ReceiverLock);
            const auto regular = m_NpcMakerReceivingNpcPosEvent.find(name);
            if (regular != m_NpcMakerReceivingNpcPosEvent.end()) {
                regularController->AddNpcPosEvent(regular->second, 1);
            }
            const auto extended = mNpcMakerExReceivingNpcPosEvent.find(name);
            if (extended != mNpcMakerExReceivingNpcPosEvent.end()) {
                extendedController->AddNpcPosExEvent(extended->second, 1);
            }
        }
    }

    regularController->StartTimer();
    extendedController->StartTimer();
}

void CNpcMakerDB::OnHunterGuildBless(bool enabled) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ReceiverLock);
    // Current 0x004ea120 iterates the live vector while retaining the owner
    // lock. The Guard destructor supplies TemplateHelper.h:0x65 on Leave.
    for (NpcMakerEx* maker : m_npcHunterGuildBlessMakers) {
        if (maker) {
            maker->OnHunterGuildBless(enabled);
        }
    }
}

void CNpcMakerDB::CreateAllNPC() {
    L2_GUARD_FUNCTION();
    WaitForL2ServerPreparation();
    // Log.Add(
    //     LOG_NORMAL,
    //     "Making server create NPC objects")
    // Log.Add(LOG_NORMAL,
    //     "[NPC startup] Scheduling regular and extended NPC maker initialization.")
    L2_LOG_INFO(Log, "[NPC startup] Scheduling regular and extended NPC maker initialization.");
    // Target CNpcMakerDB::CreateAllNPC resets only these two counters. In
    // particular, repeated calls do not clear either creator barrier or the
    // loading-complete flag. Each call simply queues another id-1 timer.
    runtime::raw_atomic::StoreI32Relaxed(&m_nMakerProcessed, 0);
    runtime::raw_atomic::StoreI32Relaxed(&m_nSiegeRelatedProcessed, 0);
    AddTimer(1000U, 1);
}

void CNpcMakerDB::WaitForL2ServerPreparation() {
    L2_GUARD_FUNCTION();
    CDeadlockCheckerInactivator deadlockCheckerInactivator;
    // Current EXE 0x004e99f4 spins directly on g_ns.m_pServerSocket before
    // probing the protection object.
    bool socketWaitingReported = false;
    while (!g_ns.GetServerSocket()) {
        if (!socketWaitingReported) {
            L2_LOG_INFO(Log, "[NPC startup] Waiting for the L2Server socket before checking protection status.");
            socketWaitingReported = true;
        }
        std::this_thread::yield();
    }
    if (socketWaitingReported) {
        L2_LOG_INFO(Log, "[NPC startup] L2Server socket is available; checking protection status.");
    }

    bool waitingReported = false;
    for (;;) {
        if (CheckMutex()) {
            if (waitingReported) {
                // Log.Add(LOG_NORMAL, "[NPC startup] L2Server protection check passed, continuing initialization.")
                L2_LOG_INFO(Log, "[NPC startup] L2Server protection check passed; continuing initialization.");
            }
            return;
        }
        // Noise: Expected protection polling is reported once below. Lock API failures keep their own diagnostics.
        // Log.Add(
        //     LOG_ERROR,
        //     L"Start after seeing 'L2Server is released from protection "
        //     L"mode!!!' in L2Server")
        if (!waitingReported) {
            // Log.Add(LOG_NORMAL,
            //     "[NPC startup] Waiting for L2Server's protection check to pass, retrying once a second.")
            L2_LOG_INFO(Log, "[NPC startup] Waiting for L2Server's protection check to pass; retrying once a second.");
            waitingReported = true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void CNpcMakerDB::FastCreateAllNPC(int threadId) {
    L2_GUARD_FUNCTION();
    CDeadlockCheckerInactivator deadlockCheckerInactivator;
    // Log.Add(
    //     LOG_NORMAL,
    //     "Making server create NPC objects "
    //     "[Fast Version][%d]",
    //     threadId)
    // Log.Add(LOG_NORMAL,
    //     "[NPC startup] Starting regular maker traversal on creation lane %d.",
    //     threadId)
    L2_LOG_INFO(
        Log,
        // "[NPC startup] Starting regular maker traversal on creation lane %d.",
        "[NPC startup] Processing regular NPC makers on worker %d.", threadId
    );
    const int threadCount = ::GetIOThreadCount();
    for (int index = threadId; index < GetNpcMakerSize(); index += threadCount) {
        ProcessOneNpcMaker(index);
    }

    WaitForOtherNPCCreator(false);
    // Log.Add(
    //     LOG_NORMAL,
    //     "NPC Make process done.. Thread[%d]",
    //     threadId)
    // Log.Add(LOG_NORMAL,
    //     "[NPC startup] Regular maker traversal passed the creator barrier on lane %d. Scheduling extended makers.",
    //     threadId)
    L2_LOG_INFO(
        Log,
        // "[NPC startup] Regular maker traversal passed the creator barrier on lane %d. Scheduling extended makers.",
        // "[NPC startup] Worker %d finished processing regular NPC makers and waiting for the other workers. Scheduling
        // extended makers.",
        "[NPC startup] Worker %d completed the regular NPC maker phase. Other workers have finished this phase; scheduling "
        "extended makers.",
        threadId
    );
    // This timer is unconditional, including an empty MakerEx list.
    AddTimer(100U, threadId + 100);
}

void CNpcMakerDB::WaitForOtherNPCCreator(bool exPhase) {
    L2_GUARD_FUNCTION();
    const auto waitForAll = [](const volatile std::int32_t* counter) {
        while (runtime::raw_atomic::LoadI32Relaxed(counter) < ::GetIOThreadCount()) {
            // Sleep(0) yields the remainder of the current Windows
            // quantum, std::this_thread::yield is its portable peer.
            std::this_thread::yield();
        }
    };

    if (!exPhase) {
        runtime::raw_atomic::AddI32(&m_nMakerDoneCount, 1);
        waitForAll(&m_nMakerDoneCount);
        runtime::raw_atomic::StoreI32Relaxed(&m_nMakerProcessed, 0);
        return;
    }

    runtime::raw_atomic::AddI32(&m_nMakerExDoneCount, 1);
    waitForAll(&m_nMakerExDoneCount);

    const std::int32_t previous = runtime::raw_atomic::CompareExchangeI32(&m_MakerFinished, 1, 0);
    // InterlockedCompareExchange returns the old value. The target calls
    // OnMakerFinished for every old value except 1. Only 0 is reachable in
    // the constructor-initialized one-shot startup.
    if (previous != 1) {
        OnMakerFinished();
    }
}

void CNpcMakerDB::TimerExpired(int timerId) {
    L2_GUARD_FUNCTION();
    const auto addTimer = [this](std::uint32_t delay, int id) { AddTimer(delay, id); };

    if (timerId == 1000) {
        // This is a one-shot post-startup status heartbeat. The literal
        // check precedes fast-mode decoding, so MakerEx index 900 collides
        // with this id in the shipping target.
        g_ns.Send("ch", 0xfe, 0x19);
        return;
    }

    if (g_bFastEnterworld) {
        CDeadlockCheckerInactivator deadlockCheckerInactivator;
        int index = timerId - 100;
        int processed = 0;
        const int threadCount = ::GetIOThreadCount();
        const auto indexInRange = [this](int value) {
            return static_cast<std::uint64_t>(static_cast<std::int64_t>(value)) < mNpcMakerExList.size();
        };
        while (indexInRange(index) && processed < 100) {
            ProcessOneNpcMakerEx(index);
            index += threadCount;
            ++processed;
        }
        if (indexInRange(index)) {
            AddTimer(100U, index + 100);
        } else {
            WaitForOtherNPCCreator(true);
            // Log.Add(
            //     LOG_NORMAL,
            //     "NPC MakerEx process done.. Thread[%d]",
            //     (index % 100) % threadCount)
            // Log.Add(LOG_NORMAL,
            //     "[NPC startup] Extended maker traversal passed the creator barrier (timer %d).",
            //     timerId)
            L2_LOG_INFO(
                Log,
                // "[NPC startup] Extended maker traversal passed the creator barrier (timer %d).",
                // "[NPC startup] Finished processing extended NPC makers and waiting for the other workers (timer %d).",
                "[NPC startup] Completed the extended NPC maker phase (timer %d). Other workers have finished this phase.",
                timerId
            );
        }
        return;
    }

    switch (timerId) {
        case 1:
            // Both calls are present in the shipping machine code. The
            // first result is discarded and the second controls the re-arm.
            ProcessOneNpcMaker();
            if (ProcessOneNpcMaker()) {
                addTimer(100U, 1);
            } else {
                runtime::raw_atomic::StoreI32Relaxed(&m_nMakerProcessed, 0);
                addTimer(100U, 2);
            }
            return;

        case 2:
            ProcessOneNpcMakerEx();
            if (ProcessOneNpcMakerEx()) {
                addTimer(100U, 2);
            } else {
                OnMakerFinished();
            }
            return;

        default:
            return;
    }
}

void CNpcMakerDB::OnMakerFinished() {
    L2_GUARD_FUNCTION();
    m_stLoadingProcess.bIsFinished = true;

    const NpcSpawnInfo* const spawnInfo = NpcSpawnInfo::Inst();
    g_ns.Send("cdd", 0x82, runtime::crt::Time32Now(), spawnInfo->NpcMaker_RequestCreate);
    InitDynamicQuestMaker();
    InitEventCampaignMaker();
    AddTimer(90000U, 1000);
}

void CNpcMakerDB::InitDynamicQuestMaker() {
    L2_GUARD_FUNCTION();
    g_ns.Send("ccdddddd", 0xec, 3, 0, 0, 0, 0, 0, 0);
}

void CNpcMakerDB::InitEventCampaignMaker() {
    L2_GUARD_FUNCTION();
    g_ns.Send("chcdddddd", 0xfe, 0x10, 3, 0, 0, 0, 0, 0, 0);
}

void CNpcMakerDB::AddNpcMaker(CNpcMaker* maker) {
    L2_GUARD_FUNCTION();
    // The target vector is a raw observer registry. Nulls and duplicates are
    // appended without validation, reference-count changes or locking.
    m_NpcMakers.push_back(maker);
}

CNpcMaker* CNpcMakerDB::GetNpcMaker(int index) {
    L2_GUARD_FUNCTION();
    // Deliberately no bounds or sign check: callers in the shipping target
    // perform only their own signed `index < GetNpcMakerSize()` comparison.
    return runtime::CheckedVectorAt(m_NpcMakers, static_cast<std::size_t>(index));
}

int CNpcMakerDB::GetNpcMakerSize() {
    L2_GUARD_FUNCTION();
    return static_cast<int>(m_NpcMakers.size());
}

bool CNpcMakerDB::AddNpcMakerEx(NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    if (!maker) {
        return false;
    }
    const std::u16string makerName(maker->GetName());
    if (mNpcMakerExMap.find(makerName) != mNpcMakerExMap.end()) {
        const std::string makerNameWide = Utf16ToUtf8(makerName.data());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Duplicated npcmaker name[%s]",
        //     L"NpcMakerDB.cpp",
        //     0x9c1,
        //     makerNameWide.c_str())
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] Extended maker '%s' is already registered. Rejecting the duplicate.",
        //     WideToUtf8(makerNameWide).c_str())
        L2_LOG_ERROR(
            Log, "[NPC maker] Extended maker '%s' is already registered; rejecting the duplicate.",
            makerNameWide.c_str()
        );
        return false;
    }

    // Target order is observable during invalid parser input: map first,
    // then the raw observer vector. No lock or reference transfer occurs.
    mNpcMakerExMap.emplace(makerName, maker);
    mNpcMakerExList.push_back(maker);
    return true;
}

NpcMakerEx* CNpcMakerDB::FindNpcMakerEx(const std::u16string& name) const {
    L2_GUARD_FUNCTION();
    const auto found = mNpcMakerExMap.find(name);
    return found == mNpcMakerExMap.end() ? nullptr : found->second;
}

CTerritory* CNpcMakerDB::GetTerritory(const std::u16string& name) const {
    L2_GUARD_FUNCTION();
    const auto found = m_TerritoryMap.find(name);
    if (found == m_TerritoryMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Reference undefined NameId[%s]",
        //     "NpcMakerDB.cpp",
        //     0x93,
        //     name.data())
        // Log.Add(LOG_ERROR,
        //     "[NPC maker] Territory '%s' is not registered. Returning no territory.",
        //     Utf16ToUtf8(name.view()).c_str())
        L2_LOG_ERROR(
            Log, "[NPC maker] Territory '%s' is not registered; returning no territory.",
            Utf16ToUtf8(runtime::StringView(name)).c_str()
        );
        return nullptr;
    }
    return found->second;
}

std::map<std::u16string, CTerritory*, runtime::CStringLess<std::u16string>>* CNpcMakerDB::GetTerritoryMap() {
    return &m_TerritoryMap;
}

void CNpcMakerDB::InsertTerritory(CTerritory* territory) {
    L2_GUARD_FUNCTION();
    m_TerritoryMap.insert(std::make_pair(territory->GetNameString(), territory));
}

void CNpcMakerDB::AddDomainTerritory(int domainId, CTerritory* territory) {
    L2_GUARD_FUNCTION();
    DomainStructType entry;
    entry.m_nDomainID = domainId;
    entry.m_pTerritory = territory;
    m_DomainTerritories.push_back(entry);
}

int CNpcMakerDB::FindDomain(const FVector& position) {
    L2_GUARD_FUNCTION();
    for (const DomainStructType& entry : m_DomainTerritories) {
        if (entry.m_pTerritory->IsInside(position, false)) {
            return entry.m_nDomainID;
        }
    }
    return 0;
}

bool CNpcMakerDB::ProcessOneNpcMaker() {
    L2_GUARD_FUNCTION();
    const int index = runtime::raw_atomic::LoadI32Relaxed(&m_nMakerProcessed);
    if (index >= GetNpcMakerSize()) {
        // Log.Add(
        //     LOG_NORMAL,
        //     "NPC Make process done..")
        // Log.Add(LOG_NORMAL,
        //     "[NPC startup] Finished traversing the regular maker list, NPC creation replies may still be pending.")
        L2_LOG_INFO(
            Log, "[NPC startup] Finished traversing the regular maker list; NPC creation replies may still be pending."
        );
        return false;
    }

    CNpcMaker* maker = GetNpcMaker(index);
    if (!IsSpawnPossible(maker)) {
        // Exact legacy behavior: IsSpawnPossible(false) returns true from
        // this method without advancing the index. A corrupt registry thus
        // retries the same slot indefinitely.
        return true;
    }

    if (g_bSpawnLog) {
        // Log.Add(
        //     LOG_NORMAL,
        //     "  Processing NPC Maker #%d/%d",
        //     index + 1,
        //     GetNpcMakerSize())
        // Log.Add(LOG_NORMAL,
        //     "[NPC startup] Processing regular maker %d of %d.",
        //     index + 1, GetNpcMakerSize())
        L2_LOG_INFO(Log, "[NPC startup] Processing regular maker %d of %d.", index + 1, GetNpcMakerSize());
    }
    if (InitializeTerritoryRandomPositions(maker->mTerritoryInfo, maker->m_bFlying, 400)) {
        maker->OnStart();
    }
    if (maker->GetSpawnTimeType() > SPAWN_NORMAL &&
        (maker->GetSpawnTimeType() < SPAWN_DOOR_OPEN ||
            (maker->GetSpawnTimeType() > SPAWN_DOOR_OPEN && maker->GetSpawnTimeType() < SPAWN_POS_EVENT))) {
        runtime::raw_atomic::AddI32(&m_nSiegeRelatedProcessed, 1);
    }
    runtime::raw_atomic::AddI32(&m_nMakerProcessed, 1);
    return true;
}

bool CNpcMakerDB::ProcessOneNpcMaker(int index) {
    L2_GUARD_FUNCTION();
    if (index >= GetNpcMakerSize()) {
        // Noise: An out-of-range worker index is not whole-phase completion. The creator barrier reports phase progress.
        // Log.Add(
        //     LOG_NORMAL,
        //     "NPC Make process done.. Thread[%d]",
        //     CThreadManager::GetThreadID())
        return false;
    }

    CNpcMaker* maker = GetNpcMaker(index);
    if (IsSpawnPossible(maker)) {
        if (g_bSpawnLog) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "  Processing NPC Maker #%d/%d",
            //     index + 1,
            //     GetNpcMakerSize())
            // Log.Add(LOG_NORMAL,
            //     "[NPC startup] Processing regular maker %d of %d.",
            //     index + 1, GetNpcMakerSize())
            L2_LOG_INFO(Log, "[NPC startup] Processing regular maker %d of %d.", index + 1, GetNpcMakerSize());
        }
        if (InitializeTerritoryRandomPositions(maker->mTerritoryInfo, maker->m_bFlying, 400)) {
            maker->OnStart();
        }
    }
    if (maker->GetSpawnTimeType() > SPAWN_NORMAL &&
        (maker->GetSpawnTimeType() < SPAWN_DOOR_OPEN ||
            (maker->GetSpawnTimeType() > SPAWN_DOOR_OPEN && maker->GetSpawnTimeType() < SPAWN_POS_EVENT))) {
        runtime::raw_atomic::AddI32(&m_nSiegeRelatedProcessed, 1);
    }
    return true;
}

bool CNpcMakerDB::ProcessOneNpcMakerEx() {
    L2_GUARD_FUNCTION();
    const std::int32_t index = runtime::raw_atomic::LoadI32Relaxed(&m_nMakerProcessed);
    if (index < 0 || static_cast<std::size_t>(index) >= mNpcMakerExList.size()) {
        return false;
    }

    NpcMakerEx* maker = runtime::CheckedVectorAt(mNpcMakerExList, static_cast<std::size_t>(index));
    if (IsSpawnPossible(maker) && maker->InitializeRandomPos()) {
        DispatchMakerStartEvent(*maker);
    }
    // Unlike the regular overload, the target advances past null MakerEx
    // slots and failed territory/event initialization.
    runtime::raw_atomic::AddI32(&m_nMakerProcessed, 1);
    return true;
}

bool CNpcMakerDB::ProcessOneNpcMakerEx(int index) {
    L2_GUARD_FUNCTION();
    if (index < 0 || static_cast<std::size_t>(index) >= mNpcMakerExList.size()) {
        return false;
    }

    NpcMakerEx* maker = runtime::CheckedVectorAt(mNpcMakerExList, static_cast<std::size_t>(index));
    if (IsSpawnPossible(maker)) {
        if (maker->InitializeRandomPos()) {
            DispatchMakerStartEvent(*maker);
        }
        if (IssuedReportCallStack != 0) {
            const std::string makerName = Utf16ToUtf8(maker->GetName());
            // Log.Add(
            //     LOG_ERROR,
            //     L"[ProcessOneNpcMakerEx] Suspected Growing "
            //     L"Memory NpcMakerEx[%s]",
            //     makerName.c_str())
            // Log.Add(LOG_WARN,
            //     "[NPC maker] Call-stack report flag is set after processing extended maker '%s'. Inspect earlier
            //     diagnostics on this thread.", WideToUtf8(makerName).c_str())
            L2_LOG_WARN(
                Log,
                "[NPC maker] Call-stack report flag is set after processing extended maker '%s'; inspect earlier "
                "diagnostics on this thread.",
                makerName.c_str()
            );
            IssuedReportCallStack = 0;
        }
    }
    return true;
}

bool CNpcMakerDB::IsSpawnPossible(CNpcMaker* maker) {
    L2_GUARD_FUNCTION();
    return maker != nullptr;
}

bool CNpcMakerDB::IsSpawnPossible(NpcMakerEx* maker) {
    L2_GUARD_FUNCTION();
    return maker != nullptr;
}

bool CNpcMakerDB::CheckMutex() {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    HANDLE mutex = ::CreateMutexW(nullptr, FALSE, kWindowsProtectionMutex);
    if (!mutex) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"L2Mutex_Protector_090720 error: %d\n",
        //     static_cast<int>(::GetLastError()))
        // Noise: Identical polling failures are summarized every 30 seconds. A changed cause is reported immediately.
        // Log.Add(LOG_ERROR,
        //     "[NPC startup] Cannot create or open the L2Server protection mutex (Windows error %lu). Protection check
        //     failed.", static_cast<unsigned long>(::GetLastError()))
        const unsigned long diagnosticError = static_cast<unsigned long>(::GetLastError());
        if (ShouldReportProtectionFailure(ProtectionFailure::CreateMutex, diagnosticError)) {
            // Log.Add(LOG_ERROR,
            //     "[NPC startup] Cannot create or open the L2Server protection mutex (Windows error %lu). Protection check
            //     failed.", diagnosticError)
            L2_LOG_ERROR(
                Log,
                "[NPC startup] Cannot create or open the L2Server protection mutex (Windows error %lu); protection check "
                "failed.",
                diagnosticError
            );
        }
        return false;
    }

    bool released = false;
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        // Noise: An existing protection mutex is expected while L2Server starts. The waiting caller reports this state once.
        // Log.Add(
        //     LOG_ERROR,
        //     L"L2Mutex_Protector_090720 already exists")
        ReportProtectionCheckProgress(false);
    } else {
        // released =
        //     ::WaitForSingleObject(mutex, 0) ==
        //     WAIT_OBJECT_0.
        const DWORD waitResult = ::WaitForSingleObject(mutex, 0);
        released = waitResult == WAIT_OBJECT_0;
        if (waitResult == WAIT_FAILED) {
            // Noise: Identical polling failures are summarized every 30 seconds. A changed cause is reported immediately.
            // Log.Add(LOG_ERROR,
            //     "[NPC startup] Waiting for the L2Server protection mutex failed (Windows error %lu).",
            //     static_cast<unsigned long>(::GetLastError()))
            const unsigned long diagnosticError = static_cast<unsigned long>(::GetLastError());
            if (ShouldReportProtectionFailure(ProtectionFailure::WaitMutex, diagnosticError)) {
                // Log.Add(LOG_ERROR,
                //     "[NPC startup] Waiting for the L2Server protection mutex failed (Windows error %lu).",
                //     diagnosticError)
                L2_LOG_ERROR(
                    Log, "[NPC startup] Waiting for the L2Server protection mutex failed (Windows error %lu).",
                    diagnosticError
                );
            }
        } else if (waitResult == WAIT_ABANDONED) {
            // Noise: Identical polling failures are summarized every 30 seconds. A changed cause is reported immediately.
            // Log.Add(LOG_WARN,
            //     "[NPC startup] L2Server protection mutex was abandoned, the protection check did not pass.")
            const unsigned long diagnosticError = static_cast<unsigned long>(waitResult);
            if (ShouldReportProtectionFailure(ProtectionFailure::AbandonedMutex, diagnosticError)) {
                // Log.Add(LOG_WARN,
                //     "[NPC startup] L2Server protection mutex was abandoned, the protection check did not pass.")
                L2_LOG_WARN(
                    Log, "[NPC startup] L2Server protection mutex was abandoned; the protection check did not pass."
                );
            }
        } else if (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_TIMEOUT) {
            ReportProtectionCheckProgress(released);
        }
    }

    // The target calls ReleaseMutex on both branches and ignores failure
    // when ERROR_ALREADY_EXISTS means this process never owned the object.
    ::ReleaseMutex(mutex);
    ::CloseHandle(mutex);
    return released;
#else
    // The producer holds an exclusive flock for the lifetime of the named
    // protection object. Unlike a POSIX named semaphore, that ownership is
    // released automatically if L2Server terminates, matching the lifetime
    // of the target's Win32 kernel object. A leftover path therefore is not
    // mistaken for a live producer.
    const int descriptor = ::open(kPosixProtectionMutexPath, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (descriptor < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"L2Mutex_Protector_090720 error: %d\n",
        //     errno)
        // Noise: Identical polling failures are summarized every 30 seconds. A changed cause is reported immediately.
        // Log.Add(LOG_ERROR,
        //     "[NPC startup] Cannot open protection lock '%s' (errno %d). Protection check failed.",
        //     kPosixProtectionMutexPath, errno)
        const unsigned long diagnosticError = static_cast<unsigned long>(errno);
        if (ShouldReportProtectionFailure(ProtectionFailure::OpenLock, diagnosticError)) {
            // Log.Add(LOG_ERROR,
            //     "[NPC startup] Cannot open protection lock '%s' (errno %d). Protection check failed.",
            //     kPosixProtectionMutexPath, static_cast<int>(diagnosticError))
            L2_LOG_ERROR(
                Log, "[NPC startup] Cannot open protection lock '%s' (errno %d); protection check failed.",
                kPosixProtectionMutexPath, static_cast<int>(diagnosticError)
            );
        }
        return false;
    }

    if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        const int error = errno;
        ::close(descriptor);
        if (error == EWOULDBLOCK || error == EAGAIN) {
            // Noise: A held protection lock is expected while L2Server starts. The waiting caller reports this state once.
            // Log.Add(
            //     LOG_ERROR,
            //     L"L2Mutex_Protector_090720 already exists")
            ReportProtectionCheckProgress(false);
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"L2Mutex_Protector_090720 error: %d\n",
            //     error)
            // Noise: Identical polling failures are summarized every 30 seconds. A changed cause is reported immediately.
            // Log.Add(LOG_ERROR,
            //     "[NPC startup] Cannot acquire protection lock '%s' (errno %d). Protection check failed.",
            //     kPosixProtectionMutexPath, error)
            const unsigned long diagnosticError = static_cast<unsigned long>(error);
            if (ShouldReportProtectionFailure(ProtectionFailure::AcquireLock, diagnosticError)) {
                // Log.Add(LOG_ERROR,
                //     "[NPC startup] Cannot acquire protection lock '%s' (errno %d). Protection check failed.",
                //     kPosixProtectionMutexPath, static_cast<int>(diagnosticError))
                L2_LOG_ERROR(
                    Log, "[NPC startup] Cannot acquire protection lock '%s' (errno %d); protection check failed.",
                    kPosixProtectionMutexPath, static_cast<int>(diagnosticError)
                );
            }
        }
        return false;
    }

    ::flock(descriptor, LOCK_UN);
    ::close(descriptor);
    ReportProtectionCheckProgress(true);
    return true;
#endif
}

} // namespace l2npc
