#include "FieldCycleMissionData.h"

#include "XmlLoader.h"

#include "Shared/src/CategoryDataDB.h"
#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/TextRuntime.h"

#include <string>
#include <string_view>

namespace {

// Noise: Operator records now receive the source basename from the logging macro.
// constexpr const wchar_t* kSourceFile =
//     L"FieldCycleMissionData.cpp".
constexpr const wchar_t* kDataFile = L"..\\Script\\FieldCycleMissionData.xml";

constexpr std::u16string_view kMissionElement = u"FieldCycleMission";
constexpr std::u16string_view kStepElement = u"Step";
constexpr std::u16string_view kGroupElement = u"Group";
constexpr std::u16string_view kQuestElement = u"Quest";

bool HasName(const XMLElement* element, std::u16string_view expected) {
    return element && runtime::StringView(element->m_sName) == expected;
}

// void LogInvalidElement(
//     const XMLElement* element,
//     int line) {
//     const std::wstring name =
//         element
//             ? Utf16ToWide(element->m_sName.view())
//             : std::wstring()
//     // Log.Add(
//     //     LOG_ERROR,
//     //     L"[FIELD_CYCLE][%s][%d]Invalid element: %s",
//     //     kSourceFile,
//     //     line,
//     //     name.c_str())
//     L2_LOG_ERROR(Log,
//         L"[FIELD_CYCLE][%s][%d]Invalid element: %s",
//         kSourceFile,
//         line,
//         name.c_str())
// }
void LogInvalidElement(const XMLElement* element, const char* expected) {
    L2_LOG_ERROR(
        Log, "FieldCycleMissionData.xml: expected element '%s', got '%s'; this element is rejected.", expected,
        element != nullptr ? Utf16ToUtf8(element->m_sName).c_str() : "<null>"
    );
}

} // namespace

FieldCycleMissionGroup::~FieldCycleMissionGroup() {
    for (FieldCycleMissionQuest* quest : m_Quests) {
        delete quest;
    }
    m_Quests.clear();
}

const FieldCycleMissionQuest* FieldCycleMissionGroup::FindNextQuest(int questID) const {
    if (m_Quests.empty()) {
        return nullptr;
    }

    const int questCount = static_cast<int>(m_Quests.size());
    if (questID != 0 && questCount != 1) {
        for (int index = 0; index < questCount - 1; ++index) {
            if (runtime::CheckedVectorAt(m_Quests, index)->m_nQuestID == questID) {
                return runtime::CheckedVectorAt(m_Quests, index + 1);
            }
        }
    }
    return runtime::CheckedVectorAt(m_Quests, 0);
}

FieldCycleMissionStep::~FieldCycleMissionStep() {
    for (FieldCycleMissionGroup* group : m_Groups) {
        delete group;
    }
    m_Groups.clear();
}

const FieldCycleMissionGroup* FieldCycleMissionStep::FindGroup(int classID, int level) const {
    L2_GUARD_FUNCTION();

    for (FieldCycleMissionGroup* group : m_Groups) {
        if (group->m_nLevelMin <= level && level <= group->m_nLevelMax &&
            CCategoryDataDB::Inst().IsInCategory(classID, group->m_nCategoryID)) {
            return group;
        }
    }
    return nullptr;
}

FieldCycleMissionData::FieldCycleMissionData() : m_nID(0) {}

FieldCycleMissionData::~FieldCycleMissionData() {
    for (const auto& entry : m_Steps) {
        delete entry.second;
    }
    m_Steps.clear();
}

const FieldCycleMissionStep* FieldCycleMissionData::FindStep(int stepNo) const {
    L2_GUARD_FUNCTION();

    const auto found = m_Steps.find(stepNo);
    return found != m_Steps.end() ? found->second : nullptr;
}

FieldCycleMissionDB::FieldCycleMissionDB() {}

FieldCycleMissionDB::~FieldCycleMissionDB() {
    for (const auto& entry : m_Missions) {
        delete entry.second;
    }
    m_Missions.clear();
}

const FieldCycleMissionData* FieldCycleMissionDB::FindData(int fieldCycleID) const {
    L2_GUARD_FUNCTION();

    const auto found = m_Missions.find(fieldCycleID);
    return found != m_Missions.end() ? found->second : nullptr;
}

const FieldCycleMissionQuest* FieldCycleMissionDB::FindQuest(int questID) const {
    L2_GUARD_FUNCTION();

    const auto found = m_QuestCache.find(questID);
    return found != m_QuestCache.end() ? found->second : nullptr;
}

