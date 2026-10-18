// eventcampaign.txt scanner.
// re2c generates EventCampaignLexer.cpp.

#include "L2Server/src/EventCampaignLexer.h"

#include "L2Server/src/EventCampaignParser.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "EventCampaignParserTokens.h"

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

CLexerForEventCampaign::CLexerForEventCampaign() :
    m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0), comment_count(0) {
}

CLexerForEventCampaign::~CLexerForEventCampaign() { m_FileBuf = nullptr; }

int CLexerForEventCampaign::Create(char16_t* fileBuffer, int fileSize) {
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

int CLexerForEventCampaign::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "event_campaign_begin" { EndToken(); return TK_EVENT_CAMPAIGN_BEGIN; }
            <MAIN> "event_campaign_end" { EndToken(); return TK_EVENT_CAMPAIGN_END; }
            <MAIN> "event_campaign_id" { EndToken(); return TK_EVENT_CAMPAIGN_ID; }
            <MAIN> "elite_count" { EndToken(); return TK_ELITE_COUNT; }
            <MAIN> "reward_period" { EndToken(); return TK_REWARD_PERIOD; }
            <MAIN> "start_condition_check_begin" { EndToken(); return TK_START_CONDITION_CHECK_BEGIN; }
            <MAIN> "start_condition_check_end" { EndToken(); return TK_START_CONDITION_CHECK_END; }
            <MAIN> "level" { EndToken(); return TK_LEVEL; }
            <MAIN> "auto_execution_begin" { EndToken(); return TK_AUTO_EXECUTION_BEGIN; }
            <MAIN> "auto_execution_end" { EndToken(); return TK_AUTO_EXECUTION_END; }
            <MAIN> "period" { EndToken(); return TK_PERIOD; }
            <MAIN> "time_dice" { EndToken(); return TK_TIME_DICE; }
            <MAIN> "step_list_begin" { EndToken(); return TK_STEP_LIST_BEGIN; }
            <MAIN> "step_list_end" { EndToken(); return TK_STEP_LIST_END; }
            <MAIN> "step_begin" { EndToken(); return TK_STEP_BEGIN; }
            <MAIN> "step_end" { EndToken(); return TK_STEP_END; }
            <MAIN> "step_id" { EndToken(); return TK_STEP_ID; }
            <MAIN> "duration" { EndToken(); return TK_DURATION; }
            <MAIN> "group_random_select_rate" { EndToken(); return TK_GROUP_RANDOM_SELECT_RATE; }
            <MAIN> "start_html" { EndToken(); return TK_START_HTML; }
            <MAIN> "npc_maker" { EndToken(); return TK_NPC_MAKER; }
            <MAIN> "area_on" { EndToken(); return TK_AREA_ON; }
            <MAIN> "goal_group_list_begin" { EndToken(); return TK_GOAL_GROUP_LIST_BEGIN; }
            <MAIN> "goal_group_list_end" { EndToken(); return TK_GOAL_GROUP_LIST_END; }
            <MAIN> "goal_group_begin" { EndToken(); return TK_GOAL_GROUP_BEGIN; }
            <MAIN> "goal_group_end" { EndToken(); return TK_GOAL_GROUP_END; }
            <MAIN> "goal_group_id" { EndToken(); return TK_GOAL_GROUP_ID; }
            <MAIN> "context_html" { EndToken(); return TK_CONTEXT_HTML; }
            <MAIN> "failed_html" { EndToken(); return TK_FAILED_HTML; }
            <MAIN> "reward_html" { EndToken(); return TK_REWARD_HTML; }
            <MAIN> "elite_reward_html" { EndToken(); return TK_ELITE_REWARD_HTML; }
            <MAIN> "reward_received_html" { EndToken(); return TK_REWARD_RECEIVED_HTML; }
            <MAIN> "goal" { EndToken(); return TK_GOAL; }
            <MAIN> "accept_item" { EndToken(); return TK_ACCEPT_ITEM; }
            <MAIN> "reward" { EndToken(); return TK_REWARD; }
            <MAIN> "elite_reward" { EndToken(); return TK_ELITE_REWARD; }
            <MAIN> "teleport_pos" { EndToken(); return TK_TELEPORT_POS; }
            <MAIN> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            <MAIN> "0" [0-9] {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            <MAIN> [%,:;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                *lval =
                    grammar::PointerSemantic(DuplicateInnerText(yytext, yyleng));
                return TK_NAME;
            }
            <MAIN> "\"" [^"]* "\"" {
                EndToken();
                *lval =
                    grammar::PointerSemantic(DuplicateInnerText(yytext, yyleng));
                return TK_STRING;
            }
            <INITIAL,MAIN> [ \t\r] { EndToken(); continue; }
            <INITIAL,MAIN> "\n" {
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
