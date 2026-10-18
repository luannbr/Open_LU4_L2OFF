// PledgeMission.txt scanner.
// re2c generates PledgeMissionLexer.cpp.

#include "L2Server/src/PledgeMissionLexer.h"

#include "L2Server/src/PledgeMissionParser.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "PledgeMissionParserTokens.h"

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

std::int32_t ParseWindowsInt32(std::u16string_view value) {
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
    if (errno == ERANGE || parsed > INT32_MAX) {
        return INT32_MAX;
    }
    if (parsed < INT32_MIN) {
        return INT32_MIN;
    }
    return static_cast<std::int32_t>(parsed);
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

char16_t* DuplicateText(const char16_t* text, int length) {
    auto* const duplicate = static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(length + 1) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, duplicate);
    duplicate[length] = u'\0';
    return duplicate;
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

CLexerForPledgeMission::CLexerForPledgeMission() :
    m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0), comment_count(0) {
}

CLexerForPledgeMission::~CLexerForPledgeMission() { m_FileBuf = nullptr; }

int CLexerForPledgeMission::Create(char16_t* fileBuffer, int fileSize) {
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

int CLexerForPledgeMission::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "pledge_mission_begin" { EndToken(); return TK_PLEDGE_MISSION_BEGIN; }
            <MAIN> "pledge_mission_end" { EndToken(); return TK_PLEDGE_MISSION_END; }
            <MAIN> "id" { EndToken(); return TK_ID; }
            <MAIN> "category" { EndToken(); return TK_CATEGORY; }
            <MAIN> "is_repeat" { EndToken(); return TK_IS_REPEAT; }
            <MAIN> "pledge_level" { EndToken(); return TK_PLEDGE_LEVEL; }
            <MAIN> "s_pledge_mastery" { EndToken(); return TK_S_PLEDGE_MASTERY; }
            <MAIN> "min_level" { EndToken(); return TK_MIN_LEVEL; }
            <MAIN> "max_level" { EndToken(); return TK_MAX_LEVEL; }
            <MAIN> "job_main" { EndToken(); return TK_JOB_MAIN; }
            <MAIN> "job_dual" { EndToken(); return TK_JOB_DUAL; }
            <MAIN> "job_sub" { EndToken(); return TK_JOB_SUB; }
            <MAIN> "pre_mission_id" { EndToken(); return TK_PRE_MISSION_ID; }
            <MAIN> "start_date" { EndToken(); return TK_START_DATE; }
            <MAIN> "end_date" { EndToken(); return TK_END_DATE; }
            <MAIN> "start_time" { EndToken(); return TK_START_TIME; }
            <MAIN> "end_time" { EndToken(); return TK_END_TIME; }
            <MAIN> "activate_time" { EndToken(); return TK_ACTIVATE_TIME; }
            <MAIN> "deactivate_time" { EndToken(); return TK_DEACTIVATE_TIME; }
            <MAIN> "available_day" { EndToken(); return TK_AVAILABLE_DAY; }
            <MAIN> "goal_type" { EndToken(); return TK_GOAL_TYPE; }
            <MAIN> "goal_target" { EndToken(); return TK_GOAL_TARGET; }
            <MAIN> "goal_sub_target" { EndToken(); return TK_GOAL_SUB_TARGET; }
            <MAIN> "goal_count" { EndToken(); return TK_GOAL_COUNT; }
            <MAIN> "reward_pledge_name_value" { EndToken(); return TK_REWARD_PLEDGE_NAME_VALUE; }
            <MAIN> "reward_pvp_point" { EndToken(); return TK_REWARD_PVP_POINT; }
            <MAIN> "reward_items" { EndToken(); return TK_REWARD_ITEMS; }
            <MAIN> "=" { EndToken(); return TK_EQUAL; }
            <MAIN> "-"? [0-9]+ {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(static_cast<std::int64_t>(
                    ParseWindowsInt32(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                ));
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
            <MAIN> [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                char16_t* const text = DuplicateText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_IDENTIFIER;
            }
            <MAIN> [&/;{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
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
                //     static_cast<int>(static_cast<std::uint16_t>(*yytext)))
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char[%c]",
                //     static_cast<int>(static_cast<std::uint16_t>(*yytext)))
                L2_LOG_ERROR(
                    Log, "PledgeMission.txt line %lld: invalid UTF-16 code unit U+%04X; skipping it.",
                    static_cast<long long>(line) + 1, static_cast<unsigned>(static_cast<std::uint16_t>(*yytext))
                );
                continue;
            }
            <INITIAL> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}
