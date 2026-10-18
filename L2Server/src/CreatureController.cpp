#include "CreatureController.h"
#include "Shared/src/MemorySub.h"

#include "CWorld.h"
#include "Creature.h"
#include "CursedWeapon.h"
#include "Fishing.h"
#include "HolyThing.h"
#include "ObjectDB.h"
#include "Pet.h"
#include "Portable/RuntimeTimingTrace.h"
#include "Protocol/src/SystemMessageIds.h"
#include "SerializedMessage.h"
#include "Shared/src/Exception.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/IOC.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/TimeAnalysis.h"
#include "SharedContainer.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>


#include "Shared/src/Log.h"


namespace {

void AssembleInstantEffectType(
    CCreature* actor,
    CSmartID targetId,
    CSkillInfo* skillInfo,
    CSkillAction2* skillAction,
    short& fatalBlowCount,
    char* buffer,
    const unsigned int capacity,
    unsigned int& length,
    const CSkillInfo::SkillEffects& effects
) {
    L2_GUARD_FUNCTION();

    for (CSkillEffect* const effect : effects) {
        effect->CalcSuccess(actor, targetId.GetWorldObject(), skillInfo, skillAction);
        if (skillAction->m_nFatalBlowSuccess == 1) {
            ++fatalBlowCount;
            const int assembled =
                ::Assemble(buffer + length, static_cast<int>(capacity - length), "h", static_cast<int>(effect->eEffectType));
            length += static_cast<unsigned int>(assembled);
        }
    }
}

void AssembleInstantEffects(
    CCreature* actor,
    CSmartID targetId,
    CSkillInfo* skillInfo,
    CSkillAction2* skillAction,
    short& fatalBlowCount,
    char* buffer,
    const unsigned int capacity,
    unsigned int& length,
    std::vector<FVector>* positions
) {
    L2_GUARD_FUNCTION();

    AssembleInstantEffectType(
        actor, targetId, skillInfo, skillAction, fatalBlowCount, buffer, capacity, length,
        *skillInfo->GetEffects(CSkillInfo::EAT_SUCCESS)
    );
    AssembleInstantEffectType(
        actor, targetId, skillInfo, skillAction, fatalBlowCount, buffer, capacity, length,
        *skillInfo->GetEffects(CSkillInfo::EAT_PVP)
    );
    AssembleInstantEffectType(
        actor, targetId, skillInfo, skillAction, fatalBlowCount, buffer, capacity, length,
        *skillInfo->GetEffects(CSkillInfo::EAT_PVE)
    );

    if (skillInfo->m_AffectScope == SAFS_BALAKAS_SCOPE) {
        if (positions == nullptr) {
            const int assembled = ::Assemble(buffer + length, static_cast<int>(capacity - length), "h", 0);
            length += static_cast<unsigned int>(assembled);
            // Log.Add(
            //     LOG_ERROR,
            //     L"pSkillInfo->m_AffectScope == SAFS_BALAKAS_SCOPE && NULL == pVectors")
            // L2_LOG_ERROR(Log,
            //     L"pSkillInfo->m_AffectScope == SAFS_BALAKAS_SCOPE && NULL == pVectors")
            L2_LOG_ERROR(
                Log,
                "Skill %d level %d uses the Balakas effect scope without a position list; zero-position count assembly "
                "returned %d, resulting packet length %u of %u bytes.",
                skillInfo->m_nSkillId, skillInfo->m_nLevel, assembled, length, capacity
            );
            return;
        }

        int assembled = ::Assemble(
            buffer + length, static_cast<int>(capacity - length), "h",
            static_cast<int>(static_cast<short>(positions->size()))
        );
        length += static_cast<unsigned int>(assembled);
        for (const FVector& position : *positions) {
            assembled = ::Assemble(
                buffer + length, static_cast<int>(capacity - length), "ddd", runtime::Win64CvttDoubleToInt32(position.x),
                runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z)
            );
            length += static_cast<unsigned int>(assembled);
        }
        return;
    }

    const SkillTargetTypeEnum targetType = skillInfo->m_TargetType;
    if ((targetType == STGT_GROUND || targetType == STGT_ARTILLERY || targetType == STGT_SHIP_CANNON) &&
        !actor->m_NonTargetSkillPos.IsEqual(FVector{})) {
        const FVector& position = actor->m_NonTargetSkillPos;
        const int assembled = ::Assemble(
            buffer + length, static_cast<int>(capacity - length), "hddd", 1, runtime::Win64CvttDoubleToInt32(position.x),
            runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z)
        );
        length += static_cast<unsigned int>(assembled);
        return;
    }

    if (targetType == STGT_GROUND || targetType == STGT_ARTILLERY || targetType == STGT_SHIP_CANNON) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"AssembleEffectType. pActor->m_NonTargetSkillPos = [0, 0, 0]")
        // L2_LOG_ERROR(Log,
        //     L"AssembleEffectType. pActor->m_NonTargetSkillPos = [0, 0, 0]")
        L2_LOG_ERROR(
            Log,
            "Skill %d level %d for actor object %u has target type %d but ground position (0, 0, 0); assembling zero target "
            "positions.",
            skillInfo->m_nSkillId, skillInfo->m_nLevel, static_cast<unsigned>(actor->m_nId), static_cast<int>(targetType)
        );
    }
    const int assembled = ::Assemble(buffer + length, static_cast<int>(capacity - length), "h", 0);
    length += static_cast<unsigned int>(assembled);
}

} // namespace

// Portable registration replaces the shipping number 97.
L2_REGISTER_OBJECT_TYPE(CCreatureController)

L2_DEFINE_OBJECT_CLASS_NAME_STORAGE(CCreatureController)

L2_DEFINE_OBJECT_CLASS_ID(CCreatureController)

L2_DEFINE_OBJECT_CLASS_NAME(CCreatureController, m_pClassName_CCreatureController)

L2_DEFINE_OBJECT_ALLOCATOR(CCreatureController, __GetClassName())

L2_DEFINE_OBJECT_NEW(CCreatureController)

L2_DEFINE_OBJECT_ADD_REF(CCreatureController)

L2_DEFINE_OBJECT_RELEASE(CCreatureController)

CCreatureController::CCreatureController() {
    L2_GUARD_FUNCTION();

    m_nActionNumber[0] = 0;
    m_nActionNumber[1] = 0;
    m_Action[0].m_Skill[0].m_nMyIndex = 0;
    m_Action[0].m_Skill[1].m_nMyIndex = 1;
    m_Action[1].m_Skill[0].m_nMyIndex = 0;
    m_Action[1].m_Skill[1].m_nMyIndex = 1;
    for (ActionData& action : m_Action) {
        action.m_Nothing.SetCreatureController(this);
        action.m_Stand.SetCreatureController(this);
        action.m_Sit.SetCreatureController(this);
        action.m_Move.SetCreatureController(this);
        action.m_Attack.SetCreatureController(this);
        action.m_Skill[0].SetCreatureController(this);
        action.m_Skill[1].SetCreatureController(this);
        action.m_Talk.SetCreatureController(this);
        action.m_General.SetCreatureController(this);
        action.m_GetItem.SetCreatureController(this);
        action.m_OpenClose.SetCreatureController(this);
        action.m_MoveToTarget.SetCreatureController(this);
        action.m_MoveToTargetRelative.SetCreatureController(this);
        action.m_MoveRelative.SetCreatureController(this);
        action.m_MoveToTargetWait.SetCreatureController(this);
        action.m_AttackWait.SetCreatureController(this);
        action.m_PrivateStore.SetCreatureController(this);
        action.m_CoupleAction.SetCreatureController(this);
    }

    SetAsNothing(0);
    SetAsNothing(1);
}

CCreatureController::~CCreatureController() { m_spActor = static_cast<CObject*>(nullptr); }

void CCreatureController::SetActor(CCreature* actor) {
    L2_GUARD_FUNCTION();

    m_spActor = CSmartID(actor);
#if defined(_WIN32)
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    Serializer.SetOwnerName(reinterpret_cast<const char16_t*>(actor->GetName()));
#else
    actor->GetName();
    Serializer.SetOwnerName(actor->m_data->Name());
#endif
}

void CCreatureController::ActionBroadcastMove(const FVector& source, const FVector& destination) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (runtime::runtime_timing::Matches(actor)) {
        runtime::runtime_timing::Record(
            "move_broadcast", actor, nullptr,
            "source=%.3f,%.3f,%.3f destination=%.3f,%.3f,%.3f "
            "mode=%d environment=%d base_speed=%d "
            "effective_speed=%.6f",
            source.x, source.y, source.z, destination.x, destination.y, destination.z,
            static_cast<int>(actor->m_data->m_MoveMode), static_cast<int>(actor->m_data->m_Environment),
            actor->m_data->m_nMoveTypeSpeed, actor->m_data->m_dParam[VALIDATE::SPEED]
        );
    }
    if (!actor->IsGMInvisible()) {
        g_ptlUserMap->BroadcastToNeighborContainer(
            actor, 0x14000, source, "cddddddd", 0x2f, static_cast<int>(actor->m_nId), static_cast<int>(destination.x),
            static_cast<int>(destination.y), static_cast<int>(destination.z), static_cast<int>(source.x),
            static_cast<int>(source.y), static_cast<int>(source.z)
        );
    } else if (actor->IsPC()) {
        actor->CastUser()->GetSocket()->Send(
            "cddddddd", 0x2f, static_cast<int>(actor->m_nId), static_cast<int>(destination.x),
            static_cast<int>(destination.y), static_cast<int>(destination.z), static_cast<int>(source.x),
            static_cast<int>(source.y), static_cast<int>(source.z)
        );
    }
}

void CCreatureController::ActionBroadcastMoveToTarget(CWorldObject& target, int distance) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    const FVector actorPosition = actor->GetPos();
    const FVector targetPosition = target.GetPos();
    if (!actor->IsGMInvisible()) {
        g_ptlUserMap->BroadcastToNeighborDist(
            actor, 0x80000, actorPosition, 0x1000, "cddddddddd", 0x72, static_cast<int>(actor->m_nId),
            static_cast<int>(target.m_nId), distance, static_cast<int>(actorPosition.x), static_cast<int>(actorPosition.y),
            static_cast<int>(actorPosition.z), static_cast<int>(targetPosition.x), static_cast<int>(targetPosition.y),
            static_cast<int>(targetPosition.z)
        );
    } else if (actor->IsPC()) {
        actor->CastUser()->GetSocket()->Send(
            "cddddddddd", 0x72, static_cast<int>(actor->m_nId), static_cast<int>(target.m_nId), distance,
            static_cast<int>(actorPosition.x), static_cast<int>(actorPosition.y), static_cast<int>(actorPosition.z),
            static_cast<int>(targetPosition.x), static_cast<int>(targetPosition.y), static_cast<int>(targetPosition.z)
        );
    }
}

