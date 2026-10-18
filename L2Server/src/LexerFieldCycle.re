// FieldCycle.txt scanner (original L2Server/lexer_fieldcycle.l).
// re2c generates LexerFieldCycle.cpp.

#include "L2Server/src/LexerFieldCycle.h"

#include "L2Server/src/ParserFieldCycle.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ParserFieldCycleTokens.h"

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

CLexerForFieldCycle::CLexerForFieldCycle() :
    m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0), comment_count(0) {
}

CLexerForFieldCycle::~CLexerForFieldCycle() { m_FileBuf = nullptr; }

int CLexerForFieldCycle::Create(char16_t* const fileBuffer, const int fileSize) {
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

int CLexerForFieldCycle::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "cycle_begin" { EndToken(); return TK_CYCLE_BEGIN; }
            <MAIN> "cycle_end" { EndToken(); return TK_CYCLE_END; }
            <MAIN> "id" { EndToken(); return TK_ID; }
            <MAIN> "mission_start_step" { EndToken(); return TK_MISSION_START_STEP; }
            <MAIN> "mission_time_limit" { EndToken(); return TK_MISSION_TIME_LIMIT; }
            <MAIN> "mission_success_step" { EndToken(); return TK_MISSION_SUCCESS_STEP; }
            <MAIN> "mission_failed_step" { EndToken(); return TK_MISSION_FAILED_STEP; }
            <MAIN> "mission_area" { EndToken(); return TK_MISSION_AREA; }
            <MAIN> "step_begin" { EndToken(); return TK_STEP_BEGIN; }
            <MAIN> "step_end" { EndToken(); return TK_STEP_END; }
            <MAIN> "step_change_time" { EndToken(); return TK_STEP_CHANGE_TIME; }
            <MAIN> "step_change_time_in_range" { EndToken(); return TK_STEP_CHANGE_TIME_IN_RANGE; }
            <MAIN> "step_change_time_by_weekday" { EndToken(); return TK_STEP_CHANGE_TIME_BY_WEEKDAY; }
            <MAIN> "expiration_time" { EndToken(); return TK_EXPIRATION_TIME; }
            <MAIN> "step" { EndToken(); return TK_STEP; }
            <MAIN> "step_point" { EndToken(); return TK_STEP_POINT; }
            <MAIN> "lock_time" { EndToken(); return TK_LOCK_TIME; }
            <MAIN> "drop_time" { EndToken(); return TK_DROP_TIME; }
            <MAIN> "open_door" { EndToken(); return TK_OPEN_DOOR; }
            <MAIN> "area_on" { EndToken(); return TK_AREA_ON; }
            <MAIN> "restart_begin" { EndToken(); return TK_RESTART_BEGIN; }
            <MAIN> "restart_end" { EndToken(); return TK_RESTART_END; }
            <MAIN> "range" { EndToken(); return TK_RANGE; }
            <MAIN> "normal_point" { EndToken(); return TK_NORMAL_POINT; }
            <MAIN> "chao_point" { EndToken(); return TK_CHAO_POINT; }
            <MAIN> "interval_time" { EndToken(); return TK_INTERVAL_TIME; }
            <MAIN> "interval_point" { EndToken(); return TK_INTERVAL_POINT; }
            <MAIN> "map_point" { EndToken(); return TK_MAP_POINT; }
            <MAIN> "map_string" { EndToken(); return TK_MAP_STRING; }
            <MAIN> "map_hunting_zone_id" { EndToken(); return TK_MAP_HUNTING_ZONE_ID; }
            <MAIN> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            <MAIN> [,:;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const duplicate = DuplicateInnerText(yytext, yyleng);
                if (duplicate != nullptr) {
                    *lval = grammar::PointerSemantic(duplicate);
                }
                return TK_NAME;
            }
            <MAIN> [ \t\r] { EndToken(); continue; }
            <MAIN> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <MAIN> . { EndToken(); continue; }
            <INITIAL> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}
