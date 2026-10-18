// npcpos.txt scanner (original L2NPC/lexer_spawn.l). The rules match
// Shared/src/LexerSpawn.re.
// re2c generates LexerSpawn.cpp.

#include "L2NPC/src/LexerSpawn.h"

#include "Runtime.h"
#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserSpawnTokens.h"

namespace l2npc {
namespace {

std::string NarrowAscii(std::u16string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char16_t unit : text) {
        result.push_back(static_cast<char>(static_cast<unsigned char>(unit)));
    }
    return result;
}

// Noise: only the kept legacy diagnostic bodies use this helper.
/*
void AppendAscii(
    std::u16string& output,
    const std::string& input) {
    for (const unsigned char value : input) {
        output.push_back(
            static_cast<char16_t>(value))
    }
}

*/
void LogInvalidSpawnCharacter(char16_t unit, int line) {
    /* Original diagnostic body, retained for recovery reference:
    if (unit == u'\uffff') {
        // VS2010 treats WCHAR 0xffff as WEOF in this formatter.
        runtime::log::WriteLogFormatterLine(
            Log,
            LOG_ERROR,
            u"log overflow. type[2] id[0] format[invalid char [%c] at line [%d]]"),
        return
    }

    std::u16string message =
        u"invalid char [",
    message.push_back(unit),
    message.append(u"] at line ["),
    AppendAscii(
        message,
        std::to_string(line)),
    message.push_back(u']'),
    runtime::log::WriteLogFormatterLine(
        Log,
        LOG_ERROR,
        message),
    */
    // Previous: L2_LOG_ERROR(Log,
    //     "Invalid UTF-16 code unit U+%04X in spawn definitions at line %d. Skipping this character.",
    //     static_cast<unsigned int>(unit), line)
    L2_LOG_ERROR(
        Log, "[spawn parser] Invalid UTF-16 code unit U+%04X at line %d; skipping this character.",
        static_cast<unsigned int>(unit), line
    );
}

void LogUndefinedPrecompiledName(std::u16string_view name, int line) {
    /* Original diagnostic body, retained for recovery reference:
    std::u16string message = u"line[",
    AppendAscii(
        message,
        std::to_string(line)),
    message.append(
        u"] : Undefined precompiled name["),
    message.append(name),
    message.push_back(u']'),
    runtime::log::WriteLogFormatterLine(
        Log,
        LOG_ERROR,
        message),
    */
    // Previous: L2_LOG_ERROR(Log,
    //     "Undefined precompiled name '%s' in spawn definitions at line %d. Continuing with value -1.",
    //     Utf16ToUtf8(name).c_str(), line)
    L2_LOG_ERROR(
        Log, "[spawn parser] Undefined precompiled name '%s' at line %d; continuing with value -1.",
        Utf16ToUtf8(name).c_str(), line
    );
}

} // namespace

CLexerForNPCSpawn::CLexerForNPCSpawn() = default;

CLexerForNPCSpawn::~CLexerForNPCSpawn() = default;

int CLexerForNPCSpawn::Create(std::uint16_t* text, int codeUnitCount) {
    // Current 0x004013ec sets the source fields.
    m_nFilePointer = 0;
    m_FileBuf = text;
    m_nFileSize = codeUnitCount;
    return 1;
}

