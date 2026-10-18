#pragma once

#include "PledgeMissionInfo.h"
#include "PledgeMissionLexer.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class IsInIntList_MissionCondition;

// The grammar is PledgeMissionParser.y. Bison generates yyparse as a member,
// so the grammar actions use these fields. The offset comments give the layout
// in the original image.
class CParserForPledgeMission {
public:
    CParserForPledgeMission();
    ~CParserForPledgeMission();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

private:
    [[maybe_unused]] std::vector<char16_t*>* m_pStringVector; // +0x0f0
    [[maybe_unused]] std::u16string* m_pString; // +0x0f8

public:
    CLexerForPledgeMission m_lexer; // +0x100
    std::int32_t nMissionId; // +0x1d8

private:
    [[maybe_unused]] std::uint32_t m_alignment1dcTo1e0;

public:
    PledgeMissionInfo* pInfo; // +0x1e0
    bool bEmpty; // +0x1e8
    bool bAvailableDay[7]; // +0x1e9
    IsInIntList_MissionCondition* pGoalCondition; // +0x1f0
};
