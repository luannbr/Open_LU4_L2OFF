#pragma once

#include "EventCampaign.h"
#include "EventCampaignLexer.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>

class CParserForEventCampaign {
public:
    CParserForEventCampaign();
    ~CParserForEventCampaign();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void Init();
    void AddEventCampaign();

    CLexerForEventCampaign m_lexer; // +0x0f0
    CEventCampaign* m_pEventCampaign; // +0x1c8
    CEventCampaignStep* m_pStep; // +0x1d0
    CEventCampaignStep* m_pPrevStep; // +0x1d8
    CEventCampaignStep* m_pNextStep; // +0x1e0
    CEventCampaignStepMultiInfo* m_pStepMultiInfo; // +0x1e8
    CEventCampaignStepMultiInfo::RewardItemDataType m_rewardItemData; // +0x1f0
    std::vector<int> intList; // +0x200
    std::vector<std::u16string> stringList; // +0x220
};
