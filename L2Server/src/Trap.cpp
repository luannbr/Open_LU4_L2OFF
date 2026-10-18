#include "Trap.h"
#include "Shared/src/MemorySub.h"

#include "CWorld.h"
#include "NpcServer.h"
#include "SerializedMessage.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Territory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace {

void ReportTrapReferences(CCreature& creature, const char* callerFile, int callerLine, std::int32_t& reportTick) {
    const int threadId = CThreadManager::GetThreadID();
    ++reportTick;
    int sequence = 1;
    if (callerFile) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin called from "
        //     "file[%s], line[%d]",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     callerFile,
        //     callerLine)
        // L2_LOG_ERROR(Log,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin called from "
        //     "file[%s], line[%d]",
        //     threadId,
        //     reportTick,
        //     sequence,
        //     callerFile,
        //     callerLine)
        L2_LOG_ERROR(
            Log, "[trap references] Begin report %d, entry %d, for object %p (diagnostic worker %d), requested at %s:%d.",
            reportTick, sequence, static_cast<void*>(&creature), threadId, callerFile, callerLine
        );
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin",
        //     threadId,
        //     reportTick,
        //     sequence)
        // L2_LOG_ERROR(Log,
        //     "[ORT Report][tid:%d][tick:%d][%d] begin",
        //     threadId,
        //     reportTick,
        //     sequence)
        L2_LOG_ERROR(
            Log, "[trap references] Begin report %d, entry %d, for object %p (diagnostic worker %d); caller is unknown.",
            reportTick, sequence, static_cast<void*>(&creature), threadId
        );
    }

    for (std::size_t index = 0; index < RefHelpers::kCount; ++index) {
        const RefHelperStruct& helper = creature.m_RefHelpers.m_RefData[index];
        const std::int32_t count = helper.m_nRefCount.load(std::memory_order_relaxed);
        if (!helper.m_pAddCaller || !helper.m_pReleaseCaller || count == 0) {
            continue;
        }
        ++sequence;
        const std::string typeName = ::WideToUtf8(ObjectRefTypeName(static_cast<ObjectRefType>(index)));
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
            "[trap references] Report %d, entry %d, object %p (diagnostic worker %d): subtype %zu (%s) has %d references; "
            "last add at %s:%d, last release at %s:%d.",
            reportTick, sequence, static_cast<void*>(&creature), threadId, index, typeName.c_str(), count,
            helper.m_pAddCaller, helper.m_nAddLine, helper.m_pReleaseCaller, helper.m_nReleaseLine
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
        Log, "[trap references] End report %d, entry %d, for object %p (diagnostic worker %d).", reportTick, sequence + 1,
        static_cast<void*>(&creature), threadId
    );
}

