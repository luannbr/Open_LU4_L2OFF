// CLexerForResidence scanner (original L2Server/castle_lex.l).
// re2c generates CastleLex.cpp.

#include "L2Server/src/CastleLex.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/CastleParser.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

#include "CastleParserTokens.h"

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
        return parsed < 0 ? std::numeric_limits<std::int64_t>::min() : std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(parsed);
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

CLexerForResidence::CLexerForResidence() : m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0), comment_count(0) {
}

CLexerForResidence::~CLexerForResidence() { m_FileBuf = nullptr; }

int CLexerForResidence::Create(char16_t* const fileBuffer, const int fileSize) {
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

int CLexerForResidence::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "castle_siege_music_begin" { EndToken(); return TK_CASTLE_SIEGE_MUSIC_BEGIN; }
            <MAIN> "castle_siege_music_end" { EndToken(); return TK_CASTLE_SIEGE_MUSIC_END; }
            <MAIN> "fortress_siege_music_begin" { EndToken(); return TK_FORTRESS_SIEGE_MUSIC_BEGIN; }
            <MAIN> "fortress_siege_music_end" { EndToken(); return TK_FORTRESS_SIEGE_MUSIC_END; }
            <MAIN> "castle_begin" { EndToken(); return TK_CASTLE_BEGIN; }
            <MAIN> "agit_begin" { EndToken(); return TK_AGIT_BEGIN; }
            <MAIN> "fortress_begin" { EndToken(); return TK_FORTRESS_BEGIN; }
            <MAIN> "dominion_begin" { EndToken(); return TK_DOMINION_BEGIN; }
            <MAIN> "residence_territory" { EndToken(); return TK_RESIDENCE_TERRITORY; }
            <MAIN> "battlefield_territory" { EndToken(); return TK_BATTLEFIELD_TERRITORY; }
            <MAIN> "headquarter_territory" { EndToken(); return TK_HEADQUARTER_TERRITORY; }
            <MAIN> "owner_restart_territory" { EndToken(); return TK_OWNER_RESTART_TERRITORY; }
            <MAIN> "domain_territory" { EndToken(); return TK_DOMAIN_TERRITORY; }
            <MAIN> "manor_territory" { EndToken(); return TK_MANOR_TERRITORY; }
            <MAIN> "owner_restart_point_list" { EndToken(); return TK_OWNER_RESTART_POINT_LIST; }
            <MAIN> "other_restart_village_list" { EndToken(); return TK_OTHER_RESTART_VILLAGE_LIST; }
            <MAIN> "banish_point_list" { EndToken(); return TK_BANISH_POINT_LIST; }
            <MAIN> "chao_restart_point_list" { EndToken(); return TK_CHAO_RESTART_POINT_LIST; }
            <MAIN> "mercenary_point_list" { EndToken(); return TK_MERCENARY_POINT_LIST; }
            <MAIN> "owning_npc_list" { EndToken(); return TK_OWNING_NPC_LIST; }
            <MAIN> "boss_npc_list" { EndToken(); return TK_BOSS_NPC_LIST; }
            <MAIN> "next_siege" { EndToken(); return TK_NEXT_SIEGE; }
            <MAIN> "siege_term" { EndToken(); return TK_SIEGE_TERM; }
            <MAIN> "residence_state" { EndToken(); return TK_RESIDENCE_STATE; }
            <MAIN> "residence_gate_list" { EndToken(); return TK_RESIDENCE_GATE_LIST; }
            <MAIN> "control_tower_list" { EndToken(); return TK_CONTROL_TOWER_LIST; }
            <MAIN> "breakable_wall_list" { EndToken(); return TK_BREAKABLE_WALL_LIST; }
            <MAIN> "holything" { EndToken(); return TK_HOLYTHING; }
            <MAIN> "ownthing" { EndToken(); return TK_OWNTHING; }
            <MAIN> "sys_castle_begin" { EndToken(); return TK_SYS_CASTLE_BEGIN; }
            <MAIN> "sys_castle_end" { EndToken(); return TK_SYS_CASTLE_END; }
            <MAIN> "good_castle_begin" { EndToken(); return TK_GOOD_CASTLE_BEGIN; }
            <MAIN> "good_castle_end" { EndToken(); return TK_GOOD_CASTLE_END; }
            <MAIN> "bad_castle_begin" { EndToken(); return TK_BAD_CASTLE_BEGIN; }
            <MAIN> "bad_castle_end" { EndToken(); return TK_BAD_CASTLE_END; }
            <MAIN> "tax_rate" { EndToken(); return TK_TAX_RATE; }
            <MAIN> "tax_sysget_rate" { EndToken(); return TK_TAX_SYSGET_RATE; }
            <MAIN> "tribute_rate" { EndToken(); return TK_TRIBUTE_RATE; }
            <MAIN> "buy_tax_rate" { EndToken(); return TK_BUY_TAX_RATE; }
            <MAIN> "buy_tax_sysget_rate" { EndToken(); return TK_BUY_TAX_SYSGET_RATE; }
            <MAIN> "buy_tribute_rate" { EndToken(); return TK_BUY_TRIBUTE_RATE; }
            <MAIN> "editor_pos" { EndToken(); return TK_EDITOR_POS; }
            <MAIN> "editor_id" { EndToken(); return TK_EDITOR_ID; }
            <MAIN> "basic_rate" { EndToken(); return TK_BASIC_RATE; }
            <MAIN> "residence_guard_mapping" { EndToken(); return TK_RESIDENCE_GUARD_MAPPING; }
            <MAIN> "agit_auction" { EndToken(); return TK_AGIT_AUCTION; }
            <MAIN> "next_auction" { EndToken(); return TK_NEXT_AUCTION; }
            <MAIN> "agit_auction_min" { EndToken(); return TK_AGIT_AUCTION_MIN; }
            <MAIN> "agit_deposit" { EndToken(); return TK_AGIT_DEPOSIT; }
            <MAIN> "agit_lease" { EndToken(); return TK_AGIT_LEASE; }
            <MAIN> "agit_size" { EndToken(); return TK_AGIT_SIZE; }
            <MAIN> "agit_parent_castleid" { EndToken(); return TK_AGIT_PARENT_CASTLEID; }
            <MAIN> "battle_royal_flag_point_list" { EndToken(); return TK_BATTLE_ROYAL_FLAG_POINT_LIST; }
            <MAIN> "final_flag_point_list" { EndToken(); return TK_FINAL_FLAG_POINT_LIST; }
            <MAIN> "final_open_door_list" { EndToken(); return TK_FINAL_OPEN_DOOR_LIST; }
            <MAIN> "door_list" { EndToken(); return TK_DOOR_LIST; }
            <MAIN> "agit_end" { EndToken(); return TK_AGIT_END; }
            <MAIN> "castle_end" { EndToken(); return TK_CASTLE_END; }
            <MAIN> "fortress_end" { EndToken(); return TK_FORTRESS_END; }
            <MAIN> "dominion_end" { EndToken(); return TK_DOMINION_END; }
            <MAIN> "challenger_restart_point_list" { EndToken(); return TK_CHALLENGER_RESTART_POINT_LIST; }
            <MAIN> "agit_entrance_point" { EndToken(); return TK_AGIT_ENTRANCE_POINT; }
            <MAIN> "agit_preparatory_term" { EndToken(); return TK_AGIT_PREPARATORY_TERM; }
            <MAIN> "agit_progress_term" { EndToken(); return TK_AGIT_PROGRESS_TERM; }
            <MAIN> "agit_close_term" { EndToken(); return TK_AGIT_CLOSE_TERM; }
            <MAIN> "agit_delete_item_list" { EndToken(); return TK_AGIT_DELETE_ITEM_LIST; }
            <MAIN> "residence_skill_list" { EndToken(); return TK_RESIDENCE_SKILL_LIST; }
            <MAIN> "dominion_main_castle" { EndToken(); return TK_DOMINION_MAIN_CASTLE; }
            <MAIN> "pickup_ownthing" { EndToken(); return TK_PICKUP_OWNTHING; }
            <MAIN> "hide_name_item" { EndToken(); return TK_HIDE_NAME_ITEM; }
            <MAIN> "agit_compete_day" { EndToken(); return TK_AGIT_COMPETE_DAY; }
            <MAIN> "agit_compete_hour" { EndToken(); return TK_AGIT_COMPETE_HOUR; }
            <MAIN> "agit_compete_min" { EndToken(); return TK_AGIT_COMPETE_MIN; }
            <MAIN> "agit_compete_progress" { EndToken(); return TK_AGIT_COMPETE_PROGRESS; }
            <MAIN> "fortress_scale" { EndToken(); return TK_FORTRESS_SCALE; }
            <MAIN> "fortress_related_castle" { EndToken(); return TK_FORTRESS_RELATED_CASTLE; }
            <MAIN> "fortress_flagpole" { EndToken(); return TK_FORTRESS_FLAGPOLE; }
            <MAIN> "fortress_flag" { EndToken(); return TK_FORTRESS_FLAG; }
            <MAIN> "residence_grade" { EndToken(); return TK_RESIDENCE_GRADE; }
            <MAIN> "residence_domains" { EndToken(); return TK_RESIDENCE_DOMAINS; }
            <MAIN> "deco_npc_slots" { EndToken(); return TK_DECO_NPC_SLOTS; }
            <MAIN> "castle_common_begin" { EndToken(); return TK_CASTLE_COMMON_BEGIN; }
            <MAIN> "castle_common_end" { EndToken(); return TK_CASTLE_COMMON_END; }
            <MAIN> "base_siege_time" { EndToken(); return TK_BASE_SIEGE_TIME; }
            <MAIN> "reservable_siege_time" { EndToken(); return TK_RESERVABLE_SIEGE_TIME; }
            <MAIN> "max_concurrent_siege" { EndToken(); return TK_MAX_CONCURRENT_SIEGE; }
            <MAIN> "observer_import" { EndToken(); return TK_OBSERVER_IMPORT; }
            <MAIN> "SAT" {
                EndToken();
                *lval = 6;
                return TK_SAT_SUN;
            }
            <MAIN> "SUN" {
                EndToken();
                *lval = 0;
                return TK_SAT_SUN;
            }
            <MAIN> "siege" {
                EndToken();
                *lval = 0;
                return TK_SIEGE_GROUP;
            }
            <MAIN> "npc_battle" {
                EndToken();
                *lval = 1;
                return TK_SIEGE_GROUP;
            }
            <MAIN> "team_battle" {
                EndToken();
                *lval = 2;
                return TK_SIEGE_GROUP;
            }
            <MAIN> "auction" {
                EndToken();
                *lval = 3;
                return TK_SIEGE_GROUP;
            }
            <MAIN> "minigame" {
                EndToken();
                *lval = 4;
                return TK_SIEGE_GROUP;
            }
            <MAIN> "compete" {
                EndToken();
                *lval = 5;
                return TK_SIEGE_GROUP;
            }
            <MAIN> "basecamp" {
                EndToken();
                *lval = 6;
                return TK_SIEGE_GROUP;
            }
            <MAIN> "teleporter" {
                EndToken();
                *lval = 0;
                return TK_TELEPORTER_GROUP;
            }
            <MAIN> "wizard" {
                EndToken();
                *lval = 1;
                return TK_TELEPORTER_GROUP;
            }
            <MAIN> "cleric" {
                EndToken();
                *lval = 2;
                return TK_TELEPORTER_GROUP;
            }
            <MAIN> "sword" {
                EndToken();
                *lval = 3;
                return TK_TELEPORTER_GROUP;
            }
            <MAIN> "pole" {
                EndToken();
                *lval = 4;
                return TK_TELEPORTER_GROUP;
            }
            <MAIN> "bow" {
                EndToken();
                *lval = 5;
                return TK_TELEPORTER_GROUP;
            }
            <MAIN> "notmove" {
                EndToken();
                *lval = 0;
                return TK_NOTMOVE_MOVE;
            }
            <MAIN> "move" {
                EndToken();
                *lval = 1;
                return TK_NOTMOVE_MOVE;
            }
            <MAIN> "ssq_normal" {
                EndToken();
                *lval = 0;
                return TK_SSQ_VALUE;
            }
            <MAIN> "ssq_dawn" {
                EndToken();
                *lval = 2;
                return TK_SSQ_VALUE;
            }
            <MAIN> "ssq_twilight" {
                EndToken();
                *lval = 1;
                return TK_SSQ_VALUE;
            }
            <MAIN> "initial" {
                EndToken();
                *lval = 0;
                return TK_INITIAL_NORMAL;
            }
            <MAIN> "normal" {
                EndToken();
                *lval = 0;
                return TK_INITIAL_NORMAL;
            }
            <MAIN> "=" { EndToken(); return TK_EQUAL; }
            <MAIN> "-"? [0-9]+ {
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
            <MAIN> "-"{0,2} [0-9]+ "." [0-9]* {
                EndToken();
                *lval = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> [&/;{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <MAIN> "[" [^[\]]* "]" {
                EndToken();
                char16_t* const duplicate = DuplicateInnerText(yytext, yyleng);
                if (duplicate != nullptr) {
                    *lval = grammar::PointerSemantic(duplicate);
                }
                return TK_NAME;
            }
            <MAIN> [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                char16_t* const duplicate = DuplicateText(yytext, yyleng);
                *lval = grammar::PointerSemantic(duplicate);
                return TK_IDENTIFIER;
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
                //     static_cast<wchar_t>(*yytext))
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char[%c]",
                //     static_cast<wchar_t>(*yytext))
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