void CCreatureController::ActionBroadcastMoveToTargetInAirShip(
    const FVector& position, const FVector& relativePosition, int targetId, int distance, unsigned int airShipId
) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (!actor->IsGMInvisible()) {
        g_ptlUserMap->BroadcastToNeighborDist(
            actor, 0x80000, position, 0x1000, "chddddddd", 0xfe, 0x72, static_cast<int>(actor->m_nId), targetId, distance,
            static_cast<int>(relativePosition.x), static_cast<int>(relativePosition.y), static_cast<int>(relativePosition.z),
            airShipId
        );
    } else if (actor->IsPC()) {
        actor->CastUser()->GetSocket()->Send(
            "chddddddd", 0xfe, 0x72, static_cast<int>(actor->m_nId), targetId, distance,
            static_cast<int>(relativePosition.x), static_cast<int>(relativePosition.y), static_cast<int>(relativePosition.z),
            airShipId
        );
    }
}

void CCreatureController::ActionBroadcastStopMove() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor->IsRidingWyvern() && actor->m_data->m_bIsAlive == 0) {
        return;
    }

    int broadcastDistance = actor->GetBroadcastDist(0x800);
    const FVector broadcastPosition = actor->GetPos();
    if (actor->IsPC() && static_cast<std::int32_t>(actor->m_data->m_moveType) == CMT_FLY) {
        broadcastDistance = 0x1000;
    }

    const int vehicleId = CContainerDB::Inst().GetVehicleID(actor->m_data->container_index);
    const unsigned int airShipId = actor->GetMyAirShipID();
    const unsigned int shuttleId = actor->GetMyShuttleID();
    const int actorId = static_cast<int>(actor->m_nId);

    if (vehicleId > 0) {
        const FVector relativePosition = actor->GetRelPos();
        if (actor->IsGMInvisible() && actor->IsPC()) {
            actor->CastUser()->GetSocket()->Send(
                "cdddddd", 0x7f, actorId, vehicleId, static_cast<int>(relativePosition.x),
                static_cast<int>(relativePosition.y), static_cast<int>(relativePosition.z),
                static_cast<int>(static_cast<std::uint16_t>(actor->m_data->GetDir().Yaw))
            );
        } else {
            g_ptlUserMap->BroadcastToNeighbor(
                *actor, 0x80000, broadcastPosition, "cdddddd", 0x7f, actorId, vehicleId,
                static_cast<int>(relativePosition.x), static_cast<int>(relativePosition.y),
                static_cast<int>(relativePosition.z),
                static_cast<int>(static_cast<std::uint16_t>(actor->m_data->GetDir().Yaw))
            );
        }
        return;
    }

    if (airShipId > 0) {
        const FVector relativePosition = actor->GetRelPos();
        if (actor->IsGMInvisible() && actor->IsPC()) {
            actor->CastUser()->GetSocket()->Send(
                "chdddddd", 0xfe, 0x6f, actorId, static_cast<int>(airShipId), static_cast<int>(relativePosition.x),
                static_cast<int>(relativePosition.y), static_cast<int>(relativePosition.z),
                static_cast<int>(static_cast<std::uint16_t>(actor->m_data->GetDir().Yaw))
            );
        } else {
            g_ptlUserMap->BroadcastToNeighbor(
                *actor, 0x80000, broadcastPosition, "chdddddd", 0xfe, 0x6f, actorId, static_cast<int>(airShipId),
                static_cast<int>(relativePosition.x), static_cast<int>(relativePosition.y),
                static_cast<int>(relativePosition.z),
                static_cast<int>(static_cast<std::uint16_t>(actor->m_data->GetDir().Yaw))
            );
        }
        return;
    }

    if (shuttleId > 0) {
        const FVector relativePosition = actor->GetRelPos();
        if (actor->IsGMInvisible() && actor->IsPC()) {
            actor->CastUser()->GetSocket()->Send(
                "chdddddd", 0xfe, 0xd0, actorId, static_cast<int>(shuttleId), static_cast<int>(relativePosition.x),
                static_cast<int>(relativePosition.y), static_cast<int>(relativePosition.z),
                static_cast<int>(static_cast<std::uint16_t>(actor->m_data->GetDir().Yaw))
            );
        } else {
            g_ptlUserMap->BroadcastToNeighbor(
                *actor, 0x80000, broadcastPosition, "chdddddd", 0xfe, 0xd0, actorId, static_cast<int>(shuttleId),
                static_cast<int>(relativePosition.x), static_cast<int>(relativePosition.y),
                static_cast<int>(relativePosition.z),
                static_cast<int>(static_cast<std::uint16_t>(actor->m_data->GetDir().Yaw))
            );
        }
        return;
    }

    if (actor->IsGMInvisible() && actor->IsPC()) {
        const int heading = static_cast<int>(static_cast<std::uint16_t>(actor->m_data->GetDir().Yaw));
        const int z = static_cast<int>(actor->GetPos().z);
        const int y = static_cast<int>(actor->GetPos().y);
        const int x = static_cast<int>(actor->GetPos().x);
        actor->CastUser()->GetSocket()->Send("cddddd", 0x47, actorId, x, y, z, heading);
        return;
    }

    const int heading = static_cast<int>(static_cast<std::uint16_t>(actor->m_data->GetDir().Yaw));
    const int z = static_cast<int>(actor->GetPos().z);
    const int y = static_cast<int>(actor->GetPos().y);
    const int x = static_cast<int>(actor->GetPos().x);
    const FVector position = actor->GetPos();
    g_ptlUserMap->BroadcastToNeighborDist(
        actor, 0x80000, position, broadcastDistance, "cddddd", 0x47, actorId, x, y, z, heading
    );
}

