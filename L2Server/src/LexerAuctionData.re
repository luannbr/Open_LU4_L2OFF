// auctiondata.txt scanner (original L2Server/lexer_auctiondata.l).
// re2c generates LexerAuctionData.cpp.

#include "L2Server/src/LexerAuctionData.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserAuctionData.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ParserAuctionDataTokens.h"

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

CLexerForAuction::CLexerForAuction() :
    m_FileBuf(nullptr), m_pRawFileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0), comment_count(0) {
}

CLexerForAuction::~CLexerForAuction() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
}

int CLexerForAuction::Create() {
    int byteCount = 0;
#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\auctiondata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/auctiondata.txt";
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

int CLexerForAuction::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "auction_begin" { EndToken(); return TK_AUCTION_BEGIN; }
            <MAIN> "auction_end" { EndToken(); return TK_AUCTION_END; }
            <MAIN> "npc_id" { EndToken(); return TK_NPC_ID; }
            <MAIN> "auction_doing" { EndToken(); return TK_AUCTION_DOING; }
            <MAIN> "nextinfo_open_time" { EndToken(); return TK_NEXTINFO_OPEN_TIME; }
            <MAIN> "auction_start_time" { EndToken(); return TK_AUCTION_START_TIME; }
            <MAIN> "auction_duration" { EndToken(); return TK_AUCTION_DURATION; }
            <MAIN> "auction_week_day" { EndToken(); return TK_AUCTION_WEEK_DAY; }
            <MAIN> "auction_date_begin" { EndToken(); return TK_AUCTION_DATE_BEGIN; }
            <MAIN> "auction_date_end" { EndToken(); return TK_AUCTION_DATE_END; }
            <MAIN> "item_count" { EndToken(); return TK_ITEM_COUNT; }
            <MAIN> "item_begin" { EndToken(); return TK_ITEM_BEGIN; }
            <MAIN> "item_end" { EndToken(); return TK_ITEM_END; }
            <MAIN> "item" { EndToken(); return TK_ITEM; }
            <MAIN> "amount" { EndToken(); return TK_AMOUNT; }
            <MAIN> "price" { EndToken(); return TK_PRICE; }
            <MAIN> "index" { EndToken(); return TK_INDEX; }
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
            <MAIN> [,;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            <MAIN> "@" [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(g_PrecompiledHeaderDB.Search(yytext + 1));
                return TK_INTEGER;
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
                const std::wstring invalid = Utf16ToWide(yytext);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
                L2_LOG_ERROR(
                    Log, L"auctiondata.txt line %lld: invalid token '%ls'; the token is skipped.",
                    static_cast<long long>(line) + 1, invalid.c_str()
                );
                continue;
            }
            <INITIAL> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}
