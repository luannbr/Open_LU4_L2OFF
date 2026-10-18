#pragma once

#include "QuestData.h"
#include "QuestHelperUserQuestLexer.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CParserForQuestHelperUserQuest {
public:
    CParserForQuestHelperUserQuest();
    ~CParserForQuestHelperUserQuest();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    // Report a syntax error. lookahead is the bison token that the parser read.
    void yysyntaxerror(int lookahead);

    CLexerForQuestHelperUserQuest m_lexer; // +0x0f0
    std::int32_t m_choice; // +0x2c8
    std::int32_t m_ask; // +0x2cc
    std::int32_t m_reply; // +0x2d0

private:
    [[maybe_unused]] std::byte m_alignment2d4To2d8[4];

public:
    std::vector<std::int32_t> m_tempIntVector; // +0x2d8
    std::vector<std::int32_t> m_npcList; // +0x2f8
    QuestNpcEventType m_eventType; // +0x318
    std::int32_t QuestID; // +0x31c
    std::u16string QuestName; // +0x320
    std::int64_t m_line; // +0x348
    bool bSaveHtml; // +0x350

private:
    [[maybe_unused]] std::byte m_alignment351To358[7];

public:
    std::u16string strHtmlFileName; // +0x358
};
