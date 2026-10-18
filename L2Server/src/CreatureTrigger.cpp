#include "Creature.h"

#include "SerializedMessage.h"
#include "Skill.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"

#include <cstdint>
#include <cstring>

namespace {



template <typename TriggerInfo>
void FindMatchingAbnormalSlots(
    const CSharedCreatureData& sharedData, const TriggerInfo& triggerInfo, std::vector<int>& matchedSlots
) {
    for (const AbnormalDataForTrigger& required : triggerInfo.m_vAbnormalData) {
        for (int slot = 0; slot < 108; ++slot) {
            const CSharedCreatureData::AbnormalData& active = sharedData.m_AbnormalStatus[slot];
            if (active.m_Type == required.m_Type && active.m_cAbnormalLevel == required.m_cAbnormalLevel) {
                matchedSlots.push_back(slot);
                break;
            }
        }
    }
}

} // namespace

void CCreature::TriggerAlterSkill(AlterSkill::Event event, AbnormalStatusSlotType abnormalType) {
    // Current L2Server 0x0062afdc.
    L2_GUARD_FUNCTION();
    if (!IsPC() || event < AlterSkill::Critical || event >= AlterSkill::EventMax) {
        return;
    }

    TriggerAlterSkillInfo* triggerInfo = nullptr;
    {
        L2_GUARD(m_SkillMod.m_Lock);
        triggerInfo = m_SkillMod.m_TriggerAlterSkill[event];
    }
    if (triggerInfo == nullptr || (event == AlterSkill::Abnormal && triggerInfo->abnormalType != abnormalType) ||
        genrand64_real1() * 100.0 > triggerInfo->prob) {
        return;
    }

    AlterSkillInfo* const alterSkill = CSkillInfo::GetAlterSkillByProb(triggerInfo->skillList);
    if (alterSkill == nullptr || alterSkill->pSkillInfo == nullptr) {
        return;
    }

    CastUser()->GetSocket()->Send(
        "chddd", 0xfe, 0x10f, alterSkill->pSkillInfo->m_nSkillId, triggerInfo->oriSkillID, triggerInfo->expiredSec
    );
}

bool CCreature::CheckTriggerSkillCondition(CSkillInfo& skillInfo, CWorldObject& target) {
    L2_GUARD_FUNCTION();

    const int traitType = static_cast<int>(skillInfo.m_TraitType);
    if (0 < traitType && traitType < TRAIT_MAX && target.IsCreature()) {
        CCreature* const targetCreature = target.CastCreature();
        if (targetCreature != nullptr && targetCreature->m_bzDefendingTrait[traitType]) {
            return false;
        }
    }

    if (skillInfo.m_TargetType != STGT_SELF && skillInfo.m_nCastRange != -2) {
        const FVector sourcePosition = GetPos();
        const FVector targetPosition = target.GetPos();
        const double deltaX = sourcePosition.x - targetPosition.x;
        const double deltaY = sourcePosition.y - targetPosition.y;
        const double squaredRange =
            static_cast<double>(runtime::MultiplyInt32Wrapping(skillInfo.m_nCastRange, skillInfo.m_nCastRange));
        if (squaredRange < deltaX * deltaX + deltaY * deltaY) {
            return false;
        }
    }

    if (skillInfo.CheckOperateCond(this, &target, CSkillInfo::OCT_BOTH) != -2) {
        return false;
    }

    L2_GUARD(m_UsedTriggerSkillsLock);
    const auto used = m_UsedTriggerSkillLastTimeDB.find(skillInfo.m_nSkillId);
    if (used != m_UsedTriggerSkillLastTimeDB.end() &&
        CIOObject::IsInShortTime(static_cast<std::uint32_t>(used->second), 0)) {
        return false;
    }

    std::uint32_t timestamp = 0;
    CIOObject::SetShortTimeStamp(&timestamp, static_cast<std::uint32_t>(skillInfo.GetNormalReuseDelay(this)));
    m_UsedTriggerSkillLastTimeDB[skillInfo.m_nSkillId] = runtime::Int32FromBits(timestamp);
    return true;
}

