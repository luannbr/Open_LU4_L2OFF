// CLexerForUpgradeSystem scanner.
// re2c generates LexerUpgradeSystem.cpp.

#include "L2Server/src/LexerUpgradeSystem.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserUpgradeSystem.h"

#include "L2Server/src/Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ParserUpgradeSystemTokens.h"

namespace {

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

std::int64_t ParseWindowsInt64(std::u16string_view value) {
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

CLexerForUpgradeSystem::CLexerForUpgradeSystem() :
    m_FileBuf(nullptr),
    m_pRawFileBuf(nullptr),
    m_nFilePointer(0),
    m_nFileSize(0),
    line(0),
    comment_count(0),
    m_bOnlyPrecompiledHeader(false) {
}

CLexerForUpgradeSystem::~CLexerForUpgradeSystem() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
}

int CLexerForUpgradeSystem::Create(char16_t* fileBuffer, int fileSize) {
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

int CLexerForUpgradeSystem::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "upgradesystem_begin" { EndToken(); return TK_UPGRADESYSTEM_BEGIN; }
            <MAIN> "upgradesystem_end" { EndToken(); return TK_UPGRADESYSTEM_END; }
            <MAIN> "upgrade_id" { EndToken(); return TK_UPGRADE_ID; }
            <MAIN> "upgrade_item" { EndToken(); return TK_UPGRADE_ITEM; }
            <MAIN> "material_items" { EndToken(); return TK_MATERIAL_ITEMS; }
            <MAIN> "commission" { EndToken(); return TK_COMMISSION; }
            <MAIN> "result_item" { EndToken(); return TK_RESULT_ITEM; }
            <MAIN> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                L2_GUARD_FUNCTION();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            <INITIAL,COMMENT,MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const name = DuplicateInnerText(yytext, yyleng);
                if (name == nullptr) {
                    *lval = 0;
                    return TK_INTEGER;
                }
                const std::int64_t value = g_PrecompiledHeaderDB.Search(name);
                if (value == -1 && !m_bOnlyPrecompiledHeader) {
                    // const std::wstring hostText = target_utf16::ToHostCodeUnits(yytext)
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Undefined word[%s] in precompiled header",
                    //     hostText.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"Undefined word[%s] in precompiled header",
                    //     hostText.c_str())
                    L2_LOG_ERROR(
                        Log, "Undefined precompiled-header name '%s' at script line %lld; passing -1 to the parser.",
                        Utf16ToUtf8(std::u16string_view(name)).c_str(), static_cast<long long>(line) + 1
                    );
                }
                *lval = static_cast<grammar::SemanticBits>(value);
                std::free(name);
                return TK_INTEGER;
            }
            <MAIN> "$" ("0" | [1-9][0-9]*) {
                EndToken();
                L2_GUARD_FUNCTION();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext + 1, static_cast<std::size_t>(yyleng - 1)))
                );
                return TK_INTEGER;
            }
            <MAIN> [,;=[\]{}~] {
                EndToken();
                L2_GUARD_FUNCTION();
                return grammar::CharacterToken(yytext[0]);
            }
            <MAIN> [ \t\r] { EndToken(); continue; }
            <MAIN> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <MAIN> . {
                EndToken();
                // const std::wstring invalid = target_utf16::ToHostCodeUnits(yytext)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
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
