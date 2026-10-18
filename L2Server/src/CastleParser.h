#pragma once

#include "CastleLex.h"
#include "Residence.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <utility>

class CNPC;
class CTerritory;

struct AdditionalNPCData {
    char16_t pszName[26]; // +0x00..+0x33
    std::int32_t cost; // +0x34
    std::int32_t num; // +0x38
    [[maybe_unused]] std::byte m_alignment3CTo40[0x04];
    CNPC* pNPC; // +0x40
};

class CParserForResidence {
public:
    CParserForResidence();
    ~CParserForResidence();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CResidenceData* m_pResidenceData; // +0x0f0
    CResidence* m_pResidence; // +0x0f8
    CResidence* m_pCastleResidence; // +0x100
    std::vector<CTerritory*>* m_pLstTerritory; // +0x108
    CTerritory* m_pTerritory; // +0x110
    FVector* m_pVector; // +0x118
    FAngle* m_pAngle; // +0x120
    OwningNPCData* m_pOwningNPCData; // +0x128
    AdditionalNPCData* m_pAdditionalNPCData; // +0x130
    bool m_bVectorVector; // +0x138
    bool m_bPairVector; // +0x139
    [[maybe_unused]] std::byte m_alignment13ATo140[0x06];
    std::vector<FVector>* m_pVectorVector; // +0x140
    std::vector<char16_t*>* m_pStringVector; // +0x148
    std::vector<char16_t*> m_TempStringVector; // +0x150
    std::vector<std::int32_t>* m_pIntVector; // +0x170
    std::vector<std::pair<FVector, FAngle>>* m_pPairVector; // +0x178
    std::u16string m_TempString; // +0x180
    FVector m_TempVector; // +0x1a8
    FAngle m_TempAngle; // +0x1c0
    double m_dTemp1; // +0x1c8
    double m_dTemp2; // +0x1d0
    ResidenceGuardInfo* m_pResidenceGuardInfo; // +0x1d8
    std::vector<char16_t*> m_tempStringVector; // +0x1e0
    std::vector<std::int32_t> m_tempIntVector; // +0x200
    bool m_SSQGuardType[3]; // +0x220
    [[maybe_unused]] std::byte m_alignment223To224;
    std::int32_t m_tempTexRate; // +0x224
    std::int32_t m_tempSysGetRate; // +0x228
    std::int32_t m_tempTributeRate; // +0x22c
    std::int32_t m_tempBuyRate; // +0x230
    std::int32_t m_tempBuySysGetRate; // +0x234
    std::int32_t m_tempBuyTributeRate; // +0x238
    CastleType::Enum m_tempCastleType; // +0x23c
    std::int32_t m_nEditorID; // +0x240
    [[maybe_unused]] std::byte m_alignment244To248[0x04];
    CLexerForResidence m_lexer; // +0x248
};