void CCreatureController::ActionBroadcastSkillUse(
    CWorldObject& target,
    CSkillInfo* skillInfo,
    int hitTime,
    int reuseDelay,
    CSkillAction2* skillAction,
    std::vector<FVector>* positions,
    const SkillUseInfo& skillUseInfo
) {
    L2_GUARD_FUNCTION();

    if (skillInfo == nullptr) {
        return;
    }

    CCreature* const actor = m_spActor.GetCreature();
    short fatalBlowCount = 0;
    unsigned int effectPacketLength = 0;
    std::array<char, 0x400> effectPacket;
    actor->m_data->m_NeedUpdatePacketBitSet.SetBoolValue_IfModifed(actor->m_IsSheathingWeapon, false, 0x5a);

    const bool initiallyInvisible = actor->IsGMInvisible();
    if (!initiallyInvisible) {
        goto visible_path;
    }

    if (!actor->IsPC()) {
        return;
    }

    AssembleInstantEffects(
        actor, target.GetSmartID(), skillInfo, skillAction, fatalBlowCount, effectPacket.data(),
        static_cast<unsigned int>(effectPacket.size()), effectPacketLength, positions
    );

    if (actor->IsInAirShip()) {
        const FVector relativePosition = actor->GetRelPos();
        CUserSocket* const socket = actor->CastUser()->GetSocket();
        const int packedLevel = (skillInfo->m_nSubLevel << 16) | (skillInfo->m_nLevel & 0xffff);
        const unsigned int airShipId = actor->GetMyAirShipID();
        socket->Send(
            "chddddddddddddhb", 0xfe, 0x74, skillAction->m_nSlotNo, static_cast<int>(actor->m_nId),
            static_cast<int>(target.m_nId), skillInfo->m_nSkillId, packedLevel, hitTime, skillInfo->m_nReuseDelayGroup,
            reuseDelay, runtime::Win64CvttDoubleToInt32(relativePosition.x),
            runtime::Win64CvttDoubleToInt32(relativePosition.y), runtime::Win64CvttDoubleToInt32(relativePosition.z),
            static_cast<int>(airShipId), static_cast<int>(fatalBlowCount), static_cast<int>(effectPacketLength),
            effectPacket.data()
        );
        return;
    }

    if (!actor->IsCheckBot1(hitTime, reuseDelay)) {
        goto invisible_normal_path;
    }

    {
        const char16_t* const actorName = actor->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[CHECK_BOT] [%s][%d] S_MAGIC_SKILL_USE UserId = %d, UserName = %s, HitTime = % d, CoolTime = %d",
        //     L"CreatureController.cpp",
        //     600,
        //     static_cast<int>(actor->m_nId),
        //     actorName,
        //     hitTime,
        //     reuseDelay)
        // L2_LOG_ERROR(Log,
        //     L"[CHECK_BOT] [%s][%d] S_MAGIC_SKILL_USE UserId = %d, UserName = %s, HitTime = % d, CoolTime = %d",
        //     L"CreatureController.cpp",
        //     600,
        //     static_cast<int>(actor->m_nId),
        //     actorName,
        //     hitTime,
        //     reuseDelay)
        // L2_LOG_WARN(Log,
        //     "Skill timing check triggered for invisible player '%s' (object %u), skill %d level %d: hit time %d ms, reuse
        //     delay %d ms. Socket validation precedes forced close.", actorName ? WideToUtf8(actorName).c_str() : "<null>",
        //     static_cast<unsigned>(actor->m_nId), skillInfo->m_nSkillId, skillInfo->m_nLevel, hitTime, reuseDelay)
        L2_LOG_WARN(
            Log,
            "Skill timing check triggered for invisible player '%s' (object %u), skill %d level %d: hit time %d ms, reuse "
            "delay %d ms; socket validation precedes forced close.",
            actorName ? Utf16ToUtf8(actorName).c_str() : "<null>", static_cast<unsigned>(actor->m_nId),
            skillInfo->m_nSkillId, skillInfo->m_nLevel, hitTime, reuseDelay
        );
        if (actor->CastUser()->GetSocket() == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[CHECK_BOT] [%s][%d] S_MASIC_SKILL_USE Socket is NULL",
            //     L"CreatureController.cpp",
            //     0x260)
            // L2_LOG_ERROR(Log,
            //     L"[CHECK_BOT] [%s][%d] S_MASIC_SKILL_USE Socket is NULL",
            //     L"CreatureController.cpp",
            //     0x260)
            L2_LOG_WARN(
                Log,
                "Skill timing check cannot close invisible player object %u: socket is missing; skill-use processing stops.",
                static_cast<unsigned>(actor->m_nId)
            );
            return;
        }
    }
    goto force_close;

force_close:
    actor->CastUser()->GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
    return;

invisible_normal_path:
    if (actor->IsSkillDebugMode()) {
        std::array<char16_t, 128> message{};
        FormatUtf16To(
            message.data(), message.size(), u"[SkillDebug] skill[id:%d, level:%d] EstimatedTime[%d] nReuseDelay[%d]",
            skillInfo->m_nSkillId, skillInfo->m_nLevel, hitTime, reuseDelay
        );
        actor->SendSystemMessage(u"SYS", message.data());
    }
    {
        int slotNo = skillAction->m_nSlotNo;
        int fatalCount = static_cast<int>(fatalBlowCount);
        int packetLength = static_cast<int>(effectPacketLength);
        actor->CastUser()->SendMagicSkill(
            slotNo, &target, skillUseInfo, hitTime, skillInfo->m_nReuseDelayGroup, reuseDelay, fatalCount, packetLength,
            effectPacket.data()
        );
    }
    return;

visible_path: {
    AssembleInstantEffects(
        actor, target.GetSmartID(), skillInfo, skillAction, fatalBlowCount, effectPacket.data(),
        static_cast<unsigned int>(effectPacket.size()), effectPacketLength, positions
    );

    const int broadcastDistance = actor->GetBroadcastDist(0xc00);
    if (actor->IsInAirShip()) {
        const FVector relativePosition = actor->GetRelPos();
        const int packedLevel = (skillInfo->m_nSubLevel << 16) | (skillInfo->m_nLevel & 0xffff);
        const unsigned int airShipId = actor->GetMyAirShipID();
        const FVector broadcastPosition = actor->GetPos();
        g_ptlUserMap->BroadcastToNeighborDist(
            actor, 0x5000, broadcastPosition, broadcastDistance, "chddddddddddddhb", 0xfe, 0x74, skillAction->m_nSlotNo,
            static_cast<int>(actor->m_nId), static_cast<int>(target.m_nId), skillInfo->m_nSkillId, packedLevel, hitTime,
            skillInfo->m_nReuseDelayGroup, reuseDelay, runtime::Win64CvttDoubleToInt32(relativePosition.x),
            runtime::Win64CvttDoubleToInt32(relativePosition.y), runtime::Win64CvttDoubleToInt32(relativePosition.z),
            static_cast<int>(airShipId), static_cast<int>(fatalBlowCount), static_cast<int>(effectPacketLength),
            effectPacket.data()
        );
        return;
    }

    if (!actor->IsCheckBot1(hitTime, reuseDelay)) {
        if (actor->IsSkillDebugMode()) {
            std::array<char16_t, 128> message{};
            FormatUtf16To(
                message.data(), message.size(), u"[SkillDebug] skill[id:%d, level:%d] EstimatedTime[%d] nReuseDelay[%d]",
                skillInfo->m_nSkillId, skillInfo->m_nLevel, hitTime, reuseDelay
            );
            actor->SendSystemMessage(u"SYS", message.data());
        }

        int slotNo = skillAction->m_nSlotNo;
        int fatalCount = static_cast<int>(fatalBlowCount);
        int packetLength = static_cast<int>(effectPacketLength);
        DefaultIOBuffer* const packet = actor->MakePacketForMagicSkillUse(
            slotNo, &target, skillUseInfo, hitTime, skillInfo->m_nReuseDelayGroup, reuseDelay, fatalCount, packetLength,
            effectPacket.data()
        );
        const FVector actorPosition = actor->GetPos();
        g_ptlUserMap->BroadcastToNeighborDist(actor, 0x5000, actorPosition, broadcastDistance, packet);
        packet->Release();
        return;
    }

    const char16_t* const actorName = actor->GetName();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[CHECK_BOT] [%s][%d] S_MAGIC_SKILL_USE UserId = %d, UserName = %s, HitTime = % d, CoolTime = %d",
    //     L"CreatureController.cpp",
    //     0x286,
    //     static_cast<int>(actor->m_nId),
    //     actorName,
    //     hitTime,
    //     reuseDelay)
    // L2_LOG_ERROR(Log,
    //     L"[CHECK_BOT] [%s][%d] S_MAGIC_SKILL_USE UserId = %d, UserName = %s, HitTime = % d, CoolTime = %d",
    //     L"CreatureController.cpp",
    //     0x286,
    //     static_cast<int>(actor->m_nId),
    //     actorName,
    //     hitTime,
    //     reuseDelay)
    // L2_LOG_WARN(Log,
    //     "Skill timing check triggered for actor '%s' (object %u), skill %d level %d: hit time %d ms, reuse delay %d ms.
    //     player/socket validation precedes forced close.", actorName ? WideToUtf8(actorName).c_str() : "<null>",
    //     static_cast<unsigned>(actor->m_nId), skillInfo->m_nSkillId, skillInfo->m_nLevel, hitTime, reuseDelay)
    L2_LOG_WARN(
        Log,
        "Skill timing check triggered for actor '%s' (object %u), skill %d level %d: hit time %d ms, reuse delay %d ms; "
        "player/socket validation precedes forced close.",
        actorName ? Utf16ToUtf8(actorName).c_str() : "<null>", static_cast<unsigned>(actor->m_nId), skillInfo->m_nSkillId,
        skillInfo->m_nLevel, hitTime, reuseDelay
    );
    if (!actor->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[CHECK_BOT] [%s][%d] S_MASIC_SKILL_USE pActor is not User",
        //     L"CreatureController.cpp",
        //     0x296)
        // L2_LOG_ERROR(Log,
        //     L"[CHECK_BOT] [%s][%d] S_MASIC_SKILL_USE pActor is not User",
        //     L"CreatureController.cpp",
        //     0x296)
        L2_LOG_WARN(
            Log, "Skill timing check cannot close actor object %u: actor is not a player; skill-use processing stops.",
            static_cast<unsigned>(actor->m_nId)
        );
        return;
    }
    if (actor->CastUser()->GetSocket() == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[CHECK_BOT] [%s][%d] S_MASIC_SKILL_USE Socket is NULL",
        //     L"CreatureController.cpp",
        //     0x291)
        // L2_LOG_ERROR(Log,
        //     L"[CHECK_BOT] [%s][%d] S_MASIC_SKILL_USE Socket is NULL",
        //     L"CreatureController.cpp",
        //     0x291)
        L2_LOG_WARN(
            Log, "Skill timing check cannot close player object %u: socket is missing; skill-use processing stops.",
            static_cast<unsigned>(actor->m_nId)
        );
        return;
    }
}
    goto force_close;
}

void CCreatureController::ActionBroadcastAttack(
    const AttackTargetDataType& primaryTarget, int targetCount, AttackTargetDataType* targets, CWorldObject& target
) {
    // Current L2Server 0x00634404.
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return;
    }

    const FVector actorPosition = actor->GetPos();
    const FVector targetPosition = target.GetPos();
    int broadcastDistance = actor->GetBroadcastDist(0xc00);
    if (actor->IsPC() && actor->CastUser()->m_nUserCountInSight > 50) {
        broadcastDistance = 0x800;
    }

    const auto attackFlags = [](const AttackTargetDataType& data) {
        // std::uint8_t flags = data.bIsHit ? 0 : 1
        std::uint8_t flags = data.bIsHit ? 0 : AttackFlag::MISS;
        if (data.eShieldDefense != AttackTargetDataType::SDT_NONE) {
            // flags |= 2.
            flags |= AttackFlag::SHIELD_DEFENSE;
        }
        if (data.bIsCritical) {
            // flags |= 4.
            flags |= AttackFlag::CRITICAL;
        }
        if (data.nIsSoulShot != 0) {
            // flags |= 8.
            flags |= AttackFlag::SPIRIT;
        }
        return flags;
    };

    actor->m_data->m_NeedUpdatePacketBitSet.SetBoolValue_IfModifed(actor->m_IsSheathingWeapon, false, 0x5a);
    const int soulShotClassId = g_ObjectDB.GetSoulShotClassID(actor);
    const int actorId = static_cast<int>(actor->m_nId);
    const int damage = runtime::Win64CvttDoubleToInt32(primaryTarget.dDamage);
    const int flags = attackFlags(primaryTarget);
    const int soulShotState = primaryTarget.nIsSoulShot - 1;

    if (targetCount == 0) {
        if (!actor->IsInAirShip()) {
            g_ptlUserMap->BroadcastToNeighborDist(
                actor, 0x5000, actorPosition, broadcastDistance, "cdddddddddhddd", 0x33, actorId, primaryTarget.nTargetId,
                soulShotClassId, damage, flags, soulShotState, runtime::Win64CvttDoubleToInt32(actorPosition.x),
                runtime::Win64CvttDoubleToInt32(actorPosition.y), runtime::Win64CvttDoubleToInt32(actorPosition.z), 0,
                runtime::Win64CvttDoubleToInt32(targetPosition.x), runtime::Win64CvttDoubleToInt32(targetPosition.y),
                runtime::Win64CvttDoubleToInt32(targetPosition.z)
            );
        } else {
            const FVector relativePosition = actor->GetRelPos();
            g_ptlUserMap->BroadcastToNeighborDist(
                actor, 0x5000, actorPosition, broadcastDistance, "chddddddddddh", 0xfe, 0x73, actorId,
                primaryTarget.nTargetId, soulShotClassId, damage, flags, soulShotState,
                runtime::Win64CvttDoubleToInt32(relativePosition.x),
                runtime::Win64CvttDoubleToInt32(relativePosition.y),
                runtime::Win64CvttDoubleToInt32(relativePosition.z), static_cast<int>(actor->GetMyAirShipID()), 0
            );
        }
        return;
    }

    int packetTargetCount = targetCount;
    if (packetTargetCount > 0x7fff) {
        packetTargetCount = 0x7fff;
    }
    std::array<char, 0x5000> targetPacket{};
    int targetPacketLength = 0;
    for (int index = 0; index < packetTargetCount; ++index) {
        const AttackTargetDataType& extraTarget = targets[index];
        const int assembled = ::Assemble(
            targetPacket.data() + targetPacketLength, static_cast<int>(targetPacket.size()) - targetPacketLength, "dddd",
            extraTarget.nTargetId, runtime::Win64CvttDoubleToInt32(extraTarget.dDamage),
            static_cast<int>(attackFlags(extraTarget)), soulShotState
        );
        if (assembled < 1) {
            // LOGE(
            //     "Assemble Error at file[%s], line[%d], "
            //     "assemble result = [%d], total length = [%d]",
            //     "CreatureController.cpp",
            //     0x363,
            //     assembled,
            //     targetPacketLength)
            // L2_LOG_ERROR(Log,
            //     "Assemble Error at file[%s], line[%d], "
            //     "assemble result = [%d], total length = [%d]",
            //     "CreatureController.cpp",
            //     0x363,
            //     assembled,
            //     targetPacketLength)
            L2_LOG_ERROR(
                Log,
                "Attack packet for actor object %u could not append target index %d of %d: assembly returned %d at %d of "
                "%zu bytes; processing continues with the declared target count unchanged.",
                static_cast<unsigned>(actor->m_nId), index, packetTargetCount, assembled, targetPacketLength,
                targetPacket.size()
            );
        } else {
            targetPacketLength += assembled;
        }
    }

    if (!actor->IsInAirShip()) {
        g_ptlUserMap->BroadcastToNeighborDist(
            actor, 0x5000, actorPosition, broadcastDistance, "cdddddddddhbddd", 0x33, actorId, primaryTarget.nTargetId,
            soulShotClassId, damage, flags, soulShotState, runtime::Win64CvttDoubleToInt32(actorPosition.x),
            runtime::Win64CvttDoubleToInt32(actorPosition.y), runtime::Win64CvttDoubleToInt32(actorPosition.z),
            static_cast<int>(static_cast<std::int16_t>(packetTargetCount)), targetPacketLength, targetPacket.data(),
            runtime::Win64CvttDoubleToInt32(targetPosition.x), runtime::Win64CvttDoubleToInt32(targetPosition.y),
            runtime::Win64CvttDoubleToInt32(targetPosition.z)
        );
    } else {
        const FVector relativePosition = actor->GetRelPos();
        g_ptlUserMap->BroadcastToNeighborDist(
            actor, 0x5000, actorPosition, broadcastDistance, "chddddddddddhb", 0xfe, 0x73, actorId, primaryTarget.nTargetId,
            soulShotClassId, damage, flags, soulShotState, runtime::Win64CvttDoubleToInt32(relativePosition.x),
            runtime::Win64CvttDoubleToInt32(relativePosition.y), runtime::Win64CvttDoubleToInt32(relativePosition.z),
            static_cast<int>(actor->GetMyAirShipID()), static_cast<int>(static_cast<std::int16_t>(packetTargetCount)),
            targetPacketLength, targetPacket.data()
        );
    }
}

