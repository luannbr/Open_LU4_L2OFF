// CLexerForTransform scanner (original L2Server/lexer_transform.l).
// re2c generates LexerTransform.cpp.

#include "L2Server/src/LexerTransform.h"

#include "L2Server/src/ParserTransform.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserTransformTokens.h"

namespace {

// Exact current images referenced by
// CLexerForTransform::CLexerForTransform@0x00cbfd70.

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

double ParseWindowsDouble(const std::u16string_view value) {
    const std::string ascii = NarrowAscii(value);
    if (ascii.empty() && !value.empty()) {
        return 0.0;
    }
    char* end = nullptr;
    const double parsed = std::strtod(ascii.c_str(), &end);
    return end == ascii.c_str() ? 0.0 : parsed;
}

char16_t* DuplicateText(const char16_t* text, const int length) {
    auto* const duplicate = static_cast<char16_t*>(std::malloc((static_cast<std::size_t>(length) + 1) * sizeof(char16_t)));
    if (duplicate == nullptr) {
        return nullptr;
    }
    std::copy_n(text, length, duplicate);
    duplicate[length] = u'\0';
    return duplicate;
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

} // namespace

CLexerForTransform::CLexerForTransform() : m_FileBuf(nullptr), m_nFilePointer(0), m_nFileSize(0), comment_count(0), line(0) {
}

CLexerForTransform::~CLexerForTransform() { m_FileBuf = nullptr; }

int CLexerForTransform::Create(char16_t* const fileBuffer, const int fileSize) {
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

int CLexerForTransform::Lex(grammar::SemanticBits* lval) {
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
            <MAIN> "transform_begin" { EndToken(); return TK_TRANSFORM_BEGIN; }
            <MAIN> "transform_end" { EndToken(); return TK_TRANSFORM_END; }
            <MAIN> "id" { EndToken(); return TK_ID; }
            <MAIN> "type" { EndToken(); return TK_TYPE; }
            <MAIN> "NON_COMBAT" { EndToken(); return TK_NON_COMBAT; }
            <MAIN> "COMBAT" { EndToken(); return TK_COMBAT; }
            <MAIN> "CURSED" { EndToken(); return TK_CURSED; }
            <MAIN> "MODE_CHANGE" { EndToken(); return TK_MODE_CHANGE; }
            <MAIN> "RIDING_MODE" { EndToken(); return TK_RIDING_MODE; }
            <MAIN> "FLYING" { EndToken(); return TK_FLYING; }
            <MAIN> "PURE_STAT" { EndToken(); return TK_PURE_STAT; }
            <MAIN> "can_swim" { EndToken(); return TK_CAN_SWIM; }
            <MAIN> "spawn_height" { EndToken(); return TK_SPAWN_HEIGHT; }
            <MAIN> "normal_attackable" { EndToken(); return TK_NORMAL_ATTACKABLE; }
            <MAIN> "can_move" { EndToken(); return TK_CAN_MOVE; }
            <MAIN> "female_begin" { EndToken(); return TK_FEMALE_BEGIN; }
            <MAIN> "female_end" { EndToken(); return TK_FEMALE_END; }
            <MAIN> "male_begin" { EndToken(); return TK_MALE_BEGIN; }
            <MAIN> "male_end" { EndToken(); return TK_MALE_END; }
            <MAIN> "common_begin" { EndToken(); return TK_COMMON_BEGIN; }
            <MAIN> "common_end" { EndToken(); return TK_COMMON_END; }
            <MAIN> "collision_box" { EndToken(); return TK_COLLISION_BOX; }
            <MAIN> "moving_speed" { EndToken(); return TK_MOVING_SPEED; }
            <MAIN> "skill" { EndToken(); return TK_SKILL; }
            <MAIN> "additional_skill" { EndToken(); return TK_ADDITIONAL_SKILL; }
            <MAIN> "item_check" { EndToken(); return TK_ITEM_CHECK; }
            <MAIN> "action" { EndToken(); return TK_ACTION; }
            <MAIN> "combat_begin" { EndToken(); return TK_COMBAT_BEGIN; }
            <MAIN> "combat_end" { EndToken(); return TK_COMBAT_END; }
            <MAIN> "basic_stat" { EndToken(); return TK_BASIC_STAT; }
            <MAIN> "base_attack_type" { EndToken(); return TK_BASE_ATTACK_TYPE; }
            <MAIN> "none" { EndToken(); return TK_NONE; }
            <MAIN> "sword" { EndToken(); return TK_SWORD; }
            <MAIN> "blunt" { EndToken(); return TK_BLUNT; }
            <MAIN> "dagger" { EndToken(); return TK_DAGGER; }
            <MAIN> "pole" { EndToken(); return TK_POLE; }
            <MAIN> "fist" { EndToken(); return TK_FIST; }
            <MAIN> "bow" { EndToken(); return TK_BOW; }
            <MAIN> "etc" { EndToken(); return TK_ETC; }
            <MAIN> "dual" { EndToken(); return TK_DUAL; }
            <MAIN> "dualfist" { EndToken(); return TK_DUALFIST; }
            <MAIN> "fishingrod" { EndToken(); return TK_FISHINGROD; }
            <MAIN> "flag" { EndToken(); return TK_FLAG; }
            <MAIN> "ownthing" { EndToken(); return TK_OWNTHING; }
            <MAIN> "base_defend" { EndToken(); return TK_BASE_DEFEND; }
            <MAIN> "base_magic_defend" { EndToken(); return TK_BASE_MAGIC_DEFEND; }
            <MAIN> "base_attack_range" { EndToken(); return TK_BASE_ATTACK_RANGE; }
            <MAIN> "base_random_damage" { EndToken(); return TK_BASE_RANDOM_DAMAGE; }
            <MAIN> "base_attack_speed" { EndToken(); return TK_BASE_ATTACK_SPEED; }
            <MAIN> "base_critical_prob" { EndToken(); return TK_BASE_CRITICAL_PROB; }
            <MAIN> "base_physical_attack" { EndToken(); return TK_BASE_PHYSICAL_ATTACK; }
            <MAIN> "base_magical_attack" { EndToken(); return TK_BASE_MAGICAL_ATTACK; }
            <MAIN> "org_hp_regen" { EndToken(); return TK_ORG_HP_REGEN; }
            <MAIN> "org_mp_regen" { EndToken(); return TK_ORG_MP_REGEN; }
            <MAIN> "org_cp_regen" { EndToken(); return TK_ORG_CP_REGEN; }
            <MAIN> "level_bonus" { EndToken(); return TK_LEVEL_BONUS; }
            <MAIN> "str_bonus" { EndToken(); return TK_STR_BONUS; }
            <MAIN> "int_bonus" { EndToken(); return TK_INT_BONUS; }
            <MAIN> "con_bonus" { EndToken(); return TK_CON_BONUS; }
            <MAIN> "dex_bonus" { EndToken(); return TK_DEX_BONUS; }
            <MAIN> "wit_bonus" { EndToken(); return TK_WIT_BONUS; }
            <MAIN> "men_bonus" { EndToken(); return TK_MEN_BONUS; }
            <MAIN> "luc_bonus" { EndToken(); return TK_LUC_BONUS; }
            <MAIN> "cha_bonus" { EndToken(); return TK_CHA_BONUS; }
            <MAIN> "hp_table" { EndToken(); return TK_HP_TABLE; }
            <MAIN> "mp_table" { EndToken(); return TK_MP_TABLE; }
            <MAIN> "cp_table" { EndToken(); return TK_CP_TABLE; }
            <MAIN> "-"? ("0" | [1-9][0-9]*) {
                EndToken();
                *semanticSlot = static_cast<grammar::SemanticBits>(
                    ParseWindowsInt64(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)))
                );
                return TK_INTEGER;
            }
            // The original lex scanner trimmed this trailing context back to the
            // longest float. A float followed by "." or by the end of the input
            // keeps its last digit there, so these two rules do the same.
            <MAIN> "-"? [0-9]+ "." ([0-9]+ | [0-9]* [eE] [+-]? [0-9][0-9]+) / "." {
                EndToken();
                const double value = ParseWindowsDouble(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                std::memcpy(semanticSlot, &value, sizeof(value));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> "-"? [0-9]+ "." [0-9]* ([eE] [+-]? [0-9]+)? / [^.] {
                if (limit_ - cursor_ == 1 && *cursor_ >= u'0' && *cursor_ <= u'9') {
                    ++cursor_;
                }
                EndToken();
                const double value = ParseWindowsDouble(std::u16string_view(yytext, static_cast<std::size_t>(yyleng)));
                std::memcpy(semanticSlot, &value, sizeof(value));
                return TK_FLOAT_NUMBER;
            }
            <MAIN> [a-zA-Z_][a-zA-Z0-9_]* {
                EndToken();
                *semanticSlot =
                    grammar::PointerSemantic(DuplicateText(yytext, yyleng));
                return TK_IDENTIFIER;
            }
            <INITIAL,MAIN> [%,:;=[\]{}~] { EndToken(); return grammar::CharacterToken(yytext[0]); }
            <INITIAL,COMMENT,MAIN> "[" [^[\]]* "]" {
                EndToken();
                *semanticSlot =
                    grammar::PointerSemantic(DuplicateInnerText(yytext, yyleng));
                return TK_NAME;
            }
            <INITIAL,MAIN> [ \t\r] { EndToken(); continue; }
            <INITIAL,MAIN> "\n" {
                EndToken();
                ++line;
                continue;
            }
            <INITIAL,MAIN> . { EndToken(); continue; }
            <INITIAL,COMMENT,MAIN> $ { return 0; }
        */
    }
}