int CCreature::TriggerSkillByDamage(CCreature& attacker, double damage) {
    L2_GUARD_FUNCTION();

    CCreatureSkillMod::SkillTriggerMap triggerMap;
    {
        L2_GUARD(m_SkillMod.m_Lock);
        if (m_SkillMod.m_SkillTriggerMap.empty()) {
            return 0;
        }
        triggerMap = m_SkillMod.m_SkillTriggerMap;
    }

    CCreatureSkillMod::SkillInterceptMap interceptMap;
    {
        L2_GUARD(m_SkillMod.m_InterceptSkillLock);
        interceptMap = m_SkillMod.m_SkillInterceptMap;
    }

    int triggered = 0;
    for (const auto& entry : triggerMap) {
        CSkillInfo* triggerSkill = entry.first;
        const CCreatureSkillMod::SkillTriggerInfo& info = entry.second;
        if (!info.m_bOn) {
            continue;
        }

        EnemyType enemyType = ENT_MAX;
        if (attacker.IsPC()) {
            enemyType = ENT_PLAYER;
        } else if (attacker.IsNPC()) {
            enemyType = attacker.IsBoss() ? ENT_BOSS : ENT_MONSTER;
        }

        if ((info.m_eEnemyType != ENT_ALL && info.m_eEnemyType != enemyType) ||
            !(info.m_nAttackerLevelMin < attacker.Level()) || !(attacker.Level() < info.m_nAttackerLevelMax) ||
            damage < m_data->GetHP() * info.m_nAttackedDamagePer + info.m_nAttackedDamageDiff ||
            !(::genrand64_real1() * 100.0 + 0.0 < static_cast<double>(info.m_Prob)) || triggerSkill == nullptr) {
            continue;
        }

        const CSkillKey triggerKey(triggerSkill->m_nSkillId, triggerSkill->m_nLevel, 0);
        const auto intercept = interceptMap.find(triggerKey);
        if (intercept == interceptMap.end()) {
            if (!CheckTriggerSkillCondition(*triggerSkill, attacker)) {
                continue;
            }
        } else {
            CSkillInfo* const interceptSkill = intercept->second.m_pInterceptSkill;
            if (interceptSkill == nullptr ||
                static_cast<double>(static_cast<int>(::genrand64_real2() * 101.0 + 0.0)) > intercept->second.m_dProb ||
                (interceptSkill->m_OperateType != SKILL_INSTANT && interceptSkill->m_OperateType != SKILL_DURATION) ||
                !CheckTriggerSkillCondition(*interceptSkill, attacker)) {
                continue;
            }
            triggerSkill = interceptSkill;
        }

        PushSerializedMessage(new CSerializedTriggerSkillMessage(this, &attacker, *triggerSkill, info.m_eTargetType));
        ++triggered;
    }
    return triggered;
}

