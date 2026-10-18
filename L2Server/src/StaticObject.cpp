#include "Protocol/src/L2Constants.h"
#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include "Shared/src/ScopedLockGuards.h"
#include "StaticObject.h"

#include "CWorld.h"
#include "Creature.h"
#include "SharedContainer.h"
#include "ThreadLocalWorldMap.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Exception.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadManager.h"

#include <atomic>
#include <cstdint>

namespace {

void IncrementCurrentStaticObjectCounter(CStaticObject* object) noexcept {
    std::atomic<std::int32_t>* counter = nullptr;
    if (object->IsResidenceDecoVisible()) {
        counter = &g_ServerContext.ResidenceDecoVisible;
    } else if (object->IsAirShipSteering()) {
        counter = &g_ServerContext.AirShipSteering;
    } else if (object->IsAirShipPart()) {
        counter = &g_ServerContext.AirShipPart;
    } else if (object->IsBattleCamp()) {
        counter = &g_ServerContext.BattleCamp;
    } else if (object->IsChair()) {
        counter = &g_ServerContext.Chair;
    } else if (object->IsControlTower()) {
        counter = &g_ServerContext.ControlTower;
    } else if (object->IsDoor()) {
        counter = &g_ServerContext.Door;
    } else if (object->IsEventController()) {
        counter = &g_ServerContext.EventController;
    } else if (object->IsEventMatch()) {
        counter = &g_ServerContext.EventMatch;
    } else if (object->IsFortressFlagpole()) {
        counter = &g_ServerContext.FortressFlagpole;
    } else if (object->IsObserverRemains()) {
        counter = &g_ServerContext.ObserverRemains;
    } else if (object->IsSignBoard()) {
        counter = &g_ServerContext.SignBoard;
    } else if (object->IsStatue()) {
        counter = &g_ServerContext.Statue;
    } else if (object->IsTeamBattleFlag()) {
        counter = &g_ServerContext.TeamBattleFlag;
    } else if (object->IsHolyThing()) {
        counter = &g_ServerContext.HolyThing;
    } else if (object->IsOwnThing()) {
        counter = &g_ServerContext.OwnThing;
    } else {
        counter = &g_ServerContext.EtcStaticObject;
    }
    counter->fetch_add(1, std::memory_order_relaxed);
}

} // namespace

CStaticObjectSmartIdManager g_StaticObjectSmartIdManager;
CPendingStaticObjects g_PendingStaticObjects;

CPendingStaticObjects::CPendingStaticObjects() :
    m_bLoading(true), m_alignment01To04{}, m_alignment14To18{} {}

CPendingStaticObjects::~CPendingStaticObjects() = default;

void CPendingStaticObjects::AppearInWorld() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        m_bLoading = false;
        for (CStaticObject* const object : m_Pendings) {
            if (object != nullptr) {
                CWorld::Inst().StaticObjectAppear(object, object->GetPos());
            }
        }
    }
}

void CPendingStaticObjects::RegisterAsPending(CStaticObject* const object) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Lock);
        if (!m_bLoading) {
            // Log.Add(LOG_ERROR, L"Loading is over")
            // L2_LOG_ERROR(Log, L"Loading is over")
            L2_LOG_ERROR(
                Log,
                "Cannot queue static object %p for startup appearance: loading has finished; the object is not added to the "
                "pending list.",
                static_cast<void*>(object)
            );
        } else {
            m_Pendings.push_back(object);
        }
    }
}

CStaticObject::CStaticObject() {
    L2_GUARD_FUNCTION();
    CStaticObjectFactory& factory = CStaticObjectFactory::Inst();
    m_data = factory.Add(this);
}

CStaticObject::CStaticObject(const CStaticObject& other) : CWorldObject(other) {
    L2_GUARD_FUNCTION();
    CStaticObjectFactory& factory = CStaticObjectFactory::Inst();
    m_data = factory.Add(this);
    m_data->CopyFrom(other.m_data);
}

CStaticObject::~CStaticObject() { CStaticObjectFactory::Inst().Remove(this); }

L2_DEFINE_OBJECT_CLASS_ID(CStaticObject)

L2_DEFINE_OBJECT_CLASS_NAME(CStaticObject, m_pClassName_CStaticObject)

L2_DEFINE_OBJECT_ALLOCATOR(CStaticObject, __GetClassName())

