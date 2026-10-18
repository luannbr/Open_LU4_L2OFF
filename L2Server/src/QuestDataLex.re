// UserQuestData.txt scanner.
// re2c generates QuestDataLex.cpp.

#include "L2Server/src/QuestDataLex.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/QuestDataYacc.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "QuestDataYaccTokens.h"

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

std::int32_t ParseWindowsInt32(const std::u16string_view value) {
    return static_cast<std::int32_t>(ParseWindowsInt64(value));
}

char16_t* DuplicateText(const char16_t* const text, const int length) {
    auto* const result = static_cast<char16_t*>(std::malloc((static_cast<std::size_t>(length) + 1) * sizeof(char16_t)));
    if (result == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, result);
    result[length] = u'\0';
    return result;
}

char16_t* DuplicateString(const std::u16string& text) { return DuplicateText(text.c_str(), static_cast<int>(text.size())); }

} // namespace

CLexerForQuestData::CLexerForQuestData() :
    m_FileBuf(nullptr),
    m_nFilePointer(0),
    m_nFileSize(0),
    m_line(0),
    m_charCount(0),
    comment_count(0),
    string_mode(0),
    qstring() {
    // The constructor leaves m_LastInput and m_nLastInput as they are.
    // Create resets both before any input is read.
}

CLexerForQuestData::~CLexerForQuestData() { m_FileBuf = nullptr; }

int CLexerForQuestData::Create(char16_t* const fileBuffer, const int fileSize) {
    // Clear the input history before the scanner starts.
    m_nLastInput = 0;
    std::fill_n(m_LastInput, 100, u'\0');
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

int CLexerForQuestData::yygetchar() {
    if (m_nFilePointer >= m_nFileSize) {
        return -1;
    }
    const char16_t value = m_FileBuf[m_nFilePointer++];
    m_LastInput[m_nLastInput % 100] = value;
    ++m_nLastInput;
    return static_cast<int>(static_cast<std::uint16_t>(value));
}

int CLexerForQuestData::ErrorLine() const noexcept { return m_line + 1; }

int CLexerForQuestData::ErrorAt() const noexcept { return std::max(m_nFilePointer - m_charCount, 0); }

char16_t* CLexerForQuestData::ErrorText() const noexcept { return m_FileBuf + std::min(m_nFileSize - 1, m_nFilePointer); }

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN, yycMAIN2};

} // namespace

