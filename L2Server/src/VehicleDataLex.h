#pragma once

#include "Shared/src/Runtime/GrammarRuntime.h"

#include <cstddef>
#include <cstdint>

class CParserForVehicle;

class CLexerForVehicle : public grammar::UTF16Scanner {
public:
    CLexerForVehicle();
    ~CLexerForVehicle();

    int Create(const wchar_t* fileName);
    int Lex(grammar::SemanticBits* lval);
    int yygetchar();

private:
    char* m_pFileBuf; // +0xc0
    std::int32_t m_nFilePointer; // +0xc8
    std::int32_t m_nFileSize; // +0xcc

public:
    std::int32_t line; // +0xd0

private:
    std::int32_t comment_count; // +0xd4
};
