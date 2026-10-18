// CLexerForAlchemy scanner.
// re2c generates LexerAlchemy.cpp.

#include "L2Server/src/LexerAlchemy.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserAlchemy.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserAlchemyTokens.h"

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

char16_t* DuplicateFullText(const char16_t* const text, const int length) {
    auto* const duplicate = static_cast<char16_t*>(std::malloc((static_cast<std::size_t>(length) + 1) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, duplicate);
    duplicate[length] = u'\0';
    return duplicate;
}

} // namespace

CLexerForAlchemy::CLexerForAlchemy() :
    m_FileBuf(nullptr),
    m_pRawFileBuf(nullptr),
    m_nFilePointer(0),
    m_nFileSize(0),
    line(0),
    comment_count(0),
    m_bOnlyPrecompiledHeader(false) {
}

CLexerForAlchemy::~CLexerForAlchemy() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
}

int CLexerForAlchemy::Create(char16_t* fileBuffer, int fileSize) {
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

int CLexerForAlchemy::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "rolly_fixed_begin" { EndToken(); return TK_ROLLY_FIXED_BEGIN; }
            <MAIN> "rolly_fixed_end" { EndToken(); return TK_ROLLY_FIXED_END; }
            <MAIN> "level" { EndToken(); return TK_LEVEL; }
            <MAIN> "reward" { EndToken(); return TK_REWARD; }
            <MAIN> "conversion_recipe_begin" { EndToken(); return TK_CONVERSION_RECIPE_BEGIN; }
            <MAIN> "conversion_recipe_end" { EndToken(); return TK_CONVERSION_RECIPE_END; }
            <MAIN> "skill_id" { EndToken(); return TK_SKILL_ID; }
            <MAIN> "material_list" { EndToken(); return TK_MATERIAL_LIST; }
            <MAIN> "material_cond_begin" { EndToken(); return TK_MATERIAL_COND_BEGIN; }
            <MAIN> "material_cond_end" { EndToken(); return TK_MATERIAL_COND_END; }
            <MAIN> "success_reward" { EndToken(); return TK_SUCCESS_REWARD; }
            <MAIN> "fail_reward" { EndToken(); return TK_FAIL_REWARD; }
            <MAIN> "conv_prob" { EndToken(); return TK_CONV_PROB; }
            <MAIN> "conv_success_effect" { EndToken(); return TK_CONV_SUCCESS_EFFECT; }
            <MAIN> "conv_fail_effect" { EndToken(); return TK_CONV_FAIL_EFFECT; }
            <MAIN> "fixed_item" { EndToken(); return TK_FIXED_ITEM; }
            <MAIN> "rolly_success_effect" { EndToken(); return TK_ROLLY_SUCCESS_EFFECT; }
            <MAIN> "rolly_prob" { EndToken(); return TK_ROLLY_PROB; }
            <MAIN> "rolly_reward" { EndToken(); return TK_ROLLY_REWARD; }
            <MAIN> "rolly_level" { EndToken(); return TK_ROLLY_LEVEL; }
            <MAIN> "level_cond_begin" { EndToken(); return TK_LEVEL_COND_BEGIN; }
            <MAIN> "level_cond_end" { EndToken(); return TK_LEVEL_COND_END; }
            <MAIN> "global_begin" { EndToken(); return TK_GLOBAL_BEGIN; }
            <MAIN> "global_end" { EndToken(); return TK_GLOBAL_END; }
            <MAIN> "rolly_base_id" { EndToken(); return TK_ROLLY_BASE_ID; }
            <MAIN> "rolly_base_dp" { EndToken(); return TK_ROLLY_BASE_DP; }
            <MAIN> "rolly_criterion" { EndToken(); return TK_ROLLY_CRITERION; }
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
            // The original lex scanner trimmed this trailing context back to the
            // longest float. A float followed by "." or by the end of the input
            // keeps its last digit there, so these two rules do the same.
            <MAIN> "-"? [0-9]+ "." ([0-9]+ | [0-9]* [eE] [+-]? [0-9][0-9]+) / "." {
                EndToken();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> "-"? [0-9]+ "." [0-9]* ([eE] [+-]? [0-9]+)? / [^.] {
                if (limit_ - cursor_ == 1 && *cursor_ >= u'0' && *cursor_ <= u'9') {
                    ++cursor_;
                }
                EndToken();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                char16_t* const text = DuplicateFullText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_IDENTIFIER;
            }
            <MAIN> [,;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
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
