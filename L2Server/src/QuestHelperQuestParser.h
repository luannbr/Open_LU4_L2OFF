#pragma once

#include "QuestHelperQuestLexer.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CParserForQuestHelperQuest {
public:
    CParserForQuestHelperQuest();
    ~CParserForQuestHelperQuest();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    // Report a syntax error. lookahead is the bison token that the parser read.
    void yysyntaxerror(int lookahead);

    CLexerForQuestHelperQuest m_lexer; // +0x0f0
    std::int32_t QuestID; // +0x2c8

private:
    [[maybe_unused]] std::byte m_alignment2ccTo2d0[4];

public:
    std::u16string QuestName; // +0x2d0
    std::u16string NPCName; // +0x2f8
    std::u16string Condition; // +0x320
    std::int32_t Choice; // +0x348

private:
    [[maybe_unused]] std::byte m_alignment34cTo350[4];

public:
    std::u16string Handler; // +0x350
    std::int32_t ConditionLine; // +0x378
    std::int32_t HandlerLine; // +0x37c
    std::int32_t Ask; // +0x380
    std::int32_t Reply; // +0x384
    std::set<std::u16string> WordSet; // +0x388
};
