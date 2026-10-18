#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include "Area.h"
#include "BalthusEvent.h"
#include "CWorld.h"
#include "CursedWeapon.h"
#include "Fishing.h"
#include "Item.h"
#include "KalieEvent.h"
#include "L2Time.h"
#include "NPC.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "Party.h"
#include "Pet.h"
#include "Protocol/src/NPCProtocol.h"
#include "Protocol/src/SystemMessageIds.h"
#include "SerializedMessage.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "Shared/src/Vitality.h"
#include "SkillFxFunc.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"
#include "WorldCollision.h"

namespace {

void CopyTriggerSkillName(char16_t (&destination)[256], const char16_t* source) noexcept {
    if (source == nullptr) {
        destination[0] = u'\0';
        return;
    }

    std::size_t length = 0;
    while (length < 256 && source[length] != u'\0') {
        ++length;
    }
    if (length == 256) {
        destination[0] = u'\0';
        return;
    }
    std::copy_n(source, length + 1, destination);
}


} // namespace

bool CSkillEffect_p_block_act_bonus::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aba320.
    L2_GUARD_FUNCTION();
    const CCreatureSkillMod::ModName mod =
        m_TargetType == 0 ? CCreatureSkillMod::BlockActBonusSelf : CCreatureSkillMod::BlockActBonusTarget;
    L2SkillFunc::AddBonus(creature, mod, m_Sfct, m_Bonus);
    return true;
}

double CSkillEffect_p_block_act_bonus::CalcDamage(CCreature& attacker, CCreature& target, double damage) {
    // Current L2Server 0x00aba3e0.
    L2_GUARD_FUNCTION();
    if (!target.m_SkillMod.m_bCanAct && !target.m_data->m_bCanAct) {
        damage = damage * attacker.m_SkillMod.m_Mods[CCreatureSkillMod::BlockActBonusTarget].m_Per *
                target.m_SkillMod.m_Mods[CCreatureSkillMod::BlockActBonusSelf].m_Per +
            attacker.m_SkillMod.m_Mods[CCreatureSkillMod::BlockActBonusTarget].m_Diff +
            target.m_SkillMod.m_Mods[CCreatureSkillMod::BlockActBonusSelf].m_Diff;
    }
    return damage;
}

CSkillEffect_p_trigger_skill_by_dmg::CSkillEffect_p_trigger_skill_by_dmg(
    const EnemyType enemyType,
    const int attackerLevelMin,
    const int attackerLevelMax,
    const int attackedDamage,
    const int probability,
    const SkillFxChangeType changeType,
    const char16_t* skillName,
    const SkillTargetTypeEnum targetType
) :
    m_eEnemyType(enemyType),
    m_nAttackerLevelMin(attackerLevelMin),
    m_nAttackerLevelMax(attackerLevelMax),
    m_nAttackedDamage(attackedDamage),
    m_nProbability(probability),
    m_Sfct(changeType),
    m_SkillName{},
    m_eTargetType(targetType),
    m_pSkillInfo(nullptr) {
    // Current L2Server 0x00ac65f4.
    CopyTriggerSkillName(m_SkillName, skillName);
}

void CSkillEffect_p_trigger_skill_by_dmg::OnPostLoad() {
    // Current L2Server 0x00ab3cb8.
    m_pSkillInfo = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(target_utf16::ToHostCodeUnits(m_SkillName).c_str()));
}

bool CSkillEffect_p_trigger_skill_by_dmg::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac31e0.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    const auto found = creature->m_SkillMod.m_SkillTriggerMap.find(m_pSkillInfo);
    if (found == creature->m_SkillMod.m_SkillTriggerMap.end()) {
        return false;
    }
    found->second.m_bOn = true;
    return true;
}

bool CSkillEffect_p_trigger_skill_by_dmg::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ac99b8.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    CCreatureSkillMod::SkillTriggerInfo trigger(m_nProbability, m_eTargetType);
    L2SkillFunc::AddBonus(
        &trigger.m_nAttackedDamagePer, &trigger.m_nAttackedDamageDiff, m_Sfct, static_cast<double>(m_nAttackedDamage)
    );
    trigger.m_nAttackerLevelMin = m_nAttackerLevelMin;
    trigger.m_nAttackerLevelMax = m_nAttackerLevelMax;
    trigger.m_eEnemyType = m_eEnemyType;

    L2_GUARD(creature.m_SkillMod.m_Lock);
    creature.m_SkillMod.m_SkillTriggerMap.emplace(m_pSkillInfo, trigger);
    return true;
}

bool CSkillEffect_p_trigger_skill_by_dmg::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac8c50.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    const auto found = creature.m_SkillMod.m_SkillTriggerMap.find(m_pSkillInfo);
    if (found == creature.m_SkillMod.m_SkillTriggerMap.end()) {
        return false;
    }
    creature.m_SkillMod.m_SkillTriggerMap.erase(found);
    return true;
}

CSkillEffect_p_trigger_skill_by_attack::CSkillEffect_p_trigger_skill_by_attack(
    const EnemyType enemyType,
    const int targetLevelMin,
    const int targetLevelMax,
    const CriticalType::Enum isCritical,
    const int minimumDamage,
    const int probability,
    const SkillFxChangeType changeType,
    const char16_t* skillName,
    const SkillTargetTypeEnum targetType
) :
    m_eEnemyType(enemyType),
    m_nTargetLevelMin(targetLevelMin),
    m_nTargetLevelMax(targetLevelMax),
    m_IsCritical(isCritical),
    m_MinimumDamage(minimumDamage),
    m_Prob(probability),
    m_Sfct(changeType),
    m_SkillName{},
    m_eTargetType(targetType),
    m_pSkillInfo(nullptr) {
    // Current L2Server 0x00ac6680.
    CopyTriggerSkillName(m_SkillName, skillName);
}

void CSkillEffect_p_trigger_skill_by_attack::OnPostLoad() {
    // Current L2Server 0x00ab3ce0.
    m_pSkillInfo = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(target_utf16::ToHostCodeUnits(m_SkillName).c_str()));
}

bool CSkillEffect_p_trigger_skill_by_attack::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac9cdc.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    auto found = creature->m_SkillMod.m_SkillAttackTriggerMap.find(m_pSkillInfo);
    if (found == creature->m_SkillMod.m_SkillAttackTriggerMap.end()) {
        CCreatureSkillMod::SkillAttackTriggerInfo trigger(
            m_eEnemyType, m_nTargetLevelMin, m_nTargetLevelMax, m_IsCritical, m_MinimumDamage, m_Prob, m_Sfct, m_eTargetType,
            true
        );
        creature->m_SkillMod.m_SkillAttackTriggerMap.emplace(m_pSkillInfo, trigger);
    } else {
        found->second.m_bOn = true;
    }
    return true;
}

bool CSkillEffect_p_trigger_skill_by_attack::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ac9b50.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    CCreatureSkillMod::SkillAttackTriggerInfo trigger(
        m_eEnemyType, m_nTargetLevelMin, m_nTargetLevelMax, m_IsCritical, m_MinimumDamage, m_Prob, m_Sfct, m_eTargetType,
        false
    );
    L2_GUARD(creature.m_SkillMod.m_Lock);
    creature.m_SkillMod.m_SkillAttackTriggerMap.emplace(m_pSkillInfo, trigger);
    return true;
}

bool CSkillEffect_p_trigger_skill_by_attack::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac8dd4.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    const auto found = creature.m_SkillMod.m_SkillAttackTriggerMap.find(m_pSkillInfo);
    if (found == creature.m_SkillMod.m_SkillAttackTriggerMap.end()) {
        return false;
    }
    creature.m_SkillMod.m_SkillAttackTriggerMap.erase(found);
    return true;
}

CSkillEffectTrigger::CSkillEffectTrigger(
    const int probability, const char16_t* calledSkillName, const SkillTargetTypeEnum targetType
) :
    m_Prob(probability), m_CalledSkillName(calledSkillName), m_eTargetType(targetType), m_pSkillInfo(nullptr) {
    // Current L2Server 0x00ac67fc.
}

CSkillEffectTrigger::~CSkillEffectTrigger() = default;

void CSkillEffectTrigger::OnPostLoad() {
    // Current L2Server 0x00abf638.
    m_pSkillInfo = const_cast<CSkillInfo*>(
        g_SkillDB.GetSkillInfoByName(target_utf16::ToHostCodeUnits(m_CalledSkillName.c_str()).c_str())
    );
}

CCreatureSkillMod::SkillSkillTriggerInfo::SkillSkillTriggerInfo(
    const std::vector<int>* skillIdList,
    const int probability,
    const SkillTargetTypeEnum targetType,
    const bool checkCritical,
    const EnemyType enemyType,
    const int targetLevelMin,
    const int targetLevelMax
) :
    SkillBaseTriggerInfo(probability, targetType),
    m_skillIDList(*skillIdList),
    m_bCheckCritical(checkCritical),
    m_eEnemyType(enemyType),
    m_nTargetLevelMin(targetLevelMin),
    m_nTargetLevelMax(targetLevelMax) {}

CCreatureSkillMod::SkillSkillTriggerInfo::SkillSkillTriggerInfo(
    const std::vector<int>* skillIdList,
    const int probability,
    const SkillTargetTypeEnum targetType,
    const bool checkCritical
) :
    SkillSkillTriggerInfo(skillIdList, probability, targetType, checkCritical, ENT_MAX, 0, 120) {}

CCreatureSkillMod::SkillAvoidTriggerInfo::SkillAvoidTriggerInfo(
    const int probability, const SkillTargetTypeEnum targetType
) :
    SkillBaseTriggerInfo(probability, targetType) {}

CCreatureSkillMod::SkillDeathTriggerInfo::SkillDeathTriggerInfo(
    const int probability, const SkillTargetTypeEnum targetType
) :
    SkillBaseTriggerInfo(probability, targetType), m_nAttackerLevelMin(0), m_nAttackerLevelMax(0), m_eEnemyType(ENT_MAX) {}

CCreatureSkillMod::SkillKillTriggerInfo::SkillKillTriggerInfo(
    const EnemyType enemyType, const int probability, const SkillTargetTypeEnum targetType
) :
    SkillBaseTriggerInfo(probability, targetType), m_enemyType(enemyType) {}

CCreatureSkillMod::SkillBlockTriggerInfo::SkillBlockTriggerInfo(
    const EnemyType enemyType,
    const int attackerLevelMin,
    const int attackerLevelMax,
    const int skillId,
    const int shieldDefense,
    const int probability,
    const SkillTargetTypeEnum targetType
) :
    SkillBaseTriggerInfo(probability, targetType),
    m_eEnemyType(enemyType),
    m_nAttackerLevelMin(attackerLevelMin),
    m_nAttackerLevelMax(attackerLevelMax),
    m_nSkillId(skillId),
    m_nShieldDefense(shieldDefense) {}

CSkillEffect_p_trigger_skill_by_kill::CSkillEffect_p_trigger_skill_by_kill(
    const EnemyType enemyType, const int probability, const char16_t* skillName
) :
    m_eEnemyType(enemyType), m_Prob(probability), m_SkillName{}, m_pSkillInfo(nullptr) {
    // Current L2Server 0x00ac6714.
    CopyTriggerSkillName(m_SkillName, skillName);
}

void CSkillEffect_p_trigger_skill_by_kill::OnPostLoad() {
    // Current L2Server 0x00ab3d08.
    m_pSkillInfo = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(target_utf16::ToHostCodeUnits(m_SkillName).c_str()));
}

bool CSkillEffect_p_trigger_skill_by_kill::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac34a0.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    const auto found = creature->m_SkillMod.m_SkillKillTriggerMap.find(m_pSkillInfo);
    if (found == creature->m_SkillMod.m_SkillKillTriggerMap.end()) {
        return false;
    }
    found->second.m_bOn = true;
    return true;
}

bool CSkillEffect_p_trigger_skill_by_kill::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ac9eb0.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    CCreatureSkillMod::SkillKillTriggerInfo trigger(m_eEnemyType, m_Prob, STGT_SELF);
    creature.m_SkillMod.m_SkillKillTriggerMap.emplace(m_pSkillInfo, std::move(trigger));
    return true;
}

bool CSkillEffect_p_trigger_skill_by_kill::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac8f58.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    const auto found = creature.m_SkillMod.m_SkillKillTriggerMap.find(m_pSkillInfo);
    if (found == creature.m_SkillMod.m_SkillKillTriggerMap.end()) {
        return false;
    }
    creature.m_SkillMod.m_SkillKillTriggerMap.erase(found);
    return true;
}

CSkillEffect_p_trigger_skill_by_avoid::CSkillEffect_p_trigger_skill_by_avoid(
    const int probability, const char16_t* calledSkillName, const SkillTargetTypeEnum targetType
) :
    CSkillEffectTrigger(probability, calledSkillName, targetType) {
    // Current L2Server 0x00ac68e0.
}

bool CSkillEffect_p_trigger_skill_by_avoid::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac38bc.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    const auto found = creature->m_SkillMod.m_SkillAvoidTriggerMap.find(m_pSkillInfo);
    if (found == creature->m_SkillMod.m_SkillAvoidTriggerMap.end()) {
        return false;
    }
    found->second.m_bOn = true;
    return true;
}

bool CSkillEffect_p_trigger_skill_by_avoid::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00aca180.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    CCreatureSkillMod::SkillAvoidTriggerInfo trigger(m_Prob, m_eTargetType);
    creature.m_SkillMod.m_SkillAvoidTriggerMap.emplace(m_pSkillInfo, std::move(trigger));
    return true;
}

bool CSkillEffect_p_trigger_skill_by_avoid::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac92c0.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    const auto found = creature.m_SkillMod.m_SkillAvoidTriggerMap.find(m_pSkillInfo);
    if (found == creature.m_SkillMod.m_SkillAvoidTriggerMap.end()) {
        return false;
    }
    creature.m_SkillMod.m_SkillAvoidTriggerMap.erase(found);
    return true;
}

CSkillEffect_p_trigger_skill_by_block::CSkillEffect_p_trigger_skill_by_block(
    const EnemyType enemyType,
    const int attackerLevelMin,
    const int attackerLevelMax,
    const int shieldDefence,
    const int probability,
    const char16_t* calledSkillName,
    const SkillTargetTypeEnum targetType
) :
    CSkillEffectTrigger(probability, calledSkillName, targetType),
    m_eEnemyType(enemyType),
    m_nAttackerLevelMin(attackerLevelMin),
    m_nAttackerLevelMax(attackerLevelMax),
    m_nShieldDefence(shieldDefence) {
    // Current L2Server 0x00ac90dc.
}

bool CSkillEffect_p_trigger_skill_by_block::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac3604.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    const auto found = creature->m_SkillMod.m_SkillBlockTriggerMap.find(m_pSkillInfo);
    if (found == creature->m_SkillMod.m_SkillBlockTriggerMap.end()) {
        return false;
    }
    found->second.m_bOn = true;
    return true;
}

bool CSkillEffect_p_trigger_skill_by_block::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00aca00c.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    CCreatureSkillMod::SkillBlockTriggerInfo trigger(
        m_eEnemyType, m_nAttackerLevelMin, m_nAttackerLevelMax, 0, m_nShieldDefence, m_Prob, m_eTargetType
    );
    creature.m_SkillMod.m_SkillBlockTriggerMap.emplace(m_pSkillInfo, std::move(trigger));
    return true;
}

bool CSkillEffect_p_trigger_skill_by_block::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac9144.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    const auto found = creature.m_SkillMod.m_SkillBlockTriggerMap.find(m_pSkillInfo);
    if (found == creature.m_SkillMod.m_SkillBlockTriggerMap.end()) {
        return false;
    }
    creature.m_SkillMod.m_SkillBlockTriggerMap.erase(found);
    return true;
}

CSkillEffect_p_trigger_skill_by_death::CSkillEffect_p_trigger_skill_by_death(
    const EnemyType enemyType,
    const int attackerLevelMin,
    const int attackerLevelMax,
    const int probability,
    const char16_t* calledSkillName,
    const SkillTargetTypeEnum targetType
) :
    CSkillEffectTrigger(probability, calledSkillName, targetType),
    m_eEnemyType(enemyType),
    m_nAttackerLevelMin(attackerLevelMin),
    m_nAttackerLevelMax(attackerLevelMax) {
    // Current L2Server 0x00ac6904.
}

bool CSkillEffect_p_trigger_skill_by_death::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac3a18.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    const auto found = creature->m_SkillMod.m_SkillDeathTriggerMap.find(m_pSkillInfo);
    if (found == creature->m_SkillMod.m_SkillDeathTriggerMap.end()) {
        return false;
    }
    found->second.m_bOn = true;
    return true;
}

bool CSkillEffect_p_trigger_skill_by_death::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00aca2d4.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    CCreatureSkillMod::SkillDeathTriggerInfo trigger(m_Prob, m_eTargetType);
    trigger.m_nAttackerLevelMin = m_nAttackerLevelMin;
    trigger.m_nAttackerLevelMax = m_nAttackerLevelMax;
    trigger.m_eEnemyType = m_eEnemyType;
    L2_GUARD(creature.m_SkillMod.m_Lock);
    creature.m_SkillMod.m_SkillDeathTriggerMap.emplace(m_pSkillInfo, std::move(trigger));
    return true;
}

bool CSkillEffect_p_trigger_skill_by_death::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac943c.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    const auto found = creature.m_SkillMod.m_SkillDeathTriggerMap.find(m_pSkillInfo);
    if (found == creature.m_SkillMod.m_SkillDeathTriggerMap.end()) {
        return false;
    }
    creature.m_SkillMod.m_SkillDeathTriggerMap.erase(found);
    return true;
}

CSkillEffect_p_trigger_skill_by_skill::CSkillEffect_p_trigger_skill_by_skill(
    std::vector<std::int64_t>* skillIds,
    const int probability,
    const char16_t* calledSkillName,
    const SkillTargetTypeEnum targetType
) :
    CSkillEffectTrigger(probability, calledSkillName, targetType),
    m_bCheckCritical(false),
    m_EnemyType(ENT_MAX),
    m_TargetMinLevel(0),
    m_TargetMaxLevel(120) {
    // Current L2Server 0x00acab40.
    for (const std::int64_t skillId : *skillIds) {
        m_skillIDList.push_back(static_cast<int>(skillId));
    }
}

CSkillEffect_p_trigger_skill_by_skill::CSkillEffect_p_trigger_skill_by_skill(
    std::vector<std::int64_t>* skillIds,
    const int probability,
    const char16_t* calledSkillName,
    const SkillTargetTypeEnum targetType,
    const EnemyType enemyType,
    const int targetLevelMin,
    const int targetLevelMax
) :
    CSkillEffectTrigger(probability, calledSkillName, targetType),
    m_bCheckCritical(false),
    m_EnemyType(enemyType),
    m_TargetMinLevel(targetLevelMin),
    m_TargetMaxLevel(targetLevelMax) {
    // Current L2Server 0x00acabcc.
    for (const std::int64_t skillId : *skillIds) {
        m_skillIDList.push_back(static_cast<int>(skillId));
    }
}

bool CSkillEffect_p_trigger_skill_by_skill::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac3760.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    const auto found = creature->m_SkillMod.m_SkillSkillTriggerMap.find(m_pSkillInfo);
    if (found == creature->m_SkillMod.m_SkillSkillTriggerMap.end()) {
        return false;
    }
    found->second.m_bOn = true;
    return true;
}

bool CSkillEffect_p_trigger_skill_by_skill::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00acbef4.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    CCreatureSkillMod::SkillSkillTriggerInfo trigger(
        &m_skillIDList, m_Prob, m_eTargetType, m_bCheckCritical, m_EnemyType, m_TargetMinLevel, m_TargetMaxLevel
    );
    creature.m_SkillMod.m_SkillSkillTriggerMap.emplace(m_pSkillInfo, std::move(trigger));
    return true;
}

bool CSkillEffect_p_trigger_skill_by_skill::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00acbaac.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    const auto found = creature.m_SkillMod.m_SkillSkillTriggerMap.find(m_pSkillInfo);
    if (found == creature.m_SkillMod.m_SkillSkillTriggerMap.end()) {
        return false;
    }
    creature.m_SkillMod.m_SkillSkillTriggerMap.erase(found);
    return true;
}

CSkillEffect_p_ability_change::CSkillEffect_p_ability_change() : m_EnableStatBonus{} {
    // Current L2Server 0x00ca275c leaves m_BonusRate
    // indeterminate until the generated parser enables an entry.
}

bool CSkillEffect_p_ability_change::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab6a04.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const user = creature->CastUser();
    for (int index = 0; index < 13; ++index) {
        if (m_EnableStatBonus[index]) {
            user->m_SkillMod.m_ModSummonBonus[index] = static_cast<double>(m_BonusRate[index]);
        }
    }
    return true;
}

bool CSkillEffect_p_ability_change::PumpEnd(CCreature&, CCreature* source, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ac6b9c.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !source->IsPC()) {
        return false;
    }

    User* const user = source->CastUser();
    std::memset(user->m_SkillMod.m_ModSummonBonus, 0, sizeof(user->m_SkillMod.m_ModSummonBonus));

    FOR_ALL_SUMMON_IN_SUMMON_LIST(user, summon, summon->ValidateChanged(VB_MAX););
    return true;
}

void CSkillEffect_p_ability_change::SetEnableStat(const int index, const int rate) {
    // Current L2Server 0x00ab6b00.
    L2_GUARD_FUNCTION();
    m_EnableStatBonus[index] = true;
    m_BonusRate[index] = rate;
}

CCreatureSkillMod::SkillAbnormalSlotChangeInfo::SkillAbnormalSlotChangeInfo(
    std::vector<AbnormalDataForTrigger> abnormalData, const int probability, const SkillTargetTypeEnum targetType
) :
    SkillBaseTriggerInfo(probability, targetType), m_vAbnormalData(std::move(abnormalData)) {
    // Current L2Server 0x00aca4d4.
}

CSkillEffect_p_change_slot::CSkillEffect_p_change_slot(
    std::vector<AbnormalDataForTrigger> abnormalData, const char16_t* skillName
) :
    CSkillEffectTrigger(100, skillName, STGT_ENEMY), m_vAbnormalData(std::move(abnormalData)) {
    // Current L2Server 0x00ca65d0.
}

CSkillEffect_p_change_slot::~CSkillEffect_p_change_slot() = default;

bool CSkillEffect_p_change_slot::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac3cd0.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    const auto found = creature->m_SkillMod.m_SkillAbnormalSlotChangeMap.find(m_pSkillInfo);
    if (found == creature->m_SkillMod.m_SkillAbnormalSlotChangeMap.end()) {
        return false;
    }
    found->second.m_bOn = true;
    return true;
}

bool CSkillEffect_p_change_slot::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00acdd7c.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    CCreatureSkillMod::SkillAbnormalSlotChangeInfo trigger(m_vAbnormalData, m_Prob, m_eTargetType);
    creature.m_SkillMod.m_SkillAbnormalSlotChangeMap.emplace(m_pSkillInfo, std::move(trigger));
    return true;
}

bool CSkillEffect_p_change_slot::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00acd5b4.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    const auto found = creature.m_SkillMod.m_SkillAbnormalSlotChangeMap.find(m_pSkillInfo);
    if (found == creature.m_SkillMod.m_SkillAbnormalSlotChangeMap.end()) {
        return false;
    }
    creature.m_SkillMod.m_SkillAbnormalSlotChangeMap.erase(found);
    return true;
}

CSkillEffect_p_intercept_skill::CSkillEffect_p_intercept_skill(
    const int beforeSkillId,
    const int beforeSkillLevel,
    const int afterSkillId,
    const int afterSkillLevel,
    const double probability
) :
    m_nBeforeSkillId(beforeSkillId),
    m_nBeforeSkillLevel(beforeSkillLevel),
    m_nAfterSkillId(afterSkillId),
    m_nAfterSkillLevel(afterSkillLevel),
    m_dProbability(probability) {
    // Current L2Server 0x00ca3034.
}

bool CSkillEffect_p_intercept_skill::PumpStart(CCreature&, CCreature* source, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ac7e0c.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !source->IsPC()) {
        return false;
    }

    L2_GUARD(source->m_SkillMod.m_InterceptSkillLock);
    const CSkillKey beforeKey(m_nBeforeSkillId, static_cast<std::uint16_t>(m_nBeforeSkillLevel));
    if (source->m_SkillMod.m_SkillInterceptMap.find(beforeKey) == source->m_SkillMod.m_SkillInterceptMap.end()) {
        const CSkillKey afterKey(m_nAfterSkillId, static_cast<std::uint16_t>(m_nAfterSkillLevel));
        CSkillInfo* const interceptSkill = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfo(afterKey, nullptr));
        source->m_SkillMod.m_SkillInterceptMap.emplace(
            beforeKey, CCreatureSkillMod::SkillInterceptInfo(interceptSkill, m_dProbability)
        );
        source->m_SkillMod.m_bHasInterceptSkill = true;
    }
    return true;
}

bool CSkillEffect_p_intercept_skill::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ac441c.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_InterceptSkillLock);
    const CSkillKey beforeKey(m_nBeforeSkillId, static_cast<std::uint16_t>(m_nBeforeSkillLevel));
    const auto found = creature.m_SkillMod.m_SkillInterceptMap.find(beforeKey);
    if (found != creature.m_SkillMod.m_SkillInterceptMap.end()) {
        creature.m_SkillMod.m_SkillInterceptMap.erase(found);
    }
    if (creature.m_SkillMod.m_SkillInterceptMap.empty()) {
        creature.m_SkillMod.m_bHasInterceptSkill = false;
    }
    return false;
}

CSkillEffect_p_replace_skill::CSkillEffect_p_replace_skill(
    const int beforeSkillId, const int afterSkillId, const double probability
) :
    m_nBeforeSkillId(beforeSkillId), m_nAfterSkillId(afterSkillId), m_dProbability(probability) {
    // Current L2Server 0x00ca3074.
}

bool CSkillEffect_p_replace_skill::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ac7fb0.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !creature.IsPC()) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_ReplacedSkillLock);
    if (creature.m_SkillMod.m_ReplacedSkillMap.find(m_nBeforeSkillId) == creature.m_SkillMod.m_ReplacedSkillMap.end()) {
        creature.m_SkillMod.m_ReplacedSkillMap.emplace(
            m_nBeforeSkillId, CCreatureSkillMod::SkillReplaceInfo(m_nAfterSkillId, m_dProbability)
        );
        creature.m_SkillMod.m_bHasReplacedSkill = true;
    }
    return true;
}

bool CSkillEffect_p_replace_skill::PumpEnd(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac4558.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !creature.IsPC()) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_ReplacedSkillLock);
    const auto found = creature.m_SkillMod.m_ReplacedSkillMap.find(m_nBeforeSkillId);
    if (found != creature.m_SkillMod.m_ReplacedSkillMap.end()) {
        creature.m_SkillMod.m_ReplacedSkillMap.erase(found);
    }
    if (creature.m_SkillMod.m_ReplacedSkillMap.empty()) {
        creature.m_SkillMod.m_bHasReplacedSkill = false;
    }
    return false;
}

CSkillEffect_p_trigger_skill_by_magic_type::CSkillEffect_p_trigger_skill_by_magic_type(
    std::set<int> magicTypes,
    const BadSKillType::Enum goodSkill,
    const int probability,
    const char16_t* calledSkillName,
    const SkillTargetTypeEnum targetType
) :
    CSkillEffectTrigger(probability, calledSkillName, targetType),
    m_sMagicType(std::move(magicTypes)),
    m_nGoodSkill(goodSkill) {
    // Current L2Server 0x00ca5b40.
}

bool CSkillEffect_p_trigger_skill_by_magic_type::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac3b74.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    const auto found = creature->m_SkillMod.m_SkillMagicTypeTriggerMap.find(m_pSkillInfo);
    if (found == creature->m_SkillMod.m_SkillMagicTypeTriggerMap.end()) {
        return false;
    }
    found->second.m_bOn = true;
    return true;
}

bool CSkillEffect_p_trigger_skill_by_magic_type::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ace1f0.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    CCreatureSkillMod::SkillMagicTypeTriggerInfo trigger(m_sMagicType, m_nGoodSkill, m_Prob, m_eTargetType);
    creature.m_SkillMod.m_SkillMagicTypeTriggerMap.emplace(m_pSkillInfo, std::move(trigger));
    return true;
}

bool CSkillEffect_p_trigger_skill_by_magic_type::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00acdc00.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    const auto found = creature.m_SkillMod.m_SkillMagicTypeTriggerMap.find(m_pSkillInfo);
    if (found == creature.m_SkillMod.m_SkillMagicTypeTriggerMap.end()) {
        return false;
    }
    creature.m_SkillMod.m_SkillMagicTypeTriggerMap.erase(found);
    return true;
}

