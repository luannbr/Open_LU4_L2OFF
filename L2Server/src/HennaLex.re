// CLexerForHenna scanner (original L2Server/henna_lex.l).
// re2c generates HennaLex.cpp.

#include "L2Server/src/HennaLex.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/HennaParser.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "HennaParserTokens.h"

namespace {

std::string NarrowAscii(std::u16string_view value) {
    std::string ascii;
    ascii.reserve(value.size());
    for (const char16_t codeUnit : value) {
        if (codeUnit > 0x7f) {
            return {};
        }
        ascii.push_back(static_cast<char>(codeUnit));
    }
    return ascii;
}

std::int64_t ParseWindowsInt64(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    if (ascii.empty() && !value.empty()) {
        return 0;
    }

    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(ascii.c_str(), &end, 10);
    if (end == ascii.c_str()) {
        return 0;
    }
    if (errno == ERANGE) {
        return parsed < 0 ? INT64_MIN : INT64_MAX;
    }
    return static_cast<std::int64_t>(parsed);
}

char16_t* DuplicateText(const char16_t* source, int length) {
    auto* const result = static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(length + 1) * sizeof(char16_t)));
    if (result == nullptr) {
        return nullptr;
    }
    std::copy_n(source, length, result);
    result[length] = u'\0';
    return result;
}

} // namespace

CLexerForHenna::CLexerForHenna() : m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0), comment_count(0) {
}

CLexerForHenna::~CLexerForHenna() { m_FileBuf = nullptr; }

int CLexerForHenna::Create(char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN};

} // namespace

int CLexerForHenna::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_FileBuf one code unit at a time.
        SetInput(
            m_FileBuf + m_nFilePointer,
            m_nFilePointer < m_nFileSize ? static_cast<std::size_t>(m_nFileSize - m_nFilePointer) : 0
        );
    }
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            re2c:define:YYGETCONDITION = "kConditionByStart[yystart / 2]";
            <COMMENT> "/*" {
                EndToken();
                ++comment_count;
                continue;
            }
            <COMMENT> "*/" {
                EndToken();
                --comment_count;
                if (comment_count == 0) {
                    yybegin(4);
                }
                continue;
            }
            <COMMENT> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <COMMENT> . { EndToken(); continue; }
            <MAIN> "//" [^\n]* / "\n" { EndToken(); continue; }
            <MAIN> "/*" {
                EndToken();
                yybegin(2);
                comment_count = 1;
                continue;
            }
            <MAIN> "dye_begin" { EndToken(); return TK_DYE_BEGIN; }
            <MAIN> "dye_name" { EndToken(); return TK_DYE_NAME; }
            <MAIN> "dye_id" { EndToken(); return TK_DYE_ID; }
            <MAIN> "dye_item_id" { EndToken(); return TK_DYE_ITEM_ID; }
            <MAIN> "dye_level" { EndToken(); return TK_DYE_LEVEL; }
            <MAIN> "str" { EndToken(); return TK_STR; }
            <MAIN> "con" { EndToken(); return TK_CON; }
            <MAIN> "dex" { EndToken(); return TK_DEX; }
            <MAIN> "int" { EndToken(); return TK_INT_KW; }
            <MAIN> "men" { EndToken(); return TK_MEN; }
            <MAIN> "wit" { EndToken(); return TK_WIT; }
            <MAIN> "luc" { EndToken(); return TK_LUC; }
            <MAIN> "cha" { EndToken(); return TK_CHA; }
            <MAIN> "need_count" { EndToken(); return TK_NEED_COUNT; }
            <MAIN> "wear_fee" { EndToken(); return TK_WEAR_FEE; }
            <MAIN> "cancel_count" { EndToken(); return TK_CANCEL_COUNT; }
            <MAIN> "cancel_fee" { EndToken(); return TK_CANCEL_FEE; }
            <MAIN> "wear_class" { EndToken(); return TK_WEAR_CLASS; }
            <MAIN> "dye_skill" { EndToken(); return TK_DYE_SKILL; }
            <MAIN> "dye_premium" { EndToken(); return TK_DYE_PREMIUM; }
            <MAIN> "dye_period" { EndToken(); return TK_DYE_PERIOD; }
            <MAIN> "dye_unequip_skill" { EndToken(); return TK_DYE_UNEQUIP_SKILL; }
            <MAIN> "dye_cond" { EndToken(); return TK_DYE_COND; }
            <MAIN> "hc_is_in_battle_field" { EndToken(); return TK_HC_IS_IN_BATTLE_FIELD; }
            <MAIN> "hc_is_in_castle_war" { EndToken(); return TK_HC_IS_IN_CASTLE_WAR; }
            <MAIN> "hc_is_in_agit_war" { EndToken(); return TK_HC_IS_IN_AGIT_WAR; }
            <MAIN> "hc_is_in_fortress_siege" { EndToken(); return TK_HC_IS_IN_FORTRESS_SIEGE; }
            <MAIN> "hc_is_in_olympiad" { EndToken(); return TK_HC_IS_IN_OLYMPIAD; }
            <MAIN> "hc_is_in_pledge_game" { EndToken(); return TK_HC_IS_IN_PLEDGE_GAME; }
            <MAIN> "dye_end" { EndToken(); return TK_DYE_END; }
            <MAIN> "=" { EndToken(); return TK_EQUAL; }
            <MAIN> "-"? [0-9]+ {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            <MAIN> "$" ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext + 1, static_cast<std::size_t>(yyleng - 1)))
                );
                return TK_INTEGER;
            }
            <MAIN> [&/;{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                const int destinationLength = yyleng - 1;
                auto* const text =
                    static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(destinationLength) * sizeof(char16_t)));
                if (text != nullptr) {
                    std::copy_n(yytext + 1, yyleng - 2, text);
                    text[yyleng - 2] = u'\0';
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            <MAIN> [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                *lval =
                    grammar::PointerSemantic(DuplicateText(yytext, yyleng));
                return TK_IDENTIFIER;
            }
            <MAIN> [ \t\r] { EndToken(); continue; }
            <MAIN> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <MAIN> . {
                EndToken();
                // Log.Add(
                //     LOG_ERROR,
                //     L"[LEXER_ERROR] Invalid char[%c]",
                //     static_cast<wchar_t>(*yytext))
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char[%c]",
                //     static_cast<wchar_t>(*yytext))
                L2_LOG_ERROR(
                    Log, "Unexpected input '%s' (first UTF-16 code unit 0x%04x) at script line %lld; scanning continues.",
                    Utf16ToUtf8(std::u16string_view(yytext, static_cast<std::size_t>(yyleng))).c_str(),
                    static_cast<unsigned>(static_cast<std::uint16_t>(*yytext)), static_cast<long long>(line) + 1
                );
                continue;
            }
            <INITIAL> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}
