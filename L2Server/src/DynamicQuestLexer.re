// DynamicQuest.txt scanner (original L2Server/dynamicquest_lexer.l).
// re2c generates DynamicQuestLexer.cpp.

#include "L2Server/src/DynamicQuestLexer.h"

#include "L2Server/src/DynamicQuestParser.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "DynamicQuestParserTokens.h"

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

CLexerForDynamicQuest::CLexerForDynamicQuest() :
    m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), comment_count(0), line(0) {
}

CLexerForDynamicQuest::~CLexerForDynamicQuest() { m_FileBuf = nullptr; }

int CLexerForDynamicQuest::Create(char16_t* fileBuffer, int fileSize) {
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

int CLexerForDynamicQuest::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "default_setting_begin" { EndToken(); return TK_DEFAULT_SETTING_BEGIN; }
            <MAIN> "default_setting_end" { EndToken(); return TK_DEFAULT_SETTING_END; }
            <MAIN> "fhtml_param_setting_begin" { EndToken(); return TK_FHTML_PARAM_SETTING_BEGIN; }
            <MAIN> "fhtml_param_setting_end" { EndToken(); return TK_FHTML_PARAM_SETTING_END; }
            <MAIN> "feedback_full_inventory" { EndToken(); return TK_FEEDBACK_FULL_INVENTORY; }
            <MAIN> "msg" { EndToken(); return TK_MSG; }
            <MAIN> "html" { EndToken(); return TK_HTML; }
            <MAIN> "fhtml" { EndToken(); return TK_FHTML; }
            <MAIN> "qhtml" { EndToken(); return TK_QHTML; }
            <MAIN> "qfhtml" { EndToken(); return TK_QFHTML; }
            <MAIN> "dynamic_contents_setting_begin" { EndToken(); return TK_DYNAMIC_CONTENTS_SETTING_BEGIN; }
            <MAIN> "dynamic_contents_setting_end" { EndToken(); return TK_DYNAMIC_CONTENTS_SETTING_END; }
            <MAIN> "zone_quest_setting_begin" { EndToken(); return TK_ZONE_QUEST_SETTING_BEGIN; }
            <MAIN> "zone_quest_setting_end" { EndToken(); return TK_ZONE_QUEST_SETTING_END; }
            <MAIN> "elite_count" { EndToken(); return TK_ELITE_COUNT; }
            <MAIN> "elite_count1" { EndToken(); return TK_ELITE_COUNT1; }
            <MAIN> "elite_count2" { EndToken(); return TK_ELITE_COUNT2; }
            <MAIN> "reward_count1" { EndToken(); return TK_REWARD_COUNT1; }
            <MAIN> "reward_period" { EndToken(); return TK_REWARD_PERIOD; }
            <MAIN> "dynamic_content_begin" { EndToken(); return TK_DYNAMIC_CONTENT_BEGIN; }
            <MAIN> "dynamic_content_end" { EndToken(); return TK_DYNAMIC_CONTENT_END; }
            <MAIN> "zone_quest_begin" { EndToken(); return TK_ZONE_QUEST_BEGIN; }
            <MAIN> "zone_quest_end" { EndToken(); return TK_ZONE_QUEST_END; }
            <MAIN> "dynamic_quest_id" { EndToken(); return TK_DYNAMIC_QUEST_ID; }
            <MAIN> "start_condition_check_begin" { EndToken(); return TK_START_CONDITION_CHECK_BEGIN; }
            <MAIN> "start_condition_check_end" { EndToken(); return TK_START_CONDITION_CHECK_END; }
            <MAIN> "level" { EndToken(); return TK_LEVEL; }
            <MAIN> "class" { EndToken(); return TK_CLASS; }
            <MAIN> "group_type" { EndToken(); return TK_GROUP_TYPE; }
            <MAIN> "castle_pledge_type" { EndToken(); return TK_CASTLE_PLEDGE_TYPE; }
            <MAIN> "auto_execution_begin" { EndToken(); return TK_AUTO_EXECUTION_BEGIN; }
            <MAIN> "auto_execution_end" { EndToken(); return TK_AUTO_EXECUTION_END; }
            <MAIN> "weekday" { EndToken(); return TK_WEEKDAY; }
            <MAIN> "time_dice" { EndToken(); return TK_TIME_DICE; }
            <MAIN> "period" { EndToken(); return TK_PERIOD; }
            <MAIN> "castle_type" { EndToken(); return TK_CASTLE_TYPE; }
            <MAIN> "step_list_begin" { EndToken(); return TK_STEP_LIST_BEGIN; }
            <MAIN> "step_list_end" { EndToken(); return TK_STEP_LIST_END; }
            <MAIN> "type" { EndToken(); return TK_TYPE; }
            <MAIN> "onetime" {
                EndToken();
                *lval = grammar::PointerSemantic(u"onetime");
                return TK_ONETIME_CYCLE;
            }
            <MAIN> "cycle" {
                EndToken();
                *lval = grammar::PointerSemantic(u"cycle");
                return TK_ONETIME_CYCLE;
            }
            <MAIN> "step_begin" { EndToken(); return TK_STEP_BEGIN; }
            <MAIN> "step_end" { EndToken(); return TK_STEP_END; }
            <MAIN> "step_id" { EndToken(); return TK_STEP_ID; }
            <MAIN> "duration" { EndToken(); return TK_DURATION; }
            <MAIN> "goal" { EndToken(); return TK_GOAL; }
            <MAIN> "enable_adjusted_goal" { EndToken(); return TK_ENABLE_ADJUSTED_GOAL; }
            <MAIN> "npc_maker" { EndToken(); return TK_NPC_MAKER; }
            <MAIN> "accept_item" { EndToken(); return TK_ACCEPT_ITEM; }
            <MAIN> "reward" { EndToken(); return TK_REWARD; }
            <MAIN> "reward1" { EndToken(); return TK_REWARD1; }
            <MAIN> "reward2" { EndToken(); return TK_REWARD2; }
            <MAIN> "elite_reward" { EndToken(); return TK_ELITE_REWARD; }
            <MAIN> "elite_reward1" { EndToken(); return TK_ELITE_REWARD1; }
            <MAIN> "elite_reward2" { EndToken(); return TK_ELITE_REWARD2; }
            <MAIN> "basic_reward" { EndToken(); return TK_BASIC_REWARD; }
            <MAIN> "open_door" { EndToken(); return TK_OPEN_DOOR; }
            <MAIN> "area_on" { EndToken(); return TK_AREA_ON; }
            <MAIN> "start_html" { EndToken(); return TK_START_HTML; }
            <MAIN> "context_html" { EndToken(); return TK_CONTEXT_HTML; }
            <MAIN> "reward_html" { EndToken(); return TK_REWARD_HTML; }
            <MAIN> "elite_reward_html" { EndToken(); return TK_ELITE_REWARD_HTML; }
            <MAIN> "reward_received_html" { EndToken(); return TK_REWARD_RECEIVED_HTML; }
            <MAIN> "failed_html" { EndToken(); return TK_FAILED_HTML; }
            <MAIN> "range" { EndToken(); return TK_RANGE; }
            <MAIN> "adjusted_item_begin" { EndToken(); return TK_ADJUSTED_ITEM_BEGIN; }
            <MAIN> "adjusted_item_end" { EndToken(); return TK_ADJUSTED_ITEM_END; }
            <MAIN> "excluded_id" { EndToken(); return TK_EXCLUDED_ID; }
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
            <MAIN> [%,:;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME;
            }
            <MAIN> "\"" [^"]* "\"" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_ONETIME_CYCLE;
            }
            <INITIAL,MAIN> [ \t\r] { EndToken(); continue; }
            <INITIAL,MAIN> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <INITIAL> * { EndToken(); continue; }
            <MAIN> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}
