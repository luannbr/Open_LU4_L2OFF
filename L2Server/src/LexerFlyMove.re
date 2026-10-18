// CLexerForFlyMove scanner (original L2Server/lexer_flymove.l).
// re2c generates LexerFlyMove.cpp.

#include "L2Server/src/LexerFlyMove.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserFlyMove.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <limits>
#include <string>
#include <string_view>

#include "ParserFlyMoveTokens.h"

namespace {

// The generated stream lexer drops the UTF-16 BOM only on the first
// FlyMove stream opened in the process.
bool firstTime = true;

std::int32_t Msvc2010Wcstol(const char16_t* value) {
    while (std::iswspace(static_cast<std::wint_t>(*value)) != 0) {
        ++value;
    }

    bool negative = false;
    if (*value == u'-' || *value == u'+') {
        negative = *value == u'-';
        ++value;
    }

    const std::uint32_t limit = negative ? 0x80000000U : 0x7fffffffU;
    std::uint32_t result = 0;
    bool converted = false;
    bool overflow = false;
    while (*value >= u'0' && *value <= u'9') {
        converted = true;
        const std::uint32_t digit = static_cast<std::uint32_t>(*value - u'0');
        if (result > (limit - digit) / 10U) {
            overflow = true;
        } else if (!overflow) {
            result = result * 10U + digit;
        }
        ++value;
    }

    if (!converted) {
        return 0;
    }
    if (overflow) {
        errno = ERANGE;
        return negative ? std::numeric_limits<std::int32_t>::min() : std::numeric_limits<std::int32_t>::max();
    }
    if (!negative) {
        return static_cast<std::int32_t>(result);
    }
    if (result == 0x80000000U) {
        return std::numeric_limits<std::int32_t>::min();
    }
    return -static_cast<std::int32_t>(result);
}

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

grammar::SemanticBits ParseWindowsDoubleBits(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    if (ascii.empty() && !value.empty()) {
        return 0;
    }

    char* end = nullptr;
    const double parsed = std::strtod(ascii.c_str(), &end);
    if (end == ascii.c_str()) {
        return 0;
    }

    grammar::SemanticBits bits = 0;
    static_assert(sizeof(bits) == sizeof(parsed));
    std::memcpy(&bits, &parsed, sizeof(bits));
    return bits;
}

char16_t* DuplicateInnerText(const char16_t* text, int length) {
    const int destinationLength = length - 1;
    auto* const duplicate =
        static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(destinationLength) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }

    std::copy_n(text + 1, length - 2, duplicate);
    duplicate[length - 2] = u'\0';
    return duplicate;
}

} // namespace

CLexerForFlyMove::CLexerForFlyMove() : line(0) {
}

int CLexerForFlyMove::create() { return 1; }

int CLexerForFlyMove::yygetchar() {
    if (firstTime) {
        yyin->get();
        firstTime = false;
    }
    return ReadStream();
}

int CLexerForFlyMove::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        SetReader([this] { return yygetchar(); });
    }
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "flymove_data_begin" { EndToken(); return TK_FLYMOVE_DATA_BEGIN; }
            "flymove_data_end" { EndToken(); return TK_FLYMOVE_DATA_END; }
            "flymove_path_id" { EndToken(); return TK_FLYMOVE_PATH_ID; }
            "node_list_begin" { EndToken(); return TK_NODE_LIST_BEGIN; }
            "node_list_end" { EndToken(); return TK_NODE_LIST_END; }
            "id" { EndToken(); return TK_ID; }
            "pos" { EndToken(); return TK_POS; }
            "landing_pos" { EndToken(); return TK_LANDING_POS; }
            "related_node" { EndToken(); return TK_RELATED_NODE; }
            "flying_height" { EndToken(); return TK_FLYING_HEIGHT; }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(static_cast<std::int64_t>(Msvc2010Wcstol(yytext)));
                return TK_INTEGER;
            }
            // The original lex scanner trimmed this trailing context back to the
            // longest float. A float followed by "." or by the end of the input
            // keeps its last digit there, so these two rules do the same.
            "-"? [0-9]+ "." ([0-9]+ | [0-9]* [eE] [+-]? [0-9][0-9]+) / "." {
                EndToken();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            "-"? [0-9]+ "." [0-9]* ([eE] [+-]? [0-9]+)? / [^.] {
                if (limit_ - cursor_ == 1 && *cursor_ >= u'0' && *cursor_ <= u'9') {
                    ++cursor_;
                }
                EndToken();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            [%(),;=[\]{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            [ \t\r] { EndToken(); continue; }
            "\n" {
                EndToken();
                ++line;
                continue;
            }
            . {
                EndToken();
                // Log.Add(
                //     LOG_ERROR,
                //     L"invalid char [%c] at line [%d]",
                //     static_cast<wchar_t>(*yytext),
                //     line)
                // L2_LOG_ERROR(Log,
                //     L"invalid char [%c] at line [%d]",
                //     static_cast<wchar_t>(*yytext),
                //     line)
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
