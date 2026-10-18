// CLexerForArmorEnchantBonus scanner (original L2Server/lexer_armorenchantbonus.l).
// re2c generates LexerArmorEnchantBonus.cpp.

#include "L2Server/src/LexerArmorEnchantBonus.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserArmorEnchantBonus.h"

#include "Shared/src/Log.h"

#include <cerrno>
#include <cstdint>
#include <cwctype>
#include <limits>
#include <string_view>

#include "ParserArmorEnchantBonusTokens.h"

namespace {

// Current L2Server 0x015efe3c. The generated stream lexer discards the
// UTF-16 byte-order mark only on the first input stream in the process.
bool firstTime = true;

std::int32_t Msvc2010Wcstol(const char16_t* value) {
    while (std::iswspace(static_cast<std::wint_t>(*value)) != 0) {
        ++value;
    }

    bool negative = false;
    if (*value == u'-' || *value == u'+') {
        negative = *value == u'-';
        ++value;
    }

    const std::uint32_t limit = negative ? 0x80000000U : 0x7fffffffU;
    std::uint32_t result = 0;
    bool converted = false;
    bool overflow = false;
    while (*value >= u'0' && *value <= u'9') {
        converted = true;
        const std::uint32_t digit = static_cast<std::uint32_t>(*value - u'0');
        if (result > (limit - digit) / 10U) {
            overflow = true;
        } else if (!overflow) {
            result = result * 10U + digit;
        }
        ++value;
    }

    if (!converted) {
        return 0;
    }
    if (overflow) {
        errno = ERANGE;
        return negative ? std::numeric_limits<std::int32_t>::min() : std::numeric_limits<std::int32_t>::max();
    }
    if (!negative) {
        return static_cast<std::int32_t>(result);
    }
    if (result == 0x80000000U) {
        return std::numeric_limits<std::int32_t>::min();
    }
    return -static_cast<std::int32_t>(result);
}

} // namespace

CLexerForArmorEnchantBonus::CLexerForArmorEnchantBonus() : line(0) {
}

int CLexerForArmorEnchantBonus::Create() { return 1; }

int CLexerForArmorEnchantBonus::yygetchar() {
    if (firstTime) {
        yyin->get();
        firstTime = false;
    }
    return ReadStream();
}

int CLexerForArmorEnchantBonus::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        SetReader([this] { return yygetchar(); });
    }
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "onepiece_factor" { EndToken(); return TK_ONEPIECE_FACTOR; }
            "bonus_grade_none" { EndToken(); return TK_BONUS_GRADE_NONE; }
            "bonus_grade_d" { EndToken(); return TK_BONUS_GRADE_D; }
            "bonus_grade_c" { EndToken(); return TK_BONUS_GRADE_C; }
            "bonus_grade_b" { EndToken(); return TK_BONUS_GRADE_B; }
            "bonus_grade_a" { EndToken(); return TK_BONUS_GRADE_A; }
            "bonus_grade_s" { EndToken(); return TK_BONUS_GRADE_S; }
            "bonus_grade_s80" { EndToken(); return TK_BONUS_GRADE_S80; }
            "bonus_grade_s84" { EndToken(); return TK_BONUS_GRADE_S84; }
            "bonus_grade_r" { EndToken(); return TK_BONUS_GRADE_R; }
            "bonus_grade_r95" { EndToken(); return TK_BONUS_GRADE_R95; }
            "bonus_grade_r99" { EndToken(); return TK_BONUS_GRADE_R99; }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(static_cast<std::int64_t>(Msvc2010Wcstol(yytext)));
                return TK_INTEGER;
            }
            [%(),;=[\]{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            [ \t\r] { EndToken(); continue; }
            "\n" {
                EndToken();
                ++line;
                continue;
            }
            . {
                EndToken();
                // Log.Add(
                //     LOG_ERROR,
                //     L"invalid char [%c] at line [%d]",
                //     static_cast<wchar_t>(*yytext),
                //     yylineno)
                // L2_LOG_ERROR(Log,
                //     L"invalid char [%c] at line [%d]",
                //     static_cast<wchar_t>(*yytext),
                //     yylineno)
                L2_LOG_ERROR(
                    Log, "Unexpected input '%s' (first UTF-16 code unit 0x%04x) at script line %lld; scanning continues.",
                    Utf16ToUtf8(std::u16string_view(yytext, static_cast<std::size_t>(yyleng))).c_str(),
                    static_cast<unsigned>(static_cast<std::uint16_t>(*yytext)), static_cast<long long>(yylineno)
                );
                continue;
            }
            $ { return 0; }
        */
    }
}
