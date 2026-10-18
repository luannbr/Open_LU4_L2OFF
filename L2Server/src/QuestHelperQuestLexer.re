// questdata.txt scanner.
// re2c generates QuestHelperQuestLexer.cpp.

#include "L2Server/src/QuestHelperQuestLexer.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/QuestHelperQuestParser.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "QuestHelperQuestParserTokens.h"

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

grammar::SemanticBits ParseWindowsDoubleBits(const std::u16string_view value) {
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

CLexerForQuestHelperQuest::CLexerForQuestHelperQuest() :
    m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0), comment_count(0), string_mode(0), qstring() {
}

CLexerForQuestHelperQuest::~CLexerForQuestHelperQuest() { m_FileBuf = nullptr; }

int CLexerForQuestHelperQuest::Create(char16_t* const fileBuffer, const int fileSize) {
    m_nLastInput = 0;
    std::fill(std::begin(m_LastInput), std::end(m_LastInput), u'\0');
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

int CLexerForQuestHelperQuest::yygetchar() {
    if (m_nFilePointer >= m_nFileSize) {
        return -1;
    }
    const char16_t value = m_FileBuf[m_nFilePointer++];
    m_LastInput[m_nLastInput % 100] = value;
    ++m_nLastInput;
    return static_cast<int>(static_cast<std::uint16_t>(value));
}

/*!conditions:re2c*/

namespace {

// Original yybegin value / 2 -> re2c condition.
constexpr YYCONDTYPE kConditionByStart[] = {yycINITIAL, yycCOMMENT, yycMAIN, yycMAIN2};

} // namespace

int CLexerForQuestHelperQuest::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "quest_begin" { EndToken(); return TK_QUEST_BEGIN; }
            <MAIN> "quest_id" { EndToken(); return TK_QUEST_ID; }
            <MAIN> "quest_name" { EndToken(); return TK_QUEST_NAME; }
            <MAIN> "npc_begin" { EndToken(); return TK_NPC_BEGIN; }
            <MAIN> "npc_name" { EndToken(); return TK_NPC_NAME; }
            <MAIN> "talked_event_begin" { EndToken(); return TK_TALKED_EVENT_BEGIN; }
            <MAIN> "talked_event_end" { EndToken(); return TK_TALKED_EVENT_END; }
            <MAIN> "talk_selected_event_begin" { EndToken(); return TK_TALK_SELECTED_EVENT_BEGIN; }
            <MAIN> "talk_selected_event_end" { EndToken(); return TK_TALK_SELECTED_EVENT_END; }
            <MAIN> "condition" { EndToken(); return TK_CONDITION; }
            <MAIN> "choice_string" { EndToken(); return TK_CHOICE_STRING; }
            <MAIN> "handler" { EndToken(); return TK_HANDLER; }
            <MAIN> "quest_accepted_event_begin" { EndToken(); return TK_QUEST_ACCEPTED_EVENT_BEGIN; }
            <MAIN> "quest_accepted_event_end" { EndToken(); return TK_QUEST_ACCEPTED_EVENT_END; }
            <MAIN> "my_dying_event_begin" { EndToken(); return TK_MY_DYING_EVENT_BEGIN; }
            <MAIN> "my_dying_event_end" { EndToken(); return TK_MY_DYING_EVENT_END; }
            <MAIN> "timer_fired_event_begin" { EndToken(); return TK_TIMER_FIRED_EVENT_BEGIN; }
            <MAIN> "timer_fired_event_end" { EndToken(); return TK_TIMER_FIRED_EVENT_END; }
            <MAIN> "timer_fired_ex_event_begin" { EndToken(); return TK_TIMER_FIRED_EX_EVENT_BEGIN; }
            <MAIN> "timer_fired_ex_event_end" { EndToken(); return TK_TIMER_FIRED_EX_EVENT_END; }
            <MAIN> "created_event_begin" { EndToken(); return TK_CREATED_EVENT_BEGIN; }
            <MAIN> "created_event_end" { EndToken(); return TK_CREATED_EVENT_END; }
            <MAIN> "attacked_event_begin" { EndToken(); return TK_ATTACKED_EVENT_BEGIN; }
            <MAIN> "attacked_event_end" { EndToken(); return TK_ATTACKED_EVENT_END; }
            <MAIN> "spelled_event_begin" { EndToken(); return TK_SPELLED_EVENT_BEGIN; }
            <MAIN> "spelled_event_end" { EndToken(); return TK_SPELLED_EVENT_END; }
            <MAIN> "menu_selected_event_begin" { EndToken(); return TK_MENU_SELECTED_EVENT_BEGIN; }
            <MAIN> "menu_selected_event_end" { EndToken(); return TK_MENU_SELECTED_EVENT_END; }
            <MAIN> "script_event_begin" { EndToken(); return TK_SCRIPT_EVENT_BEGIN; }
            <MAIN> "script_event_end" { EndToken(); return TK_SCRIPT_EVENT_END; }
            <MAIN> "npc_end" { EndToken(); return TK_NPC_END; }
            <MAIN> "quest_end" { EndToken(); return TK_QUEST_END; }
            <MAIN> "quest_items" { EndToken(); return TK_QUEST_ITEMS; }
            <MAIN> "condition_target" { EndToken(); return TK_CONDITION_TARGET; }
            <MAIN> "execution_type" { EndToken(); return TK_EXECUTION_TYPE; }
            <MAIN> "always" {
                EndToken();
                *semanticSlot = 0;
                return TK_ALWAYS_GROUP;
            }
            <MAIN> "random1" {
                EndToken();
                *semanticSlot = 1;
                return TK_ALWAYS_GROUP;
            }
            <MAIN> "pc" {
                EndToken();
                *semanticSlot = 0;
                return TK_ALWAYS_GROUP;
            }
            <MAIN> "party_one" {
                EndToken();
                *semanticSlot = 1;
                return TK_ALWAYS_GROUP;
            }
            <MAIN> "party_all" {
                EndToken();
                *semanticSlot = 2;
                return TK_ALWAYS_GROUP;
            }
            <MAIN> "mpcc_all" {
                EndToken();
                *semanticSlot = 3;
                return TK_ALWAYS_GROUP;
            }
            <MAIN> "condition_range" { EndToken(); return TK_CONDITION_RANGE; }
            <MAIN> "-"? [0-9]+ {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    static_cast<std::int64_t>(ParseWindowsInt32(std::u16string_view(yytext, yyleng)))
                );
                return TK_ALWAYS_GROUP;
            }
            <MAIN> "$" ("0" | [1-9][0-9]*) {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    static_cast<std::int64_t>(ParseWindowsInt32(std::u16string_view(yytext + 1, yyleng - 1)))
                );
                return TK_ALWAYS_GROUP;
            }
            <MAIN> "-"{0,2} [0-9]+ "." [0-9]* {
                EndToken();
                *semanticSlot = ParseWindowsDoubleBits(std::u16string_view(yytext, yyleng));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> [&(),;=_{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
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
                const auto appendFirst = [this]() { qstring.append(std::u16string_view(yytext, 1)); };
                --string_mode;
                if (string_mode == 0) {
                    yybegin(4);
                    setPointer(DuplicateString(qstring));
                    return TK_RBRACKET;
                }
                appendFirst();
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
                ++line;
                continue;
            }
            <MAIN2> . {
                EndToken();
                const auto appendFirst = [this]() { qstring.append(std::u16string_view(yytext, 1)); };
                appendFirst();
                continue;
            }
            <MAIN> [a-zA-Z_][a-zA-Z0-9_<>]* {
                EndToken();
                setPointer(DuplicateText(yytext, yyleng));
                return TK_IDENTIFIER;
            }
            <MAIN> [ \t] { EndToken(); continue; }
            <MAIN> "\r" { EndToken(); continue; }
            <MAIN> "\n" {
                EndToken();
                ++line;
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
                    Log, "questdata.txt line %lld: quest helper skipped invalid input '%s' (first UTF-16 code unit 0x%04X).",
                    static_cast<long long>(line) + 1, invalid.c_str(),
                    static_cast<unsigned int>(static_cast<std::uint16_t>(*yytext))
                );
                continue;
            }
            <INITIAL> * { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN,MAIN2> $ { return 0; }
        */
    }
}
