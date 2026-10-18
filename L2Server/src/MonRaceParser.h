#pragma once

#include "MonRaceLex.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>

class CMonRaceData;

class CParserForMonRace {
public:
    CParserForMonRace();
    ~CParserForMonRace();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForMonRace m_lexer; // +0xf0

private:
    CMonRaceData* m_monrace; // +0x1d0
    FVector m_point; // +0x1d8
    std::vector<FVector> m_pointlist; // +0x1f0
    int m_feerate; // +0x210
};
