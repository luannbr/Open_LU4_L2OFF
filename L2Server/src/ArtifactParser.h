#pragma once

#include "ArtifactLexer.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CParserForArtifact {
public:
    CParserForArtifact();
    ~CParserForArtifact();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CLexerForArtifact m_lexer; // +0xf0

private:
    std::vector<int> m_tmpIntVector; // +0x1d8
    int m_nEnchant; // +0x1f8
    int m_nMinEnchantMaterial; // +0x1fc
    int m_nGroupId; // +0x200
    int m_nItemId; // +0x204
    int m_nCount; // +0x208
    int m_nResultProb; // +0x20c
    std::vector<std::int64_t> m_SkillList; // +0x210
};
