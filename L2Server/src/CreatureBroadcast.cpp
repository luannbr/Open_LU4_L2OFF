#include "CConfig.h"
#include "Creature.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/Utils.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include <cstdint>
#include <cstring>

namespace {


struct DamageMessageArgument {
    bool isName;
    const char16_t* name;
    int classId;
};

DamageMessageArgument MakeDamageMessageArgument(CCreature* actor, User* viewer, const bool self) {
    if (actor->IsPC()) {
        return DamageMessageArgument{true, self ? actor->GetName() : actor->CastUser()->GetName2(viewer), 0};
    }
    return DamageMessageArgument{false, nullptr, actor->GetClassId()};
}

void SendDamageSystemMessage(
    User* receiver,
    const int messageId,
    const DamageMessageArgument& first,
    const DamageMessageArgument& second,
    const int damage,
    const int shieldDefence,
    CCreature* damaged,
    CCreature* attacker,
    const int appliedDifference
) {
    CUserSocket* const socket = receiver->GetSocket();
    const int damagedId = runtime::Int32FromBits(damaged->m_nId);
    const int attackerId = runtime::Int32FromBits(attacker->m_nId);

    if (shieldDefence <= 0) {
        if (first.isName && second.isName) {
            socket->Send(
                "chccScScdcddd", 0x62, messageId, 4, 0xc, first.name, 0xc, second.name, 1, damage, 0x10, damagedId,
                attackerId, appliedDifference
            );
        } else if (first.isName) {
            socket->Send(
                "chccScdcdcddd", 0x62, messageId, 4, 0xc, first.name, 2, second.classId, 1, damage, 0x10, damagedId,
                attackerId, appliedDifference
            );
        } else if (second.isName) {
            socket->Send(
                "chccdcScdcddd", 0x62, messageId, 4, 2, first.classId, 0xc, second.name, 1, damage, 0x10, damagedId,
                attackerId, appliedDifference
            );
        } else {
            socket->Send(
                "chccdcdcdcddd", 0x62, messageId, 4, 2, first.classId, 2, second.classId, 1, damage, 0x10, damagedId,
                attackerId, appliedDifference
            );
        }
        return;
    }

    if (first.isName && second.isName) {
        socket->Send(
            "chccScScdcdcddd", 0x62, messageId, 5, 0xc, first.name, 0xc, second.name, 1, damage, 1, shieldDefence, 0x10,
            damagedId, attackerId, appliedDifference
        );
    } else if (first.isName) {
        socket->Send(
            "chccScdcdcdcddd", 0x62, messageId, 5, 0xc, first.name, 2, second.classId, 1, damage, 1, shieldDefence, 0x10,
            damagedId, attackerId, appliedDifference
        );
    } else if (second.isName) {
        socket->Send(
            "chccdcScdcdcddd", 0x62, messageId, 5, 2, first.classId, 0xc, second.name, 1, damage, 1, shieldDefence, 0x10,
            damagedId, attackerId, appliedDifference
        );
    } else {
        socket->Send(
            "chccdcdcdcdcddd", 0x62, messageId, 5, 2, first.classId, 2, second.classId, 1, damage, 1, shieldDefence, 0x10,
            damagedId, attackerId, appliedDifference
        );
    }
}

} // namespace

void CCreature::SendMessageOnDamaged(
    CCreature* damaged,
    CCreature* attacker,
    const int damage,
    const bool sendToAttacker,
    const bool sendToDamaged,
    const int shieldDefence,
    const int appliedDifference
) {
    // Current L2Server 0x005d48c4.
    L2_GUARD_FUNCTION();

    User* damagedReceiver = nullptr;
    if (damaged->IsPC()) {
        damagedReceiver = damaged->CastUser();
    } else if (damaged->IsPCOrPCRelatedNPC()) {
        damagedReceiver = damaged->GetUserOrMaster();
    }

    if (sendToDamaged && damagedReceiver != nullptr) {
        const DamageMessageArgument damagedArgument = MakeDamageMessageArgument(damaged, damagedReceiver, true);
        const DamageMessageArgument attackerArgument = MakeDamageMessageArgument(attacker, damagedReceiver, false);
        SendDamageSystemMessage(
            damagedReceiver, SystemMessageId::TARGET_RECEIVED_DAMAGE_FROM_ATTACKER, damagedArgument, attackerArgument,
            damage, 0, damaged, attacker, -damage
        );
    }

    User* attackerReceiver = nullptr;
    if (attacker->IsPC()) {
        attackerReceiver = attacker->CastUser();
    } else if (attacker->IsPCOrPCRelatedNPC()) {
        attackerReceiver = attacker->GetUserOrMaster();
    }

    if (!sendToAttacker || attackerReceiver == nullptr) {
        return;
    }

    const DamageMessageArgument attackerArgument = MakeDamageMessageArgument(attacker, attackerReceiver, true);
    const DamageMessageArgument damagedArgument = MakeDamageMessageArgument(damaged, attackerReceiver, false);
    SendDamageSystemMessage(
        attackerReceiver,
        shieldDefence > 0 ? SystemMessageId::DAMAGE_DEALT_TO_CHARACTER_AND_TRANSFER_TARGET
                          : SystemMessageId::CHARACTER_DEALT_DAMAGE_TO_CHARACTER,
        attackerArgument, damagedArgument, damage, shieldDefence, damaged, attacker, appliedDifference
    );
}