int FieldCycleMissionDB::RetrieveFieldCycleID(int questID) const {
    L2_GUARD_FUNCTION();

    const auto found = m_QuestID2FieldCycleID.find(questID);
    return found != m_QuestID2FieldCycleID.end() ? found->second : 0;
}

bool FieldCycleMissionDB::IsMissionQuest(int questID) const {
    L2_GUARD_FUNCTION();

    return m_QuestID2FieldCycleID.find(questID) != m_QuestID2FieldCycleID.end();
}

void FieldCycleMissionDB::BuildQuestID2FieldCycleIDTableAndQuestCache() {
    L2_GUARD_FUNCTION();

    for (const auto& missionEntry : m_Missions) {
        const int fieldCycleID = missionEntry.first;
        const FieldCycleMissionData* data = missionEntry.second;
        for (const auto& stepEntry : data->m_Steps) {
            const FieldCycleMissionStep* step = stepEntry.second;
            for (const FieldCycleMissionGroup* group : step->m_Groups) {
                for (FieldCycleMissionQuest* quest : group->m_Quests) {
                    m_QuestID2FieldCycleID[quest->m_nQuestID] = fieldCycleID;
                    m_QuestCache[quest->m_nQuestID] = quest;
                }
            }
        }
    }
}

void FieldCycleMissionDB::Load() {
    L2_GUARD_FUNCTION();

    XMLElement* root = CXmlLoader::inst().Load(kDataFile);
    if (!root) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]Failed to load : %s",
        //     kSourceFile,
        //     0xa5,
        //     kDataFile)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]Failed to load : %s",
        //     kSourceFile,
        //     0xa5,
        //     kDataFile)
        L2_LOG_ERROR(
            Log, "Cannot load FieldCycleMissionData.xml; no mission definitions are added and lookup caches are not rebuilt."
        );
        return;
    }

    for (XMLElement* element : root->m_vChild) {
        FieldCycleMissionData* data = new FieldCycleMissionData;
        if (!FieldCycleMissionUtil::LoadFromXML(data, element)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[FIELD_CYCLE][%s][%d]Failed to load: %s",
            //     kSourceFile,
            //     0xb0,
            //     L"FieldCycleMission")
            // L2_LOG_ERROR(Log,
            //     L"[FIELD_CYCLE][%s][%d]Failed to load: %s",
            //     kSourceFile,
            //     0xb0,
            //     L"FieldCycleMission")
            L2_LOG_ERROR(
                Log,
                "FieldCycleMissionData.xml: a mission entry was rejected; that entry is discarded and other missions "
                "continue loading."
            );
            delete data;
        } else {
            m_Missions[data->m_nID] = data;
        }
    }

    root->ReleaseChild();
    delete root;
    BuildQuestID2FieldCycleIDTableAndQuestCache();
}

void FieldCycleMissionDB::Reload() {
    L2_GUARD_FUNCTION();

    // Current 0x00438ddc drops the three tree nodes without deleting the
    // formerly referenced mission objects before loading replacements.
    m_Missions.clear();
    m_QuestID2FieldCycleID.clear();
    m_QuestCache.clear();
    Load();
}

namespace FieldCycleMissionUtil {

bool LoadFromXML(FieldCycleMissionQuest* quest, const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!quest) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x12a,
        //     L"(pQuest != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x12a,
        //     L"(pQuest != NULL)")
        L2_LOG_ERROR(Log, "Cannot load FieldCycleMissionData.xml/Quest: the destination object is null; this load fails.");
        return false;
    }
    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x12b,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x12b,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(Log, "Cannot load FieldCycleMissionData.xml/Quest: the XML element is null; this load fails.");
        return false;
    }
    if (!HasName(element, kQuestElement)) {
        // LogInvalidElement(element, 0x12f)
        LogInvalidElement(element, "Quest");
        return false;
    }

    quest->m_nQuestID = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"QuestID"), 0);
    quest->m_nCompletePoint = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"CompletePoint"), 0);
    quest->m_nCancelPoint = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"CancelPoint"), 0);
    return true;
}

