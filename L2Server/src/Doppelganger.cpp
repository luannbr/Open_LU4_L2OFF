#include "Doppelganger.h"
#include "Shared/src/MemorySub.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "User.h"

#include <atomic>
#include <string>

namespace {
// Shipping number (metadata only): 103.
constexpr const char* kSource = "Doppelganger.cpp";
constexpr int kReportLine = 5;
} // namespace

CDoppelganger::CDoppelganger() {}
CDoppelganger::CDoppelganger(const CDoppelganger& other) : CNPC(other) { L2_GUARD_FUNCTION(); }
CDoppelganger::~CDoppelganger() = default;

L2_DEFINE_OBJECT_SINGLE_ALLOCATOR_LITERAL(CDoppelganger)

std::int32_t CDoppelganger::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
    static_cast<void>(releaseFromTimer);
    const std::size_t typeIndex = static_cast<std::size_t>(type);
    RefHelperStruct& helper = m_RefHelpers.m_RefData[typeIndex];
    const std::int32_t subtypeRemaining = helper.m_nRefCount.fetch_sub(1, std::memory_order_relaxed) - 1;
    helper.m_pReleaseCaller = callerFile;
    helper.m_nReleaseLine = callerLine;
    if (subtypeRemaining < 0) {
        // Log.Add(LOG_ERROR,
        //     "[%s][%d] sub-type[%zu] sub-reference count[%d] is below zero!",
        //     callerFile ? callerFile : "", callerLine, typeIndex,
        //     subtypeRemaining)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] sub-type[%zu] sub-reference count[%d] is below zero!",
        //     callerFile ? callerFile : "", callerLine, typeIndex,
        //     subtypeRemaining)
        L2_LOG_ERROR(
            Log,
            "Doppelganger object %u: reference category %zu fell to %d at %s:%d; total-reference release continues after "
            "diagnostic reports.",
            static_cast<unsigned>(m_nId), typeIndex, subtypeRemaining,
            callerFile ? runtime::logging::SourceFileName(callerFile).data() : "<unknown>", callerLine
        );
        runtime::process::ReportCallStack(2, false);
        ReportOrt(kSource, kReportLine);
    }
    const std::int32_t remaining = runtime::raw_atomic::AddI32(&m_nRefCount, -1);
    if (remaining == 0) {
        if (!IsInTimer()) {
            void* const allocation = this;
            this->~CDoppelganger();
            runtime::process::FreeMemoryObject(allocation);
        } else {
            runtime::process::ReportDeletingInTimer("CDoppelganger", callerFile, callerLine);
            ReportOrt(kSource, kReportLine);
        }
        runtime::process::DecrementMemoryClassReference(L2_MEMORY_CLASS_ID(CDoppelganger));
    } else if (remaining < 0) {
        runtime::process::ReportInvalidReference(remaining, "CDoppelganger", MemoryObjectID(), callerFile, callerLine);
        ReportOrt(kSource, kReportLine);
        runtime::process::ReportCallStack(2, true);
    }
    return remaining;
}