int CLexerForQuestData::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        SetReader([this] { return yygetchar(); });
    }
    auto* const semanticSlot = lval;
    const auto setPointer = [semanticSlot](const void* const value) {
    *semanticSlot = grammar::PointerSemantic(value);
};
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
                ++m_line;
                m_charCount = m_nFilePointer;
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
            <MAIN> "\"" { EndToken(); return TK_SYM; }
            <MAIN> "quest_begin" { EndToken(); return TK_QUEST_BEGIN; }
            <MAIN> "quest_end" { EndToken(); return TK_QUEST_END; }
            <MAIN> "quest_id" { EndToken(); return TK_QUEST_ID; }
            <MAIN> "quest_name" { EndToken(); return TK_QUEST_NAME; }
            <MAIN> "journal_begin" { EndToken(); return TK_JOURNAL_BEGIN; }
            <MAIN> "journal_end" { EndToken(); return TK_JOURNAL_END; }
            <MAIN> "start_conditions" { EndToken(); return TK_START_CONDITIONS; }
            <MAIN> "accept_ok_operations" { EndToken(); return TK_ACCEPT_OK_OPERATIONS; }
            <MAIN> "accept_fail_operations" { EndToken(); return TK_ACCEPT_FAIL_OPERATIONS; }
            <MAIN> "complete_conditions" { EndToken(); return TK_COMPLETE_CONDITIONS; }
            <MAIN> "complete_operations" { EndToken(); return TK_COMPLETE_OPERATIONS; }
            <MAIN> "complete_actions" { EndToken(); return TK_COMPLETE_ACTIONS; }
            <MAIN> "cancel_operations" { EndToken(); return TK_CANCEL_OPERATIONS; }
            <MAIN> "begin_operations" { EndToken(); return TK_BEGIN_OPERATIONS; }
            <MAIN> "npc_die_event_start" {
                EndToken();
                setPointer(DuplicateText(yytext, yyleng));
                return TK_NPC_DIE_EVENT_START_TALK_SELECTED_EVENT_START_MENU_SELECTED_EVENT_START;
            }
            <MAIN> "npc_die_event_end" {
                EndToken();
                return TK_NPC_DIE_EVENT_END_TALK_SELECTED_EVENT_END_MENU_SELECTED_EVENT_END;
            }
            <MAIN> "talk_selected_event_start" {
                EndToken();
                setPointer(DuplicateText(yytext, yyleng));
                return TK_NPC_DIE_EVENT_START_TALK_SELECTED_EVENT_START_MENU_SELECTED_EVENT_START;
            }
            <MAIN> "talk_selected_event_end" {
                EndToken();
                return TK_NPC_DIE_EVENT_END_TALK_SELECTED_EVENT_END_MENU_SELECTED_EVENT_END;
            }
            <MAIN> "menu_selected_event_start" {
                EndToken();
                setPointer(DuplicateText(yytext, yyleng));
                return TK_NPC_DIE_EVENT_START_TALK_SELECTED_EVENT_START_MENU_SELECTED_EVENT_START;
            }
            <MAIN> "menu_selected_event_end" {
                EndToken();
                return TK_NPC_DIE_EVENT_END_TALK_SELECTED_EVENT_END_MENU_SELECTED_EVENT_END;
            }
            <MAIN> "choice_string" { EndToken(); return TK_CHOICE_STRING; }
            <MAIN> "event_conditions" { EndToken(); return TK_EVENT_CONDITIONS; }
            <MAIN> "event_actions" { EndToken(); return TK_EVENT_ACTIONS; }
            <MAIN> "npc_list" { EndToken(); return TK_NPC_LIST; }
            <MAIN> "condition_target_type" { EndToken(); return TK_CONDITION_TARGET_TYPE; }
            <MAIN> "execution_type" { EndToken(); return TK_EXECUTION_TYPE; }
            <MAIN> "quest_items" { EndToken(); return TK_QUEST_ITEMS; }
            <MAIN> "event_range" { EndToken(); return TK_EVENT_RANGE; }
            <MAIN> "ask" { EndToken(); return TK_ASK; }
            <MAIN> "reply" { EndToken(); return TK_REPLY; }
            <MAIN> "action_begin" { EndToken(); return TK_ACTION_BEGIN; }
            <MAIN> "action_end" { EndToken(); return TK_ACTION_END; }
            <MAIN> "conditions" { EndToken(); return TK_CONDITIONS; }
            <MAIN> "operations" { EndToken(); return TK_OPERATIONS; }
            <MAIN> "-"? [0-9]+ {
                EndToken();
                *semanticSlot =
                    static_cast<grammar::SemanticBits>(ParseWindowsInt64(std::u16string_view(yytext, yyleng)));
                return TK_INTEGER;
            }
            <MAIN> "$" ("0" | [1-9][0-9]*) {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    static_cast<std::int64_t>(ParseWindowsInt32(std::u16string_view(yytext + 1, yyleng - 1)))
                );
                return TK_INTEGER;
            }
            <MAIN> [a-zA-Z_][a-zA-Z0-9_.<>]* {
                EndToken();
                setPointer(DuplicateText(yytext, yyleng));
                return TK_IDENTIFIER;
            }
            <MAIN> "@" [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(g_PrecompiledHeaderDB.Search(yytext + 1));
                return TK_INTEGER;
            }
            <MAIN> "!=" {
                EndToken();
                *semanticSlot = 0;
                return TK_INTEGER;
            }
            <MAIN> "==" {
                EndToken();
                *semanticSlot = 1;
                return TK_INTEGER;
            }
            <MAIN> ">=" {
                EndToken();
                *semanticSlot = 2;
                return TK_INTEGER;
            }
            <MAIN> "<=" {
                EndToken();
                *semanticSlot = 3;
                return TK_INTEGER;
            }
            <MAIN> ">" {
                EndToken();
                *semanticSlot = 4;
                return TK_INTEGER;
            }
            <MAIN> "<" {
                EndToken();
                *semanticSlot = 5;
                return TK_INTEGER;
            }
            <MAIN> [&(),;={}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" {
                EndToken();
                qstring.assign(u"");
                string_mode = 1;
                yybegin(6);
                continue;
            }
            <MAIN2> "[" {
                EndToken();
                const auto appendFirst = [this]() { qstring.append(std::u16string_view(yytext, 1)); };
                appendFirst();
                ++string_mode;
                continue;
            }
            <MAIN2> "]" {
                EndToken();
                --string_mode;
                if (string_mode == 0) {
                    yybegin(4);
                    setPointer(DuplicateString(qstring));
                    return TK_RBRACKET;
                }
                continue;
            }
            <MAIN2> "\r" {
                EndToken();
                const auto appendFirst = [this]() { qstring.append(std::u16string_view(yytext, 1)); };
                appendFirst();
                continue;
            }
            <MAIN2> "\n" {
                EndToken();
                const auto appendFirst = [this]() { qstring.append(std::u16string_view(yytext, 1)); };
                appendFirst();
                ++m_line;
                m_charCount = m_nFilePointer;
                continue;
            }
            <MAIN2> . {
                EndToken();
                const auto appendFirst = [this]() { qstring.append(std::u16string_view(yytext, 1)); };
                appendFirst();
                continue;
            }
            <MAIN> [ \t] { EndToken(); continue; }
            <MAIN> "\r" { EndToken(); continue; }
            <MAIN> "\n" {
                EndToken();
                ++m_line;
                m_charCount = m_nFilePointer;
                continue;
            }
            <MAIN> . {
                EndToken();
                // const std::wstring invalid = target_utf16::ToHostCodeUnits(yytext)
                const std::string invalid = Utf16ToUtf8(yytext);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
                L2_LOG_ERROR(
                    Log, "UserQuestData.txt line %lld: invalid input '%s'; this input is skipped and lexing continues.",
                    static_cast<long long>(m_line) + 1, invalid.c_str()
                );
                continue;
            }
            <INITIAL> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN,MAIN2> $ { return 0; }
        */
    }
}
