#pragma once

#include "DynamicQuest.h"
#include "DynamicQuestLexer.h"
#include "QuestBase.h"

#include "Protocol/src/L2Constants.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>

class CCampaign;
class CCampaignData;
class CTerritory;
class DynamicQuest;
struct QuestCondition;
struct QuestFeedback;
class QuestPage;

namespace DynamicQuestSetting {
class AutoExecutionByTimer;
}

class CParserForDynamicQuest {
public:
    CParserForDynamicQuest();
    ~CParserForDynamicQuest();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void Init();
    void AddDynamicQuest(DynamicQuestType::Enum type);

    CLexerForDynamicQuest m_lexer; // +0x0f0
    DynamicQuestType::Enum m_DynamicQuestType; // +0x1c8
    DynamicQuest* m_pDynamicQuest; // +0x1d0
    DynamicQuestSetting::AutoExecutionByTimer* m_pAutoExecutionByTimer; // +0x1d8
    CCampaignData* m_pCampaignData; // +0x1e0
    CCampaign* m_pCampaign; // +0x1e8
    CCampaign* m_pPrevCampaign; // +0x1f0
    CCampaign* m_pNextCampaign; // +0x1f8
    std::vector<int> intList; // +0x200
    std::vector<std::u16string> stringList; // +0x220
    std::vector<QuestCondition*> m_StartConditionList; // +0x240
    std::vector<QuestOperation_Item::ItemDataType> m_ItemManiDataList; // +0x260
    QuestFeedback* m_pFeedback; // +0x280
    QuestPage* m_pPage; // +0x288
    CTerritory* m_pTerritory; // +0x290
    bool m_EnableAdjGoal; // +0x298
};
