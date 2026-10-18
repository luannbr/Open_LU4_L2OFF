#include "RuneItem.h"

#include "Inventory.h"
#include "Item.h"
#include "Skill.h"

#include "Shared/src/SharedFactory.h"

std::vector<const CSkillInfo*> CRuneItem::m_RuneSelectSkillListXP;
std::vector<const CSkillInfo*> CRuneItem::m_RuneSelectSkillListSP;
std::vector<const CSkillInfo*> CRuneItem::m_RuneSelectSkillListRP;
std::set<const CSkillInfo*> CRuneItem::g_RuneItemSkillSet;

void CRuneItem::InitRuneItemSkills() {
    L2_GUARD_FUNCTION();

    m_RuneSelectSkillListXP.clear();
    m_RuneSelectSkillListSP.clear();
    m_RuneSelectSkillListRP.clear();
    g_RuneItemSkillSet.clear();
}

void CRuneItem::AddRuneSelectSkill(CItem* item) {
    L2_GUARD_FUNCTION();

    constexpr std::int32_t kEtcItemTypeRune = 0x22;
    constexpr std::int32_t kEtcItemTypeRuneSelect = 0x23;

    if (item == nullptr || item->m_data->m_Type != IT_ETCITEM) {
        return;
    }

    const std::int32_t etcItemType = item->m_const->m_EtcitemType;
    if (etcItemType != kEtcItemTypeRune && etcItemType != kEtcItemTypeRuneSelect) {
        return;
    }

    if (etcItemType == kEtcItemTypeRune) {
        for (const CSkillInfo* const skill : item->m_ItemSkillList) {
            g_RuneItemSkillSet.insert(skill);
        }
    }

    if (etcItemType != kEtcItemTypeRuneSelect) {
        return;
    }

    std::vector<const CSkillInfo*>* destination = nullptr;
    switch (static_cast<MaterialType>(item->m_const->m_Material)) {
        case MATT_RUNE_XP:
            destination = &m_RuneSelectSkillListXP;
            break;
        case MATT_RUNE_SP:
            destination = &m_RuneSelectSkillListSP;
            break;
        case MATT_RUNE_REMOVE_PENALTY:
            destination = &m_RuneSelectSkillListRP;
            break;
        default:
            return;
    }

    if (!destination->empty()) {
        return;
    }
    destination->assign(item->m_ItemSkillList.begin(), item->m_ItemSkillList.end());
}

void CRuneItem::PostLoad() {
    L2_GUARD_FUNCTION();

    const auto disablePersistence = [](const CSkillInfo* skill) { const_cast<CSkillInfo*>(skill)->m_bSaveToDB = false; };
    for (const CSkillInfo* const skill : m_RuneSelectSkillListXP) {
        disablePersistence(skill);
    }
    for (const CSkillInfo* const skill : m_RuneSelectSkillListSP) {
        disablePersistence(skill);
    }
    for (const CSkillInfo* const skill : m_RuneSelectSkillListRP) {
        disablePersistence(skill);
    }
    for (const CSkillInfo* const skill : g_RuneItemSkillSet) {
        disablePersistence(skill);
    }
}
