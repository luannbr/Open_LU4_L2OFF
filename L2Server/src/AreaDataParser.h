#pragma once

#include "Area.h"
#include "AreaDataLex.h"

#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CGeoSector;
class CGeoZone;
class CSkillInfo;

class CParserForArea {
public:
    using Territories = std::vector<CTerritory*>;
    using PointsList = std::vector<FVector>;

    CParserForArea();
    ~CParserForArea();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CGeoSector* m_pGeoSector; // +0x0f0
    CTerritory* m_pTerritory; // +0x0f8
    Territories* m_pTerritoryList; // +0x100
    PointsList* m_pPointsList; // +0x108
    CGeoZone* m_pGeoZone; // +0x110
    std::array<FVector, 8> m_vWaterPos; // +0x118
    FVector* m_pVector; // +0x1d8
    CArea* m_pArea; // +0x1e0
    std::vector<char16_t*>* pStringVector; // +0x1e8
    std::int32_t m_nMoveBonusDef; // +0x1f0
    std::int32_t m_nDamageOnHPDef; // +0x1f4
    std::int32_t m_nDamageOnMPDef; // +0x1f8
    [[maybe_unused]] std::byte m_padding1fc[4]; // +0x1fc
    CSkillInfo* m_pSkillInfoDef; // +0x200
    std::int32_t m_nHpRegenBonus; // +0x208
    std::int32_t m_nMpRegenBonus; // +0x20c
    std::int32_t m_nEventID; // +0x210
    [[maybe_unused]] std::byte m_padding214[4]; // +0x214
    std::vector<std::int64_t> mvIntList; // +0x218
    CLexerForArea m_lexer; // +0x238
};
