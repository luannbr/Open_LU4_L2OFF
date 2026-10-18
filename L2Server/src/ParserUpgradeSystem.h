#pragma once

#include "LexerUpgradeSystem.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CObjectDB;
class CUpgradeSystem;

class CParserForUpgradeSystem {
public:
    CParserForUpgradeSystem();
    ~CParserForUpgradeSystem();

    int Create(CObjectDB* objectDb, char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CLexerForUpgradeSystem m_lexer; // +0xf0

private:
    CObjectDB* m_pObjDB; // +0x1d8
    CUpgradeSystem* m_pUpgradeSystem; // +0x1e0
    [[maybe_unused]] char16_t* m_pszDesc; // +0x1e8
    std::vector<std::int64_t> m_tmpIntVector; // +0x1f0
};
