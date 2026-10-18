#include "CWorld.h"
#include "Creature.h"
#include "Pet.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "SkillFxFunc.h"
#include "User.h"

#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

bool IsNegativeNonZero(double value) { return value <= 0.0 && value != 0.0; }

User* ResolvePvPUser(CCreature* creature) {
    if (creature == nullptr) {
        return nullptr;
    }
    if (creature->IsPC()) {
        return creature->CastUser();
    }
    CSummon* const summon = creature->CastSummon();
    return summon == nullptr ? nullptr : summon->GetMaster();
}

bool IsSpecialPvPUser(const User& user) { return user.IsEinhasadeTransformed() || user.m_data->m_nKarma < 0; }

bool IsProtectedFromSpecialLevelGap(CCreature* target, CCreature* source) {
    if (target == nullptr || source == nullptr || !target->IsPCOrSummon() || !source->IsPCOrSummon()) {
        return false;
    }

    User* const targetUser = ResolvePvPUser(target);
    User* const sourceUser = ResolvePvPUser(source);
    if (targetUser == nullptr || sourceUser == nullptr) {
        return false;
    }

    const bool targetSpecial = IsSpecialPvPUser(*targetUser);
    const bool sourceSpecial = IsSpecialPvPUser(*sourceUser);
    if (targetSpecial == sourceSpecial) {
        return false;
    }

    User* const normalUser = targetSpecial ? sourceUser : targetUser;
    User* const specialUser = targetSpecial ? targetUser : sourceUser;
    return normalUser->m_SkillMod.m_bIsPKProtect &&
        (!targetUser->m_AreaMod.m_bIsInBattleField || !sourceUser->m_AreaMod.m_bIsInBattleField) &&
        specialUser->Level() - normalUser->Level() > 9;
}

} // namespace

bool CSkillEffect_t_hp_in_invincible::Tick(CWorldObject* target, double amount, unsigned sourceId, const CSkillInfo*) {
    // Current L2Server 0x00ace790.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    CObject* const sourceObject = CObject::GetObjectW(sourceId);
    if (sourceObject == nullptr) {
        return false;
    }

    CCreature* const sourceCreature = sourceObject->CastCreature();
    const double change = amount * dAmount;
    const double damage = -change;
    if (IsNegativeNonZero(dAmount)) {
        targetCreature->SafeAccumulateContribute(sourceCreature, damage);
    }

    if (!sourceCreature->IsPCOrPCRelatedNPC() || !targetCreature->IsPC() || !IsNegativeNonZero(dAmount)) {
        L2SkillFunc::IncreaseHP(targetCreature, sourceId, m_Sfct, change, 6, false);
    } else {
        L2SkillFunc::DecreaseCP(targetCreature, sourceId, m_Sfct, damage, 6);
    }
    return true;
}

bool CSkillEffect_t_cub_hp::Tick(CWorldObject* target, double amount, unsigned sourceId, const CSkillInfo*) {
    // Current L2Server 0x00ace938.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    const double change = amount * dAmount;
    if (sourceId == 0) {
        L2SkillFunc::IncreaseHP(targetCreature, 0, m_Sfct, change, 6, false);
        return true;
    }

    CObject* const sourceObject = CObject::GetObjectW(sourceId);
    if (sourceObject == nullptr || !sourceObject->IsCreature()) {
        L2SkillFunc::IncreaseHP(targetCreature, sourceId, m_Sfct, change, 6, false);
        return true;
    }

    CCreature* sourceCreature = sourceObject->CastCreature();
    if (IsNegativeNonZero(dAmount)) {
        User* master = nullptr;
        if (sourceCreature->IsSummonOrPet()) {
            CSummon* const summon = sourceCreature->CastSummon();
            master = summon == nullptr ? nullptr : summon->GetMaster();
        }
        if (master != nullptr && master->IsPC()) {
            sourceCreature = master;
            targetCreature->CastCreature()->AccumulateContribute(sourceCreature, -dAmount * amount);
        } else {
            targetCreature->CastCreature()->AccumulateContribute(sourceCreature, -dAmount * amount);
        }
    }

    if (sourceObject->IsPCOrSummon() && targetCreature->IsPC() && dAmount < 0.0) {
        L2SkillFunc::DecreaseCP(targetCreature, sourceId, m_Sfct, -dAmount * amount, 6);
    } else {
        L2SkillFunc::IncreaseHP(targetCreature, sourceId, m_Sfct, change, 6, false);
    }
    return true;
}

