#include "Boss.h"
#include "Shared/src/MemorySub.h"

#include "DB.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"

#include <atomic>
#include <string>

namespace {

constexpr const char* kBossSource = "Boss.cpp";
constexpr int kBossReportLine = 8;
// Shipping number (metadata only): 64.

} // namespace

CBoss::CBoss() : m_nPledgeID(0), m_bIsFieldBoss(false), m_alignment3d7dTo3d80{} {}

CBoss::CBoss(const CBoss& other) :
    CNPC(other), m_nPledgeID(0), m_bIsFieldBoss(other.m_bIsFieldBoss), m_alignment3d7dTo3d80{} {
    L2_GUARD_FUNCTION();
}

CBoss::~CBoss() = default;

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR_LITERAL(CBoss)

std::int32_t CBoss::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
    static_cast<void>(releaseFromTimer);
    const std::size_t typeIndex = static_cast<std::size_t>(type);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[typeIndex];
    const std::int32_t subtypeRemaining = helper.m_nRefCount.fetch_sub(1, std::memory_order_relaxed) - 1;
    helper.m_pReleaseCaller = callerFile;
    helper.m_nReleaseLine = callerLine;
    if (subtypeRemaining < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] sub-type[%zu] sub-reference count[%d] is below zero!",
        //     callerFile ? callerFile : "",
        //     callerLine,
        //     typeIndex,
        //     subtypeRemaining)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] sub-type[%zu] sub-reference count[%d] is below zero!",
        //     callerFile ? callerFile : "",
        //     callerLine,
        //     typeIndex,
        //     subtypeRemaining)
        L2_LOG_ERROR(
            Log,
            "Boss object %u: reference category %zu fell to %d at %s:%d; total-reference release continues after diagnostic "
            "reports.",
            static_cast<unsigned>(m_nId), typeIndex, subtypeRemaining,
            callerFile ? runtime::logging::SourceFileName(callerFile).data() : "<unknown>", callerLine
        );
        runtime::process::ReportCallStack(2, false);
        ReportOrt(kBossSource, kBossReportLine);
    }
    const std::int32_t remaining = runtime::raw_atomic::AddI32(&m_nRefCount, -1);
    if (remaining == 0) {
        if (!IsInTimer()) {
            void* const allocation = this;
            this->~CBoss();
            runtime::process::FreeMemoryObject(allocation);
        } else {
            runtime::process::ReportDeletingInTimer("CBoss", callerFile, callerLine);
            ReportOrt(kBossSource, kBossReportLine);
        }
        runtime::process::DecrementMemoryClassReference(L2_MEMORY_CLASS_ID(CBoss));
    } else if (remaining < 0) {
        runtime::process::ReportInvalidReference(remaining, "CBoss", MemoryObjectID(), callerFile, callerLine);
        ReportOrt(kBossSource, kBossReportLine);
        runtime::process::ReportCallStack(2, true);
    }
    return remaining;
}

