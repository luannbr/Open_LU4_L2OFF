#pragma once

#include "AirShip.h"
#include "LexerAirShip.h"

#include <cstddef>
#include <cstdint>

class CParserForAirShip {
public:
    CParserForAirShip();
    ~CParserForAirShip();

    int Create(char16_t* buffer, std::int32_t size);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForAirShip m_lexer; // +0xf0
    std::int32_t m_HandlerOption; // +0x1c8
    std::int32_t padding_1cc;
    CAirShipConst* m_AirShipConst; // +0x1d0
    FVector m_Pos; // +0x1d8
    std::vector<FVector> m_PosList; // +0x1f0
    CAirport* m_pAirport; // +0x210
    CAirportPlatform* m_pPlatform; // +0x218
    CAirShipArea* m_pArea; // +0x220
};