CSkillEffect_p_trigger_skill_by_abnormal_slot::CSkillEffect_p_trigger_skill_by_abnormal_slot(
    const EnemyType enemyType,
    const int targetLevelMin,
    const int targetLevelMax,
    const int probability,
    const char16_t* calledSkillName,
    const SkillTargetTypeEnum targetType,
    std::vector<AbnormalDataForTrigger> abnormalData
) :
    CSkillEffectTrigger(probability, calledSkillName, targetType),
    m_eEnemyType(enemyType),
    m_nTargetLevelMin(targetLevelMin),
    m_nTargetLevelMax(targetLevelMax),
    m_vAbnormalData(std::move(abnormalData)) {
    // Current L2Server 0x00ca66c0.
}

bool CSkillEffect_p_trigger_skill_by_abnormal_slot::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac3344.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    const auto found = creature->m_SkillMod.m_SkillAbnormalSlotTriggerMap.find(m_pSkillInfo);
    if (found == creature->m_SkillMod.m_SkillAbnormalSlotTriggerMap.end()) {
        return false;
    }
    found->second.m_bOn = true;
    return true;
}

bool CSkillEffect_p_trigger_skill_by_abnormal_slot::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00acda34.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    CCreatureSkillMod::SkillAbnormalSlotTriggerInfo trigger(
        m_nTargetLevelMin, m_nTargetLevelMax, m_eEnemyType, m_vAbnormalData, m_Prob, m_eTargetType
    );
    creature.m_SkillMod.m_SkillAbnormalSlotTriggerMap.emplace(m_pSkillInfo, std::move(trigger));
    return true;
}

bool CSkillEffect_p_trigger_skill_by_abnormal_slot::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00acd438.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    const auto found = creature.m_SkillMod.m_SkillAbnormalSlotTriggerMap.find(m_pSkillInfo);
    if (found == creature.m_SkillMod.m_SkillAbnormalSlotTriggerMap.end()) {
        return false;
    }
    creature.m_SkillMod.m_SkillAbnormalSlotTriggerMap.erase(found);
    return true;
}

CSkillEffect_p_trigger_skill_by_attacked_skill::CSkillEffect_p_trigger_skill_by_attacked_skill(
    const int probability,
    std::set<std::u16string> skillNames,
    const char16_t* calledSkillName,
    const SkillTargetTypeEnum targetType
) :
    CSkillEffectTrigger(probability, calledSkillName, targetType), m_setSkillName(std::move(skillNames)) {
    // Current L2Server 0x00ca8d30.
}

void CSkillEffect_p_trigger_skill_by_attacked_skill::OnPostLoad() {
    // Current L2Server 0x00ac3e2c.
    CSkillEffectTrigger::OnPostLoad();
    if (!m_setSkillId.empty()) {
        return;
    }
    for (const std::u16string& skillName : m_setSkillName) {
        const CSkillInfo* const skillInfo =
            g_SkillDB.GetSkillInfoByName(target_utf16::ToHostCodeUnits(skillName.c_str()).c_str());
        m_setSkillId.insert(skillInfo->m_nSkillId);
    }
}

bool CSkillEffect_p_trigger_skill_by_attacked_skill::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac3f08.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    const auto found = creature->m_SkillMod.m_SkillAttackedSkillTriggerMap.find(m_pSkillInfo);
    if (found == creature->m_SkillMod.m_SkillAttackedSkillTriggerMap.end()) {
        return false;
    }
    found->second.m_bOn = true;
    return true;
}

bool CSkillEffect_p_trigger_skill_by_attacked_skill::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ace364.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    CCreatureSkillMod::SkillAttackedSkillTriggerInfo trigger(m_Prob, m_setSkillId, m_eTargetType);
    creature.m_SkillMod.m_SkillAttackedSkillTriggerMap.emplace(m_pSkillInfo, std::move(trigger));
    return true;
}

bool CSkillEffect_p_trigger_skill_by_attacked_skill::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00acdf28.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    const auto found = creature.m_SkillMod.m_SkillAttackedSkillTriggerMap.find(m_pSkillInfo);
    if (found == creature.m_SkillMod.m_SkillAttackedSkillTriggerMap.end()) {
        return false;
    }
    creature.m_SkillMod.m_SkillAttackedSkillTriggerMap.erase(found);
    return true;
}

CSkillEffect_p_max_hp::CSkillEffect_p_max_hp(const double bonus, const SkillFxChangeType changeType, const bool charge) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::MaxHP), m_bCharge(charge) {
    // Current L2Server 0x00ac5f20.
}

CSkillEffect_p_max_mp::CSkillEffect_p_max_mp(const double bonus, const SkillFxChangeType changeType, const bool charge) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::MaxMP), m_bCharge(charge) {
    // Current L2Server 0x00ac5f58.
}

CSkillEffect_p_max_mp_add::CSkillEffect_p_max_mp_add(const double bonus, const SkillFxChangeType changeType) :
    CSkillEffectAddBonusByAdditionalArmorType(bonus, changeType, CCreatureSkillMod::MaxMP) {
    // Current L2Server 0x00ac5f90.
}

CSkillEffect_p_max_cp::CSkillEffect_p_max_cp(const double bonus, const SkillFxChangeType changeType, const bool charge) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::MaxCP), m_bCharge(charge) {
    // Current L2Server 0x00ac5fb8.
}

bool CSkillEffect_p_max_hp::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo*, const SkillPumpStartType type
) const {
    // Current L2Server 0x00aae830.
    L2_GUARD_FUNCTION();
    if (!m_bCharge) {
        return true;
    }
    if (!creature.m_SkillMod.CanGetDamageHP(source, &creature)) {
        return false;
    }

    const double previousMaximum = creature.m_data->m_dBaseHP;
    L2SkillFunc::AddBonus(
        &creature.m_SkillMod.m_Mods[m_ModName].m_Per, &creature.m_SkillMod.m_Mods[m_ModName].m_Diff, m_Sfct, m_dBonus
    );

    double newMaximum = 0.0;
    if (!creature.IsPC()) {
        creature.ValidateBaseHP();
        newMaximum = creature.m_data->m_dBaseHP;
    } else {
        User* const user = creature.CastUser();
        if (user == nullptr) {
            return false;
        }
        newMaximum = user->ValidateBaseHPAndGetLimitlessValue();
    }

    if (type != SPST_ENTER_WORLD) {
        const double charged = creature.IncreaseHP_diff(newMaximum - previousMaximum, true, 4, 0, false);
        L2SkillFunc::SendHpMpChangedSystemMessage(
            nullptr, creature, runtime::Win64CvttDoubleToInt32(charged), SystemMessageId::HP_RESTORED
        );
    }
    return true;
}

bool CSkillEffect_p_max_hp::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00aae9b8.
    L2_GUARD_FUNCTION();
    CSkillEffectAddBonusByArmorType::Pump(creature, skillInfo, 0);
    if (creature->IsNPC()) {
        static_cast<CNPC*>(creature)->m_bChargeFullHPBySkillEnterWorld = true;
    }
    return true;
}

bool CSkillEffect_p_max_mp::PumpStart(CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00aaea94.
    L2_GUARD_FUNCTION();
    if (!m_bCharge) {
        return true;
    }
    if (!creature.m_SkillMod.CanGetDamageMP(source, &creature)) {
        return false;
    }

    const double previousMaximum = creature.m_data->m_dBaseMP;
    L2SkillFunc::AddBonus(
        &creature.m_SkillMod.m_Mods[m_ModName].m_Per, &creature.m_SkillMod.m_Mods[m_ModName].m_Diff, m_Sfct, m_dBonus
    );
    creature.ValidateBaseMP();
    const double charged = creature.IncreaseMP_diff(creature.m_data->m_dBaseMP - previousMaximum, 4, 0, false);
    L2SkillFunc::SendHpMpChangedSystemMessage(
        nullptr, creature, runtime::Win64CvttDoubleToInt32(charged), SystemMessageId::MP_RESTORED
    );
    return true;
}

bool CSkillEffect_p_max_mp::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00aaebdc.
    L2_GUARD_FUNCTION();
    CSkillEffectAddBonusByArmorType::Pump(creature, skillInfo, 0);
    if (creature->IsNPC()) {
        static_cast<CNPC*>(creature)->m_bChargeFullMPBySkillEnterWorld = true;
    }
    return true;
}

bool CSkillEffect_p_max_mp_add::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00aaecb8.
    L2_GUARD_FUNCTION();
    CSkillEffectAddBonusByAdditionalArmorType::Pump(creature, skillInfo, 0);
    if (creature->IsNPC()) {
        static_cast<CNPC*>(creature)->m_bChargeFullMPBySkillEnterWorld = true;
    }
    return true;
}

bool CSkillEffect_p_max_cp::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo*, const SkillPumpStartType type
) const {
    // Current L2Server 0x00aaed94.
    L2_GUARD_FUNCTION();
    if (!m_bCharge) {
        return true;
    }
    if (!creature.m_SkillMod.CanGetDamageHP(source, &creature)) {
        return false;
    }

    const double previousMaximum = creature.m_data->m_dBaseCP;
    L2SkillFunc::AddBonus(
        &creature.m_SkillMod.m_Mods[m_ModName].m_Per, &creature.m_SkillMod.m_Mods[m_ModName].m_Diff, m_Sfct, m_dBonus
    );
    creature.ValidateBaseCP();
    if (type != SPST_ENTER_WORLD) {
        const double charged = creature.IncreaseCP_diff(creature.m_data->m_dBaseCP - previousMaximum, 4, 0, false);
        L2SkillFunc::SendHpMpChangedSystemMessage(
            nullptr, creature, runtime::Win64CvttDoubleToInt32(charged), SystemMessageId::CP_RESTORED
        );
    }
    return true;
}

bool CSkillEffect_p_max_cp::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00aaeee0.
    L2_GUARD_FUNCTION();
    CSkillEffectAddBonusByArmorType::Pump(creature, skillInfo, 0);
    if (creature->IsNPC()) {
        // The current executable uses the MP enter-world flag.
        static_cast<CNPC*>(creature)->m_bChargeFullMPBySkillEnterWorld = true;
    }
    return true;
}

bool CSkillEffect_p_defence_fatal_blow_rate::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab1484.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(
        &creature.m_DefenceFatalBlowSuccessBonus.m_Per, &creature.m_DefenceFatalBlowSuccessBonus.m_Diff, sfct, dBonus
    );
    return true;
}

bool CSkillEffect_p_defence_fatal_blow_rate::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ab1540.
    L2_GUARD_FUNCTION();
    L2SkillFunc::RemoveBonus(
        &creature.m_DefenceFatalBlowSuccessBonus.m_Per, &creature.m_DefenceFatalBlowSuccessBonus.m_Diff, sfct, dBonus
    );
    return true;
}

bool CSkillEffect_p_enlarge_abnormal_slot::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab35f4.
    L2_GUARD_FUNCTION();
    creature.m_SkillDurationMod.m_nBuffSlotBonus =
        std::max(creature.m_SkillDurationMod.m_nBuffSlotBonus + m_nBuffSlotBonus, 0);
    return true;
}

bool CSkillEffect_p_enlarge_abnormal_slot::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ab36a8.
    L2_GUARD_FUNCTION();
    creature.m_SkillDurationMod.m_nBuffSlotBonus =
        std::max(creature.m_SkillDurationMod.m_nBuffSlotBonus - m_nBuffSlotBonus, 0);
    return true;
}

bool CSkillEffect_p_enlarge_storage::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab3464.
    L2_GUARD_FUNCTION();
    creature.m_SkillDurationMod.m_aStorageBonus[sesType] += nBonus;
    return true;
}

bool CSkillEffect_p_enlarge_storage::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab3510.
    L2_GUARD_FUNCTION();
    int& storageBonus = creature.m_SkillDurationMod.m_aStorageBonus[sesType];
    storageBonus = std::max(storageBonus - nBonus, 0);
    creature.ValidateCarryWeight();
    return true;
}

bool CSkillEffect_p_reflect_dd::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab3ea0.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    creature.m_SkillMod.m_dReflectDDPer = m_refectRatio;
    return true;
}

bool CSkillEffect_p_reflect_dd::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab3f60.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    creature.m_SkillMod.m_dReflectDDPer = 0.0;
    return true;
}

bool CSkillEffect_p_reuse_delay::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00aaf34c.
    L2_GUARD_FUNCTION();
    if (sfct == SFCT_PER) {
        creature.m_SkillDurationMod.m_dReuseTimeBonusPer[nIsMagic] *= (dBonus + 100.0) / 100.0;
    }
    return true;
}

bool CSkillEffect_p_reuse_delay::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00aaf418.
    L2_GUARD_FUNCTION();
    if (sfct == SFCT_PER) {
        creature.m_SkillDurationMod.m_dReuseTimeBonusPer[nIsMagic] /= (dBonus + 100.0) / 100.0;
    }
    return true;
}

bool CSkillEffect_p_magic_mp_cost::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab1a88.
    L2_GUARD_FUNCTION();
    if (nIsMagic < 0 || nIsMagic >= SIMT_MAX) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"nIsMagic %d is not in right range",
        //     nIsMagic)
        // L2_LOG_ERROR(Log,
        //     L"nIsMagic %d is not in right range",
        //     nIsMagic)
        L2_LOG_ERROR(
            Log, "Cannot apply p_magic_mp_cost: magic type %d is outside 0..%d; the MP-cost modifier is left unchanged.",
            nIsMagic, static_cast<int>(SIMT_MAX) - 1
        );
        return false;
    }
    L2SkillFunc::AddBonus(
        creature, static_cast<CCreatureSkillMod::ModName>(CCreatureSkillMod::MPCost0 + nIsMagic), sfct, dBonus
    );
    return true;
}

bool CSkillEffect_p_magic_mp_cost::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab1b68.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }

    User* const user = creature.CastUser();
    if (user != nullptr) {
        CCreatureSkillMod::Mod& mod = user->m_SkillDurationMod.m_mpcost[nIsMagic];
        if (sfct == SFCT_PER) {
            mod.m_Per *= (dBonus + 100.0) / 100.0;
        } else if (sfct == SFCT_DIFF) {
            mod.m_Diff += dBonus;
        }
        user->GetSocket()->Send("chdf", 0xfe, 0xeb, nIsMagic, mod.m_Per * 100.0 - 100.0);
    }
    return true;
}

bool CSkillEffect_p_magic_mp_cost::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab1cec.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }

    User* const user = creature.CastUser();
    if (user != nullptr) {
        CCreatureSkillMod::Mod& mod = user->m_SkillDurationMod.m_mpcost[nIsMagic];
        if (sfct == SFCT_PER) {
            mod.m_Per /= (dBonus + 100.0) / 100.0;
        } else if (sfct == SFCT_DIFF) {
            mod.m_Diff -= dBonus;
        }
        user->GetSocket()->Send("chdf", 0xfe, 0xeb, nIsMagic, mod.m_Per * 100.0 - 100.0);
    }
    return true;
}

CSkillEffect_p_after_mp_cost::CSkillEffect_p_after_mp_cost(int afterEffectType, double bonus, SkillFxChangeType changeType) :
    m_dBonus(bonus), m_Sfct(changeType), m_nAfterEffectType(afterEffectType) {
    // Current L2Server 0x00c9176c.
}

bool CSkillEffect_p_after_mp_cost::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abf1d8.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    if (m_nAfterEffectType < 0 || m_nAfterEffectType > 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"nAfterEffectType %d is in invalid range",
        //     m_nAfterEffectType)
        // L2_LOG_ERROR(Log,
        //     L"nAfterEffectType %d is in invalid range",
        //     m_nAfterEffectType)
        L2_LOG_ERROR(
            Log,
            "Cannot apply p_after_mp_cost: after-effect type %d is outside 0..1; the MP-cost modifier is left unchanged.",
            m_nAfterEffectType
        );
        return false;
    }

    CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_ModMpCostAfterEffect[m_nAfterEffectType];
    L2SkillFunc::AddBonus(&mod.m_Per, &mod.m_Diff, m_Sfct, m_dBonus);
    return true;
}

CSkillEffect_p_block_getdamage::CSkillEffect_p_block_getdamage(wchar_t* blockType) {
    // Current L2Server 0x00ac5e4c.
    L2_GUARD_FUNCTION();
    bIsBlockHP = std::wcscmp(blockType, L"block_hp") == 0;
}

bool CSkillEffect_p_block_getdamage::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aae54c.
    L2_GUARD_FUNCTION();
    if (bIsBlockHP) {
        creature->m_SkillMod.m_bCanGetDamageHP = false;
    } else {
        creature->m_SkillMod.m_bCanGetDamageMP = false;
    }
    return true;
}

bool CSkillEffect_p_block_getdamage_except::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abbda0.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !creature.IsPC() || !source->IsPC()) {
        return false;
    }
    if (creature.m_nId != source->m_nId) {
        creature.m_nDamageExceptAttackerId = source->m_nId;
    }
    return true;
}

bool CSkillEffect_p_block_getdamage_except::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00abbe78.
    L2_GUARD_FUNCTION();
    creature.m_nDamageExceptAttackerId = 0;
    return false;
}

bool CSkillEffect_p_block_getdamage_except_each_other::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abd94c.
    L2_GUARD_FUNCTION();
    if (source == nullptr) {
        return false;
    }
    if (creature.m_nId != source->m_nId) {
        creature.m_nDamageExceptAttackerId = source->m_nId;
        source->m_nDamageExceptAttackerId = creature.m_nId;
    }
    return true;
}

bool CSkillEffect_p_block_getdamage_except_each_other::PumpEnd(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac171c.
    L2_GUARD_FUNCTION();
    CCreature* const partner = g_CreatureSmartIdManager.GetObjectW(source->m_nDamageExceptAttackerId);
    if (partner != nullptr) {
        partner->m_nDamageExceptAttackerId = 0;
    }
    creature.m_nDamageExceptAttackerId = 0;
    source->m_nDamageExceptAttackerId = 0;
    return false;
}

bool CSkillEffect_p_block_getdamage_target_only::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abe1c0.
    L2_GUARD_FUNCTION();
    CCreature* const target = creature.m_spTarget.GetCreature();
    if (target == nullptr || !creature.IsPC() || !target->IsPC()) {
        return false;
    }
    if (creature.m_nId != target->m_nId) {
        creature.m_nCanIgnoreDamageTargetId = target->m_nId;
    }
    return true;
}

bool CSkillEffect_p_block_getdamage_target_only::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00abe2a4.
    L2_GUARD_FUNCTION();
    creature.m_nCanIgnoreDamageTargetId = 0;
    return false;
}

bool CSkillEffect_p_disappear_target::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab730c.
    L2_GUARD_FUNCTION();
    creature.SetTargetable(false, true);
    return true;
}

bool CSkillEffect_p_disappear_target::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab73b8.
    L2_GUARD_FUNCTION();
    creature.SetTargetable(true, true);
    return true;
}

bool CSkillEffect_cub_physical_attack::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab16d0.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::PAttack, m_Sfct, m_dBonus);
    return true;
}

bool CSkillEffect_cub_physical_defence::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab1780.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::PDefend, m_Sfct, m_dBonus);
    return true;
}

bool CSkillEffect_cub_attack_speed::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab1834.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::AttackSpeed, m_Sfct, m_dBonus);
    return true;
}

bool CSkillEffect_p_reflect_skill::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab2ccc.
    L2_GUARD_FUNCTION();
    if (creature != nullptr) {
        for (std::size_t index = 0; index < 8; ++index) {
            creature->m_SkillMod.m_SkillRefectProb.m_Prob[index] = index < 6 ? m_Probability[index] : 0.0;
        }
    }
    return true;
}

bool CSkillEffect_cub_block_act::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab18e8.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bCanAct = false;
    creature->m_data->m_bCanAct = 0;
    creature->StopMove();
    if (creature->IsNPC()) {
        creature->CastNPC()->ParamChanged(NPCT_PUMP_BLOCK);
    } else if (creature->IsPC()) {
        creature->ParamChanged(std::int64_t{PCB_CHAR_SKILL});
    } else {
        creature->ParamChanged();
    }
    return true;
}

bool CEffectCond_p_castle_siege::CheckEffectCondition(CCreature* creature) {
    // Current L2Server 0x00abe0e4.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    return !user->m_AreaMod.m_bIsInPeaceField && user->m_AreaMod.m_bIsInBattleField;
}

bool CEffectCond_p_olympiad_pledge::CheckEffectCondition(CCreature* creature) {
    // Current L2Server 0x00abe000.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    return user->IsOlympiadPlaying() || user->m_PledgeGameUser.IsPlayer();
}

bool CEffectCond_p_over_hp::CheckEffectCondition(CCreature* creature) {
    // Current L2Server 0x00ac0a50.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    return user->m_data->m_dOrgHP * m_dPer < user->m_data->m_dHP;
}

bool CSKillEffetct_p_attack_position_change::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abc1e0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    creature->m_SkillMod.m_eChangeAttackPosition = m_eDirection;
    return true;
}

bool CSKillEffetct_p_real_resist::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abc114.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    creature->m_SkillMod.m_dRealRegist = (1.0 - m_dPower / 100.0) * creature->m_SkillMod.m_dRealRegist;
    return true;
}

CSkillEffect_p_physical_attack::CSkillEffect_p_physical_attack(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::PAttack) {
    // Current L2Server 0x00ac8b8c.
}

CSkillEffect_p_physical_attack_shield::CSkillEffect_p_physical_attack_shield(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByShield(bonus, changeType, CCreatureSkillMod::PAttack) {
    // Current L2Server 0x00ac970c.
}

CSkillEffect_p_physical_attack_by_defence::CSkillEffect_p_physical_attack_by_defence(int ignoreDefence, double bonus) :
    CSkillEffectAddBonusByWeaponType(bonus, SFCT_PER, CCreatureSkillMod::PAttack), m_IgnoreDefence(ignoreDefence) {
    // Current L2Server 0x00ac5b44.
}

bool CSkillEffect_p_physical_attack_by_defence::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aacf24.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    double defence = 0.0;
    if (m_IgnoreDefence == 0) {
        defence = creature->GetShieldDefense();
        defence += creature->GetPDefend();
    } else if (m_IgnoreDefence == 1) {
        defence = creature->GetPDefend();
    } else if (m_IgnoreDefence == 2) {
        defence = creature->GetShieldDefense();
    }
    L2SkillFunc::AddBonus(creature, m_ModName, SFCT_DIFF, m_dBonus / 100.0 * defence);
    return true;
}

CSkillEffect_p_physical_attack_by_material::CSkillEffect_p_physical_attack_by_material(
    double bonus, SkillFxChangeType changeType
) :
    m_MaterialType(MATT_NONE), m_Bonus(bonus), m_ChangeType(changeType) {
    // Current inline/COMDAT body folded at 0x00c9155c.
}

bool CSkillEffect_p_physical_attack_by_material::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00aad050.
    L2_GUARD_FUNCTION();
    CSharedItemData* const weapon = creature->GetEquipedWeapon();
    if (weapon == nullptr) {
        return false;
    }
    return weapon->GetConstant()->m_Material == static_cast<int>(m_MaterialType);
}

bool CSkillEffect_p_physical_attack_by_material::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aad10c.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::PAttack, m_ChangeType, m_Bonus);
    return true;
}

CSkillEffect_p_physical_defence::CSkillEffect_p_physical_defence(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::PDefend) {
    // Current L2Server 0x00ac5b80.
}

CSkillEffect_p_physical_defence_pvp::CSkillEffect_p_physical_defence_pvp(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::PDefend) {
    // Current L2Server 0x00ac73d8.
}

bool CSkillEffect_p_physical_defence_pvp::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00abd1c4.
    L2_GUARD_FUNCTION();
    if (creature != nullptr && creature->IsPC()) {
        User* const user = creature->CastUser();
        if (user->IsOlympiadPlaying() || user->m_PledgeGameUser.IsPlayer()) {
            return true;
        }
        creature->m_SkillMod.m_bIsHighHPState = false;
    }
    return false;
}

bool CSkillEffect_p_physical_defence_pvp::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ac045c.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bIsHighHPState = true;
    if (creature->m_data->m_dBaseHP * 0.9 < creature->m_data->m_dHP) {
        CSkillEffectAddBonusByArmorType::Pump(creature, skillInfo, 0);
    }
    return true;
}

CSkillEffect_p_physical_defence_by_material::CSkillEffect_p_physical_defence_by_material(
    double bonus, SkillFxChangeType changeType
) :
    m_MaterialType(MATT_NONE), m_Bonus(bonus), m_ChangeType(changeType) {
    // Current inline/COMDAT body folded at 0x00c91588.
}

bool CSkillEffect_p_physical_defence_by_material::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00ac1898.
    L2_GUARD_FUNCTION();
    CSharedItemData* const armor = CItemFactory::Inst().GetData(creature->m_data->m_nSlotIndex[ST_CHEST]);
    if (armor == nullptr) {
        return false;
    }
    return armor->GetConstant()->m_Material == static_cast<int>(m_MaterialType);
}

bool CSkillEffect_p_physical_defence_by_material::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aad1bc.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::PDefend, m_ChangeType, m_Bonus);
    return true;
}

CSkillEffect_p_pve_physical_defence::CSkillEffect_p_pve_physical_defence(
    double amount, SkillFxChangeType changeType, std::vector<std::int64_t>* npcIds
) :
    m_dAmount(amount), m_eSkillFxChangeType(changeType) {
    // Current L2Server 0x00acada4.
    for (const std::int64_t npcId : *npcIds) {
        m_vNpcID.push_back(static_cast<int>(npcId));
    }
}

bool CSkillEffect_p_pve_physical_defence::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00abd478.
    L2_GUARD_FUNCTION();
    return creature != nullptr && creature->IsPC();
}

bool CSkillEffect_p_pve_physical_defence::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac7428.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    for (const int npcId : m_vNpcID) {
        auto& bonuses = creature->m_SkillMod.m_PVEPhysicalDefenceBonusMap;
        const auto found = bonuses.find(npcId);
        if (found == bonuses.end()) {
            CCreatureSkillMod::BonusInfoInCondition info;
            info.m_dAmountPer = 1.0;
            info.m_dAmountDiff = 0.0;
            if (m_eSkillFxChangeType == SFCT_PER) {
                info.m_dAmountPer = m_dAmount;
            } else if (m_eSkillFxChangeType == SFCT_DIFF) {
                info.m_dAmountDiff = m_dAmount;
            }
            bonuses.emplace(npcId, info);
        } else if (m_eSkillFxChangeType == SFCT_PER) {
            found->second.m_dAmountPer *= m_dAmount / 100.0;
        } else if (m_eSkillFxChangeType == SFCT_DIFF) {
            found->second.m_dAmountDiff += m_dAmount;
        }
    }
    return true;
}

CSkillEffect_p_pve_magical_defence::CSkillEffect_p_pve_magical_defence(
    double amount, SkillFxChangeType changeType, std::vector<std::int64_t>* npcIds
) :
    m_dAmount(amount), m_eSkillFxChangeType(changeType) {
    // Current L2Server 0x00acae64.
    for (const std::int64_t npcId : *npcIds) {
        m_vNpcID.push_back(static_cast<int>(npcId));
    }
}

bool CSkillEffect_p_pve_magical_defence::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00abd534.
    L2_GUARD_FUNCTION();
    return creature != nullptr && creature->IsPC();
}

bool CSkillEffect_p_pve_magical_defence::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac7660.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    for (const int npcId : m_vNpcID) {
        auto& bonuses = creature->m_SkillMod.m_PVEMagicDefenceBonusMap;
        const auto found = bonuses.find(npcId);
        if (found == bonuses.end()) {
            CCreatureSkillMod::BonusInfoInCondition info;
            info.m_dAmountPer = 1.0;
            info.m_dAmountDiff = 0.0;
            if (m_eSkillFxChangeType == SFCT_PER) {
                info.m_dAmountPer = m_dAmount;
            } else if (m_eSkillFxChangeType == SFCT_DIFF) {
                info.m_dAmountDiff = m_dAmount;
            }
            bonuses.emplace(npcId, info);
        } else if (m_eSkillFxChangeType == SFCT_PER) {
            found->second.m_dAmountPer *= m_dAmount / 100.0;
        } else if (m_eSkillFxChangeType == SFCT_DIFF) {
            found->second.m_dAmountDiff += m_dAmount;
        }
    }
    return true;
}

CSkillEffect_p_pve_defence_attribute::CSkillEffect_p_pve_defence_attribute(
    EnemyType enemyType, AttributeTypeEnum attributeType, double difference
) :
    m_eEnemyType(enemyType), m_Attribute(attributeType), m_dDiff(difference) {
    // Current L2Server 0x00ac7898.
}