void LogPCTrapInteraction(int eventId, CPCTrap& trap, User& master, User& target) {
    const FVector position = target.GetPos();
    const std::int32_t z = runtime::Win64CvttDoubleToInt32(position.z);
    const std::int32_t y = runtime::Win64CvttDoubleToInt32(position.y);
    const std::int32_t x = runtime::Win64CvttDoubleToInt32(position.x);
    const CSharedCreatureData* const targetData = target.m_data;
    const auto* const masterAccountName = master.m_sAccountName.data();
    const char16_t* const masterName = master.GetName();
    const auto* const targetAccountName = target.m_sAccountName.data();
    const char16_t* const targetName = target.GetName();
    const CSharedCreatureData* const trapData = trap.m_data;
    const std::int32_t npcId = trap.GetNPCId();
    const std::uint32_t masterAccountId = master.GetAccountID();
    const std::uint32_t masterDbId = master.GetDBID();
    const std::uint32_t targetAccountId = target.GetAccountID();
    const std::uint32_t targetDbId = target.GetDBID();
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,%s,%s,",
    //     static_cast<std::int64_t>(eventId),
    //     static_cast<std::uint64_t>(targetDbId),
    //     static_cast<std::uint64_t>(targetAccountId),
    //     static_cast<std::uint64_t>(masterDbId),
    //     static_cast<std::uint64_t>(masterAccountId),
    //     static_cast<std::int64_t>(x),
    //     static_cast<std::int64_t>(y),
    //     static_cast<std::int64_t>(z),
    //     static_cast<std::int64_t>(targetData->m_nClass),
    //     static_cast<std::int64_t>(targetData->m_Race),
    //     static_cast<std::int64_t>(targetData->m_Sex),
    //     static_cast<std::int64_t>(targetData->m_nLevel),
    //     static_cast<std::int64_t>(npcId),
    //     static_cast<std::int64_t>(trapData->m_nLevel),
    //     targetName,
    //     targetAccountName,
    //     masterName,
    //     masterAccountName)
    // Log.Add(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,%s,%s,",
    //     static_cast<std::int64_t>(eventId),
    //     static_cast<std::uint64_t>(targetDbId),
    //     static_cast<std::uint64_t>(targetAccountId),
    //     static_cast<std::uint64_t>(masterDbId),
    //     static_cast<std::uint64_t>(masterAccountId),
    //     static_cast<std::int64_t>(x),
    //     static_cast<std::int64_t>(y),
    //     static_cast<std::int64_t>(z),
    //     static_cast<std::int64_t>(targetData->m_nClass),
    //     static_cast<std::int64_t>(targetData->m_Race),
    //     static_cast<std::int64_t>(targetData->m_Sex),
    //     static_cast<std::int64_t>(targetData->m_nLevel),
    //     static_cast<std::int64_t>(npcId),
    //     static_cast<std::int64_t>(trapData->m_nLevel),
    //     Utf16ToWide(targetName).c_str(),
    //     targetAccountName,
    //     Utf16ToWide(masterName).c_str(),
    //     masterAccountName)
    Log.Add(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,%s,%s,",
        static_cast<std::int64_t>(eventId), static_cast<std::uint64_t>(targetDbId),
        static_cast<std::uint64_t>(targetAccountId), static_cast<std::uint64_t>(masterDbId),
        static_cast<std::uint64_t>(masterAccountId), static_cast<std::int64_t>(x), static_cast<std::int64_t>(y),
        static_cast<std::int64_t>(z), static_cast<std::int64_t>(targetData->m_nClass),
        static_cast<std::int64_t>(targetData->m_Race), static_cast<std::int64_t>(targetData->m_Sex),
        static_cast<std::int64_t>(targetData->m_nLevel), static_cast<std::int64_t>(npcId),
        static_cast<std::int64_t>(trapData->m_nLevel), targetName, targetAccountName, masterName, masterAccountName
    );
}

} // namespace

CWorldTrapDB g_WorldTrapDB;

void CWorldTrapInfo::SetTrapArea(CTerritory* territory) {
    m_pTrapArea = territory;
    m_TrapPos = territory->GetCenterPos();
}

CTrap::CTrap() : m_nActivated(0), m_padding3d7c{} { SetVisible(false); }

CTrap::CTrap(const CTrap& other) : CNPC(other), m_nActivated(0), m_padding3d7c{} { SetVisible(false); }

L2_DEFINE_OBJECT_CLASS_ID(CTrap)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CTrap)

std::int32_t CTrap::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
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
            "Trap %p has negative reference count %d for subtype %zu after release at %s:%d; reporting references and call "
            "stack.",
            static_cast<void*>(this), subtypeRemaining, typeIndex, callerFile ? callerFile : "<unknown>", callerLine
        );
        runtime::process::ReportCallStack(2, false);
        ReportOrt("Trap.cpp", 0x11);
    }

    const std::int32_t remaining = runtime::raw_atomic::AddI32(&m_nRefCount, -1);
    if (remaining == 0) {
        if (!IsInTimer()) {
            void* const allocation = this;
            this->~CTrap();
            runtime::process::FreeMemoryObject(allocation);
        } else {
            runtime::process::ReportDeletingInTimer("CTrap", callerFile, callerLine);
            ReportOrt("Trap.cpp", 0x11);
        }
        runtime::process::DecrementMemoryClassReference(L2_MEMORY_CLASS_ID(CTrap));
    } else if (remaining < 0) {
        runtime::process::ReportInvalidReference(remaining, "CTrap", MemoryObjectID(), callerFile, callerLine);
        ReportOrt("Trap.cpp", 0x11);
        runtime::process::ReportCallStack(2, true);
    }
    return remaining;
}

void CTrap::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    static thread_local std::int32_t reportTick = 0;
    ReportTrapReferences(*this, callerFile, callerLine, reportTick);
}

bool CTrap::IsTrap() { return true; }

CTrap* CTrap::CastTrap() { return this; }

bool CTrap::Activate(CCreature* creature) {
    L2_GUARD_FUNCTION();
    const std::int32_t previous = runtime::raw_atomic::ExchangeI32(&m_nActivated, 1);
    if (previous == 1) {
        return false;
    }
    OnActivateTo(creature);
    PushSerializedMessage(new CSerializedLookNeighborMessage(this, true, GetPos()));
    return true;
}

