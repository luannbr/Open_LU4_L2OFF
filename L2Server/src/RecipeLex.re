// recipe.txt scanner (original L2Server/recipe_lex.l).
// re2c generates RecipeLex.cpp.

#include "L2Server/src/RecipeLex.h"

#include "L2Server/src/RecipeParser.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "RecipeParserTokens.h"

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

double ParseWindowsDouble(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    if (ascii.empty() && !value.empty()) {
        return 0.0;
    }
    char* end = nullptr;
    const double parsed = std::strtod(ascii.c_str(), &end);
    return end == ascii.c_str() ? 0.0 : parsed;
}

char16_t* DuplicateText(const char16_t* text, int length) {
    auto* const duplicate = static_cast<char16_t*>(std::malloc((static_cast<std::size_t>(length) + 1U) * sizeof(char16_t)));
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

CLexerForRecipe::CLexerForRecipe() {
    m_FileBuf = nullptr;
    m_nFilePointer = 0;
    m_nFileSize = 0;
    line = 0;
    comment_count = 0;
}

CLexerForRecipe::~CLexerForRecipe() { m_FileBuf = nullptr; }

int CLexerForRecipe::Create(char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

int CLexerForRecipe::getLine() { return line; }

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN};

} // namespace

int CLexerForRecipe::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_FileBuf one code unit at a time.
        SetInput(
            m_FileBuf + m_nFilePointer,
            m_nFilePointer < m_nFileSize ? static_cast<std::size_t>(m_nFileSize - m_nFilePointer) : 0
        );
    }
    auto* const semanticSlot = lval;
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
            <MAIN> "none" { EndToken(); return TK_NONE; }
            <MAIN> "recipe_begin" { EndToken(); return TK_RECIPE_BEGIN; }
            <MAIN> "level" { EndToken(); return TK_LEVEL; }
            <MAIN> "material" { EndToken(); return TK_MATERIAL; }
            <MAIN> "npc_fee" { EndToken(); return TK_NPC_FEE; }
            <MAIN> "catalyst" { EndToken(); return TK_CATALYST; }
            <MAIN> "product" { EndToken(); return TK_PRODUCT; }
            <MAIN> "mp_consume" { EndToken(); return TK_MP_CONSUME; }
            <MAIN> "success_rate" { EndToken(); return TK_SUCCESS_RATE; }
            <MAIN> "item_id" { EndToken(); return TK_ITEM_ID; }
            <MAIN> "iscommonrecipe" { EndToken(); return TK_ISCOMMONRECIPE; }
            <MAIN> "create_critical" { EndToken(); return TK_CREATE_CRITICAL; }
            <MAIN> "recipe_end" { EndToken(); return TK_RECIPE_END; }
            <MAIN> "rare_condition" { EndToken(); return TK_RARE_CONDITION; }
            <MAIN> "rare_announce" { EndToken(); return TK_RARE_ANNOUNCE; }
            <MAIN> "=" { EndToken(); return TK_EQUAL; }
            <MAIN> "-"? [0-9]+ {
                EndToken();
                const char16_t* const number = yytext;
                const int length = yyleng;
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(number, static_cast<std::size_t>(length)))
                );
                return TK_INTEGER;
            }
            <MAIN> "$" ("0" | [1-9][0-9]*) {
                EndToken();
                const char16_t* const number = yytext + 1;
                const int length = yyleng - 1;
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(number, static_cast<std::size_t>(length)))
                );
                return TK_INTEGER;
            }
            <MAIN> "-"{0,2} [0-9]+ "." [0-9]* {
                EndToken();
                const double value = ParseWindowsDouble(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                std::memcpy(semanticSlot, &value, sizeof(value));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> [&/;{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateQuotedText(yytext, yyleng);
                if (text != nullptr) {
                    *semanticSlot = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            <MAIN> [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                char16_t* const text = DuplicateText(yytext, yyleng);
                if (text != nullptr) {
                    *semanticSlot = grammar::PointerSemantic(text);
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
                //     L"[LEXER_ERROR] Invalid char[%c]",
                //     static_cast<int>(
                //         static_cast<std::uint16_t>(*yytext)))
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char[%c]",
                //     static_cast<int>(
                //         static_cast<std::uint16_t>(*yytext)))
                L2_LOG_ERROR(
                    Log, "recipe.txt line %lld: invalid UTF-16 code unit 0x%04X; input skipped.",
                    static_cast<long long>(line) + 1, static_cast<unsigned int>(static_cast<std::uint16_t>(*yytext))
                );
                continue;
            }
            <INITIAL> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}