void CCreatureController::DoAttack(
    AttackTargetDataType& primaryTarget,
    int targetCount,
    AttackTargetDataType* targets,
    int weaponClassId,
    AttackType attackType,
    SlotType slotType
) {
    // Current L2Server 0x006371a4.
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return;
    }
    if (!actor->IsAlive()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"dead man cannot attack. CCreatureController::DoAttack.")
        // L2_LOG_ERROR(Log,
        //     L"dead man cannot attack. CCreatureController::DoAttack.")
        L2_LOG_WARN(Log, "Attack ignored for actor object %u: actor is dead.", static_cast<unsigned>(actor->m_nId));
        return;
    }

    actor->m_data->m_nLastAttackTick = static_cast<std::int32_t>(runtime::GetTickCount());
    CObject* const target = CObject::GetObjectW(static_cast<std::uint32_t>(primaryTarget.nTargetId));

    bool cursedWeaponRelation = false;
    User* attackUser = nullptr;
    if (actor->IsPC()) {
        attackUser = actor->CastUser();
    } else if (actor->IsSummonOrPet()) {
        if (actor->CastSummon()->GetMaster() != nullptr) {
            attackUser = actor->CastSummon()->GetMaster();
        }
    }
    if (attackUser != nullptr) {
        static_cast<void>(CCursedWeaponMgr::inst());
        cursedWeaponRelation = attackUser->m_nCursedWeaponClassId > 0;
    }
    if (!cursedWeaponRelation) {
        attackUser = nullptr;
        if (target != nullptr) {
            if (target->IsPC()) {
                attackUser = target->CastUser();
            } else if (target->IsSummonOrPet()) {
                if (target->CastSummon()->GetMaster() != nullptr) {
                    attackUser = target->CastSummon()->GetMaster();
                }
            }
        }
        if (attackUser != nullptr) {
            static_cast<void>(CCursedWeaponMgr::inst());
            cursedWeaponRelation = attackUser->m_nCursedWeaponClassId > 0;
        }
    }

    bool duelRelation = false;
    if (actor->IsInDuel() && target != nullptr && target->IsCreature()) {
        duelRelation = actor->IsDuelEnemy(target->CastCreature());
    }

    actor->m_dPenaltyTargetCanResist = 1.0;
    CWorld::Inst().Attack(static_cast<int>(actor->m_nId), weaponClassId, primaryTarget, attackType, slotType, true);

    if (target != nullptr) {
        const bool targetIsGuilty = target->IsCreature() && target->CastCreature()->IsGuilty();
        if (!cursedWeaponRelation && !duelRelation && target->IsMakeAttackerGuilty(actor, false)) {
            CCreature* guiltyActor = actor;
            if (actor->IsSummonOrPet()) {
                guiltyActor = actor->CastSummon()->GetMaster();
            }
            if (guiltyActor == nullptr) {
                guiltyActor = actor;
            }
            if (!targetIsGuilty) {
                guiltyActor->m_data->m_cIsGuilty = 1;
            }
            CIOObject::SetTimeStamp(&guiltyActor->m_data->m_nPKTimeStamp);
        }
    }

    const bool suppressNeighborNotification = attackType == AT_POLE || attackType == AT_TWOHANDSWORD ||
        attackType == AT_TWOHANDBLUNT || attackType == AT_TWOHANDSTAFF ||
        (attackType == AT_ANCIENTSWORD && slotType == ST_RLHAND);
    for (int index = 0; index < targetCount; ++index) {
        CWorld::Inst().Attack(
            static_cast<int>(actor->m_nId), weaponClassId, targets[index], attackType, slotType,
            !suppressNeighborNotification
        );
    }
}

void CCreatureController::ActionBroadcastEarthQuake(FVector position, int intensity, int duration) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    const int broadcastDistance = actor->GetBroadcastDist(0x200);
    g_ptlUserMap->BroadcastToNeighborDist(
        actor, 0x5000, position, broadcastDistance, "cdddddd", 0xd3, static_cast<int>(position.x),
        static_cast<int>(position.y), static_cast<int>(position.z), intensity, duration, 1
    );
}

void CCreatureController::ActionBroadcastSkillLaunched(
    int skillId, int skillLevel, int skillSubLevel, int targetId, int skillIndex
) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    const int actorId = static_cast<int>(actor->m_nId);
    const int packedLevel = (skillSubLevel << 16) | (skillLevel & 0xffff);
    if (!actor->IsGMInvisible()) {
        const int broadcastDistance = actor->GetBroadcastDist(0xc00);
        const FVector position = actor->GetPos();
        g_ptlUserMap->BroadcastToNeighborDist(
            actor, 0xa000, position, broadcastDistance, "cdddddd", 0x54, skillIndex, actorId, skillId, packedLevel, 1,
            targetId
        );
    } else if (actor->IsPC()) {
        actor->CastUser()->GetSocket()->Send("cdddddd", 0x54, skillIndex, actorId, skillId, packedLevel, 1, targetId);
    }
}

void CCreatureController::ActionBroadcastSkillCanceled(CSkillAction2* skillAction) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    const int actorId = static_cast<int>(actor->m_nId);
    if (!actor->IsGMInvisible()) {
        const int broadcastDistance = actor->GetBroadcastDist(0xc00);
        const FVector position = actor->GetPos();
        g_ptlUserMap->BroadcastToNeighborDist(actor, 0xf000, position, broadcastDistance, "cd", 0x49, actorId);
    } else if (actor->IsPC()) {
        actor->CastUser()->GetSocket()->Send("cd", 0x49, actorId);
    }

    const int skillId = skillAction->GetSkillId();
    if (skillId != 246 && skillId != 19034 && skillId != 19035) {
        return;
    }

    CObject* const target = skillAction->m_spTarget.GetObjectW();
    if (target != nullptr && target->IsHolyThing() && actor->IsPC()) {
        target->CastHolyThing()->RemovePossessingMaster(actor->CastUser());
    }
}

void CCreatureController::ActionBroadcastSkillLaunched(
    int skillId, int skillLevel, int skillSubLevel, CreatureSP2VectorType targets, int skillIndex
) {
    L2_GUARD_FUNCTION();

    std::array<char, 0x5000> targetPacket{};
    int targetPacketLength = 0;
    for (const CSafePointer2<CCreature>& target : targets) {
        const int assembled = packet_helper_detail::AssemblePacket(
            targetPacket.data() + targetPacketLength, static_cast<int>(targetPacket.size()) - targetPacketLength, "d",
            target.id
        );
        if (assembled < 1) {
            // LOGE(
            //     "Assemble Error at file[%s], line[%d], assemble result = [%d], total length = [%d]",
            //     "CreatureController.cpp",
            //     0x2e7,
            //     assembled,
            //     targetPacketLength)
            // L2_LOG_ERROR(Log,
            //     "Assemble Error at file[%s], line[%d], assemble result = [%d], total length = [%d]",
            //     "CreatureController.cpp",
            //     0x2e7,
            //     assembled,
            //     targetPacketLength)
            L2_LOG_ERROR(
                Log,
                "Skill-launch packet for skill %d level %d sublevel %d could not append target smart ID 0x%08x: assembly "
                "returned %d at %d of %zu bytes; remaining targets continue and the declared count is unchanged.",
                skillId, skillLevel, skillSubLevel, static_cast<unsigned>(target.id), assembled, targetPacketLength,
                targetPacket.size()
            );
        } else {
            targetPacketLength += assembled;
        }
    }

    CCreature* const actor = m_spActor.GetCreature();
    const int actorId = static_cast<int>(actor->m_nId);
    const int packedLevel = (skillSubLevel << 16) | (skillLevel & 0xffff);
    const int targetCount = static_cast<int>(targets.size());
    if (!actor->IsGMInvisible()) {
        const int broadcastDistance = actor->GetBroadcastDist(0xc00);
        const FVector position = actor->GetPos();
        g_ptlUserMap->BroadcastToNeighborDist(
            actor, 0xa000, position, broadcastDistance, "cdddddb", 0x54, skillIndex, actorId, skillId, packedLevel,
            targetCount, targetPacketLength, targetPacket.data()
        );
    } else if (actor->IsPC()) {
        actor->CastUser()->GetSocket()->Send(
            "cdddddb", 0x54, skillIndex, actorId, skillId, packedLevel, targetCount, targetPacketLength, targetPacket.data()
        );
    }
}

void CCreatureController::BroadcastFlySelfDestination(CSkillInfo* skillInfo, int targetId) {
    L2_GUARD_FUNCTION();

    if (skillInfo == nullptr) {
        return;
    }

    CCreature* const actor = m_spActor.GetCreature();
    CCreature* const target = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(targetId));
    if (target == nullptr) {
        return;
    }

    const FVector actorPosition = actor->m_data->m_Pos;
    const FVector targetPosition = target->GetPos();
    const double deltaX = targetPosition.x - actorPosition.x;
    const double deltaY = targetPosition.y - actorPosition.y;
    int heading = actor->m_data->GetDir().Yaw;
    if (!FVector{deltaX, deltaY, 0.0}.IsEqual(FVector{})) {
        constexpr double twoPi = 6.28318530717958647692;
        heading = static_cast<int>(std::atan2(deltaY, deltaX) * 65535.0 / twoPi);
    }

    heading =
        (heading + static_cast<int>(static_cast<double>(skillInfo->m_DamageRange.direction) / 360.0 * 65535.0)) & 0xffff;
    constexpr double pi = 3.14159265358979323846;
    const double radians = static_cast<double>(heading) * pi / 32767.5;
    const double distance = static_cast<double>(skillInfo->m_DamageRange.distance);
    const FVector requestedDestination{
        targetPosition.x + distance * std::cos(radians), targetPosition.y + distance * std::sin(radians), targetPosition.z};

    FVector destination{};
    double travelledDistance = 0.0;
    bool distanceExhausted = false;
    const double requestedDistance = std::sqrt(
        (requestedDestination.x - targetPosition.x) * (requestedDestination.x - targetPosition.x) +
        (requestedDestination.y - targetPosition.y) * (requestedDestination.y - targetPosition.y) +
        (requestedDestination.z - targetPosition.z) * (requestedDestination.z - targetPosition.z)
    );
    CGeoData* const geo = CWorld::Inst().GetGeo();
    geo->MoveStraightTest(
        targetPosition, requestedDestination, destination, travelledDistance, distanceExhausted, requestedDistance, false, 0
    );
    destination.z = static_cast<double>(
        geo->FindGround(static_cast<int>(destination.x), static_cast<int>(destination.y), static_cast<int>(destination.z))
    );
    actor->m_NonTargetSkillPos = destination;

    const int broadcastDistance = actor->GetBroadcastDist(0xc00);
    const FVector broadcastPosition = actor->GetPos();
    g_ptlUserMap->BroadcastToNeighborDist(
        actor, 0x5000, broadcastPosition, broadcastDistance, "chdddd", 0xfe, 0x44, static_cast<int>(actor->m_nId),
        runtime::Win64CvttDoubleToInt32(destination.x), runtime::Win64CvttDoubleToInt32(destination.y),
        runtime::Win64CvttDoubleToInt32(destination.z)
    );
}

