// CLexerForItemAttrChange scanner (original L2Server/itemattrchange_lexer.l).
// re2c generates ItemAttrChangeLexer.cpp.

#include "L2Server/src/ItemAttrChangeLexer.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ItemAttrChangeParser.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ItemAttrChangeParserTokens.h"

namespace {

// Current L2Server 0x015d4f68. The generated stream lexer consumes the
// UTF-16 byte-order mark only for the first ItemAttrChange stream opened in
// the process.
bool firstTime = true;

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

CLexerForItemAttrChange::CLexerForItemAttrChange() : line(0), comment_count(0) {
}

int CLexerForItemAttrChange::create() { return 1; }

int CLexerForItemAttrChange::yygetchar() {
    if (firstTime) {
        yyin->get();
        firstTime = false;
    }
    return ReadStream();
}

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN};

} // namespace

int CLexerForItemAttrChange::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        SetReader([this] { return yygetchar(); });
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
            <MAIN> "ItemAttrChange_begin" { EndToken(); return TK_ITEMATTRCHANGE_BEGIN; }
            <MAIN> "Itemlist" { EndToken(); return TK_ITEMLIST; }
            <MAIN> "ItemAttrChange_end" { EndToken(); return TK_ITEMATTRCHANGE_END; }
            <MAIN> "ia_fire" {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(0);
                return TK_IA_VALUE;
            }
            <MAIN> "ia_water" {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(1);
                return TK_IA_VALUE;
            }
            <MAIN> "ia_wind" {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(2);
                return TK_IA_VALUE;
            }
            <MAIN> "ia_earth" {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(3);
                return TK_IA_VALUE;
            }
            <MAIN> "ia_holy" {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(4);
                return TK_IA_VALUE;
            }
            <MAIN> "ia_unholy" {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(5);
                return TK_IA_VALUE;
            }
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
                char16_t* const text =
                    static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(destinationLength) * sizeof(char16_t)));
                if (text != nullptr) {
                    std::copy_n(yytext + 1, yyleng - 2, text);
                    text[yyleng - 2] = u'\0';
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
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
