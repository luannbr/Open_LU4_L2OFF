// predefinedlistdata.txt scanner.
// re2c generates PredefinedListLexer.cpp.

#include "L2Server/src/PredefinedListLexer.h"

#include "L2Server/src/PredefinedListParser.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "PredefinedListParserTokens.h"

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

char16_t* DuplicateText(const char16_t* text, std::size_t length) {
    auto* const duplicate = static_cast<char16_t*>(std::malloc((length + 1) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, duplicate);
    duplicate[length] = u'\0';
    return duplicate;
}

} // namespace

CLexerForPredefinedList::CLexerForPredefinedList() :
    m_FileBuf(nullptr),
    m_pRawFileBuf(nullptr),
    m_nFilePointer(0),
    m_nFileSize(0),
    line(0),
    comment_count(0),
    m_bOnlyPrecompiledHeader(false) {
}

CLexerForPredefinedList::~CLexerForPredefinedList() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
        m_pRawFileBuf = nullptr;
    }
    m_FileBuf = nullptr;
}

int CLexerForPredefinedList::Create(char16_t* fileBuffer, int fileSize) {
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

int CLexerForPredefinedList::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "predefined_skill_list_begin" { EndToken(); return TK_PREDEFINED_SKILL_LIST_BEGIN; }
            <MAIN> "predefined_skill_list_end" { EndToken(); return TK_PREDEFINED_SKILL_LIST_END; }
            <MAIN> "skill_list" { EndToken(); return TK_SKILL_LIST; }
            <MAIN> "predefined_item_list_begin" { EndToken(); return TK_PREDEFINED_ITEM_LIST_BEGIN; }
            <MAIN> "predefined_item_list_end" { EndToken(); return TK_PREDEFINED_ITEM_LIST_END; }
            <MAIN> "item_list" { EndToken(); return TK_ITEM_LIST; }
            <MAIN> "predefined_territory_list_begin" { EndToken(); return TK_PREDEFINED_TERRITORY_LIST_BEGIN; }
            <MAIN> "predefined_territory_list_end" { EndToken(); return TK_PREDEFINED_TERRITORY_LIST_END; }
            <MAIN> "territory_list" { EndToken(); return TK_TERRITORY_LIST; }
            <MAIN> "op_not_territory" { EndToken(); return TK_OP_NOT_TERRITORY; }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateText(yytext + 1, static_cast<std::size_t>(yyleng - 2));
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            <MAIN> "=" { EndToken(); return TK_EQUAL; }
            <MAIN> "-"? ("0" | [1-9][0-9]*) {
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
            <MAIN> [,:;[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "&" { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "@" [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                std::int64_t value = -1;
                if (!m_bOnlyPrecompiledHeader) {
                    value = g_PrecompiledHeaderDB.Search(yytext + 1);
                    if (value == -1) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"line[%d] : Undefined word[%s] in precompiled header",
                        //     line + 1,
                        //     yytext)
                        // L2_LOG_ERROR(Log,
                        //     L"line[%d] : Undefined word[%s] in precompiled header",
                        //     line + 1,
                        //     yytext)
                        L2_LOG_ERROR(
                            Log,
                            "predefinedlistdata.txt line %lld: unknown PCH reference '%s'; parsing continues with numeric "
                            "value "
                            "-1.",
                            static_cast<long long>(line) + 1, Utf16ToUtf8(yytext).c_str()
                        );
                    }
                }
                *lval = static_cast<grammar::SemanticBits>(value);
                return TK_INTEGER;
            }
            <MAIN> [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                char16_t* const text = DuplicateText(yytext, static_cast<std::size_t>(yyleng));
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_IDENTIFIER;
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
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     yytext)
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     yytext)
                L2_LOG_ERROR(
                    Log,
                    "predefinedlistdata.txt line %lld: invalid input '%s' (first UTF-16 code unit 0x%04X); input skipped.",
                    static_cast<long long>(line) + 1, Utf16ToUtf8(yytext).c_str(),
                    static_cast<unsigned int>(static_cast<std::uint16_t>(*yytext))
                );
                continue;
            }
            <INITIAL> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}