// Field-bearing current CopyToPad roots placement-copy the concrete carrier.
// The portable pad may be unaligned, so copy the vptr and observed fields
// separately while leaving the same padding bytes unwritten. Empty current
// carrier roots leave the entire destination untouched.
CCreatureController::__async_arg_DoNothing::__async_arg_DoNothing(__async_arg_DoNothing* other) : __a1(other->__a1) {}

CCreatureController::__async_arg_DoNothing::__async_arg_DoNothing() {}

CCreatureController::__async_arg_DoNothing::__async_arg_DoNothing(bool a1) : __a1(a1) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CCreatureController, __async_arg_DoNothing)

CCreatureController::__async_arg_Stand::__async_arg_Stand(__async_arg_Stand* other) : __a1(other->__a1) {}

CCreatureController::__async_arg_Stand::__async_arg_Stand() {}

CCreatureController::__async_arg_Stand::__async_arg_Stand(int a1) : __a1(a1) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CCreatureController, __async_arg_Stand)

CCreatureController::__async_arg_Move::__async_arg_Move(__async_arg_Move* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3) {}

CCreatureController::__async_arg_Move::__async_arg_Move() {}

CCreatureController::__async_arg_Move::__async_arg_Move(FVector a1, bool a2, OptionData* a3) :
    __a1(a1), __a2(a2), __a3(a3) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_3(CCreatureController, __async_arg_Move)

CCreatureController::__async_arg_MoveToTarget::__async_arg_MoveToTarget(__async_arg_MoveToTarget* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3), __a4(other->__a4) {}

CCreatureController::__async_arg_MoveToTarget::__async_arg_MoveToTarget() {}

CCreatureController::__async_arg_MoveToTarget::__async_arg_MoveToTarget(CObject* a1, bool a2, SkillUseInfo a3, int a4) :
    __a1(a1), __a2(a2), __a3(a3), __a4(a4) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_4(CCreatureController, __async_arg_MoveToTarget)

CCreatureController::__async_arg_MoveToTargetRelative::__async_arg_MoveToTargetRelative(
    __async_arg_MoveToTargetRelative* other
) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3), __a4(other->__a4) {}

CCreatureController::__async_arg_MoveToTargetRelative::__async_arg_MoveToTargetRelative() {}

CCreatureController::__async_arg_MoveToTargetRelative::__async_arg_MoveToTargetRelative(
    CObject* a1, bool a2, SkillUseInfo a3, int a4
) :
    __a1(a1), __a2(a2), __a3(a3), __a4(a4) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_4(CCreatureController, __async_arg_MoveToTargetRelative)

CCreatureController::__async_arg_Attack::__async_arg_Attack(__async_arg_Attack* other) :
    __a1(other->__a1), __a2(other->__a2) {}

CCreatureController::__async_arg_Attack::__async_arg_Attack() {}

CCreatureController::__async_arg_Attack::__async_arg_Attack(CObject* a1, bool a2) : __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CCreatureController, __async_arg_Attack)

CCreatureController::__async_arg_Skill::__async_arg_Skill(__async_arg_Skill* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3), __a4(other->__a4) {}

CCreatureController::__async_arg_Skill::__async_arg_Skill() {}

CCreatureController::__async_arg_Skill::__async_arg_Skill(SkillUseInfo a1, bool a2, bool a3, unsigned int a4) :
    __a1(a1), __a2(a2), __a3(a3), __a4(a4) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_4(CCreatureController, __async_arg_Skill)

CCreatureController::__async_arg_Talk::__async_arg_Talk(__async_arg_Talk* other) : __a1(other->__a1) {}

CCreatureController::__async_arg_Talk::__async_arg_Talk() {}

CCreatureController::__async_arg_Talk::__async_arg_Talk(CCreature* a1) : __a1(a1) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CCreatureController, __async_arg_Talk)

CCreatureController::__async_arg_MoveRelatively::__async_arg_MoveRelatively(__async_arg_MoveRelatively* other) :
    __a1(other->__a1), __a2(other->__a2) {}

CCreatureController::__async_arg_MoveRelatively::__async_arg_MoveRelatively() {}

CCreatureController::__async_arg_MoveRelatively::__async_arg_MoveRelatively(CAbstractCarrier* a1, FVector a2) :
    __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CCreatureController, __async_arg_MoveRelatively)

CCreatureController::__async_arg_PrivateStore::__async_arg_PrivateStore(__async_arg_PrivateStore* other) :
    __a1(other->__a1) {}

CCreatureController::__async_arg_PrivateStore::__async_arg_PrivateStore() {}

CCreatureController::__async_arg_PrivateStore::__async_arg_PrivateStore(int a1) : __a1(a1) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CCreatureController, __async_arg_PrivateStore)

CCreatureController::__async_arg_OnStopMove::__async_arg_OnStopMove() {}

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(CCreatureController, __async_arg_OnStopMove)

CCreatureController::__async_arg_LostTarget::__async_arg_LostTarget(__async_arg_LostTarget* other) :
    __a1(other->__a1), __a2(other->__a2) {}

CCreatureController::__async_arg_LostTarget::__async_arg_LostTarget() {}

CCreatureController::__async_arg_LostTarget::__async_arg_LostTarget(int a1, bool a2) : __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CCreatureController, __async_arg_LostTarget)

CCreatureController::__async_arg_Disappear::__async_arg_Disappear() {}

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(CCreatureController, __async_arg_Disappear)

CCreatureController::__async_arg_TargetOutOfSight::__async_arg_TargetOutOfSight(__async_arg_TargetOutOfSight* other) :
    __a1(other->__a1) {}

CCreatureController::__async_arg_TargetOutOfSight::__async_arg_TargetOutOfSight() {}

CCreatureController::__async_arg_TargetOutOfSight::__async_arg_TargetOutOfSight(int a1) : __a1(a1) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CCreatureController, __async_arg_TargetOutOfSight)

CCreatureController::__async_arg_OnDamagedMessage::__async_arg_OnDamagedMessage(__async_arg_OnDamagedMessage* other) :
    __a1(other->__a1), __a2(other->__a2) {}

CCreatureController::__async_arg_OnDamagedMessage::__async_arg_OnDamagedMessage() {}

CCreatureController::__async_arg_OnDamagedMessage::__async_arg_OnDamagedMessage(CSmartID a1, int a2) : __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CCreatureController, __async_arg_OnDamagedMessage)

CCreatureController::__async_arg_CancelReservedAction::__async_arg_CancelReservedAction() {}

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(CCreatureController, __async_arg_CancelReservedAction)

CCreatureController::__async_arg_DoAutoFishing::__async_arg_DoAutoFishing(__async_arg_DoAutoFishing* other) :
    __a1(other->__a1) {}

CCreatureController::__async_arg_DoAutoFishing::__async_arg_DoAutoFishing() {}

CCreatureController::__async_arg_DoAutoFishing::__async_arg_DoAutoFishing(bool a1) : __a1(a1) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CCreatureController, __async_arg_DoAutoFishing)

CCreatureController::__async_arg_AsyncTimerExpired::__async_arg_AsyncTimerExpired(__async_arg_AsyncTimerExpired* other) :
    __a1(other->__a1), __a2(other->__a2), __a3(other->__a3) {}

CCreatureController::__async_arg_AsyncTimerExpired::__async_arg_AsyncTimerExpired() {}

CCreatureController::__async_arg_AsyncTimerExpired::__async_arg_AsyncTimerExpired(int a1, int a2, int a3) :
    __a1(a1), __a2(a2), __a3(a3) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_3(CCreatureController, __async_arg_AsyncTimerExpired)

CCreatureController::__async_arg_Sit::__async_arg_Sit(__async_arg_Sit* other) : __a1(other->__a1), __a2(other->__a2) {}

CCreatureController::__async_arg_Sit::__async_arg_Sit() {}

CCreatureController::__async_arg_Sit::__async_arg_Sit(int a1, CObject* a2) : __a1(a1), __a2(a2) {}

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CCreatureController, __async_arg_Sit)

CCreatureController::ActionData::ActionData() : m_ActionType(ACT_NOTHING) {}

CCreatureController::ActionData& CCreatureController::ActionData::operator=(const ActionData& other) {
    m_ActionType = other.m_ActionType;
    switch (m_ActionType) {
        case ACT_NOTHING:
            m_Nothing = other.m_Nothing;
            break;
        case ACT_STAND:
            m_Stand = other.m_Stand;
            break;
        case ACT_SIT:
            m_Sit = other.m_Sit;
            break;
        case ACT_MOVE:
            m_Move = other.m_Move;
            break;
        case ACT_ATTACK:
            m_Attack = other.m_Attack;
            break;
        case ACT_SKILL:
            m_spPostTarget = other.m_spPostTarget;
            m_Skill[0] = other.m_Skill[0];
            m_Skill[1] = other.m_Skill[1];
            break;
        case ACT_TALK:
            m_Talk = other.m_Talk;
            break;
        case ACT_GENERAL:
            m_General = other.m_General;
            break;
        case ACT_GETITEM:
            m_GetItem = other.m_GetItem;
            break;
        case ACT_OPENCLOSE:
            m_OpenClose = other.m_OpenClose;
            break;
        case ACT_MOVE_TO_TARGET:
            m_MoveToTarget = other.m_MoveToTarget;
            break;
        case ACT_MOVE_TO_TARGET_WAIT:
            m_MoveToTargetWait = other.m_MoveToTargetWait;
            break;
        case ACT_MOVE_RELATIVE:
            m_MoveRelative = other.m_MoveRelative;
            break;
        case ACT_ATTACK_WAIT:
            m_AttackWait = other.m_AttackWait;
            break;
        case ACT_PRIVATE_STORE:
            m_PrivateStore = other.m_PrivateStore;
            break;
        case ACT_MOVE_TO_TARGET_RELATIVE:
            m_MoveToTargetRelative = other.m_MoveToTargetRelative;
            break;
        case ACT_COUPLE:
            m_CoupleAction = other.m_CoupleAction;
            break;
        case ACT_MAX:
            break;
    }
    return *this;
}