void CCreature::BroadcastFlyToLocation(
    const int x,
    const int y,
    const int z,
    const FLY_TO_LOCATION_TYPE type,
    const int time,
    const int unknown1,
    const int unknown2
) {
    // Current L2Server 0x005d600c.
    L2_GUARD_FUNCTION();
    const int duration = static_cast<int>(static_cast<double>(nHeartBeat * time) / 1000.0);
    const FVector position = GetPos();
    const int currentX = runtime::Win64CvttDoubleToInt32(position.x);
    const int currentY = runtime::Win64CvttDoubleToInt32(position.y);
    const int currentZ = runtime::Win64CvttDoubleToInt32(position.z);

    if (!IsGMInvisible()) {
        g_ptlUserMap->BroadcastToNeighborDist(
            this, 0xf000, GetPos(), 0x1000, "cddddddddddd", 0xd4, runtime::Int32FromBits(m_nId), x, y, z, currentX,
            currentY, currentZ, static_cast<int>(type), unknown1, unknown2, duration
        );
        return;
    }
    if (IsPC()) {
        CastUser()->GetSocket()->Send(
            "cddddddddddd", 0xd4, runtime::Int32FromBits(m_nId), x, y, z, currentX, currentY, currentZ,
            static_cast<int>(type), unknown1, unknown2, duration
        );
    }
}

// CCreature::BroadcastEmitterSpawn@0x005d6718.
void CCreature::BroadcastEmitterSpawn(const int emitterId, const int creatureId, const int unknown) {
    L2_GUARD_FUNCTION();
    if (!IsGMInvisible()) {
        const int distance = GetBroadcastDist(0x1000);
        const FVector position = GetPos();
        g_ptlUserMap->BroadcastToNeighborDist(
            this, 0xf000, position, distance, "chddd", 0xfe, 0x5e, emitterId, creatureId, unknown
        );
        return;
    }
    if (IsPC()) {
        CastUser()->GetSocket()->Send("chddd", 0xfe, 0x5e, emitterId, creatureId, unknown);
    }
}

