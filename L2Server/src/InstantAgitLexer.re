// CLexerForInstantAgit scanner (original L2Server/instantagit_lexer.l).
// re2c generates InstantAgitLexer.cpp.

#include "L2Server/src/InstantAgitLexer.h"

#include "L2Server/src/InstantAgitParser.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "InstantAgitParserTokens.h"

namespace {

std::string NarrowAscii(std::u16string_view value) {
    std::string result;
    result.reserve(value.size());
    for (char16_t codeUnit : value) {
        if (codeUnit > 0x7f) {
            break;
        }
        result.push_back(static_cast<char>(codeUnit));
    }
    return result;
}

std::int64_t ParseWindowsInt64(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
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

double ParseWindowsDouble(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    char* end = nullptr;
    return std::strtod(ascii.c_str(), &end);
}

char16_t* DuplicateQuoted(const char16_t* text, int length) {
    if (text == nullptr || length < 2) {
        return nullptr;
    }
    const int destinationLength = length - 1;
    auto* result = static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(destinationLength) * sizeof(char16_t)));
    if (result != nullptr) {
        std::copy_n(text + 1, length - 2, result);
        result[length - 2] = u'\0';
    }
    return result;
}

} // namespace

CLexerForInstantAgit::CLexerForInstantAgit() :
    m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), comment_count(0), line(0) {
}

CLexerForInstantAgit::~CLexerForInstantAgit() { m_FileBuf = nullptr; }

int CLexerForInstantAgit::Create(char16_t* fileBuffer, int fileSize) {
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

int CLexerForInstantAgit::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_FileBuf one code unit at a time.
        SetInput(
            m_FileBuf + m_nFilePointer,
            m_nFilePointer < m_nFileSize ? static_cast<std::size_t>(m_nFileSize - m_nFilePointer) : 0
        );
    }
    auto* const semanticSlot = lval;
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
            <MAIN> "instant_agit_begin" { EndToken(); return TK_INSTANT_AGIT_BEGIN; }
            <MAIN> "instant_agit_end" { EndToken(); return TK_INSTANT_AGIT_END; }
            <MAIN> "id" { EndToken(); return TK_ID; }
            <MAIN> "rental_fee" { EndToken(); return TK_RENTAL_FEE; }
            <MAIN> "commission" { EndToken(); return TK_COMMISSION; }
            <MAIN> "rental_period" { EndToken(); return TK_RENTAL_PERIOD; }
            <MAIN> "apply_period" { EndToken(); return TK_APPLY_PERIOD; }
            <MAIN> "first_lottery_date" { EndToken(); return TK_FIRST_LOTTERY_DATE; }
            <MAIN> "instant_zone" { EndToken(); return TK_INSTANT_ZONE; }
            <MAIN> "min_pledge_level" { EndToken(); return TK_MIN_PLEDGE_LEVEL; }
            <MAIN> "origin" { EndToken(); return TK_ORIGIN; }
            <MAIN> "residence_grade" { EndToken(); return TK_RESIDENCE_GRADE; }
            <MAIN> "deco_npc_slots" { EndToken(); return TK_DECO_NPC_SLOTS; }
            <MAIN> "day" { EndToken(); return TK_DAY; }
            <MAIN> "hour" { EndToken(); return TK_HOUR; }
            <MAIN> [0-9]{4} {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            <MAIN> [0-9]{2} {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            <MAIN> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            // The original lex scanner trimmed this trailing context back to the
            // longest float. A float followed by "." or by the end of the input
            // keeps its last digit there, so these two rules do the same.
            <MAIN> "-"? [0-9]+ "." ([0-9]+ | [0-9]* [eE] [+-]? [0-9][0-9]+) / "." {
                EndToken();
                const double value = ParseWindowsDouble(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                std::memcpy(semanticSlot, &value, sizeof(value));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> "-"? [0-9]+ "." [0-9]* ([eE] [+-]? [0-9]+)? / [^.] {
                if (limit_ - cursor_ == 1 && *cursor_ >= u'0' && *cursor_ <= u'9') {
                    ++cursor_;
                }
                EndToken();
                const double value = ParseWindowsDouble(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                std::memcpy(semanticSlot, &value, sizeof(value));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> [%,/:;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                *semanticSlot =
                    grammar::PointerSemantic(DuplicateQuoted(yytext, yyleng));
                return TK_NAME;
            }
            <MAIN> "\"" [^"]* "\"" {
                EndToken();
                *semanticSlot =
                    grammar::PointerSemantic(DuplicateQuoted(yytext, yyleng));
                return TK_STRING;
            }
            <INITIAL,MAIN> [ \t\r] { EndToken(); continue; }
            <INITIAL,MAIN> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <MAIN> . { EndToken(); continue; }
            <INITIAL> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}