CSkillAction2& CCreatureController::ActionData::GetSkillAction(int& index, bool flag, int requestedIndex) {
    L2_GUARD_FUNCTION();

    index = 0;
    if (requestedIndex == 0 || requestedIndex == 1) {
        return m_Skill[requestedIndex];
    }

    CSkillAction2& first = m_Skill[0];
    if (!first.IsDoubleCastingAction()) {
        return first;
    }

    const bool useSecond = flag ? first.m_ActionState == ActionState::End && m_Skill[1].m_ActionState != ActionState::End
                                : first.m_ActionState != ActionState::End && m_Skill[1].m_ActionState == ActionState::End;
    if (!useSecond) {
        return first;
    }

    index = 1;
    return m_Skill[1];
}

void CCreatureController::AddTimer2(int delayMilliseconds, std::int64_t actionNumber) {
    L2_GUARD_FUNCTION();

    const int actionIndex = static_cast<int>(actionNumber);
    if (actionIndex >= 0 && actionIndex < 2) {
        ++m_nActionNumber[actionIndex];
        CCreature* const actor = m_spActor.GetCreature();
        if (runtime::runtime_timing::Matches(actor)) {
            runtime::runtime_timing::Record(
                "action_timer_schedule", actor, nullptr, "slot=%d generation=%d delay_ms=%d packed=%lld", actionIndex,
                m_nActionNumber[actionIndex], delayMilliseconds, static_cast<long long>(actionNumber)
            );
        }
        CIOObject::AddTimer2(static_cast<std::int64_t>(delayMilliseconds), m_nActionNumber[actionIndex], actionNumber);
    }
}

ActionType CCreatureController::GetCurrentActionType() { return m_Action[0].m_ActionType; }

ActionType CCreatureController::GetNextActionType() { return m_Action[1].m_ActionType; }

int CCreatureController::GetCurrentActionSkillId() {
    int index;
    return m_Action[0].GetSkillAction(index, true, -1).GetSkillId();
}

int CCreatureController::GetCurrentActionSkillLevel() {
    int index;
    return m_Action[0].GetSkillAction(index, true, -1).GetSkillLevel();
}

int CCreatureController::GetCurrentActionSkillSubLevel() {
    int index;
    return m_Action[0].GetSkillAction(index, true, -1).GetSkillSubLevel();
}

int CCreatureController::GetCurrentActionSkillMagicType() {
    int index;
    return m_Action[0].GetSkillAction(index, true, -1).GetMagicType();
}

CNewAction* CCreatureController::GetCurrentAction(int skillIndex) { return GetAction(0, skillIndex); }

CNewAction* CCreatureController::GetPendingAction() { return GetAction(1, -1); }

CNewAction* CCreatureController::GetAction(int actionNumber, int skillIndex) {
    return GetAction(m_Action[actionNumber].m_ActionType, actionNumber, skillIndex);
}

CNewAction* CCreatureController::GetAction(ActionType actionType, int actionNumber, int skillIndex) {
    ActionData& action = m_Action[actionNumber];
    switch (actionType) {
        case ACT_NOTHING:
            return &action.m_Nothing;
        case ACT_STAND:
            return &action.m_Stand;
        case ACT_SIT:
            return &action.m_Sit;
        case ACT_MOVE:
            return &action.m_Move;
        case ACT_ATTACK:
            return &action.m_Attack;
        case ACT_SKILL: {
            int index;
            return &action.GetSkillAction(index, true, skillIndex);
        }
        case ACT_TALK:
            return &action.m_Talk;
        case ACT_GENERAL:
            return &action.m_General;
        case ACT_GETITEM:
            return &action.m_GetItem;
        case ACT_OPENCLOSE:
            return &action.m_OpenClose;
        case ACT_MOVE_TO_TARGET:
            return &action.m_MoveToTarget;
        case ACT_MOVE_TO_TARGET_WAIT:
            return &action.m_MoveToTargetWait;
        case ACT_MOVE_RELATIVE:
            return &action.m_MoveRelative;
        case ACT_ATTACK_WAIT:
            return &action.m_AttackWait;
        case ACT_PRIVATE_STORE:
            return &action.m_PrivateStore;
        case ACT_MOVE_TO_TARGET_RELATIVE:
            return &action.m_MoveToTargetRelative;
        case ACT_COUPLE:
            return &action.m_CoupleAction;
        case ACT_MAX:
            break;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%d][%d] Invalid GetAction",
    //     actionNumber,
    //     static_cast<int>(actionType))
    // L2_LOG_ERROR(Log,
    //     L"[%d][%d] Invalid GetAction",
    //     actionNumber,
    //     static_cast<int>(actionType))
    L2_LOG_ERROR(
        Log,
        "Creature action lookup failed: action type %d is unsupported (action slot %d, skill index %d); returning no "
        "action.",
        static_cast<int>(actionType), actionNumber, skillIndex
    );
    return nullptr;
}

void CCreatureController::EndCurrentAction(bool success) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return;
    }

    if (actor->m_nLaunchedSkills.load() == 2) {
        GetCurrentAction(-1)->OnEnd(success);
    }
    GetCurrentAction(-1)->OnEnd(success);
}

void CCreatureController::SetJustUnfrozen() {
    L2_GUARD_FUNCTION();

    const ActionType actionType = m_Action[0].m_ActionType;
    if (actionType != ACT_NOTHING && actionType != ACT_MOVE &&
        (actionType < ACT_MOVE_TO_TARGET || (actionType > ACT_ATTACK_WAIT && actionType != ACT_MOVE_TO_TARGET_RELATIVE))) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Abnormal Frozen Action [%d]",
        //     static_cast<int>(actionType))
        // L2_LOG_ERROR(Log,
        //     L"Abnormal Frozen Action [%d]",
        //     static_cast<int>(actionType))
        L2_LOG_WARN(
            Log,
            "Creature unfreeze found unexpected action type %d; ending the current action as failed and resetting both "
            "action slots.",
            static_cast<int>(actionType)
        );
    }

    EndCurrentAction(false);
    SetAsNothing(0);
    SetAsNothing(1);
}

bool CCreatureController::IsActorAndTargetFrozen(CCreature* actor) {
    if (actor->IsFrozen(true)) {
        SetAsNothing(0);
        SetAsNothing(1);
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        if (actor->IsPC()) {
            User* const user = actor->CastUser();
            CUserSocket* const socket = user->GetSocket();
            if (socket != nullptr) {
                socket->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_FROZEN);
            }
        }
        return true;
    }

    CCreature* const target = actor->m_spTarget.GetCreature();
    if (target == nullptr || !target->IsFrozen(false)) {
        return false;
    }

    SetAsNothing(0);
    SetAsNothing(1);
    // actor->ActionFailed(0)
    actor->ActionFailed(AF_DEFAULT);
    if (actor->IsPC()) {
        User* const user = actor->CastUser();
        CUserSocket* const socket = user->GetSocket();
        if (socket != nullptr) {
            socket->SendSystemMessage(SystemMessageId::TARGET_FROZEN);
        }
    }
    return true;
}

void CCreatureController::DoPendingAction(int actionNumber, bool flag) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || IsActorAndTargetFrozen(actor)) {
        return;
    }

    if (runtime::runtime_timing::Matches(actor)) {
        runtime::runtime_timing::Record(
            "action_pending_begin", actor, nullptr, "depth=%d flag=%d current_action=%d next_action=%d", actionNumber,
            flag ? 1 : 0, static_cast<int>(m_Action[0].m_ActionType), static_cast<int>(m_Action[1].m_ActionType)
        );
    }

    EndCurrentAction(!flag);
    m_Action[0] = m_Action[1];
    actor->m_data->m_Action = m_Action[0].m_ActionType;
    SetAsNothing(1);

    const bool started = GetCurrentAction(-1)->OnStart();
    if (runtime::runtime_timing::Matches(actor)) {
        runtime::runtime_timing::Record(
            "action_pending_started", actor, nullptr, "depth=%d started=%d current_action=%d next_action=%d", actionNumber,
            started ? 1 : 0, static_cast<int>(m_Action[0].m_ActionType), static_cast<int>(m_Action[1].m_ActionType)
        );
    }
    if (!started) {
        if (actionNumber > 1) {
            SetAsNothing(1);
        }
        DoPendingAction(runtime::AddInt32Wrapping(actionNumber, 1), false);
    }
}

void CCreatureController::DoFirstAction() {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive || IsActorAndTargetFrozen(actor)) {
        return;
    }

    User* const user = actor->IsPC() ? actor->CastUser() : nullptr;
    if (user != nullptr &&
        (user->m_pFishingController->m_FishingState != FISHINGS_NONE ||
            user->m_pFishingController->m_bIsAutoFishing == true)) {
        if (m_Action[0].m_ActionType != ACT_NOTHING) {
            CUserSocket* const socket = user->GetSocket();
            if (socket != nullptr) {
                socket->SendSystemMessage(SystemMessageId::CANNOT_PERFORM_OTHER_ACTIONS_WHILE_FISHING);
            }
        }
        SetAsNothing(0);
        SetAsNothing(1);
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return;
    }

    runtime::raw_atomic::IncrementI32(actor->IsPC() ? &g_nUserActionStartCount : &g_nNpcActionStartCount);
    actor->m_data->m_Action = m_Action[0].m_ActionType;
    if (!GetCurrentAction(-1)->OnStart()) {
        DoPendingAction(0, false);
    }
}

void CCreatureController::ShowActionChange(int actionNumber, wchar_t* actionName) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || actor->m_ShowActionChangeTarget < 1) {
        return;
    }

    CCreature* const target =
        g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(actor->m_ShowActionChangeTarget));
    User* const user = target != nullptr && target->IsPC() ? target->CastUser() : nullptr;
    if (user == nullptr) {
        return;
    }

    std::array<char16_t, 1024> message;
    constexpr const char16_t* format = u"action[%s] [%d][%s]";
    FormatUtf16To(message.data(), message.size(), format, actor->GetName(), actionNumber, actionName);
    user->GetSocket()->SendSystemMessage(u"SYS", message.data());
}

void CCreatureController::SetAsNothing(int actionNumber) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_NOTHING;
    action.m_Nothing.Set(m_spActor);
    action.m_spPostTarget = static_cast<CObject*>(nullptr);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_NOTHING"));
}

void CCreatureController::SetAsStand(int actionNumber, int delayTime) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_STAND;
    action.m_Stand.Set(m_spActor, delayTime);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_STAND"));
}

void CCreatureController::SetAsSit(int actionNumber, int delayTime, CSmartID target, bool sitType) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_SIT;
    action.m_Sit.Set(m_spActor, delayTime, target, sitType);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_SIT"));
}