bool CSkillEffect_p_pve_defence_attribute::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00abd5f0.
    L2_GUARD_FUNCTION();
    return creature != nullptr && creature->IsPC();
}

bool CSkillEffect_p_pve_defence_attribute::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac78c4.
    L2_GUARD_FUNCTION();
    if (static_cast<int>(m_Attribute) < 0) {
        return false;
    }

    L2_GUARD(creature->m_SkillMod.m_Lock);
    const std::int16_t difference =
        static_cast<std::int16_t>(static_cast<std::uint16_t>(runtime::Win64CvttDoubleToInt32(m_dDiff)));
    auto& bonuses = creature->m_SkillMod.m_PVEAttrDefenceBonusMap;
    const int enemyType = static_cast<int>(m_eEnemyType);
    const auto found = bonuses.find(enemyType);
    if (found == bonuses.end()) {
        auto* values = static_cast<short*>(::operator new(sizeof(short) * ATTRT_MAX));
        std::fill_n(values, ATTRT_MAX, static_cast<short>(0));
        values[static_cast<int>(m_Attribute)] = difference;
        bonuses.emplace(enemyType, values);
    } else {
        short& value = found->second[static_cast<int>(m_Attribute)];
        const std::uint16_t sum = static_cast<std::uint16_t>(value) + static_cast<std::uint16_t>(difference);
        value = static_cast<std::int16_t>(sum);
    }
    return true;
}

CSkillEffect_p_transmute_attr_attack::CSkillEffect_p_transmute_attr_attack(AttributeTypeEnum attributeType) :
    m_eAttrAttack(ATTRT_NONE) {
    // Current L2Server 0x00ac678c.
    if (static_cast<unsigned int>(attributeType) < ATTRT_MAX) {
        m_eAttrAttack = attributeType;
    }
}

bool CSkillEffect_p_transmute_attr_attack::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab4fa0.
    L2_GUARD_FUNCTION();
    creature.m_data->m_TAttackType = m_eAttrAttack;
    return true;
}

bool CSkillEffect_p_transmute_attr_attack::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ab504c.
    L2_GUARD_FUNCTION();
    creature.m_data->m_TAttackType = ATTRT_NONE;
    if (creature.IsPC()) {
        creature.ValidateChanged(VB_MAX);
        creature.CastUser()->ParamChanged(PCB_USER | PCB_CHAR_SKILL);
    }
    return true;
}

CSkillEffect_p_transmute_attr_defence::CSkillEffect_p_transmute_attr_defence(
    AttributeTypeEnum originalAttribute, AttributeTypeEnum transmutedAttribute
) :
    m_eOriginalAttribute(ATTRT_NONE), m_eTransmutedAttribute(ATTRT_NONE) {
    // Current L2Server 0x00ac67c0.
    if (static_cast<unsigned int>(originalAttribute) < ATTRT_MAX) {
        m_eOriginalAttribute = originalAttribute;
    }
    if (static_cast<unsigned int>(transmutedAttribute) < ATTRT_MAX) {
        m_eTransmutedAttribute = transmutedAttribute;
    }
}

bool CSkillEffect_p_transmute_attr_defence::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab5138.
    L2_GUARD_FUNCTION();
    creature.m_data->m_OrgDefenceType = m_eOriginalAttribute;
    creature.m_data->m_TransDefenceType = m_eTransmutedAttribute;
    return true;
}

bool CSkillEffect_p_transmute_attr_defence::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ab51f4.
    L2_GUARD_FUNCTION();
    creature.m_data->m_OrgDefenceType = ATTRT_NONE;
    creature.m_data->m_TransDefenceType = ATTRT_NONE;
    if (creature.IsPC()) {
        creature.ValidateChanged(VB_MAX);
        creature.CastUser()->ParamChanged(PCB_USER | PCB_CHAR_SKILL);
    }
    return true;
}

CSkillEffect_p_magical_defence::CSkillEffect_p_magical_defence(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::MDefend) {
    // Current L2Server 0x00ac5ba8.
}

CSkillEffect_p_speed::CSkillEffect_p_speed(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::Speed) {
    // Current L2Server 0x00ac5dac.
}

bool CSkillEffect_p_speed::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00aad4e8.
    L2_GUARD_FUNCTION();
    if (m_dBonus == 0.0) {
        return false;
    }
    return CSkillEffectByArmorType::CheckPumpCondition(creature);
}

bool CSkillEffect_p_speed::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00aad5a4.
    L2_GUARD_FUNCTION();
    CSkillEffectAddBonusByArmorType::Pump(creature, skillInfo, 0);
    creature->ValidateSpeed();
    if (!creature->IsNPC()) {
        if (!creature->IsPC()) {
            creature->ParamChanged();
        } else {
            creature->ParamChanged(std::int64_t{PCB_CHAR_SKILL});
        }
    } else if (creature->m_InitializingSkillMod.load(std::memory_order_relaxed) != 0) {
        creature->CastNPC()->ParamChanged(std::int64_t{NPCT_PUMP_SPEED});
    }
    return true;
}

CSkillEffect_p_speed_pvp::CSkillEffect_p_speed_pvp(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::Speed) {
    // Current L2Server 0x00ac7400.
}

bool CSkillEffect_p_speed_pvp::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00abd2b0.
    L2_GUARD_FUNCTION();
    if (creature != nullptr && creature->IsPC()) {
        User* const user = creature->CastUser();
        if (user->IsOlympiadPlaying() || user->m_PledgeGameUser.IsPlayer()) {
            return true;
        }
        creature->m_SkillMod.m_bIsHighHPState = false;
    }
    return false;
}

bool CSkillEffect_p_speed_pvp::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ac0548.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bIsHighHPState = true;
    if (creature->m_data->m_dBaseHP * 0.9 < creature->m_data->m_dHP) {
        CSkillEffectAddBonusByArmorType::Pump(creature, skillInfo, 0);
    }
    creature->ValidateSpeed();
    creature->ParamChanged();
    return true;
}

CSkillEffect_p_attack_speed::CSkillEffect_p_attack_speed(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::AttackSpeed) {
    // Current L2Server 0x00ac5dd4.
}

bool CSkillEffect_p_attack_speed::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00aad6dc.
    L2_GUARD_FUNCTION();
    if (m_dBonus == 0.0) {
        return false;
    }
    return CSkillEffectByArmorType::CheckPumpCondition(creature);
}

bool CSkillEffect_p_attack_speed::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00aad798.
    L2_GUARD_FUNCTION();
    CSkillEffectAddBonusByArmorType::Pump(creature, skillInfo, 0);
    creature->ValidateAttackSpeed();
    if (!creature->IsNPC()) {
        if (!creature->IsPC()) {
            creature->ParamChanged();
        } else {
            creature->ParamChanged(std::int64_t{PCB_CHAR_SKILL});
        }
    } else if (creature->m_InitializingSkillMod.load(std::memory_order_relaxed) != 0) {
        creature->CastNPC()->ParamChanged(std::int64_t{NPCT_PUMP_SPEED2});
    }
    return true;
}

CSkillEffect_p_attack_speed_by_weapon::CSkillEffect_p_attack_speed_by_weapon(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::AttackSpeedByWeapon) {
    // Current L2Server 0x00ac6158.
}

CSkillEffect_p_attack_range::CSkillEffect_p_attack_range(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::Range) {
    // Current L2Server 0x00ac6068.
}

CSkillEffect_p_attack_speed_shield::CSkillEffect_p_attack_speed_shield(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByShield(bonus, changeType, CCreatureSkillMod::AttackSpeed) {
    // Current L2Server 0x00ac8c00.
}

CSkillEffect_p_magic_speed::CSkillEffect_p_magic_speed(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::MagicSpeed) {
    // Current L2Server 0x00ac5dfc.
}

CSkillEffect_p_magic_speed_by_magic_type::CSkillEffect_p_magic_speed_by_magic_type(
    double bonus, SkillFxChangeType changeType, std::set<int> magicTypes
) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::MagicSpeed), m_sMagicType(std::move(magicTypes)) {
    // Current L2Server 0x00aca5e4.
}

bool CSkillEffect_p_magic_speed_by_magic_type::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac2520.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    for (const int magicType : m_sMagicType) {
        CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_MagicSpeedBonusByMagicType[magicType];
        L2SkillFunc::AddBonus(&mod.m_Per, &mod.m_Diff, m_Sfct, m_dBonus);
    }
    return true;
}

CSkillEffect_p_magic_speed_by_weapon::CSkillEffect_p_magic_speed_by_weapon(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::MagicSpeed) {
    // Current L2Server 0x00ac6180.
}

CSkillEffect_p_magical_attack::CSkillEffect_p_magical_attack(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::MAttack) {
    // Current L2Server 0x00ac6018.
}

CSkillEffect_p_magical_attack_add::CSkillEffect_p_magical_attack_add(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByAdditionalArmorType(bonus, changeType, CCreatureSkillMod::MAttack) {
    // Current L2Server 0x00ac6040.
}

CSkillEffect_p_magical_attack_by_weapon::CSkillEffect_p_magical_attack_by_weapon(
    double bonus, SkillFxChangeType changeType
) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::MAttack) {
    // Current L2Server 0x00ac73b0.
}

CSkillEffect_p_magical_defence_add::CSkillEffect_p_magical_defence_add(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByAdditionalArmorType(bonus, changeType, CCreatureSkillMod::MDefend) {
    // Current L2Server 0x00ac8c28.
}

CSkillEffect_p_hp_regen::CSkillEffect_p_hp_regen(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::HPRegen) {
    // Current L2Server 0x00ac5d34.
}

CSkillEffect_p_mp_regen::CSkillEffect_p_mp_regen(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::MPRegen) {
    // Current L2Server 0x00ac5d5c.
}

CSkillEffect_p_mp_regen_add::CSkillEffect_p_mp_regen_add(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByAdditionalArmorType(bonus, changeType, CCreatureSkillMod::MPRegen) {
    // Current L2Server 0x00ac5d84.
}

CSkillEffect_p_avoid::CSkillEffect_p_avoid(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::PAvoid) {
    // Current L2Server 0x00ac5e24.
}

CSkillEffect_p_defence_critical_rate::CSkillEffect_p_defence_critical_rate(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::DefenceCriticalProb) {
    // Current L2Server 0x00ac5af4.
}

bool CSkillEffect_p_defence_critical_rate::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aacd94.
    L2_GUARD_FUNCTION();
    CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_Mods[CCreatureSkillMod::DefenceCriticalProb];
    if (m_Sfct == SFCT_PER) {
        mod.m_Per = m_dBonus / 100.0 + mod.m_Per;
    } else if (m_Sfct == SFCT_DIFF) {
        mod.m_Diff = m_dBonus + mod.m_Diff;
    }
    return true;
}

CSkillEffect_p_defence_critical_damage::CSkillEffect_p_defence_critical_damage(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::DefenceCriticalDamage) {
    // Current L2Server 0x00ac5b1c.
}

CSkillEffect_p_hit::CSkillEffect_p_hit(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::PHit) {
    // Current L2Server 0x00ac5ff0.
}

CSkillEffect_p_physical_armor_hit::CSkillEffect_p_physical_armor_hit(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::PHit) {
    // Current L2Server 0x00ac6388.
}

CSkillEffect_p_critical_damage::CSkillEffect_p_critical_damage(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::CriticalDamage) {
    // Current L2Server 0x00ac6130.
}

CSkillEffect_p_critical_damage_shield::CSkillEffect_p_critical_damage_shield(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByShield(bonus, changeType, CCreatureSkillMod::CriticalDamage) {
    // Current L2Server 0x00ac8bd8.
}

CSkillEffect_p_critical_rate::CSkillEffect_p_critical_rate(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::CriticalProb) {
    // Current L2Server 0x00ac6090.
}

CSkillEffect_p_critical_rate_shield::CSkillEffect_p_critical_rate_shield(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByShield(bonus, changeType, CCreatureSkillMod::CriticalProb) {
    // Current L2Server 0x00ac8bb0.
}

CSkillEffect_p_magic_critical_rate::CSkillEffect_p_magic_critical_rate(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::MagicCriticalProb) {
    // Current L2Server 0x00ac6360.
}

CSkillEffect_p_magic_critical_dmg::CSkillEffect_p_magic_critical_dmg(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::MagicCriticalDamage) {
    // Current L2Server 0x00ac6574.
}

CSkillEffect_p_magic_defence_critical_rate::CSkillEffect_p_magic_defence_critical_rate(
    double bonus, SkillFxChangeType changeType
) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::MagicDefenceCriticalProb) {
    // Current L2Server 0x00ac64d4.
}

CSkillEffect_p_magic_defence_critical_dmg::CSkillEffect_p_magic_defence_critical_dmg(
    double bonus, SkillFxChangeType changeType
) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::MagicDefenceCriticalDamage) {
    // Current L2Server 0x00ac64fc.
}

CSkillEffect_p_magic_defence_critical_rate_add::CSkillEffect_p_magic_defence_critical_rate_add(
    double bonus, SkillFxChangeType changeType
) :
    CSkillEffectAddBonusByAdditionalArmorType(bonus, changeType, CCreatureSkillMod::MagicDefenceCriticalProb) {
    // Current L2Server 0x00ac6524.
}

CSkillEffect_p_magic_defence_critical_dmg_add::CSkillEffect_p_magic_defence_critical_dmg_add(
    double bonus, SkillFxChangeType changeType
) :
    CSkillEffectAddBonusByAdditionalArmorType(bonus, changeType, CCreatureSkillMod::MagicDefenceCriticalDamage) {
    // Current L2Server 0x00ac654c.
}

CSkillEffect_p_magic_hit::CSkillEffect_p_magic_hit(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::MHit) {
    // Current inline/COMDAT body folded at 0x00c919a8.
}

CSkillEffect_p_physical_critical_rate_maxup::CSkillEffect_p_physical_critical_rate_maxup(
    double bonus, SkillFxChangeType changeType
) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::CriticalProb), m_dBonus(bonus) {
    // Current L2Server 0x00ac60b8.
}

bool CSkillEffect_p_physical_critical_rate_maxup::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aaf098.
    L2_GUARD_FUNCTION();
    if (creature != nullptr) {
        creature->m_SkillMod.CriticalMaxUp.m_nPhysics = m_dBonus;
    }
    return true;
}

CSkillEffect_p_magical_critical_rate_maxup::CSkillEffect_p_magical_critical_rate_maxup(
    double bonus, SkillFxChangeType changeType
) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::CriticalProb), m_dBonus(bonus) {
    // Current L2Server 0x00ac60f4.
}

bool CSkillEffect_p_magical_critical_rate_maxup::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aaf144.
    L2_GUARD_FUNCTION();
    if (creature != nullptr) {
        creature->m_SkillMod.CriticalMaxUp.m_nMagic = m_dBonus;
    }
    return true;
}

CSkillEffect_p_hit_number::CSkillEffect_p_hit_number(int bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(static_cast<double>(bonus), changeType, CCreatureSkillMod::HitNumber) {
    // Current L2Server 0x00ac659c.
}

CSkillEffect_p_limit_hp::CSkillEffect_p_limit_hp(int limit, SkillFxChangeType changeType) :
    CSkillEffect_p_limit(HP, limit, changeType) {
    // Current L2Server 0x00ac6b10.
}

CSkillEffect_p_limit_mp::CSkillEffect_p_limit_mp(int limit, SkillFxChangeType changeType) :
    CSkillEffect_p_limit(MP, limit, changeType) {
    // Current L2Server 0x00ac6b3c.
}

CSkillEffect_p_limit_cp::CSkillEffect_p_limit_cp(int limit, SkillFxChangeType changeType) :
    CSkillEffect_p_limit(CP, limit, changeType) {
    // Current L2Server 0x00ac6b6c.
}

bool CSkillEffect_p_limit::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab5e6c.
    L2_GUARD_FUNCTION();
    CCreatureSkillMod::ModLimit& limit = creature.m_SkillMod.m_ModsRegenLimit[static_cast<int>(m_RegenLimit)];
    limit.m_Type = m_Sfct;
    limit.m_Limit = static_cast<double>(m_Limit);
    return true;
}

bool CSkillEffect_p_limit::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab5f34.
    L2_GUARD_FUNCTION();
    creature.m_SkillMod.m_ModsRegenLimit[static_cast<int>(m_RegenLimit)].m_Type = SFCT_NONE;
    return true;
}

CSkillEffect_p_get_damage_limit::CSkillEffect_p_get_damage_limit(double damageLimit) : m_DamageLimit(damageLimit) {
    // Current L2Server 0x00ac6f90.
}

bool CSkillEffect_p_get_damage_limit::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab8fc4.
    L2_GUARD_FUNCTION();
    double& activeLimit = creature->m_SkillMod.m_dDamageLimit;
    if (m_DamageLimit <= activeLimit || activeLimit == 0.0) {
        activeLimit = m_DamageLimit;
    }
    return true;
}

CSkillEffect_p_block_getdamage_distance::CSkillEffect_p_block_getdamage_distance(double distance, wchar_t* blockType) {
    // Current L2Server 0x00ac6ffc.
    L2_GUARD_FUNCTION();
    m_dDistance = distance;
    m_bIsBlockHP = std::wcscmp(blockType, L"block_hp") == 0;
}

bool CSkillEffect_p_block_getdamage_distance::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abbf0c.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !creature.IsPC() || !source->IsPC()) {
        return false;
    }

    const double distanceSquared = m_dDistance * m_dDistance;
    if (!m_bIsBlockHP) {
        source->m_SkillMod.m_bCanGetRangeDamageMP = false;
        double& activeDistance = source->m_SkillMod.m_dCanGetRangeDamageMPDistance;
        if (activeDistance <= distanceSquared) {
            activeDistance = distanceSquared;
        }
    } else {
        source->m_SkillMod.m_bCanGetRangeDamageHP = false;
        double& activeDistance = source->m_SkillMod.m_dCanGetRangeDamageHPDistance;
        if (activeDistance <= distanceSquared) {
            activeDistance = distanceSquared;
        }
    }
    return true;
}

bool CSkillEffect_p_block_getdamage_distance::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00abc050.
    L2_GUARD_FUNCTION();
    if (!m_bIsBlockHP) {
        creature.m_SkillMod.m_bCanGetRangeDamageMP = true;
        creature.m_SkillMod.m_dCanGetRangeDamageMPDistance = 0.0;
    } else {
        creature.m_SkillMod.m_bCanGetRangeDamageHP = true;
        creature.m_SkillMod.m_dCanGetRangeDamageHPDistance = 0.0;
    }
    return false;
}

CSkillEffect_p_hate_attack::CSkillEffect_p_hate_attack(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::HateBonus) {
    // Current L2Server 0x00ac6764.
}

CSkillEffect_p_ignore_skill::CSkillEffect_p_ignore_skill(std::vector<std::int64_t>* skillIds) {
    // Current L2Server 0x00acaac0.
    for (const std::int64_t skillId : *skillIds) {
        m_vIgnoreSkillId.push_back(static_cast<int>(skillId));
    }
}

bool CSkillEffect_p_ignore_skill::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ac2068.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    for (const int skillId : m_vIgnoreSkillId) {
        L2SkillFunc::_IncrementMapRef(&creature.m_SkillDurationMod.m_mapIgnoreSkillId, skillId);
    }
    return true;
}

bool CSkillEffect_p_ignore_skill::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ac215c.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    for (const int skillId : m_vIgnoreSkillId) {
        L2SkillFunc::_DecrementMapRef(&creature.m_SkillDurationMod.m_mapIgnoreSkillId, skillId);
    }
    return true;
}

CSkillEffect_p_passive_bonus_skill::CSkillEffect_p_passive_bonus_skill(
    int requireSkillId, int requireSkillLevel, int acquireSkillId, int acquireSkillLevel
) :
    m_isSkillAcquired(false), require(requireSkillId, requireSkillLevel, 0), acquire(acquireSkillId, acquireSkillLevel, 0) {
    // Current L2Server 0x00ac6fb8.
}

void CSkillEffect_p_passive_bonus_skill::OnPostLoad() {
    // Current L2Server 0x00abb894. Unlike the older donor, current defers
    // both skill lookups to PumpStart and leaves this hook guard-only.
    L2_GUARD_FUNCTION();
}

bool CSkillEffect_p_passive_bonus_skill::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abb910.
    L2_GUARD_FUNCTION();
    const CSkillInfo* const requiredSkillInfo =
        g_SkillDB.GetSkillInfo(CSkillKey(require.m_nSkillId, require.m_nLevel), nullptr);
    const CSkillInfo* const acquiredSkillInfo =
        g_SkillDB.GetSkillInfo(CSkillKey(acquire.m_nSkillId, acquire.m_nLevel), nullptr);
    if (requiredSkillInfo == nullptr || acquiredSkillInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid required skill: %d, acquire skill: %d]",
        //     L"SkillFxFunc_pump.cpp",
        //     0x22e2,
        //     require.m_nSkillId,
        //     acquire.m_nSkillId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid required skill: %d, acquire skill: %d]",
        //     L"SkillFxFunc_pump.cpp",
        //     0x22e2,
        //     require.m_nSkillId,
        //     acquire.m_nSkillId)
        L2_LOG_ERROR(
            Log, "Cannot start p_passive_bonus_skill: required skill %d level %u is %s; acquired skill %d level %u is %s.",
            require.m_nSkillId, static_cast<unsigned>(require.m_nLevel),
            requiredSkillInfo == nullptr ? "missing" : "available", acquire.m_nSkillId,
            static_cast<unsigned>(acquire.m_nLevel), acquiredSkillInfo == nullptr ? "missing" : "available"
        );
        return false;
    }
    return source == &creature && creature.IsPC();
}

bool CSkillEffect_p_passive_bonus_skill::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abba78.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    const int acquiredLevel = creature->GetAcquiredSkillLevel(require.m_nSkillId);
    const CSkillKey acquireKey(acquire.m_nSkillId, static_cast<int>(acquire.m_nLevel), 0);
    const CSkillInfo* const acquiredSkillInfo = g_SkillDB.GetSkillInfo(acquireKey, creature);
    if (acquiredSkillInfo == nullptr) {
        return true;
    }

    CSerializedMessage* message = nullptr;
    if (acquiredLevel < static_cast<int>(require.m_nLevel)) {
        if (!creature->HaveSkill(acquire.m_nSkillId, acquire.m_nLevel, acquire.m_nSubLevel)) {
            return true;
        }
        message = new CSerializedDeletePassiveBonusSkill(creature, *acquiredSkillInfo, acquire);
    } else {
        if (creature->HaveSkill(acquire.m_nSkillId, acquire.m_nLevel, acquire.m_nSubLevel)) {
            return true;
        }
        message = new CSerializedAcquirePassiveBonusSkill(creature, *acquiredSkillInfo, require, acquire);
    }
    creature->PushSerializedMessage(message);
    return true;
}

bool CSkillEffect_p_passive_bonus_skill::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo* skillInfo, SkillPumpEndType
) const {
    // Current L2Server 0x00abbc7c.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    if (creature.GetAcquiredSkillLevel(require.m_nSkillId) >= static_cast<int>(require.m_nLevel)) {
        creature.PushSerializedMessage(new CSerializedDeletePassiveBonusSkill(&creature, *skillInfo, acquire));
    }
    return true;
}

CSkillEffect_p_play_music::CSkillEffect_p_play_music(int number) : musicNumber(number) {
    // Current L2Server 0x00ac6f6c.
}

bool CSkillEffect_p_play_music::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab8844.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->GetSocket()->Send("cdSdddddd", 0x9e, 4, u"", musicNumber, 0, 0, 0, 0, 0);
    return true;
}

bool CSkillEffect_p_play_music::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab8968.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->GetSocket()->Send("cdSdddddd", 0x9e, 4, u"", -1, 0, 0, 0, 0, 0);
    return true;
}

bool CSkillEffect_p_resist_abnormal_by_category::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab31b8.
    L2_GUARD_FUNCTION();
    const double factor = (static_cast<double>(nBonus) + 100.0) / 100.0;
    if (eDeBuffType == DB_GOOD) {
        if (sfct == SFCT_PER) {
            creature->m_SkillMod.m_dGoddBuffAbnormalBonusPer *= factor;
        }
    } else if (eDeBuffType > DB_GOOD) {
        if (eDeBuffType < DB_MAX) {
            if (sfct == SFCT_PER) {
                creature->m_SkillMod.m_dBadBuffAbnormalBonusPer *= factor;
            }
        } else if (eDeBuffType == DB_MAX && sfct == SFCT_PER) {
            creature->m_SkillMod.m_dBadBuffAbnormalBonusPer *= factor;
            creature->m_SkillMod.m_dGoddBuffAbnormalBonusPer *= factor;
        }
    }
    return true;
}

bool CSkillEffect_p_resist_dispel_by_category::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab3088.
    L2_GUARD_FUNCTION();
    const double bonus = static_cast<double>(nBonus) / 100.0;
    if (eDeBuffType == DB_GOOD) {
        if (sfct == SFCT_PER) {
            creature->m_SkillMod.m_dGoddBuffDispelBonusPer += bonus;
        }
    } else if (eDeBuffType > DB_GOOD) {
        if (eDeBuffType < DB_MAX) {
            if (sfct != SFCT_PER) {
                return true;
            }
        } else {
            if (eDeBuffType != DB_MAX || sfct != SFCT_PER) {
                return true;
            }
            creature->m_SkillMod.m_dGoddBuffDispelBonusPer += bonus;
        }
        creature->m_SkillMod.m_dBadBuffDispelBonusPer += bonus;
    }
    return true;
}

bool CSkillEffect_p_critical_rate::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aaefbc.
    L2_GUARD_FUNCTION();
    CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_Mods[CCreatureSkillMod::CriticalProb];
    if (m_Sfct == SFCT_PER) {
        mod.m_Per += m_dBonus / 100.0;
    } else if (m_Sfct == SFCT_DIFF) {
        mod.m_Diff += m_dBonus;
    }
    return true;
}

bool CSkillEffect_p_heal_effect::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab0354.
    L2_GUARD_FUNCTION();
    CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_Mods[CCreatureSkillMod::HealBonus];
    if (sfct == SFCT_PER) {
        mod.m_Per *= (dBonus + 100.0) / 100.0;
    } else if (sfct == SFCT_DIFF) {
        mod.m_Diff += dBonus;
    }
    return true;
}

CSkillEffect_p_condition_block_act_skill::CSkillEffect_p_condition_block_act_skill(std::vector<std::int64_t>* skillIds) {
    // Current L2Server 0x00aca940.
    for (const std::int64_t skillId : *skillIds) {
        m_vCanCastSkillId.push_back(static_cast<int>(skillId));
    }
}

bool CSkillEffect_p_changebody::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00aca7cc.
    L2_GUARD_FUNCTION();
    std::optional<std::vector<ChangeBodyStruct>> changesStorage;
    {
        L2_GUARD(creature.m_SkillMod.m_Lock);
        changesStorage.emplace(m_vChangeBody);
    }
    std::vector<ChangeBodyStruct>& changes = *changesStorage;

    const int roll = static_cast<int>(::genrand64_real1() * 100.0 + 0.0);
    for (const ChangeBodyStruct& change : changes) {
        if (roll <= change.Prob) {
            creature.Morph(change.TransformId);
            return true;
        }
    }
    return false;
}

bool CSkillEffect_p_changebody::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab52f0.
    L2_GUARD_FUNCTION();
    creature.UnMorph();
    if (creature.IsPC()) {
        creature.ParamChanged(PCB_USER | PCB_CHAR_SKILL);
    } else {
        creature.ParamChanged();
    }
    return false;
}

bool CSkillEffect_p_condition_block_act_skill::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ac1aa0.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    for (const int skillId : m_vCanCastSkillId) {
        L2SkillFunc::_IncrementMapRef(&creature.m_SkillDurationMod.m_SkillCondMap, skillId);
    }
    L2SkillFunc::BlockAction(&creature, true);
    return true;
}

bool CSkillEffect_p_condition_block_act_skill::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab53c4.
    L2_GUARD_FUNCTION();
    L2SkillFunc::BlockAction(creature, true);
    return true;
}

bool CSkillEffect_p_condition_block_act_skill::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac1b9c.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    for (const int skillId : m_vCanCastSkillId) {
        L2SkillFunc::_DecrementMapRef(&creature.m_SkillDurationMod.m_SkillCondMap, skillId);
    }
    return false;
}

