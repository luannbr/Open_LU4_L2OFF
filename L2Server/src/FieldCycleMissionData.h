#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <type_traits>

struct XMLElement;

class FieldCycleMissionQuest {
public:
    FieldCycleMissionQuest() : m_nQuestID(0), m_nCompletePoint(0), m_nCancelPoint(0) {}

    int m_nQuestID; // +0x00
    int m_nCompletePoint; // +0x04
    int m_nCancelPoint; // +0x08
};

class FieldCycleMissionGroup {
public:
    FieldCycleMissionGroup() : m_nCategoryID(0), m_nLevelMin(0), m_nLevelMax(0) {}
    ~FieldCycleMissionGroup();

    // Present in the original public surface, but not compiled by the current
    // executable. Keep it declaration-only until a current machine body is
    // observable.
    const FieldCycleMissionQuest* FindQuest(int questID) const;
    const FieldCycleMissionQuest* FindNextQuest(int questID) const;

    int m_nCategoryID; // +0x00
    int m_nLevelMin; // +0x04
    int m_nLevelMax; // +0x08
    std::vector<FieldCycleMissionQuest*> m_Quests; // +0x10
};

class FieldCycleMissionStep {
public:
    FieldCycleMissionStep() : m_nStepNo(0) {}
    ~FieldCycleMissionStep();

    const FieldCycleMissionGroup* FindGroup(int classID, int level) const;

    int m_nStepNo; // +0x00
    std::vector<FieldCycleMissionGroup*> m_Groups; // +0x08
};

class FieldCycleMissionData {
public:
    using StepMap = std::map<int, FieldCycleMissionStep*>;

    FieldCycleMissionData();
    ~FieldCycleMissionData();

    const FieldCycleMissionStep* FindStep(int stepNo) const;

    int m_nID; // +0x00
    StepMap m_Steps; // +0x08
};


class FieldCycleMissionDB : public ::Singleton<FieldCycleMissionDB> {
public:
    using MissionMap = std::map<int, FieldCycleMissionData*>;
    using FieldCycleIDMap = std::map<int, int>;
    using QuestMap = std::map<int, FieldCycleMissionQuest*>;

    FieldCycleMissionDB();
    ~FieldCycleMissionDB() override;

    void Load();
    const FieldCycleMissionData* FindData(int fieldCycleID) const;
    const FieldCycleMissionQuest* FindQuest(int questID) const;
    bool IsMissionQuest(int questID) const;
    int RetrieveFieldCycleID(int questID) const;

    // The current image keeps this public declaration but does not emit a
    // body or call it in the startup path.
    void AddData(FieldCycleMissionData* data);
    void Reload();

private:
    void BuildQuestID2FieldCycleIDTableAndQuestCache();

    MissionMap m_Missions; // +0x08
    FieldCycleIDMap m_QuestID2FieldCycleID; // +0x28
    QuestMap m_QuestCache; // +0x48
};

namespace FieldCycleMissionUtil {

bool LoadFromXML(FieldCycleMissionQuest* quest, const XMLElement* element);
bool LoadFromXML(FieldCycleMissionGroup* group, const XMLElement* element);
bool LoadFromXML(FieldCycleMissionStep* step, const XMLElement* element);
bool LoadFromXML(FieldCycleMissionData* data, const XMLElement* element);

int RetrieveMissionQuestID(const FieldCycleMissionDB* database, const std::vector<int>& questIDs);

const FieldCycleMissionGroup* FindGroupByClass(
    const FieldCycleMissionDB* database, int fieldCycleID, int stepNo, int classID, int level
);

const FieldCycleMissionQuest* FindNextQuest(
    const FieldCycleMissionDB* database, int fieldCycleID, int stepNo, int classID, int level, int currentQuestID
);

} // namespace FieldCycleMissionUtil


static_assert(std::is_polymorphic_v<FieldCycleMissionDB>);