void CBoss::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    const int threadId = CThreadManager::GetThreadID();
    static thread_local std::int32_t reportTick = 0;
    ++reportTick;
    int sequence = 1;
    // Log.Add(
    //     LOG_ERROR,
    //     callerFile
    //         ? "[ORT Report][tid:%d][tick:%d][%d] begin called from file[%s], line[%d]"
    //         : "[ORT Report][tid:%d][tick:%d][%d] begin",
    //     threadId,
    //     reportTick,
    //     sequence,
    //     callerFile,
    //     callerLine)
    // L2_LOG_ERROR(Log,
    //     callerFile
    //         ? "[ORT Report][tid:%d][tick:%d][%d] begin called from file[%s], line[%d]"
    //         : "[ORT Report][tid:%d][tick:%d][%d] begin",
    //     threadId,
    //     reportTick,
    //     sequence,
    //     callerFile,
    //     callerLine)
    L2_LOG_ERROR(
        Log, "Boss object %u: reference report %d begins (worker %d, entry %d), requested at %s:%d.",
        static_cast<unsigned>(m_nId), reportTick, threadId, sequence,
        callerFile ? runtime::logging::SourceFileName(callerFile).data() : "<unknown>", callerLine
    );
    for (std::size_t index = 0; index < RefHelpers::kCount; ++index) {
        const RefHelperStruct& helper = m_RefHelpers.m_RefData[index];
        const std::int32_t count = helper.m_nRefCount.load(std::memory_order_relaxed);
        if (!helper.m_pAddCaller || !helper.m_pReleaseCaller || count == 0) {
            continue;
        }
        ++sequence;
        const std::string typeName = WideToUtf8(ObjectRefTypeName(static_cast<ObjectRefType>(index)));
        // Log.Add(
        //     LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] [RefCount:%d]-[%s] "
        //     "last_add[%s][%d] last_release[%s][%d]",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     count,
        //     typeName.c_str(),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        // L2_LOG_ERROR(Log,
        //     "[ORT Report][tid:%d][tick:%d][%d] [RefCount:%d]-[%s] "
        //     "last_add[%s][%d] last_release[%s][%d]",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     count,
        //     typeName.c_str(),
        //     helper.m_pAddCaller,
        //     helper.m_nAddLine,
        //     helper.m_pReleaseCaller,
        //     helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log,
            "Boss object %u: reference report %d, entry %d (worker %d): category '%s' has %d references; last add %s:%d, "
            "last release %s:%d.",
            static_cast<unsigned>(m_nId), reportTick, sequence, threadId, typeName.c_str(), count,
            runtime::logging::SourceFileName(helper.m_pAddCaller).data(), helper.m_nAddLine,
            runtime::logging::SourceFileName(helper.m_pReleaseCaller).data(), helper.m_nReleaseLine
        );
    }
    // Log.Add(
    //     LOG_ERROR,
    //     "[ORT Report][tid:%d][tick:%d][%d] end",
    //     threadId,
    //     reportTick,
    //     sequence + 1)
    // L2_LOG_ERROR(Log,
    //     "[ORT Report][tid:%d][tick:%d][%d] end",
    //     threadId,
    //     reportTick,
    //     sequence + 1)
    L2_LOG_ERROR(
        Log, "Boss object %u: reference report %d ends (worker %d, entry %lld).", static_cast<unsigned>(m_nId), reportTick,
        threadId, static_cast<long long>(sequence) + 1
    );
}

CObject* CBoss::Copy() {
    L2_GUARD_FUNCTION();
    CBoss* const boss = new CBoss(*this);
    boss->OnCreate();
    return boss;
}

bool CBoss::IsBoss() { return true; }

CBoss* CBoss::CastBoss() { return this; }

int CBoss::GetNeighborPledgeMember() {
    // Current L2Server 0x004bfac8.
    L2_GUARD_FUNCTION();
    return 0;
}

// static_assert(L2ServerMemory::kCBossRecordClassId == 65)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::CBossRecord) == 65);
// static_assert(L2ServerMemory::kCBossRecordListClassId == 66)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::CBossRecordList) == 66);
// static_assert(L2ServerMemory::kCBossRecordRankingClassId == 67)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::CBossRecordRanking) == 67);
// static_assert(L2ServerMemory::kCBossRecordRoundClassId == 68)
static_assert(static_cast<std::int32_t>(ShippingMemoryClassId::CBossRecordRound) == 68);

L2_DEFINE_OBJECT_CLASS_ID(CBossRecord)

L2_DEFINE_OBJECT_CLASS_NAME(CBossRecord, m_pClassName_CBossRecord)

L2_DEFINE_OBJECT_ALLOCATOR(CBossRecord, m_pClassName_CBossRecord)

L2_DEFINE_OBJECT_NEW(CBossRecord)

L2_DEFINE_OBJECT_RELEASE(CBossRecord)

