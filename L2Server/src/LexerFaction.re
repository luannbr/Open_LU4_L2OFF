// factiondata.txt scanner.
// re2c generates LexerFaction.cpp.

#include "L2Server/src/LexerFaction.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ParserFactionTokens.h"

namespace {

// Current L2Server.exe 0x01045e00..0x01046558 and 0x013662f8.

std::int64_t ParseWindowsInt64(std::u16string_view value) {
    std::string ascii;
    ascii.reserve(value.size());
    for (const char16_t codeUnit : value) {
        if (codeUnit > 0x7f) {
            return 0;
        }
        ascii.push_back(static_cast<char>(codeUnit));
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

} // namespace

CLexerForFaction::CLexerForFaction() :
    m_inputBuffer(nullptr), m_inputBufferOffset(0), m_inputBufferLength(0), comment_count(0), line(0) {
}

CLexerForFaction::~CLexerForFaction() = default;

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN};

} // namespace

int CLexerForFaction::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_inputBuffer one code unit at a time.
        const std::size_t size = m_inputBufferOffset < m_inputBufferLength
            ? static_cast<std::size_t>(m_inputBufferLength - m_inputBufferOffset)
            : 0;
        SetInput(m_inputBuffer + m_inputBufferOffset, size);
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
            <MAIN> "faction_level_cap" { EndToken(); return TK_FACTION_LEVEL_CAP; }
            <MAIN> "faction_begin" { EndToken(); return TK_FACTION_BEGIN; }
            <MAIN> "faction_end" { EndToken(); return TK_FACTION_END; }
            <MAIN> "faction_id" { EndToken(); return TK_FACTION_ID; }
            <MAIN> "faction_name" { EndToken(); return TK_FACTION_NAME; }
            <MAIN> "faction_name_k" { EndToken(); return TK_FACTION_NAME_K; }
            <MAIN> "faction_max_level" { EndToken(); return TK_FACTION_MAX_LEVEL; }
            <MAIN> "faction_table_begin" { EndToken(); return TK_FACTION_TABLE_BEGIN; }
            <MAIN> "faction_table_end" { EndToken(); return TK_FACTION_TABLE_END; }
            <MAIN> "faction_level" { EndToken(); return TK_FACTION_LEVEL; }
            <MAIN> "faction_point" { EndToken(); return TK_FACTION_POINT; }
            <MAIN> "monster_book_reward_lv" { EndToken(); return TK_MONSTER_BOOK_REWARD_LV; }
            <MAIN> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                const std::int64_t parsed = ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                *lval = static_cast<std::uint32_t>(parsed);
                // Noise: successful numeric/separator values are already consumed by the parser.
                // std::wprintf(L"int %d\n", static_cast<int>(parsed))
                return TK_INTEGER;
            }
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
                // Noise: echoing the parsed string adds no validation or outcome.
                // const auto* const value = reinterpret_cast<const char16_t*>(
                //     static_cast<std::uintptr_t>(*lval))
                // const std::wstring hostValue = target_utf16::ToHostCodeUnits(value)
                // std::wprintf(L"value %ls\n", hostValue.c_str())
                return TK_NAME;
            }
            <MAIN> [,:;=[\]{}~] {
                EndToken();
                // const auto separator = static_cast<std::uint16_t>(*yytext)
                // Noise: successful numeric/separator values are already consumed by the parser.
                // std::wprintf(L"sep %lc\n", static_cast<wint_t>(separator))
                return grammar::CharacterToken(yytext[0]);
            }
            <MAIN> [ \t\r] { EndToken(); continue; }
            <MAIN> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <INITIAL> * { EndToken(); continue; }
            <MAIN> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}