L2_DEFINE_OBJECT_NEW(CStaticObject)

L2_DEFINE_OBJECT_TYPED_RELEASE(CStaticObject)

bool CStaticObject::SetTargetable(bool targetable, bool flag) {
    L2_GUARD_FUNCTION();
    const bool changed = m_data->m_bTargetable != targetable;
    if (changed) {
        m_data->m_bTargetable = targetable;
        CWorldObject::SetTargetable(targetable, flag);
    }
    return changed;
}

bool CStaticObject::IsTargetable() { return m_data->m_bTargetable; }

double CStaticObject::GetCollisionRadius() const { return m_data->m_dCollisionRadius; }

FAngle CStaticObject::GetDir() const { return m_data->GetDir(); }

int CStaticObject::GetIndex() const { return m_data->index; }

CObject* CStaticObject::Copy() {
    L2_GUARD_FUNCTION();
    return new CStaticObject(*this);
}

bool CStaticObject::IsStaticObject() { return true; }

CStaticObject* CStaticObject::CastStaticObject() { return this; }

bool CStaticObject::IsInVehicle() { return static_cast<std::uint32_t>(m_data->container_index) - 0x14100U < 50U; }

bool CStaticObject::IsInAirShip() { return static_cast<std::uint32_t>(m_data->container_index) - 0x14132U < 100U; }

bool CStaticObject::IsInShuttle() { return static_cast<std::uint32_t>(m_data->container_index) - 0x14196U < 100U; }

unsigned int CStaticObject::GetMyAirShipID() {
    return static_cast<unsigned int>(CContainerDB::Inst().GetAirShipID(m_data->container_index));
}

FVector CStaticObject::GetMeleeAttackPosition(CCreature* attacker) {
    L2_GUARD_FUNCTION();
    CWorldObject* const attackerWorld = attacker;
    const FVector attackerPosition = attackerWorld->GetPos();
    const FVector position = GetPos();
    const FVector direction{
        attackerPosition.x - position.x,
        attackerPosition.y - position.y,
        attackerPosition.z - position.z,
    };
    const FVector normalized = direction.SafeNormal();
    const std::int32_t attackRange = attacker->m_data->m_nAttackRange;
    const double distance = static_cast<double>(attackRange) * 0.67;
    return FVector{
        position.x + normalized.x * distance,
        position.y + normalized.y * distance,
        position.z + normalized.z * distance,
    };
}

void CStaticObject::SetCollisionHeight(double height) { m_data->m_dCollisionHeight = height; }

double CStaticObject::GetCollisionHeight() const { return m_data->m_dCollisionHeight; }

void CStaticObject::SetCollisionRadius(double radius) { m_data->m_dCollisionRadius = radius; }

FVector CStaticObject::GetPos() const { return m_data->m_Pos; }

void CStaticObject::SetDir(const FAngle& direction) { m_data->SetDir(direction); }

void CStaticObject::Delete() {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::IncrementI32(&m_nDeleteCount) == 1) {
        SetDelete();
        g_StaticObjectSmartIdManager.DeleteObject(m_nId);
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid CStaticObject::Delete",
    //     L"StaticObject.cpp",
    //     0x46)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid CStaticObject::Delete",
    //     L"StaticObject.cpp",
    //     0x46)
    L2_LOG_ERROR(
        Log,
        "Repeated or invalid delete request for static object %p; deletion is skipped for this request. Reporting call "
        "stack.",
        static_cast<void*>(this)
    );
    runtime::process::ReportCallStack(2, false);
}

void CStaticObject::OnCreate() {
    L2_GUARD_FUNCTION();
    IncrementCurrentStaticObjectCounter(this);
    const std::int32_t index = m_data->index;
    m_nId = g_StaticObjectSmartIdManager.RegisterObject(
        // 11,
        SCLS_STATIC_OBJECT, static_cast<std::uint32_t>(index), this
    );
    CStaticObjectFactory::Inst().PublishIndexId(index, m_nId);
    m_data->m_nId = static_cast<std::int32_t>(m_nId);
    m_data->m_OrgTargetable = true;
    m_data->m_bTargetable = true;
}

void CStaticObject::SetPos(const FVector& position) { m_data->m_Pos = position; }