int CCreature::TriggerSkillByAttack(CCreature& target, double damage, bool critical) {
    // Current L2Server 0x0062cef4.
    L2_GUARD_FUNCTION();

    CCreatureSkillMod::SkillAttackTriggerMap triggerMap;
    {
        L2_GUARD(m_SkillMod.m_Lock);
        if (m_SkillMod.m_SkillAttackTriggerMap.empty()) {
            return 0;
        }
        for (const auto& entry : m_SkillMod.m_SkillAttackTriggerMap) {
            triggerMap.emplace(entry.first, entry.second);
        }
    }

    CCreatureSkillMod::SkillInterceptMap interceptMap;
    {
        L2_GUARD(m_SkillMod.m_InterceptSkillLock);
        interceptMap = m_SkillMod.m_SkillInterceptMap;
    }

    int triggered = 0;
    for (const auto& entry : triggerMap) {
        CSkillInfo* triggerSkill = entry.first;
        const CCreatureSkillMod::SkillAttackTriggerInfo& info = entry.second;
        const bool criticalMatches = info.m_IsCritical > CriticalType::CTY_CRITICAL ||
            ((info.m_IsCritical == CriticalType::CTY_CRITICAL) == critical);
        if (!info.m_bOn || !criticalMatches) {
            continue;
        }

        double minimumDamage = static_cast<double>(info.m_MinimumDamage);
        if (info.m_Sfct != SFCT_DIFF) {
            minimumDamage = minimumDamage / 100.0 * m_data->m_dBaseHP;
        }
        if (damage < minimumDamage) {
            continue;
        }

        EnemyType enemyType = ENT_MAX;
        if (target.IsPC()) {
            enemyType = ENT_PLAYER;
        } else if (target.IsNPC()) {
            enemyType = target.IsBoss() ? ENT_BOSS : ENT_MONSTER;
        }
        if ((info.m_eEnemyType != ENT_ALL && info.m_eEnemyType != enemyType) || target.Level() < info.m_nTargetLevelMin ||
            info.m_nTargetLevelMax < target.Level() || static_cast<int>(::genrand64_real2() * 101.0) > info.m_Prob ||
            triggerSkill == nullptr) {
            continue;
        }

        const CSkillKey key(triggerSkill->m_nSkillId, triggerSkill->m_nLevel, 0);
        const auto intercept = interceptMap.find(key);
        if (intercept == interceptMap.end()) {
            if (!CheckTriggerSkillCondition(*triggerSkill, target)) {
                continue;
            }
        } else {
            CSkillInfo* const interceptSkill = intercept->second.m_pInterceptSkill;
            if (interceptSkill == nullptr ||
                static_cast<double>(static_cast<int>(::genrand64_real2() * 101.0)) > intercept->second.m_dProb ||
                (interceptSkill->m_OperateType != SKILL_INSTANT && interceptSkill->m_OperateType != SKILL_DURATION) ||
                !CheckTriggerSkillCondition(*interceptSkill, target)) {
                continue;
            }
            triggerSkill = interceptSkill;
        }

        PushSerializedMessage(new CSerializedTriggerSkillMessage(this, &target, *triggerSkill, info.m_eTargetType));
        ++triggered;
    }
    return triggered;
}

int CCreature::TriggerSkillByAvoid(CCreature& attacker) {
    // Current L2Server 0x0062d340.
    L2_GUARD_FUNCTION();

    CCreatureSkillMod::SkillAvoidTriggerMap triggerMap;
    {
        L2_GUARD(m_SkillMod.m_Lock);
        if (m_SkillMod.m_SkillAvoidTriggerMap.empty()) {
            return 0;
        }
        triggerMap = m_SkillMod.m_SkillAvoidTriggerMap;
    }

    CCreatureSkillMod::SkillInterceptMap interceptMap;
    {
        L2_GUARD(m_SkillMod.m_InterceptSkillLock);
        interceptMap = m_SkillMod.m_SkillInterceptMap;
    }

    int triggered = 0;
    for (const auto& entry : triggerMap) {
        CSkillInfo* triggerSkill = entry.first;
        const CCreatureSkillMod::SkillAvoidTriggerInfo& info = entry.second;
        if (!info.m_bOn || ::genrand64_real1() * 100.0 > static_cast<double>(info.m_Prob) || triggerSkill == nullptr) {
            continue;
        }

        const CSkillKey key(triggerSkill->m_nSkillId, triggerSkill->m_nLevel, 0);
        const auto intercept = interceptMap.find(key);
        if (intercept == interceptMap.end()) {
            if (!CheckTriggerSkillCondition(*triggerSkill, attacker)) {
                continue;
            }
        } else {
            CSkillInfo* const interceptSkill = intercept->second.m_pInterceptSkill;
            if (interceptSkill == nullptr ||
                static_cast<double>(static_cast<int>(::genrand64_real2() * 101.0)) > intercept->second.m_dProb ||
                (interceptSkill->m_OperateType != SKILL_INSTANT && interceptSkill->m_OperateType != SKILL_DURATION) ||
                !CheckTriggerSkillCondition(*interceptSkill, attacker)) {
                continue;
            }
            triggerSkill = interceptSkill;
        }

        PushSerializedMessage(new CSerializedTriggerSkillMessage(this, &attacker, *triggerSkill, info.m_eTargetType));
        ++triggered;
    }
    return triggered;
}

