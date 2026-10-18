// CLexerForDecoData scanner (original L2Server/decodata_lexer.l).
// re2c generates DecoDataLexer.cpp.

#include "L2Server/src/DecoDataLexer.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/DecoDataParser.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "DecoDataParserTokens.h"

namespace {

// Exact current L2Server.exe images at 0x00e22518, 0x012a7f20,
// 0x012a85a0, and 0x013662f8.

std::string NarrowAscii(std::u16string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char16_t unit : value) {
        if (unit > 0x7f) {
            break;
        }
        result.push_back(static_cast<char>(unit));
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

CLexerForDecoData::CLexerForDecoData() : m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0), comment_count(0) {
}

CLexerForDecoData::~CLexerForDecoData() { m_FileBuf = nullptr; }

int CLexerForDecoData::Create(char16_t* fileBuffer, int fileSize) {
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

int CLexerForDecoData::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "none" { EndToken(); return TK_NONE; }
            <MAIN> "deco_begin" { EndToken(); return TK_DECO_BEGIN; }
            <MAIN> "deco_end" { EndToken(); return TK_DECO_END; }
            <MAIN> "id" { EndToken(); return TK_ID; }
            <MAIN> "name" { EndToken(); return TK_NAME; }
            <MAIN> "type" { EndToken(); return TK_TYPE; }
            <MAIN> "level" { EndToken(); return TK_LEVEL; }
            <MAIN> "depth" { EndToken(); return TK_DEPTH; }
            <MAIN> "func" { EndToken(); return TK_FUNC; }
            <MAIN> "cost" { EndToken(); return TK_COST; }
            <MAIN> "hp_regen" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(1);
                return TK_HP_REGEN_GROUP;
            }
            <MAIN> "mp_regen" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(2);
                return TK_HP_REGEN_GROUP;
            }
            <MAIN> "teleport" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(3);
                return TK_HP_REGEN_GROUP;
            }
            <MAIN> "produce" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(4);
                return TK_HP_REGEN_GROUP;
            }
            <MAIN> "observe" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(5);
                return TK_HP_REGEN_GROUP;
            }
            <MAIN> "exp_restore" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(6);
                return TK_HP_REGEN_GROUP;
            }
            <MAIN> "buff" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(8);
                return TK_HP_REGEN_GROUP;
            }
            <MAIN> "vp_regen" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(7);
                return TK_HP_REGEN_GROUP;
            }
            <MAIN> "=" { EndToken(); return TK_EQUAL; }
            <MAIN> ":" { EndToken(); return TK_COLON; }
            <MAIN> "(" { EndToken(); return TK_LPAREN; }
            <MAIN> ")" { EndToken(); return TK_RPAREN; }
            <MAIN> "-"? [0-9]+ {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            <MAIN> "$" ("0" | [1-9][0-9]*) {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext + 1, static_cast<std::size_t>(yyleng - 1)))
                );
                return TK_INTEGER;
            }
            <MAIN> "-"{0,2} [0-9]+ "." [0-9]* {
                EndToken();
                const double value = ParseWindowsDouble(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                std::memcpy(semanticSlot, &value, sizeof(value));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> [&/;{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                *semanticSlot =
                    grammar::PointerSemantic(DuplicateQuoted(yytext, yyleng));
                return TK_NAME_2;
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
                //     static_cast<wint_t>(static_cast<std::uint16_t>(*yytext)))
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char[%c]",
                //     static_cast<wint_t>(static_cast<std::uint16_t>(*yytext)))
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
