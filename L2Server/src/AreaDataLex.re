// areadata.txt scanner (original L2Server/areadata_lex.l).
// re2c generates AreaDataLex.cpp.

#include "L2Server/src/AreaDataLex.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/AreaDataParser.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "AreaDataParserTokens.h"

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
    double parsed = 0.0;
    if (!ascii.empty() || value.empty()) {
        char* end = nullptr;
        parsed = std::strtod(ascii.c_str(), &end);
        if (end == ascii.c_str()) {
            parsed = 0.0;
        }
    }
    grammar::SemanticBits bits = 0;
    std::memcpy(&bits, &parsed, sizeof(bits));
    return bits;
}

char16_t* DuplicateInnerText(const char16_t* text, const int length) {
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

char16_t* DuplicateText(const char16_t* text, const int length) {
    auto* const duplicate = static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(length + 1) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, duplicate);
    duplicate[length] = u'\0';
    return duplicate;
}

} // namespace

CLexerForArea::CLexerForArea() :
    line(0), comment_count(0), m_FileBuf(nullptr), m_pRawFileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0) {
}

CLexerForArea::~CLexerForArea() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
}

int CLexerForArea::Create(char16_t* const fileBuffer, const int fileSize) {
    // Current L2Server 0x00c712e8.
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

int CLexerForArea::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "area_begin" { EndToken(); return TK_AREA_BEGIN; }
            <MAIN> "area_end" { EndToken(); return TK_AREA_END; }
            <MAIN> "type" { EndToken(); return TK_TYPE; }
            <MAIN> "name" { EndToken(); return TK_NAME; }
            <MAIN> "instantzone_id" { EndToken(); return TK_INSTANTZONE_ID; }
            <MAIN> "enter_inzone_id" { EndToken(); return TK_ENTER_INZONE_ID; }
            <MAIN> "move_bonus" { EndToken(); return TK_MOVE_BONUS; }
            <MAIN> "message_no" { EndToken(); return TK_MESSAGE_NO; }
            <MAIN> "entering_message_no" { EndToken(); return TK_ENTERING_MESSAGE_NO; }
            <MAIN> "leaving_message_no" { EndToken(); return TK_LEAVING_MESSAGE_NO; }
            <MAIN> "damage_on_hp" { EndToken(); return TK_DAMAGE_ON_HP; }
            <MAIN> "damage_on_mp" { EndToken(); return TK_DAMAGE_ON_MP; }
            <MAIN> "hp_regen_bonus" { EndToken(); return TK_HP_REGEN_BONUS; }
            <MAIN> "mp_regen_bonus" { EndToken(); return TK_MP_REGEN_BONUS; }
            <MAIN> "range" { EndToken(); return TK_RANGE; }
            <MAIN> "banned_range" { EndToken(); return TK_BANNED_RANGE; }
            <MAIN> "teleport_range" { EndToken(); return TK_TELEPORT_RANGE; }
            <MAIN> "teleport_points" { EndToken(); return TK_TELEPORT_POINTS; }
            <MAIN> "default_status" { EndToken(); return TK_DEFAULT_STATUS; }
            <MAIN> "show_dangerzone" { EndToken(); return TK_SHOW_DANGERZONE; }
            <MAIN> "event_id" { EndToken(); return TK_EVENT_ID; }
            <MAIN> "initial_delay" { EndToken(); return TK_INITIAL_DELAY; }
            <MAIN> "on_time" { EndToken(); return TK_ON_TIME; }
            <MAIN> "off_time" { EndToken(); return TK_OFF_TIME; }
            <MAIN> "random_time" { EndToken(); return TK_RANDOM_TIME; }
            <MAIN> "exp_penalty_per" { EndToken(); return TK_EXP_PENALTY_PER; }
            <MAIN> "item_drop" { EndToken(); return TK_ITEM_DROP; }
            <MAIN> "hpmp_regen_on_levelup" { EndToken(); return TK_HPMP_REGEN_ON_LEVELUP; }
            <MAIN> "skill_name" { EndToken(); return TK_SKILL_NAME; }
            <MAIN> "skill_prob" { EndToken(); return TK_SKILL_PROB; }
            <MAIN> "unit_tick" { EndToken(); return TK_UNIT_TICK; }
            <MAIN> "skill_list" { EndToken(); return TK_SKILL_LIST; }
            <MAIN> "skill_action_type" { EndToken(); return TK_SKILL_ACTION_TYPE; }
            <MAIN> "water_range" { EndToken(); return TK_WATER_RANGE; }
            <MAIN> "map_no" { EndToken(); return TK_MAP_NO; }
            <MAIN> "target" { EndToken(); return TK_TARGET; }
            <MAIN> "affect_race" { EndToken(); return TK_AFFECT_RACE; }
            <MAIN> "ssq" { EndToken(); return TK_SSQ; }
            <MAIN> "restart_time" { EndToken(); return TK_RESTART_TIME; }
            <MAIN> "restart_allowed_time" { EndToken(); return TK_RESTART_ALLOWED_TIME; }
            <MAIN> "restart_points" { EndToken(); return TK_RESTART_POINTS; }
            <MAIN> "flymove_info" { EndToken(); return TK_FLYMOVE_INFO; }
            <MAIN> "blocked_actions" { EndToken(); return TK_BLOCKED_ACTIONS; }
            <MAIN> "priv_store" {
                EndToken();
                *lval = 0;
                return TK_PRIV_VALUE;
            }
            <MAIN> "priv_rstore" {
                EndToken();
                *lval = 1;
                return TK_PRIV_VALUE;
            }
            <MAIN> "random1" {
                EndToken();
                *lval = 0;
                return TK_RANDOM1_SERIAL;
            }
            <MAIN> "serial" {
                EndToken();
                *lval = 1;
                return TK_RANDOM1_SERIAL;
            }
            <MAIN> "on" {
                EndToken();
                *lval = 1;
                return TK_ON_OFF;
            }
            <MAIN> "off" {
                EndToken();
                *lval = 0;
                return TK_ON_OFF;
            }
            <MAIN> "ssq_zone" {
                EndToken();
                *lval = 12;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "battle_zone" {
                EndToken();
                *lval = 0;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "siege_zone" {
                EndToken();
                *lval = 20;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "peace_zone" {
                EndToken();
                *lval = 2;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "water" {
                EndToken();
                *lval = 3;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "damage" {
                EndToken();
                *lval = 8;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "poison" {
                EndToken();
                *lval = 9;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "swamp" {
                EndToken();
                *lval = 5;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "mother_tree" {
                EndToken();
                *lval = 6;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "no_restart" {
                EndToken();
                *lval = 7;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "teleport" {
                EndToken();
                *lval = 10;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "instant_skill" {
                EndToken();
                *lval = 11;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "airport" {
                EndToken();
                *lval = 13;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "inzone_enter" {
                EndToken();
                *lval = 14;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "inzone_leave" {
                EndToken();
                *lval = 15;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "flymove" {
                EndToken();
                *lval = 16;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "shuttle" {
                EndToken();
                *lval = 17;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "dragon_lair" {
                EndToken();
                *lval = 18;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "no_skill" {
                EndToken();
                *lval = 19;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "fishing" {
                EndToken();
                *lval = 21;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "training_zone" {
                EndToken();
                *lval = 22;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "only_pve" {
                EndToken();
                *lval = 4;
                return TK_SSQ_ZONE_GROUP;
            }
            <MAIN> "=" { EndToken(); return TK_EQUAL; }
            <MAIN> "min" { EndToken(); return TK_MIN_KW; }
            <MAIN> "sec" { EndToken(); return TK_SEC; }
            <MAIN> "hour" { EndToken(); return TK_HOUR; }
            <MAIN> "day" { EndToken(); return TK_DAY; }
            <MAIN> "union_as_party" { EndToken(); return TK_UNION_AS_PARTY; }
            <MAIN> "mpcc_as_party" { EndToken(); return TK_MPCC_AS_PARTY; }
            <MAIN> "-"? [0-9]+ {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_RANDOM1_SERIAL;
            }
            <MAIN> "$" ("0" | [1-9][0-9]*) {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext + 1, static_cast<std::size_t>(yyleng - 1)))
                );
                return TK_RANDOM1_SERIAL;
            }
            <MAIN> "-"{0,2} [0-9]+ "." [0-9]* {
                EndToken();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> [&()/:;{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_NAME_2;
            }
            <MAIN> [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                char16_t* const text = DuplicateText(yytext, yyleng);
                if (text != nullptr) {
                    *lval = grammar::PointerSemantic(text);
                }
                return TK_IDENTIFIER;
            }
            <MAIN> "@" [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                *lval = static_cast<grammar::SemanticBits>(g_PrecompiledHeaderDB.Search(yytext + 1));
                return TK_RANDOM1_SERIAL;
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
                //     static_cast<int>(
                //         static_cast<std::uint16_t>(*yytext)))
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char[%c]",
                //     static_cast<int>(
                //         static_cast<std::uint16_t>(*yytext)))
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
