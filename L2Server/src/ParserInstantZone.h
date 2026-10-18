#pragma once

#include "LexerInstantZone.h"

#include "InstantZone.h"

#include <cstddef>
#include <cstdint>
#include <utility>

class CParserForInstantZone {
public:
    CParserForInstantZone();
    ~CParserForInstantZone();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void AddInstantZone();
    void yysyntaxerror();

private:
    friend class CInstantZoneDB;

    CInstantZone* m_pInstantZone; // +0x0f0
    CInstantZoneConst* m_pInstantZoneConst; // +0x0f8
    char16_t* m_pInstantZoneName; // +0x100
    FVector m_Vector; // +0x108
    bool m_bAppendingVector; // +0x120
    [[maybe_unused]] std::byte m_padding121[7]; // +0x121..+0x127
    CInstantZoneConst::VectorListType* m_pVectorList; // +0x128
    std::vector<int>* m_pIntegerList; // +0x130
    CInstantZoneConst::ItemList m_ItemList; // +0x138
    CInstantZoneCluster* m_pCluster; // +0x158
    CInstantZoneGroup* m_pGroup; // +0x160
    std::int32_t m_nExtraEntrance; // +0x168
    [[maybe_unused]] std::byte m_padding16c[4]; // +0x16c..+0x16f

public:
    CLexerForInstantZone m_lexer; // +0x170

private:
    std::int32_t m_nInstantZoneCount; // +0x250
    std::int32_t m_nMaximumZoneCount; // +0x254
};