int CCreature::TriggerSkillByBlock(CCreature& attacker, int shieldDefense) {
    // Current L2Server 0x0062d6b8.
    L2_GUARD_FUNCTION();

    CCreatureSkillMod::SkillBlockTriggerMap triggerMap;
    {
        L2_GUARD(m_SkillMod.m_Lock);
        if (m_SkillMod.m_SkillBlockTriggerMap.empty()) {
            return 0;
        }
        triggerMap = m_SkillMod.m_SkillBlockTriggerMap;
    }

    CCreatureSkillMod::SkillInterceptMap interceptMap;
    {
        L2_GUARD(m_SkillMod.m_InterceptSkillLock);
        interceptMap = m_SkillMod.m_SkillInterceptMap;
    }

    int triggered = 0;
    for (const auto& entry : triggerMap) {
        CSkillInfo* triggerSkill = entry.first;
        const CCreatureSkillMod::SkillBlockTriggerInfo& info = entry.second;
        if (!info.m_bOn || shieldDefense != info.m_nShieldDefense ||
            ::genrand64_real1() * 100.0 > static_cast<double>(info.m_Prob) || triggerSkill == nullptr) {
            continue;
        }

        const CSkillKey key(triggerSkill->m_nSkillId, triggerSkill->m_nLevel, 0);
        const auto intercept = interceptMap.find(key);
        if (intercept == interceptMap.end()) {
            if (!CheckTriggerSkillCondition(*triggerSkill, attacker)) {
                continue;
            }
        } else {
            CSkillInfo* const interceptSkill = intercept->second.m_pInterceptSkill;
            if (interceptSkill == nullptr ||
                static_cast<double>(static_cast<int>(::genrand64_real2() * 101.0)) > intercept->second.m_dProb ||
                (interceptSkill->m_OperateType != SKILL_INSTANT && interceptSkill->m_OperateType != SKILL_DURATION) ||
                !CheckTriggerSkillCondition(*interceptSkill, attacker)) {
                continue;
            }
            triggerSkill = interceptSkill;
        }

        PushSerializedMessage(new CSerializedTriggerSkillMessage(this, &attacker, *triggerSkill, info.m_eTargetType));
        ++triggered;
    }
    return triggered;
}