bool CSkillEffect_t_mp::Tick(CWorldObject* target, double amount, unsigned sourceId, const CSkillInfo*) {
    // Current L2Server 0x00acebd8.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }
    CCreature* const targetCreature = target->CastCreature();
    if (dAmount <= 0.0 && !targetCreature->m_SkillMod.CanGetDamageMP(nullptr, targetCreature)) {
        return false;
    }
    L2SkillFunc::IncreaseMP(targetCreature, sourceId, m_Sfct, amount * dAmount);
    return true;
}

bool CSkillEffect_t_cp::Tick(CWorldObject* target, double amount, unsigned sourceId, const CSkillInfo*) {
    // Current L2Server 0x00acecf0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }
    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature == nullptr) {
        return false;
    }

    const double change = amount * dAmount;
    if (sourceId == 0) {
        L2SkillFunc::IncreaseCP(targetCreature, 0, m_Sfct, change, 6);
        return true;
    }

    CObject* const sourceObject = CObject::GetObjectW(sourceId);
    if (sourceObject == nullptr || !sourceObject->IsCreature()) {
        L2SkillFunc::IncreaseCP(targetCreature, sourceId, m_Sfct, change, 6);
        return true;
    }

    CCreature* const sourceCreature = sourceObject->CastCreature();
    if (IsNegativeNonZero(dAmount)) {
        targetCreature->SafeAccumulateContribute(sourceCreature, -change);
    }
    if (sourceCreature->IsPCOrPCRelatedNPC() && targetCreature->IsPC() && IsNegativeNonZero(dAmount)) {
        L2SkillFunc::DecreaseCP(targetCreature, sourceId, m_Sfct, -change, 6);
    } else {
        L2SkillFunc::IncreaseCP(targetCreature, sourceId, m_Sfct, change, 6);
    }
    return true;
}

bool CSkillEffect_t_get_energy::Tick(CWorldObject* target, double, unsigned, const CSkillInfo*) {
    // Current L2Server 0x00acef14.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(target)) {
        return false;
    }
    User* const user = target->CastUser();
    if (!user->m_SkillMod.m_CanGetEnergy) {
        return true;
    }

    L2_GUARD(*user->m_lock);
    if (user->m_nEnergyCount < user->m_SkillMod.m_MaxEnergyCount) {
        ++user->m_nEnergyCount;
        if (user->m_nEnergyCount == 1) {
            user->m_dwEnergyStartTime = runtime::GetTickCount();
        }
        user->SendSystemMessage_d(SystemMessageId::FORCE_LEVEL_INCREASED, user->m_nEnergyCount);
        user->SendETCBuffStatusInfo();
    }
    return true;
}

bool CSkillEffect_t_energy::Tick(CWorldObject* target, double, unsigned, const CSkillInfo*) {
    // Current L2Server 0x00acf06c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(target)) {
        return false;
    }
    User* const user = target->CastUser();
    if (user->m_SkillMod.m_CanGetEnergy) {
        L2_GUARD(*user->m_lock);
        if (m_EnergyVar < 1) {
            user->m_nEnergyCount += m_EnergyVar;
            if (user->m_nEnergyCount < 0) {
                user->m_nEnergyCount = 0;
            }
        } else {
            user->m_nEnergyCount = std::min(user->m_nEnergyCount + m_EnergyVar, user->m_SkillMod.m_MaxEnergyCount);
        }
        user->SendETCBuffStatusInfo();
    }
    return true;
}

