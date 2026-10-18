// npcdata.txt scanner (original L2Server/lexer_npc.l).
// re2c generates LexerNPC.cpp.

#include "L2Server/src/LexerNPC.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ParserNPC.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserNPCTokens.h"

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
    if (errno == ERANGE || parsed > LLONG_MAX) {
        return LLONG_MAX;
    }
    if (parsed < LLONG_MIN) {
        return LLONG_MIN;
    }
    return static_cast<std::int64_t>(parsed);
}

std::uint64_t ParseWindowsDoubleBits(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    char* end = nullptr;
    const double parsed = std::strtod(ascii.c_str(), &end);
    const double normalized = end == ascii.c_str() ? 0.0 : parsed;
    std::uint64_t bits = 0;
    std::memcpy(&bits, &normalized, sizeof(bits));
    return bits;
}

char16_t* DuplicateInnerText(const char16_t* text, int length) {
    if (text == nullptr || length < 2) {
        return nullptr;
    }
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

char16_t* DuplicateText(const char16_t* text, int length) {
    if (text == nullptr || length < 0) {
        return nullptr;
    }
    auto* const duplicate = static_cast<char16_t*>(std::malloc((static_cast<std::size_t>(length) + 1) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, duplicate);
    duplicate[length] = u'\0';
    return duplicate;
}

} // namespace

CLexerForNPC::CLexerForNPC() :
    m_FileBuf(nullptr),
    m_pRawFileBuf(nullptr),
    m_nFilePointer(0),
    m_nFileSize(0),
    m_nLastInput(0),
    line(0),
    m_bOnlyPrecompiledHeader(false) {
}

CLexerForNPC::~CLexerForNPC() {
    if (m_pRawFileBuf != nullptr) {
        std::free(m_pRawFileBuf);
    }
    m_pRawFileBuf = nullptr;
}

int CLexerForNPC::Create(CParserForNPC* parser, char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    m_bOnlyPrecompiledHeader = parser->m_bOnlyPrecompiledHeader;
    return 1;
}

int CLexerForNPC::yygetchar() {
    if (m_nFilePointer >= m_nFileSize) {
        return -1;
    }
    const char16_t value = m_FileBuf[m_nFilePointer++];
    m_LastInput[m_nLastInput % 100] = value;
    ++m_nLastInput;
    return static_cast<int>(static_cast<std::uint16_t>(value));
}

int CLexerForNPC::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        SetReader([this] { return yygetchar(); });
    }
    auto* const semanticSlot = lval;
    auto* const semanticInt = reinterpret_cast<std::int32_t*>(semanticSlot);
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "npc_begin" { EndToken(); return TK_NPC_BEGIN; }
            "npc_end" { EndToken(); return TK_NPC_END; }
            "name_id" {
                EndToken();
                *semanticInt = 2;
                return TK_NAME_ID_GROUP;
            }
            "level" {
                EndToken();
                *semanticInt = 71;
                return TK_NAME_ID_GROUP;
            }
            "race" {
                EndToken();
                *semanticInt = 72;
                return TK_NAME_ID_GROUP;
            }
            "class" {
                EndToken();
                *semanticInt = 73;
                return TK_NAME_ID_GROUP;
            }
            "sex" {
                EndToken();
                *semanticInt = 74;
                return TK_NAME_ID_GROUP;
            }
            "tend" {
                EndToken();
                *semanticInt = 75;
                return TK_NAME_ID_GROUP;
            }
            "int" {
                EndToken();
                *semanticInt = 76;
                return TK_NAME_ID_GROUP;
            }
            "str" {
                EndToken();
                *semanticInt = 77;
                return TK_NAME_ID_GROUP;
            }
            "con" {
                EndToken();
                *semanticInt = 78;
                return TK_NAME_ID_GROUP;
            }
            "men" {
                EndToken();
                *semanticInt = 79;
                return TK_NAME_ID_GROUP;
            }
            "dex" {
                EndToken();
                *semanticInt = 80;
                return TK_NAME_ID_GROUP;
            }
            "wit" {
                EndToken();
                *semanticInt = 81;
                return TK_NAME_ID_GROUP;
            }
            "luc" {
                EndToken();
                *semanticInt = 306;
                return TK_NAME_ID_GROUP;
            }
            "cha" {
                EndToken();
                *semanticInt = 307;
                return TK_NAME_ID_GROUP;
            }
            "exp" {
                EndToken();
                *semanticInt = 82;
                return TK_NAME_ID_GROUP;
            }
            "hp_increase" {
                EndToken();
                *semanticInt = 83;
                return TK_NAME_ID_GROUP;
            }
            "mp_increase" {
                EndToken();
                *semanticInt = 84;
                return TK_NAME_ID_GROUP;
            }
            "org_hp_regen" {
                EndToken();
                *semanticInt = 160;
                return TK_NAME_ID_GROUP;
            }
            "org_mp_regen" {
                EndToken();
                *semanticInt = 161;
                return TK_NAME_ID_GROUP;
            }
            "base_hp_regen" {
                EndToken();
                *semanticInt = 85;
                return TK_NAME_ID_GROUP;
            }
            "base_mp_regen" {
                EndToken();
                *semanticInt = 86;
                return TK_NAME_ID_GROUP;
            }
            "weapon" {
                EndToken();
                *semanticInt = 87;
                return TK_NAME_ID_GROUP;
            }
            "armor" {
                EndToken();
                *semanticInt = 88;
                return TK_NAME_ID_GROUP;
            }
            "physical_hit_modify" {
                EndToken();
                *semanticInt = 89;
                return TK_NAME_ID_GROUP;
            }
            "base_physical_attack" {
                EndToken();
                *semanticInt = 90;
                return TK_NAME_ID_GROUP;
            }
            "base_critical" {
                EndToken();
                *semanticInt = 91;
                return TK_NAME_ID_GROUP;
            }
            "base_attack_speed" {
                EndToken();
                *semanticInt = 93;
                return TK_NAME_ID_GROUP;
            }
            "base_defend" {
                EndToken();
                *semanticInt = 94;
                return TK_NAME_ID_GROUP;
            }
            "base_hit" {
                EndToken();
                *semanticInt = 95;
                return TK_NAME_ID_GROUP;
            }
            "base_avoid" {
                EndToken();
                *semanticInt = 96;
                return TK_NAME_ID_GROUP;
            }
            "physical_avoid_modify" {
                EndToken();
                *semanticInt = 97;
                return TK_NAME_ID_GROUP;
            }
            "base_magic_attack" {
                EndToken();
                *semanticInt = 98;
                return TK_NAME_ID_GROUP;
            }
            "base_magic_defend" {
                EndToken();
                *semanticInt = 99;
                return TK_NAME_ID_GROUP;
            }
            "magic_use_speed" {
                EndToken();
                *semanticInt = 100;
                return TK_NAME_ID_GROUP;
            }
            "ground_low" {
                EndToken();
                *semanticInt = 102;
                return TK_NAME_ID_GROUP;
            }
            "ground_high" {
                EndToken();
                *semanticInt = 103;
                return TK_NAME_ID_GROUP;
            }
            "base_jump" {
                EndToken();
                *semanticInt = 110;
                return TK_NAME_ID_GROUP;
            }
            "additional_make_list" {
                EndToken();
                *semanticInt = 112;
                return TK_NAME_ID_GROUP;
            }
            "additional_make_multi_list" {
                EndToken();
                *semanticInt = 219;
                return TK_NAME_ID_GROUP;
            }
            "g_additional_make_multi_list" { EndToken(); return TK_G_ADDITIONAL_MAKE_MULTI_LIST; }
            "ex_item_drop_list" {
                EndToken();
                *semanticInt = 259;
                return TK_NAME_ID_GROUP;
            }
            "vitality_item_drop_list" {
                EndToken();
                *semanticInt = 300;
                return TK_NAME_ID_GROUP;
            }
            "lucky_stat_item_list" {
                EndToken();
                *semanticInt = 308;
                return TK_NAME_ID_GROUP;
            }
            "selling_goods" {
                EndToken();
                *semanticInt = 113;
                return TK_NAME_ID_GROUP;
            }
            "buying_goods" {
                EndToken();
                *semanticInt = 114;
                return TK_NAME_ID_GROUP;
            }
            "skill_list" {
                EndToken();
                *semanticInt = 115;
                return TK_NAME_ID_GROUP;
            }
            "weight_limit" {
                EndToken();
                *semanticInt = 117;
                return TK_NAME_ID_GROUP;
            }
            "satiety" {
                EndToken();
                *semanticInt = 118;
                return TK_NAME_ID_GROUP;
            }
            "tendency" {
                EndToken();
                *semanticInt = 120;
                return TK_NAME_ID_GROUP;
            }
            "evaluation" {
                EndToken();
                *semanticInt = 121;
                return TK_NAME_ID_GROUP;
            }
            "friends" {
                EndToken();
                *semanticInt = 122;
                return TK_NAME_ID_GROUP;
            }
            "org_hp" {
                EndToken();
                *semanticInt = 126;
                return TK_NAME_ID_GROUP;
            }
            "org_mp" {
                EndToken();
                *semanticInt = 127;
                return TK_NAME_ID_GROUP;
            }
            "hp_recover" {
                EndToken();
                *semanticInt = 128;
                return TK_NAME_ID_GROUP;
            }
            "mp_recover" {
                EndToken();
                *semanticInt = 129;
                return TK_NAME_ID_GROUP;
            }
            "hit" {
                EndToken();
                *semanticInt = 130;
                return TK_NAME_ID_GROUP;
            }
            "physical_attack" {
                EndToken();
                *semanticInt = 131;
                return TK_NAME_ID_GROUP;
            }
            "critical" {
                EndToken();
                *semanticInt = 45;
                return TK_NAME_ID_GROUP;
            }
            "attack_speed" {
                EndToken();
                *semanticInt = 50;
                return TK_NAME_ID_GROUP;
            }
            "attack_interval" {
                EndToken();
                *semanticInt = 132;
                return TK_NAME_ID_GROUP;
            }
            "physical_avoid" {
                EndToken();
                *semanticInt = 133;
                return TK_NAME_ID_GROUP;
            }
            "magic_attack" {
                EndToken();
                *semanticInt = 134;
                return TK_NAME_ID_GROUP;
            }
            "magic_defend" {
                EndToken();
                *semanticInt = 135;
                return TK_NAME_ID_GROUP;
            }
            "jump" {
                EndToken();
                *semanticInt = 136;
                return TK_NAME_ID_GROUP;
            }
            "earshot" {
                EndToken();
                *semanticInt = 137;
                return TK_NAME_ID_GROUP;
            }
            "base_rand_dam" {
                EndToken();
                *semanticInt = 138;
                return TK_NAME_ID_GROUP;
            }
            "undying" {
                EndToken();
                *semanticInt = 139;
                return TK_NAME_ID_GROUP;
            }
            "can_be_attacked" {
                EndToken();
                *semanticInt = 140;
                return TK_NAME_ID_GROUP;
            }
            "targetable" {
                EndToken();
                *semanticInt = 290;
                return TK_NAME_ID_GROUP;
            }
            "show_name_tag" {
                EndToken();
                *semanticInt = 291;
                return TK_NAME_ID_GROUP;
            }
            "base_attack_range" {
                EndToken();
                *semanticInt = 48;
                return TK_NAME_ID_GROUP;
            }
            "base_damage_range" {
                EndToken();
                *semanticInt = 49;
                return TK_NAME_ID_GROUP;
            }
            "acquire_rp" {
                EndToken();
                *semanticInt = 258;
                return TK_NAME_ID_GROUP;
            }
            "acquire_sp" {
                EndToken();
                *semanticInt = 141;
                return TK_NAME_ID_GROUP;
            }
            "safe_height" {
                EndToken();
                *semanticInt = 143;
                return TK_NAME_ID_GROUP;
            }
            "corpse_make_list" {
                EndToken();
                *semanticInt = 146;
                return TK_NAME_ID_GROUP;
            }
            "slot_chest" {
                EndToken();
                *semanticInt = 147;
                return TK_NAME_ID_GROUP;
            }
            "slot_rhand" {
                EndToken();
                *semanticInt = 148;
                return TK_NAME_ID_GROUP;
            }
            "slot_lhand" {
                EndToken();
                *semanticInt = 149;
                return TK_NAME_ID_GROUP;
            }
            "shield_defense" {
                EndToken();
                *semanticInt = 169;
                return TK_NAME_ID_GROUP;
            }
            "shield_defense_rate" {
                EndToken();
                *semanticInt = 168;
                return TK_NAME_ID_GROUP;
            }
            "base_attack_type" {
                EndToken();
                *semanticInt = 92;
                return TK_NAME_ID_GROUP;
            }
            "agro_range" {
                EndToken();
                *semanticInt = 151;
                return TK_NAME_ID_GROUP;
            }
            "collision_radius" {
                EndToken();
                *semanticInt = 153;
                return TK_NAME_ID_GROUP;
            }
            "collision_height" {
                EndToken();
                *semanticInt = 154;
                return TK_NAME_ID_GROUP;
            }
            "guild_leader" {
                EndToken();
                *semanticInt = 155;
                return TK_NAME_ID_GROUP;
            }
            "npc_ai" {
                EndToken();
                *semanticInt = 162;
                return TK_NAME_ID_GROUP;
            }
            "category" { EndToken(); return TK_CATEGORY; }
            "clan" {
                EndToken();
                *semanticInt = 163;
                return TK_NAME_ID_GROUP;
            }
            "clan_help_range" {
                EndToken();
                *semanticInt = 164;
                return TK_NAME_ID_GROUP;
            }
            "hit_time_factor" {
                EndToken();
                *semanticInt = 224;
                return TK_NAME_ID_GROUP;
            }
            "hit_time_factor_skill" {
                EndToken();
                *semanticInt = 225;
                return TK_NAME_ID_GROUP;
            }
            "acquire_exp_rate" {
                EndToken();
                *semanticInt = 171;
                return TK_NAME_ID_GROUP;
            }
            "base_reuse_delay" {
                EndToken();
                *semanticInt = 172;
                return TK_NAME_ID_GROUP;
            }
            "corpse_time" {
                EndToken();
                *semanticInt = 174;
                return TK_NAME_ID_GROUP;
            }
            "ignore_clan_list" {
                EndToken();
                *semanticInt = 165;
                return TK_NAME_ID_GROUP;
            }
            "no_sleep_mode" {
                EndToken();
                *semanticInt = 220;
                return TK_NAME_ID_GROUP;
            }
            "soulshot_count" {
                EndToken();
                *semanticInt = 221;
                return TK_NAME_ID_GROUP;
            }
            "spiritshot_count" {
                EndToken();
                *semanticInt = 222;
                return TK_NAME_ID_GROUP;
            }
            "unsowing" {
                EndToken();
                *semanticInt = 227;
                return TK_NAME_ID_GROUP;
            }
            "ex_crt_effect" {
                EndToken();
                *semanticInt = 264;
                return TK_NAME_ID_GROUP;
            }
            "mp_reward" {
                EndToken();
                *semanticInt = 0x12d;
                return TK_MP_REWARD;
            }
            "s_npc_prop_hp_rate" {
                EndToken();
                *semanticInt = 261;
                return TK_NAME_ID_GROUP;
            }
            "flying" { EndToken(); return TK_FLYING; }
            "base_attribute_attack" {
                EndToken();
                *semanticInt = 273;
                return TK_NAME_ID_GROUP;
            }
            "base_attribute_defend" {
                EndToken();
                *semanticInt = 274;
                return TK_NAME_ID_GROUP;
            }
            "passable_door" {
                EndToken();
                *semanticInt = 276;
                return TK_NAME_ID_GROUP;
            }
            "unique" {
                EndToken();
                *semanticInt = 280;
                return TK_NAME_ID_GROUP;
            }
            "can_move" {
                EndToken();
                *semanticInt = 282;
                return TK_NAME_ID_GROUP;
            }
            "can_change_dir" {
                EndToken();
                *semanticInt = 311;
                return TK_NAME_ID_GROUP;
            }
            "event_flag" {
                EndToken();
                *semanticInt = 283;
                return TK_NAME_ID_GROUP;
            }
            "has_summoner" {
                EndToken();
                *semanticInt = 285;
                return TK_NAME_ID_GROUP;
            }
            "private_respawn_log" {
                EndToken();
                *semanticInt = 292;
                return TK_NAME_ID_GROUP;
            }
            "fake_class_id" {
                EndToken();
                *semanticInt = 295;
                return TK_NAME_ID_GROUP;
            }
            "abnormal_resist" {
                EndToken();
                *semanticInt = 299;
                return TK_NAME_ID_GROUP;
            }
            "is_death_penalty" {
                EndToken();
                *semanticInt = 303;
                return TK_NAME_ID_GROUP;
            }
            "event_drop" { EndToken(); return TK_EVENT_DROP; }
            "ex_drop" { EndToken(); return TK_EX_DROP; }
            "enable_move_after_talk" {
                EndToken();
                *semanticInt = 304;
                return TK_NAME_ID_GROUP;
            }
            "broadcast_cond" { EndToken(); return TK_BROADCAST_COND; }
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
                    ParseWindowsInt64(std::u16string_view(yytext + 1, static_cast<std::size_t>(std::max(0, yyleng - 1))))
                );
                return TK_INTEGER;
            }
            "-"{0,2} ("0" | [1-9][0-9]*) "." [0-9]* {
                EndToken();
                *semanticSlot = ParseWindowsDoubleBits(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                return TK_FLOAT_NUMBER;
            }
            [;={}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateInnerText(yytext, yyleng);
                *semanticSlot = grammar::PointerSemantic(text);
                return TK_NAME;
            }
            "@" [A-Za-z_'] [A-Za-z0-9_']* {
                EndToken();
                const char16_t* const name = yytext + 1;
                std::int64_t value = -1;
                if (!m_bOnlyPrecompiledHeader) {
                    value = g_PrecompiledHeaderDB.Search(name);
                    if (value == -1) {
                        // const std::wstring hostName = target_utf16::ToHostCodeUnits(name)
                        const std::wstring hostName = Utf16ToWide(name);
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"line[%d] : Undefined precompiled name[%ls]",
                        //     line + 1,
                        //     hostName.c_str())
                        // L2_LOG_ERROR(Log,
                        //     L"line[%d] : Undefined precompiled name[%ls]",
                        //     line + 1,
                        //     hostName.c_str())
                        L2_LOG_ERROR(
                            Log, L"npcdata.txt line %lld: unresolved PCH reference '%ls'; parsing continues with ID -1.",
                            static_cast<long long>(line) + 1, hostName.c_str()
                        );
                    }
                }
                *semanticSlot = static_cast<grammar::SemanticBits>(value);
                return TK_INTEGER;
            }
            [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                char16_t* const text = DuplicateText(yytext, yyleng);
                *semanticSlot = grammar::PointerSemantic(text);
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
                //     L"[LEXER_ERROR] line [%d] Invalid char [%ls] ",
                //     line + 1,
                //     invalid.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] line [%d] Invalid char [%ls] ",
                //     line + 1,
                //     invalid.c_str())
                L2_LOG_ERROR(
                    Log, L"npcdata.txt line %lld: invalid token '%ls'; the token is skipped.",
                    static_cast<long long>(line) + 1, invalid.c_str()
                );
                continue;
            }
            $ { return 0; }
        */
    }
}
