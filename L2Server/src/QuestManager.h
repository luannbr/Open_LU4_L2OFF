#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

class CParserForTeleportQuestPath;
class QuestData;
class UserQuest;

// Current QuestManager.cpp singleton owner. The pointer at +0x08 is the
// quest-PCH stream used by the adjacent UserQuest loader. The three target
// tree owners and the complete 0x70-byte extent are fixed by
// QuestManager::QuestManager@0x00a144d0 and its destructor@0x00a145e4.
class QuestManager : public Singleton<QuestManager> {
public:
    using TelPositionMap = std::map<std::int32_t, std::int32_t>;
    using TelPathMap = std::map<std::int32_t, TelPositionMap>;

    QuestManager();
    ~QuestManager() override;

    int LoadUserQuest(bool onlyPrecompiledHeader);
    bool ReloadUserQuest();
    static void LoadTelQuestPath();
    void Uniputs(char16_t* text);
    int BuildQuestDB(std::list<QuestData*>& quests);
    UserQuest* CreateQuest(QuestData* data);
    UserQuest* GetQuest(int questId);
    int FindFirstTelList(int npcClassId, int selectedQuestZone);

    std::FILE* PCHFile; // +0x08
    std::multimap<std::int32_t, std::int32_t> m_mmmapNpcZone; // +0x10
    TelPathMap m_mapTelPath; // +0x30
    std::map<std::int32_t, UserQuest*> m_questDB; // +0x50
};
