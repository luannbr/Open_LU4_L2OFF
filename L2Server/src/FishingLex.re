// fishingdata.txt scanner (original L2Server/fishing_lex.l).
// re2c generates FishingLex.cpp.

#include "L2Server/src/FishingLex.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/FishingParser.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

#include "FishingParserTokens.h"

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

double ParseWindowsDouble(std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    if (ascii.empty() && !value.empty()) {
        return 0.0;
    }
    char* end = nullptr;
    const double parsed = std::strtod(ascii.c_str(), &end);
    return end == ascii.c_str() ? 0.0 : parsed;
}

char16_t* DuplicateText(const char16_t* text, int length) {
    auto* const duplicate = static_cast<char16_t*>(std::malloc((static_cast<std::size_t>(length) + 1U) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, duplicate);
    duplicate[length] = u'\0';
    return duplicate;
}

char16_t* DuplicateQuotedText(const char16_t* text, int length) {
    if (length < 2) {
        return DuplicateText(u"", 0);
    }
    auto* const duplicate = static_cast<char16_t*>(std::malloc(static_cast<std::size_t>(length - 1) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text + 1, length - 2, duplicate);
    duplicate[length - 2] = u'\0';
    return duplicate;
}

} // namespace

CLexerForFishing::CLexerForFishing() : m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), line(0), m_tailPadding00d4{} {
}

CLexerForFishing::~CLexerForFishing() { m_FileBuf = nullptr; }

int CLexerForFishing::Create(char16_t* fileBuffer, int fileSize) {
    m_nFilePointer = 0;
    m_FileBuf = fileBuffer;
    m_nFileSize = fileSize;
    return 1;
}

int CLexerForFishing::getLine() { return line; }

