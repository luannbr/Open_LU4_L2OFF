// FormationInfo.txt scanner (original L2NPC/lexer_formation.l).
// re2c generates LexerFormation.cpp.

#include "L2NPC/src/LexerFormation.h"

#include "ParserFormationTokens.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <istream>
#include <string>

namespace {

// lexer_formation.l::yygetchar keeps this as process state and consumes
// exactly two leading bytes only for the first formation stream.
bool g_FirstFormationInput = true;

std::int64_t ParseWindowsInt64(const char16_t* value) {
    // The current lexer calls _wcstoi64(yytext, nullptr, 10) directly.
    // wchar_t is 16-bit in the target, so widen the same null-terminated
    // code units only at the host CRT boundary.
    std::wstring widened;
    while (*value != u'\0') {
        widened.push_back(static_cast<wchar_t>(*value++));
    }
    return static_cast<std::int64_t>(std::wcstoll(widened.c_str(), nullptr, 10));
}

void CopyTargetWideN(char16_t* destination, const char16_t* source, std::size_t count) {
    // Valid generated string tokens call wcsncpy_s with count exactly one
    // below the destination extent. Preserve its stop-at-NUL copy without
    // adding a portable bounds/fallback branch absent from the lexer.
    std::size_t copied = 0;
    while (copied < count && source[copied] != u'\0') {
        destination[copied] = source[copied];
        ++copied;
    }
    destination[copied] = u'\0';
}

void IncrementTargetInt32(int& value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    ++bits;
    std::memcpy(&value, &bits, sizeof(value));
}

} // namespace

CLexerForFormation::CLexerForFormation() = default;

CLexerForFormation::~CLexerForFormation() = default;

int CLexerForFormation::create() { return 1; }

int CLexerForFormation::yygetchar() {
    // Current 0x00548b28 reads a UTF-16LE code unit as two raw bytes.
    if (g_FirstFormationInput) {
        yyin->get();
        yyin->get();
        g_FirstFormationInput = false;
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

int CLexerForFormation::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read the stream one code unit at a time.
        SetReader([this] { return yygetchar(); });
    }
    std::uint64_t& semantic = *lval;
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "formation_begin" { EndToken(); return TK_FORMATION_BEGIN; }
            "formation_end" { EndToken(); return TK_FORMATION_END; }
            "formation_id" { EndToken(); return TK_FORMATION_ID; }
            "formation_name" { EndToken(); return TK_FORMATION_NAME; }
            "pos_begin" { EndToken(); return TK_POS_BEGIN; }
            "pos_end" { EndToken(); return TK_POS_END; }
            "pos_id" { EndToken(); return TK_POS_ID; }
            "pos_var" { EndToken(); return TK_POS_VAR; }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                semantic = static_cast<std::uint64_t>(ParseWindowsInt64(yytext));
                return TK_INTEGER;
            }
            [,:;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            "[" [^[\]]* "]" {
                EndToken();
                const int length = yyleng;
                const std::size_t sizeInWords = static_cast<std::size_t>(static_cast<std::int64_t>(length) - 1);
                char16_t* const text = static_cast<char16_t*>(std::malloc(sizeInWords * sizeof(char16_t)));
                if (text) {
                    CopyTargetWideN(text, yytext + 1, static_cast<std::size_t>(static_cast<std::int64_t>(length) - 2));
                    text[static_cast<std::ptrdiff_t>(yyleng) - 2] = u'\0';
                    semantic = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(text));
                }
                return TK_NAME;
            }
            [ \t\r] { EndToken(); continue; }
            "\n" { EndToken(); IncrementTargetInt32(line); continue; }
            . { EndToken(); continue; }
            $ { return 0; }
        */
    }
}
