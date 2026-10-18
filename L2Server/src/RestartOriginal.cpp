#include "RestartOriginal.h"

#include "Skill.h"
#include "User.h"


CRestartOriginalManager g_RestartOriginalManager;

CRestartOriginalManager::CRestartOriginalManager() {
    L2_GUARD_FUNCTION();

    m_restartItems.clear();
    m_restartNpcItems.clear();
    for (CSkillInfo* const skillInfo : m_restartSkills) {
        delete skillInfo;
    }
    m_restartSkills.clear();
}

CRestartOriginalManager::~CRestartOriginalManager() {
    for (CSkillInfo* const skillInfo : m_restartSkills) {
        delete skillInfo;
    }
    m_restartSkills.clear();
}

void CRestartOriginalManager::AddRestartItem(int itemClassId) {
    L2_GUARD_FUNCTION();
    m_restartItems.push_back(itemClassId);
}

void CRestartOriginalManager::AddRestartNpcItem(int itemClassId) {
    L2_GUARD_FUNCTION();
    m_restartNpcItems.push_back(itemClassId);
}

void CRestartOriginalManager::AddRestartSkill(CSkillInfo* skillInfo) {
    L2_GUARD_FUNCTION();
    m_restartSkills.push_back(skillInfo);
}

int CRestartOriginalManager::HaveRestartItem(User* user) {
    L2_GUARD_FUNCTION();
    for (const int itemClassId : m_restartItems) {
        if (user->m_inv.HaveItemByClassId(itemClassId, 1, false, false)) {
            return itemClassId;
        }
    }
    return 0;
}

bool CRestartOriginalManager::HaveRestartNpcItem(User* user, std::vector<int>& itemClassIds) {
    L2_GUARD_FUNCTION();
    for (const int itemClassId : m_restartNpcItems) {
        if (user->m_inv.HaveItemByClassId(itemClassId, 1, false, false)) {
            itemClassIds.push_back(itemClassId);
        }
    }
    return !itemClassIds.empty();
}

CSkillInfo* CRestartOriginalManager::HaveRestartSkill(User* user) {
    L2_GUARD_FUNCTION();
    for (CSkillInfo* const skillInfo : m_restartSkills) {
        if (user->HaveSkill(skillInfo->m_nSkillId, skillInfo->m_nLevel, skillInfo->m_nSubLevel)) {
            return skillInfo;
        }
    }
    return nullptr;
}