bool LoadFromXML(FieldCycleMissionGroup* group, const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!group) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x140,
        //     L"(pGroup != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x140,
        //     L"(pGroup != NULL)")
        L2_LOG_ERROR(Log, "Cannot load FieldCycleMissionData.xml/Group: the destination object is null; this load fails.");
        return false;
    }
    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x141,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x141,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(Log, "Cannot load FieldCycleMissionData.xml/Group: the XML element is null; this load fails.");
        return false;
    }
    if (!HasName(element, kGroupElement)) {
        // LogInvalidElement(element, 0x145)
        LogInvalidElement(element, "Group");
        return false;
    }

    const std::u16string category = XMLUtil::GetAttributeValue(element, std::u16string(u"Category"), std::u16string(u""));
    group->m_nCategoryID = static_cast<int>(g_PrecompiledHeaderDB.Search(category.c_str()));
    if (group->m_nCategoryID == -1) {
        const std::string categoryName = Utf16ToUtf8(runtime::StringView(category));
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]Invalid category: %s",
        //     kSourceFile,
        //     0x14f,
        //     categoryName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]Invalid category: %s",
        //     kSourceFile,
        //     0x14f,
        //     categoryName.c_str())
        L2_LOG_ERROR(
            Log, "FieldCycleMissionData.xml/Group: category '%s' has no PCH definition; this group is rejected.",
            categoryName.c_str()
        );
        return false;
    }

    group->m_nLevelMin = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"LevelMin"), 0);
    group->m_nLevelMax = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"LevelMax"), 0);
    if (group->m_nLevelMax < group->m_nLevelMin) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]Invalid level: %d, %d",
        //     kSourceFile,
        //     0x158,
        //     group->m_nLevelMin,
        //     group->m_nLevelMax)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]Invalid level: %d, %d",
        //     kSourceFile,
        //     0x158,
        //     group->m_nLevelMin,
        //     group->m_nLevelMax)
        L2_LOG_ERROR(
            Log, "FieldCycleMissionData.xml/Group: maximum level %d is below minimum level %d; this group is rejected.",
            group->m_nLevelMax, group->m_nLevelMin
        );
        return false;
    }

    for (XMLElement* child : element->m_vChild) {
        FieldCycleMissionQuest* quest = new FieldCycleMissionQuest;
        if (!LoadFromXML(quest, child)) {
            const std::string categoryName = Utf16ToUtf8(runtime::StringView(category));
            // Log.Add(
            //     LOG_ERROR,
            //     L"[FIELD_CYCLE][%s][%d]Failed to load Mission: %s, %d, %d",
            //     kSourceFile,
            //     0x167,
            //     categoryName.c_str(),
            //     group->m_nLevelMin,
            //     group->m_nLevelMax)
            // L2_LOG_ERROR(Log,
            //     L"[FIELD_CYCLE][%s][%d]Failed to load Mission: %s, %d, %d",
            //     kSourceFile,
            //     0x167,
            //     categoryName.c_str(),
            //     group->m_nLevelMin,
            //     group->m_nLevelMax)
            L2_LOG_ERROR(
                Log,
                "FieldCycleMissionData.xml/Group category '%s', levels %d..%d: a quest entry failed to load; that quest is "
                "discarded and the group continues loading.",
                categoryName.c_str(), group->m_nLevelMin, group->m_nLevelMax
            );
            delete quest;
        } else {
            group->m_Quests.push_back(quest);
        }
    }
    return true;
}

bool LoadFromXML(FieldCycleMissionStep* step, const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!step) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x179,
        //     L"(pStep != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x179,
        //     L"(pStep != NULL)")
        L2_LOG_ERROR(Log, "Cannot load FieldCycleMissionData.xml/Step: the destination object is null; this load fails.");
        return false;
    }
    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x17a,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x17a,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(Log, "Cannot load FieldCycleMissionData.xml/Step: the XML element is null; this load fails.");
        return false;
    }
    if (!HasName(element, kStepElement)) {
        // LogInvalidElement(element, 0x17e)
        LogInvalidElement(element, "Step");
        return false;
    }

    step->m_nStepNo = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"No"), 0);
    if (step->m_nStepNo == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]Invalid No",
        //     kSourceFile,
        //     0x186)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]Invalid No",
        //     kSourceFile,
        //     0x186)
        L2_LOG_ERROR(Log, "FieldCycleMissionData.xml/Step: No is 0 or missing; this step is rejected.");
        return false;
    }

    for (XMLElement* child : element->m_vChild) {
        FieldCycleMissionGroup* group = new FieldCycleMissionGroup;
        if (!LoadFromXML(group, child)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[FIELD_CYCLE][%s][%d]Failed to load Group: %d",
            //     kSourceFile,
            //     0x191,
            //     step->m_nStepNo)
            // L2_LOG_ERROR(Log,
            //     L"[FIELD_CYCLE][%s][%d]Failed to load Group: %d",
            //     kSourceFile,
            //     0x191,
            //     step->m_nStepNo)
            L2_LOG_ERROR(
                Log,
                "FieldCycleMissionData.xml/Step %d: a group failed to load; that group is discarded and the step continues "
                "loading.",
                step->m_nStepNo
            );
            delete group;
        } else {
            step->m_Groups.push_back(group);
        }
    }
    return true;
}

