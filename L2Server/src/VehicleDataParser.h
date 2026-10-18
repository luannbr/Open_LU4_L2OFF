#pragma once

#include "Vehicle.h"
#include "VehicleDataLex.h"

#include <cstddef>

class CParserForVehicle {
public:
    CParserForVehicle();
    ~CParserForVehicle();

    int Create(const wchar_t* fileName);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CVehicle* m_pVehicle; // +0xf0
    CVehicleController* m_pController; // +0xf8
    CVehicleStationInfo* m_pStation; // +0x100
    VehicleActionInfo* m_pAction; // +0x108
    ::std::vector<int>* m_pMessageList; // +0x110
    CLexerForVehicle m_lexer; // +0x118
};
