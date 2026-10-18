#pragma once

#include "QuestData.h"
#include "QuestDataLex.h"

#include <cstddef>
#include <cstdint>

// The grammar is QuestDataYacc.y. Bison generates yyparse as a member, so the
// grammar actions use these fields. The offset comments give the layout in the
// original image.
class CParserForQuestData {
public:
    using ConditionVector = std::vector<QuestConditionData>;
    using OperationVector = std::vector<QuestOperationData>;
    using ActionVector = std::vector<QuestActionData>;
    using JournalVector = std::vector<QuestJournalData>;
    using ParamVector = std::vector<QuestDataVariable*>;
    using IntVector = std::vector<int>;
    using QuestList = std::list<QuestData*>;

    CParserForQuestData();
    ~CParserForQuestData();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForQuestData m_lexer; // +0x0f0
    bool m_bOnlyPrecompiledHeader; // +0x2c8
    [[maybe_unused]] std::byte m_targetPadding2c9To2d0[7];
    QuestList m_quests; // +0x2d0

private:
    QuestData* m_pQuest; // +0x2e8
    ConditionVector m_startConditions; // +0x2f0
    OperationVector m_acceptOKOperations; // +0x310
    OperationVector m_acceptFailOperations; // +0x330
    ActionVector m_completeActions; // +0x350
    OperationVector m_cancelOperations; // +0x370
    JournalVector m_journals; // +0x390
    ConditionVector m_journalCompleteConditions; // +0x3b0
    OperationVector m_journalCompleteOperations; // +0x3d0
    OperationVector m_journalStartOperations; // +0x3f0
    QuestJournalData m_journal; // +0x410
    ConditionVector m_conditions; // +0x460
    OperationVector m_operations; // +0x480
    ActionVector m_actions; // +0x4a0
    ParamVector m_params; // +0x4c0
    QuestNpcEventData m_npcEvent; // +0x4e0
    std::int32_t m_choice; // +0x598
    std::int32_t m_eventRange; // +0x59c
    std::int32_t m_journalNum; // +0x5a0
    [[maybe_unused]] std::byte m_targetPadding5a4To5a8[4];
    std::int32_t m_ask; // +0x5a8
    std::int32_t m_reply; // +0x5ac
    IntVector m_tempInts; // +0x5b0
    IntVector m_npcList; // +0x5d0
    IntVector m_relatedItems; // +0x5f0
    QuestConditionTargetType m_conditionTargetType; // +0x610
    QuestExecutionType m_executionType; // +0x614
    [[maybe_unused]] std::byte m_targetPadding618To620[8];
    ConditionVector m_npcEventConditions; // +0x620
    ActionVector m_npcEventActions; // +0x640
    std::int32_t m_QuestID; // +0x660
    [[maybe_unused]] std::byte m_targetPadding664To668[4];
    std::u16string m_QuestName; // +0x668
};
