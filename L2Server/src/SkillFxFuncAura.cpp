#include "Pet.h"
#include "Protocol/src/NPCProtocol.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/UserConst.h"
#include "SkillFxFunc.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace {

void CopySkillName(char16_t (&destination)[256], const char16_t* source) noexcept {
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

CSkillEffect_p_aura::CSkillEffect_p_aura(char16_t* skillName) : m_SkillName{}, m_pSkillInfo(nullptr) {
    // Current L2Server 0x00a84890.
    CopySkillName(m_SkillName, skillName);
}

void CSkillEffect_p_aura::OnPostLoad() {
    m_pSkillInfo = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(target_utf16::ToHostCodeUnits(m_SkillName).c_str()));
}

bool CSkillEffect_p_aura::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00a848d8.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    creature.m_SkillMod.m_SkillAuraMap.emplace(
        m_pSkillInfo, CCreatureSkillMod::SkillAuraInfo(m_pSkillInfo->m_TargetType, m_pSkillInfo->m_nSkillId)
    );
    return true;
}

bool CSkillEffect_p_aura::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00a83ed8.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    const auto found = creature->m_SkillMod.m_SkillAuraMap.find(m_pSkillInfo);
    if (found == creature->m_SkillMod.m_SkillAuraMap.end()) {
        return false;
    }
    found->second.m_bOn = true;
    return true;
}

bool CSkillEffect_p_aura::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00a8403c.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    const auto found = creature.m_SkillMod.m_SkillAuraMap.find(m_pSkillInfo);
    if (found == creature.m_SkillMod.m_SkillAuraMap.end()) {
        return false;
    }
    creature.m_SkillMod.m_SkillAuraMap.erase(found);
    return true;
}

CSkillEffect_a_speed::CSkillEffect_a_speed(
    double bonus, SkillFxChangeType changeType, char16_t* skillName, SkillTargetTypeEnum targetType
) :
    CSkillEffectAddBonusByArmorType(bonus, changeType, CCreatureSkillMod::Speed),
    m_SkillName{},
    m_eTargetType(targetType),
    m_pSkillInfo(nullptr) {
    // Current L2Server 0x00a84c98.
    CopySkillName(m_SkillName, skillName);
}

void CSkillEffect_a_speed::OnPostLoad() {
    m_pSkillInfo = const_cast<CSkillInfo*>(g_SkillDB.GetSkillInfoByName(target_utf16::ToHostCodeUnits(m_SkillName).c_str()));
}

bool CSkillEffect_a_speed::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00a84a18.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    creature.m_SkillMod.m_SkillAuraMap.emplace(
        m_pSkillInfo, CCreatureSkillMod::SkillAuraInfo(m_eTargetType, m_pSkillInfo->m_nSkillId)
    );
    return true;
}

bool CSkillEffect_a_speed::Pump(CCreature* creature, const CSkillInfo* skillInfo, int) {
    // Current L2Server 0x00a841c0.
    L2_GUARD_FUNCTION();
    CSkillEffectAddBonusByArmorType::Pump(creature, skillInfo, 0);
    creature->ValidateSpeed();
    if (creature->IsNPC()) {
        creature->CastNPC()->ParamChanged(NPCT_AURA_SPEED);
    } else if (creature->IsPC()) {
        creature->ParamChanged(PCB_CHAR_SKILL);
    } else {
        creature->ParamChanged();
    }

    L2_GUARD(creature->m_SkillMod.m_Lock);
    const auto found = creature->m_SkillMod.m_SkillAuraMap.find(m_pSkillInfo);
    if (found == creature->m_SkillMod.m_SkillAuraMap.end()) {
        return false;
    }
    found->second.m_bOn = true;
    return true;
}

bool CSkillEffect_a_speed::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00a8439c.
    L2_GUARD_FUNCTION();
    if (m_pSkillInfo == nullptr) {
        return false;
    }

    L2_GUARD(creature.m_SkillMod.m_Lock);
    const auto found = creature.m_SkillMod.m_SkillAuraMap.find(m_pSkillInfo);
    if (found == creature.m_SkillMod.m_SkillAuraMap.end()) {
        return false;
    }
    creature.m_SkillMod.m_SkillAuraMap.erase(found);
    return true;
}

CSkillEffect_a_synergy_skill::CSkillEffect_a_synergy_skill(int synergyId, SkillTargetTypeEnum targetType) :
    m_nSynergeId(synergyId), m_eTargetType(targetType) {
    // Current L2Server 0x00a84b58.
}

void CSkillEffect_a_synergy_skill::OnPostLoad() {
    // Current L2Server 0x00a84520 performs this otherwise empty traversal.
    TriggerSynergySkillInfo* const synergy = m_mapSynergySkill.find(m_nSynergeId)->second;
    for (const auto& skill : synergy->m_mapSynergySkillByCount) {
        static_cast<void>(skill);
    }
}

bool CSkillEffect_a_synergy_skill::PumpStart(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpStartType) const {
    // Current L2Server 0x00a84b80.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillMod.m_Lock);
    creature.m_SkillMod.m_SkillSynergyAuraMap.emplace(
        m_nSynergeId, CCreatureSkillMod::SkillSynergyAuraInfo(m_eTargetType, m_nSynergeId)
    );
    return true;
}

bool CSkillEffect_a_synergy_skill::Pump(CCreature* creature, const CSkillInfo*, int) {
    // Current L2Server 0x00a845c8.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature->m_SkillMod.m_Lock);
    const auto found = creature->m_SkillMod.m_SkillSynergyAuraMap.find(m_nSynergeId);
    if (found == creature->m_SkillMod.m_SkillSynergyAuraMap.end()) {
        return false;
    }
    found->second.m_bOn = true;
    return true;
}

bool CSkillEffect_a_synergy_skill::PumpEnd(CCreature& creature, CCreature*, const CSkillInfo*, SkillPumpEndType) const {
    // Current L2Server 0x00a84724.
    L2_GUARD_FUNCTION();
    L2_GUARD(creature.m_SkillMod.m_Lock);
    const auto found = creature.m_SkillMod.m_SkillSynergyAuraMap.find(m_nSynergeId);
    if (found == creature.m_SkillMod.m_SkillSynergyAuraMap.end()) {
        return false;
    }
    creature.m_SkillMod.m_SkillSynergyAuraMap.erase(found);
    return true;
}