CSkillEffect_p_condition_block_act_item::CSkillEffect_p_condition_block_act_item(std::vector<std::int64_t>* itemClassIds) {
    // Current L2Server 0x00aca9c0.
    for (const std::int64_t itemClassId : *itemClassIds) {
        m_vCanUseItemClass.push_back(static_cast<int>(itemClassId));
    }
}

bool CSkillEffect_p_condition_block_act_item::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ac1c90.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    for (const int itemClassId : m_vCanUseItemClass) {
        L2SkillFunc::_IncrementMapRef(&creature.m_SkillDurationMod.m_ItemCondMap, itemClassId);
    }
    L2SkillFunc::BlockAction(&creature, true);
    return true;
}

bool CSkillEffect_p_condition_block_act_item::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab546c.
    L2_GUARD_FUNCTION();
    L2SkillFunc::BlockAction(creature, true);
    return true;
}

bool CSkillEffect_p_condition_block_act_item::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac1d8c.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    for (const int itemClassId : m_vCanUseItemClass) {
        L2SkillFunc::_DecrementMapRef(&creature.m_SkillDurationMod.m_ItemCondMap, itemClassId);
    }
    return false;
}

CSkillEffect_p_block_use_skill::CSkillEffect_p_block_use_skill(std::vector<std::int64_t>* skillIds) {
    // Current L2Server 0x00acaa40.
    for (const std::int64_t skillId : *skillIds) {
        m_vBlockSkillId.push_back(static_cast<int>(skillId));
    }
}

bool CSkillEffect_p_block_use_skill::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ac1e80.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    for (const int skillId : m_vBlockSkillId) {
        L2SkillFunc::_IncrementMapRef(&creature.m_SkillDurationMod.m_mapBlockSkillId, skillId);
    }
    return true;
}

bool CSkillEffect_p_block_use_skill::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ac1f74.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    for (const int skillId : m_vBlockSkillId) {
        L2SkillFunc::_DecrementMapRef(&creature.m_SkillDurationMod.m_mapBlockSkillId, skillId);
    }
    return true;
}

bool CSkillEffect_p_disarm::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00acb944.
    L2_GUARD_FUNCTION();
    if (m_eDisarmType == ST_RHAND) {
        ++creature.m_SkillDurationMod.m_nBlockEquipWeaponCount;
        if (creature.IsPC()) {
            static_cast<void>(CCursedWeaponMgr::inst());
            User* const user = creature.CastUser();
            if (user != nullptr && user->m_nCursedWeaponClassId > 0) {
                return false;
            }
        }
        if (creature.IsPC()) {
            const AttackType attackType = creature.GetAttackType();
            if (attackType == AT_FLAG || attackType == AT_OWNTHING) {
                return false;
            }
        }
        creature.UnequipWeapon(true);
    } else if (m_eDisarmType == ST_CHEST) {
        ++creature.m_SkillMod.m_nBlockEquipChestArmorCount;
        if (!creature.IsPC()) {
            return false;
        }
        creature.UnequipArmor(m_eDisarmType);
    }
    return true;
}

bool CSkillEffect_p_disarm::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00aaca38.
    L2_GUARD_FUNCTION();
    if (m_eDisarmType == ST_RHAND) {
        int& count = creature.m_SkillDurationMod.m_nBlockEquipWeaponCount;
        if (count < 1) {
            count = 0;
            return false;
        }
        --count;
        if (count == 0) {
            creature.RestoreUnequipedWeapon();
        }
    } else if (m_eDisarmType == ST_CHEST) {
        int& count = creature.m_SkillMod.m_nBlockEquipChestArmorCount;
        if (count < 1) {
            count = 0;
            return false;
        }
        --count;
        if (count == 0) {
            if (!creature.IsPC()) {
                return false;
            }
            creature.RestoreUnequipedArmor(ST_CHEST);
        }
    }
    return true;
}

bool CSkillEffect_p_transform::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, const SkillPumpStartType type
) const {
    // Current L2Server 0x00aacb58.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    return creature.CastUser()->Transform(m_transformID, type == SPST_CASTING);
}

bool CSkillEffect_p_transform::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, const SkillPumpEndType type
) const {
    // Current L2Server 0x00aacc2c.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    return creature.CastUser()->CancelTransform(m_transformID, false, type);
}

CSkillEffect_p_attack_attribute::CSkillEffect_p_attack_attribute(AttributeTypeEnum attributeType, double bonus) {
    // Current L2Server 0x00ac5bd0.
    if (static_cast<std::uint32_t>(attributeType) < ATTRT_MAX) {
        const BaseAttribute attribute(
            static_cast<short>(attributeType), static_cast<short>(runtime::Win64CvttDoubleToInt32(bonus))
        );
        m_Attribute = attribute;
    }
}

bool CSkillEffect_p_attack_attribute::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aad270.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint16_t>(creature->m_data->m_BaseAttribute.m_nAttackType) < ATTRT_MAX) {
        creature->m_data->m_BaseAttribute += m_Attribute;
        creature->m_data->m_NeedUpdatePacketBitSet.SetBit(0x24);
    }
    return true;
}

CSkillEffect_p_attack_attribute_add::CSkillEffect_p_attack_attribute_add(int bonus) : m_nBonus(bonus) {
    // Current L2Server 0x00ac5c5c.
}

bool CSkillEffect_p_attack_attribute_add::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aad344.
    L2_GUARD_FUNCTION();
    BaseAttribute& attribute = creature->m_data->m_BaseAttribute;
    if (static_cast<std::uint16_t>(attribute.m_nAttackType) < ATTRT_MAX && attribute.m_nAttackValue != 0) {
        const std::uint16_t wrapped = static_cast<std::uint16_t>(attribute.m_nAttackValue) +
            static_cast<std::uint16_t>(static_cast<std::int16_t>(m_nBonus));
        creature->m_data->m_NeedUpdatePacketBitSet.SetShortValue_IfModifed(
            attribute.m_nAttackValue, static_cast<std::int16_t>(wrapped), 0x24
        );
    }
    return true;
}

CSkillEffect_p_cp_regen::CSkillEffect_p_cp_regen(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::CPRegen) {
    // Current L2Server 0x00ac65cc.
}

bool CSkillEffect_p_cp_regen::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00ab3988.
    L2_GUARD_FUNCTION();
    return creature->IsPC() && CSkillEffectByArmorType::CheckPumpCondition(creature);
}

bool CSkillEffect_p_cp_regen::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ab3a48.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }
    CSkillEffectAddBonusByArmorType::Pump(creature, skillInfo, 0);
    return true;
}

CSkillEffect_p_exp_modify::CSkillEffect_p_exp_modify(double value) : expModify(value) {
    // Current L2Server 0x00ac6ef4.
}

bool CSkillEffect_p_exp_modify::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab82ec.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->AddExpBonusPercent(expModify);
    return true;
}

bool CSkillEffect_p_exp_modify::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab83c4.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->AddExpBonusPercent(-expModify);
    return true;
}

CSkillEffect_p_sp_modify::CSkillEffect_p_sp_modify(double value) : spModify(value) {
    // Current L2Server 0x00ac6f1c.
}

bool CSkillEffect_p_sp_modify::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab84a4.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->AddSPBonusPercent(spModify);
    return true;
}

bool CSkillEffect_p_sp_modify::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab857c.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->AddSPBonusPercent(-spModify);
    return true;
}

CSkillEffect_p_trade::CSkillEffect_p_trade(int bonus0, int bonus1, int bonus2, int bonus3, int bonus4, int bonus5) {
    // Current L2Server 0x00c91644.
    // The current constructor writes only these six elements. The remaining
    // six lanes keep their original uninitialised meaning.
    dBonus[0] = static_cast<double>(bonus0);
    dBonus[1] = static_cast<double>(bonus1);
    dBonus[2] = static_cast<double>(bonus2);
    dBonus[3] = static_cast<double>(bonus3);
    dBonus[4] = static_cast<double>(bonus4);
    dBonus[5] = static_cast<double>(bonus5);
}

bool CSkillEffect_p_trade::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aafe9c.
    L2_GUARD_FUNCTION();
    for (std::size_t index = 0; index < 12; ++index) {
        creature->m_SkillMod.m_dTradeBonusPer[index] *= (dBonus[index] + 100.0) / 100.0;
    }
    return true;
}

CSkillEffect_p_crystal_grade_modify::CSkillEffect_p_crystal_grade_modify(double bonus) : crystalGradeModify(bonus) {
    // Current L2Server 0x00ac6f44.
}

bool CSkillEffect_p_crystal_grade_modify::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab865c.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->SetCrystalGradepBonus(crystalGradeModify);
    user->SetItemCrystalEquipPenalty(user->CalcCrystalEquipPenalty());
    return true;
}

bool CSkillEffect_p_crystal_grade_modify::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ab874c.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->SetCrystalGradepBonus(-crystalGradeModify);
    user->SetItemCrystalEquipPenalty(user->CalcCrystalEquipPenalty());
    return true;
}

CSkillEffect_p_exp_sp_fishing::CSkillEffect_p_exp_sp_fishing(double valuePercent) : m_valuePercent(valuePercent) {
    // Current L2Server 0x00ac838c.
}

bool CSkillEffect_p_exp_sp_fishing::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00abe40c.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    double& bonus = user->m_SkillDurationMod.m_dExpSpBonusFishingPercent;
    bonus += m_valuePercent;
    if (bonus < 0.0) {
        bonus = 0.0;
    }
    return true;
}

bool CSkillEffect_p_exp_sp_fishing::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00abe4fc.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    double& bonus = user->m_SkillDurationMod.m_dExpSpBonusFishingPercent;
    bonus -= m_valuePercent;
    if (bonus < 0.0) {
        bonus = 0.0;
    }
    return true;
}

CSkillEffect_p_get_item_by_exp::CSkillEffect_p_get_item_by_exp(std::int64_t expUnit, int itemType, std::int64_t itemAmount) :
    m_nExpUnit(expUnit), m_nItemType(itemType), m_nItemAmount(itemAmount) {
    // Current L2Server 0x00c918f4.
}

bool CSkillEffect_p_get_item_by_exp::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abfd64.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillMod.m_Lock);
    creature.m_SkillMod.m_bGetItemByExp = true;
    creature.m_SkillMod.m_nGetItemByExpUnit = m_nExpUnit;
    creature.m_SkillMod.m_nGetItemByExpItemType = m_nItemType;
    creature.m_SkillMod.m_nGetItemByExpItemAmount = m_nItemAmount;
    return true;
}

bool CSkillEffect_p_get_item_by_exp::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00abfe5c.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillMod.m_Lock);
    creature.m_SkillMod.m_bGetItemByExp = false;
    creature.m_SkillMod.m_nGetItemByExpUnit = 100000000;
    creature.m_SkillMod.m_nGetItemByExpItemType = 0;
    creature.m_SkillMod.m_nGetItemByExpItemAmount = 0;
    return true;
}

CSkillEffect_p_lucky_enchant_rate::CSkillEffect_p_lucky_enchant_rate(
    int increaseRate, int maxIncreaseRate, int enchantRateOption, SkillFxChangeType changeType
) :
    m_nWeaponEnchantIncreaseRate(0),
    m_nArmorEnchantIncreaseRate(0),
    m_nMaxEnchantIncreaseRate(maxIncreaseRate),
    m_nEnchantRateOption(enchantRateOption),
    m_sfct(changeType) {
    // Current L2Server 0x00c918a4.
    switch (enchantRateOption) {
        case ALL:
            m_nWeaponEnchantIncreaseRate = increaseRate;
            m_nArmorEnchantIncreaseRate = increaseRate;
            break;
        case WP:
            m_nWeaponEnchantIncreaseRate = increaseRate;
            break;
        case ARMOR:
            m_nArmorEnchantIncreaseRate = increaseRate;
            break;
        default:
            break;
    }
}

bool CSkillEffect_p_lucky_enchant_rate::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab9dd0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    switch (creature->m_SkillMod.m_nEnchantOption) {
        case ALL:
            creature->m_SkillMod.m_nArmorEnchantRate =
                runtime::AddInt32Wrapping(creature->m_SkillMod.m_nArmorEnchantRate, m_nArmorEnchantIncreaseRate);
            creature->m_SkillMod.m_nWeaponEnchantRate =
                runtime::AddInt32Wrapping(creature->m_SkillMod.m_nWeaponEnchantRate, m_nWeaponEnchantIncreaseRate);
            break;
        case WP:
            creature->m_SkillMod.m_nWeaponEnchantRate =
                runtime::AddInt32Wrapping(creature->m_SkillMod.m_nWeaponEnchantRate, m_nWeaponEnchantIncreaseRate);
            break;
        case ARMOR:
            creature->m_SkillMod.m_nArmorEnchantRate =
                runtime::AddInt32Wrapping(creature->m_SkillMod.m_nArmorEnchantRate, m_nArmorEnchantIncreaseRate);
            break;
        default:
            break;
    }
    creature->m_SkillMod.m_nMaxEnchantIncreaseRate =
        runtime::AddInt32Wrapping(creature->m_SkillMod.m_nMaxEnchantIncreaseRate, m_nMaxEnchantIncreaseRate);
    creature->m_SkillMod.m_nEnchantOption = static_cast<TshirtOptionType>(m_nEnchantRateOption);
    creature->m_SkillMod.m_EnchantType = m_sfct;
    return true;
}

CSkillEffect_p_raid_point_modify::CSkillEffect_p_raid_point_modify(double pointModifier) : pointModPer(pointModifier) {
    // Current L2Server 0x00ac70e0.
}

bool CSkillEffect_p_raid_point_modify::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abc28c.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->AddRaidPointBonus(pointModPer);
    return true;
}

bool CSkillEffect_p_raid_point_modify::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00abc364.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->AddRaidPointBonus(-pointModPer);
    return true;
}

CSkillEffect_p_reduce_drop_penalty::CSkillEffect_p_reduce_drop_penalty(
    EnemyType enemyType, double itemDropBonus, double expDropBonus
) :
    ent(enemyType), dItemDropBonus(itemDropBonus), dExpDropBonus(expDropBonus) {
    // Current L2Server 0x00ac6330.
}

bool CSkillEffect_p_reduce_drop_penalty::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abf0c0.
    L2_GUARD_FUNCTION();
    const int index = static_cast<int>(ent);
    const double item = creature->m_SkillMod.m_dItemDropBonus[index] + dItemDropBonus;
    const double exp = creature->m_SkillMod.m_dExpDropBonus[index] + dExpDropBonus;

    double clampedItem = -100.0;
    if (-100.0 <= item) {
        clampedItem = 0.0;
        if (item <= 0.0) {
            clampedItem = item;
        }
    }
    double clampedExp = -90.0;
    if (-90.0 <= exp) {
        clampedExp = 0.0;
        if (exp <= 0.0) {
            clampedExp = exp;
        }
    }
    creature->m_SkillMod.m_dItemDropBonus[index] = clampedItem;
    creature->m_SkillMod.m_dExpDropBonus[index] = clampedExp;
    return true;
}

CSkillEffect_p_avoid_by_move_mode::CSkillEffect_p_avoid_by_move_mode(double bonus, SkillFxChangeType changeType) :
    dBonus(bonus), sfct(changeType), eMoveModeType(MVMD_MAX) {
    // Current L2Server 0x00c915b4.
}

bool CSkillEffect_p_avoid_by_move_mode::CheckPumpCondition(CCreature*) {
    // Current L2Server 0x00aaf1f0.
    L2_GUARD_FUNCTION();
    return eMoveModeType != MVMD_MAX;
}

bool CSkillEffect_p_avoid_by_move_mode::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aaf284.
    L2_GUARD_FUNCTION();
    CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_ModsMove[CCreatureSkillMod::PAvoidMoveMode][eMoveModeType];
    L2SkillFunc::AddBonus(&mod.m_Per, &mod.m_Diff, sfct, dBonus);
    return true;
}

CSkillEffect_p_hp_regen_by_move_mode::CSkillEffect_p_hp_regen_by_move_mode(double bonus, SkillFxChangeType changeType) :
    dBonus(bonus), sfct(changeType), eMoveModeType(MVMD_MAX) {
    // Current L2Server 0x00c915e4.
}

bool CSkillEffect_p_hp_regen_by_move_mode::CheckPumpCondition(CCreature*) {
    // Current L2Server 0x00aaf65c.
    L2_GUARD_FUNCTION();
    return eMoveModeType != MVMD_MAX;
}

bool CSkillEffect_p_hp_regen_by_move_mode::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aaf6f0.
    L2_GUARD_FUNCTION();
    CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_ModsMove[CCreatureSkillMod::HPRegenMoveMode][eMoveModeType];
    L2SkillFunc::AddBonus(&mod.m_Per, &mod.m_Diff, sfct, dBonus);
    return true;
}

CSkillEffect_p_mp_regen_by_move_mode::CSkillEffect_p_mp_regen_by_move_mode(double bonus, SkillFxChangeType changeType) :
    dBonus(bonus), sfct(changeType), eMoveModeType(MVMD_MAX) {
    // Current L2Server 0x00c91614.
}

bool CSkillEffect_p_mp_regen_by_move_mode::CheckPumpCondition(CCreature*) {
    // Current L2Server 0x00aaf7b8.
    L2_GUARD_FUNCTION();
    return eMoveModeType != MVMD_MAX;
}

bool CSkillEffect_p_mp_regen_by_move_mode::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aaf84c.
    L2_GUARD_FUNCTION();
    CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_ModsMove[CCreatureSkillMod::MPRegenMoveMode][eMoveModeType];
    L2SkillFunc::AddBonus(&mod.m_Per, &mod.m_Diff, sfct, dBonus);
    return true;
}

CSkillEffect_p_cp_regen_by_move_mode::CSkillEffect_p_cp_regen_by_move_mode(double bonus, SkillFxChangeType changeType) :
    eMoveModeType(MVMD_MAX), dBonus(bonus), sfct(changeType) {
    // Current L2Server 0x00c91848.
}

bool CSkillEffect_p_cp_regen_by_move_mode::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00ab3b1c.
    L2_GUARD_FUNCTION();
    return creature->IsPC() && eMoveModeType != MVMD_MAX;
}

bool CSkillEffect_p_cp_regen_by_move_mode::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab3bd8.
    L2_GUARD_FUNCTION();
    const bool isPc = creature->IsPC();
    if (isPc) {
        CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_ModsMove[CCreatureSkillMod::CPRegenMoveMode][eMoveModeType];
        L2SkillFunc::AddBonus(&mod.m_Per, &mod.m_Diff, sfct, dBonus);
    }
    return isPc;
}

CSkillEffect_p_hit_at_night::CSkillEffect_p_hit_at_night(double bonus, SkillFxChangeType changeType) :
    dBonus(bonus), sfct(changeType), nMsg(0) {
    // Current L2Server 0x00c916bc.
}

bool CSkillEffect_p_hit_at_night::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00ab05d0.
    L2_GUARD_FUNCTION();
    nMsg = creature->m_bIsSysMsg ? 1 : -1;
    creature->m_bIsSysMsg = false;
    if (!g_L2Time.m_bIsNight) {
        ++nMsg;
    }
    return true;
}

bool CSkillEffect_p_hit_at_night::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ab0694.
    L2_GUARD_FUNCTION();
    if (creature->IsPC()) {
        int messageId = 0;
        User* user = nullptr;
        if (nMsg == 1) {
            messageId = SystemMessageId::NIGHT_SKILL_EFFECT_ACTIVATED;
            user = creature->CastUser();
        } else if (nMsg == 2) {
            messageId = SystemMessageId::NIGHT_SKILL_EFFECT_ENDED_AT_DAWN;
            user = creature->CastUser();
        }
        if (messageId != 0) {
            const int encodedSkillLevel = (skillInfo->m_nSubLevel << 16) | static_cast<std::uint16_t>(skillInfo->m_nLevel);
            user->GetSocket()->Send("chccdd", 0x62, messageId, 1, 4, skillInfo->m_nSkillId, encodedSkillLevel);
        }
    }

    const bool isNight = g_L2Time.m_bIsNight;
    if (isNight) {
        L2SkillFunc::AddBonus(creature, CCreatureSkillMod::HitBonusAtNight, sfct, dBonus);
    }
    return isNight;
}

CSkillEffect_p_critical_rate_by_hp1::CSkillEffect_p_critical_rate_by_hp1(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::CriticalProb) {
    // Current L2Server 0x00ac61a8.
}

bool CSkillEffect_p_critical_rate_by_hp1::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ab0a14.
    L2_GUARD_FUNCTION();
    L2SkillFunc::pump_rate_by_hp(creature, skillInfo, creature->m_bIsHP1SysMsg, 0.3, m_ModName, m_dBonus, m_Sfct);
    return true;
}

CSkillEffect_p_critical_rate_by_hp2::CSkillEffect_p_critical_rate_by_hp2(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::CriticalProb) {
    // Current L2Server 0x00ac61d0.
}

bool CSkillEffect_p_critical_rate_by_hp2::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ab0af4.
    L2_GUARD_FUNCTION();
    L2SkillFunc::pump_rate_by_hp(creature, skillInfo, creature->m_bIsHP2SysMsg, 0.6, m_ModName, m_dBonus, m_Sfct);
    return true;
}

CSkillEffect_p_physical_attack_by_hp1::CSkillEffect_p_physical_attack_by_hp1(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::PAttack) {
    // Current L2Server 0x00ac61f8.
}

bool CSkillEffect_p_physical_attack_by_hp1::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ab0bd4.
    L2_GUARD_FUNCTION();
    L2SkillFunc::pump_rate_by_hp(creature, skillInfo, creature->m_bIsHP1SysMsg, 0.3, m_ModName, m_dBonus, m_Sfct);
    return true;
}

CSkillEffect_p_physical_attack_by_hp2::CSkillEffect_p_physical_attack_by_hp2(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::PAttack) {
    // Current L2Server 0x00ac621c.
}

bool CSkillEffect_p_physical_attack_by_hp2::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ab0cb4.
    L2_GUARD_FUNCTION();
    L2SkillFunc::pump_rate_by_hp(creature, skillInfo, creature->m_bIsHP2SysMsg, 0.6, m_ModName, m_dBonus, m_Sfct);
    return true;
}

CSkillEffect_p_physical_defence_by_hp1::CSkillEffect_p_physical_defence_by_hp1(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::PDefend) {
    // Current L2Server 0x00ac6240.
}

bool CSkillEffect_p_physical_defence_by_hp1::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ab0d94.
    L2_GUARD_FUNCTION();
    CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_Mods[static_cast<int>(m_ModName)];
    L2SkillFunc::pump_rate_by_hp(
        creature, skillInfo, creature->m_bIsHP1SysMsg, 0.3, mod.m_Per, mod.m_Diff, m_dBonus, m_Sfct
    );
    return true;
}

CSkillEffect_p_physical_defence_by_hp2::CSkillEffect_p_physical_defence_by_hp2(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::PDefend) {
    // Current L2Server 0x00ac6268.
}

bool CSkillEffect_p_physical_defence_by_hp2::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ab0e94.
    L2_GUARD_FUNCTION();
    L2SkillFunc::pump_rate_by_hp(creature, skillInfo, creature->m_bIsHP2SysMsg, 0.6, m_ModName, m_dBonus, m_Sfct);
    return true;
}

CSkillEffect_p_avoid_rate_by_hp1::CSkillEffect_p_avoid_rate_by_hp1(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::PAvoid) {
    // Current L2Server 0x00ac6290.
}

bool CSkillEffect_p_avoid_rate_by_hp1::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ab0f74.
    L2_GUARD_FUNCTION();
    L2SkillFunc::pump_rate_by_hp(creature, skillInfo, creature->m_bIsHP1SysMsg, 0.3, m_ModName, m_dBonus, m_Sfct);
    return true;
}

CSkillEffect_p_avoid_rate_by_hp2::CSkillEffect_p_avoid_rate_by_hp2(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::PAvoid) {
    // Current L2Server 0x00ac62b8.
}

bool CSkillEffect_p_avoid_rate_by_hp2::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ab1054.
    L2_GUARD_FUNCTION();
    L2SkillFunc::pump_rate_by_hp(creature, skillInfo, creature->m_bIsHP2SysMsg, 0.6, m_ModName, m_dBonus, m_Sfct);
    return true;
}

CSkillEffect_p_attack_speed_by_hp1::CSkillEffect_p_attack_speed_by_hp1(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::AttackSpeed) {
    // Current L2Server 0x00ac62e0.
}

bool CSkillEffect_p_attack_speed_by_hp1::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ab1134.
    L2_GUARD_FUNCTION();
    if (!L2SkillFunc::pump_rate_by_hp(creature, skillInfo, creature->m_bIsHP1SysMsg, 0.3, m_ModName, m_dBonus, m_Sfct)) {
        return false;
    }

    creature->ValidateAttackSpeed();
    if (creature->IsNPC()) {
        // Current 0x00ab1232 calls ParamChanged with the NPC index.
        creature->CastNPC()->ParamChanged(std::int64_t{NPCT_PUMP_SPEED3});
    } else if (creature->IsPC()) {
        static_cast<User*>(creature)->ParamChanged(std::int64_t{PCB_CHAR_SKILL});
    } else {
        creature->ParamChanged();
    }
    return true;
}

CSkillEffect_p_attack_speed_by_hp2::CSkillEffect_p_attack_speed_by_hp2(double bonus, SkillFxChangeType changeType) :
    CSkillEffectAddBonusByWeaponType(bonus, changeType, CCreatureSkillMod::AttackSpeed) {
    // Current L2Server 0x00ac6308.
}

bool CSkillEffect_p_attack_speed_by_hp2::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ab1284.
    L2_GUARD_FUNCTION();
    if (L2SkillFunc::pump_rate_by_hp(creature, skillInfo, creature->m_bIsHP2SysMsg, 0.6, m_ModName, m_dBonus, m_Sfct)) {
        creature->ValidateAttackSpeed();
        if (creature->IsNPC()) {
            creature->CastNPC()->ParamChanged(std::int64_t{NPCT_PUMP_SPEED4});
        } else if (creature->IsPC()) {
            static_cast<User*>(creature)->ParamChanged(std::int64_t{PCB_CHAR_SKILL});
        } else {
            creature->ParamChanged();
        }
    }
    return true;
}

CSkillEffect_p_defence_attribute::CSkillEffect_p_defence_attribute(AttributeTypeEnum attributeType, double bonus) {
    // Current L2Server 0x00ac5c80.
    if (static_cast<unsigned int>(attributeType) < ATTRT_MAX) {
        m_Attribute.m_nDefendValue[attributeType] = static_cast<short>(runtime::Win64CvttDoubleToInt32(bonus));
    }
}

bool CSkillEffect_p_defence_attribute::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aad420.
    L2_GUARD_FUNCTION();
    creature->m_data->m_BaseAttribute += m_Attribute;
    creature->m_data->m_NeedUpdatePacketBitSet.SetBit(0x24);
    return true;
}

CSkillEffect_p_attack_trait::CSkillEffect_p_attack_trait(TraitTypeEnum traitType, double bonus) :
    m_TraitType(traitType), m_Bonus(bonus) {
    // Current L2Server 0x00c91920.
}

bool CSkillEffect_p_attack_trait::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abe894.
    L2_GUARD_FUNCTION();
    const int trait = static_cast<int>(m_TraitType);
    if (static_cast<std::uint32_t>(trait) >= TRAIT_MAX) {
        throw std::out_of_range("invalid xarray<T> subscript");
    }
    creature->m_bzAttackingTrait[trait] = true;
    creature->m_dzAttackingTraitValue[trait] *= (m_Bonus + 100.0) / 100.0;
    return true;
}

CSkillEffect_p_defence_trait::CSkillEffect_p_defence_trait(TraitTypeEnum traitType, double bonus) :
    m_TraitType(traitType), m_Bonus(bonus), m_PerfectDefence(bonus == 100.0) {
    // Current L2Server 0x00ac5cf0.
}

bool CSkillEffect_p_defence_trait::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abe9fc.
    L2_GUARD_FUNCTION();
    if (m_TraitType < TRAIT_AT_SWORD || m_TraitType >= TRAIT_MAX) {
        return false;
    }

    const int trait = static_cast<int>(m_TraitType);
    creature->m_bzDefendingTrait[trait] = true;
    if (m_PerfectDefence) {
        creature->m_bzDefendingTraitPerfect[trait] = true;
    }
    creature->m_dzDefendingTraitValue[trait] *= (m_Bonus + 100.0) / 100.0;
    return true;
}

bool CSkillEffect_p_weight_limit::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aafde8.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::WeightLimit, m_Sfct, m_dBonus);
    return true;
}

bool CSkillEffect_p_weight_penalty::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab00d0.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::WeightPenalty, m_Sfct, m_dBonus);
    return true;
}

