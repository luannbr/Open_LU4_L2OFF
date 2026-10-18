#include "HolyThing.h"
#include "Castle.h"
#include "ObjectDB.h"
#include "Pledge.h"
#include "PledgeMemberService.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Residence.h"
#include "Shared/src/MemorySub.h"
#include "SharedContainer.h"
#include "Skill.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"

#include <atomic>
#include <climits>
#include <cstdint>
#include <string>


CHolyThing::CHolyThing() :
    m_nOwningResidenceID(0),
    m_residenceAlignment(0),
    m_pOwningResidence(nullptr),
    m_nOwningPledgeID(0),
    m_nPossessingMasterCount(0),
    // m_nStatus(0),
    m_nStatus(HTS_CLOSED),
    m_nClassId(0) {
    L2_GUARD_FUNCTION();

    CStaticObject::SetBreakable(false);
    m_data->m_dMaxHP = 150.0;
    CStaticObject::SetHP(150.0);
    CStaticObject::OnCreate();
}

CHolyThing::CHolyThing(const CHolyThing& other) :
    CStaticObject(other),
    m_nOwningResidenceID(0),
    m_residenceAlignment(0),
    m_pOwningResidence(nullptr),
    m_nOwningPledgeID(0),
    m_nPossessingMasterCount(0),
    // m_nStatus(0),
    m_nStatus(HTS_CLOSED),
    m_nClassId(0) {
    L2_GUARD_FUNCTION();
    CStaticObject::OnCreate();
}

CHolyThing::~CHolyThing() { g_ServerContext.HolyThing.fetch_sub(1, std::memory_order_relaxed); }

L2_DEFINE_OBJECT_CLASS_ID(CHolyThing)

L2_DEFINE_OBJECT_CLASS_NAME(CHolyThing, m_pClassName_CHolyThing)

L2_DEFINE_OBJECT_ALLOCATOR(CHolyThing, __GetClassName())

L2_DEFINE_OBJECT_NEW(CHolyThing)

L2_DEFINE_OBJECT_TYPED_RELEASE(CHolyThing)

void CHolyThing::TimerExpired(int id) {
    L2_GUARD_FUNCTION();
    static_cast<void>(id);
}

CObject* CHolyThing::Copy() { return new CHolyThing(*this); }

bool CHolyThing::IsHolyThing() { return true; }

CHolyThing* CHolyThing::CastHolyThing() { return this; }

bool CHolyThing::IsEnemyTo(CCreature* creature) {
    static_cast<void>(creature);
    L2_GUARD_FUNCTION();
    return false;
}

bool CHolyThing::IsAlive() { return true; }

bool CHolyThing::AddHolyThing(const wchar_t* const className, CHolyThing* const holyThing) {
    // Current L2Server 0x00780e34.
    L2_GUARD_FUNCTION();
    const FVector position = holyThing->GetPos();
    CWorldContainer* const container = CContainerDB::Inst().GetWorldContainer(
        static_cast<int>(position.x), static_cast<int>(position.y), static_cast<int>(position.z)
    );
    if (container == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"container null for holything %s",
        //     className)
        // L2_LOG_ERROR(Log,
        //     L"container null for holything %s",
        //     className)
        L2_LOG_ERROR(
            Log,
            "Cannot register holy thing '%s': no world container at (%.17g, %.17g, %.17g); pending-object registration "
            "skipped.",
            className ? WideToUtf8(className).c_str() : "<null>", position.x, position.y, position.z
        );
        return false;
    }
    g_PendingStaticObjects.RegisterAsPending(holyThing);
    const std::u16string targetName = target_utf16::FromRecoveredWindowsWide(className);
    holyThing->m_nClassId = g_ObjectDB.GetClassIdFromName(targetName.c_str(), false);
    return true;
}

void CHolyThing::DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) {
    static_cast<void>(creatureId);
    data->m_nDistance = 0;
    data->m_nAcceptableDistance = 0;
}

double CHolyThing::AttackedBy(CCreature* attacker, double damage) {
    static_cast<void>(attacker);
    static_cast<void>(damage);
    L2_GUARD_FUNCTION();
    return 0.0;
}

void CHolyThing::SendHolyThingInfo(CUserSocket* socket, bool useRequestedStatus, int status) {
    L2_GUARD_FUNCTION();

    if (!useRequestedStatus) {
        status = m_nStatus;
    }

    const int positionX = static_cast<int>(GetPos().x);
    const int positionY = static_cast<int>(GetPos().y);
    const int positionZ = static_cast<int>(GetPos().z);
    const int direction = static_cast<int>(static_cast<std::uint16_t>(GetDir().Yaw));
    const double collisionRadius = GetCollisionRadius();
    const double collisionHeight = GetCollisionHeight();
    constexpr const wchar_t* kEmptyName = L"";
    constexpr const char* kFormat = "cddSdddddffffdddd";
    if (socket == nullptr) {
        g_ptlUserMap->BroadcastToNeighbor(
            *this, 0x80000, GetPos(), kFormat, 0x92, static_cast<int>(m_nId), m_nClassId, kEmptyName, 0, positionX,
            positionY, positionZ, direction, 1.0, 1.0, collisionRadius, collisionHeight, 0, 0,
            // 3,
            SOT_HOLY_THING, status
        );
    } else {
        socket->Send(
            kFormat, 0x92, static_cast<int>(m_nId), m_nClassId, kEmptyName, 0, positionX, positionY, positionZ, direction,
            1.0, 1.0, collisionRadius, collisionHeight, 0, 0,
            // 3,
            SOT_HOLY_THING, status
        );
    }
}

