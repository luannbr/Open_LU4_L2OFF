#pragma once

#include "LexerTeleportQuestPath.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CParserForTeleportQuestPath {
public:
    CParserForTeleportQuestPath();
    ~CParserForTeleportQuestPath();

    int Create();
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

private:
    std::map<std::int32_t, std::int32_t> m_telPositionList; // +0xf0
    std::int32_t m_nNpcID; // +0x110
    std::int32_t m_nNpcLocZoneID; // +0x114

public:
    CLexerForTeleportQuestPath m_lexer; // +0x118
};