void CCreature::BroadcastWaitType(int waitType) {
    L2_GUARD_FUNCTION();

    if (waitType == STP_STAND) {
        m_data->m_bFakeDeathFail = static_cast<double>(m_SkillMod.m_nAvoidAgroProb) <= ::genrand64_real1() * 100.0 + 0.0;
        m_data->m_bIsFakeDeath = true;
        m_dwFakeDeathStartTime = runtime::GetTickCount();
    }

    const FVector position = GetPos();
    if (!IsGMInvisible()) {
        int packetWaitType = 0;
        if (waitType == STP_STAND) {
            // packetWaitType = STP_STORE.
            packetWaitType = WTE_FAKE_DEATH;
        } else if (m_data->m_bIsFakeDeath && m_data->m_StopMode == STP_STAND) {
            // packetWaitType = STP_MAX.
            packetWaitType = WTE_STAND_FROM_DEATH;
        } else {
            packetWaitType = m_data->m_StopMode;
            if (packetWaitType == STP_STORE) {
                // packetWaitType = STP_SIT.
                packetWaitType = WTE_SIT;
            }
        }
        const FVector broadcastPosition = GetPos();
        const int x = runtime::Win64CvttDoubleToInt32(position.x);
        const int y = runtime::Win64CvttDoubleToInt32(position.y);
        const int z = runtime::Win64CvttDoubleToInt32(position.z);
        g_ptlUserMap->BroadcastToNeighbor(
            *this, 0x80000, broadcastPosition, "cddddd", 0x29, runtime::Int32FromBits(m_nId), packetWaitType, x, y, z
        );
        return;
    }

    if (IsPC()) {
        if (waitType == STP_STAND) {
            CUserSocket* const socket = CastUser()->GetSocket();
            const int z = runtime::Win64CvttDoubleToInt32(position.z);
            const int y = runtime::Win64CvttDoubleToInt32(position.y);
            const int x = runtime::Win64CvttDoubleToInt32(position.x);
            socket->Send(
                "cddddd", 0x29, runtime::Int32FromBits(m_nId),
                // STP_STORE,
                WTE_FAKE_DEATH, x, y, z
            );
        } else if (m_data->m_bIsFakeDeath && m_data->m_StopMode == STP_STAND) {
            CUserSocket* const socket = CastUser()->GetSocket();
            const int z = runtime::Win64CvttDoubleToInt32(position.z);
            const int y = runtime::Win64CvttDoubleToInt32(position.y);
            const int x = runtime::Win64CvttDoubleToInt32(position.x);
            socket->Send(
                "cddddd", 0x29, runtime::Int32FromBits(m_nId),
                // STP_MAX,
                WTE_STAND_FROM_DEATH, x, y, z
            );
        } else {
            int packetWaitType = m_data->m_StopMode;
            if (packetWaitType == STP_STORE) {
                // packetWaitType = STP_SIT.
                packetWaitType = WTE_SIT;
            }
            CUserSocket* const socket = CastUser()->GetSocket();
            const int z = runtime::Win64CvttDoubleToInt32(position.z);
            const int y = runtime::Win64CvttDoubleToInt32(position.y);
            const int x = runtime::Win64CvttDoubleToInt32(position.x);
            socket->Send("cddddd", 0x29, runtime::Int32FromBits(m_nId), packetWaitType, x, y, z);
        }
    }
}

void CCreature::BroadcastCombatModeStart() {
    L2_GUARD_FUNCTION();
    if (!IsGMInvisible()) {
        const FVector position = GetPos();
        g_ptlUserMap->BroadcastToNeighborDist(this, 0x80000, position, 0x1400, "cd", 0x25, runtime::Int32FromBits(m_nId));
        return;
    }
    if (IsPC()) {
        CastUser()->GetSocket()->Send("cd", 0x25, runtime::Int32FromBits(m_nId));
    }
}

void CCreature::BroadcastCombatModeFinish() {
    L2_GUARD_FUNCTION();
    if (!IsGMInvisible()) {
        const FVector position = GetPos();
        g_ptlUserMap->BroadcastToNeighborDist(this, 0x80000, position, 0x1400, "cd", 0x26, runtime::Int32FromBits(m_nId));
        return;
    }
    if (IsPC()) {
        CastUser()->GetSocket()->Send("cd", 0x26, runtime::Int32FromBits(m_nId));
    }
}

void CCreature::BroadcastHP(const int reason, const unsigned int objectId) {
    // Current L2Server 0x005d6e6c.
    L2_GUARD_FUNCTION();

    const FVector position = GetPos();
    const int hp = runtime::Win64CvttDoubleToInt32(m_data->m_dHP);
    if (!g_bSendSummonHPMPInfo && IsSummon()) {
        g_ptlUserMap->BroadcastToNeighborParty(
            this, 0x80000, position, g_nDistanceBroadcastingHPMP, "cddcccd", 0x18, runtime::Int32FromBits(m_nId),
            static_cast<int>(objectId), reason, 1, 9, hp
        );

        // The current target publishes HP+MP on the lawful-PVP
        // route even though this entry point is named BroadcastHP.
        const int mp = runtime::Win64CvttDoubleToInt32(m_data->m_dMP);
        g_ptlUserMap->BroadcastToLawfulPVPMember(
            this, 0x80000, position, g_nDistanceBroadcastingHPMP, "cddcccdcd", 0x18, runtime::Int32FromBits(m_nId),
            static_cast<int>(objectId), reason, 2, 9, hp, 0x0b, mp
        );
        return;
    }

    g_ptlUserMap->BroadcastToNeighborDist(
        this, 0x80000, position, g_nDistanceBroadcastingHPMP, "cddcccd", 0x18, runtime::Int32FromBits(m_nId),
        static_cast<int>(objectId), reason, 1, 9, hp
    );
}

