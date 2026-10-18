#pragma once

#include "LexerWeaponEnchantEx.h"

#include <cstddef>
#include <cstdint>
#include <vector>

enum AttackType : std::int32_t;
enum CrystalType : std::int32_t;

class CParserForWeaponEnchantExtraBonus {
public:
    CParserForWeaponEnchantExtraBonus();
    ~CParserForWeaponEnchantExtraBonus();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();
    void InitMemberValues();

    CLexerForEnchantExtraBonus m_lexer; // +0x0f0

private:
    [[maybe_unused]] char16_t* m_pszDesc; // +0x1d8. Retained donor name, unused by current
    std::vector<AttackType> m_vAttackType; // +0x1e0
    [[maybe_unused]] std::byte m_currentGap1f8[0x08]; // current-only, no observed behavior
    CrystalType m_grade; // +0x200
    double m_dSoulPower; // +0x208
    double m_dSpiritPower; // +0x210
    std::int32_t m_nIsMagic; // +0x218
};
