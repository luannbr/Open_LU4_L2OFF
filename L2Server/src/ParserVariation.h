#pragma once

#include "LexerVariation.h"
#include "VariationDB.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CParserForVariation {
public:
    CParserForVariation();
    ~CParserForVariation();

    int Create(VariationDB* variationDb);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForVariation m_lexer; // +0x0f0

private:
    VariationDB* m_pVariationDB; // +0x1d0
    Variation* m_pTempVariation; // +0x1d8
    OptionGroup* m_pTempOptionGroup; // +0x1e0
    VariationType m_variType; // +0x1e8
    [[maybe_unused]] std::byte m_targetPadding1ec[4]; // +0x1ec
    double m_groupRate; // +0x1f0
    double m_setRate; // +0x1f8
    std::map<std::u16string, std::uint16_t, runtime::CStringLess<std::u16string>> m_tempItemGroupNameMap; // +0x200
    std::uint16_t m_tempItemGroupNo; // +0x220
    std::uint16_t m_feeItemGroupNo; // +0x222
    std::uint32_t m_feeIntensiveClassID; // +0x224
};
