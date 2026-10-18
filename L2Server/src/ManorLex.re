// manordata.txt scanner (original L2Server/manor_lex.l).
// re2c generates ManorLex.cpp.

#include "L2Server/src/ManorLex.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ManorParser.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ManorParserTokens.h"

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

char16_t* DuplicateInnerText(const char16_t* const text, const int length) {
    const int destinationLength = length - 1;
    auto* const result = static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(destinationLength) * sizeof(char16_t)));
    if (result == nullptr) {
        return nullptr;
    }
    std::copy_n(text + 1, length - 2, result);
    result[length - 2] = u'\0';
    return result;
}

char16_t* DuplicateText(const char16_t* const text, const int length) {
    auto* const result = static_cast<char16_t*>(std::malloc((static_cast<std::size_t>(length) + 1) * sizeof(char16_t)));
    if (result == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, result);
    result[length] = u'\0';
    return result;
}

} // namespace

CLexerForManor::CLexerForManor() :
    m_FileBuf(nullptr), m_pRawFileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), m_nLastInput(0), line(0) {
}

CLexerForManor::~CLexerForManor() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
}

int CLexerForManor::Create(char16_t* const fileBuffer, const int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

int CLexerForManor::Lex(grammar::SemanticBits* lval) {
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
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "manor_begin" { EndToken(); return TK_MANOR_BEGIN; }
            "manor_id" { EndToken(); return TK_MANOR_ID; }
            "residence_id" { EndToken(); return TK_RESIDENCE_ID; }
            "manor_name" { EndToken(); return TK_MANOR_NAME; }
            "crop_list" { EndToken(); return TK_CROP_LIST; }
            "procure_list" { EndToken(); return TK_PROCURE_LIST; }
            "manor_end" { EndToken(); return TK_MANOR_END; }
            "=" { EndToken(); return TK_EQUAL; }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(ParseWindowsInt64(std::u16string_view(yytext, yyleng)));
                return TK_INTEGER;
            }
            "$" ("0" | [1-9][0-9]*) {
                EndToken();
                *lval =
                    static_cast<grammar::SemanticBits>(ParseWindowsInt64(std::u16string_view(yytext + 1, yyleng - 1)));
                return TK_INTEGER;
            }
            [;{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                *lval =
                    grammar::PointerSemantic(DuplicateText(yytext, yyleng));
                return TK_IDENTIFIER;
            }
            [ \t\r] { EndToken(); continue; }
            "\n" {
                EndToken();
                ++line;
                continue;
            }
            . {
                EndToken();
                // const std::wstring invalid = target_utf16::ToHostCodeUnits(yytext)
                const std::string invalid = Utf16ToUtf8(yytext);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
                L2_LOG_ERROR(
                    Log, "manordata.txt line %lld: invalid token '%s'; the token is skipped.",
                    static_cast<long long>(line) + 1, invalid.c_str()
                );
                continue;
            }
            $ { return 0; }
        */
    }
}