bool CSkillEffect_p_vital_add::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab6654.
    L2_GUARD_FUNCTION();
    creature.m_SkillDurationMod.m_VitalityBonusPer += m_VitalityBonus;
    return true;
}

bool CSkillEffect_p_vital_add::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00abf674.
    L2_GUARD_FUNCTION();
    creature.m_SkillDurationMod.m_VitalityBonusPer =
        std::max(creature.m_SkillDurationMod.m_VitalityBonusPer - m_VitalityBonus, 0.0);
    return true;
}

bool CSkillEffect_p_vital_sub::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab6704.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::AddI32(&creature.m_SkillDurationMod.m_nVitalitySubPer, m_nVitalitySub);
    return true;
}

bool CSkillEffect_p_vital_sub::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab67ac.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::AddI32(&creature.m_SkillDurationMod.m_nVitalitySubPer, -m_nVitalitySub);
    if (creature.m_SkillDurationMod.m_nVitalitySubPer < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] p_vital_sub m_nVitalitySubPer is minus.",
        //     L"SkillFxFunc_pump.cpp",
        //     0x18c1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] p_vital_sub m_nVitalitySubPer is minus.",
        //     L"SkillFxFunc_pump.cpp",
        //     0x18c1)
        L2_LOG_ERROR(
            Log,
            "Vitality reduction became negative while ending p_vital_sub (effect contribution %d); resetting the total to "
            "0.",
            m_nVitalitySub
        );
        runtime::raw_atomic::ExchangeI32(&creature.m_SkillDurationMod.m_nVitalitySubPer, 0);
    }
    return true;
}

bool CSkillEffect_p_exp_add::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab688c.
    L2_GUARD_FUNCTION();
    if (m_Sfct == SFCT_PER) {
        creature.m_SkillDurationMod.m_ExpBonusPer += m_ExpBonus;
    } else if (m_Sfct == SFCT_DIFF) {
        creature.m_SkillDurationMod.m_ExpBonusDiff += m_ExpBonus;
    }
    return true;
}

bool CSkillEffect_p_exp_add::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab6948.
    L2_GUARD_FUNCTION();
    if (m_Sfct == SFCT_PER) {
        creature.m_SkillDurationMod.m_ExpBonusPer -= m_ExpBonus;
    } else if (m_Sfct == SFCT_DIFF) {
        creature.m_SkillDurationMod.m_ExpBonusDiff -= m_ExpBonus;
    }
    return true;
}

namespace {

void NotifyStatBonusChanged(CCreature& creature) {
    if (!creature.IsPC()) {
        return;
    }
    creature.ValidateChanged(VB_MAX);
    creature.CastUser()->ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_SKILL});
}

} // namespace

bool CSkillEffect_p_stat_up::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab4a88.
    L2_GUARD_FUNCTION();
    if (nType >= 8) {
        return false;
    }
    creature.m_SkillDurationMod.m_StatBonus.m_N[nType] += nBonus;
    NotifyStatBonusChanged(creature);
    return true;
}

bool CSkillEffect_p_stat_up::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab4b84.
    L2_GUARD_FUNCTION();
    if (nType >= 8) {
        return false;
    }
    creature.m_SkillDurationMod.m_StatBonus.m_N[nType] -= nBonus;
    NotifyStatBonusChanged(creature);
    return true;
}

bool CSkillEffect_p_stat_up_limited::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab4c80.
    L2_GUARD_FUNCTION();
    if (nType >= 8) {
        return false;
    }
    creature.m_SkillDurationMod.m_StatBonusLimited.m_N[nType] += nBonus;
    NotifyStatBonusChanged(creature);
    return true;
}

bool CSkillEffect_p_stat_up_limited::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab4d7c.
    L2_GUARD_FUNCTION();
    if (nType >= 8) {
        return false;
    }
    creature.m_SkillDurationMod.m_StatBonusLimited.m_N[nType] -= nBonus;
    NotifyStatBonusChanged(creature);
    return true;
}

bool CSkillEffect_p_statbonus_skillcritical::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abaabc.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const user = creature->CastUser();
    double statBonus = 0.0;
    switch (m_eParamType) {
        case PARAM_INT:
            statBonus = user->IntBonusF();
            break;
        case PARAM_STR:
            statBonus = user->StrBonusF();
            break;
        case PARAM_CON:
            statBonus = user->ConBonusF();
            break;
        case PARAM_MEN:
            statBonus = user->MenBonusF();
            break;
        case PARAM_DEX:
            statBonus = user->DexBonusF();
            break;
        case PARAM_WIT:
            statBonus = user->WitBonusF();
            break;
        default:
            break;
    }

    user->m_SkillMod.m_Mods[CCreatureSkillMod::PhysicalSkillCriticalProbByStat].m_Per +=
        ((m_dBonus / 100.0 + 1.0) * statBonus - 1.185) * 2.5;
    return true;
}

bool CSKillEffect_p_statbonus_speed::Pump(CCreature* creature, const CSkillInfo*, int) {
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const user = creature->CastUser();
    double stat = 0.0;
    switch (m_eParamType) {
        case PARAM_INT:
            stat = static_cast<double>(user->m_data->m_Stat.m_N[0]);
            break;
        case PARAM_STR:
            stat = static_cast<double>(user->m_data->m_Stat.m_N[1]);
            break;
        case PARAM_CON:
            stat = static_cast<double>(user->m_data->m_Stat.m_N[2]);
            break;
        case PARAM_MEN:
            stat = static_cast<double>(user->m_data->m_Stat.m_N[3]);
            break;
        case PARAM_DEX:
            stat = static_cast<double>(user->m_data->m_Stat.m_N[4]);
            break;
        case PARAM_WIT:
            stat = static_cast<double>(user->m_data->m_Stat.m_N[5]);
            break;
        case PARAM_LUC:
            stat = static_cast<double>(user->m_data->m_Stat.m_N[6]);
            break;
        case PARAM_CHA:
            stat = static_cast<double>(user->m_data->m_Stat.m_N[7]);
            break;
        default:
            break;
    }

    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::Speed, SFCT_DIFF, (m_dBonus / 100.0 + 1.0) * stat - 55.0);
    return true;
}

bool CSkillEffect_p_instant_kill_resist::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abfb70.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    creature.m_SkillDurationMod.m_dInstantKillResist += m_dResist;
    return true;
}

bool CSkillEffect_p_instant_kill_resist::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00abfc54.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    creature.m_SkillDurationMod.m_dInstantKillResist =
        std::max(creature.m_SkillDurationMod.m_dInstantKillResist - m_dResist, 0.0);
    return true;
}

bool CSkillEffect_p_hp_special_add::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00abff4c.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    creature.m_SkillDurationMod.m_dAddedHPRecovery =
        runtime::Win64CvttDoubleToInt32(static_cast<double>(creature.m_SkillDurationMod.m_dAddedHPRecovery) + m_dAmount);
    return true;
}

bool CSkillEffect_p_hp_special_add::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ac0038.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    creature.m_SkillDurationMod.m_dAddedHPRecovery =
        runtime::Win64CvttDoubleToInt32(static_cast<double>(creature.m_SkillDurationMod.m_dAddedHPRecovery) - m_dAmount);
    return true;
}

bool CSkillEffect_p_reflect_dd_resist::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abf44c.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    creature.m_SkillDurationMod.m_dReflectDDResistPer += m_resisteRatio;
    return true;
}

bool CSkillEffect_p_reflect_dd_resist::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00abf530.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    creature.m_SkillDurationMod.m_dReflectDDResistPer =
        std::max(creature.m_SkillDurationMod.m_dReflectDDResistPer - m_resisteRatio, 0.0);
    return true;
}

bool CSkillEffect_p_damage_shield_resist::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abf970. The target protects this
    // duration field with CCreatureSkillMod::m_Lock.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillMod.m_Lock);
    creature.m_SkillDurationMod.m_dDamageShieldResist += static_cast<double>(m_nResist);
    return true;
}

bool CSkillEffect_p_damage_shield_resist::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00abfa58. Preserve the same cross-modifier lock as
    // PumpStart instead of normalizing it to the duration lock.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillMod.m_Lock);
    creature.m_SkillDurationMod.m_dDamageShieldResist =
        std::max(creature.m_SkillDurationMod.m_dDamageShieldResist - static_cast<double>(m_nResist), 0.0);
    return true;
}

bool CSkillEffect_p_damage_shield_resist_castle_siege::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ac064c.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !source->IsPC()) {
        return false;
    }

    User* const sourceUser = source->CastUser();
    if (sourceUser->m_AreaMod.m_bIsInPvPZone || !sourceUser->m_AreaMod.m_bIsInBattleField) {
        return false;
    }

    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    creature.m_SkillDurationMod.m_dDamageShieldResist += static_cast<double>(m_nResist);
    return true;
}

bool CSkillEffect_p_damage_shield_resist_castle_siege::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac0788.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    creature.m_SkillDurationMod.m_dDamageShieldResist =
        std::max(creature.m_SkillDurationMod.m_dDamageShieldResist - static_cast<double>(m_nResist), 0.0);
    return true;
}

bool CSkillEffect_p_add_vital_bonus::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo* skillInfo, SkillPumpStartType type
) const {
    // Current L2Server 0x00ac0f38.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    CSPointer<User> userPointer(user, ObjectRefType::FunctionChain);

    int itemClassId = 0;
    int itemCount = 0;
    if (skillInfo != nullptr) {
        itemClassId = skillInfo->m_nConsumeItem;
        itemCount = static_cast<int>(skillInfo->m_nConsumeItemCount);
    }
    if (type == SPST_CASTING &&
        !user->IncVitalItemConsumeCount(VitalityRestore::BY_SKILL, itemClassId, static_cast<std::int64_t>(itemCount))) {
        return false;
    }

    user->AddAddedVitalityBonusPer(m_nBonusPer);
    user->SendVitalityEffectInfo(nullptr);
    return true;
}

bool CSkillEffect_p_add_vital_bonus::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ac10d4.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    CSPointer<User> userPointer(user, ObjectRefType::FunctionChain);
    user->AddAddedVitalityBonusPer(-m_nBonusPer);
    user->SendVitalityEffectInfo(nullptr);
    return true;
}

bool CSkillEffect_p_vital_rate::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ac14ac.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    CSPointer<User> userPointer(user, ObjectRefType::FunctionChain);
    if (user == nullptr) {
        return false;
    }
    user->AddAddedVitalityBonusPer(m_nBonusPer);
    user->SendVitalityEffectInfo(nullptr);
    return true;
}

bool CSkillEffect_p_vital_rate::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ac15e4.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    CSPointer<User> userPointer(user, ObjectRefType::FunctionChain);
    if (user == nullptr) {
        return false;
    }
    user->AddAddedVitalityBonusPer(-m_nBonusPer);
    user->SendVitalityEffectInfo(nullptr);
    return true;
}

bool CSkillEffect_p_inc_spoil_rate::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00abd018.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->AddSpoilBonusPer(m_nIncPer);
    return true;
}

bool CSkillEffect_p_inc_spoil_rate::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00abd0ec.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->AddSpoilBonusPer(-m_nIncPer);
    return true;
}

bool CSkillEffect_p_anti_cubic::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab5fe4.
    L2_GUARD_FUNCTION();
    ++creature.m_SkillDurationMod.m_AntiCubic;
    creature.DespawnAllCubic(false);
    return true;
}

bool CSkillEffect_p_anti_cubic::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab6090.
    L2_GUARD_FUNCTION();
    int& antiCubic = creature.m_SkillDurationMod.m_AntiCubic;
    if (antiCubic < 1) {
        return false;
    }
    --antiCubic;
    antiCubic = std::max(antiCubic, 0);
    return true;
}

bool CSkillEffect_p_block_chat::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab4234.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    creature.CastUser()->m_bCanSay = false;
    return true;
}

bool CSkillEffect_p_block_chat::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab42f8.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    creature.CastUser()->m_bCanSay = true;
    return true;
}

bool CSkillEffect_p_ignore_death::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab8d48.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*creature.m_lock);
        creature.m_data->m_bUndying = true;
    }
    return true;
}

bool CSkillEffect_p_ignore_death::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab8e1c.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*creature.m_lock);
        creature.m_data->m_bUndying = false;
    }
    return true;
}

bool CSkillEffect_p_weak_death_penalty::PumpStart(
    CCreature&, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00aba0ac.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !source->IsPC()) {
        return false;
    }
    source->CastUser()->m_data->m_bWeakenedDeathPenalty = true;
    return true;
}

bool CSkillEffect_p_weak_death_penalty::PumpEnd(CCreature&, CCreature* source, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00aba17c.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !source->IsPC()) {
        return false;
    }
    source->CastUser()->m_data->m_bWeakenedDeathPenalty = false;
    return true;
}

bool CSkillEffect_p_stop_consume_vital_point::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo* skillInfo, SkillPumpStartType type
) const {
    // Current L2Server 0x00ab8a84.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }

    int itemClassId = 0;
    std::int64_t itemCount = 0;
    if (skillInfo != nullptr) {
        itemClassId = skillInfo->m_nConsumeItem;
        itemCount = skillInfo->m_nConsumeItemCount;
    }
    if (type == SPST_CASTING && !user->IncVitalItemConsumeCount(m_nType, itemClassId, itemCount)) {
        return false;
    }

    user->SuspendConsumingVitalPoint(m_nType);
    user->SendVitalityEffectInfo(nullptr);
    return true;
}

bool CSkillEffect_p_stop_consume_vital_point::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ab8ba8.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    return user != nullptr && user->StopSuspendConsumingVitalPoint();
}

bool CSkillEffect_p_stop_vitality_effect::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abb2fc.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->SuspendConsumingVitalPoint(m_nType);
    user->SuspendVitalityEffect(m_nType);
    return true;
}

bool CSkillEffect_p_stop_vitality_effect::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00abb3e4.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    return user != nullptr && user->StopSuspendVitaliltyEffect() && user->StopSuspendConsumingVitalPoint();
}

bool CSkillEffect_p_recharge_vital_point::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo* skillInfo, SkillPumpStartType type
) const {
    // Current L2Server 0x00ab9a84.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();

    int itemClassId = 0;
    int itemCount = 0;
    if (skillInfo != nullptr) {
        itemClassId = skillInfo->m_nConsumeItem;
        itemCount = static_cast<int>(skillInfo->m_nConsumeItemCount);
    }
    if (type == SPST_CASTING &&
        !user->IncVitalItemConsumeCount(m_nType, itemClassId, static_cast<std::int64_t>(itemCount))) {
        return false;
    }

    runtime::raw_atomic::AddI32(&user->m_SkillDurationMod.m_RechargeVitalityPoint, 1);
    user->SendVitalityEffectInfo(nullptr);
    return true;
}

bool CSkillEffect_p_recharge_vital_point::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ab9b9c.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    runtime::raw_atomic::AddI32(&user->m_SkillDurationMod.m_RechargeVitalityPoint, -1);
    return true;
}

bool CSkillEffect_p_vital_recharge_boost::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo* skillInfo, SkillPumpStartType type
) const {
    // Current L2Server 0x00abce34.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();

    int itemClassId = 0;
    int itemCount = 0;
    if (skillInfo != nullptr) {
        itemClassId = skillInfo->m_nConsumeItem;
        itemCount = static_cast<int>(skillInfo->m_nConsumeItemCount);
    }
    if (type == SPST_CASTING &&
        !user->IncVitalItemConsumeCount(VitalityRestore::BY_SKILL, itemClassId, static_cast<std::int64_t>(itemCount))) {
        return false;
    }

    user->SetVitalityRechargeBoost(m_nBoostPer);
    user->SendVitalityEffectInfo(nullptr);
    return true;
}

bool CSkillEffect_p_vital_recharge_boost::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00abcf50.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    creature.CastUser()->SetVitalityRechargeBoost(0);
    return true;
}

bool CSkillEffect_p_reduce_vital_consume_point::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo* skillInfo, SkillPumpStartType type
) const {
    // Current L2Server 0x00ac120c.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();

    int itemClassId = 0;
    int itemCount = 0;
    if (skillInfo != nullptr) {
        itemClassId = skillInfo->m_nConsumeItem;
        itemCount = static_cast<int>(skillInfo->m_nConsumeItemCount);
    }
    if (type == SPST_CASTING &&
        !user->IncVitalItemConsumeCount(VitalityRestore::BY_SKILL, itemClassId, static_cast<std::int64_t>(itemCount))) {
        return false;
    }

    CSPointer<User> userPointer(user, ObjectRefType::FunctionChain);
    user->SetVitalityConsumePointReducePer(userPointer, m_nReducePer);
    user->SendVitalityEffectInfo(nullptr);
    return true;
}

bool CSkillEffect_p_reduce_vital_consume_point::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac1384.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    CSPointer<User> userPointer(user, ObjectRefType::FunctionChain);
    user->SetVitalityConsumePointReducePer(userPointer, 0);
    return true;
}

bool CSkillEffect_p_protect_death_penalty::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab9c60.
    L2_GUARD_FUNCTION();
    if (creature.IsPC()) {
        creature.m_SkillMod.m_OnProtectDP = true;
        return true;
    }
    return false;
}

bool CSkillEffect_p_protect_death_penalty::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ab9d18.
    L2_GUARD_FUNCTION();
    if (creature.IsPC()) {
        creature.m_SkillMod.m_OnProtectDP = false;
        return true;
    }
    return false;
}

bool CSkillEffect_p_resist_dd_magic::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aace70.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::DefenceDDResist, m_Sfct, m_dBonus);
    return true;
}

bool CSkillEffect_p_reduce_cancel::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aaf4e8.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::CancelDamage, m_Sfct, m_dBonus);
    return true;
}

bool CSkillEffect_p_safe_fall_height::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aaf914.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::SafeHeight, m_Sfct, m_dBonus);
    return true;
}

bool CSkillEffect_p_breath::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aaf9c8.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::Breath, m_Sfct, m_dBonus);
    return true;
}

bool CSkillEffect_p_area_damage::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab051c.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::AreaDamage, m_Sfct, m_dBonus);
    return true;
}

bool CSkillEffect_p_fatal_blow_rate::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab13d0.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::FatalBlow, m_Sfct, m_dBonus);
    return true;
}

bool CSkillEffect_p_physical_shield_defence::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab2708.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::Shield, m_Sfct, m_dBonus);
    return true;
}

CSkillEffect_p_critical_rate_position_bonus::CSkillEffect_p_critical_rate_position_bonus(
    SkillFxPositionType position, double bonus, SkillFxChangeType changeType
) :
    m_Position(position), m_Bonus(bonus), m_ChangeType(changeType) {
    // Current L2Server 0x00c91714.
}

bool CSkillEffect_p_critical_rate_position_bonus::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab15fc.
    L2_GUARD_FUNCTION();
    CCreatureSkillMod::ModName modName;
    switch (m_Position) {
        case SFPT_FRONT:
            modName = CCreatureSkillMod::CriticalPositionFront;
            break;
        case SFPT_SIDE:
            modName = CCreatureSkillMod::CriticalPositionSide;
            break;
        case SFPT_BACK:
            modName = CCreatureSkillMod::CriticalPositionBack;
            break;
        default:
            return true;
    }
    L2SkillFunc::AddBonus(creature, modName, m_ChangeType, m_Bonus);
    return true;
}

bool CSkillEffect_p_direction_damage::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab8ef0.
    L2_GUARD_FUNCTION();
    CCreatureSkillMod::ModName modName;
    switch (m_Position) {
        case SFPT_FRONT:
            modName = CCreatureSkillMod::DefenceDamagePositionFront;
            break;
        case SFPT_SIDE:
            modName = CCreatureSkillMod::DefenceDamagePositionSide;
            break;
        case SFPT_BACK:
            modName = CCreatureSkillMod::DefenceDamagePositionBack;
            break;
        default:
            return true;
    }
    L2SkillFunc::AddBonus(creature, modName, m_ChangeType, m_Bonus);
    return true;
}

bool CSkillEffect_p_damage_by_attack::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac0124.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    double* playerAmount;
    double* monsterAmount;
    double* bossAmount;
    if (m_ChangeType == SFCT_PER) {
        playerAmount = &creature->m_SkillMod.m_PVPDamageBonusInfo.m_dAmountPer;
        monsterAmount = &creature->m_SkillMod.m_PVEDamageBonusInfo.m_dAmountPer;
        bossAmount = &creature->m_SkillMod.m_PVBossDamageBonusInfo.m_dAmountPer;
    } else {
        playerAmount = &creature->m_SkillMod.m_PVPDamageBonusInfo.m_dAmountDiff;
        monsterAmount = &creature->m_SkillMod.m_PVEDamageBonusInfo.m_dAmountDiff;
        bossAmount = &creature->m_SkillMod.m_PVBossDamageBonusInfo.m_dAmountDiff;
    }

    switch (m_EnemyType) {
        case ENT_PLAYER:
            *playerAmount = m_Amount + *playerAmount;
            break;
        case ENT_MONSTER:
            *monsterAmount = m_Amount + *monsterAmount;
            break;
        case ENT_BOSS:
            *bossAmount = m_Amount + *bossAmount;
            break;
        case ENT_ALL:
            *monsterAmount = *monsterAmount + m_Amount;
            *bossAmount = m_Amount + *bossAmount;
            break;
        default:
            break;
    }

    creature->m_SkillMod.m_PVEDamageBonusInfo.m_dAmountPer =
        std::max(-90.0, creature->m_SkillMod.m_PVEDamageBonusInfo.m_dAmountPer);
    creature->m_SkillMod.m_PVPDamageBonusInfo.m_dAmountPer =
        std::max(-90.0, creature->m_SkillMod.m_PVPDamageBonusInfo.m_dAmountPer);
    creature->m_SkillMod.m_PVBossDamageBonusInfo.m_dAmountPer =
        std::max(-90.0, creature->m_SkillMod.m_PVBossDamageBonusInfo.m_dAmountPer);
    return true;
}

bool CSkillEffect_p_position_damage::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aba24c.
    L2_GUARD_FUNCTION();
    CCreatureSkillMod::ModName modName;
    switch (m_Position) {
        case SFPT_FRONT:
            modName = CCreatureSkillMod::PositionDamageFront;
            break;
        case SFPT_SIDE:
            modName = CCreatureSkillMod::PositionDamageSide;
            break;
        case SFPT_BACK:
            modName = CCreatureSkillMod::PositionDamageBack;
            break;
        default:
            return true;
    }
    L2SkillFunc::AddBonus(creature, modName, m_ChangeType, m_Bonus);
    return true;
}

bool CSkillEffect_p_physical_skill_critical_rate::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aba634.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::PhysicalSkillCriticalProb, m_ChangeType, m_Bonus);
    return true;
}

CSkillEffect_p_physical_skill_critical_damage::CSkillEffect_p_physical_skill_critical_damage(
    double bonus, SkillFxChangeType changeType
) :
    m_ChangeType(changeType), m_Bonus(bonus) {
    // Current L2Server 0x00c91878.
}

bool CSkillEffect_p_physical_skill_critical_damage::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aba6e8.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::PhysicalSkillCriticalDamage, m_ChangeType, m_Bonus);
    return true;
}

bool CSkillEffect_p_hit_summon::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aba79c.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::HitSummon, m_ChangeType, m_Bonus);
    return true;
}

bool CSkillEffect_p_dispel_by_realtarget::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abdb94.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }
    creature->m_SkillMod.m_dRealTargetDispelProb = m_Probability;
    return true;
}

bool CSkillEffect_p_heal_effect_add::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab0438.
    L2_GUARD_FUNCTION();
    CCreatureSkillMod::Mod& mod = creature->m_SkillMod.m_Mods[CCreatureSkillMod::HealBonus];
    if (m_ChangeType == SFCT_PER) {
        mod.m_Per *= (m_Bonus + 100.0) / 100.0;
    } else if (m_ChangeType == SFCT_DIFF) {
        mod.m_Diff += m_Bonus;
    }
    return true;
}

bool CSkillEffect_p_absorb_shield::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab375c.
    L2_GUARD_FUNCTION();
    creature.m_SkillMod.m_bLimitDamageInAbsorbShield = m_LimitDamage;
    creature.m_SkillMod.m_dAbsorbShield = m_ShieldHP <= 0.0 ? 0.0 : m_ShieldHP;
    return true;
}

bool CSkillEffect_p_absorb_shield::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab381c.
    L2_GUARD_FUNCTION();
    creature.m_SkillMod.m_dAbsorbShield = 0.0;
    creature.m_SkillMod.m_bLimitDamageInAbsorbShield = false;
    return true;
}

bool CSkillEffect_p_absorb_shield_status::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abf31c.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !source->IsPC() || !creature.IsPC() || m_StatType < VALIDATE::PDEFEND ||
        m_StatType >= VALIDATE::PATTACKSPEED) {
        return false;
    }

    double shield = source->m_data->m_dParam[static_cast<int>(m_StatType)] * m_Rate / 100.0;
    if (shield < 0.0) {
        shield = 0.0;
    } else if (shield > 150000.0) {
        shield = 150000.0;
    }
    creature.m_SkillMod.m_dAbsorbShield = shield;
    return true;
}

bool CSkillEffect_p_absorb_shield_status::PumpEnd(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ab38b8.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !source->IsPC() || !creature.IsPC()) {
        return false;
    }
    creature.m_SkillMod.m_dAbsorbShield = 0.0;
    return true;
}

bool CSkillEffect_p_mp_vampiric_attack::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab4018.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    const int index = creature->m_SkillMod.m_nMPVampiricIndex;
    if (index >= 10) {
        return false;
    }
    creature->m_SkillMod.m_nMPVampiricIndex = index + 1;
    creature->m_SkillMod.m_MPVampiric[index].m_dBonus = m_VampiricRatio;
    creature->m_SkillMod.m_MPVampiric[index].m_dProb = m_Probability;
    return true;
}

bool CSkillEffect_p_alter_receive_normal_damage::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abac3c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }
    if (creature->IsNPC()) {
        creature->m_SkillMod.m_stAlterNormalDamage.m_nAlterRatio = m_Ratio;
        creature->m_SkillMod.m_stAlterNormalDamage.m_eType = m_TargetType;
    }
    return true;
}

bool CSkillEffect_p_energy_skill_power::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abc7cc.
    L2_GUARD_FUNCTION();
    if (creature != nullptr) {
        L2SkillFunc::AddBonus(creature, CCreatureSkillMod::EnergyAttack, m_ChangeType, m_Bonus);
    }
    return creature != nullptr;
}

bool CSkillEffect_p_droprate_modify::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abb4c4.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->AddDropRateByRune(0, m_DropRate[0]);
    user->AddDropRateByRune(1, m_DropRate[1]);
    return true;
}

bool CSkillEffect_p_droprate_modify::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00abb5b4.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->AddDropRateByRune(0, runtime::NegateInt32Wrapping(m_DropRate[0]));
    user->AddDropRateByRune(1, runtime::NegateInt32Wrapping(m_DropRate[1]));
    return true;
}

bool CSkillEffect_p_dropamount_modify::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abb6ac.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->AddDropAmountByRune(0, m_DropAmount[0]);
    user->AddDropAmountByRune(1, m_DropAmount[1]);
    return true;
}

bool CSkillEffect_p_dropamount_modify::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00abb79c.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->AddDropAmountByRune(0, runtime::NegateInt32Wrapping(m_DropAmount[0]));
    user->AddDropAmountByRune(1, runtime::NegateInt32Wrapping(m_DropAmount[1]));
    return true;
}

bool CSkillEffect_p_mp_friendly_payback::PumpStart(
    CCreature&, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abda04.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !source->IsPC()) {
        return false;
    }
    source->m_SkillMod.m_dMpFriendlyPaybackPer = m_PaybackPercent;
    source->m_SkillMod.m_dMpFriendlyPaybackProb = m_Probability;
    return true;
}

bool CSkillEffect_p_mp_friendly_payback::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00abdad8.
    L2_GUARD_FUNCTION();
    if (creature.IsPC()) {
        creature.m_SkillMod.m_dMpFriendlyPaybackPer = 0.0;
        creature.m_SkillMod.m_dMpFriendlyPaybackProb = 0.0;
    }
    return false;
}

bool CSkillEffect_p_prevention_death::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abdc44.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    for (int index = 0; index < CCreatureSkillMod::PDP_MAX; ++index) {
        creature.m_SkillMod.m_bPreventDeathParam[index] = m_PreventDeath[index];
    }
    return true;
}

bool CSkillEffect_p_prevention_death::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00abdd1c.
    L2_GUARD_FUNCTION();
    creature.m_SkillMod.m_bPreventDeathParam[0] = false;
    creature.m_SkillMod.m_bPreventDeathParam[1] = false;
    creature.m_SkillMod.m_bPreventDeathParam[2] = false;
    return true;
}

