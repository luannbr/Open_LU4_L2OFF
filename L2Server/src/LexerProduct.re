// CLexerForProduct scanner (original L2Server/lexer_product.l).
// re2c generates LexerProduct.cpp.

#include "L2Server/src/LexerProduct.h"

#include "L2Server/src/ParserProduct.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <istream>
#include <string>
#include <string_view>

#include "ParserProductTokens.h"

namespace {

bool firstTime = true;

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

char16_t* DuplicateText(const char16_t* text, int length) {
    auto* const duplicate = static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(length + 1) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, duplicate);
    duplicate[length] = u'\0';
    return duplicate;
}

} // namespace

CLexerForProduct::CLexerForProduct() : line(0), comment_count(0) {
}

int CLexerForProduct::yygetchar() {
    if (firstTime) {
        yyin->get();
        firstTime = false;
    }
    return ReadStream();
}

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN};

} // namespace

int CLexerForProduct::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        SetReader([this] { return yygetchar(); });
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
            <MAIN> "product_begin" { EndToken(); return TK_PRODUCT_BEGIN; }
            <MAIN> "id" { EndToken(); return TK_ID; }
            <MAIN> "name" { EndToken(); return TK_NAME; }
            <MAIN> "category" { EndToken(); return TK_CATEGORY; }
            <MAIN> "price" { EndToken(); return TK_PRICE; }
            <MAIN> "item_panel_type" { EndToken(); return TK_ITEM_PANEL_TYPE; }
            <MAIN> "is_event_product" { EndToken(); return TK_IS_EVENT_PRODUCT; }
            <MAIN> "is_best_product" { EndToken(); return TK_IS_BEST_PRODUCT; }
            <MAIN> "is_new_product" { EndToken(); return TK_IS_NEW_PRODUCT; }
            <MAIN> "is_featured_product" { EndToken(); return TK_IS_FEATURED_PRODUCT; }
            <MAIN> "is_advertising" { EndToken(); return TK_IS_ADVERTISING; }
            <MAIN> "is_recommend" { EndToken(); return TK_IS_RECOMMEND; }
            <MAIN> "is_popular" { EndToken(); return TK_IS_POPULAR; }
            <MAIN> "buyable" { EndToken(); return TK_BUYABLE; }
            <MAIN> "buy_condition" { EndToken(); return TK_BUY_CONDITION; }
            <MAIN> "sale_start_date" { EndToken(); return TK_SALE_START_DATE; }
            <MAIN> "sale_end_date" { EndToken(); return TK_SALE_END_DATE; }
            <MAIN> "location_id" { EndToken(); return TK_LOCATION_ID; }
            <MAIN> "server_type" { EndToken(); return TK_SERVER_TYPE; }
            <MAIN> "items" { EndToken(); return TK_ITEMS; }
            <MAIN> "itemids" { EndToken(); return TK_ITEMIDS; }
            <MAIN> "day_of_week" { EndToken(); return TK_DAY_OF_WEEK; }
            <MAIN> "time_of_day" { EndToken(); return TK_TIME_OF_DAY; }
            <MAIN> "stock" { EndToken(); return TK_STOCK; }
            <MAIN> "sale_percent" { EndToken(); return TK_SALE_PERCENT; }
            <MAIN> "min_level" { EndToken(); return TK_MIN_LEVEL; }
            <MAIN> "max_level" { EndToken(); return TK_MAX_LEVEL; }
            <MAIN> "birthday_start" { EndToken(); return TK_BIRTHDAY_START; }
            <MAIN> "birthday_end" { EndToken(); return TK_BIRTHDAY_END; }
            <MAIN> "sale_limit" { EndToken(); return TK_SALE_LIMIT; }
            <MAIN> "product_end" { EndToken(); return TK_PRODUCT_END; }
            <MAIN> "=" { EndToken(); return TK_EQUAL; }
            <MAIN> "/" { EndToken(); return TK_SLASH; }
            <MAIN> ":" { EndToken(); return TK_COLON; }
            <MAIN> "~" { EndToken(); return TK_TILDE; }
            <MAIN> "-"? [0-9]+ {
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
            <MAIN> "-"{0,2} [0-9]+ "." [0-9]* {
                EndToken();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> [&;{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME_2;
            }
            <MAIN> [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                char16_t* const text = DuplicateText(yytext, yyleng);
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
                //     L"[LEXER_ERROR] Invalid char[%c]",
                //     static_cast<wchar_t>(*yytext))
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char[%c]",
                //     static_cast<wchar_t>(*yytext))
                L2_LOG_ERROR(
                    Log, "ProductData contains unexpected UTF-16 code unit 0x%04x at line %lld; scanning continues.",
                    static_cast<unsigned>(static_cast<std::uint16_t>(*yytext)), static_cast<long long>(line) + 1
                );
                continue;
            }
            <INITIAL> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}
