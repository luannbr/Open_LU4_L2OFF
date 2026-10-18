// questsystem.txt scanner (original L2Server/lexer_questsystem.l).
// re2c generates LexerQuestSystem.cpp.

#include "L2Server/src/LexerQuestSystem.h"

#include "L2Server/src/ParserQuestSystem.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserQuestSystemTokens.h"

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

grammar::SemanticBits ParseWindowsDoubleBits(const std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    char* end = nullptr;
    const double parsed = std::strtod(ascii.c_str(), &end);
    double valueOrZero = end == ascii.c_str() ? 0.0 : parsed;
    grammar::SemanticBits bits = 0;
    std::memcpy(&bits, &valueOrZero, sizeof(bits));
    return bits;
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

CLexerForQuestSystem::CLexerForQuestSystem() :
    m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), comment_count(0), line(0), questId(0) {
}

CLexerForQuestSystem::~CLexerForQuestSystem() { m_FileBuf = nullptr; }

int CLexerForQuestSystem::Create(char16_t* const fileBuffer, const int fileSize) {
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

int CLexerForQuestSystem::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "quest_setting_begin" { EndToken(); return TK_QUEST_SETTING_BEGIN; }
            <MAIN> "quest_setting_end" { EndToken(); return TK_QUEST_SETTING_END; }
            <MAIN> "feedback_full_quest" { EndToken(); return TK_FEEDBACK_FULL_QUEST; }
            <MAIN> "feedback_full_inventory" { EndToken(); return TK_FEEDBACK_FULL_INVENTORY; }
            <MAIN> "feedback_duplicated_quest" { EndToken(); return TK_FEEDBACK_DUPLICATED_QUEST; }
            <MAIN> "feedback_finished_onetime_quest" { EndToken(); return TK_FEEDBACK_FINISHED_ONETIME_QUEST; }
            <MAIN> "feedback_finished_daily_quest" { EndToken(); return TK_FEEDBACK_FINISHED_DAILY_QUEST; }
            <MAIN> "sound_quest_accepted" { EndToken(); return TK_SOUND_QUEST_ACCEPTED; }
            <MAIN> "quest_begin" { EndToken(); return TK_QUEST_BEGIN; }
            <MAIN> "quest_end" { EndToken(); return TK_QUEST_END; }
            <MAIN> "quest_id" { EndToken(); return TK_QUEST_ID; }
            <MAIN> "onetime_quest_id" { EndToken(); return TK_ONETIME_QUEST_ID; }
            <MAIN> "quest_start_begin" { EndToken(); return TK_QUEST_START_BEGIN; }
            <MAIN> "quest_start_end" { EndToken(); return TK_QUEST_START_END; }
            <MAIN> "start_item" { EndToken(); return TK_START_ITEM; }
            <MAIN> "start_npc" { EndToken(); return TK_START_NPC; }
            <MAIN> "show_html" { EndToken(); return TK_SHOW_HTML; }
            <MAIN> "condition_check_begin" { EndToken(); return TK_CONDITION_CHECK_BEGIN; }
            <MAIN> "condition_check_end" { EndToken(); return TK_CONDITION_CHECK_END; }
            <MAIN> "level" { EndToken(); return TK_LEVEL; }
            <MAIN> "item" { EndToken(); return TK_ITEM; }
            <MAIN> "feedback_on_failed" { EndToken(); return TK_FEEDBACK_ON_FAILED; }
            <MAIN> "quest_accept_begin" { EndToken(); return TK_QUEST_ACCEPT_BEGIN; }
            <MAIN> "quest_accept_end" { EndToken(); return TK_QUEST_ACCEPT_END; }
            <MAIN> "give_item" { EndToken(); return TK_GIVE_ITEM; }
            <MAIN> "delete_item" { EndToken(); return TK_DELETE_ITEM; }
            <MAIN> "msg" { EndToken(); return TK_MSG; }
            <MAIN> "html" { EndToken(); return TK_HTML; }
            <MAIN> "fhtml" { EndToken(); return TK_FHTML; }
            <MAIN> "qhtml" { EndToken(); return TK_QHTML; }
            <MAIN> "qfhtml" { EndToken(); return TK_QFHTML; }
            <MAIN> "$QID$" {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(questId);
                return TK_INTEGER;
            }
            <MAIN> "fstring" { EndToken(); return TK_FSTRING; }
            <MAIN> "-"? ("0" | [1-9][0-9]*) {
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
                char16_t* const duplicate = DuplicateInnerText(yytext, yyleng);
                if (duplicate != nullptr) {
                    *lval = grammar::PointerSemantic(duplicate);
                }
                return TK_NAME;
            }
            <MAIN> "\"" [^"]* "\"" {
                EndToken();
                char16_t* const duplicate = DuplicateInnerText(yytext, yyleng);
                if (duplicate != nullptr) {
                    *lval = grammar::PointerSemantic(duplicate);
                }
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