void CHolyThing::SendWarningMessage(const CSkillInfo* skillInfo) {
    // Current L2Server 0x00781264.
    L2_GUARD_FUNCTION();
    if (!m_pOwningResidence->IsCastle()) {
        return;
    }

    CCastle* const castle = static_cast<CCastle*>(m_pOwningResidence);
    CResidenceData* const residenceData = castle->m_data;
    const int defendingCount = residenceData->m_DefendingPledgeListSize;
    for (int index = 0; index < defendingCount; ++index) {
        const PledgeInfo& defendingPledge = residenceData->m_stDefendingPledgeList[index];
        CSPointer<CPledge> pledge = CPledge::FindPledge(defendingPledge.m_nWorldID, defendingPledge.m_nPledgeID);
        if (pledge && skillInfo != nullptr) {
            SystemMessage<BattleFieldChecker, CCastle> message(
                SystemMessageId::ENEMY_CLAN_STARTED_NAMED_ACTION, castle, skillInfo->m_nSkillId, skillInfo->m_nLevel
            );
            pledge->ServeForAllMember(message, false);
        }
    }
}

void CHolyThing::SendCountDownMessage(const unsigned int messageId, const int argument) {
    // Current L2Server 0x0078142c.
    L2_GUARD_FUNCTION();
    g_ptlUserMap->BroadcastToNeighbor(
        *this, 0x80000, GetPos(), "chccc", 0x62, static_cast<int>(static_cast<std::uint16_t>(messageId)), 1, 0x14,
        static_cast<int>(static_cast<std::uint8_t>(argument))
    );
}

bool CHolyThing::AddPossessingMaster(User* user) {
    // Current L2Server 0x00781828.
    L2_GUARD_FUNCTION();
    const std::int32_t previous = runtime::raw_atomic::AddI32(&m_nPossessingMasterCount, 1) - 1;
    if (previous == 0) {
        // m_nStatus = 1.
        m_nStatus = HTS_OPENED;
        // SendHolyThingInfo(nullptr, true, 3)
        SendHolyThingInfo(nullptr, true, HTS_OPENING);
    }

    const CSkillInfo* const skillInfo = g_SkillDB.GetSkillInfo(CSkillKey(0xf6, 1), user);
    if (skillInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"possess_holything doesn't exist! [%d]",
        //     0xf6)
        // L2_LOG_ERROR(Log,
        //     L"possess_holything doesn't exist! [%d]",
        //     0xf6)
        L2_LOG_ERROR(
            Log,
            "Holy thing object %u cannot start possession: skill possess_holything (246, level 1) is missing; incremented "
            "possessing-master count and prior status update remain.",
            static_cast<unsigned>(m_nId)
        );
        return false;
    }
    user->StartSpelling(skillInfo, this);
    return true;
}

bool CHolyThing::PossessedBy(CCreature* creature, int isGood) {
    // Current L2Server 0x00781a18.
    L2_GUARD_FUNCTION();

    CResidence* const residence = m_pOwningResidence;
    if (residence == nullptr || !residence->IsCastle() || creature == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    RemovePossessingMaster(user);

    CPledge* const pledge = user->GetPledge();
    const std::uint32_t previousOwnerId = residence->m_data->m_nResidenceOwnerID;
    if (pledge == nullptr) {
        return false;
    }

    const std::uint32_t pledgeId = pledge->GetDbId();
    m_nOwningPledgeID = pledgeId;
    residence->ChangeOwner(pledge, isGood, true);

    const std::int32_t residenceId = residence->m_data != nullptr ? residence->m_data->m_nResidenceID : 0;
    constexpr const char16_t* kEmptyPledgeName = u"";
    constexpr std::u16string_view kAuditFormat = u"%I64d,,,,,,,,%s,%s,,%I64d,%I64d,%I64d,,,,,,,,,,,,";
    if (previousOwnerId == 0) {
        Log.AddT(
            LOG_AUDIT, kAuditFormat, static_cast<long long>(0x586), pledge->GetPledgeName(), kEmptyPledgeName,
            static_cast<long long>(pledgeId), static_cast<long long>(residenceId), static_cast<long long>(0)
        );
    } else {
        CSPointer<CPledge> previousPledge = CPledge::FindPledge(g_nWorldId, previousOwnerId);
        if (previousPledge) {
            Log.AddT(
                LOG_AUDIT, kAuditFormat, static_cast<long long>(0x586), pledge->GetPledgeName(),
                previousPledge->GetPledgeName(), static_cast<long long>(pledgeId), static_cast<long long>(residenceId),
                static_cast<long long>(previousPledge->GetDbId())
            );
        }
    }
    return true;
}

void CHolyThing::RemovePossessingMaster(User* user) {
    L2_GUARD_FUNCTION();

    const std::int32_t previous = runtime::raw_atomic::AddI32(&m_nPossessingMasterCount, -1) + 1;
    if (previous == 1) {
        // m_nStatus = 0.
        m_nStatus = HTS_CLOSED;
        // SendHolyThingInfo(nullptr, true, 2)
        SendHolyThingInfo(nullptr, true, HTS_CLOSING);
    }
    user->StopSpelling();
}