bool CTrap::Defuse(CCreature* creature) {
    L2_GUARD_FUNCTION();
    const std::int32_t previous = runtime::raw_atomic::ExchangeI32(&m_nActivated, 0);
    if (previous != 0) {
        OnDefuse(creature);
        return true;
    }
    return false;
}

void CTrap::OnInteracted(CCreature* creature, TrapInteractType interaction) {
    L2_GUARD_FUNCTION();
    g_npcserver.Send(
        "cddd", 100, static_cast<int>(Id()), creature ? static_cast<int>(creature->Id()) : 0, static_cast<int>(interaction)
    );
}

void CTrap::OnActivateTo(CCreature* creature) {
    L2_GUARD_FUNCTION();
    OnInteracted(creature, TIT_ACTIVATE);
}

void CTrap::OnDefuse(CCreature* creature) {
    L2_GUARD_FUNCTION();
    OnInteracted(creature, TIT_DEFUSE);
}

void CTrap::OnDetected(CCreature* creature) {
    L2_GUARD_FUNCTION();
    OnInteracted(creature, TIT_DETECTED);
}

CPCTrap::CPCTrap() {}

CPCTrap::CPCTrap(const CPCTrap& other) : CTrap(other) {}

L2_DEFINE_OBJECT_CLASS_ID(CPCTrap)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CPCTrap)

L2_DEFINE_OBJECT_ALLOCATOR(CPCTrap, __GetClassName())

L2_DEFINE_OBJECT_NEW(CPCTrap)

std::int32_t CPCTrap::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
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
            "Player trap %p has negative reference count %d for subtype %zu after release at %s:%d; reporting references "
            "and call stack.",
            static_cast<void*>(this), subtypeRemaining, typeIndex, callerFile ? callerFile : "<unknown>", callerLine
        );
        runtime::process::ReportCallStack(2, false);
        ReportOrt("Trap.cpp", 0x1a3);
    }

    const std::int32_t remaining = runtime::raw_atomic::AddI32(&m_nRefCount, -1);
    if (remaining == 0) {
        if (!IsInTimer()) {
            void* const allocation = this;
            this->~CPCTrap();
            runtime::process::FreeMemoryObject(allocation);
        } else {
            runtime::process::ReportDeletingInTimer("CPCTrap", callerFile, callerLine);
            ReportOrt("Trap.cpp", 0x1a3);
        }
        runtime::process::DecrementMemoryClassReference(L2_MEMORY_CLASS_ID(CPCTrap));
    } else if (remaining < 0) {
        runtime::process::ReportInvalidReference(remaining, "CPCTrap", MemoryObjectID(), callerFile, callerLine);
        ReportOrt("Trap.cpp", 0x1a3);
        runtime::process::ReportCallStack(2, true);
    }
    return remaining;
}

void CPCTrap::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    static thread_local std::int32_t reportTick = 0;
    ReportTrapReferences(*this, callerFile, callerLine, reportTick);
}

CObject* CPCTrap::Copy() {
    L2_GUARD_FUNCTION();
    CPCTrap* const copy = new CPCTrap(*this);
    copy->OnCreate();
    return copy;
}

bool CPCTrap::IsPCTrap() { return true; }

bool CPCTrap::IsPCOrPCRelatedNPC() const { return true; }

CPCTrap* CPCTrap::CastPCTrap() { return this; }

User* CPCTrap::GetUserOrMaster() {
    L2_GUARD_FUNCTION();
    return GetMaster();
}

void CPCTrap::SendSystemMessageWithSelf(unsigned int messageId) {
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    if (master != nullptr) {
        master->GetSocket()->Send("chccd", 0x62, static_cast<int>(static_cast<std::int16_t>(messageId)), 1, 2, GetClassId());
    }
}

bool CPCTrap::IsEnemyTo(CCreature* creature) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }
    User* const summoner = GetSummoner();
    return summoner == nullptr || summoner->IsEnemyTo(creature);
}

void CPCTrap::InSight(CObject* object, bool) {
    L2_GUARD_FUNCTION();
    if (object != nullptr && object->IsCreature()) {
        CheckTrap(*object->CastCreature(), STEP_IN);
    }
}

