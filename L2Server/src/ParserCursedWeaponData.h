#pragma once

#include "CursedWeapon.h"
#include "LexerCursedWeaponData.h"

#include <cstddef>
#include <cstdint>

class CParserForCursedWeapon {
public:
    CParserForCursedWeapon();
    ~CParserForCursedWeapon();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CLexerForCursedWeapon m_lexer; // +0x0f0

private:
    [[maybe_unused]] std::int32_t classType; // +0x1c8
    [[maybe_unused]] std::uint32_t m_msvcPadding; // +0x1cc
    CursedWeaponInfo* pInfo; // +0x1d0
};