int CCreature::TriggerSkillByDeath(CCreature& killer) {
    // Current L2Server 0x0062da48.
    L2_GUARD_FUNCTION();

    CCreatureSkillMod::SkillDeathTriggerMap triggerMap;
    {
        L2_GUARD(m_SkillMod.m_Lock);
        if (m_SkillMod.m_SkillDeathTriggerMap.empty()) {
            return 0;
        }
        triggerMap = m_SkillMod.m_SkillDeathTriggerMap;
    }

    CCreatureSkillMod::SkillInterceptMap interceptMap;
    {
        L2_GUARD(m_SkillMod.m_InterceptSkillLock);
        interceptMap = m_SkillMod.m_SkillInterceptMap;
    }

    int triggered = 0;
    for (const auto& entry : triggerMap) {
        CSkillInfo* const triggerSkill = entry.first;
        const CCreatureSkillMod::SkillDeathTriggerInfo& info = entry.second;
        if (!info.m_bOn) {
            continue;
        }

        EnemyType enemyType = ENT_MAX;
        if (killer.IsPC()) {
            enemyType = ENT_PLAYER;
        } else if (killer.IsNPC()) {
            enemyType = killer.IsBoss() ? ENT_BOSS : ENT_MONSTER;
        }
        if ((info.m_eEnemyType != ENT_ALL && info.m_eEnemyType != enemyType) ||
            !(info.m_nAttackerLevelMin < killer.Level()) || !(killer.Level() < info.m_nAttackerLevelMax) ||
            ::genrand64_real1() * 100.0 > static_cast<double>(info.m_Prob) || triggerSkill == nullptr) {
            continue;
        }

        const CSkillKey key(triggerSkill->m_nSkillId, triggerSkill->m_nLevel, 0);
        if (interceptMap.find(key) != interceptMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]cannot use p_trigger_skill_by_death "
            //     L"with p_intercept_skill.",
            //     kCreatureTriggerSourceFile,
            //     0x282)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d]cannot use p_trigger_skill_by_death "
            //     L"with p_intercept_skill.",
            //     kCreatureTriggerSourceFile,
            //     0x282)
            L2_LOG_ERROR(
                Log,
                "Creature object %u has death-trigger skill %d level %d in the intercept-skill map; the conflict is "
                "reported but trigger condition checking continues.",
                static_cast<unsigned>(m_nId), triggerSkill->m_nSkillId, triggerSkill->m_nLevel
            );
        }
        if (!CheckTriggerSkillCondition(*triggerSkill, killer)) {
            continue;
        }

        {
            L2_GUARD(m_InfectedSkillsSect);
            _DeleteAbnormalStatus(ABNS_KNIGHT_BUFF4, &killer, nullptr, true, SPET_TRIGGER, false);
        }
        PushSerializedMessage(new CSerializedTriggerSkillMessage(this, &killer, *triggerSkill, info.m_eTargetType));
        ++triggered;
    }
    return triggered;
}

int CCreature::TriggerSkillByKill(CObject& killedObject) {
    // Current L2Server 0x0062deb4.
    L2_GUARD_FUNCTION();

    CCreatureSkillMod::SkillKillTriggerMap triggerMap;
    {
        L2_GUARD(m_SkillMod.m_Lock);
        if (m_SkillMod.m_SkillKillTriggerMap.empty()) {
            return 0;
        }
        triggerMap = m_SkillMod.m_SkillKillTriggerMap;
    }

    CCreatureSkillMod::SkillInterceptMap interceptMap;
    {
        L2_GUARD(m_SkillMod.m_InterceptSkillLock);
        interceptMap = m_SkillMod.m_SkillInterceptMap;
    }

    int triggered = 0;
    for (const auto& entry : triggerMap) {
        CSkillInfo* triggerSkill = entry.first;
        const CCreatureSkillMod::SkillKillTriggerInfo& info = entry.second;
        if (!info.m_bOn) {
            continue;
        }

        EnemyType enemyType = ENT_MAX;
        if (killedObject.IsPC()) {
            enemyType = ENT_PLAYER;
        } else if (killedObject.IsNPC()) {
            enemyType = killedObject.IsBoss() ? ENT_BOSS : ENT_MONSTER;
        }
        if ((info.m_enemyType != ENT_ALL && info.m_enemyType != enemyType) ||
            ::genrand64_real1() * 100.0 > static_cast<double>(info.m_Prob) || triggerSkill == nullptr) {
            continue;
        }

        const CSkillKey key(triggerSkill->m_nSkillId, triggerSkill->m_nLevel, 0);
        const auto intercept = interceptMap.find(key);
        if (intercept == interceptMap.end()) {
            if (!CheckTriggerSkillCondition(*triggerSkill, *this)) {
                continue;
            }
        } else {
            CSkillInfo* const interceptSkill = intercept->second.m_pInterceptSkill;
            if (interceptSkill == nullptr ||
                static_cast<double>(static_cast<int>(::genrand64_real2() * 101.0)) > intercept->second.m_dProb ||
                (interceptSkill->m_OperateType != SKILL_INSTANT && interceptSkill->m_OperateType != SKILL_DURATION) ||
                !CheckTriggerSkillCondition(*interceptSkill, *this)) {
                continue;
            }
            triggerSkill = interceptSkill;
        }

        PushSerializedMessage(new CSerializedTriggerSkillMessage(this, this, *triggerSkill, info.m_eTargetType));
        ++triggered;
    }
    return triggered;
}

