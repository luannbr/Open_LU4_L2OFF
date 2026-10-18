#pragma once

#include "LexerQuestSystem.h"
#include "Quest.h"

#include <cstddef>
#include <cstdint>

class CParserForQuestSystem {
public:
    CParserForQuestSystem();
    ~CParserForQuestSystem();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

private:
    friend class CQuestSystem;

public:
    CLexerForQuestSystem m_lexer; // +0x0f0

private:
    std::int32_t m_QuestId; // +0x1d0
    std::int32_t m_OnetimeQuestId; // +0x1d4
    CQuest* m_pQuest; // +0x1d8
    QuestFeedback* m_pFeedback; // +0x1e0
    QuestPage* m_pPage; // +0x1e8
    CQuestBase::OperationList m_OperationList; // +0x1f0
    CQuestBase::ConditionList m_ConditionList; // +0x210
    QuestCondition_Item* m_pConditionItem; // +0x230
    QuestCondition_FullInven* m_pConditionInven; // +0x238
    QuestOperation_Item::ItemDataListType m_ItemManiDataList; // +0x240
    std::int32_t m_ItemManiState; // +0x260
};