bool CSkillEffect_t_pull::Tick(CWorldObject* target, double, unsigned sourceId, const CSkillInfo*) {
    // Current L2Server 0x00acf1c0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }
    CObject* const sourceObject = CObject::GetObjectW(sourceId);
    if (sourceObject == nullptr || !sourceObject->IsPC()) {
        return false;
    }

    User* const sourceUser = sourceObject->CastUser();
    const FVector sourcePosition = sourceUser->GetPos();
    const FVector targetPosition = target->GetPos();
    const double distance = std::sqrt(sourcePosition.DistCube(targetPosition));
    if (distance <= 30.0) {
        return true;
    }

    const double moveDistance = std::max(distance - m_Dist, 30.0);
    FVector direction{1.0, 1.0, 1.0};
    if (distance <= -0.00001 || 0.00001 <= distance) {
        direction.x = (targetPosition.x - sourcePosition.x) / distance;
        direction.y = (targetPosition.y - sourcePosition.y) / distance;
        direction.z = (targetPosition.z - sourcePosition.z) / distance;
    }
    FVector destination{sourcePosition.x + direction.x * moveDistance, sourcePosition.y + direction.y * moveDistance,
        sourcePosition.z + direction.z * moveDistance};
    FVector output{};
    bool distanceExhausted = false;
    double travelledDistance = 0.0;
    CWorld& world = CWorld::Inst();
    if (!world.CanGo(
            targetPosition, destination, output, distanceExhausted, travelledDistance,
            static_cast<int>(sourceUser->GetInZoneID()), false
        )) {
        return false;
    }

    CCreature* const targetCreature = target->CastCreature();
    if (!world.IsValidPosition(static_cast<int>(destination.x), static_cast<int>(destination.y))) {
        return false;
    }
    targetCreature->BroadcastFlyToLocation(
        static_cast<int>(destination.x), static_cast<int>(destination.y), static_cast<int>(destination.z), FTLT_STRAIGHT, 0,
        200, 0
    );
    world.MoveTo(targetCreature, destination, false);
    return true;
}

void CSkillEffect_t_hp_to_owner::ApplyCPBonus(CCreature& creature, double amount) {
    // Current L2Server 0x00acf4c8.
    L2_GUARD_FUNCTION();
    L2SkillFunc::IncreaseCP(&creature, 0, m_Sfct, amount, 6);
}

bool CSkillEffect_t_hp_by_hp::Tick(CWorldObject* target, double amount, unsigned sourceId, const CSkillInfo* skillInfo) {
    // Current L2Server 0x00acf58c.
    L2_GUARD_FUNCTION();
    CCreature* const creature = target->CastCreature();
    if (creature == nullptr) {
        return false;
    }
    if (creature->m_data->m_dHP <= creature->m_data->m_dOrgHP * m_dHPratio) {
        m_effect.Tick(target, amount, sourceId, skillInfo);
    }
    return true;
}

bool CSkillEffect_t_hp::Tick(CWorldObject* target, double amount, unsigned sourceId, const CSkillInfo*) {
    // Current L2Server 0x00acf6a0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }
    CCreature* targetCreature = target->CastCreature();
    if (targetCreature == nullptr) {
        return false;
    }

    CCreature* const damageSource = g_CreatureSmartIdManager.GetObjectW(sourceId);
    if (IsNegativeNonZero(dAmount) && !targetCreature->m_SkillMod.CanGetDamageHP(damageSource, targetCreature)) {
        return false;
    }

    const double scale = m_Sfct == SFCT_PER ? 0.01 : 1.0;
    double change = amount * dAmount * scale;
    const double damage = -change;
    int reason = damage > 0.0 ? 6 : 1;
    if (sourceId == 0) {
        L2SkillFunc::IncreaseHP(targetCreature, 0, m_Sfct, change, reason, false);
        return true;
    }

    CObject* const sourceObject = CObject::GetObjectW(sourceId);
    if (sourceObject == nullptr || !sourceObject->IsCreature()) {
        L2SkillFunc::IncreaseHP(targetCreature, sourceId, m_Sfct, change, reason, false);
        return true;
    }

    CCreature* const sourceCreature = sourceObject->CastCreature();
    if (IsNegativeNonZero(dAmount)) {
        targetCreature->SafeAccumulateContribute(sourceCreature, damage);
    }
    if (IsProtectedFromSpecialLevelGap(targetCreature, sourceCreature)) {
        return true;
    }

    if (reason == 6 && targetCreature->m_SkillMod.m_nTransferDamageRatio > 0) {
        CCreature* const transferTarget = g_CreatureSmartIdManager.GetObjectW(
            static_cast<std::uint32_t>(targetCreature->m_SkillMod.m_TransferDamageTargetId)
        );
        if (transferTarget != nullptr && transferTarget->IsAlive() &&
            targetCreature->GetPos().DistCube(transferTarget->GetPos()) < 1000000.0) {
            const int integerDamage = runtime::Win64CvttDoubleToInt32(damage);
            const int transferredDamage = runtime::Win64CvttDoubleToInt32(
                static_cast<double>(targetCreature->m_SkillMod.m_nTransferDamageRatio) * static_cast<double>(integerDamage) /
                100.0
            );
            const int remainingDamage = integerDamage - transferredDamage;
            if (transferTarget->m_data->m_dHP - static_cast<double>(transferredDamage) > 0.0) {
                if (m_CpEnable && sourceCreature->IsPCOrPCRelatedNPC() && targetCreature->IsPC() &&
                    IsNegativeNonZero(dAmount)) {
                    L2SkillFunc::DecreaseCP(targetCreature, sourceId, m_Sfct, static_cast<double>(remainingDamage), 6);
                    L2SkillFunc::DecreaseCP(transferTarget, sourceId, m_Sfct, static_cast<double>(transferredDamage), 6);
                    return true;
                }
                L2SkillFunc::IncreaseHP(targetCreature, sourceId, m_Sfct, -static_cast<double>(remainingDamage), 6, false);
                targetCreature = transferTarget;
                change = -static_cast<double>(transferredDamage);
                reason = 6;
            }
        }
    }

    if (m_CpEnable && sourceCreature->IsPCOrPCRelatedNPC() && targetCreature->IsPC() && IsNegativeNonZero(dAmount)) {
        L2SkillFunc::DecreaseCP(targetCreature, sourceId, m_Sfct, -change, reason);
    } else {
        L2SkillFunc::IncreaseHP(targetCreature, sourceId, m_Sfct, change, reason, false);
    }
    return true;
}