int CCreature::TriggerSkillBySkill(int skillId, CWorldObject& target, bool critical) {
    // Current L2Server 0x0062eb18.
    L2_GUARD_FUNCTION();

    const CCreatureSkillMod::SkillSkillTriggerMap triggerMap = [&] {
        L2_GUARD(m_SkillMod.m_Lock);
        return m_SkillMod.m_SkillSkillTriggerMap;
    }();
    if (triggerMap.empty()) {
        return 0;
    }

    CCreatureSkillMod::SkillInterceptMap interceptMap;
    {
        L2_GUARD(m_SkillMod.m_InterceptSkillLock);
        interceptMap = m_SkillMod.m_SkillInterceptMap;
    }

    int triggered = 0;
    for (const auto& entry : triggerMap) {
        CSkillInfo* triggerSkill = entry.first;
        const CCreatureSkillMod::SkillSkillTriggerInfo& info = entry.second;

        bool matchesSkill = false;
        for (const int configuredSkillId : info.m_skillIDList) {
            if (configuredSkillId == skillId) {
                matchesSkill = true;
                break;
            }
        }
        if (!info.m_bOn || !matchesSkill || (info.m_bCheckCritical && !critical)) {
            continue;
        }

        if (info.m_eEnemyType != ENT_MAX) {
            EnemyType targetType = ENT_MAX;
            if (target.IsPC()) {
                targetType = ENT_PLAYER;
            } else if (target.IsNPC()) {
                targetType = target.IsBoss() ? ENT_BOSS : ENT_MONSTER;
            }
            if ((info.m_eEnemyType != ENT_ALL && info.m_eEnemyType != targetType) ||
                target.Level() < info.m_nTargetLevelMin || info.m_nTargetLevelMax < target.Level()) {
                continue;
            }
        }

        if (::genrand64_real1() * 100.0 > static_cast<double>(info.m_Prob) || triggerSkill == nullptr) {
            continue;
        }

        const CSkillKey triggerKey(triggerSkill->m_nSkillId, triggerSkill->m_nLevel, 0);
        const auto intercept = interceptMap.find(triggerKey);
        if (intercept == interceptMap.end()) {
            if (!CheckTriggerSkillCondition(*triggerSkill, target)) {
                continue;
            }
        } else {
            CSkillInfo* const interceptSkill = intercept->second.m_pInterceptSkill;
            if (interceptSkill == nullptr ||
                static_cast<double>(static_cast<int>(::genrand64_real2() * 101.0)) > intercept->second.m_dProb ||
                (interceptSkill->m_OperateType != SKILL_INSTANT && interceptSkill->m_OperateType != SKILL_DURATION) ||
                !CheckTriggerSkillCondition(*interceptSkill, target)) {
                continue;
            }
            triggerSkill = interceptSkill;
        }

        PushSerializedMessage(new CSerializedTriggerSkillMessage(this, &target, *triggerSkill, info.m_eTargetType));
        ++triggered;
    }
    return triggered;
}

