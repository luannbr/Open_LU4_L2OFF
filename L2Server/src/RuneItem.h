#pragma once

#include "Shared/src/StandardContainers.h"

class CItem;
class CSkillInfo;

// Current 0x00a3b334..0x00a3ba50 is a static-only owner. The three
// xvectors and the ordered pointer set are process-lifetime indices borrowed
// from item skill lists. No CRuneItem instance is constructed.
class CRuneItem {
public:
    static void InitRuneItemSkills();
    static void AddRuneSelectSkill(CItem* item);
    static void PostLoad();

    static std::vector<const CSkillInfo*> m_RuneSelectSkillListXP;
    static std::vector<const CSkillInfo*> m_RuneSelectSkillListSP;
    static std::vector<const CSkillInfo*> m_RuneSelectSkillListRP;
    static std::set<const CSkillInfo*> g_RuneItemSkillSet;
};