void CCreature::BroadcastHPMP(const int reason, const unsigned int objectId) {
    // Current L2Server 0x005d7100.
    L2_GUARD_FUNCTION();

    const FVector position = GetPos();
    const int hp = runtime::Win64CvttDoubleToInt32(m_data->m_dHP);
    const int mp = runtime::Win64CvttDoubleToInt32(m_data->m_dMP);
    if (!g_bSendSummonHPMPInfo && IsSummon()) {
        g_ptlUserMap->BroadcastToNeighborParty(
            this, 0x80000, position, g_nDistanceBroadcastingHPMP, "cddcccdcd", 0x18, runtime::Int32FromBits(m_nId),
            static_cast<int>(objectId), reason, 2, 9, hp, 0x0b, mp
        );
        g_ptlUserMap->BroadcastToLawfulPVPMember(
            this, 0x80000, position, g_nDistanceBroadcastingHPMP, "cddcccdcd", 0x18, runtime::Int32FromBits(m_nId),
            static_cast<int>(objectId), reason, 2, 9, hp, 0x0b, mp
        );
        return;
    }

    g_ptlUserMap->BroadcastToNeighborDist(
        this, 0x80000, position, g_nDistanceBroadcastingHPMP, "cddcccdcd", 0x18, runtime::Int32FromBits(m_nId),
        static_cast<int>(objectId), reason, 2, 9, hp, 0x0b, mp
    );
}

DefaultIOBuffer* CCreature::MakePacketForMagicSkillUse(
    const int& parameter1,
    const CWorldObject* target,
    const SkillUseInfo& skill,
    const int& parameter2,
    const int& parameter3,
    const int& parameter4,
    const int& parameter5,
    const int& parameter6,
    char* buffer
) {
    L2_GUARD_FUNCTION();

    const FVector sourcePosition = GetPos();
    const FVector targetPosition = target->GetPos();
    const std::uint32_t skillLevel =
        (static_cast<std::uint32_t>(skill.m_skillSubLevel) << 16U) | static_cast<std::uint16_t>(skill.m_skillLevel);

    if (buffer == nullptr) {
        return ::MakeConstBuffer(
            "cdddddddddddhhddddd", 0x48, parameter1, runtime::Int32FromBits(m_nId),
            runtime::Int32FromBits(target->m_nId), skill.m_skillID, runtime::Int32FromBits(skillLevel), parameter2,
            parameter3, parameter4, runtime::Win64CvttDoubleToInt32(sourcePosition.x),
            runtime::Win64CvttDoubleToInt32(sourcePosition.y), runtime::Win64CvttDoubleToInt32(sourcePosition.z), 0, 0,
            runtime::Win64CvttDoubleToInt32(targetPosition.x), runtime::Win64CvttDoubleToInt32(targetPosition.y),
            runtime::Win64CvttDoubleToInt32(targetPosition.z), skill.m_type, skill.m_param1
        );
    }

    return ::MakeConstBuffer(
        "cdddddddddddhbddddd", 0x48, parameter1, runtime::Int32FromBits(m_nId), runtime::Int32FromBits(target->m_nId),
        skill.m_skillID, runtime::Int32FromBits(skillLevel), parameter2, parameter3, parameter4,
        runtime::Win64CvttDoubleToInt32(sourcePosition.x), runtime::Win64CvttDoubleToInt32(sourcePosition.y),
        runtime::Win64CvttDoubleToInt32(sourcePosition.z), parameter5, parameter6, buffer,
        runtime::Win64CvttDoubleToInt32(targetPosition.x), runtime::Win64CvttDoubleToInt32(targetPosition.y),
        runtime::Win64CvttDoubleToInt32(targetPosition.z), skill.m_type, skill.m_param1
    );
}