bool LoadFromXML(FieldCycleMissionData* data, const XMLElement* element) {
    L2_GUARD_FUNCTION();

    if (!data) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x1a2,
        //     L"(pData != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x1a2,
        //     L"(pData != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot load FieldCycleMissionData.xml/FieldCycleMission: the destination object is null; this load fails."
        );
        return false;
    }
    if (!element) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x1a3,
        //     L"(pElement != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x1a3,
        //     L"(pElement != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot load FieldCycleMissionData.xml/FieldCycleMission: the XML element is null; this load fails."
        );
        return false;
    }
    if (!HasName(element, kMissionElement)) {
        // LogInvalidElement(element, 0x1a7)
        LogInvalidElement(element, "FieldCycleMission");
        return false;
    }

    data->m_nID = XMLUtil::GetAttributeValueAsInt(element, std::u16string(u"ID"), 0);
    if (data->m_nID == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]Invalid ID",
        //     kSourceFile,
        //     0x1af)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]Invalid ID",
        //     kSourceFile,
        //     0x1af)
        L2_LOG_ERROR(Log, "FieldCycleMissionData.xml/FieldCycleMission: ID is 0 or missing; this mission is rejected.");
        return false;
    }

    for (XMLElement* child : element->m_vChild) {
        FieldCycleMissionStep* step = new FieldCycleMissionStep;
        if (!LoadFromXML(step, child)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[FIELD_CYCLE][%s][%d]Failed to load Step: %d",
            //     kSourceFile,
            //     0x1ba,
            //     data->m_nID)
            // L2_LOG_ERROR(Log,
            //     L"[FIELD_CYCLE][%s][%d]Failed to load Step: %d",
            //     kSourceFile,
            //     0x1ba,
            //     data->m_nID)
            L2_LOG_ERROR(
                Log,
                "FieldCycleMissionData.xml/FieldCycleMission %d: a step failed to load; that step is discarded and the "
                "mission continues loading.",
                data->m_nID
            );
            delete step;
        } else {
            data->m_Steps[step->m_nStepNo] = step;
        }
    }
    return true;
}

int RetrieveMissionQuestID(const FieldCycleMissionDB*, const std::vector<int>& questIDs) {
    L2_GUARD_FUNCTION();

    for (int questID : questIDs) {
        if (FieldCycleMissionDB::inst().IsMissionQuest(questID)) {
            return questID;
        }
    }
    return 0;
}

const FieldCycleMissionGroup* FindGroupByClass(
    const FieldCycleMissionDB* database, int fieldCycleID, int stepNo, int classID, int level
) {
    L2_GUARD_FUNCTION();

    if (!database) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x1d0,
        //     L"(pDB != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     kSourceFile,
        //     0x1d0,
        //     L"(pDB != NULL)")
        L2_LOG_ERROR(
            Log,
            "Cannot select a mission group for field cycle %d, step %d, class %d, level %d: the mission database is null; "
            "no group is returned.",
            fieldCycleID, stepNo, classID, level
        );
        return nullptr;
    }

    const FieldCycleMissionData* data = database->FindData(fieldCycleID);
    if (!data) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]No data: %d",
        //     kSourceFile,
        //     0x1d6,
        //     fieldCycleID)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]No data: %d",
        //     kSourceFile,
        //     0x1d6,
        //     fieldCycleID)
        L2_LOG_ERROR(
            Log,
            "Cannot select a mission group for field cycle %d, step %d, class %d, level %d: no mission definition exists; "
            "no group is returned.",
            fieldCycleID, stepNo, classID, level
        );
        return nullptr;
    }

    const FieldCycleMissionStep* step = data->FindStep(stepNo);
    if (!step) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]No step: %d",
        //     kSourceFile,
        //     0x1de,
        //     stepNo)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]No step: %d",
        //     kSourceFile,
        //     0x1de,
        //     stepNo)
        L2_LOG_ERROR(
            Log,
            "Cannot select a mission group for field cycle %d, step %d, class %d, level %d: the step is missing from the "
            "mission definition; no group is returned.",
            fieldCycleID, stepNo, classID, level
        );
        return nullptr;
    }
    return step->FindGroup(classID, level);
}

const FieldCycleMissionQuest* FindNextQuest(
    const FieldCycleMissionDB* database, int fieldCycleID, int stepNo, int classID, int level, int currentQuestID
) {
    L2_GUARD_FUNCTION();

    const FieldCycleMissionGroup* group = FindGroupByClass(database, fieldCycleID, stepNo, classID, level);
    return group ? group->FindNextQuest(currentQuestID) : nullptr;
}

} // namespace FieldCycleMissionUtil