int CLexerForFishing::Lex(grammar::SemanticBits* lval) {
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
            "//" [^\n]* / "\n" {
                EndToken();
                continue;
            }
            "/*" .* "*/" {
                EndToken();
                continue;
            }
            "distribution_begin" {
                EndToken();
                return TK_DISTRIBUTION_BEGIN;
            }
            "distribution_id" {
                EndToken();
                return TK_DISTRIBUTION_ID;
            }
            "default_distribution" {
                EndToken();
                return TK_DEFAULT_DISTRIBUTION;
            }
            "night_fishing_distribution" {
                EndToken();
                return TK_NIGHT_FISHING_DISTRIBUTION;
            }
            "distribution_end" {
                EndToken();
                return TK_DISTRIBUTION_END;
            }
            "fishing_place_begin" {
                EndToken();
                return TK_FISHING_PLACE_BEGIN;
            }
            "fishing_place_id" {
                EndToken();
                return TK_FISHING_PLACE_ID;
            }
            "territory_group" {
                EndToken();
                return TK_TERRITORY_GROUP;
            }
            "territory" {
                EndToken();
                return TK_TERRITORY;
            }
            "limit_grid" {
                EndToken();
                return TK_LIMIT_GRID;
            }
            "fishing_place_type" {
                EndToken();
                return TK_FISHING_PLACE_TYPE;
            }
            "maintain_distribution_time" {
                EndToken();
                return TK_MAINTAIN_DISTRIBUTION_TIME;
            }
            "fishing_species" {
                EndToken();
                return TK_FISHING_SPECIES;
            }
            "fishing_place_end" {
                EndToken();
                return TK_FISHING_PLACE_END;
            }
            "lure_begin" {
                EndToken();
                return TK_LURE_BEGIN;
            }
            "lure_id" {
                EndToken();
                return TK_LURE_ID;
            }
            "lure_item_id" {
                EndToken();
                return TK_LURE_ITEM_ID;
            }
            "revision_number" {
                EndToken();
                return TK_REVISION_NUMBER;
            }
            "fish_group_preference" {
                EndToken();
                return TK_FISH_GROUP_PREFERENCE;
            }
            "none" {
                EndToken();
                return TK_NONE;
            }
            "length_bonus" {
                EndToken();
                return TK_LENGTH_BONUS;
            }
            "length_rate_bonus" {
                EndToken();
                return TK_LENGTH_RATE_BONUS;
            }
            "lure_type" {
                EndToken();
                return TK_LURE_TYPE;
            }
            "lure_grade" {
                EndToken();
                return TK_LURE_GRADE;
            }
            "lure_end" {
                EndToken();
                return TK_LURE_END;
            }
            "fish_begin" {
                EndToken();
                return TK_FISH_BEGIN;
            }
            "fish_id" {
                EndToken();
                return TK_FISH_ID;
            }
            "item_id" {
                EndToken();
                return TK_ITEM_ID;
            }
            "item_name" {
                EndToken();
                return TK_ITEM_NAME;
            }
            "fish_group" {
                EndToken();
                return TK_FISH_GROUP;
            }
            "fish_level" {
                EndToken();
                return TK_FISH_LEVEL;
            }
            "fish_guts" {
                EndToken();
                return TK_FISH_GUTS;
            }
            "fish_hp" {
                EndToken();
                return TK_FISH_HP;
            }
            "hp_regen" {
                EndToken();
                return TK_HP_REGEN;
            }
            "start_combat_time" {
                EndToken();
                return TK_START_COMBAT_TIME;
            }
            "combat_duration" {
                EndToken();
                return TK_COMBAT_DURATION;
            }
            "guts_check_time" {
                EndToken();
                return TK_GUTS_CHECK_TIME;
            }
            "guts_check_probability" {
                EndToken();
                return TK_GUTS_CHECK_PROBABILITY;
            }
            "fish_grade" {
                EndToken();
                return TK_FISH_GRADE;
            }
            "fish_species" {
                EndToken();
                return TK_FISH_SPECIES;
            }
            "cheating_prob" {
                EndToken();
                return TK_CHEATING_PROB;
            }
            "fish_max_length" {
                EndToken();
                return TK_FISH_MAX_LENGTH;
            }
            "fish_length_rate" {
                EndToken();
                return TK_FISH_LENGTH_RATE;
            }
            "fish_bite_rate" {
                EndToken();
                return TK_FISH_BITE_RATE;
            }
            "fish_exp" {
                EndToken();
                return TK_FISH_EXP;
            }
            "fish_sp" {
                EndToken();
                return TK_FISH_SP;
            }
            "fish_end" {
                EndToken();
                return TK_FISH_END;
            }
            "fishingrod_begin" {
                EndToken();
                return TK_FISHINGROD_BEGIN;
            }
            "fishingrod_id" {
                EndToken();
                return TK_FISHINGROD_ID;
            }
            "fishingrod_item_id" {
                EndToken();
                return TK_FISHINGROD_ITEM_ID;
            }
            "fishingrod_level" {
                EndToken();
                return TK_FISHINGROD_LEVEL;
            }
            "fishingrod_name" {
                EndToken();
                return TK_FISHINGROD_NAME;
            }
            "fishingrod_damage" {
                EndToken();
                return TK_FISHINGROD_DAMAGE;
            }
            "fishing_duration_bonus" {
                EndToken();
                return TK_FISHING_DURATION_BONUS;
            }
            "fishingrod_end" {
                EndToken();
                return TK_FISHINGROD_END;
            }
            "fishingmonster_begin" {
                EndToken();
                return TK_FISHINGMONSTER_BEGIN;
            }
            "user_level" {
                EndToken();
                return TK_USER_LEVEL;
            }
            "monster_probability" {
                EndToken();
                return TK_MONSTER_PROBABILITY;
            }
            "fishingmonsters" {
                EndToken();
                return TK_FISHINGMONSTERS;
            }
            "fishingmonster_end" {
                EndToken();
                return TK_FISHINGMONSTER_END;
            }
            "fishing_skill_begin" {
                EndToken();
                return TK_FISHING_SKILL_BEGIN;
            }
            "fishingskill_level" {
                EndToken();
                return TK_FISHINGSKILL_LEVEL;
            }
            "fishingskill_fishid" {
                EndToken();
                return TK_FISHINGSKILL_FISHID;
            }
            "fishing_skill_end" {
                EndToken();
                return TK_FISHING_SKILL_END;
            }
            "fishing_probablity_begin" {
                EndToken();
                return TK_FISHING_PROBABLITY_BEGIN;
            }
            "probablity_percent" {
                EndToken();
                return TK_PROBABLITY_PERCENT;
            }
            "fishing_probablity_end" {
                EndToken();
                return TK_FISHING_PROBABLITY_END;
            }
            "fishing_setting_begin" {
                EndToken();
                return TK_FISHING_SETTING_BEGIN;
            }
            "fishing_setting_end" {
                EndToken();
                return TK_FISHING_SETTING_END;
            }
            "wait_next_fishing" {
                EndToken();
                return TK_WAIT_NEXT_FISHING;
            }
            "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                const char16_t* const number = yytext;
                const int length = yyleng;
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(number, static_cast<std::size_t>(std::max(length, 0))))
                );
                return TK_INTEGER;
            }
            "$" ("0" | [1-9][0-9]*) {
                EndToken();
                const char16_t* const number = yytext + 1;
                const int length = yyleng - 1;
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(number, static_cast<std::size_t>(std::max(length, 0))))
                );
                return TK_INTEGER;
            }
            [&;={}] {
                EndToken();
                return grammar::CharacterToken(yytext[0]);
            }
            "[" [^[\]]* "]" {
                EndToken();
                char16_t* const text = DuplicateQuotedText(yytext, yyleng);
                *semanticSlot = grammar::PointerSemantic(text);
                return TK_NAME;
            }
            "@" [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                const char16_t* const name = yytext + 1;
                const std::int64_t value = g_PrecompiledHeaderDB.Search(name);
                if (value == -1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"line[%d] : Undefined item name[%s]",
                    //     line + 1,
                    //     name)
                    // L2_LOG_ERROR(Log,
                    //     L"line[%d] : Undefined item name[%s]",
                    //     line + 1,
                    //     name)
                    L2_LOG_ERROR(
                        Log, "Undefined precompiled-header name '%s' at script line %lld; passing -1 to the parser.",
                        Utf16ToUtf8(std::u16string_view(name)).c_str(), static_cast<long long>(line) + 1
                    );
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
            "-"{0,2} ("0" | [1-9][0-9]*) "." [0-9]* {
                EndToken();
                const double value = ParseWindowsDouble(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                std::memcpy(semanticSlot, &value, sizeof(value));
                return TK_FLOAT_NUMBER;
            }
            [ \t\r] {
                EndToken();
                continue;
            }
            "\n" {
                EndToken();
                ++line;
                continue;
            }
            . {
                EndToken();
                // Log.Add(
                //     LOG_ERROR,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     yytext)
                // L2_LOG_ERROR(Log,
                //     L"[LEXER_ERROR] Invalid char = %s",
                //     yytext)
                L2_LOG_ERROR(
                    Log, "Unexpected input '%s' (first UTF-16 code unit 0x%04x) at script line %lld; scanning continues.",
                    Utf16ToUtf8(std::u16string_view(yytext, static_cast<std::size_t>(yyleng))).c_str(),
                    static_cast<unsigned>(static_cast<std::uint16_t>(*yytext)), static_cast<long long>(line) + 1
                );
                continue;
            }
            $ { return 0; }
        */
    }
}