int CCreature::TriggerSkillByMagicType(int magicType, CWorldObject& target, bool harmful, bool multiTarget) {
    // Current L2Server 0x0062f7e8.
    L2_GUARD_FUNCTION();

    const CCreatureSkillMod::SkillMagicTypeTriggerMap triggerMap = [&] {
        L2_GUARD(m_SkillMod.m_Lock);
        return m_SkillMod.m_SkillMagicTypeTriggerMap;
    }();
    if (triggerMap.empty()) {
        return 0;
    }

    CCreatureSkillMod::SkillInterceptMap interceptMap;
    {
        L2_GUARD(m_SkillMod.m_InterceptSkillLock);
        interceptMap = m_SkillMod.m_SkillInterceptMap;
    }

    int triggered = 0;
    for (const auto& entry : triggerMap) {
        CSkillInfo* triggerSkill = entry.first;
        const CCreatureSkillMod::SkillMagicTypeTriggerInfo& info = entry.second;
        if (!info.m_bOn ||
            (info.m_sMagicType.find(-1) == info.m_sMagicType.end() &&
                info.m_sMagicType.find(magicType) == info.m_sMagicType.end()) ||
            (multiTarget && info.m_eTargetType != STGT_SELF && info.m_eTargetType != STGT_MY_PARTY)) {
            continue;
        }

        if (info.m_nGoodSkill != BadSKillType::BST_ALL) {
            const BadSKillType::Enum actualType = harmful ? BadSKillType::BST_BAD : BadSKillType::BST_GOOD;
            if (info.m_nGoodSkill != actualType) {
                continue;
            }
        }

        if (::genrand64_real1() * 100.0 > static_cast<double>(info.m_Prob) || triggerSkill == nullptr) {
            continue;
        }

        const CSkillKey triggerKey(triggerSkill->m_nSkillId, triggerSkill->m_nLevel, 0);
        const auto intercept = interceptMap.find(triggerKey);
        if (intercept == interceptMap.end()) {
            if (!CheckTriggerSkillCondition(*triggerSkill, target)) {
                continue;
            }
        } else {
            CSkillInfo* const interceptSkill = intercept->second.m_pInterceptSkill;
            if (interceptSkill == nullptr ||
                static_cast<double>(static_cast<int>(::genrand64_real2() * 101.0)) > intercept->second.m_dProb ||
                (interceptSkill->m_OperateType != SKILL_INSTANT && interceptSkill->m_OperateType != SKILL_DURATION) ||
                !CheckTriggerSkillCondition(*interceptSkill, target)) {
                continue;
            }
            triggerSkill = interceptSkill;
        }

        PushSerializedMessage(new CSerializedTriggerSkillMessage(this, &target, *triggerSkill, info.m_eTargetType));
        ++triggered;
    }
    return triggered;
}

int CCreature::TriggerSkillByAttackedSkill(int skillId, CCreature* attacker) {
    // Current L2Server 0x0062fc38.
    L2_GUARD_FUNCTION();

    const CCreatureSkillMod::SkillAttackedSkillTriggerMap triggerMap = [&] {
        L2_GUARD(m_SkillMod.m_Lock);
        return m_SkillMod.m_SkillAttackedSkillTriggerMap;
    }();
    if (triggerMap.empty()) {
        return 0;
    }

    CCreatureSkillMod::SkillInterceptMap interceptMap;
    {
        L2_GUARD(m_SkillMod.m_InterceptSkillLock);
        interceptMap = m_SkillMod.m_SkillInterceptMap;
    }

    int triggered = 0;
    for (const auto& entry : triggerMap) {
        CSkillInfo* triggerSkill = entry.first;
        const CCreatureSkillMod::SkillAttackedSkillTriggerInfo& info = entry.second;
        if (!info.m_bOn || info.m_sSkillId.find(skillId) == info.m_sSkillId.end() ||
            ::genrand64_real1() * 100.0 > static_cast<double>(info.m_Prob) || triggerSkill == nullptr) {
            continue;
        }

        const CSkillKey triggerKey(triggerSkill->m_nSkillId, triggerSkill->m_nLevel, 0);
        const auto intercept = interceptMap.find(triggerKey);
        if (intercept == interceptMap.end()) {
            if (!CheckTriggerSkillCondition(*triggerSkill, *attacker)) {
                continue;
            }
        } else {
            CSkillInfo* const interceptSkill = intercept->second.m_pInterceptSkill;
            if (interceptSkill == nullptr ||
                static_cast<double>(static_cast<int>(::genrand64_real2() * 101.0)) > intercept->second.m_dProb ||
                (interceptSkill->m_OperateType != SKILL_INSTANT && interceptSkill->m_OperateType != SKILL_DURATION) ||
                !CheckTriggerSkillCondition(*interceptSkill, *attacker)) {
                continue;
            }
            triggerSkill = interceptSkill;
        }

        PushSerializedMessage(new CSerializedTriggerSkillMessage(this, attacker, *triggerSkill, info.m_eTargetType));
        ++triggered;
    }
    return triggered;
}

