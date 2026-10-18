#pragma once

#include <cstddef>

class CLexerForFaction;

class CParserForFaction {
public:
    CParserForFaction();
    ~CParserForFaction();

    // Read the tokens of the next yyparse call from `lexer`. The caller keeps it alive.
    int Create(CLexerForFaction* lexer);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

private:
    CLexerForFaction* m_pLexer = nullptr;
    int m_nServerFactionID; // +0x0f0
};
