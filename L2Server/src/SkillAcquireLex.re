// skillacquire.txt scanner (original L2Server/skillacquire_lex.l).
// re2c generates SkillAcquireLex.cpp.

#include "L2Server/src/SkillAcquireLex.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/SkillAcquireYacc.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "SkillAcquireYaccTokens.h"

namespace {

std::string ToAscii(std::u16string_view value) {
    std::string ascii;
    ascii.reserve(value.size());
    for (const char16_t codeUnit : value) {
        if (codeUnit > 0x7f) {
            return {};
        }
        ascii.push_back(static_cast<char>(codeUnit));
    }
    return ascii;
}

std::int64_t ParseWindowsInt64(std::u16string_view value) {
    const std::string ascii = ToAscii(value);
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

char16_t* DuplicateBracketedText(const char16_t* text, int length) {
    const std::size_t capacity = static_cast<std::size_t>(length - 1);
    auto* const duplicate = static_cast<char16_t*>(std::malloc(capacity * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }

    const std::size_t contentLength = static_cast<std::size_t>(length - 2);
    std::copy_n(text + 1, contentLength, duplicate);
    duplicate[contentLength] = u'\0';
    return duplicate;
}

} // namespace

CLexerForSkillAcquire::CLexerForSkillAcquire() : m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0) {
}

CLexerForSkillAcquire::~CLexerForSkillAcquire() { m_FileBuf = nullptr; }

int CLexerForSkillAcquire::Create(char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

int CLexerForSkillAcquire::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_FileBuf one code unit at a time.
        SetInput(
            m_FileBuf + m_nFilePointer,
            m_nFilePointer < m_nFileSize ? static_cast<std::size_t>(m_nFileSize - m_nFilePointer) : 0
        );
    }
    auto* const semanticSlot = lval;
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "/*" .* "*/" { EndToken(); continue; }
            "fighter_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(0);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "warrior_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(1);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "warlord_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(3);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "gladiator_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(2);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "knight_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(4);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "paladin_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(5);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "dark_avenger_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(6);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "rogue_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(7);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "treasure_hunter_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(8);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "hawkeye_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(9);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "elven_fighter_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(18);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "elven_knight_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(19);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "temple_knight_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(20);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "swordsinger_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(21);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "elven_scout_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(22);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "plain_walker_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(23);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "silver_ranger_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(24);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "dark_fighter_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(31);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "palus_knight_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(32);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "shillien_knight_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(33);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "bladedancer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(34);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "assasin_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(35);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "abyss_walker_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(36);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "phantom_ranger_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(37);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "orc_fighter_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(44);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "orc_raider_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(45);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "destroyer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(46);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "orc_monk_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(47);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "tyrant_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(48);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "dwarven_fighter_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(53);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "scavenger_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(54);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "bounty_hunter_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(55);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "artisan_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(56);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "warsmith_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(57);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "mage_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(10);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "wizard_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(11);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "sorcerer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(12);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "necromancer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(13);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "warlock_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(14);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "cleric_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(15);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "prophet_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(17);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "bishop_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(16);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "elven_mage_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(25);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "elven_wizard_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(26);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "spellsinger_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(27);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "elemental_summoner_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(28);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "oracle_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(29);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "elder_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(30);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "dark_mage_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(38);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "dark_wizard_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(39);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "spellhowler_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(40);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "phantom_summoner_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(41);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "shillien_oracle_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(42);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "shillien_elder_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(43);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "orc_mage_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(49);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "orc_shaman_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(50);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "overlord_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(51);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "warcryer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(52);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "duelist_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(88);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "dreadnought_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(89);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "phoenix_knight_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(90);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "hell_knight_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(91);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "sagittarius_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(92);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "adventurer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(93);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "archmage_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(94);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "soultaker_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(95);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "arcana_lord_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(96);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "cardinal_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(97);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "hierophant_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(98);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "evas_templar_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(99);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "sword_muse_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(100);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "wind_rider_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(101);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "moonlight_sentinel_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(102);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "mystic_muse_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(103);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "elemental_master_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(104);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "evas_saint_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(105);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "shillien_templar_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(106);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "spectral_dancer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(107);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "ghost_hunter_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(108);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "ghost_sentinel_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(109);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "storm_screamer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(110);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "spectral_master_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(111);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "shillien_saint_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(112);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "titan_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(113);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "grand_khavatari_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(114);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "dominator_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(115);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "doomcryer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(116);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "fortune_seeker_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(117);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "maestro_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(118);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "kamael_m_soldier_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(123);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "kamael_f_soldier_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(124);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "trooper_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(125);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "warder_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(126);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "berserker_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(127);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "m_soul_breaker_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(128);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "f_soul_breaker_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(129);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "arbalester_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(130);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "doombringer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(131);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "m_soul_hound_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(132);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "f_soul_hound_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(133);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "trickster_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(134);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "inspector_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(135);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "judicator_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(136);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "sigel_knight_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(139);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "tir_warrior_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(140);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "othel_rogue_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(141);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "yr_archer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(142);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "feoh_wizard_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(143);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "is_enchanter_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(144);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "wynn_summoner_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(145);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "eolh_healer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(146);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "mass_lenker_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(190);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "seer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(191);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "sigel_phoenix_knight_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(148);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "sigel_hell_knight_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(149);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "sigel_evas_templar_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(150);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "sigel_shilien_templar_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(151);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "tir_duelist_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(152);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "tir_dreadnought_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(153);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "tir_titan_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(154);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "tir_grand_khavatari_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(155);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "tir_maestro_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(156);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "tir_doombringer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(157);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "othel_adventurer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(158);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "othel_wind_rider_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(159);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "othel_ghost_hunter_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(160);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "othel_fortune_seeker_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(161);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "yr_sagittarius_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(162);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "yr_moonlight_sentinel_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(163);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "yr_ghost_sentinel_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(164);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "yr_trickster_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(165);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "feoh_archmage_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(166);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "feoh_soultaker_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(167);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "feoh_mystic_muse_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(168);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "feoh_storm_screamer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(169);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "feoh_soul_hound_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(170);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "is_hierophant_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(171);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "is_sword_muse_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(172);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "is_spectral_dancer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(173);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "is_dominator_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(174);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "is_doomcryer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(175);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "wynn_arcana_lord_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(176);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "wynn_elemental_master_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(177);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "wynn_spectral_master_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(178);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "eolh_cardinal_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(179);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "eolh_evas_saint_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(180);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "eolh_shilien_saint_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(181);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "ertheia_wizard_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(183);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "menace_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(185);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "storm_menace_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(187);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "sayhas_seer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(189);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "ertheia_fighter_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(182);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "marauder_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(184);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "lenker_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(186);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "strato_lenker_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(188);
                return TK_FIGHTER_BEGIN_GROUP;
            }
            "fighter_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(0);
                return TK_FIGHTER_END_GROUP;
            }
            "warrior_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(1);
                return TK_FIGHTER_END_GROUP;
            }
            "warlord_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(3);
                return TK_FIGHTER_END_GROUP;
            }
            "gladiator_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(2);
                return TK_FIGHTER_END_GROUP;
            }
            "knight_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(4);
                return TK_FIGHTER_END_GROUP;
            }
            "paladin_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(5);
                return TK_FIGHTER_END_GROUP;
            }
            "dark_avenger_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(6);
                return TK_FIGHTER_END_GROUP;
            }
            "rogue_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(7);
                return TK_FIGHTER_END_GROUP;
            }
            "treasure_hunter_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(8);
                return TK_FIGHTER_END_GROUP;
            }
            "hawkeye_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(9);
                return TK_FIGHTER_END_GROUP;
            }
            "elven_fighter_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(18);
                return TK_FIGHTER_END_GROUP;
            }
            "elven_knight_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(19);
                return TK_FIGHTER_END_GROUP;
            }
            "temple_knight_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(20);
                return TK_FIGHTER_END_GROUP;
            }
            "swordsinger_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(21);
                return TK_FIGHTER_END_GROUP;
            }
            "elven_scout_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(22);
                return TK_FIGHTER_END_GROUP;
            }
            "plain_walker_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(23);
                return TK_FIGHTER_END_GROUP;
            }
            "silver_ranger_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(24);
                return TK_FIGHTER_END_GROUP;
            }
            "dark_fighter_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(31);
                return TK_FIGHTER_END_GROUP;
            }
            "palus_knight_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(32);
                return TK_FIGHTER_END_GROUP;
            }
            "shillien_knight_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(33);
                return TK_FIGHTER_END_GROUP;
            }
            "bladedancer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(34);
                return TK_FIGHTER_END_GROUP;
            }
            "assasin_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(35);
                return TK_FIGHTER_END_GROUP;
            }
            "abyss_walker_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(36);
                return TK_FIGHTER_END_GROUP;
            }
            "phantom_ranger_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(37);
                return TK_FIGHTER_END_GROUP;
            }
            "orc_fighter_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(44);
                return TK_FIGHTER_END_GROUP;
            }
            "orc_raider_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(45);
                return TK_FIGHTER_END_GROUP;
            }
            "destroyer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(46);
                return TK_FIGHTER_END_GROUP;
            }
            "orc_monk_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(47);
                return TK_FIGHTER_END_GROUP;
            }
            "tyrant_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(48);
                return TK_FIGHTER_END_GROUP;
            }
            "dwarven_fighter_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(53);
                return TK_FIGHTER_END_GROUP;
            }
            "scavenger_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(54);
                return TK_FIGHTER_END_GROUP;
            }
            "bounty_hunter_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(55);
                return TK_FIGHTER_END_GROUP;
            }
            "artisan_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(56);
                return TK_FIGHTER_END_GROUP;
            }
            "warsmith_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(57);
                return TK_FIGHTER_END_GROUP;
            }
            "mage_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(10);
                return TK_FIGHTER_END_GROUP;
            }
            "wizard_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(11);
                return TK_FIGHTER_END_GROUP;
            }
            "sorcerer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(12);
                return TK_FIGHTER_END_GROUP;
            }
            "necromancer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(13);
                return TK_FIGHTER_END_GROUP;
            }
            "warlock_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(14);
                return TK_FIGHTER_END_GROUP;
            }
            "cleric_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(15);
                return TK_FIGHTER_END_GROUP;
            }
            "prophet_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(17);
                return TK_FIGHTER_END_GROUP;
            }
            "bishop_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(16);
                return TK_FIGHTER_END_GROUP;
            }
            "elven_mage_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(25);
                return TK_FIGHTER_END_GROUP;
            }
            "elven_wizard_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(26);
                return TK_FIGHTER_END_GROUP;
            }
            "spellsinger_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(27);
                return TK_FIGHTER_END_GROUP;
            }
            "elemental_summoner_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(28);
                return TK_FIGHTER_END_GROUP;
            }
            "oracle_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(29);
                return TK_FIGHTER_END_GROUP;
            }
            "elder_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(30);
                return TK_FIGHTER_END_GROUP;
            }
            "dark_mage_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(38);
                return TK_FIGHTER_END_GROUP;
            }
            "dark_wizard_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(39);
                return TK_FIGHTER_END_GROUP;
            }
            "spellhowler_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(40);
                return TK_FIGHTER_END_GROUP;
            }
            "phantom_summoner_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(41);
                return TK_FIGHTER_END_GROUP;
            }
            "shillien_oracle_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(42);
                return TK_FIGHTER_END_GROUP;
            }
            "shillien_elder_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(43);
                return TK_FIGHTER_END_GROUP;
            }
            "orc_mage_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(49);
                return TK_FIGHTER_END_GROUP;
            }
            "orc_shaman_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(50);
                return TK_FIGHTER_END_GROUP;
            }
            "overlord_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(51);
                return TK_FIGHTER_END_GROUP;
            }
            "warcryer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(52);
                return TK_FIGHTER_END_GROUP;
            }
            "duelist_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(88);
                return TK_FIGHTER_END_GROUP;
            }
            "dreadnought_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(89);
                return TK_FIGHTER_END_GROUP;
            }
            "phoenix_knight_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(90);
                return TK_FIGHTER_END_GROUP;
            }
            "hell_knight_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(91);
                return TK_FIGHTER_END_GROUP;
            }
            "sagittarius_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(92);
                return TK_FIGHTER_END_GROUP;
            }
            "adventurer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(93);
                return TK_FIGHTER_END_GROUP;
            }
            "archmage_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(94);
                return TK_FIGHTER_END_GROUP;
            }
            "soultaker_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(95);
                return TK_FIGHTER_END_GROUP;
            }
            "arcana_lord_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(96);
                return TK_FIGHTER_END_GROUP;
            }
            "cardinal_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(97);
                return TK_FIGHTER_END_GROUP;
            }
            "hierophant_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(98);
                return TK_FIGHTER_END_GROUP;
            }
            "evas_templar_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(99);
                return TK_FIGHTER_END_GROUP;
            }
            "sword_muse_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(100);
                return TK_FIGHTER_END_GROUP;
            }
            "wind_rider_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(101);
                return TK_FIGHTER_END_GROUP;
            }
            "moonlight_sentinel_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(102);
                return TK_FIGHTER_END_GROUP;
            }
            "mystic_muse_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(103);
                return TK_FIGHTER_END_GROUP;
            }
            "elemental_master_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(104);
                return TK_FIGHTER_END_GROUP;
            }
            "evas_saint_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(105);
                return TK_FIGHTER_END_GROUP;
            }
            "shillien_templar_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(106);
                return TK_FIGHTER_END_GROUP;
            }
            "spectral_dancer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(107);
                return TK_FIGHTER_END_GROUP;
            }
            "ghost_hunter_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(108);
                return TK_FIGHTER_END_GROUP;
            }
            "ghost_sentinel_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(109);
                return TK_FIGHTER_END_GROUP;
            }
            "storm_screamer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(110);
                return TK_FIGHTER_END_GROUP;
            }
            "spectral_master_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(111);
                return TK_FIGHTER_END_GROUP;
            }
            "shillien_saint_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(112);
                return TK_FIGHTER_END_GROUP;
            }
            "titan_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(113);
                return TK_FIGHTER_END_GROUP;
            }
            "grand_khavatari_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(114);
                return TK_FIGHTER_END_GROUP;
            }
            "dominator_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(115);
                return TK_FIGHTER_END_GROUP;
            }
            "doomcryer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(116);
                return TK_FIGHTER_END_GROUP;
            }
            "fortune_seeker_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(117);
                return TK_FIGHTER_END_GROUP;
            }
            "maestro_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(118);
                return TK_FIGHTER_END_GROUP;
            }
            "kamael_m_soldier_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(123);
                return TK_FIGHTER_END_GROUP;
            }
            "kamael_f_soldier_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(124);
                return TK_FIGHTER_END_GROUP;
            }
            "trooper_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(125);
                return TK_FIGHTER_END_GROUP;
            }
            "warder_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(126);
                return TK_FIGHTER_END_GROUP;
            }
            "berserker_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(127);
                return TK_FIGHTER_END_GROUP;
            }
            "m_soul_breaker_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(128);
                return TK_FIGHTER_END_GROUP;
            }
            "f_soul_breaker_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(129);
                return TK_FIGHTER_END_GROUP;
            }
            "arbalester_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(130);
                return TK_FIGHTER_END_GROUP;
            }
            "doombringer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(131);
                return TK_FIGHTER_END_GROUP;
            }
            "m_soul_hound_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(132);
                return TK_FIGHTER_END_GROUP;
            }
            "f_soul_hound_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(133);
                return TK_FIGHTER_END_GROUP;
            }
            "trickster_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(134);
                return TK_FIGHTER_END_GROUP;
            }
            "inspector_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(135);
                return TK_FIGHTER_END_GROUP;
            }
            "judicator_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(136);
                return TK_FIGHTER_END_GROUP;
            }
            "sigel_knight_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(139);
                return TK_FIGHTER_END_GROUP;
            }
            "tir_warrior_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(140);
                return TK_FIGHTER_END_GROUP;
            }
            "othel_rogue_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(141);
                return TK_FIGHTER_END_GROUP;
            }
            "yr_archer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(142);
                return TK_FIGHTER_END_GROUP;
            }
            "feoh_wizard_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(143);
                return TK_FIGHTER_END_GROUP;
            }
            "is_enchanter_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(144);
                return TK_FIGHTER_END_GROUP;
            }
            "wynn_summoner_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(145);
                return TK_FIGHTER_END_GROUP;
            }
            "eolh_healer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(146);
                return TK_FIGHTER_END_GROUP;
            }
            "mass_lenker_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(190);
                return TK_FIGHTER_END_GROUP;
            }
            "seer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(191);
                return TK_FIGHTER_END_GROUP;
            }
            "sigel_phoenix_knight_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(148);
                return TK_FIGHTER_END_GROUP;
            }
            "sigel_hell_knight_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(149);
                return TK_FIGHTER_END_GROUP;
            }
            "sigel_evas_templar_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(150);
                return TK_FIGHTER_END_GROUP;
            }
            "sigel_shilien_templar_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(151);
                return TK_FIGHTER_END_GROUP;
            }
            "tir_duelist_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(152);
                return TK_FIGHTER_END_GROUP;
            }
            "tir_dreadnought_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(153);
                return TK_FIGHTER_END_GROUP;
            }
            "tir_titan_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(154);
                return TK_FIGHTER_END_GROUP;
            }
            "tir_grand_khavatari_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(155);
                return TK_FIGHTER_END_GROUP;
            }
            "tir_maestro_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(156);
                return TK_FIGHTER_END_GROUP;
            }
            "tir_doombringer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(157);
                return TK_FIGHTER_END_GROUP;
            }
            "othel_adventurer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(158);
                return TK_FIGHTER_END_GROUP;
            }
            "othel_wind_rider_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(159);
                return TK_FIGHTER_END_GROUP;
            }
            "othel_ghost_hunter_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(160);
                return TK_FIGHTER_END_GROUP;
            }
            "othel_fortune_seeker_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(161);
                return TK_FIGHTER_END_GROUP;
            }
            "yr_sagittarius_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(162);
                return TK_FIGHTER_END_GROUP;
            }
            "yr_moonlight_sentinel_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(163);
                return TK_FIGHTER_END_GROUP;
            }
            "yr_ghost_sentinel_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(164);
                return TK_FIGHTER_END_GROUP;
            }
            "yr_trickster_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(165);
                return TK_FIGHTER_END_GROUP;
            }
            "feoh_archmage_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(166);
                return TK_FIGHTER_END_GROUP;
            }
            "feoh_soultaker_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(167);
                return TK_FIGHTER_END_GROUP;
            }
            "feoh_mystic_muse_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(168);
                return TK_FIGHTER_END_GROUP;
            }
            "feoh_storm_screamer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(169);
                return TK_FIGHTER_END_GROUP;
            }
            "feoh_soul_hound_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(170);
                return TK_FIGHTER_END_GROUP;
            }
            "is_hierophant_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(171);
                return TK_FIGHTER_END_GROUP;
            }
            "is_sword_muse_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(172);
                return TK_FIGHTER_END_GROUP;
            }
            "is_spectral_dancer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(173);
                return TK_FIGHTER_END_GROUP;
            }
            "is_dominator_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(174);
                return TK_FIGHTER_END_GROUP;
            }
            "is_doomcryer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(175);
                return TK_FIGHTER_END_GROUP;
            }
            "wynn_arcana_lord_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(176);
                return TK_FIGHTER_END_GROUP;
            }
            "wynn_elemental_master_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(177);
                return TK_FIGHTER_END_GROUP;
            }
            "wynn_spectral_master_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(178);
                return TK_FIGHTER_END_GROUP;
            }
            "eolh_cardinal_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(179);
                return TK_FIGHTER_END_GROUP;
            }
            "eolh_evas_saint_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(180);
                return TK_FIGHTER_END_GROUP;
            }
            "eolh_shilien_saint_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(181);
                return TK_FIGHTER_END_GROUP;
            }
            "ertheia_wizard_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(183);
                return TK_FIGHTER_END_GROUP;
            }
            "menace_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(185);
                return TK_FIGHTER_END_GROUP;
            }
            "storm_menace_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(187);
                return TK_FIGHTER_END_GROUP;
            }
            "sayhas_seer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(189);
                return TK_FIGHTER_END_GROUP;
            }
            "ertheia_fighter_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(182);
                return TK_FIGHTER_END_GROUP;
            }
            "marauder_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(184);
                return TK_FIGHTER_END_GROUP;
            }
            "lenker_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(186);
                return TK_FIGHTER_END_GROUP;
            }
            "strato_lenker_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(188);
                return TK_FIGHTER_END_GROUP;
            }
            "fishing_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(1);
                return TK_FISHING_BEGIN_GROUP;
            }
            "fishing_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(1);
                return TK_FISHING_END_GROUP;
            }
            "fishing_nondwarf_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(10);
                return TK_FISHING_BEGIN_GROUP;
            }
            "fishing_nondwarf_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(10);
                return TK_FISHING_END_GROUP;
            }
            "pledge_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(2);
                return TK_FISHING_BEGIN_GROUP;
            }
            "pledge_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(2);
                return TK_FISHING_END_GROUP;
            }
            "sub_pledge_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(3);
                return TK_FISHING_BEGIN_GROUP;
            }
            "sub_pledge_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(3);
                return TK_FISHING_END_GROUP;
            }
            "transform_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(4);
                return TK_FISHING_BEGIN_GROUP;
            }
            "transform_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(4);
                return TK_FISHING_END_GROUP;
            }
            "subjob_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(5);
                return TK_FISHING_BEGIN_GROUP;
            }
            "subjob_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(5);
                return TK_FISHING_END_GROUP;
            }
            "dualjob_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(47);
                return TK_FISHING_BEGIN_GROUP;
            }
            "dualjob_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(47);
                return TK_FISHING_END_GROUP;
            }
            "dualjob_sharing_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(48);
                return TK_FISHING_BEGIN_GROUP;
            }
            "dualjob_sharing_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(48);
                return TK_FISHING_END_GROUP;
            }
            "collect_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(6);
                return TK_FISHING_BEGIN_GROUP;
            }
            "collect_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(6);
                return TK_FISHING_END_GROUP;
            }
            "bishop_sharing_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(7);
                return TK_FISHING_BEGIN_GROUP;
            }
            "bishop_sharing_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(7);
                return TK_FISHING_END_GROUP;
            }
            "elder_sharing_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(8);
                return TK_FISHING_BEGIN_GROUP;
            }
            "elder_sharing_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(8);
                return TK_FISHING_END_GROUP;
            }
            "silen_elder_sharing_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(9);
                return TK_FISHING_BEGIN_GROUP;
            }
            "silen_elder_sharing_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(9);
                return TK_FISHING_END_GROUP;
            }
            "reward_phoenix_knight_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(90);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_hell_knight_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(91);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_evas_templar_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(99);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_shillien_templar_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(106);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_dreadnought_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(89);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_duelist_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(88);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_titan_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(113);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_grand_khavatari_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(114);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_maestro_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(118);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_doombringer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(131);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_adventurer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(93);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_wind_rider_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(101);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_ghost_hunter_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(108);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_fortune_seeker_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(117);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_sagittarius_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(92);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_moonlight_sentinel_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(102);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_ghost_sentinel_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(109);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_trickster_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(134);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_archmage_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(94);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_soultaker_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(95);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_mystic_muse_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(103);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_storm_screamer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(110);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_m_soul_hound_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(132);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_f_soul_hound_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(133);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_arcana_lord_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(96);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_elemental_master_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(104);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_spectral_master_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(111);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_hierophant_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(98);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_doomcryer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(116);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_sword_muse_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(100);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_spectral_dancer_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(107);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_dominator_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(115);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_cardinal_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(97);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_evas_saint_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(105);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_shillien_saint_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(112);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_judicator_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(136);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_phoenix_knight_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(90);
                return TK_FISHING_END_GROUP;
            }
            "reward_hell_knight_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(91);
                return TK_FISHING_END_GROUP;
            }
            "reward_evas_templar_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(99);
                return TK_FISHING_END_GROUP;
            }
            "reward_shillien_templar_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(106);
                return TK_FISHING_END_GROUP;
            }
            "reward_dreadnought_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(89);
                return TK_FISHING_END_GROUP;
            }
            "reward_duelist_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(88);
                return TK_FISHING_END_GROUP;
            }
            "reward_titan_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(113);
                return TK_FISHING_END_GROUP;
            }
            "reward_grand_khavatari_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(114);
                return TK_FISHING_END_GROUP;
            }
            "reward_maestro_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(118);
                return TK_FISHING_END_GROUP;
            }
            "reward_doombringer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(131);
                return TK_FISHING_END_GROUP;
            }
            "reward_adventurer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(93);
                return TK_FISHING_END_GROUP;
            }
            "reward_wind_rider_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(101);
                return TK_FISHING_END_GROUP;
            }
            "reward_ghost_hunter_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(108);
                return TK_FISHING_END_GROUP;
            }
            "reward_fortune_seeker_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(117);
                return TK_FISHING_END_GROUP;
            }
            "reward_sagittarius_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(92);
                return TK_FISHING_END_GROUP;
            }
            "reward_moonlight_sentinel_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(102);
                return TK_FISHING_END_GROUP;
            }
            "reward_ghost_sentinel_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(109);
                return TK_FISHING_END_GROUP;
            }
            "reward_trickster_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(134);
                return TK_FISHING_END_GROUP;
            }
            "reward_archmage_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(94);
                return TK_FISHING_END_GROUP;
            }
            "reward_soultaker_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(95);
                return TK_FISHING_END_GROUP;
            }
            "reward_mystic_muse_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(103);
                return TK_FISHING_END_GROUP;
            }
            "reward_storm_screamer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(110);
                return TK_FISHING_END_GROUP;
            }
            "reward_m_soul_hound_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(132);
                return TK_FISHING_END_GROUP;
            }
            "reward_f_soul_hound_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(133);
                return TK_FISHING_END_GROUP;
            }
            "reward_arcana_lord_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(96);
                return TK_FISHING_END_GROUP;
            }
            "reward_elemental_master_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(104);
                return TK_FISHING_END_GROUP;
            }
            "reward_spectral_master_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(111);
                return TK_FISHING_END_GROUP;
            }
            "reward_hierophant_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(98);
                return TK_FISHING_END_GROUP;
            }
            "reward_doomcryer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(116);
                return TK_FISHING_END_GROUP;
            }
            "reward_sword_muse_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(100);
                return TK_FISHING_END_GROUP;
            }
            "reward_spectral_dancer_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(107);
                return TK_FISHING_END_GROUP;
            }
            "reward_dominator_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(115);
                return TK_FISHING_END_GROUP;
            }
            "reward_cardinal_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(97);
                return TK_FISHING_END_GROUP;
            }
            "reward_evas_saint_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(105);
                return TK_FISHING_END_GROUP;
            }
            "reward_shillien_saint_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(112);
                return TK_FISHING_END_GROUP;
            }
            "reward_judicator_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(136);
                return TK_FISHING_END_GROUP;
            }
            "mentor_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(137);
                return TK_FISHING_BEGIN_GROUP;
            }
            "mentor_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(137);
                return TK_FISHING_END_GROUP;
            }
            "mentee_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(138);
                return TK_FISHING_BEGIN_GROUP;
            }
            "mentee_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(138);
                return TK_FISHING_END_GROUP;
            }
            "sigel_sharing_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(49);
                return TK_FISHING_BEGIN_GROUP;
            }
            "sigel_sharing_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(49);
                return TK_FISHING_END_GROUP;
            }
            "tir_sharing_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(50);
                return TK_FISHING_BEGIN_GROUP;
            }
            "tir_sharing_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(50);
                return TK_FISHING_END_GROUP;
            }
            "othel_sharing_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(51);
                return TK_FISHING_BEGIN_GROUP;
            }
            "othel_sharing_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(51);
                return TK_FISHING_END_GROUP;
            }
            "yr_sharing_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(52);
                return TK_FISHING_BEGIN_GROUP;
            }
            "yr_sharing_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(52);
                return TK_FISHING_END_GROUP;
            }
            "feoh_sharing_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(53);
                return TK_FISHING_BEGIN_GROUP;
            }
            "feoh_sharing_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(53);
                return TK_FISHING_END_GROUP;
            }
            "is_sharing_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(54);
                return TK_FISHING_BEGIN_GROUP;
            }
            "is_sharing_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(54);
                return TK_FISHING_END_GROUP;
            }
            "wynn_sharing_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(55);
                return TK_FISHING_BEGIN_GROUP;
            }
            "wynn_sharing_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(55);
                return TK_FISHING_END_GROUP;
            }
            "eolh_sharing_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(56);
                return TK_FISHING_BEGIN_GROUP;
            }
            "eolh_sharing_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(56);
                return TK_FISHING_END_GROUP;
            }
            "tribe_sharing_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(57);
                return TK_FISHING_BEGIN_GROUP;
            }
            "tribe_sharing_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(57);
                return TK_FISHING_END_GROUP;
            }
            "reward_noreward_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(58);
                return TK_FISHING_BEGIN_GROUP;
            }
            "reward_noreward_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(58);
                return TK_FISHING_END_GROUP;
            }
            "tribe2_sharing_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(59);
                return TK_FISHING_BEGIN_GROUP;
            }
            "tribe2_sharing_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(59);
                return TK_FISHING_END_GROUP;
            }
            "honors_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(141);
                return TK_FISHING_BEGIN_GROUP;
            }
            "honors_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(141);
                return TK_FISHING_END_GROUP;
            }
            "alchemy_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(140);
                return TK_FISHING_BEGIN_GROUP;
            }
            "alchemy_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(140);
                return TK_FISHING_END_GROUP;
            }
            "human_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(0 / 2);
                return TK_HUMAN_BEGIN_GROUP;
            }
            "human_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(1 / 2);
                return TK_HUMAN_END_GROUP;
            }
            "elf_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(2 / 2);
                return TK_HUMAN_BEGIN_GROUP;
            }
            "elf_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(3 / 2);
                return TK_HUMAN_END_GROUP;
            }
            "darkelf_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(4 / 2);
                return TK_HUMAN_BEGIN_GROUP;
            }
            "darkelf_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(5 / 2);
                return TK_HUMAN_END_GROUP;
            }
            "orc_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(6 / 2);
                return TK_HUMAN_BEGIN_GROUP;
            }
            "orc_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(7 / 2);
                return TK_HUMAN_END_GROUP;
            }
            "dwarf_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(8 / 2);
                return TK_HUMAN_BEGIN_GROUP;
            }
            "dwarf_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(9 / 2);
                return TK_HUMAN_END_GROUP;
            }
            "kamael_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(10 / 2);
                return TK_HUMAN_BEGIN_GROUP;
            }
            "kamael_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(11 / 2);
                return TK_HUMAN_END_GROUP;
            }
            "ertheia_begin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(12 / 2);
                return TK_HUMAN_BEGIN_GROUP;
            }
            "ertheia_end" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(13 / 2);
                return TK_HUMAN_END_GROUP;
            }
            "skill_begin" { EndToken(); return TK_SKILL_BEGIN; }
            "skill_end" { EndToken(); return TK_SKILL_END; }
            "skill_name" { EndToken(); return TK_SKILL_NAME; }
            "get_lv" { EndToken(); return TK_GET_LV; }
            "get_dual_lv" { EndToken(); return TK_GET_DUAL_LV; }
            "get_main_dual_lv" { EndToken(); return TK_GET_MAIN_DUAL_LV; }
            "item_needed" { EndToken(); return TK_ITEM_NEEDED; }
            "lv_up_sp" { EndToken(); return TK_LV_UP_SP; }
            "need_quest" { EndToken(); return TK_NEED_QUEST; }
            "auto_get" { EndToken(); return TK_AUTO_GET; }
            "social_class" { EndToken(); return TK_SOCIAL_CLASS; }
            "pledge_type" { EndToken(); return TK_PLEDGE_TYPE; }
            "race" { EndToken(); return TK_RACE; }
            "prerequisite_skill" { EndToken(); return TK_PREREQUISITE_SKILL; }
            "replaceable_skill" { EndToken(); return TK_REPLACEABLE_SKILL; }
            "residence_skill" { EndToken(); return TK_RESIDENCE_SKILL; }
            "quest_needed" { EndToken(); return TK_QUEST_NEEDED; }
            "excluded_from_list" { EndToken(); return TK_EXCLUDED_FROM_LIST; }
            "true" {
                EndToken();
                *reinterpret_cast<std::uint8_t*>(semanticSlot) = 1;
                return TK_TRUE_FALSE;
            }
            "false" {
                EndToken();
                *reinterpret_cast<std::uint8_t*>(semanticSlot) = 0;
                return TK_TRUE_FALSE;
            }
            "include_fighter" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(0);
                return TK_INCLUDE_VALUE;
            }
            "include_warrior" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(1);
                return TK_INCLUDE_VALUE;
            }
            "include_knight" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(4);
                return TK_INCLUDE_VALUE;
            }
            "include_rogue" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(7);
                return TK_INCLUDE_VALUE;
            }
            "include_mage" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(10);
                return TK_INCLUDE_VALUE;
            }
            "include_wizard" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(11);
                return TK_INCLUDE_VALUE;
            }
            "include_cleric" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(15);
                return TK_INCLUDE_VALUE;
            }
            "include_elven_fighter" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(18);
                return TK_INCLUDE_VALUE;
            }
            "include_elven_knight" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(19);
                return TK_INCLUDE_VALUE;
            }
            "include_elven_scout" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(22);
                return TK_INCLUDE_VALUE;
            }
            "include_elven_mage" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(25);
                return TK_INCLUDE_VALUE;
            }
            "include_elven_wizard" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(26);
                return TK_INCLUDE_VALUE;
            }
            "include_oracle" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(29);
                return TK_INCLUDE_VALUE;
            }
            "include_dark_fighter" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(31);
                return TK_INCLUDE_VALUE;
            }
            "include_palus_knight" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(32);
                return TK_INCLUDE_VALUE;
            }
            "include_assasin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(35);
                return TK_INCLUDE_VALUE;
            }
            "include_dark_mage" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(38);
                return TK_INCLUDE_VALUE;
            }
            "include_dark_wizard" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(39);
                return TK_INCLUDE_VALUE;
            }
            "include_shillien_oracle" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(42);
                return TK_INCLUDE_VALUE;
            }
            "include_orc_fighter" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(44);
                return TK_INCLUDE_VALUE;
            }
            "include_orc_raider" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(45);
                return TK_INCLUDE_VALUE;
            }
            "include_orc_monk" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(47);
                return TK_INCLUDE_VALUE;
            }
            "include_orc_mage" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(49);
                return TK_INCLUDE_VALUE;
            }
            "include_orc_shaman" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(50);
                return TK_INCLUDE_VALUE;
            }
            "include_dwarven_fighter" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(53);
                return TK_INCLUDE_VALUE;
            }
            "include_scavenger" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(54);
                return TK_INCLUDE_VALUE;
            }
            "include_artisan" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(56);
                return TK_INCLUDE_VALUE;
            }
            "include_gladiator" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(2);
                return TK_INCLUDE_VALUE;
            }
            "include_warlord" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(3);
                return TK_INCLUDE_VALUE;
            }
            "include_paladin" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(5);
                return TK_INCLUDE_VALUE;
            }
            "include_dark_avenger" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(6);
                return TK_INCLUDE_VALUE;
            }
            "include_treasure_hunter" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(8);
                return TK_INCLUDE_VALUE;
            }
            "include_hawkeye" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(9);
                return TK_INCLUDE_VALUE;
            }
            "include_sorcerer" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(12);
                return TK_INCLUDE_VALUE;
            }
            "include_necromancer" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(13);
                return TK_INCLUDE_VALUE;
            }
            "include_warlock" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(14);
                return TK_INCLUDE_VALUE;
            }
            "include_bishop" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(16);
                return TK_INCLUDE_VALUE;
            }
            "include_prophet" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(17);
                return TK_INCLUDE_VALUE;
            }
            "include_temple_knight" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(20);
                return TK_INCLUDE_VALUE;
            }
            "include_swordsinger" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(21);
                return TK_INCLUDE_VALUE;
            }
            "include_plain_walker" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(23);
                return TK_INCLUDE_VALUE;
            }
            "include_silver_ranger" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(24);
                return TK_INCLUDE_VALUE;
            }
            "include_spellsinger" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(27);
                return TK_INCLUDE_VALUE;
            }
            "include_elemental_summoner" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(28);
                return TK_INCLUDE_VALUE;
            }
            "include_elder" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(30);
                return TK_INCLUDE_VALUE;
            }
            "include_shillien_knight" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(33);
                return TK_INCLUDE_VALUE;
            }
            "include_bladedancer" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(34);
                return TK_INCLUDE_VALUE;
            }
            "include_abyss_walker" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(36);
                return TK_INCLUDE_VALUE;
            }
            "include_phantom_ranger" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(37);
                return TK_INCLUDE_VALUE;
            }
            "include_spellhowler" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(40);
                return TK_INCLUDE_VALUE;
            }
            "include_phantom_summoner" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(41);
                return TK_INCLUDE_VALUE;
            }
            "include_shillien_elder" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(43);
                return TK_INCLUDE_VALUE;
            }
            "include_destroyer" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(46);
                return TK_INCLUDE_VALUE;
            }
            "include_tyrant" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(48);
                return TK_INCLUDE_VALUE;
            }
            "include_overlord" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(51);
                return TK_INCLUDE_VALUE;
            }
            "include_warcryer" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(52);
                return TK_INCLUDE_VALUE;
            }
            "include_bounty_hunter" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(55);
                return TK_INCLUDE_VALUE;
            }
            "include_warsmith" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(57);
                return TK_INCLUDE_VALUE;
            }
            "include_kamael_m_soldier" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(123);
                return TK_INCLUDE_VALUE;
            }
            "include_kamael_f_soldier" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(124);
                return TK_INCLUDE_VALUE;
            }
            "include_ertheia_wizard" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(183);
                return TK_INCLUDE_VALUE;
            }
            "include_menace" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(185);
                return TK_INCLUDE_VALUE;
            }
            "include_storm_menace" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(187);
                return TK_INCLUDE_VALUE;
            }
            "include_ertheia_fighter" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(182);
                return TK_INCLUDE_VALUE;
            }
            "include_marauder" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(184);
                return TK_INCLUDE_VALUE;
            }
            "include_lenker" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(186);
                return TK_INCLUDE_VALUE;
            }
            "include_trooper" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(125);
                return TK_INCLUDE_VALUE;
            }
            "include_warder" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(126);
                return TK_INCLUDE_VALUE;
            }
            "include_berserker" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(127);
                return TK_INCLUDE_VALUE;
            }
            "include_m_soul_breaker" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(128);
                return TK_INCLUDE_VALUE;
            }
            "include_f_soul_breaker" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(129);
                return TK_INCLUDE_VALUE;
            }
            "include_arbalester" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(130);
                return TK_INCLUDE_VALUE;
            }
            "include_inspector" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(135);
                return TK_INCLUDE_VALUE;
            }
            "include_sigel_knight" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(139);
                return TK_INCLUDE_VALUE;
            }
            "include_tir_warrior" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(140);
                return TK_INCLUDE_VALUE;
            }
            "include_othel_rogue" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(141);
                return TK_INCLUDE_VALUE;
            }
            "include_yr_archer" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(142);
                return TK_INCLUDE_VALUE;
            }
            "include_feoh_wizard" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(143);
                return TK_INCLUDE_VALUE;
            }
            "include_is_enchanter" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(144);
                return TK_INCLUDE_VALUE;
            }
            "include_wynn_summoner" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(145);
                return TK_INCLUDE_VALUE;
            }
            "include_eolh_healer" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(146);
                return TK_INCLUDE_VALUE;
            }
            "include_mass_lenker" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(190);
                return TK_INCLUDE_VALUE;
            }
            "include_seer" {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(191);
                return TK_INCLUDE_VALUE;
            }
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
            [&;={}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            "[" [^[\]]* "]" {
                EndToken();
                char16_t* const duplicate = DuplicateBracketedText(yytext, yyleng);
                if (duplicate != nullptr) {
                    *semanticSlot = grammar::PointerSemantic(duplicate);
                }
                return TK_NAME;
            }
            "@" [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                const char16_t* const itemName = yytext + 1;
                const std::int64_t value = g_PrecompiledHeaderDB.Search(itemName);
                if (value == -1) {
                    // const std::wstring hostItemName =
                    //     target_utf16::ToHostCodeUnits(itemName)
                    const std::string hostItemName = Utf16ToUtf8(itemName);
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"line[%d] : Undefined item name[%s]",
                    //     line + 1,
                    //     hostItemName.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"line[%d] : Undefined item name[%s]",
                    //     line + 1,
                    //     hostItemName.c_str())
                    L2_LOG_ERROR(
                        Log,
                        "skillacquire.txt line %lld: PCH item reference '%s' resolves to -1; parsing continues with that "
                        "numeric value.",
                        static_cast<long long>(line) + 1, hostItemName.c_str()
                    );
                }
                *semanticSlot = static_cast<grammar::SemanticBits>(value);
                return TK_INTEGER;
            }
            [\t\r _] { EndToken(); continue; }
            "\n" {
                EndToken();
                ++line;
                continue;
            }
            . {
                EndToken();
                // const std::wstring hostText =
                //     target_utf16::ToHostCodeUnits(yytext)
                const std::string hostText = Utf16ToUtf8(yytext);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     hostText.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     hostText.c_str())
                L2_LOG_ERROR(
                    Log, "skillacquire.txt line %lld: invalid input '%s' (first UTF-16 code unit 0x%04X); input skipped.",
                    static_cast<long long>(line) + 1, hostText.c_str(),
                    static_cast<unsigned int>(static_cast<std::uint16_t>(*yytext))
                );
                continue;
            }
            $ { return 0; }
        */
    }
}
