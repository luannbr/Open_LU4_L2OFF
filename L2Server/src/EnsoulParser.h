#pragma once

#include "EnsoulData.h"
#include "EnsoulLexer.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CParserForEnsoul {
public:
    CParserForEnsoul();
    ~CParserForEnsoul();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CLexerForEnsoul m_lexer; // +0x0f0
    std::int32_t m_nLine; // +0x1d8
    bool m_bOnlyPrecompiledHeader; // +0x1dc
    bool m_bReload; // +0x1dd
    std::vector<std::int64_t> m_vEnsoulOptionData; // +0x1e0
    EnsoulFeeItemData* m_pEnsoulFeeItemData; // +0x200
    EnsoulFeeData::FeeItemVector m_vEnsoulFeeItemData; // +0x208
    EnsoulFeeData* m_pEnsoulFeeData; // +0x228
    EnsoulOptionData* m_pEnsoulOptionData; // +0x230
    EnsoulStoneData* m_pEnsoulStoneData; // +0x238
    EnsoulOptionData* m_pEmptyEnsoulOptionData; // +0x240
};