CBossRecord::CBossRecord(int bossId, int accumulatedPoint, int point) :
    m_nBossID(bossId), m_nAccumulatedPoint(accumulatedPoint), m_nPoint(point) {
    L2_GUARD_FUNCTION();
}

L2_DEFINE_OBJECT_CLASS_ID(CBossRecordList)

L2_DEFINE_OBJECT_CLASS_NAME(CBossRecordList, m_pClassName_CBossRecordList)

L2_DEFINE_OBJECT_ALLOCATOR(CBossRecordList, m_pClassName_CBossRecordList)

L2_DEFINE_OBJECT_NEW(CBossRecordList)

L2_DEFINE_OBJECT_RELEASE(CBossRecordList)

CBossRecordList::CBossRecordList() : m_nRound(0) {
    L2_GUARD_FUNCTION();
    m_mapBossRecord.clear();
}

CBossRecordList::~CBossRecordList() {
    L2_GUARD_FUNCTION();
    m_mapBossRecord.clear();
}

void CBossRecordList::Initialize() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_lock);
        for (auto& entry : m_mapBossRecord) {
            if (entry.second) {
                entry.second->m_nPoint = 0;
            }
        }
    }
}

void CBossRecordList::AddBossRecord(CBossRecord* record) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_lock);
        const auto existing = m_mapBossRecord.find(record->m_nBossID);
        if (existing == m_mapBossRecord.end()) {
            m_mapBossRecord.emplace(record->m_nBossID, record);
        } else {
            if (existing->second) {
                existing->second->m_nPoint = record->m_nPoint;
                existing->second->m_nAccumulatedPoint = record->m_nAccumulatedPoint;
            }
            L2_RELEASE(record, ObjectRefType::CreateDelete);
        }
    }
}

void CBossRecordList::SetRound(int round) { m_nRound = round; }

int CBossRecordList::GetRound() { return m_nRound; }

CBossRecordRanking::CBossRecordRanking() {
    L2_GUARD_FUNCTION();
    m_mapBossRecordMember.clear();
    m_multimapBossRecordPoint.clear();
}

CBossRecordRanking::~CBossRecordRanking() = default;

CBossRecordRanking& CBossRecordRanking::inst() {
    L2_GUARD_FUNCTION();
    static CBossRecordRanking instance;
    return instance;
}

L2_DEFINE_OBJECT_RELEASE(CBossRecordRanking)

void CBossRecordRanking::AddBossRecord(int characterId, int point) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_lock);

        const auto member = m_mapBossRecordMember.find(characterId);
        if (member == m_mapBossRecordMember.end()) {
            m_mapBossRecordMember.emplace(characterId, point);
        } else {
            const int previousPoint = member->second;
            const auto range = m_multimapBossRecordPoint.equal_range(previousPoint);
            for (auto position = range.first; position != range.second; ++position) {
                if (position->second == characterId) {
                    m_multimapBossRecordPoint.erase(position);
                    break;
                }
            }
            member->second = point;
        }

        m_multimapBossRecordPoint.emplace(point, characterId);
    }
}

void CBossRecordRanking::Initialize() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_lock);
        m_mapBossRecordMember.clear();
        m_multimapBossRecordPoint.clear();
    }
}

CBossRecordRound::CBossRecordRound() : m_nRound(0), m_nStartTime(0), m_nEndTime(0) {}

CBossRecordRound& CBossRecordRound::inst() {
    static CBossRecordRound instance;
    return instance;
}

L2_DEFINE_OBJECT_RELEASE(CBossRecordRound)

void CBossRecordRound::Set(int round, int startTime, int endTime) {
    L2_GUARD_FUNCTION();
    m_nRound = round;
    m_nStartTime = startTime;
    m_nEndTime = endTime;
}

void CBossRecordRound::MoveToNextRound() {
    L2_GUARD_FUNCTION();
    db.RequestMoveToNextBossRecordRound();
}

void CBossRecordRound::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    if (now < m_nEndTime) {
        AddTimer(5000, 0);
    } else {
        MoveToNextRound();
    }
}
