// expdata.txt scanner (original L2Server/lexer_expdata.l).
// re2c generates LexerExpData.cpp.

#include "L2Server/src/LexerExpData.h"

#include "L2Server/src/ParserExpData.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserExpDataTokens.h"

namespace {

// Current L2Server.exe 0x013af8c8..0x013afeea. The six-entry match
// image is the complete portion addressed by the generated states. The
// data immediately following it belongs to the exception directory.

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

double ParseWindowsDouble(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    if (ascii.empty() && !value.empty()) {
        return 0.0;
    }
    char* end = nullptr;
    const double parsed = std::strtod(ascii.c_str(), &end);
    return end == ascii.c_str() ? 0.0 : parsed;
}

} // namespace

LexerForExp::LexerForExp() :
    m_FileBuf(nullptr), m_RawFileBuf(nullptr), m_FilePointer(0), m_FileSize(0), comment_count(0), line(0) {
}

LexerForExp::~LexerForExp() {
    if (m_RawFileBuf != nullptr) {
        std::free(m_RawFileBuf);
        m_RawFileBuf = nullptr;
    }
}

int LexerForExp::Create(char16_t* fileBuffer, int fileSize) {
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

int LexerForExp::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_FileBuf one code unit at a time.
        SetInput(
            m_FileBuf + m_FilePointer,
            m_FilePointer < m_FileSize ? static_cast<std::size_t>(m_FileSize - m_FilePointer) : 0
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
            <MAIN> "exp_table_begin" { EndToken(); return TK_EXP_TABLE_BEGIN; }
            <MAIN> "exp_table_end" { EndToken(); return TK_EXP_TABLE_END; }
            <MAIN> "level" { EndToken(); return TK_LEVEL; }
            <MAIN> "exp" { EndToken(); return TK_EXP; }
            <MAIN> "training_rate" { EndToken(); return TK_TRAINING_RATE; }
            <INITIAL,COMMENT,MAIN,MAIN2> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            // The original lex scanner trimmed this trailing context back to the
            // longest float. A float followed by "." or by the end of the input
            // keeps its last digit there, so these two rules do the same.
            <INITIAL,COMMENT,MAIN,MAIN2> "-"? [0-9]+ "." ([0-9]+ | [0-9]* [eE] [+-]? [0-9][0-9]+) / "." {
                EndToken();
                const double parsed = ParseWindowsDouble(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                std::memcpy(semanticSlot, &parsed, sizeof(parsed));
                return TK_FLOAT_NUMBER;
            }
            <INITIAL,COMMENT,MAIN,MAIN2> "-"? [0-9]+ "." [0-9]* ([eE] [+-]? [0-9]+)? / [^.] {
                if (limit_ - cursor_ == 1 && *cursor_ >= u'0' && *cursor_ <= u'9') {
                    ++cursor_;
                }
                EndToken();
                const double parsed = ParseWindowsDouble(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                std::memcpy(semanticSlot, &parsed, sizeof(parsed));
                return TK_FLOAT_NUMBER;
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