bool CSkillEffect_t_hp_fatal::Tick(CWorldObject* target, double amount, unsigned sourceId, const CSkillInfo*) {
    // Current L2Server 0x00acfcc0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }
    CCreature* const targetCreature = target->CastCreature();
    if (IsNegativeNonZero(dAmount) && !targetCreature->m_SkillMod.CanGetDamageHP(nullptr, targetCreature)) {
        return false;
    }

    double factor = 1.0;
    {
        L2_GUARD(targetCreature->m_InfectedSkillsSect);
        for (const InfectedSkillType& infected : targetCreature->m_InfectedSkills) {
            if (infected.pSkillInfo == nullptr || infected.pSkillInfo->m_nAbnormalType != ABNS_FATAL_POISON) {
                continue;
            }
            const int remaining = targetCreature->GetAbnormalTime(infected.pSkillInfo, false) + infected.nStartTime -
                runtime::crt::Time32Now();
            if (remaining >= 1 && remaining <= 359) {
                const int elapsed = 360 - remaining;
                factor = static_cast<double>((elapsed * elapsed) / 4000);
                factor = std::clamp(factor, 1.0, 20.0);
            } else {
                factor = 1.0;
            }
        }
    }

    const double damage = -dAmount * amount * factor;
    CCreature* const sourceCreature = g_CreatureSmartIdManager.GetObjectW(sourceId);
    if (sourceCreature == nullptr) {
        targetCreature->GotDamageByEnvironment(damage, true, DAMAGE_POISON, true);
    } else {
        targetCreature->GotDamageBy(sourceCreature, damage, false, DAMAGE_POISON, true);
    }
    return true;
}

bool CSkillEffect_t_hp_magical_attack::Tick(CWorldObject* target, double, unsigned sourceId, const CSkillInfo* skillInfo) {
    // Current L2Server 0x00acffb8.
    L2_GUARD_FUNCTION();
    if (skillInfo == nullptr || !L2Common::IsAliveCreature(target) || !L2SkillFunc::IsAttackable(target)) {
        return false;
    }
    CCreature* const targetCreature = target->CastCreature();
    if (targetCreature == nullptr) {
        return false;
    }

    CCreature* sourceCreature = g_CreatureSmartIdManager.GetObjectW(sourceId);
    double magicPower = static_cast<double>(m_nBaseMagicPower);
    double magicDefence = targetCreature->m_data->m_dParam[VALIDATE::MDEFEND];
    if (sourceCreature != nullptr) {
        const L2SkillFunc::MagicAttackerInfo attackerInfo = L2SkillFunc::GetMagicAttackerInfo(*sourceCreature);
        const L2SkillFunc::MagicTargetInfo targetInfo =
            L2SkillFunc::GetMagicTargetInfo(*sourceCreature, *targetCreature, skillInfo);
        magicPower = attackerInfo.m_MagicPower;
        magicDefence = targetInfo.m_MDefend;
    }

    const double damage = std::sqrt(magicPower) * dAmount * 91.0 / magicDefence;
    if (damage < 0.0 || !targetCreature->m_SkillMod.CanGetDamageHP(sourceCreature, targetCreature)) {
        return false;
    }

    if (sourceId == 0 || sourceCreature == nullptr || !sourceCreature->IsCreature()) {
        L2SkillFunc::IncreaseHP(targetCreature, sourceId, m_Sfct, -damage, 6, false);
        return true;
    }

    sourceCreature = sourceCreature->CastCreature();
    targetCreature->SafeAccumulateContribute(sourceCreature, damage);
    if (IsProtectedFromSpecialLevelGap(targetCreature, sourceCreature)) {
        return true;
    }
    if (sourceCreature->IsPCOrPCRelatedNPC() && targetCreature->IsPC()) {
        L2SkillFunc::DecreaseCP(targetCreature, sourceId, m_Sfct, damage, 6);
    } else {
        L2SkillFunc::IncreaseHP(targetCreature, sourceId, m_Sfct, -damage, 6, false);
    }
    return true;
}