bool CPCTrap::CheckTrap(CCreature& creature, StepType) {
    L2_GUARD_FUNCTION();
    if (!creature.IsAlive() || !creature.IsVisible() || creature.IsGMInvisible() || creature.IsPCTrap()) {
        return false;
    }

    User* const master = GetUserOrMaster();
    if (master != nullptr && master != creature.GetUserOrMaster() && creature.IsEnemyTo(master)) {
        const FVector source = GetPos();
        const FVector destination = creature.GetPos();
        const double dx = destination.x - source.x;
        const double dy = destination.y - source.y;
        const double dz = destination.z - source.z;
        const double range = static_cast<double>(m_data->m_nAgroRange);
        if (dx * dx + dy * dy + dz * dz < range * range &&
            CWorld::Inst().GetGeo()->CanSee(source, destination, static_cast<int>(GetInZoneID()), true)) {
            g_npcserver.Send("cdddd", 99, static_cast<int>(Id()), static_cast<int>(creature.Id()), 1, 1);
        }
    }
    return true;
}

bool CPCTrap::Die(CCreature* killer, int reason) {
    L2_GUARD_FUNCTION();
    User* const master = GetMaster();
    if (master != nullptr) {
        master->OnDespawnPCTrap(static_cast<long>(Id()));
    }
    CNPC::Die(killer, reason);
    return true;
}

void CPCTrap::EnterWorld(bool unknown, int first, int second, unsigned int third) {
    L2_GUARD_FUNCTION();
    CNPC::EnterWorld(unknown, first, second, third);
    User* const master = GetMaster();
    if (master != nullptr) {
        master->OnSpawnPCTrap(*this);
    }
}

User* CPCTrap::GetMaster() const {
    L2_GUARD_FUNCTION();
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_data->m_SummonerID);
    if (creature != nullptr && creature->IsPC()) {
        return creature->CastUser();
    }
    return nullptr;
}

bool CPCTrap::HasSummoner() { return true; }

void CPCTrap::OnDetected(CCreature* creature) {
    L2_GUARD_FUNCTION();
    User* const master = GetUserOrMaster();
    if (master == nullptr || !master->IsEnemyTo(creature)) {
        return;
    }
    if (!creature->IsPC()) {
        CTrap::OnDetected(creature);
        return;
    }

    User* const target = creature->CastUser();
    target->SendInSight(this, false);
    LogPCTrapInteraction(0x1a5, *this, *master, *target);
}

void CPCTrap::OnDefuse(CCreature* creature) {
    L2_GUARD_FUNCTION();
    User* const master = GetUserOrMaster();
    if (master == nullptr || !master->IsEnemyTo(creature)) {
        return;
    }
    CTrap::OnDefuse(creature);
    User* const target = creature->CastUser();
    LogPCTrapInteraction(0x1a6, *this, *master, *target);
}

CWorldTrap::CWorldTrap() : m_pTrapInfo(nullptr), m_CreatureNum(0) {}

CWorldTrap::CWorldTrap(const CWorldTrap& other) :
    CTrap(other), m_pTrapInfo(other.m_pTrapInfo), m_CreatureNum(0) {}

CWorldTrap::~CWorldTrap() { m_CreatureInTrap.clear(); }

L2_DEFINE_OBJECT_CLASS_ID(CWorldTrap)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CWorldTrap)

L2_DEFINE_OBJECT_ALLOCATOR(CWorldTrap, __GetClassName())

L2_DEFINE_OBJECT_NEW(CWorldTrap)

std::int32_t CWorldTrap::Release(const char* callerFile, int callerLine, ObjectRefType type, bool releaseFromTimer) {
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
            "World trap %p has negative reference count %d for subtype %zu after release at %s:%d; reporting references and "
            "call stack.",
            static_cast<void*>(this), subtypeRemaining, typeIndex, callerFile ? callerFile : "<unknown>", callerLine
        );
        runtime::process::ReportCallStack(2, false);
        ReportOrt("Trap.cpp", 0x8a);
    }

    const std::int32_t remaining = runtime::raw_atomic::AddI32(&m_nRefCount, -1);
    if (remaining == 0) {
        if (!IsInTimer()) {
            void* const allocation = this;
            this->~CWorldTrap();
            runtime::process::FreeMemoryObject(allocation);
        } else {
            runtime::process::ReportDeletingInTimer("CWorldTrap", callerFile, callerLine);
            ReportOrt("Trap.cpp", 0x8a);
        }
        runtime::process::DecrementMemoryClassReference(L2_MEMORY_CLASS_ID(CWorldTrap));
    } else if (remaining < 0) {
        runtime::process::ReportInvalidReference(remaining, "CWorldTrap", MemoryObjectID(), callerFile, callerLine);
        ReportOrt("Trap.cpp", 0x8a);
        runtime::process::ReportCallStack(2, true);
    }
    return remaining;
}

