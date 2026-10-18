// variationdata.txt scanner (original L2Server/lexer_variation.l).
// re2c generates LexerVariation.cpp.

#include "L2Server/src/LexerVariation.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserVariation.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserVariationTokens.h"

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

std::uint64_t ParseWindowsDoubleBits(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    double parsed = 0.0;
    if (!ascii.empty() || value.empty()) {
        char* end = nullptr;
        parsed = std::strtod(ascii.c_str(), &end);
        if (end == ascii.c_str()) {
            parsed = 0.0;
        }
    }
    std::uint64_t bits = 0;
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

CLexerForVariation::CLexerForVariation() :
    m_FileBuf(nullptr), m_pRawFileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0), comment_count(0) {
}

CLexerForVariation::~CLexerForVariation() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
}

int CLexerForVariation::Create() {
    // Current L2Server 0x00cc1fb8.
    int byteCount = 0;
#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\variationdata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/variationdata.txt";
#endif
    m_pRawFileBuf = LoadBinaryAbsoluteSecure(kFileName, byteCount, true);
    if (m_pRawFileBuf == nullptr) {
        return 0;
    }
    m_nFilePointer = 0;
    m_FileBuf = reinterpret_cast<char16_t*>(m_pRawFileBuf + 2);
    m_nFileSize = byteCount / 2 - 1;
    return 1;
}

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN};

} // namespace

int CLexerForVariation::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "variation_begin" { EndToken(); return TK_VARIATION_BEGIN; }
            <MAIN> "variation_end" { EndToken(); return TK_VARIATION_END; }
            <MAIN> "weapon_type" { EndToken(); return TK_WEAPON_TYPE; }
            <MAIN> "mage" { EndToken(); return TK_MAGE; }
            <MAIN> "warrior" { EndToken(); return TK_WARRIOR; }
            <MAIN> "mineral" { EndToken(); return TK_MINERAL; }
            <MAIN> "variation1" { EndToken(); return TK_VARIATION1; }
            <MAIN> "variation2" { EndToken(); return TK_VARIATION2; }
            <MAIN> "fee_item_name" { EndToken(); return TK_FEE_ITEM_NAME; }
            <MAIN> "fee_item_count" { EndToken(); return TK_FEE_ITEM_COUNT; }
            <MAIN> "cancel_fee" { EndToken(); return TK_CANCEL_FEE; }
            <MAIN> "variation_exception_weapon" { EndToken(); return TK_VARIATION_EXCEPTION_WEAPON; }
            <MAIN> "item_group_begin" { EndToken(); return TK_ITEM_GROUP_BEGIN; }
            <MAIN> "item_group_end" { EndToken(); return TK_ITEM_GROUP_END; }
            <MAIN> "item_list" { EndToken(); return TK_ITEM_LIST; }
            <MAIN> "fee_begin" { EndToken(); return TK_FEE_BEGIN; }
            <MAIN> "fee_end" { EndToken(); return TK_FEE_END; }
            <MAIN> "item_group_name" { EndToken(); return TK_ITEM_GROUP_NAME; }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
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
            <MAIN> [,;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
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