bool CSkillEffect_p_get_damage_limit_status::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab908c.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !source->IsPC() || !creature.IsPC() || m_StatType < VALIDATE::PDEFEND ||
        m_StatType >= VALIDATE::PATTACKSPEED) {
        return false;
    }

    creature.m_SkillMod.m_bIsDamageLimitedByStatus = true;
    const double stat = source->m_data->m_dParam[static_cast<int>(m_StatType)];
    if (stat > 0.0) {
        const double quotient = static_cast<double>(m_Constant) / stat;
        const double calculated = quotient * quotient + static_cast<double>(m_AddedMinimum);
        creature.m_SkillMod.m_dDamageLimit = calculated <= 150000.0 ? calculated : 150000.0;
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"p_get_damage_limit_status::PumpStart[%s], caster[%s] stat value is 0",
    //     creature.GetName(),
    //     source->GetName())
    // L2_LOG_ERROR(Log,
    //     L"p_get_damage_limit_status::PumpStart[%s], caster[%s] stat value is 0",
    //     creature.GetName(),
    //     source->GetName())
    // L2_LOG_ERROR(Log,
    //     "Cannot initialize p_get_damage_limit_status for '%s' from caster '%s': status %d has value %.17g and fails the
    //     positive-value check. The damage-limit flag is already enabled. The limit is left unchanged.",
    //     WideToUtf8(creature.GetName()).c_str(), WideToUtf8(source->GetName()).c_str(),
    //     static_cast<int>(m_StatType), stat)
    L2_LOG_ERROR(
        Log,
        "Cannot initialize p_get_damage_limit_status for '%s' from caster '%s': status %d has value %.17g and fails the "
        "positive-value check. The damage-limit flag is already enabled; the limit is left unchanged.",
        Utf16ToUtf8(creature.GetName()).c_str(), Utf16ToUtf8(source->GetName()).c_str(), static_cast<int>(m_StatType), stat
    );
    return false;
}

bool CSkillEffect_p_get_damage_limit_status::PumpEnd(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ab922c.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !source->IsPC() || !creature.IsPC()) {
        return false;
    }
    creature.m_SkillMod.m_bIsDamageLimitedByStatus = false;
    creature.m_SkillMod.m_dDamageLimit = 0.0;
    return true;
}

bool CSkillEffect_p_sub_exp::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab6c6c.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    CSharedCreatureData* const data = user->m_data;
    for (int index = 0; index < 4; ++index) {
        if (index != data->m_nSubJobId && data->m_nLevel < data->m_SubJobLevel[data->m_nSubJobId]) {
            if (m_ChangeType == SFCT_PER) {
                user->m_SkillMod.m_SubJobExpAdjustmentPer =
                    runtime::AddInt32Wrapping(user->m_SkillMod.m_SubJobExpAdjustmentPer, m_ExpBonus);
            } else if (m_ChangeType == SFCT_DIFF) {
                user->m_SkillMod.m_SubJobExpAdjustmentDiff =
                    runtime::AddInt32Wrapping(user->m_SkillMod.m_SubJobExpAdjustmentDiff, m_ExpPenalty);
            }
            break;
        }
    }
    return true;
}

bool CSkillEffect_p_sub_vp::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab6d78.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    const bool belowMainClassLevel = user->m_data->m_nLevel < user->m_data->m_MainClassLevel;
    if (m_ChangeType == SFCT_PER) {
        int& value = user->m_SkillMod.m_SubJobVpAdjustmentPer;
        value = belowMainClassLevel ? runtime::AddInt32Wrapping(value, m_VitalityBonus)
                                    : runtime::SubtractInt32Wrapping(value, m_VitalityBonus);
    } else if (m_ChangeType == SFCT_DIFF) {
        int& value = user->m_SkillMod.m_SubJobVpAdjustmentDiff;
        value = belowMainClassLevel ? runtime::AddInt32Wrapping(value, m_VitalityPenalty)
                                    : runtime::SubtractInt32Wrapping(value, m_VitalityPenalty);
    }
    return true;
}

bool CSkillEffect_p_physical_penetrate::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac0b40.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    const double factor = 1.0 - m_PenetrationRate;
    if (m_DefenceType == 0 || m_DefenceType == 1) {
        const double armor = factor * creature->m_SkillMod.m_dArmorPenetrateFactor;
        creature->m_SkillMod.m_dArmorPenetrateFactor = armor <= 0.5 ? 0.5 : armor;
    }
    if (m_DefenceType == 0 || m_DefenceType == 2) {
        const double shield = factor * creature->m_SkillMod.m_dShieldPenetrateFactor;
        creature->m_SkillMod.m_dShieldPenetrateFactor = shield <= 0.5 ? 0.5 : shield;
    }
    return m_DefenceType >= 0 && m_DefenceType <= 2;
}

bool CSkillEffect_p_magical_penetrate::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac0ce8.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }
    const double penetration = (1.0 - m_PenetrationRate) * creature->m_SkillMod.m_dMagicalPenetrateFactor;
    creature->m_SkillMod.m_dMagicalPenetrateFactor = penetration <= 0.5 ? 0.5 : penetration;
    return true;
}

bool CSkillEffect_p_damage_by_attack_castle_siege::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00abd39c.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }
    User* const user = creature->CastUser();
    return !user->m_AreaMod.m_bIsInPvPZone && user->m_AreaMod.m_bIsInBattleField;
}

bool CSkillEffect_p_damage_by_attack_castle_siege::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac08a0.
    L2_GUARD_FUNCTION();
    double* playerAmount;
    double* monsterAmount;
    double* bossAmount;
    if (m_ChangeType == SFCT_PER) {
        playerAmount = &creature->m_SkillMod.m_PVPDamageBonusInfo.m_dAmountPer;
        monsterAmount = &creature->m_SkillMod.m_PVEDamageBonusInfo.m_dAmountPer;
        bossAmount = &creature->m_SkillMod.m_PVBossDamageBonusInfo.m_dAmountPer;
    } else {
        playerAmount = &creature->m_SkillMod.m_PVPDamageBonusInfo.m_dAmountDiff;
        monsterAmount = &creature->m_SkillMod.m_PVEDamageBonusInfo.m_dAmountDiff;
        bossAmount = &creature->m_SkillMod.m_PVBossDamageBonusInfo.m_dAmountDiff;
    }

    if (m_EnemyType == ENT_PLAYER) {
        *playerAmount = m_Amount + *playerAmount;
    } else if (m_EnemyType == ENT_MONSTER) {
        *monsterAmount = m_Amount + *monsterAmount;
    } else if (m_EnemyType == ENT_BOSS) {
        *bossAmount = m_Amount + *bossAmount;
    } else if (m_EnemyType == ENT_ALL) {
        *monsterAmount = *monsterAmount + m_Amount;
        *bossAmount = m_Amount + *bossAmount;
    }

    creature->m_SkillMod.m_PVEDamageBonusInfo.m_dAmountPer =
        std::max(-90.0, creature->m_SkillMod.m_PVEDamageBonusInfo.m_dAmountPer);
    creature->m_SkillMod.m_PVPDamageBonusInfo.m_dAmountPer =
        std::max(-90.0, creature->m_SkillMod.m_PVPDamageBonusInfo.m_dAmountPer);
    creature->m_SkillMod.m_PVBossDamageBonusInfo.m_dAmountPer =
        std::max(-90.0, creature->m_SkillMod.m_PVBossDamageBonusInfo.m_dAmountPer);
    return true;
}

bool CSkillEffect_p_abnormal_time_change_to_abType::PumpStart(
    CCreature&, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abd6ac.
    L2_GUARD_FUNCTION();
    return source != nullptr;
}

bool CSkillEffect_p_abnormal_time_change_to_abType::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00abd740.
    L2_GUARD_FUNCTION();
    return creature != nullptr && creature->IsPC();
}

bool CSkillEffect_p_abnormal_time_change_to_abType::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac7a84.
    L2_GUARD_FUNCTION();
    double amountPer = 1.0;
    double amountDiff = 0.0;
    if (m_sfct == SFCT_PER) {
        amountPer = (m_dAmount + 100.0) / 100.0;
    } else if (m_sfct == SFCT_DIFF) {
        amountDiff = m_dAmount;
    }

    L2_GUARD(creature->m_SkillMod.m_Lock);
    const std::CDualKey<int, int> key(m_nAbnormalType, m_nMagicType);
    auto& values = creature->m_SkillMod.m_SkillChangeTimeToAbnormalType;
    const auto found = values.find(key);
    if (found == values.end()) {
        CCreatureSkillMod::BonusInfoInCondition info;
        info.m_dAmountPer = amountPer;
        info.m_dAmountDiff = amountDiff;
        values.emplace(key, info);
    } else {
        found->second.m_dAmountPer *= amountPer;
        found->second.m_dAmountDiff += amountDiff;
    }
    return true;
}

bool CSkillEffect_p_abnormal_time_change_to_abType::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac4194.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillMod.m_Lock);
    auto& values = creature.m_SkillMod.m_SkillChangeTimeToAbnormalType;
    const std::CDualKey<int, int> key(m_nAbnormalType, m_nMagicType);
    const auto found = values.find(key);
    if (found != values.end()) {
        values.erase(found);
    }
    if (creature.IsPC()) {
        User* const user = creature.CastUser();
        if (user != nullptr) {
            user->m_bAbnormalStatusChanged = true;
        }
    }
    return false;
}

bool CSkillEffect_p_abnormal_time_change_to_debuff::PumpStart(
    CCreature&, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abd7fc.
    L2_GUARD_FUNCTION();
    return source != nullptr;
}

bool CSkillEffect_p_abnormal_time_change_to_debuff::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00abd890.
    L2_GUARD_FUNCTION();
    return creature != nullptr && creature->IsPC();
}

bool CSkillEffect_p_abnormal_time_change_to_debuff::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac7c48.
    L2_GUARD_FUNCTION();
    double amountPer = 1.0;
    double amountDiff = 0.0;
    if (m_sfct == SFCT_PER) {
        amountPer = (m_dAmount + 100.0) / 100.0;
    } else if (m_sfct == SFCT_DIFF) {
        amountDiff = m_dAmount;
    }

    L2_GUARD(creature->m_SkillMod.m_Lock);
    const std::CDualKey<int, int> key(static_cast<int>(m_eDeBuffType), m_nMagicType);
    auto& values = creature->m_SkillMod.m_SkillChangeTimeToMagicType;
    const auto found = values.find(key);
    if (found == values.end()) {
        CCreatureSkillMod::BonusInfoInCondition info;
        info.m_dAmountPer = amountPer;
        info.m_dAmountDiff = amountDiff;
        values.emplace(key, info);
    } else {
        found->second.m_dAmountPer *= amountPer;
        found->second.m_dAmountDiff += amountDiff;
    }
    return true;
}

bool CSkillEffect_p_abnormal_time_change_to_debuff::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac42d8.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillMod.m_Lock);
    auto& values = creature.m_SkillMod.m_SkillChangeTimeToMagicType;
    const std::CDualKey<int, int> key(static_cast<int>(m_eDeBuffType), m_nMagicType);
    const auto found = values.find(key);
    // Current compares this iterator with the neighboring abnormal-type
    // map's sentinel. During the valid Pump/PumpEnd lifecycle the key is
    // present, so this keeps its observable erase without introducing
    // a host-STL erase(end()) crash for an invalid lifecycle.
    if (found != values.end()) {
        values.erase(found);
    }
    if (creature.IsPC()) {
        User* const user = creature.CastUser();
        if (user != nullptr) {
            user->m_bAbnormalStatusChanged = true;
        }
    }
    return false;
}

bool CSkillEffect_p_death_attack::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab7984.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }
    const int hpPercent =
        runtime::Win64CvttDoubleToInt32((creature->m_data->m_dHP / creature->m_data->m_dBaseHP) * 100.0);
    if (hpPercent <= m_HPRatio) {
        L2SkillFunc::AddBonus(creature, m_SkillMod, m_Sfct, static_cast<double>(m_Amount));
    }
    return true;
}

bool CSkillEffect_p_item_chance::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo* skillInfo, SkillPumpStartType
) const {
    // Current L2Server 0x00ace4cc.
    L2_GUARD_FUNCTION();
    creature.m_SkillDurationMod.SetItemChanceInfo(skillInfo->m_nSkillId, m_NpcType, m_vItemChanceType);
    return true;
}

bool CSkillEffect_p_item_chance::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo* skillInfo, SkillPumpEndType
) const {
    // Current L2Server 0x00ace6e4.
    L2_GUARD_FUNCTION();
    creature.m_SkillDurationMod.UnsetItemChanceInfo(skillInfo->m_nSkillId);
    return true;
}

bool CSkillEffect_p_stat_up_at_night::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab980c.
    L2_GUARD_FUNCTION();
    if (L2Common::IsAliveCreature(creature) && g_L2Time.m_bIsNight) {
        if (!creature->m_SkillMod.m_bAppliedStatUpAtNight) {
            CStats stats = creature->m_data->m_Stat + m_Bonus;
            creature->m_data->SetStats(stats);
            creature->m_SkillMod.m_bAppliedStatUpAtNight = true;
        }
        return true;
    }
    if (creature->m_SkillMod.m_bAppliedStatUpAtNight) {
        CStats stats = creature->m_data->m_Stat - m_Bonus;
        creature->m_data->SetStats(stats);
        creature->m_SkillMod.m_bAppliedStatUpAtNight = false;
    }
    return false;
}

bool CSkillEffect_p_potion_bonus::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aba4c4.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::PotionBonus, sfct, m_nBonus);
    return true;
}

int CSkillEffect_p_potion_bonus::CalcHealBonus(CCreature* creature, int amount) {
    // Current L2Server 0x00aba578.
    L2_GUARD_FUNCTION();
    if (creature != nullptr) {
        const CCreatureSkillMod::Mod& bonus = creature->m_SkillMod.m_Mods[CCreatureSkillMod::PotionBonus];
        amount = runtime::Win64CvttDoubleToInt32(static_cast<double>(amount) * bonus.m_Per + bonus.m_Diff);
    }
    return amount;
}

CSkillEffect_p_skill_critical::CSkillEffect_p_skill_critical() {
    // Current L2Server 0x00c91798. The parser writes m_eParamType
    // immediately after construction. The target constructor leaves it
    // uninitialized.
}

bool CSkillEffect_p_skill_critical::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab2da0.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_eParamType = m_eParamType;
    return true;
}

bool CSkillEffect_p_skill_critical_probability::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab2e44.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::SkillCritical, m_Sfct, static_cast<double>(m_Bonus));
    return true;
}

CSkillEffect_p_skill_critical_rate_fix::CSkillEffect_p_skill_critical_rate_fix(int probability) : m_nProb(probability) {
    // Current L2Server 0x00ac8168.
}

CSkillEffect_p_skill_critical_rate_fix::~CSkillEffect_p_skill_critical_rate_fix() = default;

void CSkillEffect_p_skill_critical_rate_fix::SetSkillList(std::vector<std::int64_t>* skillIds) {
    // Current L2Server 0x00acaf24.
    L2_GUARD_FUNCTION();
    for (std::size_t index = 0; index < skillIds->size(); ++index) {
        m_vId.push_back(static_cast<int>(runtime::CheckedVectorAt((*skillIds), index)));
    }
}

bool CSkillEffect_p_skill_critical_rate_fix::PumpStart(
    CCreature&, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ac8198.
    L2_GUARD_FUNCTION();
    if (source == nullptr) {
        return false;
    }

    L2_GUARD(source->m_SkillMod.m_lockSkillCritical);
    if (!source->m_SkillMod.m_mapSkillCritical.empty()) {
        return false;
    }

    for (const int skillId : m_vId) {
        auto found = source->m_SkillMod.m_mapSkillCritical.find(skillId);
        if (found == source->m_SkillMod.m_mapSkillCritical.end()) {
            source->m_SkillMod.m_mapSkillCritical.emplace(skillId, m_nProb);
        } else {
            found->second = m_nProb;
        }
    }
    return true;
}

bool CSkillEffect_p_skill_critical_rate_fix::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac46e8.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillMod.m_lockSkillCritical);
    creature.m_SkillMod.m_mapSkillCritical.clear();
    return false;
}

bool CSkillEffect_p_skill_power::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab2fd4. Ctor 0x00c917bc.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::PSkillPower, m_Sfct, m_dBonus);
    return true;
}

CSkillEffect_p_spell_power::CSkillEffect_p_spell_power(double bonus, SkillFxChangeType changeType) :
    m_dBonus(bonus), m_Sfct(changeType) {
    // Current L2Server 0x00ca1ea0.
}

bool CSkillEffect_p_spell_power::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab7c40.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::PSpellPower, m_Sfct, m_dBonus);
    return true;
}

bool CSkillEffect_p_critical_damage_position::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab2efc.
    L2_GUARD_FUNCTION();
    CCreatureSkillMod::ModName modName;
    switch (m_Position) {
        case SFPT_FRONT:
            modName = CCreatureSkillMod::CriticalDamagePositionFront;
            break;
        case SFPT_SIDE:
            modName = CCreatureSkillMod::CriticalDamagePositionSide;
            break;
        case SFPT_BACK:
            modName = CCreatureSkillMod::CriticalDamagePositionBack;
            break;
        default:
            return true;
    }
    L2SkillFunc::AddBonus(creature, modName, m_Sfct, static_cast<double>(m_Bonus));
    return true;
}

bool CSkillEffect_p_physical_abnormal_resist::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab7cf4.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::PAbnormalResist, m_Sfct, static_cast<double>(m_Amount));
    return true;
}

bool CSkillEffect_p_magic_abnormal_resist::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab7dac.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::MAbnormalResist, m_Sfct, static_cast<double>(m_Amount));
    return true;
}

bool CSkillEffect_p_magic_cancel_resist::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab7e64.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::MCancelResist, m_Sfct, static_cast<double>(m_Amount));
    return true;
}

bool CSkillEffect_p_magic_avoid::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab7f1c.
    L2_GUARD_FUNCTION();
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::MAvoid, m_Sfct, static_cast<double>(m_Amount));
    return true;
}

bool CSkillEffect_p_abnormal_remove_by_hit::Pump(CCreature*, const CSkillInfo*, int) {
    // Current L2Server 0x00ab28e4. The counters are consumed by the
    // abnormal-status owner. Applying this pump itself only succeeds.
    L2_GUARD_FUNCTION();
    return true;
}

bool CSkillEffect_p_abnormal_remove_by_dmg::Pump(CCreature*, const CSkillInfo*, int) {
    // Current L2Server 0x00ab2964.
    L2_GUARD_FUNCTION();
    return true;
}

bool CSkillEffect_p_shield_defence_rate::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aaf59c.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_dShieldDefenceModPer *= (m_dBonus + 100.0) / 100.0;
    return true;
}

bool CSkillEffect_p_avoid_agro::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00aafc90.
    L2_GUARD_FUNCTION();
    return L2Common::IsAliveCreature(creature);
}

bool CSkillEffect_p_avoid_agro::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aafd3c.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bSeeEvent = false;
    creature->m_SkillMod.m_nAvoidAgroProb = nProb;
    return true;
}

bool CSkillEffect_p_cubic_mastery::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab0020.
    L2_GUARD_FUNCTION();
    creature->m_maxCubic = nCount < 3 ? nCount : 3;
    return true;
}

bool CSkillEffect_p_mana_charge::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab0184.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsCreature()) {
        return false;
    }
    creature->m_SkillMod.m_nManaChargeBonus += nBonus;
    return true;
}

CSkillEffect_p_vampiric_attack::CSkillEffect_p_vampiric_attack(double bonus, double probability) :
    dBonus(bonus), dProb(probability) {
    // Current L2Server 0x00c916e8.
}

bool CSkillEffect_p_vampiric_attack::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab0810.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bIsVampiricState = true;
    creature->m_SkillMod.m_dVampiricBonusPer = ((dBonus + 100.0) / 100.0 + creature->m_SkillMod.m_dVampiricBonusPer) - 1.0;

    const double numerator = dProb * dBonus + creature->m_SkillMod.m_dVampiricProbNumerator;
    creature->m_SkillMod.m_dVampiricProbNumerator = numerator;

    const double denominator = dBonus + creature->m_SkillMod.m_dVampiricProbDenominator;
    creature->m_SkillMod.m_dVampiricProbDenominator = denominator;
    if (denominator == 0.0) {
        creature->m_SkillMod.m_dVampiricProb = 1.0;
    } else {
        creature->m_SkillMod.m_dVampiricProb = (numerator / denominator) / 100.0;
    }
    if (creature->m_SkillMod.m_dVampiricProb > 1.0) {
        creature->m_SkillMod.m_dVampiricProb = 1.0;
    }
    return true;
}

bool CSkillEffect_p_vampiric_defence::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab095c.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_dVampiricDefencePer += dDefence / 100.0;
    return true;
}

bool CSkillEffect_p_nopkcount::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aba850.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_dPKCountIncreaseProb = static_cast<double>(m_nProb);
    return true;
}

bool CSkillEffect_p_avoid_skill::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab5a50.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }
    creature->m_SkillMod.m_SkillReacts[CCreatureSkillMod::AvoidSkill].nProb[nMagicType] = nProb;
    return true;
}

bool CSkillEffect_p_counter_skill::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab5b00.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }
    creature->m_SkillMod.m_SkillReacts[CCreatureSkillMod::CounterSkill].nProb[nMagicType] = nProb;
    return true;
}

bool CSkillEffect_p_avoid_counter_skill::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab5bb0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }
    creature->m_SkillMod.m_SkillReacts[CCreatureSkillMod::AvoidSkill].nProb[nMagicType] = nProb;
    creature->m_SkillMod.m_SkillReacts[CCreatureSkillMod::CounterSkill].nProb[nMagicType] = nProb;
    return true;
}

bool CSkillEffect_p_focus_energy::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab6ba4.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }
    creature->m_SkillMod.m_MaxEnergyCount = m_MaxEnergy;
    return true;
}

bool CSkillEffect_p_create_item_critical::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abad1c.
    L2_GUARD_FUNCTION();
    if (creature != nullptr && creature->IsPC()) {
        creature->m_SkillMod.m_dCreateItemCriticalProb += m_dRatio;
    }
    return true;
}

bool CSkillEffect_p_Fishing_Monster_prob::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abade4.
    L2_GUARD_FUNCTION();
    if (creature != nullptr && creature->IsPC()) {
        creature->m_SkillMod.m_dFishingMonsterProb += m_dRatio;
    }
    return true;
}

bool CSkillEffect_p_Fishing_start_time::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abaeac.
    L2_GUARD_FUNCTION();
    if (creature != nullptr && creature->IsPC()) {
        creature->m_SkillMod.m_dFishingStartTime += m_dRatio;
    }
    return true;
}

bool CSkillEffect_p_Fishing_instantkill_prob::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abaf74.
    L2_GUARD_FUNCTION();
    if (creature != nullptr && creature->IsPC()) {
        creature->m_SkillMod.m_nFishingInstantP += m_nRatio;
    }
    return true;
}

bool CSkillEffect_p_change_fishing_mastery::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00ab5894.
    L2_GUARD_FUNCTION();
    return creature != nullptr && creature->IsPC();
}

bool CSkillEffect_p_change_fishing_mastery::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab594c.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    if (nLevel <= user->m_pFishingController->m_nExperienceLevel) {
        creature->CastCreature()->m_SkillMod.m_nFishingChangeLevel = nLevel;
        creature->CastCreature()->m_SkillMod.m_dFishingChangeBonus = dBonus;
    }
    return true;
}

bool CSkillEffect_p_fishing_mastery::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ab6388.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    creature->CastUser()->m_pFishingController->m_nExperienceLevel = skillInfo->m_nLevel;
    creature->CastUser()->m_pFishingController->m_dExperienceBonus = dBonus;
    return true;
}

bool CSkillEffect_p_Fishing_Area_buff::PumpStart(
    CCreature&, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abb030.
    L2_GUARD_FUNCTION();
    return source != nullptr && source->IsPC() && source->CastUser() != nullptr;
}

bool CSkillEffect_p_Fishing_Area_buff::PumpEnd(CCreature&, CCreature* source, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ac02e0.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !source->IsPC()) {
        return false;
    }

    User* const user = source->CastUser();
    if (user == nullptr) {
        return false;
    }
    if (user->HasAbnormalStatusSlot(ABNS_FISHING_BLESS)) {
        user->_DeleteAbnormalStatus(ABNS_FISHING_BLESS, user, nullptr, true, SPET_REPLACE, false);
    }
    if (CAreaDB::inst().IsFishingZone(user->GetPos(), 0)) {
        return user->Restart(RESTART_VILLAGE, 0);
    }
    return false;
}

bool CSkillEffect_p_fishing_damage::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00abc444.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->SetFishingDamageBonus(dDamage);
    return true;
}

bool CSkillEffect_p_fishing_damage::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00abc51c.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->SetFishingDamageBonus(0.0);
    return true;
}

bool CSkillEffect_p_damage_shield::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aafa7c.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bIsReverseAttack = true;
    creature->m_SkillMod.m_dReverseDamagePer = ((dRevPer + 100.0) / 100.0 + creature->m_SkillMod.m_dReverseDamagePer) - 1.0;
    return true;
}

bool CSkillEffect_p_block_debuff::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab2be0.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }
    creature->m_SkillMod.m_bBlockDeBuff = true;
    const double squaredDistance = m_dDist * m_dDist;
    creature->m_SkillMod.m_dBlockDeBuffDist = std::max(creature->m_SkillMod.m_dBlockDeBuffDist, squaredDistance);
    return true;
}

bool CSkillEffect_p_mp_shield::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab70a8.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }
    creature->m_SkillMod.m_MPShieldPer = m_nMPShieldPer;
    creature->m_SkillMod.m_MPShieldCostPer = m_nMPShieldCostPer;
    return true;
}

bool CSkillEffect_p_add_ensoul_prob::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abe338.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }
    creature->m_SkillMod.m_dEnsoulAddProb += m_dProb;
    return true;
}

bool CSkillEffect_p_charge_vitality::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aad8d0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }
    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }
    user->m_SkillMod.m_nRechargeParam += m_nParam;
    return true;
}

bool CSkillEffect_p_create_item::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aadedc. The inverted return value is literal.
    L2_GUARD_FUNCTION();
    const bool isPc = creature != nullptr && creature->IsPC();
    if (isPc) {
        creature->CastUser()->m_nDwarfCreateItemLevel = nLevel;
    }
    return !isPc;
}

bool CSkillEffect_p_create_common_item::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00aadfa8.
    L2_GUARD_FUNCTION();
    return creature->IsPC();
}

bool CSkillEffect_p_create_common_item::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aae058.
    L2_GUARD_FUNCTION();
    const bool isPc = creature->IsPC();
    if (isPc) {
        creature->CastUser()->m_nCommonCreateItemLevel = nLevel;
    }
    return isPc;
}

bool CSkillEffect_p_summon_point::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab8c74.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }
    creature->CastUser()->m_nMaxSummonPoint = m_nPoint;
    return true;
}

bool CSkillEffect_p_defence_damage::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab5514.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }
    if (m_Sfct == SFCT_PER) {
        creature->m_SkillMod.m_nInfectedSkillDamageUPPer += m_Rate;
    } else if (m_Sfct == SFCT_DIFF) {
        creature->m_SkillMod.m_nInfectedSkillDamageUPDiff += m_Rate;
    }
    creature->m_SkillMod.m_nInfectedSkillDamageUPPer = std::max(creature->m_SkillMod.m_nInfectedSkillDamageUPPer, -100);
    return true;
}

bool CSkillEffect_p_speed_out_of_night::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab949c.
    L2_GUARD_FUNCTION();
    if (!g_L2Time.m_bIsNight || !L2Common::IsAliveCreature(creature)) {
        return false;
    }

    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::Speed, SFCT_DIFF, m_Bonus);
    return true;
}

bool CSkillEffect_p_transfer_damage_pc::Pump(CCreature* creature, const CSkillInfo*, int transferTargetId) {
    // Current L2Server 0x00aac7c8.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }

    creature->m_SkillMod.m_TransferDamageTargetId = transferTargetId;
    creature->m_SkillMod.m_nTransferDamageRatio += static_cast<int>(m_Rate);
    creature->m_SkillMod.m_nTransferDamageRatio = std::min(creature->m_SkillMod.m_nTransferDamageRatio, 100);
    return true;
}

