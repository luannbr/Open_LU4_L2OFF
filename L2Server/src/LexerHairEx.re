// LexerForHairEx scanner.
// re2c generates LexerHairEx.cpp.

#include "L2Server/src/LexerHairEx.h"

#include "L2Server/src/ParserHairEx.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ParserHairExTokens.h"

namespace {

std::string NarrowAscii(std::u16string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char16_t codeUnit : value) {
        if (codeUnit > 0x7f) {
            return {};
        }
        result.push_back(static_cast<char>(codeUnit));
    }
    return result;
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

} // namespace

LexerForHairEx::LexerForHairEx() :
    m_FileBuf(nullptr), m_RawFileBuf(nullptr), m_FilePointer(0), m_FileSize(0), comment_count(0), line(0) {
}

LexerForHairEx::~LexerForHairEx() {
    if (m_RawFileBuf != nullptr) {
        std::free(m_RawFileBuf);
    }
}

int LexerForHairEx::Create(char16_t* fileBuffer, int fileSize) {
    m_FilePointer = 0;
    m_FileBuf = fileBuffer;
    m_FileSize = fileSize;
    return 1;
}

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN, yycMAIN2};

} // namespace

int LexerForHairEx::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_FileBuf one code unit at a time.
        SetInput(
            m_FileBuf + m_FilePointer,
            m_FilePointer < m_FileSize ? static_cast<std::size_t>(m_FileSize - m_FilePointer) : 0
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
            <MAIN> "HairEx_begin" { EndToken(); return TK_HAIREX_BEGIN; }
            <MAIN> "HairEx_end" { EndToken(); return TK_HAIREX_END; }
            <MAIN> "id" { EndToken(); return TK_ID; }
            <MAIN> "Mesh_price_adena" { EndToken(); return TK_MESH_PRICE_ADENA; }
            <MAIN> "Mesh_price_item" { EndToken(); return TK_MESH_PRICE_ITEM; }
            <MAIN> "Reset_price_adena" { EndToken(); return TK_RESET_PRICE_ADENA; }
            <MAIN> "limit" { EndToken(); return TK_LIMIT; }
            <MAIN> "enable" { EndToken(); return TK_ENABLE; }
            <INITIAL,COMMENT,MAIN,MAIN2> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            <INITIAL,MAIN,MAIN2> [%,:;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <INITIAL,MAIN,MAIN2> [ \t\r] { EndToken(); continue; }
            <INITIAL,MAIN,MAIN2> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <INITIAL,MAIN,MAIN2> . { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN,MAIN2> $ { return 0; }
        */
    }
}
