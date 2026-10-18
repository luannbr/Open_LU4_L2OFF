#include "Area.h"
#include "Creature.h"
#include "Duel.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Skill.h"

namespace AreaBasedRule {

namespace details {

bool IsInPeaceField(const std::uint32_t inZoneId, FVector* const position) {
    // Current L2Server 0x0041a7d8.
    return CAreaDB::inst().FindArea(static_cast<int>(inZoneId), *position, AREAT_PEACE, true) != nullptr;
}

double GetExpPenaltyRatio(const std::uint32_t inZoneId, FVector* const position, const AreaTypeEnum areaType) {
    // Current L2Server 0x0041a818.
    CArea* const area = CAreaDB::inst().FindArea(static_cast<int>(inZoneId), *position, areaType, true);
    return area != nullptr ? area->mExpPenaltyPer : 1.0;
}

} // namespace details

bool BotReport(const CCreature* creature) {
    if (creature->m_AreaMod.m_bIsInPeaceField) {
        return false;
    }
    return !creature->m_AreaMod.m_bIsInOnlyPvEZone;
}

int Attackable(CCreature* const relatedCreature, CCreature* const attacker, CCreature* const target) {
    // Current L2Server 0x0041a620.
    if (relatedCreature != nullptr) {
        if (relatedCreature->m_AreaMod.m_bIsInPeaceField) {
            return SystemMessageId::CANNOT_ATTACK_IN_PEACE_ZONE;
        }
        if (relatedCreature->m_AreaMod.m_bIsInOnlyPvEZone) {
            return SystemMessageId::CANNOT_ATTACK_FROM_THIS_AREA;
        }
    }
    if (attacker->m_AreaMod.m_bIsInPeaceField) {
        return SystemMessageId::CANNOT_ATTACK_IN_PEACE_ZONE;
    }
    if (attacker->m_AreaMod.m_bIsInOnlyPvEZone) {
        return SystemMessageId::CANNOT_ATTACK_FROM_THIS_AREA;
    }
    if (target->m_AreaMod.m_bIsInPeaceField) {
        return SystemMessageId::CANNOT_ATTACK_TARGET_IN_PEACE_ZONE;
    }
    if (target->m_AreaMod.m_bIsInOnlyPvEZone) {
        return SystemMessageId::CANNOT_ATTACK_TARGET_IN_NONCOMBAT_AREA;
    }
    return 0;
}

bool CastBadSkill(CCreature* creature) {
    // Current L2Server 0x0041a60c.
    return !creature->m_AreaMod.m_bIsInPeaceField;
}

bool CancelPost(CCreature* creature) {
    // Current L2Server 0x0041a618.
    return creature->m_AreaMod.m_bIsInPeaceField;
}

bool KilledByPC(const std::uint32_t inZoneId, FVector* const position) {
    return details::IsInPeaceField(inZoneId, position);
}

bool PetKilledByPC(const CCreature* const creature) {
    // Current L2Server 0x0041a870. The live area lookup is intentional:
    // the cached only-PvE flag is consulted only when no peace-area owner
    // contains the creature's current position.
    FVector position = creature->GetPos();
    if (KilledByPC(creature->GetInZoneID(), &position)) {
        return true;
    }
    return creature->m_AreaMod.m_bIsInOnlyPvEZone;
}

bool ExpDownPanelty(CCreature* const creature, const std::int64_t defaultPenalty, std::int64_t* const adjustedPenalty) {
    // Current L2Server 0x0041a8cc. Preserve the original misspelling and
    // truncating double-to-int64 assignment.
    if (!creature->m_AreaMod.m_bIsInOnlyPvEZone) {
        return false;
    }
    FVector position = creature->GetPos();
    const double ratio = details::GetExpPenaltyRatio(creature->GetInZoneID(), &position, AREAT_ONLY_PVE);
    *adjustedPenalty = static_cast<std::int64_t>(ratio * static_cast<double>(defaultPenalty));
    return true;
}

bool CompassPeace(const CGeoSector* const sector, const std::uint32_t worldId, FVector* const position) {
    // Current L2Server 0x0041aae0.
    const std::uint16_t field = static_cast<std::uint16_t>(sector->m_Field);
    const std::uint16_t peaceMask = GeoFieldMask[GEO_FIELD_PEACE];
    if ((field & peaceMask) != peaceMask) {
        return false;
    }
    return details::IsInPeaceField(worldId, position);
}

bool RechargeVitality(const CCreatureAreaMod& areaMod) {
    // Current L2Server 0x0041a674.
    return areaMod.m_bIsInPeaceField;
}

bool SpendPlaytime(const CCreatureAreaMod& areaMod) {
    // Current L2Server 0x0041a678.
    return !areaMod.m_bIsInPeaceField;
}

bool UpdateTimeChangePeaceZone(const CCreature* creature) {
    // Current L2Server 0x0041a73c.
    return creature->m_AreaMod.m_bWasInPeaceField != creature->m_AreaMod.m_bIsInPeaceField;
}

DuelStartErrorType DuelStart(CCreature* creature) {
    // Current L2Server 0x0041a68c.
    if (creature->m_AreaMod.m_bIsInPeaceField || creature->m_AreaMod.m_bIsUnderWater || creature->m_AreaMod.m_bIsInSSQZone ||
        creature->m_AreaMod.m_bIsInOnlyPvEZone) {
        return DSET_IN_BATTLE_ZONE;
    }
    return DSET_SUCCESS;
}

bool KarmaPenalty(CCreature* creature) {
    // Current L2Server 0x0041a950. Being flagged as inside a peace field is
    // an immediate exemption. Otherwise the current rule repeats the exact
    // indexed AreaDB query and penalizes only when no peace area owns the
    // position.
    if (creature->m_AreaMod.m_bIsInPeaceField) {
        return false;
    }
    FVector position = creature->GetPos();
    return !details::IsInPeaceField(creature->GetInZoneID(), &position);
}

bool PrivateStore(CCreature* creature) {
    // Current L2Server 0x0041ab08.
    const FVector position = creature->GetPos();
    CArea* const area = CAreaDB::inst().FindArea(static_cast<int>(creature->GetInZoneID()), position, AREAT_PEACE, true);
    return area == nullptr || area->m_BlockedActionTable[0] == 0;
}

bool PrivateRecipeShop(CCreature* creature) {
    // Current L2Server 0x0041ab70.
    const FVector position = creature->GetPos();
    CArea* const area = CAreaDB::inst().FindArea(static_cast<int>(creature->GetInZoneID()), position, AREAT_PEACE, true);
    return area == nullptr || area->m_BlockedActionTable[1] == 0;
}

bool StartIn(const CGeoSector* const sector, const FVector& position) {
    // Current L2Server 0x0041a6bc.
    const std::uint16_t field = static_cast<std::uint16_t>(sector->m_Field);
    const std::uint16_t battleMask =
        static_cast<std::uint16_t>(GeoFieldMask[GEO_FIELD_BATTLE_RESIDENCE] | GeoFieldMask[GEO_FIELD_BATTLE_PVP]);
    return (field & battleMask) != 0 && (field & GeoFieldMask[GEO_FIELD_PEACE]) == 0 &&
        static_cast<double>(sector->m_TerritoryMinZ) <= position.z &&
        position.z <= static_cast<double>(sector->m_TerritoryMaxZ);
}

bool TakeBadSkill(CCreature* creature, CCreature* target) {
    // Current L2Server 0x0041aa6c.
    if (target->m_AreaMod.m_bIsInPeaceField) {
        return false;
    }
    if (target->m_AreaMod.m_bIsInOnlyPvEZone) {
        const CSmartID targetId = target->GetSmartID();
        const CSmartID creatureId = creature->GetSmartID();
        if ((creatureId.Sid() == 0 || creatureId.Sid() != targetId.Sid()) && target->IsPCOrPCRelatedNPC()) {
            return false;
        }
    }
    return true;
}

RangeTargetType RangeTargetOfSkill(CCreature* creature, CCreature* target, const SkillTargetTypeEnum targetType) {
    // Current L2Server 0x0041a9ac.
    RangeTargetType result = RTT_WHITE;
    if (creature->IsPCOrSummon() && !creature->m_AreaMod.m_bIsInBattleField) {
        if (!target->IsPCOrSummon()) {
            result = RTT_CHAO;
        } else {
            if (creature->m_AreaMod.m_bIsInPeaceField) {
                const CSmartID targetId = target->GetSmartID();
                const CSmartID creatureId = creature->GetSmartID();
                if (creatureId.Sid() != 0 && creatureId.Sid() == targetId.Sid() && targetType != STGT_GROUND) {
                    return RTT_PEACE_ZONE;
                }
            }
            if (target->m_data->m_nKarma < 0) {
                result = RTT_CHAO;
            } else if (target->IsGuilty()) {
                result = RTT_VIOLET;
            }
        }
    }
    return result;
}

} // namespace AreaBasedRule