void CCreature::BroadcastCubicSkillUse(CWorldObject& target, const CSkillInfo* skillInfo, int first, int second) {
    L2_GUARD_FUNCTION();

    m_data->m_NeedUpdatePacketBitSet.SetBoolValue_IfModifed(m_IsSheathingWeapon, false, 0x5a);

    SkillUseInfo skill;
    skill.m_skillID = skillInfo->m_nSkillId;
    skill.m_skillLevel = skillInfo->m_nLevel;
    skill.m_skillSubLevel = skillInfo->m_nSubLevel;
    skill.m_castRange = 0;
    skill.m_type = 0;
    skill.m_param1 = 0;

    if (!IsGMInvisible()) {
        if (!IsInAirShip()) {
            int parameter1 = 0;
            int parameter2 = first;
            int parameter3 = 0;
            int parameter4 = second;
            int parameter5 = 0;
            int parameter6 = 0;
            DefaultIOBuffer* const packet = MakePacketForMagicSkillUse(
                parameter1, &target, skill, parameter2, parameter3, parameter4, parameter5, parameter6, nullptr
            );
            const int distance = GetBroadcastDist(0x1000);
            const FVector position = GetPos();
            g_ptlUserMap->BroadcastToNeighborDist(this, 0xf000, position, distance, packet);
            packet->Release();
            return;
        }

        const FVector relativePosition = m_RelPos;
        const std::uint32_t skillLevel =
            (static_cast<std::uint32_t>(skillInfo->m_nSubLevel) << 16U) | static_cast<std::uint16_t>(skillInfo->m_nLevel);
        const int airShipId = static_cast<int>(GetMyAirShipID());
        const int distance = GetBroadcastDist(0x1000);
        const FVector position = GetPos();
        g_ptlUserMap->BroadcastToNeighborDist(
            this, 0xf000, position, distance, "chddddddddddddhh", 0xfe, 0x74, 0, runtime::Int32FromBits(m_nId),
            runtime::Int32FromBits(target.m_nId), skillInfo->m_nSkillId, runtime::Int32FromBits(skillLevel), first, 0,
            second, runtime::Win64CvttDoubleToInt32(relativePosition.x),
            runtime::Win64CvttDoubleToInt32(relativePosition.y), runtime::Win64CvttDoubleToInt32(relativePosition.z),
            airShipId, 0, 0
        );
        return;
    }

    if (!IsPC()) {
        return;
    }

    if (!IsInAirShip()) {
        int parameter1 = 0;
        int parameter2 = first;
        int parameter3 = 0;
        int parameter4 = second;
        int parameter5 = 0;
        int parameter6 = 0;
        User* const user = CastUser();
        user->SendMagicSkill(
            parameter1, &target, skill, parameter2, parameter3, parameter4, parameter5, parameter6, nullptr
        );
        return;
    }

    const FVector relativePosition = m_RelPos;
    User* const user = CastUser();
    CUserSocket* const socket = user->GetSocket();
    const std::uint32_t skillLevel =
        (static_cast<std::uint32_t>(skillInfo->m_nSubLevel) << 16U) | static_cast<std::uint16_t>(skillInfo->m_nLevel);
    const int airShipId = static_cast<int>(GetMyAirShipID());
    socket->Send(
        "chddddddddddddhh", 0xfe, 0x74, 0, runtime::Int32FromBits(m_nId), runtime::Int32FromBits(target.m_nId),
        skillInfo->m_nSkillId, runtime::Int32FromBits(skillLevel), first, 0, second,
        runtime::Win64CvttDoubleToInt32(relativePosition.x), runtime::Win64CvttDoubleToInt32(relativePosition.y),
        runtime::Win64CvttDoubleToInt32(relativePosition.z), airShipId, 0, 0
    );
}

void CCreature::SendMagicAttckInfo(CCreature* source, CCreature* target, MagicAttackInfo::Enum info) {
    L2_GUARD_FUNCTION();
    if (target == nullptr) {
        return;
    }

    int sourceId = 0;
    if (source != nullptr) {
        sourceId = runtime::Int32FromBits(source->m_nId);
        if (source->IsPC()) {
            source->CastUser()->GetSocket()->Send(
                "chddd", 0xfe, 0xfb, sourceId, runtime::Int32FromBits(target->m_nId), info
            );
        }
        if (source->m_nId == target->m_nId) {
            return;
        }
    }

    if (target->IsPC()) {
        target->CastUser()->GetSocket()->Send("chddd", 0xfe, 0xfb, sourceId, runtime::Int32FromBits(target->m_nId), info);
        return;
    }

    if (target->IsSummonOrPet() && target->GetUserOrMaster() != nullptr) {
        target->GetUserOrMaster()->GetSocket()->Send(
            "chddd", 0xfe, 0xfb, sourceId, runtime::Int32FromBits(target->m_nId), info
        );
    }
}