void CWorldTrap::ReportOrt(const char* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    static thread_local std::int32_t reportTick = 0;
    ReportTrapReferences(*this, callerFile, callerLine, reportTick);
}

CObject* CWorldTrap::Copy() {
    L2_GUARD_FUNCTION();
    CWorldTrap* const copy = new CWorldTrap(*this);
    copy->OnCreate();
    return copy;
}

bool CWorldTrap::IsWorldTrap() { return true; }

CWorldTrap* CWorldTrap::CastWorldTrap() { return this; }

bool CWorldTrap::ValidateCheckTrap(CCreature& creature) {
    L2_GUARD_FUNCTION();
    return creature.IsPCOrPCRelatedNPC() && !creature.IsGMInvisible() && creature.IsAlive();
}

void CWorldTrap::InSight(CObject* object, bool) {
    L2_GUARD_FUNCTION();
    if (object != nullptr && object->IsCreature()) {
        CCreature* const creature = object->CastCreature();
        if (ValidateCheckTrap(*creature)) {
            CheckTrap(*creature, STEP_IN);
        }
    }
}

void CWorldTrap::OutOfSight(CObject* object, bool) {
    L2_GUARD_FUNCTION();
    if (object != nullptr && object->IsCreature()) {
        CCreature* const creature = object->CastCreature();
        if (ValidateCheckTrap(*creature)) {
            CheckTrap(*creature, STEP_OUT);
        }
    }
}

void CWorldTrap::OnCreated() { L2_GUARD_FUNCTION(); }

bool CWorldTrap::CheckTrap(CCreature& creature, StepType step) {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::LoadI32Relaxed(&m_nActivated) != 1 || !ValidateCheckTrap(creature)) {
        return false;
    }
    if (m_pTrapInfo != nullptr && m_pTrapInfo->m_pTrapArea != nullptr &&
        m_pTrapInfo->m_pTrapArea->IsInside(creature.GetPos(), true)) {
        if (step == STEP_IN) {
            OnStepIn(creature);
        } else {
            OnStepOut(creature);
        }
    }
    return true;
}

bool CWorldTrap::CheckTrapByPos(CCreature& creature, const FVector& previousPosition, const FVector& currentPosition) {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::LoadI32Relaxed(&m_nActivated) != 1 || !ValidateCheckTrap(creature)) {
        return false;
    }
    if (m_pTrapInfo != nullptr && m_pTrapInfo->m_pTrapArea != nullptr) {
        const CTerritory::CollisionResult result =
            m_pTrapInfo->m_pTrapArea->IsCollide(previousPosition, currentPosition, false);
        if (result == CTerritory::CR_PASS || result == CTerritory::CR_STEP_IN) {
            OnStepIn(creature);
        }
        if (result == CTerritory::CR_PASS || result == CTerritory::CR_STEP_OUT) {
            OnStepOut(creature);
        }
    }
    return true;
}

void CWorldTrap::OnStepIn(CCreature& creature) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    const std::int32_t id = static_cast<std::int32_t>(creature.Id());
    if (m_CreatureInTrap.find(id) == m_CreatureInTrap.end()) {
        m_CreatureInTrap.insert(id);
        m_CreatureNum = m_CreatureInTrap.size();
        g_npcserver.Send("cdddd", 99, static_cast<int>(Id()), id, 1, static_cast<int>(m_CreatureNum));
    }
}

void CWorldTrap::OnStepOut(CCreature& creature) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    const std::int32_t id = static_cast<std::int32_t>(creature.Id());
    if (m_CreatureInTrap.find(id) != m_CreatureInTrap.end()) {
        m_CreatureInTrap.erase(id);
        m_CreatureNum = m_CreatureInTrap.size();
        g_npcserver.Send("cdddd", 99, static_cast<int>(Id()), id, 0, static_cast<int>(m_CreatureNum));
    }
}

void CWorldTrap::Delete() {
    L2_GUARD_FUNCTION();
    g_WorldTrapDB.DeleteWorldTrap(this);
    CNPC::Delete();
}

void CWorldTrap::EnterWorld(bool unknown, int first, int second, unsigned int third) {
    L2_GUARD_FUNCTION();
    CNPC::EnterWorld(unknown, first, second, third);
    g_WorldTrapDB.AddWorldTrap(this);
}

