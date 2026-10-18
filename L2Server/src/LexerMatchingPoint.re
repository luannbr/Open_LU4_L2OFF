// CLexerForMatchingPoint scanner.
// re2c generates LexerMatchingPoint.cpp.

#include "L2Server/src/LexerMatchingPoint.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserMatchingPointTokens.h"

namespace {

std::string NarrowAscii(const std::u16string_view value) {
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

std::int64_t ParseWindowsInt64(const std::u16string_view value) {
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

grammar::SemanticBits ParseWindowsDoubleBits(const std::u16string_view value) {
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
    std::memcpy(&bits, &parsed, sizeof(bits));
    return bits;
}

char16_t* DuplicateText(const char16_t* text, const int length) {
    auto* const duplicate = static_cast<char16_t*>(std::malloc((static_cast<std::size_t>(length) + 1) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::memcpy(duplicate, text, static_cast<std::size_t>(length) * sizeof(char16_t));
    duplicate[length] = u'\0';
    return duplicate;
}

} // namespace

CLexerForMatchingPoint::CLexerForMatchingPoint() : m_inputBuffer(nullptr), m_inputBufferOffset(0), m_inputBufferLength(0) {
}

CLexerForMatchingPoint::~CLexerForMatchingPoint() = default;

int CLexerForMatchingPoint::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_inputBuffer one code unit at a time.
        const std::size_t size = m_inputBufferOffset < m_inputBufferLength
            ? static_cast<std::size_t>(m_inputBufferLength - m_inputBufferOffset)
            : 0;
        SetInput(m_inputBuffer + m_inputBufferOffset, size);
    }
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "=" { EndToken(); return TK_EQUAL; }
            ";" { EndToken(); return TK_SEMICOLON; }
            "[" { EndToken(); return TK_LBRACKET; }
            "]" { EndToken(); return TK_RBRACKET; }
            [+\-|]? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            [+\-|]? ("0" | [1-9][0-9]*) "." [0-9]* {
                EndToken();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            [a-zA-Z][a-zA-Z0-9_\-]* {
                EndToken();
                *lval =
                    grammar::PointerSemantic(DuplicateText(yytext, yyleng));
                return TK_IDENTIFIER;
            }
            [ \t\n\r] { EndToken(); continue; }
            * { EndToken(); continue; }
            $ { return 0; }
        */
    }
}
