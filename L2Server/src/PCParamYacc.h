#pragma once

#include "PCParam.h"
#include "PCParamLex.h"

#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>

class CParserForPCParam {
public:
    CParserForPCParam();
    ~CParserForPCParam();

    int Create();
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForPCParam m_lexer; // +0x0f0

private:
    const char16_t* parsing_context; // +0x1d0
    std::int32_t parsing_class; // +0x1d8
    [[maybe_unused]] std::int32_t m_targetPadding1dc;
    [[maybe_unused]] double dTmp; // +0x1e0
    [[maybe_unused]] std::int32_t iTmp; // +0x1e8
    [[maybe_unused]] std::int32_t m_targetPadding1ec;
    PCParamDoubleVector DoubleVector; // +0x1f0
    [[maybe_unused]] PCParamDoubleVector* pDoubleVector; // +0x208
    [[maybe_unused]] std::array<std::byte, 0x08> m_targetPadding210{};
    std::map<std::u16string, std::u16string, runtime::CStringLess<std::u16string>> m_mapParameter; // +0x218
};