bool CSkillEffect_p_transfer_damage_to::Pump(CCreature* creature, const CSkillInfo*, const int transferTargetId) {
    // Current L2Server 0x00ac0e30.
    L2_GUARD_FUNCTION();
    CCreature* const target = g_CreatureSmartIdManager.GetObjectW(transferTargetId);
    if (!L2Common::IsAliveCreature(target)) {
        return false;
    }
    target->m_SkillMod.m_TransferDamageTargetId = creature->m_nId;
    target->m_SkillMod.m_nTransferDamageRatio += static_cast<int>(m_Rate);
    target->m_SkillMod.m_nTransferDamageRatio = std::min(target->m_SkillMod.m_nTransferDamageRatio, 100);
    return true;
}

bool CSkillEffect_p_transfer_damage_summon::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab0244.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const user = creature->CastUser();
    CSummon* const summon = user->GetOneSummonLive(User::SUMMON_ANY);
    if (summon == nullptr) {
        return false;
    }

    user->m_SkillMod.m_TransferDamageTargetId = static_cast<int>(summon->m_nId);
    user->m_SkillMod.m_nTransferDamageRatio += nBonus;
    user->m_SkillMod.m_nTransferDamageRatio = std::min(user->m_SkillMod.m_nTransferDamageRatio, 100);
    return true;
}

bool CSkillEffect_p_soul_eating::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aac8b0.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    if (m_MaxSoul < user->m_SoulCount) {
        user->DecreaseSoulCount(user->m_SoulCount - m_MaxSoul);
    } else {
        user->m_SkillMod.m_BarExpForSoulCollect = m_StandardExp;
    }
    user->m_SkillMod.m_SoulMaxCount = m_MaxSoul;
    return true;
}

bool CSkillEffect_p_pve_physical_attack_dmg_bonus::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab1e74.
    L2_GUARD_FUNCTION();
    if (sfct != SFCT_PER) {
        return true;
    }
    const double factor = (nBonus + 100.0) / 100.0;
    if (eEnemyType == ENT_MONSTER) {
        creature->m_SkillMod.m_dPvePhysicalAttackBonusPer *= factor;
    } else if (eEnemyType == ENT_BOSS) {
        creature->m_SkillMod.m_dPvBossPhysicalAttackBonusPer *= factor;
    } else if (eEnemyType == ENT_ALL) {
        creature->m_SkillMod.m_dPvBossPhysicalAttackBonusPer *= factor;
        creature->m_SkillMod.m_dPvePhysicalAttackBonusPer *= factor;
    }
    return true;
}

bool CSkillEffect_p_pve_physical_skill_dmg_bonus::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab1fc0.
    L2_GUARD_FUNCTION();
    if (sfct != SFCT_PER) {
        return true;
    }
    const double factor = (nBonus + 100.0) / 100.0;
    if (eEnemyType == ENT_MONSTER) {
        creature->m_SkillMod.m_dPvePhysicalSkillBonusPer *= factor;
    } else if (eEnemyType == ENT_BOSS) {
        creature->m_SkillMod.m_dPvBossPhysicalSkillBonusPer *= factor;
    } else if (eEnemyType == ENT_ALL) {
        creature->m_SkillMod.m_dPvBossPhysicalSkillBonusPer *= factor;
        creature->m_SkillMod.m_dPvePhysicalSkillBonusPer *= factor;
    }
    return true;
}

bool CSkillEffect_p_pve_magical_skill_dmg_bonus::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab210c.
    L2_GUARD_FUNCTION();
    if (sfct != SFCT_PER) {
        return true;
    }
    const double factor = (nBonus + 100.0) / 100.0;
    if (eEnemyType == ENT_MONSTER) {
        creature->m_SkillMod.m_dPveMagicalSkillBonusPer *= factor;
    } else if (eEnemyType == ENT_BOSS) {
        creature->m_SkillMod.m_dPvBossMagicalSkillBonusPer *= factor;
    } else if (eEnemyType == ENT_ALL) {
        creature->m_SkillMod.m_dPvBossMagicalSkillBonusPer *= factor;
        creature->m_SkillMod.m_dPveMagicalSkillBonusPer *= factor;
    }
    return true;
}

bool CSkillEffect_p_pvp_physical_attack_dmg_bonus::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab2258.
    L2_GUARD_FUNCTION();
    if (sfct == SFCT_PER) {
        creature->m_SkillMod.m_dPvpPhysicalAttackBonusPer *= (nBonus + 100.0) / 100.0;
    }
    return true;
}

bool CSkillEffect_p_pvp_physical_skill_dmg_bonus::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab2320.
    L2_GUARD_FUNCTION();
    if (sfct == SFCT_PER) {
        creature->m_SkillMod.m_dPvpPhysicalSkillBonusPer *= (nBonus + 100.0) / 100.0;
    }
    return true;
}

bool CSkillEffect_p_pvp_magical_skill_dmg_bonus::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab23e8.
    L2_GUARD_FUNCTION();
    if (sfct == SFCT_PER) {
        creature->m_SkillMod.m_dPvpMagicalSkillBonusPer *= (nBonus + 100.0) / 100.0;
    }
    return true;
}

bool CSkillEffect_p_pvp_physical_attack_defence_bonus::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab24b0.
    L2_GUARD_FUNCTION();
    if (sfct == SFCT_PER) {
        creature->m_SkillMod.m_dPvpPhysicalAttackDefenceBonusPer *= (100.0 - nBonus) / 100.0;
    }
    return true;
}

bool CSkillEffect_p_pvp_physical_skill_defence_bonus::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab2578.
    L2_GUARD_FUNCTION();
    if (sfct == SFCT_PER) {
        creature->m_SkillMod.m_dPvpPhysicalSkillDefenceBonusPer *= (100.0 - nBonus) / 100.0;
    }
    return true;
}

bool CSkillEffect_p_pvp_magical_skill_defence_bonus::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab2640.
    L2_GUARD_FUNCTION();
    if (sfct == SFCT_PER) {
        creature->m_SkillMod.m_dPvpMagicalSkillDefenceBonusPer *= (100.0 - nBonus) / 100.0;
    }
    return true;
}

bool CSkillEffect_p_block_attack::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aac9a4.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bCanAttack = false;
    return true;
}

bool CSkillEffect_p_block_act::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00aae120.
    L2_GUARD_FUNCTION();
    L2SkillFunc::BlockAction(creature, !skillInfo->m_IsFlySkill);
    return true;
}

bool CSkillEffect_p_transform_hangover::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aacd00.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_cannotTransform = true;
    return true;
}

bool CSkillEffect_p_luck::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00aadd80.
    L2_GUARD_FUNCTION();
    if (creature->m_data->m_nPKCount == 0 && creature->Level() < 10) {
        return true;
    }
    creature->m_bDecExp = true;
    return false;
}

bool CSkillEffect_p_luck::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aade48.
    L2_GUARD_FUNCTION();
    creature->m_bDecExp = false;
    return true;
}

bool CSkillEffect_p_block_spell::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aae4b8.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bCanSpell = false;
    return true;
}

bool CSkillEffect_p_passive::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00aafb4c.
    L2_GUARD_FUNCTION();
    return creature->IsNPC();
}

bool CSkillEffect_p_passive::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aafbfc.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bSeeEvent = false;
    return true;
}

bool CSkillEffect_p_physical_shield_defence_angle_all::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab19f4.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bIsAllAngleShieldDefence = true;
    return true;
}

bool CSkillEffect_p_physical_polarm_target_single::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab27bc.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bIsSingleTarget = true;
    return true;
}

bool CSkillEffect_p_abnormal_rate_limit::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab2850.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bAbnormalLimit = true;
    return true;
}

bool CSkillEffect_p_block_skill_physical::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab29e4.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bCanPSkill = false;
    return true;
}

bool CSkillEffect_p_block_skill_special::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab2a78.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }
    creature->m_SkillMod.m_bCanSSkill = false;
    return true;
}

bool CSkillEffect_p_block_buff::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab2b2c.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }
    creature->m_SkillMod.m_bBlockBuff = true;
    return true;
}

bool CSkillEffect_p_block_add_slot::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac9894.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }

    L2_GUARD(creature->m_SkillMod.m_Lock);
    for (const AbnormalStatusSlotType slot : m_AbnormalSlots) {
        creature->m_SkillMod.m_BlockedAddSlotType.insert(slot);
    }
    return true;
}

bool CSkillEffect_p_block_buff_slot::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00ac9730.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    {
        L2_GUARD(creature->m_SkillMod.m_Lock);
        for (const AbnormalStatusSlotType slot : m_AbnormalSlots) {
            creature->m_SkillMod.m_BlockedSlotType.insert(slot);
        }
    }

    if (!m_bNotDispel) {
        for (const AbnormalStatusSlotType slot : m_AbnormalSlots) {
            creature->DispelBySlot(slot, creature, -1, skillInfo, SPET_DISPEL, false);
        }
    }
    return true;
}

bool CSkillEffect_p_block_buff_slot::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType
) const {
    // Current L2Server 0x00ac19a8.
    L2_GUARD_FUNCTION();
    if (skillInfo == nullptr) {
        return false;
    }

    if (!m_bNotDispel) {
        for (const AbnormalStatusSlotType slot : m_AbnormalSlots) {
            creature.DispelBySlot(slot, source, -1, skillInfo, SPET_DISPEL, false);
        }
    }
    return true;
}

bool CSkillEffect_p_channel_clan::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab5800.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bCanCreateMPCC = true;
    return true;
}

bool CSkillEffect_p_pk_protect::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab7a84.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bIsPKProtect = true;
    return true;
}

bool CSkillEffect_p_expand_deco_slot::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac2670.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    creature->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(creature->m_SkillMod.m_dMaxDecoSlotCount, nCount, 0x36);

    int equippedCount = 0;
    for (int slot = ST_DECO1; slot <= ST_DECO6; ++slot) {
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(creature->m_data->m_nSlotIndex[slot]);
        if (item) {
            ++equippedCount;
        }
    }

    if (creature->m_SkillMod.m_dMaxDecoSlotCount < equippedCount) {
        for (int slot = ST_DECO1; slot <= ST_DECO6; ++slot) {
            creature->UnequipItem(static_cast<SlotType>(slot), false);
        }
    }
    return true;
}

bool CSkillEffect_p_expand_jewel_slot::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac2804.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    creature->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        creature->m_SkillMod.m_dMaxJewelSlotCount, nCount, 0x37
    );

    int equippedCount = 0;
    for (int slot = ST_JEWEL1; slot <= ST_JEWEL6; ++slot) {
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(creature->m_data->m_nSlotIndex[slot]);
        if (item) {
            ++equippedCount;
        }
    }

    if (creature->m_SkillMod.m_dMaxJewelSlotCount < equippedCount) {
        for (int slot = ST_JEWEL1; slot <= ST_JEWEL6; ++slot) {
            creature->UnequipItem(static_cast<SlotType>(slot), false);
        }
    }
    return true;
}

bool CSkillEffect_p_expand_agathion_main_slot::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac2998.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    creature->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        creature->m_SkillMod.m_dMaxAgathionMainSlotCount, m_nCount, 0x38
    );

    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(creature->m_data->m_nSlotIndex[ST_AGATHION_MAIN]);
    if (creature->m_SkillMod.m_dMaxAgathionMainSlotCount < static_cast<int>(static_cast<bool>(item))) {
        for (int slot = ST_AGATHION_MAIN; slot <= ST_AGATHION_SUB4; ++slot) {
            creature->UnequipItem(static_cast<SlotType>(slot), false);
        }
    }
    return true;
}

bool CSkillEffect_p_expand_agathion_sub_slot::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac2b54.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    creature->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        creature->m_SkillMod.m_dMaxAgathionSubSlotCount, m_nCount, 0x39
    );

    int equippedCount = 0;
    CSPointer<CItem> item;
    for (int slot = ST_AGATHION_SUB1; slot <= ST_AGATHION_SUB4; ++slot) {
        item = CItemFactory::Inst().FindObjectSP(creature->m_data->m_nSlotIndex[slot]);
        if (item) {
            ++equippedCount;
        }
    }

    if (creature->m_SkillMod.m_dMaxAgathionSubSlotCount < equippedCount) {
        for (int slot = ST_AGATHION_MAIN; slot <= ST_AGATHION_SUB4; ++slot) {
            creature->UnequipItem(static_cast<SlotType>(slot), false);
        }
    }
    return true;
}

bool CSkillEffect_p_crystallize::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00aadb90.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }

    User* const user = creature.CastUser();
    std::int32_t& crystallizeLevel = reinterpret_cast<std::int32_t&>(user->m_CrystallizeLevel);
    user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        crystallizeLevel, static_cast<std::int32_t>(eCrystalType), 0x35
    );
    user->ParamChanged(PCB_USER);
    return true;
}

bool CSkillEffect_p_crystallize::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00aadc88.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }

    User* const user = creature.CastUser();
    std::int32_t& crystallizeLevel = reinterpret_cast<std::int32_t&>(user->m_CrystallizeLevel);
    user->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(crystallizeLevel, 0, 0x35);
    user->ParamChanged(PCB_USER);
    return true;
}

bool CSkillEffect_p_block_skill_trigger::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abdf6c.
    L2_GUARD_FUNCTION();
    creature->m_SkillMod.m_bCanTSkill = false;
    return true;
}

bool CSkillEffect_p_block_skill::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ac6418.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }

    L2_GUARD(creature->m_SkillMod.m_Lock);
    creature->m_SkillMod.m_setBlockMagicType.insert(m_nMagicType);
    return true;
}

bool CSkillEffect_p_block_controll::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00aae374.
    L2_GUARD_FUNCTION();
    ++creature.m_SkillDurationMod.m_nBlockCanControllCount;
    return true;
}

bool CSkillEffect_p_block_controll::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00aae404.
    L2_GUARD_FUNCTION();
    int& count = creature.m_SkillDurationMod.m_nBlockCanControllCount;
    if (count <= 0) {
        return false;
    }
    --count;
    return true;
}

bool CSkillEffect_p_block_escape::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab9304.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*creature.m_lock);
        creature.m_bCanEscape = false;
    }
    return true;
}

bool CSkillEffect_p_block_escape::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab93d0.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*creature.m_lock);
        creature.m_bCanEscape = true;
    }
    return true;
}

bool CSkillEffect_p_block_move::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00aae1e4.
    L2_GUARD_FUNCTION();
    ++creature.m_SkillMod.m_nBlockMoveCount;
    creature.StopMove();
    if (creature.IsNPC()) {
        creature.CastNPC()->ParamChanged(NPCT_PUMP_BLOCK);
    } else if (creature.IsPC()) {
        creature.ParamChanged(std::int64_t{PCB_CHAR_SKILL});
    } else {
        creature.ParamChanged();
    }
    return true;
}

bool CSkillEffect_p_block_move::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00aae2d4.
    L2_GUARD_FUNCTION();
    if (creature.m_SkillMod.m_nBlockMoveCount <= 0) {
        return false;
    }
    --creature.m_SkillMod.m_nBlockMoveCount;
    return true;
}

bool CSkillEffect_p_block_target::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ac6e10.
    L2_GUARD_FUNCTION();
    const FVector position = creature.GetPos();
    creature.ChangeTarget(nullptr, CTR_DEFAULT);
    g_ptlUserMap->BroadcastToNeighborDist(
        &creature, 0x5000, position, 0x800, "cddddd", 0x24, static_cast<int>(creature.m_nId),
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), 0
    );
    creature.m_SkillMod.m_BlockTarget = true;
    return true;
}

bool CSkillEffect_p_block_target::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab7464.
    L2_GUARD_FUNCTION();
    creature.m_SkillMod.m_BlockTarget = false;
    return true;
}

bool CSkillEffect_p_fear::PumpStart(CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00aae5fc.
    L2_GUARD_FUNCTION();
    if ((creature.IsNPC() && creature.m_data->m_nExCrtEffect == 1) ||
        creature.m_SkillDurationMod.m_nPsychicPowerCount >= 1) {
        return false;
    }

    creature.DoNothing();
    // creature.ActionFailed(0)
    creature.ActionFailed(AF_DEFAULT);
    if (creature.IsPC()) {
        creature.CastUser()->CancelStore();
    }
    ++creature.m_SkillDurationMod.m_nFearCount;
    if (source != nullptr) {
        creature.m_vSrcPos = source->GetPos();
    }
    return true;
}

bool CSkillEffect_p_fear::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00aae738.
    L2_GUARD_FUNCTION();
    if ((creature.IsNPC() && creature.m_data->m_nExCrtEffect == 1) ||
        creature.m_SkillDurationMod.m_nPsychicPowerCount >= 1) {
        return false;
    }

    int& count = creature.m_SkillDurationMod.m_nFearCount;
    if (count <= 0) {
        return false;
    }
    --count;
    return true;
}

bool CSkillEffect_p_dominion_transform::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab4854.
    L2_GUARD_FUNCTION();
    const bool isPc = creature.IsPC();
    if (isPc) {
        creature.CastUser()->SetDominionTransformed(true);
    }
    return isPc;
}

bool CSkillEffect_p_dominion_transform::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab4970.
    L2_GUARD_FUNCTION();
    const bool isPc = creature.IsPC();
    if (isPc) {
        creature.CastUser()->SetDominionTransformed(false);
    }
    return isPc;
}

bool CSkillEffect_p_psychic_power::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abc888.
    L2_GUARD_FUNCTION();
    if (source == nullptr || (creature.IsNPC() && creature.m_data->m_nExCrtEffect == 1)) {
        return false;
    }

    if (creature.m_SkillDurationMod.m_nFearCount >= 1) {
        return false;
    }
    ++creature.m_SkillDurationMod.m_nPsychicPowerCount;
    L2SkillFunc::CancelAllActionForMove(&creature);
    creature.m_vSrcPos = source->GetPos();
    return true;
}

bool CSkillEffect_p_psychic_power::PumpEnd(
    CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, const SkillPumpEndType type
) const {
    // Current L2Server 0x00abc990.
    L2_GUARD_FUNCTION();
    if (source == nullptr || (creature.IsNPC() && creature.m_data->m_nExCrtEffect == 1)) {
        return false;
    }
    if (creature.m_SkillDurationMod.m_nFearCount >= 1) {
        return false;
    }

    int& count = creature.m_SkillDurationMod.m_nPsychicPowerCount;
    if (count < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s]PumpEnd more then PumpStart. [%d][%d][%d]",
        //     L"CSkillEffect_p_psychic_power::PumpEnd",
        //     skillInfo == nullptr ? 0 : skillInfo->m_nSkillId,
        //     static_cast<int>(type),
        //     count)
        // L2_LOG_ERROR(Log,
        //     L"[%s]PumpEnd more then PumpStart. [%d][%d][%d]",
        //     L"CSkillEffect_p_psychic_power::PumpEnd",
        //     skillInfo == nullptr ? 0 : skillInfo->m_nSkillId,
        //     static_cast<int>(type),
        //     count)
        L2_LOG_ERROR(
            Log,
            "Ending p_psychic_power for skill %d (end type %d) with nonpositive active count %d; the decrement-and-clamp "
            "operation still proceeds.",
            skillInfo == nullptr ? 0 : skillInfo->m_nSkillId, static_cast<int>(type), count
        );
    }
    --count;
    count = std::max(count, 0);
    return true;
}

bool CSKillEffetct_p_acquire_alchemy_skill::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00abcc30.
    L2_GUARD_FUNCTION();
    if (creature == nullptr || skillInfo == nullptr || !creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }
    if (user->m_data->m_Race == RT_ERTHEIA) {
        AcquiredAlchemySkillInfo acquired(skillInfo, m_bMaster);
        user->AcquireAlchemySkill(acquired, m_nType);
    }
    return true;
}

bool CSKillEffetct_p_acquire_alchemy_skill::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo* skillInfo, SkillPumpEndType
) const {
    // Current L2Server 0x00abcd44.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC() || skillInfo == nullptr) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->DeleteAlchemySkill(skillInfo->m_nSkillId, m_nType);
    return true;
}

bool CSkillEffect_p_berserker::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab6e94.
    L2_GUARD_FUNCTION();
    if (source == nullptr || creature.IsPC() || creature.IsSummonOrPet() || creature.m_data->m_nExCrtEffect != 0) {
        return false;
    }

    creature.m_SkillMod.m_IsBerserkerMode = true;
    g_npcserver.Send("cddd", 0xa1, static_cast<int>(source->m_nId), static_cast<int>(creature.m_nId), 1);
    return true;
}

bool CSkillEffect_p_berserker::PumpEnd(CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab6fa0.
    L2_GUARD_FUNCTION();
    if (source == nullptr || creature.IsPC() || creature.IsSummonOrPet() || creature.m_data->m_nExCrtEffect != 0) {
        return false;
    }

    creature.m_SkillMod.m_IsBerserkerMode = false;
    g_npcserver.Send("cddd", 0xa1, static_cast<int>(source->m_nId), static_cast<int>(creature.m_nId), 0);
    return true;
}

bool CSkillEffect_p_align_direction::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ac7108.
    L2_GUARD_FUNCTION();
    if (!creature.IsCreature()) {
        return false;
    }

    CCreature* const target = creature.CastCreature();
    if (target->IsBoss() || target->IsZzoldagu()) {
        return false;
    }

    FAngle direction = target->GetDir();
    direction.Yaw &= 0xffff;
    target->StopMove();
    FVector position = target->GetPos();
    g_ptlUserMap->BroadcastToNeighborDist(
        target, 0x5000, position, 0x800, "cdddd", 0x7a, static_cast<int>(target->m_nId), direction.Yaw, 1, 0xffff
    );

    const FVector sourcePosition = source->GetPos();
    position = target->GetPos();
    direction.Yaw = runtime::Win64CvttDoubleToInt32(
        std::atan2(position.y - sourcePosition.y, position.x - sourcePosition.x) * (65535.0 / 6.283185307179586)
    );
    target->m_data->SetDir(direction);

    direction = target->GetDir();
    direction.Yaw &= 0xffff;
    position = target->GetPos();
    g_ptlUserMap->BroadcastToNeighborDist(
        target, 0x5000, position, 0x800, "cddd", 0x61, static_cast<int>(target->m_nId), direction.Yaw, 0xffff
    );

    if (target->IsPC()) {
        target->CastUser()->ParamChanged(PCB_USER | PCB_CHAR_SKILL);
    } else {
        target->SendMyInfo();
    }
    return true;
}

bool CSkillEffect_p_remove_grade_penalty::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abc5f0.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }

    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    if (user->GetIgnoreGradePenalty() < m_eCrystalType) {
        user->SetIgnoreGradePenalty(m_eCrystalType);
        user->SendETCBuffStatusInfo();
    }
    return true;
}

bool CSkillEffect_p_remove_equip_penalty::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00aad9a4.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }
    user->SetItemCrystalOriginal(eCrystalType);
    user->SetItemCrystalEquipPenalty(user->CalcCrystalEquipPenalty());
    return true;
}

bool CSkillEffect_p_remove_equip_penalty::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00aada94.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }

    User* const user = creature.CastUser();
    if (user != nullptr) {
        if (user->Level() < 20) {
            user->SetItemCrystalEquipPenalty(CRT_NONE);
        }
        user->SetItemCrystalEquipPenalty(user->CalcCrystalEquipPenalty());
    }
    return true;
}

bool CSkillEffect_p_betray::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab5608.
    L2_GUARD_FUNCTION();
    if (!creature.IsSummonOrPet() || !creature.IsAlive()) {
        return false;
    }

    CSummon* const summon = creature.CastSummon();
    summon->m_betrayOwnMaster = true;
    g_npcserver.Send("cddd", 0x29, static_cast<int>(creature.m_nId), creature.GetIndex(), 1);
    return true;
}

bool CSkillEffect_p_betray::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab5718.
    L2_GUARD_FUNCTION();
    const bool isSummon = creature.IsSummonOrPet();
    if (isSummon) {
        creature.CastSummon()->m_betrayOwnMaster = false;
        creature.DoNothing();
        g_npcserver.Send("cdd", 0x61, static_cast<int>(creature.m_nId), -2);
    }
    return isSummon;
}

bool CSkillEffect_p_remove_grade_penalty::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00abc6e8.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }

    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    user->SetIgnoreGradePenalty(CRT_NONE);
    user->SendETCBuffStatusInfo();
    return true;
}

bool CSkillEffect_p_target_me::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab5c98.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !creature.IsPC()) {
        return false;
    }

    CCreature* const currentTarget = creature.m_spTarget.GetCreature();
    if (currentTarget == nullptr || source->m_nId != currentTarget->m_nId) {
        creature.Action(source, creature.m_AreaMod.m_bIsInBattleField, false);
    }

    ++creature.m_SkillDurationMod.m_TargetMeCount;
    return true;
}

bool CSkillEffect_p_target_me::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab5d90.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }

    int& count = creature.m_SkillDurationMod.m_TargetMeCount;
    if (count <= 0) {
        return false;
    }
    --count;
    return true;
}

bool CSkillEffect_p_hide::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab3d30.
    L2_GUARD_FUNCTION();
    creature.SetVisibleBySkill(false, true);
    creature.m_SkillMod.m_bCanPreserveHide = m_PreserveByDamage;
    return true;
}

bool CSkillEffect_p_hide::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab3dec.
    L2_GUARD_FUNCTION();
    creature.SetVisibleBySkill(true, false);
    creature.m_SkillMod.m_bCanPreserveHide = true;
    return true;
}

bool CSkillEffect_p_hide_no_inherit::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abcae0.
    L2_GUARD_FUNCTION();
    creature.SetVisible(false);
    return true;
}

bool CSkillEffect_p_hide_no_inherit::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00abcb88.
    L2_GUARD_FUNCTION();
    creature.SetVisible(true);
    return true;
}

bool CSkillEffect_p_wrong_casting::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab6144.
    L2_GUARD_FUNCTION();
    if (!creature.IsNPC() || creature.m_data->m_nExCrtEffect != 1) {
        int& count = creature.m_SkillDurationMod.m_nWrongCasting[m_nMagicType];
        ++count;
        int& probability = creature.m_SkillDurationMod.m_nWrongCastingProb[m_nMagicType];
        if (probability < m_nProb) {
            probability = m_nProb;
        }
    }
    return true;
}

bool CSkillEffect_p_wrong_casting::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab6228.
    L2_GUARD_FUNCTION();
    int& count = creature.m_SkillDurationMod.m_nWrongCasting[m_nMagicType];
    if (!creature.IsNPC() || creature.m_data->m_nExCrtEffect != 1) {
        --count;
        count = std::max(count, 0);
        if (count < 1) {
            count = 0;
            creature.m_SkillDurationMod.m_nWrongCastingProb[m_nMagicType] = 0;
        }
    } else if (count > 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] p_wrong_casting error, id[%d]",
        //     L"SkillFxFunc_pump.cpp",
        //     0x181f,
        //     creature.GetClassId())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] p_wrong_casting error, id[%d]",
        //     L"SkillFxFunc_pump.cpp",
        //     0x181f,
        //     creature.GetClassId())
        L2_LOG_ERROR(
            Log,
            "Cannot remove p_wrong_casting from excluded NPC class %d: magic type %d still has active count %d. The count "
            "and probability are retained.",
            creature.GetClassId(), m_nMagicType, count
        );
    }
    return true;
}

bool CSkillEffect_p_block_get_energy::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab76bc.
    L2_GUARD_FUNCTION();
    creature.m_SkillMod.m_CanGetEnergy = false;
    return true;
}

bool CSkillEffect_p_block_get_energy::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab7750.
    L2_GUARD_FUNCTION();
    creature.m_SkillMod.m_CanGetEnergy = true;
    return true;
}

bool CSkillEffect_p_block_elixer_premium::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab7b18.
    L2_GUARD_FUNCTION();
    creature.m_SkillMod.m_CanBeRestarted = false;
    return true;
}

bool CSkillEffect_p_block_elixer_premium::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ab7bac.
    L2_GUARD_FUNCTION();
    creature.m_SkillMod.m_CanBeRestarted = true;
    return true;
}

bool CSkillEffect_p_cheapshot::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab4e78.
    L2_GUARD_FUNCTION();
    creature.m_SkillMod.m_bNoMPConsumeByCheapshot = true;
    return true;
}

bool CSkillEffect_p_cheapshot::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab4f0c.
    L2_GUARD_FUNCTION();
    creature.m_SkillMod.m_bNoMPConsumeByCheapshot = false;
    return true;
}

bool CSkillEffect_p_block_resurrection::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab4104.
    L2_GUARD_FUNCTION();
    creature.m_data->m_bCanBeResurrected = false;
    return true;
}

bool CSkillEffect_p_block_resurrection::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab419c.
    L2_GUARD_FUNCTION();
    creature.m_data->m_bCanBeResurrected = true;
    return true;
}

