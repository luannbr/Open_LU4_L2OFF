// freewayInfo.txt scanner (original L2NPC/lexer_freeway.l).
// re2c generates LexerFreeway.cpp.

#include "L2NPC/src/LexerFreeway.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <istream>
#include <string>
#include <string_view>

#include "ParserFreewayTokens.h"

namespace {

// lexer_freeway.l::yygetchar has its own process-static first-stream flag.
bool g_FirstFreewayInput = true;

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

CLexerForFreeway::CLexerForFreeway() = default;

CLexerForFreeway::~CLexerForFreeway() = default;

int CLexerForFreeway::create() { return 1; }

int CLexerForFreeway::yygetchar() {
    // Current 0x00549210 reads one UTF-16LE code unit from two raw bytes.
    if (!yyin) {
        return -1;
    }
    if (g_FirstFreewayInput) {
        yyin->get();
        yyin->get();
        g_FirstFreewayInput = false;
    }

    const int low = yyin->get();
    if (yyin->eof()) {
        return -1;
    }
    const int high = yyin->get();
    if (yyin->eof()) {
        return -1;
    }
    return static_cast<int>(
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8U) | static_cast<std::uint16_t>(low))
    );
}

int CLexerForFreeway::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        SetReader([this] { return yygetchar(); });
    }
    std::uint64_t& semantic = *lval;
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "freeway_begin" { EndToken(); return TK_FREEWAY_BEGIN; }
            "freeway_end" { EndToken(); return TK_FREEWAY_END; }
            "freeway_id" { EndToken(); return TK_FREEWAY_ID; }
            "freeway_name" { EndToken(); return TK_FREEWAY_NAME; }
            "node_begin" { EndToken(); return TK_NODE_BEGIN; }
            "node_end" { EndToken(); return TK_NODE_END; }
            "node_id" { EndToken(); return TK_NODE_ID; }
            "pos" { EndToken(); return TK_POS; }
            "delay" { EndToken(); return TK_DELAY; }
            "social" { EndToken(); return TK_SOCIAL; }
            "state" { EndToken(); return TK_STATE; }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                const std::u16string_view match(yytext, static_cast<std::size_t>(yyleng));
                semantic = static_cast<std::uint64_t>(ParseWindowsInt64(match));
                return TK_INTEGER;
            }
            [,:;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            "[" [^[\]]* "]" {
                EndToken();
                const std::u16string_view match(yytext, static_cast<std::size_t>(yyleng));
                const std::u16string_view name = match.substr(1, match.size() - 2);
                char16_t* const text = static_cast<char16_t*>(std::malloc((name.size() + 1) * sizeof(char16_t)));
                if (text) {
                    std::copy(name.begin(), name.end(), text);
                    text[name.size()] = u'\0';
                    semantic = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(text));
                }
                return TK_NAME;
            }
            [ \t\r] { EndToken(); continue; }
            "\n" {
                EndToken();
                ++line;
                continue;
            }
            . { EndToken(); continue; }
            $ { return 0; }
        */
    }
}
