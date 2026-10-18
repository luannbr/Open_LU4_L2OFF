#pragma once

#include "Fishing.h"
#include "FishingLex.h"

#include <cstddef>

class CParserForFishing {
public:
    CParserForFishing();
    ~CParserForFishing();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CFishingDistributionInfo* m_pDistributionInfo; // +0xf0
    CFishingPlaceInfo* m_pPlaceInfo; // +0xf8
    CFishInfo* m_pFishInfo; // +0x100
    CFishingLureInfo* m_pLureInfo; // +0x108
    CFishingRodInfo* m_pRodInfo; // +0x110
    CFishingMonsterInfo* m_pMonsterInfo; // +0x118
    CFishingSkillInfo* m_pSkillInfo; // +0x120
    double m_dProbability; // +0x128
    std::vector<DistributionStruct*> m_vPreference; // +0x130
    CTerritory* m_pTerritory; // +0x150
    FVector* m_pPoint; // +0x158
    CLexerForFishing m_lexer; // +0x160
};
