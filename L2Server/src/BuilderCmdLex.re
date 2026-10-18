// CLexerForBuilderCmd scanner (original L2Server/buildercmd_lex.l).
// re2c generates BuilderCmdLex.cpp.

#include "L2Server/src/BuilderCmdLex.h"

#include "L2Server/src/BuilderCmdParser.h"

#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>

#include "BuilderCmdParserTokens.h"

namespace {

} // namespace

CLexerForBuilderCmd::CLexerForBuilderCmd() {
    line = 0;
}

CLexerForBuilderCmd::~CLexerForBuilderCmd() = default;

int CLexerForBuilderCmd::Create() { return 1; }

int CLexerForBuilderCmd::yygetchar() { return ReadStream(); }

int CLexerForBuilderCmd::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        SetReader([this] { return yygetchar(); });
    }
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "command_begin" { EndToken(); return TK_COMMAND_BEGIN; }
            "command_end" { EndToken(); return TK_COMMAND_END; }
            "command" { EndToken(); return TK_COMMAND; }
            "alias" { EndToken(); return TK_ALIAS; }
            [,;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            "[" [^[\]]* "]" {
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
            [ \t\r] { EndToken(); continue; }
            "\n" {
                EndToken();
                ++line;
                continue;
            }
            . {
                EndToken();
                // const std::wstring invalid = Utf16ToWide(yytext)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[LEXER_ERROR] Invalid char = %ls",
                //     invalid.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char = %ls",
                //     invalid.c_str())
                L2_LOG_ERROR(
                    Log, "Unexpected input '%s' (first UTF-16 code unit 0x%04x) at script line %lld; scanning continues.",
                    Utf16ToUtf8(std::u16string_view(yytext, static_cast<std::size_t>(yyleng))).c_str(),
                    static_cast<unsigned>(static_cast<std::uint16_t>(*yytext)), static_cast<long long>(line) + 1
                );
                continue;
            }
            $ { return 0; }
        */
    }
}