double CStaticObject::AttackedBy(CCreature* attacker, double damage) {
    static_cast<void>(attacker);
    static_cast<void>(damage);
    return 0.0;
}

double CStaticObject::GotDamageBy(CCreature* attacker, double damage) {
    static_cast<void>(attacker);
    static_cast<void>(damage);
    return 0.0;
}

void CStaticObject::SetHP(double hp) { m_data->m_dHP = hp; }

double CStaticObject::GetHP() const { return m_data->m_dHP; }

double CStaticObject::GetMDefend() { return m_data->m_dMDefend; }

double CStaticObject::GetPDefend() { return m_data->m_dPDefend; }

void CStaticObject::SetBreakable(bool breakable) { m_data->m_bBreakable = breakable; }

void CStaticObject::Interact(CCreature* creature) { static_cast<void>(creature); }

void CStaticObject::DecideDistanceAttackable(CMoveToTargetShareData* data, CSmartID creatureId, int distance) {
    L2_GUARD_FUNCTION();
    CCreature* const creature = creatureId.GetCreature();
    const double collisionRadius = creature->m_data->m_dCollisionRadius;
    const std::int32_t attackRange = creature->m_data->m_nAttackRange;

    std::int32_t computedDistance = distance;
    std::int32_t acceptableDistance = data->m_nSkillRange;
    if (data->m_nSkillId < 1) {
        if (data->m_bForceAttack) {
            const double adjustedRange = attackRange < 301
                ? static_cast<double>(attackRange) * 0.67
                : static_cast<double>(runtime::SubtractInt32Wrapping(attackRange, 100));
            computedDistance = runtime::Win64CvttDoubleToInt32(adjustedRange + collisionRadius);
        }
        acceptableDistance = runtime::AddInt32Wrapping(computedDistance, 10);
    } else {
        const std::int32_t skillRange = data->m_nSkillRange;
        const double adjustedRange = skillRange < 301
            ? static_cast<double>(skillRange) * 0.67
            : static_cast<double>(runtime::SubtractInt32Wrapping(skillRange, 100));
        computedDistance = runtime::Win64CvttDoubleToInt32(adjustedRange + collisionRadius);

        const double fullRange = static_cast<double>(skillRange) + collisionRadius - 2.0;
        double acceptable = static_cast<double>(runtime::AddInt32Wrapping(computedDistance, 10));
        if (fullRange <= acceptable) {
            acceptable = fullRange;
        }
        acceptableDistance = runtime::Win64CvttDoubleToInt32(acceptable);
    }

    data->m_nAcceptableDistance = acceptableDistance;
    data->m_nDistance = computedDistance;
}

void CStaticObject::SetOrgTargetable(bool targetable) {
    L2_GUARD_FUNCTION();
    m_data->m_OrgTargetable = targetable;
    m_data->m_bTargetable = targetable;
}

void CStaticObject::SendStaticObjectInfo(CUserSocket* socket) {
    if (!socket) {
        return;
    }
    socket->Send("cddddddddddd", 0x9f, m_data->m_nEditorID, m_nId, 0, 1, 0, 0, 0, 0, 0, 0, 0);
}

void CStaticObject::PushToThreadLocalMap() {
    L2_GUARD_FUNCTION();
    int thread = 0;
    int threadCount = ::CThreadManager::GetThreadCount();
    while (thread < threadCount) {
        L2_ADD_REF(this, ObjectRefType::ThreadLocalMap);
        ++thread;
        threadCount = ::CThreadManager::GetThreadCount();
    }
    IncreaseTLRef();
    if (GetIndex() >= 0) {
        g_ptlStaticObjectMap->Push(GetIndex(), GetPos(), this);
    }
}

void CStaticObject::PopFromThreadLocalMap() {
    L2_GUARD_FUNCTION();
    g_ptlStaticObjectMap->Remove(m_data->index, this);
}

CStaticObject* CStaticObject::GetObjectW(std::uint32_t smartId) {
    L2_GUARD_FUNCTION();
    return g_StaticObjectSmartIdManager.GetObjectW(smartId);
}

CSPointer<CStaticObject> CStaticObject::FindObjectSP(std::uint32_t smartId) {
    L2_GUARD_FUNCTION();
    CSPointer<CStaticObject> result;
    result.SetObject(GetObjectW(smartId), ObjectRefType::ObjectIndex);
    return result;
}