void CSkillEffect_t_hp_to_owner::ApplyHPBonus(CCreature& creature, double amount) {
    // Current L2Server 0x00ad0398.
    L2_GUARD_FUNCTION();
    if (m_Sfct == SFCT_PER) {
        creature.IncreaseHP_per(amount, false, 6, 0);
        if (!creature.IsPC()) {
            return;
        }

        FOR_ALL_SUMMON_IN_SUMMON_LIST(creature.CastUser(), summon, summon->IncreaseHP_per(amount, false, 6, 0););
        return;
    }

    if (m_Sfct == SFCT_DIFF) {
        creature.IncreaseHP_diff(amount, true, 4, 0, false);
        if (!creature.IsPC()) {
            return;
        }

        FOR_ALL_SUMMON_IN_SUMMON_LIST(creature.CastUser(), summon, summon->IncreaseHP_diff(amount, false, 6, 0, false););
    }
}

void CSkillEffect_t_hp_to_owner::ApplyMPBonus(CCreature& creature, double amount) {
    // Current L2Server 0x00ad071c.
    L2_GUARD_FUNCTION();
    if (m_Sfct == SFCT_PER) {
        creature.IncreaseMP_per(amount, 6, 0);
        if (!creature.IsPC()) {
            return;
        }

        FOR_ALL_SUMMON_IN_SUMMON_LIST(creature.CastUser(), summon, summon->IncreaseMP_per(amount, 6, 0););
        return;
    }

    if (m_Sfct == SFCT_DIFF) {
        creature.IncreaseMP_diff(amount, 6, 0, false);
        if (!creature.IsPC()) {
            return;
        }

        FOR_ALL_SUMMON_IN_SUMMON_LIST(creature.CastUser(), summon, summon->IncreaseMP_diff(amount, 6, 0, false););
    }
}

bool CSkillEffect_t_hp_to_owner::Tick(CWorldObject* target, double amount, unsigned sourceId, const CSkillInfo*) {
    // Current L2Server 0x00ad0aa8.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }
    CCreature* const targetCreature = target->CastCreature();
    CObject* const sourceObject = CObject::GetObjectW(sourceId);
    if (sourceObject == nullptr) {
        return false;
    }
    CCreature* const sourceCreature = sourceObject->CastCreature();
    const double change = amount * dAmount;
    if (change < 0.0 && !targetCreature->m_SkillMod.CanGetDamageHP(nullptr, targetCreature)) {
        return false;
    }
    if (IsNegativeNonZero(dAmount)) {
        targetCreature->SafeAccumulateContribute(sourceCreature, -change);
    }
    if (!sourceCreature->IsPCOrPCRelatedNPC() || !targetCreature->IsPC() || !IsNegativeNonZero(dAmount)) {
        L2SkillFunc::IncreaseHP(targetCreature, sourceId, m_Sfct, change, 6, false);
    } else {
        L2SkillFunc::DecreaseCP(targetCreature, sourceId, m_Sfct, -change, 6);
    }

    const double bonus = (-change * m_Rate) / 100.0;
    for (const BonusStat stat : m_BonusStat) {
        if (stat == HP) {
            ApplyHPBonus(*sourceCreature, bonus);
        } else if (stat == MP) {
            ApplyMPBonus(*sourceCreature, bonus);
        } else if (stat == CP) {
            ApplyCPBonus(*sourceCreature, bonus);
        }
    }
    return true;
}