int CCreature::_ChangeSkillByAbnormalSlot(CCreature* source, CWorldObject& target) {
    L2_GUARD_FUNCTION();

    CCreatureSkillMod::SkillAbnormalSlotChangeMap triggerMap;
    {
        L2_GUARD(m_SkillMod.m_Lock);
        if (source->m_SkillMod.m_SkillAbnormalSlotChangeMap.empty()) {
            return 0;
        }
        triggerMap = source->m_SkillMod.m_SkillAbnormalSlotChangeMap;
    }

    // Current 0x0062f180 constructs this vector once. It does
    // not clear matches when advancing to the next map entry.
    std::vector<int> matchedSlots;
    for (const auto& entry : triggerMap) {
        const CCreatureSkillMod::SkillAbnormalSlotChangeInfo& info = entry.second;
        if (!info.m_bOn) {
            continue;
        }

        FindMatchingAbnormalSlots(*m_data, info, matchedSlots);
        if (matchedSlots.size() != info.m_vAbnormalData.size()) {
            continue;
        }

        CSkillInfo* const triggerSkill = entry.first;
        if (triggerSkill == nullptr || !CheckTriggerSkillCondition(*triggerSkill, target)) {
            return 0;
        }

        PushSerializedMessage(new CSerializedTriggerSkillMessage(source, &target, *triggerSkill, info.m_eTargetType));
        for (const int slot : matchedSlots) {
            _DeleteAbnormalStatus(m_data->m_AbnormalStatus[slot].m_Type, nullptr, nullptr, true, SPET_TRIGGER, false);
        }
        return 1;
    }
    return 0;
}

int CCreature::_TriggerSkillByAbnormalSlot(CCreature* source, CWorldObject& target) {
    L2_GUARD_FUNCTION();

    CCreatureSkillMod::SkillAbnormalSlotTriggerMap triggerMap;
    {
        L2_GUARD(m_SkillMod.m_Lock);
        if (source->m_SkillMod.m_SkillAbnormalSlotTriggerMap.empty()) {
            return 0;
        }
        triggerMap = source->m_SkillMod.m_SkillAbnormalSlotTriggerMap;
    }

    // Current 0x0062f490 likewise keeps matches across map entries.
    std::vector<int> matchedSlots;
    for (const auto& entry : triggerMap) {
        const CCreatureSkillMod::SkillAbnormalSlotTriggerInfo& info = entry.second;
        if (!info.m_bOn) {
            continue;
        }

        EnemyType enemyType = ENT_MAX;
        if (target.IsPC()) {
            enemyType = ENT_PLAYER;
        } else if (target.IsNPC()) {
            enemyType = target.IsBoss() ? ENT_BOSS : ENT_MONSTER;
        }

        if (info.m_eEnemyType != ENT_ALL && info.m_eEnemyType != enemyType) {
            continue;
        }
        if (!(info.m_nAttackerLevelMin < target.Level()) || !(target.Level() < info.m_nAttackerLevelMax)) {
            continue;
        }

        FindMatchingAbnormalSlots(*m_data, info, matchedSlots);
        if (matchedSlots.size() != info.m_vAbnormalData.size()) {
            continue;
        }

        CSkillInfo* const triggerSkill = entry.first;
        if (triggerSkill == nullptr || !CheckTriggerSkillCondition(*triggerSkill, target)) {
            return 0;
        }

        PushSerializedMessage(new CSerializedTriggerSkillMessage(source, &target, *triggerSkill, info.m_eTargetType));
        return 1;
    }
    return 0;
}
