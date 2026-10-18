#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstddef>

class CSkillInfo;
class User;

class CRestartOriginalManager {
public:
    CRestartOriginalManager();
    ~CRestartOriginalManager();

    void AddRestartItem(int itemClassId);
    void AddRestartNpcItem(int itemClassId);
    void AddRestartSkill(CSkillInfo* skillInfo);

    int HaveRestartItem(User* user);
    bool HaveRestartNpcItem(User* user, std::vector<int>& itemClassIds);
    CSkillInfo* HaveRestartSkill(User* user);

private:
    std::vector<int> m_restartItems; // +0x00
    std::vector<CSkillInfo*> m_restartSkills; // +0x20
    std::vector<int> m_restartNpcItems; // +0x40
};


extern CRestartOriginalManager g_RestartOriginalManager;