void CCreatureController::SetAsMove(
    int actionNumber,
    FVector destination,
    bool pathFind,
    bool forceAttack,
    int skillId,
    int skillLevel,
    int skillSubLevel,
    int usedItemSID
) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_MOVE;
    action.m_Move.Set(m_spActor, destination, pathFind, forceAttack, skillId, skillLevel, skillSubLevel, usedItemSID);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_MOVE"));
}

void CCreatureController::SetAsMoveToTarget(
    int actionNumber,
    CSmartID target,
    bool forceAttack,
    int skillId,
    int skillLevel,
    int skillSubLevel,
    int skillRange,
    int usedItemSID
) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_MOVE_TO_TARGET;
    action.m_MoveToTarget.Set(m_spActor, target, forceAttack, skillId, skillLevel, skillSubLevel, skillRange, usedItemSID);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_MOVE_TO_TARGET"));
}

void CCreatureController::SetAsMoveToTarget(
    int actionNumber, CSmartID target, bool forceAttack, const SkillUseInfo& skillUseInfo, int usedItemSID
) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_MOVE_TO_TARGET;
    action.m_MoveToTarget.Set(m_spActor, target, forceAttack, skillUseInfo, usedItemSID);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_MOVE_TO_TARGET"));
}

void CCreatureController::SetAsMoveToTargetRelative(
    int actionNumber,
    CSmartID target,
    bool forceAttack,
    int skillId,
    int skillLevel,
    int skillSubLevel,
    int skillRange,
    int usedItemSID,
    CSmartID airShip
) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_MOVE_TO_TARGET_RELATIVE;
    action.m_MoveToTargetRelative.Set(
        m_spActor, target, forceAttack, skillId, skillLevel, skillSubLevel, skillRange, usedItemSID, airShip
    );
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_MOVE_TO_TARGET_RELATIVE"));
}

void CCreatureController::SetAsMoveToTargetWait(int actionNumber, CSmartID target, int acceptableDistance) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_MOVE_TO_TARGET_WAIT;
    action.m_MoveToTargetWait.Set(m_spActor, target, acceptableDistance);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_MOVE_TO_TARGET_WAIT"));
}

void CCreatureController::SetAsSkill(
    int actionNumber,
    CSmartID target,
    int skillId,
    int skillLevel,
    int skillSubLevel,
    bool forceAttack,
    bool flag,
    unsigned int skillIndex
) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_SKILL;
    action.m_spPostTarget = target;
    int selectedIndex = 0;
    CSkillAction2& skillAction = action.GetSkillAction(selectedIndex, true, -1);
    skillAction.Set(m_spActor, target, skillId, skillLevel, skillSubLevel, forceAttack, flag, skillIndex, 0);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_SKILL"));
}

void CCreatureController::SetAsSkill(
    int actionNumber, CSmartID target, const SkillUseInfo& skillUseInfo, bool forceAttack, bool flag, unsigned int skillIndex
) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_SKILL;
    action.m_spPostTarget = target;
    int selectedIndex = 0;
    CSkillAction2& skillAction = action.GetSkillAction(selectedIndex, true, -1);
    skillAction.Set(m_spActor, target, skillUseInfo, forceAttack, flag, skillIndex, 0);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_SKILL"));
}

void CCreatureController::SetAsAttack(int actionNumber, CSmartID target, bool surely) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_ATTACK;
    action.m_Attack.Set(m_spActor, target, surely);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_ATTACK"));
}

void CCreatureController::SetAsTalk(int actionNumber, CSmartID target) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_TALK;
    action.m_Talk.Set(m_spActor, target);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_TALK"));
}

void CCreatureController::SetAsCouple(int actionNumber, int delayTime, CSmartID target) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_COUPLE;
    action.m_CoupleAction.Set(m_spActor, target, delayTime);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_COUPLE"));
}

void CCreatureController::SetAsGeneral(int actionNumber, CSmartID target, CGeneralAction::GeneralActionType actionType) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_GENERAL;
    action.m_General.Set(m_spActor, target, actionType);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_GENERAL"));
}

void CCreatureController::SetAsGetItem(int actionNumber, CSmartID target) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_GETITEM;
    action.m_GetItem.Set(m_spActor, target);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_GETITEM"));
}

void CCreatureController::SetAsOpenClose(int actionNumber, CSmartID target) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_OPENCLOSE;
    action.m_OpenClose.Set(m_spActor, target);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_OPENCLOSE"));
}

void CCreatureController::SetAsMoveRelative(int actionNumber, CAbstractCarrier* carrier, FVector relativePosition) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_MOVE_RELATIVE;
    action.m_MoveRelative.Set(m_spActor, carrier->GetSmartID(), relativePosition);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_MOVE_RELATIVE"));
}

void CCreatureController::SetAsAttackWait(int actionNumber, CSmartID target, int delay, bool surely) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_ATTACK_WAIT;
    action.m_AttackWait.Set(m_spActor, target, delay, surely);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_ATTACK_WAIT"));
}

void CCreatureController::SetAsPrivateStore(int actionNumber, int mode) {
    ActionData& action = m_Action[actionNumber];
    action.m_ActionType = ACT_PRIVATE_STORE;
    action.m_PrivateStore.Set(m_spActor, mode);
    ShowActionChange(actionNumber, const_cast<wchar_t*>(L"ACT_PRIVATE_STORE"));
}

void CCreatureController::__async_CancelReservedAction(CExecutionArgumentPad*) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor != nullptr && actor->m_data->m_bIsActive) {
        SetAsNothing(1);
    }
}

void CCreatureController::__async_DoNothing(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive) {
        return;
    }

    SetAsNothing(1);
    EndCurrentAction(false);
    SetAsNothing(0);
    SetAsNothing(1);
    DoFirstAction();
    // actor->CastCreature()->ActionFailed(0)
    actor->CastCreature()->ActionFailed(AF_DEFAULT);

    bool broadcastStopMove;
    std::memcpy(&broadcastStopMove, argument->Buffer + 0x08, sizeof(broadcastStopMove));
    if (broadcastStopMove) {
        actor->GetController()->ActionBroadcastStopMove();
    }
}

void CCreatureController::__async_OnStopMove(CExecutionArgumentPad*) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive) {
        return;
    }

    if (!GetCurrentAction(-1)->OnStopMove()) {
        CCreature* const target = actor->m_spTarget.GetCreature();
        if (target != nullptr && target->IsFrozen(false)) {
            // actor->ActionFailed(0)
            actor->ActionFailed(AF_DEFAULT);
            return;
        }

        EndCurrentAction(false);
        m_nActionNumber[0] += 100;
        m_nActionNumber[1] += 100;
        DoPendingAction(0, false);
    }
    // actor->ActionFailed(1)
    actor->ActionFailed(AF_MOVE);
}

void CCreatureController::__async_OnDamagedMessage(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive) {
        return;
    }

    CSmartID attacker;
    int damage;
    std::memcpy(&attacker, argument->Buffer + 0x08, sizeof(attacker));
    std::memcpy(&damage, argument->Buffer + 0x0c, sizeof(damage));
    if (!GetCurrentAction(-1)->OnDamaged(attacker, damage)) {
        EndCurrentAction(false);
        m_nActionNumber[0] += 100;
        m_nActionNumber[1] += 100;
        DoPendingAction(0, false);
    }
}

void CCreatureController::__async_LostTarget(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive) {
        return;
    }

    int targetId;
    bool flag;
    std::memcpy(&targetId, argument->Buffer + 0x08, sizeof(targetId));
    std::memcpy(&flag, argument->Buffer + 0x0c, sizeof(flag));
    const bool retained = GetCurrentAction(-1)->OnLostTarget(targetId, flag);
    if (runtime::runtime_timing::Matches(actor)) {
        runtime::runtime_timing::Record(
            "action_lost_target", actor, nullptr,
            "target_sid=%d flag=%d retained=%d current_action=%d "
            "next_action=%d selected_sid=%u action_target_sid=%u",
            targetId, flag ? 1 : 0, retained ? 1 : 0, static_cast<int>(m_Action[0].m_ActionType),
            static_cast<int>(m_Action[1].m_ActionType), actor->m_spTarget.Sid(), actor->m_spActionTarget.Sid()
        );
    }
    if (!retained) {
        DoPendingAction(0, true);
    }
}

void CCreatureController::__async_Disappear(CExecutionArgumentPad*) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor != nullptr && actor->m_data->m_bIsActive && !GetCurrentAction(-1)->OnDisappear()) {
        DoPendingAction(0, false);
    }
}

void CCreatureController::__async_TargetOutOfSight(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive) {
        return;
    }

    int distance;
    std::memcpy(&distance, argument->Buffer + 0x08, sizeof(distance));
    if (!GetCurrentAction(-1)->OnTargetOutOfSight(distance)) {
        DoPendingAction(0, false);
    }
}

void CCreatureController::__async_Stand(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive) {
        return;
    }

    if (!actor->m_SkillMod.m_bCanAct) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return;
    }

    int delayTime;
    std::memcpy(&delayTime, argument->Buffer + 0x08, sizeof(delayTime));
    const int transition = g_ActionTable[m_Action[0].m_ActionType][ACT_STAND];
    if (transition == 1) {
        actor->InitCoupleAction();
        EndCurrentAction(false);
        SetAsStand(0, delayTime);
        SetAsNothing(1);
        DoFirstAction();
    } else {
        if (transition == 2) {
            SetAsStand(1, delayTime);
        }
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
    }
}

void CCreatureController::__async_PrivateStore(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00636fe8.
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive) {
        return;
    }
    if (!actor->m_SkillMod.m_bCanAct) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return;
    }

    int mode = 0;
    std::memcpy(&mode, argument->Buffer + 0x08, sizeof(mode));
    const int transition = g_ActionTable[m_Action[0].m_ActionType][ACT_PRIVATE_STORE];
    if (transition == 0) {
        if (actor->IsPC()) {
            actor->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
                actor->m_data->m_nPrivateStoreType, PST_NONE, 0x1a
            );
            actor->CastUser()->GetSocket()->SendSystemMessage(
                SystemMessageId::CANNOT_OPEN_PRIVATE_STORE_OR_WORKSHOP_WHILE_USING_SKILL
            );
        }
    } else if (transition == 1) {
        actor->InitCoupleAction();
        EndCurrentAction(false);
        SetAsPrivateStore(0, mode);
        SetAsNothing(1);
        DoFirstAction();
        return;
    } else if (transition == 2) {
        SetAsPrivateStore(1, mode);
    }
    // actor->ActionFailed(0)
    actor->ActionFailed(AF_DEFAULT);
}