void CDoppelganger::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    const int threadId = CThreadManager::GetThreadID();
    static thread_local std::int32_t reportTick = 0;
    ++reportTick;
    int sequence = 1;
    if (callerFile) {
        // Log.Add(LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin called from "
        //     "file[%s], line[%d]", threadId, reportTick, sequence,
        //     callerFile, callerLine)
        // L2_LOG_ERROR(Log,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin called from "
        //     "file[%s], line[%d]", threadId, reportTick, sequence,
        //     callerFile, callerLine)
        L2_LOG_ERROR(
            Log, "Doppelganger object %u: reference report %d begins (worker %d, entry %d), requested at %s:%d.",
            static_cast<unsigned>(m_nId), reportTick, threadId, sequence,
            callerFile ? runtime::logging::SourceFileName(callerFile).data() : "<unknown>", callerLine
        );
    } else {
        // Log.Add(LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin",
        //     threadId, reportTick, sequence)
        // L2_LOG_ERROR(Log,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin",
        //     threadId, reportTick, sequence)
        L2_LOG_ERROR(
            Log, "Doppelganger object %u: reference report %d begins (worker %d, entry %d); caller unknown.",
            static_cast<unsigned>(m_nId), reportTick, threadId, sequence
        );
    }
    for (std::size_t index = 0; index < RefHelpers::kCount; ++index) {
        const RefHelperStruct& helper = m_RefHelpers.m_RefData[index];
        const std::int32_t count = helper.m_nRefCount.load(std::memory_order_relaxed);
        if (!helper.m_pAddCaller || !helper.m_pReleaseCaller || count == 0) {
            continue;
        }
        ++sequence;
        const std::string typeName = WideToUtf8(ObjectRefTypeName(static_cast<ObjectRefType>(index)));
        // Log.Add(LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] [RefCount:%d]-[%s] "
        //     "last_add[%s][%d] last_release[%s][%d]",
        //     threadId, reportTick, sequence, count, typeName.c_str(),
        //     helper.m_pAddCaller, helper.m_nAddLine,
        //     helper.m_pReleaseCaller, helper.m_nReleaseLine)
        // L2_LOG_ERROR(Log,
        //     "[ORT Report][tid:%d][tick:%d][%d] [RefCount:%d]-[%s] "
        //     "last_add[%s][%d] last_release[%s][%d]",
        //     threadId, reportTick, sequence, count, typeName.c_str(),
        //     helper.m_pAddCaller, helper.m_nAddLine,
        //     helper.m_pReleaseCaller, helper.m_nReleaseLine)
        L2_LOG_ERROR(
            Log,
            "Doppelganger object %u: reference report %d, entry %d (worker %d): category '%s' has %d references; last add "
            "%s:%d, last release %s:%d.",
            static_cast<unsigned>(m_nId), reportTick, sequence, threadId, typeName.c_str(), count,
            runtime::logging::SourceFileName(helper.m_pAddCaller).data(), helper.m_nAddLine,
            runtime::logging::SourceFileName(helper.m_pReleaseCaller).data(), helper.m_nReleaseLine
        );
    }
    // Log.Add(LOG_ERROR, "[ORT Report][tid:%d][tick:%d][%d] end",
    //     threadId, reportTick, sequence + 1)
    // L2_LOG_ERROR(Log, "[ORT Report][tid:%d][tick:%d][%d] end",
    //     threadId, reportTick, sequence + 1)
    L2_LOG_ERROR(
        Log, "Doppelganger object %u: reference report %d ends (worker %d, entry %lld).", static_cast<unsigned>(m_nId),
        reportTick, threadId, static_cast<long long>(sequence) + 1
    );
}

CObject* CDoppelganger::Copy() {
    L2_GUARD_FUNCTION();
    CDoppelganger* const doppelganger = new CDoppelganger(*this);
    doppelganger->OnCreate();
    return doppelganger;
}

bool CDoppelganger::IsPCOrPCRelatedNPC() const { return true; }
bool CDoppelganger::IsDoppelganger() const { return true; }
CDoppelganger* CDoppelganger::CastDoppelganger() { return this; }

bool CDoppelganger::IsEnemyTo(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }
    User* const master = GetMaster();
    return master == nullptr || master->IsEnemyTo(creature);
}

void CDoppelganger::OnCreated() {
    L2_GUARD_FUNCTION();
    User* const master = GetSummoner();
    if (master == nullptr) {
        return;
    }
    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        reinterpret_cast<std::int32_t&>(m_data->m_nPledgeID), static_cast<std::int32_t>(master->m_data->m_nPledgeID), 0x2e
    );
    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        m_nPledgeCrestId, static_cast<std::int32_t>(master->m_nPledgeCrestDbId), 0x30
    );
    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        reinterpret_cast<std::int32_t&>(m_data->m_nAllianceId), static_cast<std::int32_t>(master->m_data->m_nAllianceId),
        0x2f
    );
    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        m_nAllianceCrestId, static_cast<std::int32_t>(master->m_nAllianceCrestDbId), 0x31
    );
}

int CDoppelganger::GetDoppelgangerSummonerSID() {
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    return master == nullptr ? 0 : static_cast<int>(master->Id());
}

bool CDoppelganger::HasSummoner() { return true; }