bool CSkillEffect_p_balthus_buff::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00acc798.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }

    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    CSPointer<User> retained(user, ObjectRefType::FunctionChain);
    user->m_SkillDurationMod.m_bBalthusBuff = true;
    BalthusEvent::inst().SendEventInfo(retained, GAME_EVENT::BALTHUS_ON);
    return true;
}

bool CSkillEffect_p_balthus_buff::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00acc8f4.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }

    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }
    CSPointer<User> retained(user, ObjectRefType::FunctionChain);
    user->m_SkillDurationMod.m_bBalthusBuff = false;
    BalthusEvent::inst().SendEventInfo(retained, GAME_EVENT::BALTHUS_OFF);
    return true;
}

bool CSkillEffect_p_kalie_buff::PumpStart(CCreature&, CCreature* source, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab9ed8.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !source->IsPC()) {
        return false;
    }
    User* const user = source->CastUser();
    if (user == nullptr) {
        return false;
    }
    user->m_nKalieBuff = 1;
    KalieEvent::Inst().SendKalieInfo(*user, 1);
    return true;
}

bool CSkillEffect_p_kalie_buff::PumpEnd(CCreature&, CCreature* source, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab9fc4.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !source->IsPC()) {
        return false;
    }
    User* const user = source->CastUser();
    if (user == nullptr) {
        return false;
    }
    user->m_nKalieBuff = 0;
    KalieEvent::Inst().SendKalieInfo(*user, 0);
    return true;
}

bool CSkillEffect_p_double_summon::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab77e4.
    L2_GUARD_FUNCTION();
    const bool isPc = creature.IsPC();
    if (isPc) {
        creature.CastUser()->m_bDoubleSummon = true;
    }
    return isPc;
}

bool CSkillEffect_p_double_summon::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab78a8.
    L2_GUARD_FUNCTION();
    const bool isPc = creature.IsPC();
    if (isPc) {
        User* const user = creature.CastUser();
        user->m_bDoubleSummon = false;
        creature.CastUser()->DespawnAllSummon(true, false);
    }
    return isPc;
}

bool CSkillEffect_p_physical_attack_pc_level::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab7fd4.
    L2_GUARD_FUNCTION();
    const bool isPc = creature.IsPC();
    if (isPc) {
        creature.CastUser()->m_nWeaponPAttackBonusByLevel = m_Amount;
    }
    return isPc;
}

bool CSkillEffect_p_physical_attack_pc_level::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ab809c.
    L2_GUARD_FUNCTION();
    const bool isPc = creature.IsPC();
    if (isPc) {
        creature.CastUser()->m_nWeaponPAttackBonusByLevel = 0;
    }
    return isPc;
}

bool CSkillEffect_p_magical_attack_pc_level::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab8160.
    L2_GUARD_FUNCTION();
    const bool isPc = creature.IsPC();
    if (isPc) {
        creature.CastUser()->m_nWeaponMAttackBonusByLevel = m_Amount;
    }
    return isPc;
}

bool CSkillEffect_p_magical_attack_pc_level::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ab8228.
    L2_GUARD_FUNCTION();
    const bool isPc = creature.IsPC();
    if (isPc) {
        creature.CastUser()->m_nWeaponMAttackBonusByLevel = 0;
    }
    return isPc;
}

bool CSkillEffect_p_violet_boy::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab4614.
    L2_GUARD_FUNCTION();
    const bool isPc = creature.IsPC();
    if (isPc) {
        creature.m_bIsSkillViolet = true;
        if (creature.m_data->m_cIsGuilty == 0) {
            creature.SetGuilty(1);
            User* const user = creature.CastUser();
            runtime::raw_atomic::CompareExchangeI32(&user->m_nRelationChanged, 1, 0);
            if (!creature.IsDeleting() && creature.Id() == 0) {
                creature.CastUser()->ParamChanged(PCB_USER);
            }
        }
    }
    return isPc;
}

bool CSkillEffect_p_violet_boy::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab4734.
    L2_GUARD_FUNCTION();
    const bool isPc = creature.IsPC();
    if (isPc) {
        creature.m_bIsSkillViolet = false;
        if (creature.m_data->m_cIsGuilty == 1) {
            creature.SetGuilty(0);
            User* const user = creature.CastUser();
            runtime::raw_atomic::CompareExchangeI32(&user->m_nRelationChanged, 1, 0);
            if (!creature.IsDeleting() && creature.Id() == 0) {
                creature.CastUser()->ParamChanged(PCB_USER);
            }
        }
    }
    return isPc;
}

bool CSkillEffect_p_bonustimelimit_up::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab648c.
    L2_GUARD_FUNCTION();
    const bool isPc = creature.IsPC();
    if (isPc) {
        User* const user = creature.CastUser();
        user->CalcVoteSystemTime();
        runtime::raw_atomic::AddI32(&user->m_nVoteBonusTime, 1);
        user->CalExpBonus();
        user->SendVoteSystemInfoPacket();
    }
    return isPc;
}

bool CSkillEffect_p_bonustimelimit_up::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab6574.
    L2_GUARD_FUNCTION();
    const bool isPc = creature.IsPC();
    if (isPc) {
        User* const user = creature.CastUser();
        user->CalcVoteSystemTime();
        runtime::raw_atomic::AddI32(&user->m_nVoteBonusTime, -1);
        user->CalExpBonus();
        user->SendVoteSystemInfoPacket();
    }
    return isPc;
}

bool CSkillEffect_p_knockdown::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab74f8.
    L2_GUARD_FUNCTION();
    const FVector creaturePosition = creature.GetPos();
    const FVector sourcePosition = source->GetPos();
    FAngle direction{};
    direction.Yaw = runtime::Win64CvttDoubleToInt32(
        std::atan2(sourcePosition.y - creaturePosition.y, sourcePosition.x - creaturePosition.x) *
        (65535.0 / 6.283185307179586)
    );
    creature.m_data->SetDir(direction);
    return true;
}

bool CSkillEffect_p_knockdown::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab7604.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }
    L2SkillFunc::BlockAction(creature, true);
    return true;
}

bool CSkillEffect_p_speed_out_of_fight::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab9580.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature.CastCreature())) {
        return false;
    }
    ++creature.m_SkillDurationMod.m_CombatSkillCount;
    return true;
}

bool CSkillEffect_p_speed_out_of_fight::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab9644.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature)) {
        return false;
    }
    if (creature->m_data->m_bIsInCombatMode) {
        creature->m_SkillMod.m_SpeedOutOfCombat = false;
        return false;
    }
    creature->m_SkillMod.m_SpeedOutOfCombat = true;
    L2SkillFunc::AddBonus(creature, CCreatureSkillMod::Speed, SFCT_DIFF, m_Bonus);
    return true;
}

bool CSkillEffect_p_speed_out_of_fight::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab9738.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAliveCreature(creature.CastCreature())) {
        return false;
    }
    int& count = creature.m_SkillDurationMod.m_CombatSkillCount;
    count = std::max(count - 1, 0);
    return true;
}

bool CSkillEffect_p_ability_change_itemskill::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abddb8.
    L2_GUARD_FUNCTION();
    const bool isPc = creature.IsPC();
    if (isPc) {
        User* const user = creature.CastUser();
        user->m_SkillMod.m_bItemSkillTransfer = true;
        user->ValidateChanged(VB_ITEMSKILL);
    }
    return isPc;
}

bool CSkillEffect_p_ability_change_itemskill::PumpEnd(
    CCreature&, CCreature* source, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00abde90.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !source->IsPC()) {
        return false;
    }
    User* const user = source->CastUser();
    user->m_SkillMod.m_bItemSkillTransfer = false;
    user->ValidateChanged(VB_ITEMSKILL);
    return true;
}

bool CSkillEffect_p_block_party::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00ab43bc.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }

    User* const user = creature.CastUser();
    if (user != nullptr) {
        user->m_data->m_bCanJoinParty = false;
        if (user->m_data->m_nPartyId != 0) {
            CParty* const party = user->GetParty();
            if (party != nullptr) {
                if (party->m_data->m_OnlineMaster.id == user->m_nId) {
                    party->Dismiss(false, false);
                } else if (!party->m_data->IsPVPMatching()) {
                    party->Withdraw(user, false);
                    user->GetSocket()->Send("cd", 0x3b, 0);
                } else {
                    user->GetSocket()->Send("cd", 0x3b, 1);
                }
            }
        }
    }
    return true;
}

bool CSkillEffect_p_block_party::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab4548.
    L2_GUARD_FUNCTION();
    const bool isPc = creature.IsPC();
    if (isPc) {
        creature.CastUser()->m_data->m_bCanJoinParty = true;
    }
    return isPc;
}

bool CSkillEffect_p_Worldchatpoint_Add::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00abb0f8.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr) {
        return false;
    }

    user->m_SkillDurationMod.m_nAdditionalWorldChatCnt += m_nAddCount;
    CUserSocket* const socket = user->GetSocket();
    if (socket != nullptr) {
        socket->Send("chd", 0xfe, 0x175, user->GetWorldChatCount());
    }
    return true;
}

bool CSkillEffect_p_Worldchatpoint_Add::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00abb20c.
    L2_GUARD_FUNCTION();
    if (!creature.IsPC()) {
        return false;
    }
    User* const user = creature.CastUser();
    if (user == nullptr || user->m_SkillDurationMod.m_nAdditionalWorldChatCnt <= 0) {
        return false;
    }

    int& count = user->m_SkillDurationMod.m_nAdditionalWorldChatCnt;
    count = std::max(count - m_nAddCount, 0);
    return true;
}

bool CSkillEffect_p_set_collected::CheckPumpCondition(CCreature* creature) {
    // Current L2Server 0x00aaff70.
    L2_GUARD_FUNCTION();
    return creature->IsPC();
}

bool CSkillEffect_p_set_collected::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00abebb8.
    L2_GUARD_FUNCTION();
    if (creature->m_nSetItemClassId == 0) {
        return false;
    }

    CSetItem* const setItem = g_ObjectDB.GetSetItemBySetItemId(creature->m_nSetItemClassId);
    if (setItem != nullptr) {
        CCreatureSkillMod::Mod* const mods = creature->m_SkillMod.m_Mods;
        mods[CCreatureSkillMod::PDefend].m_Per =
            ((setItem->m_p_def_inc + 100.0) / 100.0) * mods[CCreatureSkillMod::PDefend].m_Per;
        mods[CCreatureSkillMod::PAttack].m_Per =
            ((setItem->m_p_attack_inc + 100.0) / 100.0) * mods[CCreatureSkillMod::PAttack].m_Per;
        mods[CCreatureSkillMod::MAttack].m_Per =
            ((setItem->m_m_attack_inc + 100.0) / 100.0) * mods[CCreatureSkillMod::MAttack].m_Per;
        mods[CCreatureSkillMod::MDefend].m_Per =
            ((setItem->m_m_def_inc + 100.0) / 100.0) * mods[CCreatureSkillMod::MDefend].m_Per;
        mods[CCreatureSkillMod::HPRegen].m_Per =
            ((setItem->m_hp_regen_inc + 100.0) / 100.0) * mods[CCreatureSkillMod::HPRegen].m_Per;
        mods[CCreatureSkillMod::MPRegen].m_Per =
            ((setItem->m_mp_regen_inc + 100.0) / 100.0) * mods[CCreatureSkillMod::MPRegen].m_Per;
        mods[CCreatureSkillMod::Speed].m_Diff += setItem->m_move_speed_inc;
        mods[CCreatureSkillMod::MagicSpeed].m_Per =
            ((setItem->m_casting_speed_inc + 100.0) / 100.0) * mods[CCreatureSkillMod::MagicSpeed].m_Per;
        mods[CCreatureSkillMod::PAvoid].m_Diff += setItem->m_avoid_inc;
        mods[CCreatureSkillMod::PAvoid].m_Per =
            ((setItem->m_avoid_inc + 100.0) / 100.0) * mods[CCreatureSkillMod::PAvoid].m_Per;
        mods[CCreatureSkillMod::MaxHP].m_Diff += setItem->m_hp_inc;
        mods[CCreatureSkillMod::MaxMP].m_Diff += setItem->m_mp_inc;
        mods[CCreatureSkillMod::Breath].m_Diff += setItem->m_breath_inc;
        mods[CCreatureSkillMod::WeightLimit].m_Diff += setItem->m_weight_limit_inc;
        creature->m_SkillMod.m_dShieldDefenceModPer =
            ((setItem->m_shield_def_prob_inc + 100.0) / 100.0) * creature->m_SkillMod.m_dShieldDefenceModPer;
        creature->m_dzDefendingTraitValue[TRAIT_POISON] =
            ((setItem->m_resist_poison_inc + 100.0) / 100.0) * creature->m_dzDefendingTraitValue[TRAIT_POISON];
        creature->m_dzDefendingTraitValue[TRAIT_AT_DAGGER] =
            ((setItem->m_p_def_vs_dagger_inc + 100.0) / 100.0) * creature->m_dzDefendingTraitValue[TRAIT_AT_DAGGER];

        const std::uint32_t additionalSlot = setItem->m_nAdditionalItemSlot;
        if (additionalSlot != 0) {
            const int itemIndex = creature->m_data->m_nSlotIndex[additionalSlot];
            if (itemIndex != -1) {
                CItem* const item = creature->m_inv.GetByIndex(itemIndex);
                if (item != nullptr &&
                    setItem->SlotToSetItemClassID(static_cast<int>(additionalSlot), item->m_data->m_nClassID) ==
                        item->m_data->m_nClassID) {
                    mods[CCreatureSkillMod::PDefend].m_Per =
                        ((setItem->m_p_def_inc_add + 100.0) / 100.0) * mods[CCreatureSkillMod::PDefend].m_Per;
                    mods[CCreatureSkillMod::MDefend].m_Per =
                        ((setItem->m_m_def_inc_add + 100.0) / 100.0) * mods[CCreatureSkillMod::MDefend].m_Per;
                    mods[CCreatureSkillMod::MaxHP].m_Diff += setItem->m_hp_inc_add;
                    mods[CCreatureSkillMod::MaxMP].m_Diff += setItem->m_mp_inc_add;
                    mods[CCreatureSkillMod::Speed].m_Diff += setItem->m_move_speed_inc_add;
                    mods[CCreatureSkillMod::Breath].m_Diff += setItem->m_breath_inc_add;
                    mods[CCreatureSkillMod::MagicSpeed].m_Per =
                        ((setItem->m_casting_speed_inc_add + 100.0) / 100.0) * mods[CCreatureSkillMod::MagicSpeed].m_Per;
                    mods[CCreatureSkillMod::PAttack].m_Per =
                        ((setItem->m_p_attack_inc_add + 100.0) / 100.0) * mods[CCreatureSkillMod::PAttack].m_Per;
                    mods[CCreatureSkillMod::MAttack].m_Per =
                        ((setItem->m_m_attack_inc_add + 100.0) / 100.0) * mods[CCreatureSkillMod::MAttack].m_Per;
                    mods[CCreatureSkillMod::HPRegen].m_Per =
                        ((setItem->m_hp_regen_inc_add + 100.0) / 100.0) * mods[CCreatureSkillMod::HPRegen].m_Per;
                    mods[CCreatureSkillMod::MPRegen].m_Per =
                        ((setItem->m_mp_regen_inc_add + 100.0) / 100.0) * mods[CCreatureSkillMod::MPRegen].m_Per;
                    mods[CCreatureSkillMod::PAvoid].m_Diff += setItem->m_avoid_inc_add;
                    mods[CCreatureSkillMod::PAvoid].m_Per =
                        ((setItem->m_avoid_inc_add + 100.0) / 100.0) * mods[CCreatureSkillMod::PAvoid].m_Per;
                    mods[CCreatureSkillMod::WeightLimit].m_Diff += setItem->m_weight_limit_inc_add;
                    creature->m_SkillMod.m_dShieldDefenceModPer =
                        ((setItem->m_shield_def_prob_inc_add + 100.0) / 100.0) * creature->m_SkillMod.m_dShieldDefenceModPer;
                    creature->m_dzDefendingTraitValue[TRAIT_POISON] = ((setItem->m_resist_poison_inc_add + 100.0) / 100.0) *
                        creature->m_dzDefendingTraitValue[TRAIT_POISON];
                    creature->m_dzDefendingTraitValue[TRAIT_AT_DAGGER] =
                        ((setItem->m_p_def_vs_dagger_inc_add + 100.0) / 100.0) *
                        creature->m_dzDefendingTraitValue[TRAIT_AT_DAGGER];
                }
            }
        }
    }
    return true;
}

bool CSkillEffect_p_energy_heal::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00ab7178.
    L2_GUARD_FUNCTION();
    if (!L2Common::IsAlivePC(creature)) {
        return false;
    }

    User* const user = creature->CastUser();
    if (!user->m_SkillMod.CanGetDamageHP(nullptr, creature)) {
        return false;
    }

    const double recoveryPercent = std::max(static_cast<double>(user->m_nEnergyCount * 2) * 0.8, 10.0);
    if (m_EnableRecovery) {
        const double previousMaximum = user->m_data->m_dBaseHP;
        L2SkillFunc::AddBonus(creature, CCreatureSkillMod::MaxHP, SFCT_PER, recoveryPercent);
        user->ValidateBaseHP();
        const double applied = user->IncreaseHP_diff(user->m_data->m_dBaseHP - previousMaximum, false, 4, 0, false);
        L2SkillFunc::SendHpMpChangedSystemMessage(
            nullptr, *creature, runtime::Win64CvttDoubleToInt32(applied), SystemMessageId::HP_RESTORED
        );
    }
    return true;
}

bool CSkillEffect_p_alter_skill_by_event::PumpStart(
    CCreature& creature, CCreature* source, const CSkillInfo* skillInfo, SkillPumpStartType
) const {
    // Current L2Server 0x00abf740.
    L2_GUARD_FUNCTION();
    if (source == nullptr || !source->IsPC() || !creature.IsPC() || skillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    TriggerAlterSkillInfo*& trigger = creature.m_SkillMod.m_TriggerAlterSkill[m_Event];
    if (trigger != nullptr) {
        return false;
    }
    trigger = const_cast<TriggerAlterSkillInfo*>(&skillInfo->m_TriggerAlterSkillInfo);
    return true;
}

bool CSkillEffect_p_alter_skill_by_event::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00abf894.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillMod.m_Lock);
    creature.m_SkillMod.m_TriggerAlterSkill[m_Event] = nullptr;
    return true;
}

bool CSkillEffect_p_expand_artifact_slot_group::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00abe5ec.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }
    if (m_nLevel < 1 || m_nLevel > 3) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][CSkillEffect_p_expand_artifact_slot_group] Undefined Artifactbook Level[%d] (SkillName:%s)",
        //     L"SkillFxFunc_pump.cpp",
        //     0x2bce,
        //     m_nLevel,
        //     skillInfo->m_pszName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][CSkillEffect_p_expand_artifact_slot_group] Undefined Artifactbook Level[%d] (SkillName:%s)",
        //     L"SkillFxFunc_pump.cpp",
        //     0x2bce,
        //     m_nLevel,
        //     skillInfo->m_pszName)
        L2_LOG_ERROR(
            Log,
            "Cannot apply p_expand_artifact_slot_group for skill '%s': level %d is outside 1..3; artifact slots are left "
            "unchanged.",
            skillInfo == nullptr || skillInfo->m_pszName == nullptr
                ? "<null>"
                : Utf16ToUtf8(target_utf16::FromHostCodeUnits(skillInfo->m_pszName)).c_str(),
            m_nLevel
        );
        return false;
    }

    creature->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        creature->m_SkillMod.m_nActiveArtifactSlotGroup, m_nLevel, 0x66
    );

    const int slotGroups[3][7] = {
        {0x2a, 0x2b, 0x2c, 0x2d, 0x36, 0x39, 0x3c},
        {0x2e, 0x2f, 0x30, 0x31, 0x37, 0x3a, 0x3d},
        {0x32, 0x33, 0x34, 0x35, 0x38, 0x3b, 0x3e},
    };
    for (int group = m_nLevel; group < 3; ++group) {
        for (const int slot : slotGroups[group]) {
            if (creature->m_data->m_nSlotIndex[slot] != -1) {
                creature->UnequipItem(static_cast<SlotType>(slot), false);
            }
        }
    }
    return true;
}

bool CSkillEffect_p_preserve_abnormal::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab332c.
    L2_GUARD_FUNCTION();
    creature.m_SkillMod.m_nPreserveAbnormal = m_Value;
    return true;
}

bool CSkillEffect_p_preserve_abnormal::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab33d0.
    L2_GUARD_FUNCTION();
    creature.m_SkillMod.m_nPreserveAbnormal = -1;
    return true;
}

bool CSkillEffect_p_abnormal_shield::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00ab994c.
    L2_GUARD_FUNCTION();
    creature.m_SkillMod.m_AbnormalShield = m_Value;
    return true;
}

bool CSkillEffect_p_abnormal_shield::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00ab99f0.
    L2_GUARD_FUNCTION();
    creature.m_SkillMod.m_AbnormalShield = 0;
    return true;
}

bool CSkillEffect_op_target_pc::CanUse(CCreature*, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a7a09c.
    L2_GUARD_FUNCTION();
    return target != nullptr && target->IsPC();
}

bool CSkillEffect_op_blink::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject* target) {
    // Current L2Server 0x00a79b14.
    L2_GUARD_FUNCTION();
    if (target == nullptr) {
        return false;
    }

    FVector destination{};
    if (!target->CastCreature()->GetBlinkDestPos(m_Dist, m_Direction, destination)) {
        if (creature->IsPC()) {
            creature->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_FAILED_NO_DESTINATION_SPACE);
        }
        return false;
    }
    if (creature->IsPC() && creature->CastUser()->m_PledgeGameUser.IsWaiting()) {
        creature->SendSystemMessage(SystemMessageId::CANNOT_MOVE_IN_CURRENT_STATE);
        return false;
    }
    return true;
}

bool CSkillEffect_op_charge_self::GetDestPos(
    CCreature* creature, const double distance, const SkillDirectionType direction, FVector& destination
) {
    // Current L2Server 0x00a7dc10.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    const FVector source = creature->GetPos();
    const FVector facing = creature->GetNormalizedDirVector(0);
    if (direction == DT_FRONT) {
        destination =
            FVector{source.x + distance * facing.x, source.y + distance * facing.y, source.z + distance * facing.z};
    } else if (direction == DT_BACK) {
        destination =
            FVector{source.x - distance * facing.x, source.y - distance * facing.y, source.z - distance * facing.z};
    } else {
        return false;
    }

    const bool floating = creature->m_data->IsFlying() || creature->m_AreaMod.m_bIsUnderWater;
    CWorld& world = CWorld::Inst();
    if (!world.FindAcceptableDestZPos(source, destination, floating)) {
        return false;
    }

    FVector collisionPosition{};
    if (g_WorldPlaneCollision.CheckCollision(
            source, destination, 0.0, collisionPosition, static_cast<int>(creature->GetInZoneID()), false
        )) {
        destination = collisionPosition;
    }

    bool distanceExhausted = false;
    double travelledDistance = 0.0;
    return world.CanGo(
        source, destination, collisionPosition, distanceExhausted, travelledDistance,
        static_cast<int>(creature->GetInZoneID()), floating
    );
}

bool CSkillEffect_op_charge_self::CanUse(CCreature* creature, const CSkillInfo*, CWorldObject*) {
    // Current L2Server 0x00a7fdd8.
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return false;
    }

    FVector destination{};
    if (!GetDestPos(creature, m_Dist, m_Direction, destination)) {
        if (creature->IsPC()) {
            creature->SendSystemMessage(SystemMessageId::TELEPORT_EFFECT_FAILED_NO_DESTINATION_SPACE);
        }
        return false;
    }
    if (creature->IsPC() && creature->CastUser()->m_PledgeGameUser.IsWaiting()) {
        creature->SendSystemMessage(SystemMessageId::CANNOT_MOVE_IN_CURRENT_STATE);
        return false;
    }
    return true;
}

bool CSkillEffect_op_return_pos::CanUse(CCreature* creature, const CSkillInfo* skillInfo, CWorldObject*) {
    // Current L2Server 0x00a81400.
    L2_GUARD_FUNCTION();
    if (!creature->IsPC()) {
        return false;
    }

    User* const user = creature->CastUser();
    if (user == nullptr) {
        return false;
    }
    FVector returnPosition = user->FindReturnValue(skillInfo->m_nSkillId);
    if (FVector{}.IsEqual(returnPosition)) {
        return false;
    }

    const FVector currentPosition = user->GetPos();
    const double dx = returnPosition.x - currentPosition.x;
    const double dy = returnPosition.y - currentPosition.y;
    const double dz = returnPosition.z - currentPosition.z;
    if (static_cast<double>(m_Dist) < std::sqrt(dx * dx + dy * dy + dz * dz)) {
        return false;
    }

    (void)user->GetNormalizedDirVector(0);
    const bool floating = user->m_data->IsFlying() || user->m_AreaMod.m_bIsUnderWater;
    CWorld& world = CWorld::Inst();
    if (!world.FindAcceptableDestZPos(currentPosition, returnPosition, floating)) {
        return false;
    }

    FVector collisionPosition{};
    bool distanceExhausted = true;
    double travelledDistance = 0.0;
    if (!world.CanGo(
            currentPosition, returnPosition, collisionPosition, distanceExhausted, travelledDistance,
            static_cast<int>(user->GetInZoneID()), false
        )) {
        returnPosition = collisionPosition;
        if (travelledDistance < 16.0) {
            return false;
        }
    }
    return true;
}

CSkillEffect_p_resurrection_special::CSkillEffect_p_resurrection_special(
    int hpProbability, int mpProbability, int cpProbability, const char16_t* resurrectionSkillName
) :
    nHPProb(hpProbability), nMPProb(mpProbability), nCPProb(cpProbability), m_pSkillInfo(nullptr) {
    // Current L2Server 0x00ac6964.
    L2_GUARD_FUNCTION();
    if (resurrectionSkillName == nullptr) {
        std::fill_n(sResurrectionSkillName, 256, u'\0');
    } else {
        CopyTriggerSkillName(sResurrectionSkillName, resurrectionSkillName);
    }
}

CSkillEffect_p_resurrection_special::CSkillEffect_p_resurrection_special(
    int hpProbability,
    int mpProbability,
    int cpProbability,
    const char16_t* resurrectionSkillName,
    std::vector<std::int64_t>* inZoneIds
) :
    nHPProb(hpProbability), nMPProb(mpProbability), nCPProb(cpProbability), m_pSkillInfo(nullptr) {
    // Current L2Server 0x00acb1b4.
    L2_GUARD_FUNCTION();
    for (const std::int64_t inZoneId : *inZoneIds) {
        nInzoneId.push_back(static_cast<int>(inZoneId));
    }
    if (resurrectionSkillName == nullptr) {
        std::fill_n(sResurrectionSkillName, 256, u'\0');
    } else {
        CopyTriggerSkillName(sResurrectionSkillName, resurrectionSkillName);
    }
}

void CSkillEffect_p_resurrection_special::OnPostLoad() {
    // Current L2Server 0x00ab5c70.
    m_pSkillInfo =
        const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(target_utf16::ToHostCodeUnits(sResurrectionSkillName).c_str()));
}

bool CSkillEffect_p_resurrection_special::PumpStart(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType
) const {
    // Current L2Server 0x00acac60.
    L2_GUARD_FUNCTION();
    ++creature.m_SkillDurationMod.m_nResurrectionSpecial;
    creature.m_SkillDurationMod.m_dRecoverHPPer = static_cast<double>(nHPProb);
    creature.m_SkillDurationMod.m_dRecoverMPPer = static_cast<double>(nMPProb);
    creature.m_SkillDurationMod.m_dRecoverCPPer = static_cast<double>(nCPProb);
    if (!nInzoneId.empty()) {
        creature.m_SkillDurationMod.m_vResurrectionInstantZoneIds = nInzoneId;
    }
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    creature.m_SkillDurationMod.p_RecoverSkillInfo = m_pSkillInfo;
    return true;
}

bool CSkillEffect_p_resurrection_special::PumpEnd(
    CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType
) const {
    // Current L2Server 0x00ac4064.
    L2_GUARD_FUNCTION();
    int& count = creature.m_SkillDurationMod.m_nResurrectionSpecial;
    --count;
    count = std::max(count, 0);
    creature.m_SkillDurationMod.m_vResurrectionInstantZoneIds.clear();
    L2_GUARD(creature.m_SkillDurationMod.m_Lock);
    creature.m_SkillDurationMod.p_RecoverSkillInfo = nullptr;
    return true;
}
