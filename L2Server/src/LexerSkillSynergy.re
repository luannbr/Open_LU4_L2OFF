// skillsynergydata.txt scanner.
// re2c generates LexerSkillSynergy.cpp.

#include "L2Server/src/LexerSkillSynergy.h"

#include "L2Server/src/ParserSkillSynergy.h"

#include "L2Server/src/Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ParserSkillSynergyTokens.h"

namespace {

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

char16_t* DuplicateText(const char16_t* text, int length) {
    auto* const duplicate = static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(length + 1) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, duplicate);
    duplicate[length] = u'\0';
    return duplicate;
}

char16_t* DuplicateQuotedText(const char16_t* text, int length) {
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

CLexerForSkillSynergy::CLexerForSkillSynergy() :
    m_FileBuf(nullptr), m_pRawFileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0) {
}

CLexerForSkillSynergy::~CLexerForSkillSynergy() {
    m_FileBuf = nullptr;
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
}

int CLexerForSkillSynergy::Create(char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

int CLexerForSkillSynergy::Lex(grammar::SemanticBits* lval) {
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
            "/*" .* "*/" { EndToken(); continue; }
            "skill_synergy_begin" { EndToken(); return TK_SKILL_SYNERGY_BEGIN; }
            "skill_synergy_end" { EndToken(); return TK_SKILL_SYNERGY_END; }
            "synergy_id" { EndToken(); return TK_SYNERGY_ID; }
            "required_slot" { EndToken(); return TK_REQUIRED_SLOT; }
            "synergy_slot" { EndToken(); return TK_SYNERGY_SLOT; }
            "synergy_skills" { EndToken(); return TK_SYNERGY_SKILLS; }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            "$" ("0" | [1-9][0-9]*) {
                EndToken();
                L2_GUARD_FUNCTION();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext + 1, static_cast<std::size_t>(yyleng - 1)))
                );
                return TK_INTEGER;
            }
            [&;={}] {
                EndToken();
                L2_GUARD_FUNCTION();
                return grammar::CharacterToken(yytext[0]);
            }
            "[" [^[\]]* "]" {
                EndToken();
                L2_GUARD_FUNCTION();
                char16_t* const text = DuplicateQuotedText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            "@" [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                const char16_t* const name = yytext + 1;
                const std::int64_t found = g_PrecompiledHeaderDB.Search(name);
                if (found == -1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"line[%d] : Undefined item name[%s]",
                    //     line + 1,
                    //     name)
                    // L2_LOG_ERROR(Log,
                    //     L"line[%d] : Undefined item name[%s]",
                    //     line + 1,
                    //     name)
                    L2_LOG_ERROR(
                        Log,
                        "skillsynergydata.txt line %lld: PCH reference '%s' resolves to -1; parsing continues with that "
                        "numeric "
                        "value.",
                        static_cast<long long>(line) + 1, Utf16ToUtf8(name).c_str()
                    );
                }
                *lval = static_cast<grammar::SemanticBits>(static_cast<std::int32_t>(found));
                return TK_INTEGER;
            }
            [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                char16_t* const text = DuplicateText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
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
                // Log.Add(
                //     LOG_ERROR,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     yytext)
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     yytext)
                L2_LOG_ERROR(
                    Log,
                    "skillsynergydata.txt line %lld: invalid input '%s' (first UTF-16 code unit 0x%04X); input skipped.",
                    static_cast<long long>(line) + 1, Utf16ToUtf8(yytext).c_str(),
                    static_cast<unsigned>(static_cast<std::uint16_t>(*yytext))
                );
                continue;
            }
            $ { return 0; }
        */
    }
}