void CCreatureController::__async_Sit(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive) {
        return;
    }

    if (!actor->m_SkillMod.m_bCanAct) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return;
    }

    if (actor->IsPC()) {
        actor->CastUser()->CheckTutorialEvent(TCE_SIT_DOWN);
    }

    int delayTime;
    CObject* target;
    std::memcpy(&delayTime, argument->Buffer + 0x08, sizeof(delayTime));
    std::memcpy(&target, argument->Buffer + 0x10, sizeof(target));

    const int transition = g_ActionTable[m_Action[0].m_ActionType][ACT_SIT];
    if (transition == 1) {
        actor->InitCoupleAction();
        EndCurrentAction(false);
        SetAsSit(0, delayTime, CSmartID(target), false);
        SetAsNothing(1);
        DoFirstAction();
    } else {
        if (transition == 2) {
            SetAsSit(1, delayTime, CSmartID(target), false);
        }
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
    }
}

void CCreatureController::__async_Attack(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive) {
        return;
    }

    if (!actor->m_SkillMod.m_bCanAct || actor->m_data->m_StopMode == 0 || !actor->m_SkillMod.m_bCanAttack ||
        actor->m_ItemMod.m_bNoAttackWeapon) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return;
    }

    if (actor->GetMorphID() != 0) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return;
    }

    CObject* target;
    bool surely;
    std::memcpy(&target, argument->Buffer + 0x08, sizeof(target));
    std::memcpy(&surely, argument->Buffer + 0x10, sizeof(surely));

    const int transition = g_ActionTable[m_Action[0].m_ActionType][ACT_ATTACK];
    if (runtime::runtime_timing::Matches(actor, target)) {
        runtime::runtime_timing::Record(
            "attack_serializer_dispatch", actor, target, "surely=%d current_action=%d next_action=%d transition=%d",
            surely ? 1 : 0, static_cast<int>(m_Action[0].m_ActionType), static_cast<int>(m_Action[1].m_ActionType),
            transition
        );
    }
    if (transition == 1) {
        actor->InitCoupleAction();
        EndCurrentAction(false);
        SetAsAttack(0, target->GetSmartID(), surely);
        SetAsNothing(1);
        DoFirstAction();
        return;
    }

    if (transition == 2) {
        SetAsAttack(1, target->GetSmartID(), surely);
    }
    // actor->ActionFailed(0)
    actor->ActionFailed(AF_DEFAULT);
}

void CCreatureController::__async_Talk(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00636bf0 keeps this shortened
    // historical signature instead of the compiled async callback name.
    static constexpr wchar_t __FUNC_NAME__[] = L"void CCreatureController::Talk()";
    L2_GUARD_PROFILE(__FUNC_NAME__);
    GuardHelper __stack_var__(__FUNC_NAME__, CThreadManager::GetThreadID());

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive || actor->m_data->m_StopMode == 0) {
        return;
    }

    if (!actor->m_SkillMod.m_bCanAct) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return;
    }

    CCreature* target;
    std::memcpy(&target, argument->Buffer + 0x08, sizeof(target));
    const int transition = g_ActionTable[m_Action[0].m_ActionType][ACT_TALK];
    if (transition == 1) {
        actor->InitCoupleAction();
        EndCurrentAction(false);
        SetAsTalk(0, target->GetSmartID());
        SetAsNothing(1);
        DoFirstAction();
    } else if (transition == 2) {
        SetAsTalk(1, target->GetSmartID());
    }
    // actor->CastCreature()->ActionFailed(0)
    actor->CastCreature()->ActionFailed(AF_DEFAULT);
}

void CCreatureController::__async_MoveRelatively(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive) {
        return;
    }
    if (!actor->m_SkillMod.m_bCanAct || actor->m_SkillMod.m_nBlockMoveCount != 0 || !actor->m_data->m_bCanMove ||
        actor->IsFrozen(false)) {
        // actor->ActionFailed(0)
        actor->ActionFailed(AF_DEFAULT);
        return;
    }

    CAbstractCarrier* carrier;
    FVector relativePosition;
    std::memcpy(&carrier, argument->Buffer + 0x08, sizeof(carrier));
    std::memcpy(&relativePosition, argument->Buffer + 0x10, sizeof(relativePosition));

    const int transition = g_ActionTable[m_Action[0].m_ActionType][ACT_MOVE_RELATIVE];
    if (transition == 1) {
        actor->InitCoupleAction();
        EndCurrentAction(false);
        SetAsMoveRelative(0, carrier, relativePosition);
        SetAsNothing(1);
        DoFirstAction();
        return;
    }
    if (transition == 2) {
        SetAsMoveRelative(1, carrier, relativePosition);
    }
    // actor->CastCreature()->ActionFailed(0)
    actor->CastCreature()->ActionFailed(AF_DEFAULT);
}

void CCreatureController::CancelReservedAction() {
    __async_arg_CancelReservedAction argument;
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_CancelReservedAction(pad); },
        &argument, L"CancelReservedAction"
    );
}

void CCreatureController::OnStopMove() {
    __async_arg_OnStopMove argument;
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_OnStopMove(pad); }, &argument,
        L"OnStopMove"
    );
}

void CCreatureController::OnDamagedMessage(CSmartID attacker, int damage) {
    __async_arg_OnDamagedMessage argument(attacker, damage);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_OnDamagedMessage(pad); },
        &argument, L"OnDamagedMessage"
    );
}

void CCreatureController::OnDamaged(CSmartID attacker, int damage) {
    L2_GUARD_FUNCTION();

    CCreature* const victim = m_spActor.GetCreature();
    CCreature* const attackerCreature = attacker.GetCreature();
    if (victim != nullptr && attackerCreature != nullptr) {
        victim->PushSerializedMessage(new CSerializedDamageMessage(attackerCreature, victim, damage));
    }
}

void CCreatureController::LostTarget(int targetId, bool flag) {
    __async_arg_LostTarget argument(targetId, flag);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_LostTarget(pad); }, &argument,
        L"LostTarget"
    );
}

void CCreatureController::Disappear() {
    __async_arg_Disappear argument;
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_Disappear(pad); }, &argument,
        L"Disappear"
    );
}

void CCreatureController::TargetOutOfSight(int distance) {
    __async_arg_TargetOutOfSight argument(distance);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_TargetOutOfSight(pad); },
        &argument, L"TargetOutOfSight"
    );
}

void CCreatureController::DoAutoFishing(const bool start) {
    __async_arg_DoAutoFishing argument(start);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_DoAutoFishing(pad); }, &argument,
        L"DoAutoFishing"
    );
}

void CCreatureController::__async_DoAutoFishing(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00631708.
    L2_GUARD_FUNCTION();
    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr || !actor->m_data->m_bIsActive) {
        return;
    }
    User* const user = actor->CastUser();
    if (user == nullptr) {
        return;
    }

    bool start = false;
    std::memcpy(&start, argument->Buffer + 0x08, sizeof(start));
    if (actor->m_data->m_StopMode == STP_SIT) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_SITTING);
        return;
    }
    if (actor->m_data->m_StopMode == STP_STORE || user->GetPrivateStoreType() != 0) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_ACT_DURING_PRIVATE_STORE_OR_WORKSHOP);
        return;
    }
    if (actor->m_nDead != 0 || !actor->m_data->m_bCanMove || !actor->m_SkillMod.m_bCanAct ||
        m_Action[0].m_ActionType != ACT_NOTHING || user->m_pFishingController == nullptr) {
        return;
    }
    user->m_pFishingController->DoAutoFishing(start);
}

void CCreatureController::Stand(int delayTime) {
    __async_arg_Stand argument(delayTime);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_Stand(pad); }, &argument, L"Stand"
    );
}

void CCreatureController::DoNothing(bool success) {
    __async_arg_DoNothing argument(success);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_DoNothing(pad); }, &argument,
        L"DoNothing"
    );
}

void CCreatureController::Sit(int delayTime, CObject* target) {
    __async_arg_Sit argument(delayTime, target);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_Sit(pad); }, &argument, L"Sit"
    );
}

void CCreatureController::Talk(CCreature* target) {
    __async_arg_Talk argument(target);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_Talk(pad); }, &argument, L"Talk"
    );
}

void CCreatureController::PrivateStore(const int mode) {
    __async_arg_PrivateStore argument(mode);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_PrivateStore(pad); }, &argument,
        L"PrivateStore"
    );
}

void CCreatureController::AsyncTimerExpired2(int id, int actionNumber, int) {
    L2_GUARD_FUNCTION();

    if (static_cast<unsigned int>(actionNumber) >= 2) {
        return;
    }

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return;
    }

    CNewAction* const action = GetCurrentAction(actionNumber);
    if (runtime::runtime_timing::Matches(actor)) {
        runtime::runtime_timing::Record(
            "action_timer_dispatch", actor, nullptr,
            "slot=%d received_generation=%d current_generation=%d "
            "current_action=%d next_action=%d",
            actionNumber, id, m_nActionNumber[actionNumber], static_cast<int>(m_Action[0].m_ActionType),
            static_cast<int>(m_Action[1].m_ActionType)
        );
    }
    if (m_nActionNumber[actionNumber] != id || actor->IsDeleting() || actor->m_Deleted != 0) {
        return;
    }

    if (!actor->m_data->m_bIsActive) {
        EndCurrentAction(false);
        SetAsNothing(0);
        return;
    }

    const int threadId = CThreadManager::GetThreadID();
    g_nSkillActorDBID[threadId] = static_cast<int>(actor->GetDBID());
    if (!action->OnTick()) {
        actor->HandleSerializedMessages();
        DoPendingAction(0, false);
    }
}

void CCreatureController::__async_AsyncTimerExpired(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();

    int id;
    int actionNumber;
    int actionSequence;
    std::memcpy(&id, argument->Buffer + 0x08, sizeof(id));
    std::memcpy(&actionNumber, argument->Buffer + 0x0c, sizeof(actionNumber));
    std::memcpy(&actionSequence, argument->Buffer + 0x10, sizeof(actionSequence));
    AsyncTimerExpired2(id, actionNumber, actionSequence);
}

void CCreatureController::TimerExpired(int id) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return;
    }

    runtime::raw_atomic::IncrementI32(actor->IsPC() ? &g_nUserActionTickCount : &g_nNpcActionTickCount);
    __async_arg_AsyncTimerExpired argument(id, 0, 0);
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncTimerExpired(pad); },
        &argument, L"AsyncTimerExpired"
    );
}

void CCreatureController::TimerExpired2(int id, std::int64_t uid) {
    L2_GUARD_FUNCTION();

    CCreature* const actor = m_spActor.GetCreature();
    if (actor == nullptr) {
        return;
    }

    runtime::raw_atomic::IncrementI32(actor->IsPC() ? &g_nUserActionTickCount : &g_nNpcActionTickCount);
    const std::uint64_t uidBits = static_cast<std::uint64_t>(uid);
    __async_arg_AsyncTimerExpired argument(id, static_cast<int>(uidBits), static_cast<int>(uidBits >> 32));
    Serializer.AddExecutable(
        this, [](CCreatureController* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncTimerExpired(pad); },
        &argument, L"AsyncTimerExpired"
    );
}
