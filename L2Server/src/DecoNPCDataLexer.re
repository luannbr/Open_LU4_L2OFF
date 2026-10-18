// CLexerForDecoNpcData scanner.
// re2c generates DecoNPCDataLexer.cpp.

#include "L2Server/src/DecoNPCDataLexer.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/DecoNPCDataParser.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "DecoNPCDataParserTokens.h"

namespace {

// Exact current L2Server.exe images at 0x00e20318, 0x00e20340,
// 0x00e209d0, and 0x013662f8.

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

CLexerForDecoNpcData::CLexerForDecoNpcData() :
    m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0), comment_count(0) {
}

CLexerForDecoNpcData::~CLexerForDecoNpcData() { m_FileBuf = nullptr; }

int CLexerForDecoNpcData::Create(char16_t* fileBuffer, int fileSize) {
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

int CLexerForDecoNpcData::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "deco_npc_begin" { EndToken(); return TK_DECO_NPC_BEGIN; }
            <MAIN> "id" { EndToken(); return TK_ID; }
            <MAIN> "name" { EndToken(); return TK_NAME; }
            <MAIN> "type" { EndToken(); return TK_TYPE; }
            <MAIN> "sub_type" { EndToken(); return TK_SUB_TYPE; }
            <MAIN> "level" { EndToken(); return TK_LEVEL; }
            <MAIN> "period" { EndToken(); return TK_PERIOD; }
            <MAIN> "cost_adena" { EndToken(); return TK_COST_ADENA; }
            <MAIN> "cost_item" { EndToken(); return TK_COST_ITEM; }
            <MAIN> "npc_id" { EndToken(); return TK_NPC_ID; }
            <MAIN> "faction" { EndToken(); return TK_FACTION; }
            <MAIN> "residence_grades" { EndToken(); return TK_RESIDENCE_GRADES; }
            <MAIN> "residence_domains" { EndToken(); return TK_RESIDENCE_DOMAINS; }
            <MAIN> "changeable" { EndToken(); return TK_CHANGEABLE; }
            <MAIN> "deco_npc_end" { EndToken(); return TK_DECO_NPC_END; }
            <MAIN> "=" { EndToken(); return TK_EQUAL; }
            <MAIN> ";" { EndToken(); return TK_SEMICOLON; }
            <MAIN> "{" { EndToken(); return TK_LBRACE; }
            <MAIN> "}" { EndToken(); return TK_RBRACE; }
            <MAIN> "(" { EndToken(); return TK_LPAREN; }
            <MAIN> ")" { EndToken(); return TK_RPAREN; }
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
            <MAIN> [&/] { EndToken(); return grammar::CharacterToken(yytext[0]); }
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
