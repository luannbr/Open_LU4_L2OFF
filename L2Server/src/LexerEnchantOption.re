// CLexerForEnchantOption scanner (original L2Server/lexer_enchantoption.l).
// re2c generates LexerEnchantOption.cpp.

#include "L2Server/src/LexerEnchantOption.h"

#include "L2Server/src/ParserEnchantOption.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ParserEnchantOptionTokens.h"

namespace {

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

CLexerForEnchantOption::CLexerForEnchantOption() :
    m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), comment_count(0), line(0) {
}

CLexerForEnchantOption::~CLexerForEnchantOption() { m_FileBuf = nullptr; }

int CLexerForEnchantOption::Create(char16_t* fileBuffer, int fileSize) {
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

int CLexerForEnchantOption::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "enchant_option_begin" { EndToken(); return TK_ENCHANT_OPTION_BEGIN; }
            <MAIN> "enchant_option_end" { EndToken(); return TK_ENCHANT_OPTION_END; }
            <MAIN> "item" { EndToken(); return TK_ITEM; }
            <MAIN> "option_begin" { EndToken(); return TK_OPTION_BEGIN; }
            <MAIN> "option_end" { EndToken(); return TK_OPTION_END; }
            <MAIN> "enchant" { EndToken(); return TK_ENCHANT; }
            <MAIN> "option" { EndToken(); return TK_OPTION; }
            <MAIN> "skill" { EndToken(); return TK_SKILL; }
            <MAIN> "enchant_max_level" { EndToken(); return TK_ENCHANT_MAX_LEVEL; }
            <MAIN> "enchant_success_rate" { EndToken(); return TK_ENCHANT_SUCCESS_RATE; }
            <INITIAL,COMMENT,MAIN> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            <INITIAL,MAIN> [%,:;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <INITIAL,COMMENT,MAIN> "[" [^[\]]* "]" {
                EndToken();
                const int destinationLength = yyleng - 1;
                char16_t* const text =
                    static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(destinationLength) * sizeof(char16_t)));
                if (text != nullptr) {
                    std::copy_n(yytext + 1, yyleng - 2, text);
                    text[yyleng - 2] = u'\0';
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            <INITIAL,MAIN> [ \t\r] { EndToken(); continue; }
            <INITIAL,MAIN> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <INITIAL,MAIN> . { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}
