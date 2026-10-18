#pragma once

#include "FieldCycle.h"
#include "LexerFieldCycle.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CParserForFieldCycle {
public:
    CParserForFieldCycle();
    ~CParserForFieldCycle();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

private:
    friend class CFieldCycleSystem;

public:
    CLexerForFieldCycle m_lexer; // +0x0f0

private:
    std::int32_t m_nCycleID; // +0x1c8
    std::int32_t m_nStep; // +0x1cc
    std::int32_t m_nPrevCycleID; // +0x1d0
    std::int32_t m_nPrevStep; // +0x1d4
    std::int32_t m_nPoint; // +0x1d8
    std::int32_t m_nLockTime; // +0x1dc
    std::int32_t m_nDropTime; // +0x1e0
    std::int32_t m_nIntervalTime; // +0x1e4
    std::int32_t m_nIntervalPoint; // +0x1e8
    std::int32_t m_nStepChangeDay; // +0x1ec
    std::int32_t m_nStepChangeHour; // +0x1f0
    std::int32_t m_nStepChangeMinute; // +0x1f4
    CFieldCycle* m_pFieldCycle; // +0x1f8
    StepInfo* m_pStepInfo; // +0x200
    std::vector<std::u16string> m_stringList; // +0x208
    RestartInfo* m_pRestartInfo; // +0x228
    CTerritory* m_pTerritory; // +0x230
};