CWorldTrapDB::CWorldTrapDB() { L2_GUARD_FUNCTION(); }

bool CWorldTrapDB::AddTrapInfo(CWorldTrapInfo* info) {
    L2_GUARD_FUNCTION();
    if (info == nullptr) {
        return false;
    }
    L2_GUARD(m_Lock);
    if (m_WorldTrapInfoDB.find(info->m_Id) != m_WorldTrapInfoDB.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"WorldTrap - class id : %d already added!",
        //     info->m_Id)
        // L2_LOG_ERROR(Log,
        //     L"WorldTrap - class id : %d already added!",
        //     info->m_Id)
        L2_LOG_ERROR(
            Log,
            "World-trap definition for NPC class %d is already registered; the duplicate is rejected and the existing "
            "definition is retained.",
            info->m_Id
        );
        return false;
    }
    m_WorldTrapInfoDB[info->m_Id] = info;
    return true;
}

bool CWorldTrapDB::AddWorldTrap(CWorldTrap* trap) {
    L2_GUARD_FUNCTION();
    if (trap == nullptr) {
        return false;
    }
    const int classId = trap->GetNPCId();
    const auto info = m_WorldTrapInfoDB.find(classId);
    if (info == m_WorldTrapInfoDB.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"AddWorldTrap. Its trap info doesn't exist. "
        //     L"ClassId = %d, inzone = %d",
        //     classId,
        //     static_cast<int>(trap->GetInZoneID()))
        // L2_LOG_ERROR(Log,
        //     L"AddWorldTrap. Its trap info doesn't exist. "
        //     L"ClassId = %d, inzone = %d",
        //     classId,
        //     static_cast<int>(trap->GetInZoneID()))
        L2_LOG_ERROR(
            Log, "Cannot register world trap with NPC class %d in zone %u: no trap definition exists; registration failed.",
            classId, static_cast<unsigned>(trap->GetInZoneID())
        );
        return false;
    }

    trap->m_pTrapInfo = info->second;
    L2_GUARD(m_Lock);
    m_WorldTrapInzoneDB[static_cast<int>(trap->GetInZoneID())][classId] = static_cast<int>(trap->Id());
    return true;
}

bool CWorldTrapDB::DeleteWorldTrap(CWorldTrap* trap) {
    L2_GUARD_FUNCTION();
    if (trap == nullptr) {
        return false;
    }
    L2_GUARD(m_Lock);
    TrapObjectMap& traps = m_WorldTrapInzoneDB[static_cast<int>(trap->GetInZoneID())];
    const int classId = trap->GetNPCId();
    const auto position = traps.find(classId);
    if (position == traps.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"DeleteWorldTrap. Its trap info doesn't exist. "
        //     L"ClassId = %d, inzone = (%d / %d)",
        //     classId,
        //     static_cast<int>(trap->GetInZoneID()),
        //     trap->m_nInstantZoneTypeID)
        // L2_LOG_ERROR(Log,
        //     L"DeleteWorldTrap. Its trap info doesn't exist. "
        //     L"ClassId = %d, inzone = (%d / %d)",
        //     classId,
        //     static_cast<int>(trap->GetInZoneID()),
        //     trap->m_nInstantZoneTypeID)
        L2_LOG_ERROR(
            Log,
            "Cannot unregister world trap with NPC class %d: no runtime entry exists in zone %u (zone type %d); removal "
            "failed.",
            classId, static_cast<unsigned>(trap->GetInZoneID()), trap->m_nInstantZoneTypeID
        );
        return false;
    }
    traps.erase(position);
    return true;
}

CWorldTrap* CWorldTrapDB::GetWorldTrap(int inZoneId, int classId) {
    L2_GUARD_FUNCTION();
    int objectId = 0;
    {
        L2_GUARD(m_Lock);
        TrapObjectMap& traps = m_WorldTrapInzoneDB[inZoneId];
        const auto position = traps.find(classId);
        if (position == traps.end()) {
            return nullptr;
        }
        objectId = position->second;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(objectId));
    if (creature == nullptr || !creature->IsNPC()) {
        return nullptr;
    }
    CNPC* const npc = creature->CastNPC();
    return npc != nullptr && npc->IsWorldTrap() ? npc->CastWorldTrap() : nullptr;
}

void CWorldTrapDB::OnInZoneDelete(int) { L2_GUARD_FUNCTION(); }
