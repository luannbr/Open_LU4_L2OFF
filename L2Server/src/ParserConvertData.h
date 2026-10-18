#pragma once

#include "LexerConvertData.h"

#include <cstddef>
#include <cstdint>

class CParserForConvertData {
public:
    CParserForConvertData();
    ~CParserForConvertData();

    int Create();
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

private:
    std::int32_t m_nInput; // +0xf0
    std::int32_t m_nOutput; // +0xf4

public:
    CLexerForConvertData m_lexer; // +0xf8
};
