// CLexerForInstantZone scanner (original L2Server/lexer_instantzone.l).
// re2c generates LexerInstantZone.cpp.

#include "L2Server/src/LexerInstantZone.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserInstantZone.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <string>
#include <string_view>

#include "ParserInstantZoneTokens.h"

namespace {

// Exact generated images referenced by
// CLexerForInstantZone::CLexerForInstantZone@0x00c84ff4 in current
// L2Server.exe.

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

CLexerForInstantZone::CLexerForInstantZone() :
    m_FileBuf(nullptr), m_pRawFileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0), comment_count(0) {
}

CLexerForInstantZone::~CLexerForInstantZone() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
}

int CLexerForInstantZone::Create(char16_t* fileBuffer, int fileSize) {
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

int CLexerForInstantZone::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "instantzone_begin" { EndToken(); return TK_INSTANTZONE_BEGIN; }
            <MAIN> "instantzone_end" { EndToken(); return TK_INSTANTZONE_END; }
            <MAIN> "olympiadzone_begin" { EndToken(); return TK_OLYMPIADZONE_BEGIN; }
            <MAIN> "olympiadzone_end" { EndToken(); return TK_OLYMPIADZONE_END; }
            <MAIN> "pledgegamezone_begin" { EndToken(); return TK_PLEDGEGAMEZONE_BEGIN; }
            <MAIN> "pledgegamezone_end" { EndToken(); return TK_PLEDGEGAMEZONE_END; }
            <MAIN> "instantagitzone_begin" { EndToken(); return TK_INSTANTAGITZONE_BEGIN; }
            <MAIN> "instantagitzone_end" { EndToken(); return TK_INSTANTAGITZONE_END; }
            <MAIN> "pledgeraidzone_begin" { EndToken(); return TK_PLEDGERAIDZONE_BEGIN; }
            <MAIN> "pledgeraidzone_end" { EndToken(); return TK_PLEDGERAIDZONE_END; }
            <MAIN> "cluster_id" { EndToken(); return TK_CLUSTER_ID; }
            <MAIN> "id" { EndToken(); return TK_ID; }
            <MAIN> "name" { EndToken(); return TK_NAME; }
            <MAIN> "max_channel" { EndToken(); return TK_MAX_CHANNEL; }
            <MAIN> "max_user" { EndToken(); return TK_MAX_USER; }
            <MAIN> "min_user" { EndToken(); return TK_MIN_USER; }
            <MAIN> "duration" { EndToken(); return TK_DURATION; }
            <MAIN> "time_limit" { EndToken(); return TK_TIME_LIMIT; }
            <MAIN> "entrance_cond" { EndToken(); return TK_ENTRANCE_COND; }
            <MAIN> "delete_no_user" { EndToken(); return TK_DELETE_NO_USER; }
            <MAIN> "required_item" { EndToken(); return TK_REQUIRED_ITEM; }
            <MAIN> "deleted_item" { EndToken(); return TK_DELETED_ITEM; }
            <MAIN> "required_point" { EndToken(); return TK_REQUIRED_POINT; }
            <MAIN> "start_pos_type" { EndToken(); return TK_START_POS_TYPE; }
            <MAIN> "start_pos" { EndToken(); return TK_START_POS; }
            <MAIN> "order_end_pos_type" { EndToken(); return TK_ORDER_END_POS_TYPE; }
            <MAIN> "order_end_pos" { EndToken(); return TK_ORDER_END_POS; }
            <MAIN> "disorder_end_pos_type" { EndToken(); return TK_DISORDER_END_POS_TYPE; }
            <MAIN> "disorder_end_pos" { EndToken(); return TK_DISORDER_END_POS; }
            <MAIN> "door_list" { EndToken(); return TK_DOOR_LIST; }
            <MAIN> "area_list" { EndToken(); return TK_AREA_LIST; }
            <MAIN> "spawn_npc" { EndToken(); return TK_SPAWN_NPC; }
            <MAIN> "associative" { EndToken(); return TK_ASSOCIATIVE; }
            <MAIN> "todolist_check" { EndToken(); return TK_TODOLIST_CHECK; }
            <MAIN> "today_priority" { EndToken(); return TK_TODAY_PRIORITY; }
            <MAIN> "check_level" { EndToken(); return TK_CHECK_LEVEL; }
            <MAIN> "check_quest" { EndToken(); return TK_CHECK_QUEST; }
            <MAIN> "check_pledge" { EndToken(); return TK_CHECK_PLEDGE; }
            <MAIN> "check_party_chair" { EndToken(); return TK_CHECK_PARTY_CHAIR; }
            <MAIN> "check_exclusive_point_inzone" { EndToken(); return TK_CHECK_EXCLUSIVE_POINT_INZONE; }
            <MAIN> "designatory" {
                EndToken();
                *lval = 0;
                return TK_DESIGNATORY_RANDOM;
            }
            <MAIN> "random" {
                EndToken();
                *lval = 1;
                return TK_DESIGNATORY_RANDOM;
            }
            <MAIN> "nomination" {
                EndToken();
                *lval = 0;
                return TK_NOMINATION_ORIGIN_ORIGIN_RESTART;
            }
            <MAIN> "origin" {
                EndToken();
                *lval = 1;
                return TK_NOMINATION_ORIGIN_ORIGIN_RESTART;
            }
            <MAIN> "origin_restart" {
                EndToken();
                *lval = 2;
                return TK_NOMINATION_ORIGIN_ORIGIN_RESTART;
            }
            <MAIN> "reset_binding" { EndToken(); return TK_RESET_BINDING; }
            <MAIN> "start_binding" { EndToken(); return TK_START_BINDING; }
            <MAIN> "remove_buff" { EndToken(); return TK_REMOVE_BUFF; }
            <MAIN> "max_party" { EndToken(); return TK_MAX_PARTY; }
            <MAIN> "group_begin" { EndToken(); return TK_GROUP_BEGIN; }
            <MAIN> "group_end" { EndToken(); return TK_GROUP_END; }
            <MAIN> "group_id" { EndToken(); return TK_GROUP_ID; }
            <MAIN> "max_entrance" { EndToken(); return TK_MAX_ENTRANCE; }
            <MAIN> "extra_entrance" { EndToken(); return TK_EXTRA_ENTRANCE; }
            <MAIN> "instantzone_list" { EndToken(); return TK_INSTANTZONE_LIST; }
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
            <MAIN> "0" [0-9] {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            <MAIN> [(),:;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const duplicate = DuplicateInnerText(yytext, yyleng);
                if (duplicate != nullptr) {
                    *lval = grammar::PointerSemantic(duplicate);
                }
                return TK_NAME_2;
            }
            <MAIN> [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                char16_t* const duplicate = DuplicateFullText(yytext, yyleng);
                *lval = grammar::PointerSemantic(duplicate);
                return TK_IDENTIFIER;
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
                // Log.Add(
                //     LOG_ERROR,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     invalid.c_str())
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