int CLexerForNPCSpawn::Lex(grammar::SemanticBits* lval) {
    if (!HasInput()) {
        // The original lexer read m_FileBuf one code unit at a time.
        SetReader([this] { return m_nFilePointer < m_nFileSize ? static_cast<int>(m_FileBuf[m_nFilePointer++]) : -1; });
    }
    std::uint64_t& semantic = *lval;
    for (;;) {
        BeginToken();
        /*!include:re2c "UTF16Scanner.re" */
        /*!re2c
            "//" [^\n]* / "\n" { EndToken(); continue; }
            "territory_begin" { EndToken(); return TK_TERRITORY_BEGIN; }
            "territory_end" { EndToken(); return TK_TERRITORY_END; }
            "domain_begin" { EndToken(); return TK_DOMAIN_BEGIN; }
            "domain_end" { EndToken(); return TK_DOMAIN_END; }
            "domain_id" { EndToken(); return TK_DOMAIN_ID; }
            "npcmaker_begin" { EndToken(); return TK_NPCMAKER_BEGIN; }
            "npcmaker_end" { EndToken(); return TK_NPCMAKER_END; }
            "banned_territory" { EndToken(); return TK_BANNED_TERRITORY; }
            "npc_begin" { EndToken(); return TK_NPC_BEGIN; }
            "npc_end" { EndToken(); return TK_NPC_END; }
            "npc_ex_begin" { EndToken(); return TK_NPC_EX_BEGIN; }
            "npc_ex_end" { EndToken(); return TK_NPC_EX_END; }
            "npcmaker_ex_begin" { EndToken(); return TK_NPCMAKER_EX_BEGIN; }
            "npcmaker_ex_end" { EndToken(); return TK_NPCMAKER_EX_END; }
            "name" { EndToken(); return TK_NAME; }
            "pos" { EndToken(); return TK_POS; }
            "total" { EndToken(); return TK_TOTAL; }
            "respawn" { EndToken(); return TK_RESPAWN; }
            "respawn_rand" { EndToken(); return TK_RESPAWN_RAND; }
            "event_name" { EndToken(); return TK_EVENT_NAME; }
            "inzone_classid" { EndToken(); return TK_INZONE_CLASSID; }
            "-"? [0-9]+ {
                EndToken();
                const std::u16string_view lexeme(yytext, static_cast<std::size_t>(yyleng));
                const std::string text = NarrowAscii(lexeme);
                semantic = static_cast<std::uint64_t>(std::strtoull(text.c_str(), nullptr, 10));
                return TK_INTEGER;
            }
            "@" [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                const std::u16string_view lexeme(yytext, static_cast<std::size_t>(yyleng));
                const std::u16string_view name = lexeme.substr(1);
                const std::u16string ownedName(name);
                const std::int64_t value = g_PrecompiledHeaderDB.Search(ownedName.c_str());
                if (value == -1) {
                    LogUndefinedPrecompiledName(name, yylineno);
                }
                semantic = static_cast<std::uint64_t>(value);
                return TK_INTEGER;
            }
            "@\"" [^"]* "\"" {
                EndToken();
                const std::u16string_view lexeme(yytext, static_cast<std::size_t>(yyleng));
                const std::u16string_view name = lexeme.substr(2, lexeme.size() - 3);
                const std::u16string ownedName(name);
                const std::int64_t value = g_PrecompiledHeaderDB.Search(ownedName.c_str());
                if (value == -1) {
                    LogUndefinedPrecompiledName(name, yylineno);
                }
                semantic = static_cast<std::uint64_t>(value);
                return TK_INTEGER;
            }
            // The original lex scanner trimmed this trailing context back to the
            // longest float. A float followed by "." or by the end of the input
            // keeps its last digit there, so these two rules do the same.
            "-"? [0-9]+ "." ([0-9]+ | [0-9]* [eE] [+-]? [0-9][0-9]+) / "." {
                EndToken();
                const std::u16string_view lexeme(yytext, static_cast<std::size_t>(yyleng));
                const std::string text = NarrowAscii(lexeme);
                const double value = std::strtod(text.c_str(), nullptr);
                static_assert(sizeof(value) == sizeof(semantic));
                std::memcpy(&semantic, &value, sizeof(value));
                return TK_FLOAT_NUMBER;
            }
            "-"? [0-9]+ "." [0-9]* ([eE] [+-]? [0-9]+)? / [^.] {
                if (limit_ - cursor_ == 1 && *cursor_ >= u'0' && *cursor_ <= u'9') {
                    ++cursor_;
                }
                EndToken();
                const std::u16string_view lexeme(yytext, static_cast<std::size_t>(yyleng));
                const std::string text = NarrowAscii(lexeme);
                const double value = std::strtod(text.c_str(), nullptr);
                static_assert(sizeof(value) == sizeof(semantic));
                std::memcpy(&semantic, &value, sizeof(value));
                return TK_FLOAT_NUMBER;
            }
            "anywhere" { EndToken(); return TK_ANYWHERE; }
            "initial_spawn" { EndToken(); return TK_INITIAL_SPAWN; }
            "maximum_npc" { EndToken(); return TK_MAXIMUM_NPC; }
            "yes" { EndToken(); return TK_YES; }
            "no" { EndToken(); return TK_NO; }
            "ai_parameters" { EndToken(); return TK_AI_PARAMETERS; }
            "ai" { EndToken(); return TK_AI; }
            "all" { EndToken(); return TK_ALL; }
            "boss_respawn_set" { EndToken(); return TK_BOSS_RESPAWN_SET; }
            "is_chase_pc" { EndToken(); return TK_IS_CHASE_PC; }
            "flying" { EndToken(); return TK_FLYING; }
            "nickname" { EndToken(); return TK_NICKNAME; }
            "random3" { EndToken(); return TK_RANDOM3; }
            "spawn_time" { EndToken(); return TK_SPAWN_TIME; }
            "siege_warfare_start" { EndToken(); return TK_SIEGE_WARFARE_START; }
            "pc_siege_warfare_start" { EndToken(); return TK_PC_SIEGE_WARFARE_START; }
            "extended_siege_warfare_start" { EndToken(); return TK_EXTENDED_SIEGE_WARFARE_START; }
            "agit_attack_warfare_start" { EndToken(); return TK_AGIT_ATTACK_WARFARE_START; }
            "agit_defend_warfare_start" { EndToken(); return TK_AGIT_DEFEND_WARFARE_START; }
            "agit_battle_royal_start" { EndToken(); return TK_AGIT_BATTLE_ROYAL_START; }
            "agit_final_start" { EndToken(); return TK_AGIT_FINAL_START; }
            "red" {
                EndToken();
                semantic = 0;
                return TK_TEAM_COLOR;
            }
            "blue" {
                EndToken();
                semantic = 1;
                return TK_TEAM_COLOR;
            }
            "green" {
                EndToken();
                semantic = 2;
                return TK_TEAM_COLOR;
            }
            "yellow" {
                EndToken();
                semantic = 3;
                return TK_TEAM_COLOR;
            }
            "white" {
                EndToken();
                semantic = 4;
                return TK_TEAM_COLOR;
            }
            "champion" {
                EndToken();
                semantic = 0;
                return TK_FINAL_SIDE;
            }
            "challenger" {
                EndToken();
                semantic = 1;
                return TK_FINAL_SIDE;
            }
            "door_open" { EndToken(); return TK_DOOR_OPEN; }
            "WayPoints" { EndToken(); return TK_WAYPOINTS; }
            "WayPointDelays" { EndToken(); return TK_WAYPOINTDELAYS; }
            "Privates" { EndToken(); return TK_PRIVATES; }
            "dbname" { EndToken(); return TK_DBNAME; }
            "dbsaving" { EndToken(); return TK_DBSAVING; }
            "death_time" { EndToken(); return TK_DEATH_TIME; }
            "parameters" { EndToken(); return TK_PARAMETERS; }
            "on_delete_create" { EndToken(); return TK_ON_DELETE_CREATE; }
            "min" {
                EndToken();
                const int action = 64;
                // Target writes only the low int of its eight-byte yylval slot.
                const std::uint32_t unit = action == 0x40 ? 1U : action == 0x41 ? 0U : action == 0x42 ? 2U : 3U;
                std::memcpy(&semantic, &unit, sizeof(unit));
                return TK_TIME_UNIT;
            }
            "sec" {
                EndToken();
                const int action = 65;
                // Target writes only the low int of its eight-byte yylval slot.
                const std::uint32_t unit = action == 0x40 ? 1U : action == 0x41 ? 0U : action == 0x42 ? 2U : 3U;
                std::memcpy(&semantic, &unit, sizeof(unit));
                return TK_TIME_UNIT;
            }
            "hour" {
                EndToken();
                const int action = 66;
                // Target writes only the low int of its eight-byte yylval slot.
                const std::uint32_t unit = action == 0x40 ? 1U : action == 0x41 ? 0U : action == 0x42 ? 2U : 3U;
                std::memcpy(&semantic, &unit, sizeof(unit));
                return TK_TIME_UNIT;
            }
            "day" {
                EndToken();
                const int action = 67;
                // Target writes only the low int of its eight-byte yylval slot.
                const std::uint32_t unit = action == 0x40 ? 1U : action == 0x41 ? 0U : action == 0x42 ? 2U : 3U;
                std::memcpy(&semantic, &unit, sizeof(unit));
                return TK_TIME_UNIT;
            }
            "[" [^[\]]* "]" {
                EndToken();
                const std::u16string_view lexeme(yytext, static_cast<std::size_t>(yyleng));
                const std::u16string_view value = lexeme.substr(1, lexeme.size() - 2);
                char16_t* const output = static_cast<char16_t*>(std::malloc((value.size() + 1) * sizeof(char16_t)));
                if (output) {
                    std::copy(value.begin(), value.end(), output);
                    output[value.size()] = u'\0';
                    semantic = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(output));
                }
                return TK_BRACKETED_NAME;
            }
            [%(),;=[\]{}] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            [ \t\r] { EndToken(); continue; }
            "\n" { EndToken(); continue; }
            . {
                EndToken();
                const std::u16string_view lexeme(yytext, static_cast<std::size_t>(yyleng));
                LogInvalidSpawnCharacter(lexeme.front(), yylineno);
                continue;
            }
            $ { return 0; }
        */
    }
}

} // namespace l2npc
