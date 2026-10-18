// CLexerForDoor scanner (original L2Server/lexer_door.l).
// re2c generates LexerDoor.cpp.

#include "L2Server/src/LexerDoor.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserDoor.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserDoorTokens.h"

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

char16_t* DuplicateFullText(const char16_t* const text, const int length) {
    auto* const duplicate = static_cast<char16_t*>(std::malloc((static_cast<std::size_t>(length) + 1) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, duplicate);
    duplicate[length] = u'\0';
    return duplicate;
}

} // namespace

CLexerForDoor::CLexerForDoor() :
    m_FileBuf(nullptr),
    m_pRawFileBuf(nullptr),
    m_nFilePointer(0),
    m_nFileSize(0),
    line(0),
    m_bFirstTime(false),
    m_alignment0ddTo0e0{} {
}

CLexerForDoor::~CLexerForDoor() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
}

int CLexerForDoor::Create(char16_t* const fileBuffer, const int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

int CLexerForDoor::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_FileBuf one code unit at a time.
        SetInput(
            m_FileBuf + m_nFilePointer,
            m_nFilePointer < m_nFileSize ? static_cast<std::size_t>(m_nFileSize - m_nFilePointer) : 0
        );
    }
    auto* const semanticSlot = lval;
    const auto setSmallSemantic = [semanticSlot](const int value) {
    *semanticSlot = static_cast<grammar::SemanticBits>(value);
};
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "/*" .* "*/" { EndToken(); continue; }
            "door_begin" { EndToken(); return TK_DOOR_BEGIN; }
            "door_end" { EndToken(); return TK_DOOR_END; }
            "chair_begin" { EndToken(); return TK_CHAIR_BEGIN; }
            "chair_end" { EndToken(); return TK_CHAIR_END; }
            "chair_group_id" { EndToken(); return TK_CHAIR_GROUP_ID; }
            "sit_range" { EndToken(); return TK_SIT_RANGE; }
            "signboard_begin" { EndToken(); return TK_SIGNBOARD_BEGIN; }
            "signboard_end" { EndToken(); return TK_SIGNBOARD_END; }
            "controltower_begin" { EndToken(); return TK_CONTROLTOWER_BEGIN; }
            "controltower_end" { EndToken(); return TK_CONTROLTOWER_END; }
            "control_area" { EndToken(); return TK_CONTROL_AREA; }
            "controltower_type" { EndToken(); return TK_CONTROLTOWER_TYPE; }
            "trap_begin" { EndToken(); return TK_TRAP_BEGIN; }
            "trap_end" { EndToken(); return TK_TRAP_END; }
            "trap_area" { EndToken(); return TK_TRAP_AREA; }
            "life_control" {
                EndToken();
                setSmallSemantic(1);
                return TK_LIFE_CONTROL_TRAP_CONTROL_SUPPORT_CONTROL;
            }
            "trap_control" {
                EndToken();
                setSmallSemantic(2);
                return TK_LIFE_CONTROL_TRAP_CONTROL_SUPPORT_CONTROL;
            }
            "support_control" {
                EndToken();
                setSmallSemantic(3);
                return TK_LIFE_CONTROL_TRAP_CONTROL_SUPPORT_CONTROL;
            }
            "editor_id" { EndToken(); return TK_EDITOR_ID; }
            "toggle" { EndToken(); return TK_TOGGLE; }
            "html" { EndToken(); return TK_HTML; }
            "display_npc_working" { EndToken(); return TK_DISPLAY_NPC_WORKING; }
            "display_npc_not_working" { EndToken(); return TK_DISPLAY_NPC_NOT_WORKING; }
            "display_npc" { EndToken(); return TK_DISPLAY_NPC; }
            "master_name" { EndToken(); return TK_MASTER_NAME; }
            "master_open_event" { EndToken(); return TK_MASTER_OPEN_EVENT; }
            "master_close_event" { EndToken(); return TK_MASTER_CLOSE_EVENT; }
            "default_status" { EndToken(); return TK_DEFAULT_STATUS; }
            "act_open" {
                EndToken();
                setSmallSemantic(1);
                return TK_ACT_VALUE;
            }
            "act_close" {
                EndToken();
                setSmallSemantic(2);
                return TK_ACT_VALUE;
            }
            "act_none" {
                EndToken();
                setSmallSemantic(0);
                return TK_ACT_VALUE;
            }
            "pos" { EndToken(); return TK_POS; }
            "range" { EndToken(); return TK_RANGE; }
            "height" { EndToken(); return TK_HEIGHT; }
            "hp" { EndToken(); return TK_HP; }
            "physical_defence" { EndToken(); return TK_PHYSICAL_DEFENCE; }
            "magical_defence" { EndToken(); return TK_MAGICAL_DEFENCE; }
            "type" { EndToken(); return TK_TYPE; }
            "normal_type" {
                EndToken();
                setSmallSemantic(0);
                return TK_NORMAL_TYPE_GROUP;
            }
            "parent_type" {
                EndToken();
                setSmallSemantic(1);
                return TK_NORMAL_TYPE_GROUP;
            }
            "child_type" {
                EndToken();
                setSmallSemantic(2);
                return TK_NORMAL_TYPE_GROUP;
            }
            "wall_type" {
                EndToken();
                setSmallSemantic(3);
                return TK_NORMAL_TYPE_GROUP;
            }
            "parent" { EndToken(); return TK_PARENT; }
            "is_open" { EndToken(); return TK_IS_OPEN; }
            "level" { EndToken(); return TK_LEVEL; }
            "open_method" { EndToken(); return TK_OPEN_METHOD; }
            "static_object_id" { EndToken(); return TK_STATIC_OBJECT_ID; }
            "by_skill" {
                EndToken();
                setSmallSemantic(1);
                return TK_BY_VALUE;
            }
            "by_npc" {
                EndToken();
                setSmallSemantic(0);
                return TK_BY_VALUE;
            }
            "by_click" {
                EndToken();
                setSmallSemantic(2);
                return TK_BY_VALUE;
            }
            "by_time" {
                EndToken();
                setSmallSemantic(3);
                return TK_BY_VALUE;
            }
            "by_time_skill" {
                EndToken();
                setSmallSemantic(5);
                return TK_BY_VALUE;
            }
            "by_cycle" {
                EndToken();
                setSmallSemantic(6);
                return TK_BY_VALUE;
            }
            "by_item" {
                EndToken();
                setSmallSemantic(8);
                return TK_BY_VALUE;
            }
            "by_skill_item" {
                EndToken();
                setSmallSemantic(7);
                return TK_BY_VALUE;
            }
            "open_by_door_name_skill" { EndToken(); return TK_OPEN_BY_DOOR_NAME_SKILL; }
            "close_time" { EndToken(); return TK_CLOSE_TIME; }
            "open_time" { EndToken(); return TK_OPEN_TIME; }
            "random_time" { EndToken(); return TK_RANDOM_TIME; }
            "map_pos" { EndToken(); return TK_MAP_POS; }
            "texture_name" { EndToken(); return TK_TEXTURE_NAME; }
            "observer_pos" { EndToken(); return TK_OBSERVER_POS; }
            "observer_group" { EndToken(); return TK_OBSERVER_GROUP; }
            "hp_showable" { EndToken(); return TK_HP_SHOWABLE; }
            "inactive_time" { EndToken(); return TK_INACTIVE_TIME; }
            "instantzone_id" { EndToken(); return TK_INSTANTZONE_ID; }
            "emitter_id" { EndToken(); return TK_EMITTER_ID; }
            "targetable" { EndToken(); return TK_TARGETABLE; }
            "stealth" { EndToken(); return TK_STEALTH; }
            "check_collision" { EndToken(); return TK_CHECK_COLLISION; }
            "need_to_log" { EndToken(); return TK_NEED_TO_LOG; }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            "$" ("0" | [1-9][0-9]*) {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext + 1, static_cast<std::size_t>(yyleng - 1)))
                );
                return TK_INTEGER;
            }
            [,;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            "[" [^[\]]* "]" {
                EndToken();
                char16_t* const duplicate = DuplicateInnerText(yytext, yyleng);
                if (duplicate != nullptr) {
                    *semanticSlot = grammar::PointerSemantic(duplicate);
                }
                return TK_NAME;
            }
            [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                char16_t* const duplicate = DuplicateFullText(yytext, yyleng);
                *semanticSlot = grammar::PointerSemantic(duplicate);
                return TK_IDENTIFIER;
            }
            [ \t\r] { EndToken(); continue; }
            "\n" {
                EndToken();
                ++line;
                continue;
            }
            . {
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
                    Log, L"Door/trap data line %lld: invalid token '%ls'; the token is skipped.",
                    static_cast<long long>(line) + 1, invalid.c_str()
                );
                continue;
            }
            $ { return 0; }
        */
    }
}
