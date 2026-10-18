// categorydata.txt scanner (original Shared/lexer_category.l).
// re2c generates LexerCategory.cpp.

#include "Shared/src/LexerCategory.h"

#include "Shared/src/ParserCategory.h"
#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cwctype>
#include <limits>
#include <string>
#include <string_view>

#include "ParserCategoryTokens.h"

namespace {

std::int32_t Msvc2010Wcstol(const char16_t* value) {
    // Current L2NPC.exe 0x004010fc / Cached.exe 0x00401034 call the VS2010
    // 32-bit-long wcstoxl entry on the lexer's null-terminated buffer and
    // discard only the returned end pointer. Do not snapshot through yyleng
    // or clear errno on success.
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

} // namespace

CLexerForCategory::CLexerForCategory() = default;

CLexerForCategory::~CLexerForCategory() {
    // Current L2NPC.exe 0x004010c8 / Cached.exe 0x00401000 free this
    // separately owned raw image.
    std::free(m_pRawFileBuf);
}

int CLexerForCategory::Create(CParserForCategory* parser, char16_t* text, int codeUnitCount) {
    // Current L2NPC.exe 0x004012b0 / Cached.exe 0x004011e8 store these fields.
    m_nFilePointer = 0;
    m_FileBuf = text;
    m_nFileSize = codeUnitCount;
    m_bOnlyPrecompiledHeader = parser->m_bOnlyPrecompiledHeader;
    return 1;
}

int CLexerForCategory::getLine() const noexcept { return line; }

int CLexerForCategory::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_FileBuf one code unit at a time.
        SetInput(
            m_FileBuf + m_nFilePointer,
            m_nFilePointer < m_nFileSize ? static_cast<std::size_t>(m_nFileSize - m_nFilePointer) : 0
        );
    }
    std::uint64_t& semantic = *lval;
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "category_define_begin" { EndToken(); return TK_CATEGORY_DEFINE_BEGIN; }
            "category_define_end" { EndToken(); return TK_CATEGORY_DEFINE_END; }
            "category" { EndToken(); return TK_CATEGORY; }
            "name" { EndToken(); return TK_NAME; }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                semantic = static_cast<std::uint64_t>(static_cast<std::int64_t>(Msvc2010Wcstol(yytext)));
                return TK_INTEGER;
            }
            "@" [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                const std::int64_t value = g_PrecompiledHeaderDB.Search(yytext + 1);
                if (value == -1 && !m_bOnlyPrecompiledHeader) {
                    const std::wstring printable = Utf16ToWide(yytext);
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Undefined word[%s] in precompiled header",
                    //     printable.c_str())
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[categories] Undefined precompiled name \"%ls\" in categorydata.txt at line %d. Using ID -1.",
                    //     printable.c_str(),
                    //     static_cast<std::int32_t>(static_cast<std::uint32_t>(line) + 1U))
                    L2_LOG_ERROR(
                        Log, L"[categories] Undefined precompiled name \"%ls\" in categorydata.txt at line %d; using ID -1.",
                        printable.c_str(), static_cast<std::int32_t>(static_cast<std::uint32_t>(line) + 1U)
                    );
                }
                semantic = static_cast<std::uint64_t>(value);
                return TK_INTEGER;
            }
            "[" [^[\]]* "]" {
                EndToken();
                const std::int32_t sourceLength = yyleng;
                const std::int32_t destinationWords32 =
                    static_cast<std::int32_t>(static_cast<std::uint32_t>(sourceLength) - 1U);
                const std::size_t destinationWords = static_cast<std::size_t>(static_cast<std::int64_t>(destinationWords32));
                char16_t* const text = static_cast<char16_t*>(std::malloc(destinationWords * sizeof(char16_t)));
                if (text) {
                    std::copy(yytext + 1, yytext + static_cast<std::ptrdiff_t>(sourceLength - 1), text);
                    text[static_cast<std::ptrdiff_t>(sourceLength) - 2] = u'\0';
                    semantic = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(text));
                }
                return TK_BRACKETED_NAME;
            }
            [%(),;=[\]{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            [ \t\r] { EndToken(); continue; }
            "\n" {
                EndToken();
                line = static_cast<std::int32_t>(static_cast<std::uint32_t>(line) + 1U);
                continue;
            }
            . {
                EndToken();
                const std::wstring printable = Utf16ToWide(yytext);
                const std::int32_t displayLine = static_cast<std::int32_t>(static_cast<std::uint32_t>(line) + 1U);
                // Log.Add(
                //     LOG_ERROR,
                //     L"invalid char [%s] at line [%d]",
                //     printable.c_str(),
                //     displayLine)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[categories] Invalid character \"%ls\" in categorydata.txt at line %d. Character skipped.",
                //     printable.c_str(), displayLine)
                L2_LOG_ERROR(
                    Log, L"[categories] Invalid character \"%ls\" in categorydata.txt at line %d; character skipped.",
                    printable.c_str(), displayLine
                );
                continue;
            }
            $ { return 0; }
        */
    }
}
