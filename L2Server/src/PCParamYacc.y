// PC_parameter.txt grammar (original L2Server/pc_param_yacc.y).
// Bison generates PCParamYacc.cpp and PCParamYaccTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/PCParamYacc.h"

#include "Shared/src/TextRuntime.h"

#include "Shared/src/Log.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string_view>

#include "PCParamYaccTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForPCParam::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"PCParamYaccTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

struct SRCToSexRaceClass {
    std::int32_t src;
    SexType sex;
    RaceType race;
    FighterMagicType cls;
};

constexpr std::array<SRCToSexRaceClass, 28> kSrcTable{{
    {0, ST_FEMALE, RT_HUMAN, FMT_FIGHTER},
    {1, ST_MALE, RT_HUMAN, FMT_FIGHTER},
    {2, ST_FEMALE, RT_HUMAN, FMT_MAGIC},
    {3, ST_MALE, RT_HUMAN, FMT_MAGIC},
    {4, ST_FEMALE, RT_ELF, FMT_FIGHTER},
    {5, ST_MALE, RT_ELF, FMT_FIGHTER},
    {6, ST_FEMALE, RT_ELF, FMT_MAGIC},
    {7, ST_MALE, RT_ELF, FMT_MAGIC},
    {8, ST_FEMALE, RT_DARKELF, FMT_FIGHTER},
    {9, ST_MALE, RT_DARKELF, FMT_FIGHTER},
    {10, ST_FEMALE, RT_DARKELF, FMT_MAGIC},
    {11, ST_MALE, RT_DARKELF, FMT_MAGIC},
    {12, ST_FEMALE, RT_ORC, FMT_FIGHTER},
    {13, ST_MALE, RT_ORC, FMT_FIGHTER},
    {14, ST_FEMALE, RT_ORC, FMT_MAGIC},
    {15, ST_MALE, RT_ORC, FMT_MAGIC},
    {16, ST_FEMALE, RT_DWARF, FMT_FIGHTER},
    {17, ST_MALE, RT_DWARF, FMT_FIGHTER},
    {18, ST_FEMALE, RT_DWARF, FMT_MAGIC},
    {19, ST_MALE, RT_DWARF, FMT_MAGIC},
    {20, ST_FEMALE, RT_KAMAEL, FMT_FIGHTER},
    {21, ST_MALE, RT_KAMAEL, FMT_FIGHTER},
    {22, ST_FEMALE, RT_KAMAEL, FMT_MAGIC},
    {23, ST_MALE, RT_KAMAEL, FMT_MAGIC},
    {24, ST_FEMALE, RT_ERTHEIA, FMT_FIGHTER},
    {25, ST_MALE, RT_ERTHEIA, FMT_FIGHTER},
    {26, ST_FEMALE, RT_ERTHEIA, FMT_MAGIC},
    {27, ST_MALE, RT_ERTHEIA, FMT_MAGIC},
}};

grammar::SemanticBits DoubleSemantic(double value) {
    grammar::SemanticBits result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

bool ContextEquals(const char16_t* value, const char16_t* expected) {
    if (value == nullptr || expected == nullptr) {
        return value == expected;
    }
    while (*value == *expected && *value != 0) {
        ++value;
        ++expected;
    }
    return *value == *expected;
}

PCParamFieldBySRC ScalarSRCField(const char16_t* context) {
    if (ContextEquals(context, u"base_physical_attack")) {
        return PC_BASE_PATTACK;
    }
    if (ContextEquals(context, u"base_critical")) {
        return PC_BASE_CRITICAL;
    }
    if (ContextEquals(context, u"base_attack_type")) {
        return PC_BASE_ATTACK_TYPE;
    }
    if (ContextEquals(context, u"base_attack_speed")) {
        return PC_BASE_ATTACK_SPEED;
    }
    if (ContextEquals(context, u"base_defend")) {
        return PC_BASE_PDEFEND;
    }
    if (ContextEquals(context, u"base_magic_attack")) {
        return PC_BASE_MATTACK;
    }
    if (ContextEquals(context, u"base_magic_defend")) {
        return PC_BASE_MDEFEND;
    }
    if (ContextEquals(context, u"base_can_penetrate")) {
        return PC_BASE_CAN_PENETRATE;
    }
    if (ContextEquals(context, u"base_attack_range")) {
        return PC_BASE_ATTACK_RANGE;
    }
    if (ContextEquals(context, u"base_rand_dam")) {
        return PC_BASE_RAND_DAM;
    }
    if (ContextEquals(context, u"org_jump")) {
        return PC_ORG_JUMP;
    }
    if (ContextEquals(context, u"pc_breath_bonus_table")) {
        return PC_BREATH_BONUS;
    }
    if (ContextEquals(context, u"pc_safe_fall_height_table")) {
        return PC_SAFE_FALL_HEIGHT;
    }
    return static_cast<PCParamFieldBySRC>(-1);
}

// void LogBadContext(const char16_t* context) {
//     // Log.Add(
//     //     LOG_ERROR,
//     //     L"[%s][%d] Invalide parsing_context[%s]",
//     //     L"pc_param_yacc.y",
//     //     0x1c4,
//     //     context)
//     L2_LOG_ERROR(Log,
//         L"[%s][%d] Invalide parsing_context[%s]",
//         L"pc_param_yacc.y",
//         0x1c4,
//         context)
// }
void LogBadContext(const char16_t* context, int line) {
    L2_LOG_ERROR(
        Log, "PC_parameter.txt has an unknown field '%s' at script line %lld; assignment is skipped.",
        context != nullptr ? Utf16ToUtf8(std::u16string_view(context)).c_str() : "<null>", static_cast<long long>(line) + 1
    );
}

// void LogCounterMismatch(const wchar_t* message, int line) {
//     // Log.Add(
//     //     LOG_ERROR,
//     //     message,
//     //     L"..\\Script\\PC_parameter.txt",
//     //     line)
//     L2_LOG_ERROR(Log,
//         message,
//         L"..\\Script\\PC_parameter.txt",
//         line)
// }
void LogCounterMismatch(const char16_t* context, std::size_t actual, std::size_t expected, bool maximum, int line) {
    L2_LOG_ERROR(
        Log, "PC_parameter.txt field '%s' at script line %lld has %zu values; %s %zu. Processing continues.",
        context != nullptr ? Utf16ToUtf8(std::u16string_view(context)).c_str() : "<null>", static_cast<long long>(line) + 1,
        actual, maximum ? "maximum" : "expected", expected
    );
}

} // namespace

CParserForPCParam::CParserForPCParam() : m_lexer(), DoubleVector(), pDoubleVector(nullptr), m_mapParameter() {}

CParserForPCParam::~CParserForPCParam() = default;

int CParserForPCParam::Create() {
    return m_lexer.Create();
}
}

%token FFIGHTER_GROUP
%token NAME
%token IDENTIFIER
%token NONE_GROUP
%token PERCENT
%token FLOAT_NUMBER
%token LVL_VALUE
%token ATTRIBUTE_VALUE
%token FIGHTER_HP_TABLE_BEGIN_GROUP
%token FIGHTER_HP_TABLE_END_GROUP
%token FIGHTER_MP_TABLE_BEGIN_GROUP
%token FIGHTER_MP_TABLE_END_GROUP
%token FIGHTER_CP_TABLE_BEGIN_GROUP
%token FIGHTER_CP_TABLE_END_GROUP
%token INITIAL_DEFAULT_ATTRIBUTE_BEGIN
%token INITIAL_DEFAULT_ATTRIBUTE_END
%token INITIAL_LOWEST_ATTRIBUTE_BEGIN
%token INITIAL_LOWEST_ATTRIBUTE_END
%token INITIAL_HIGHEST_ATTRIBUTE_BEGIN
%token INITIAL_HIGHEST_ATTRIBUTE_END
%token BASE_PHYSICAL_ATTACK_BEGIN
%token BASE_PHYSICAL_ATTACK_END
%token BASE_CRITICAL_BEGIN
%token BASE_CRITICAL_END
%token BASE_ATTACK_TYPE_BEGIN
%token BASE_ATTACK_TYPE_END
%token BASE_ATTACK_SPEED_BEGIN
%token BASE_ATTACK_SPEED_END
%token BASE_DEFEND_BEGIN
%token BASE_DEFEND_END
%token BASE_MAGIC_ATTACK_BEGIN
%token BASE_MAGIC_ATTACK_END
%token BASE_MAGIC_DEFEND_BEGIN
%token BASE_MAGIC_DEFEND_END
%token BASE_CAN_PENETRATE_BEGIN
%token BASE_CAN_PENETRATE_END
%token BASE_ATTACK_RANGE_BEGIN
%token BASE_ATTACK_RANGE_END
%token BASE_DAMAGE_RANGE_BEGIN
%token BASE_DAMAGE_RANGE_END
%token BASE_RAND_DAM_BEGIN
%token BASE_RAND_DAM_END
%token ORG_HP_REGEN_BEGIN
%token ORG_HP_REGEN_END
%token ORG_MP_REGEN_BEGIN
%token ORG_MP_REGEN_END
%token ORG_CP_REGEN_BEGIN
%token ORG_CP_REGEN_END
%token MOVING_SPEED_BEGIN
%token MOVING_SPEED_END
%token ORG_JUMP_BEGIN
%token ORG_JUMP_END
%token PC_BREATH_BONUS_TABLE_BEGIN
%token PC_BREATH_BONUS_TABLE_END
%token PC_SAFE_FALL_HEIGHT_TABLE_BEGIN
%token PC_SAFE_FALL_HEIGHT_TABLE_END
%token PC_COLLISION_BOX_TABLE_BEGIN
%token PC_COLLISION_BOX_TABLE_END
%token PC_KARMA_INCREASE_TABLE_BEGIN
%token PC_KARMA_INCREASE_TABLE_END
%token PC_KARMA_INCREASE_CONSTANT_BEGIN
%token PC_KARMA_INCREASE_CONSTANT_END
%token PENALTY_START_KARMA
%token PENALTY_DURATION_DEFAULT
%token PENALTY_DURATION_INCREASE
%token DOWN_TIME_MULTIPLE
%token CRIMINAL_DURATION_MULTIPLE
%token REGEN_MOVE_MODE_BONUS_BEGIN
%token REGEN_MOVE_MODE_BONUS_END
%token NOISE_MOVE_MODE_BONUS_BEGIN
%token NOISE_MOVE_MODE_BONUS_END
%token HIT_COND_BONUS_BEGIN
%token HIT_COND_BONUS_END
%token SIT
%token STAND
%token LOW
%token HIGH
%token AHEAD
%token SIDE
%token BACK
%token DARK
%token RAIN
%token LEVEL_BONUS_BEGIN
%token LEVEL_BONUS_END
%token STR_BONUS_BEGIN
%token STR_BONUS_END
%token INT_BONUS_BEGIN
%token INT_BONUS_END
%token CON_BONUS_BEGIN
%token CON_BONUS_END
%token MEN_BONUS_BEGIN
%token MEN_BONUS_END
%token DEX_BONUS_BEGIN
%token DEX_BONUS_END
%token WIT_BONUS_BEGIN
%token WIT_BONUS_END
%token LUC_BONUS_BEGIN
%token LUC_BONUS_END
%token CHA_BONUS_BEGIN
%token CHA_BONUS_END
%token ORG_HP_REGEN_WEIGHT_BEGIN
%token ORG_HP_REGEN_WEIGHT_END
%token ORG_MP_REGEN_WEIGHT_BEGIN
%token ORG_MP_REGEN_WEIGHT_END
%token ORG_CP_REGEN_WEIGHT_BEGIN
%token ORG_CP_REGEN_WEIGHT_END
%token PC_STAT_MAX_BEGIN
%token PC_STAT_MAX_END
%token PC_STAT_MAX_STR
%token PC_STAT_MAX_DEX
%token PC_STAT_MAX_CON
%token PC_STAT_MAX_INT
%token PC_STAT_MAX_WIT
%token PC_STAT_MAX_MEN
%token PC_STANDARD_EXP_BY_LEVEL_BEGIN
%token PC_STANDARD_EXP_BY_LEVEL_END
// The original grammar has these conflicts. Bison resolves them as the
// original tables did. A shift wins over a reduce.
%expect 3
%start n1

%%

n1
    : n1 n2
    | n2
    ;

n2
    : initial_default_attribute
    | initial_lowest_attribute
    | initial_highest_attribute
    | base_physical_attack
    | base_critical
    | base_attack_type
    | base_attack_speed
    | base_magic_attack
    | base_can_penetrate
    | base_attack_range
    | base_damage_range
    | base_rand_dam
    | org_hp_regen
    | org_mp_regen
    | org_cp_regen
    | base_defend
    | base_magic_defend
    | moving_speed
    | org_jump
    | pc_breath_bonus_table
    | pc_safe_fall_height_table
    | pc_collision_box_table
    | level_bonus
    | str_bonus
    | int_bonus
    | con_bonus
    | men_bonus
    | dex_bonus
    | wit_bonus
    | luc_bonus
    | cha_bonus
    | fighter_hp_table_begin_group_def_list
    | fighter_mp_table_begin_group_def_list
    | fighter_cp_table_begin_group_def_list
    | pc_karma_increase_table
    | regen_move_mode_bonus
    | org_hp_regen_weight
    | org_mp_regen_weight
    | org_cp_regen_weight
    | hit_cond_bonus
    | noise_move_mode_bonus
    | pc_karma_increase_constant
    | pc_stat_max
    | pc_standard_exp_by_level
    ;

initial_default_attribute
    : INITIAL_DEFAULT_ATTRIBUTE_BEGIN
      { parsing_context = u"initial_default_attribute"; }
      ffighter_group_def_list INITIAL_DEFAULT_ATTRIBUTE_END
    ;

initial_lowest_attribute
    : INITIAL_LOWEST_ATTRIBUTE_BEGIN
      { parsing_context = u"initial_lowest_attribute"; }
      ffighter_group_def_list INITIAL_LOWEST_ATTRIBUTE_END
    ;

initial_highest_attribute
    : INITIAL_HIGHEST_ATTRIBUTE_BEGIN
      { parsing_context = u"initial_highest_attribute"; }
      ffighter_group_def_list INITIAL_HIGHEST_ATTRIBUTE_END
    ;

base_damage_range
    : BASE_DAMAGE_RANGE_BEGIN
      { parsing_context = u"base_damage_range"; }
      ffighter_group_def_list BASE_DAMAGE_RANGE_END
    ;

org_hp_regen
    : ORG_HP_REGEN_BEGIN
      { parsing_context = u"org_hp_regen"; }
      ffighter_group_def_list ORG_HP_REGEN_END
    ;

org_mp_regen
    : ORG_MP_REGEN_BEGIN
      { parsing_context = u"org_mp_regen"; }
      ffighter_group_def_list ORG_MP_REGEN_END
    ;

org_cp_regen
    : ORG_CP_REGEN_BEGIN
      { parsing_context = u"org_cp_regen"; }
      ffighter_group_def_list ORG_CP_REGEN_END
    ;

base_defend
    : BASE_DEFEND_BEGIN
      { parsing_context = u"base_defend"; }
      ffighter_group_def_list BASE_DEFEND_END
    ;

base_magic_defend
    : BASE_MAGIC_DEFEND_BEGIN
      { parsing_context = u"base_magic_defend"; }
      ffighter_group_def_list BASE_MAGIC_DEFEND_END
    ;

moving_speed
    : MOVING_SPEED_BEGIN
      { parsing_context = u"moving_speed"; }
      ffighter_group_def_list MOVING_SPEED_END
    ;

pc_collision_box_table
    : PC_COLLISION_BOX_TABLE_BEGIN
      { parsing_context = u"pc_collision_box_table"; }
      ffighter_group_def_list PC_COLLISION_BOX_TABLE_END
    ;

ffighter_group_def_list
    : ffighter_group_def_list ffighter_group_def
    | ffighter_group_def
    ;

ffighter_group_def
    : FFIGHTER_GROUP '=' '{' n27 '}'
      {
          PCParamDoubleVector* const values =
              grammar::SemanticPointer<PCParamDoubleVector>($4);
          const SRCToSexRaceClass& source = kSrcTable[static_cast<std::size_t>(grammar::SemanticInt64($1))];
          const auto value = [&](std::size_t index) { return DoubleVector[index]; };
          if (ContextEquals(parsing_context, u"initial_default_attribute") ||
              ContextEquals(parsing_context, u"initial_lowest_attribute") ||
              ContextEquals(parsing_context, u"initial_highest_attribute")) {
              const bool isDefault = ContextEquals(parsing_context, u"initial_default_attribute");
              const bool isLowest = ContextEquals(parsing_context, u"initial_lowest_attribute");
              if (DoubleVector.size() != 6U) {
                  // LogCounterMismatch(
                  //     isDefault
                  //         ? L"%s line[%d], initial_default_attribute int counter is different"
                  //         : isLowest
                  //             ? L"%s line[%d], initial_lowest_attribute int counter is different"
                  //             : L"%s line[%d], initial_highest_attribute int counter is different",
                  //     m_lexer.line)
                  LogCounterMismatch(parsing_context, DoubleVector.size(), 6, false, m_lexer.line);
              }
              const PCParamFieldBySRC fields[6] = {
                  isDefault      ? PC_INT_DEFAULT
                      : isLowest ? PC_INT_MIN
                                 : PC_INT_MAX,
                  isDefault      ? PC_STR_DEFAULT
                      : isLowest ? PC_STR_MIN
                                 : PC_STR_MAX,
                  isDefault      ? PC_CON_DEFAULT
                      : isLowest ? PC_CON_MIN
                                 : PC_CON_MAX,
                  isDefault      ? PC_MEN_DEFAULT
                      : isLowest ? PC_MEN_MIN
                                 : PC_MEN_MAX,
                  isDefault      ? PC_DEX_DEFAULT
                      : isLowest ? PC_DEX_MIN
                                 : PC_DEX_MAX,
                  isDefault      ? PC_WIT_DEFAULT
                      : isLowest ? PC_WIT_MIN
                                 : PC_WIT_MAX,
              };
              for (std::size_t index = 0; index < 6; ++index) {
                  g_PCParamDB.SetValueBySRC(
                      source.sex, source.race, source.cls, fields[index], static_cast<int>(value(index))
                  );
              }
          } else if (ContextEquals(parsing_context, u"base_damage_range")) {
              if (DoubleVector.size() != 4U) {
                  // LogCounterMismatch(
                  //     L"%s line[%d], base_damage_range int counter is different",
                  //     m_lexer.line)
                  LogCounterMismatch(parsing_context, DoubleVector.size(), 4, false, m_lexer.line);
              }
              g_PCParamDB.SetValueBySRC(
                  source.sex, source.race, source.cls, PC_BASE_DAMAGE_RANGE, static_cast<int>(value(0)),
                  static_cast<int>(value(1)), static_cast<int>(value(2)), static_cast<int>(value(3))
              );
          } else if (ContextEquals(parsing_context, u"org_hp_regen") || ContextEquals(parsing_context, u"org_mp_regen") ||
              ContextEquals(parsing_context, u"org_cp_regen")) {
              if (DoubleVector.size() > 121U) {
                  // LogCounterMismatch(
                  //     ContextEquals(parsing_context, u"org_hp_regen")
                  //         ? L"%s line[%d], org_hp_regen int counter is different"
                  //         : ContextEquals(parsing_context, u"org_mp_regen")
                  //             ? L"%s line[%d], org_mp_regen int counter is different"
                  //             : L"%s line[%d], org_cp_regen int counter is different",
                  //     m_lexer.line)
                  LogCounterMismatch(parsing_context, DoubleVector.size(), 121, true, m_lexer.line);
              }
              const PCParamFieldBySRC field = ContextEquals(parsing_context, u"org_hp_regen") ? PC_ORG_HP_REGEN
                  : ContextEquals(parsing_context, u"org_mp_regen")                           ? PC_ORG_MP_REGEN
                                                                                              : PC_ORG_CP_REGEN;
              g_PCParamDB.SetValueBySRC(source.sex, source.race, source.cls, field, values);
          } else if (ContextEquals(parsing_context, u"moving_speed")) {
              if (DoubleVector.size() != 8U) {
                  // LogCounterMismatch(
                  //     L"%s line[%d], moving_speed int counter is different",
                  //     m_lexer.line)
                  LogCounterMismatch(parsing_context, DoubleVector.size(), 8, false, m_lexer.line);
              }
              g_PCParamDB.SetValueBySRC(
                  source.sex, source.race, source.cls, PC_MOVING_SPEED, static_cast<int>(value(0)),
                  static_cast<int>(value(1)), static_cast<int>(value(2)), static_cast<int>(value(3)),
                  static_cast<int>(value(4)), static_cast<int>(value(5)), static_cast<int>(value(6)),
                  static_cast<int>(value(7))
              );
          } else if (ContextEquals(parsing_context, u"pc_collision_box_table")) {
              if (DoubleVector.size() != 2U) {
                  // LogCounterMismatch(
                  //     L"%s line[%d], pc_collision_box_table int counter is different",
                  //     m_lexer.line)
                  LogCounterMismatch(parsing_context, DoubleVector.size(), 2, false, m_lexer.line);
              }
              g_PCParamDB.SetValueBySRC(source.sex, source.race, source.cls, PC_COLLISION_BOX, value(0), value(1));
          } else if (ContextEquals(parsing_context, u"base_defend")) {
              if (DoubleVector.size() != 7U) {
                  // LogCounterMismatch(
                  //     L"%s line[%d], org_hp_regen int counter is different",
                  //     m_lexer.line)
                  LogCounterMismatch(parsing_context, DoubleVector.size(), 7, false, m_lexer.line);
              }
              g_PCParamDB.SetValueBySRC(
                  source.sex, source.race, source.cls, PC_BASE_PDEFEND, value(0), value(1), value(2), value(3), value(4),
                  value(5), value(6), 0.0
              );
          } else if (ContextEquals(parsing_context, u"base_magic_defend")) {
              if (DoubleVector.size() != 5U) {
                  // LogCounterMismatch(
                  //     L"%s line[%d], org_hp_regen int counter is different",
                  //     m_lexer.line)
                  LogCounterMismatch(parsing_context, DoubleVector.size(), 5, false, m_lexer.line);
              }
              g_PCParamDB.SetValueBySRC(
                  source.sex, source.race, source.cls, PC_BASE_MDEFEND, value(0), value(1), value(2), value(3), value(4),
                  0.0, 0.0, 0.0
              );
          } else {
              // LogBadContext(parsing_context)
              LogBadContext(parsing_context, m_lexer.line);
          }
          delete values;
      }
    ;

n27
    : n27 ';' none_group_def
      {
          const double value = grammar::SemanticDouble($3);
          DoubleVector.push_back(value);
          auto* const values = grammar::SemanticPointer<PCParamDoubleVector>($1);
          if (values != nullptr) {
              values->push_back(value);
          }
          $$ = $1;
      }
    | none_group_def
      {
          DoubleVector.clear();
          const double value = grammar::SemanticDouble($1);
          DoubleVector.push_back(value);
          auto* const values = new PCParamDoubleVector();
          values->push_back(value);
          $$ =
              grammar::PointerSemantic(values);
      }
    ;

base_physical_attack
    : BASE_PHYSICAL_ATTACK_BEGIN
      { parsing_context = u"base_physical_attack"; }
      ffighter_group_def_2_list BASE_PHYSICAL_ATTACK_END
    ;

base_critical
    : BASE_CRITICAL_BEGIN
      { parsing_context = u"base_critical"; }
      ffighter_group_def_2_list BASE_CRITICAL_END
    ;

base_attack_type
    : BASE_ATTACK_TYPE_BEGIN
      { parsing_context = u"base_attack_type"; }
      ffighter_group_def_2_list BASE_ATTACK_TYPE_END
    ;

base_attack_speed
    : BASE_ATTACK_SPEED_BEGIN
      { parsing_context = u"base_attack_speed"; }
      ffighter_group_def_2_list BASE_ATTACK_SPEED_END
    ;

base_magic_attack
    : BASE_MAGIC_ATTACK_BEGIN
      { parsing_context = u"base_magic_attack"; }
      ffighter_group_def_2_list BASE_MAGIC_ATTACK_END
    ;

base_can_penetrate
    : BASE_CAN_PENETRATE_BEGIN
      { parsing_context = u"base_can_penetrate"; }
      ffighter_group_def_2_list BASE_CAN_PENETRATE_END
    ;

base_attack_range
    : BASE_ATTACK_RANGE_BEGIN
      { parsing_context = u"base_attack_range"; }
      ffighter_group_def_2_list BASE_ATTACK_RANGE_END
    ;

base_rand_dam
    : BASE_RAND_DAM_BEGIN
      { parsing_context = u"base_rand_dam"; }
      ffighter_group_def_2_list BASE_RAND_DAM_END
    ;

org_jump
    : ORG_JUMP_BEGIN
      { parsing_context = u"org_jump"; }
      ffighter_group_def_2_list ORG_JUMP_END
    ;

pc_breath_bonus_table
    : PC_BREATH_BONUS_TABLE_BEGIN
      { parsing_context = u"pc_breath_bonus_table"; }
      ffighter_group_def_2_list PC_BREATH_BONUS_TABLE_END
    ;

pc_safe_fall_height_table
    : PC_SAFE_FALL_HEIGHT_TABLE_BEGIN
      { parsing_context = u"pc_safe_fall_height_table"; }
      ffighter_group_def_2_list PC_SAFE_FALL_HEIGHT_TABLE_END
    ;

ffighter_group_def_2_list
    : ffighter_group_def_2_list ffighter_group_def_2
    | ffighter_group_def_2
    ;

ffighter_group_def_2
    : FFIGHTER_GROUP '=' none_group_def
      {
          const SRCToSexRaceClass& source = kSrcTable[static_cast<std::size_t>(grammar::SemanticInt64($1))];
          const PCParamFieldBySRC field = ScalarSRCField(parsing_context);
          if (static_cast<int>(field) < 0) {
              // LogBadContext(parsing_context)
              LogBadContext(parsing_context, m_lexer.line);
              break;
          }
          g_PCParamDB.SetValueBySRC(
              source.sex, source.race, source.cls, field, static_cast<int>(grammar::SemanticDouble($3))
          );
      }
    ;

level_bonus
    : LEVEL_BONUS_BEGIN
      { parsing_context = u"level_bonus"; }
      n83 LEVEL_BONUS_END
    ;

str_bonus
    : STR_BONUS_BEGIN
      { parsing_context = u"str_bonus"; }
      n83 STR_BONUS_END
    ;

int_bonus
    : INT_BONUS_BEGIN
      { parsing_context = u"int_bonus"; }
      n83 INT_BONUS_END
    ;

con_bonus
    : CON_BONUS_BEGIN
      { parsing_context = u"con_bonus"; }
      n83 CON_BONUS_END
    ;

men_bonus
    : MEN_BONUS_BEGIN
      { parsing_context = u"men_bonus"; }
      n83 MEN_BONUS_END
    ;

dex_bonus
    : DEX_BONUS_BEGIN
      { parsing_context = u"dex_bonus"; }
      n83 DEX_BONUS_END
    ;

wit_bonus
    : WIT_BONUS_BEGIN
      { parsing_context = u"wit_bonus"; }
      n83 WIT_BONUS_END
    ;

luc_bonus
    : LUC_BONUS_BEGIN
      { parsing_context = u"luc_bonus"; }
      n83 LUC_BONUS_END
    ;

cha_bonus
    : CHA_BONUS_BEGIN
      { parsing_context = u"cha_bonus"; }
      n83 CHA_BONUS_END
    ;

fighter_hp_table_begin_group_def_list
    : fighter_hp_table_begin_group_def_list fighter_hp_table_begin_group_def
    | fighter_hp_table_begin_group_def
    ;

fighter_hp_table_begin_group_def
    : FIGHTER_HP_TABLE_BEGIN_GROUP
      {
          parsing_context = u"hp_table";
          parsing_class = static_cast<std::int32_t>(grammar::SemanticInt64($1));
      }
      n83 FIGHTER_HP_TABLE_END_GROUP
    ;

fighter_mp_table_begin_group_def_list
    : fighter_mp_table_begin_group_def_list fighter_mp_table_begin_group_def
    | fighter_mp_table_begin_group_def
    ;

fighter_mp_table_begin_group_def
    : FIGHTER_MP_TABLE_BEGIN_GROUP
      {
          parsing_context = u"mp_table";
          parsing_class = static_cast<std::int32_t>(grammar::SemanticInt64($1));
      }
      n83 FIGHTER_MP_TABLE_END_GROUP
    ;

fighter_cp_table_begin_group_def_list
    : fighter_cp_table_begin_group_def_list fighter_cp_table_begin_group_def
    | fighter_cp_table_begin_group_def
    ;

fighter_cp_table_begin_group_def
    : FIGHTER_CP_TABLE_BEGIN_GROUP
      {
          parsing_context = u"cp_table";
          parsing_class = static_cast<std::int32_t>(grammar::SemanticInt64($1));
      }
      n83 FIGHTER_CP_TABLE_END_GROUP
    ;

pc_karma_increase_table
    : PC_KARMA_INCREASE_TABLE_BEGIN
      { parsing_context = u"pc_karma_increase_table"; }
      n83 PC_KARMA_INCREASE_TABLE_END
    ;

pc_standard_exp_by_level
    : PC_STANDARD_EXP_BY_LEVEL_BEGIN
      { parsing_context = u"pc_standard_exp_by_level_table"; }
      n83 PC_STANDARD_EXP_BY_LEVEL_END
    ;

n83
    : lvl_value_def_list
    | attribute_value_def_list
    ;

lvl_value_def_list
    : lvl_value_def_list lvl_value_def
    | lvl_value_def
    ;

lvl_value_def
    : LVL_VALUE '=' none_group_def
      {
          const int level = static_cast<int>(grammar::SemanticInt64($1));
          const double value = grammar::SemanticDouble($3);
          if (ContextEquals(parsing_context, u"level_bonus")) {
              g_PCParamDB.SetValueByLevel(level, PC_LEVEL_BONUS, value);
          } else if (ContextEquals(parsing_context, u"hp_table")) {
              g_PCParamDB.SetValueByLevelClass(level, static_cast<ClassType>(parsing_class), PC_ORG_HP, value);
          } else if (ContextEquals(parsing_context, u"mp_table")) {
              g_PCParamDB.SetValueByLevelClass(level, static_cast<ClassType>(parsing_class), PC_ORG_MP, value);
          } else if (ContextEquals(parsing_context, u"cp_table")) {
              g_PCParamDB.SetValueByLevelClass(level, static_cast<ClassType>(parsing_class), PC_ORG_CP, value);
          } else if (ContextEquals(parsing_context, u"pc_karma_increase_table")) {
              g_PCParamDB.SetValueByLevel(level, PC_KARMA_INCREASE, value);
          } else if (ContextEquals(parsing_context, u"pc_standard_exp_by_level_table")) {
              g_PCParamDB.SetStandardExpByLevel(level, PC_STANDARD_EXP_BY_LEV, static_cast<std::int64_t>(value));
          } else {
              // LogBadContext(parsing_context)
              LogBadContext(parsing_context, m_lexer.line);
          }
      }
    ;

attribute_value_def_list
    : attribute_value_def_list attribute_value_def
    | attribute_value_def
    ;

attribute_value_def
    : ATTRIBUTE_VALUE '=' none_group_def
      {
          const int level = static_cast<int>(grammar::SemanticInt64($1));
          const int value = static_cast<int>(grammar::SemanticDouble($3));
          PCParamFieldByLevel field = PC_INT_BONUS;
          if (ContextEquals(parsing_context, u"str_bonus")) {
              field = PC_STR_BONUS;
          } else if (ContextEquals(parsing_context, u"int_bonus")) {
              field = PC_INT_BONUS;
          } else if (ContextEquals(parsing_context, u"con_bonus")) {
              field = PC_CON_BONUS;
          } else if (ContextEquals(parsing_context, u"men_bonus")) {
              field = PC_MEN_BONUS;
          } else if (ContextEquals(parsing_context, u"dex_bonus")) {
              field = PC_DEX_BONUS;
          } else if (ContextEquals(parsing_context, u"wit_bonus")) {
              field = PC_WIT_BONUS;
          } else if (ContextEquals(parsing_context, u"luc_bonus")) {
              field = PC_LUC_BONUS;
          } else if (ContextEquals(parsing_context, u"cha_bonus")) {
              field = PC_CHA_BONUS;
          } else {
              break;
          }
          g_PCParamDB.SetValueByLevel(level, field, value);
      }
    ;

regen_move_mode_bonus
    : REGEN_MOVE_MODE_BONUS_BEGIN SIT '=' PERCENT
      {
          const auto mode = static_cast<MoveModeEnum>(0);
          const double value = static_cast<double>(grammar::SemanticInt64($4) + 100) / 100.0;
          g_PCParamDB.SetRegenMoveModeBonus(mode, value);
      }
      STAND '=' PERCENT
      {
          const auto mode = static_cast<MoveModeEnum>(1);
          const double value = static_cast<double>(grammar::SemanticInt64($8) + 100) / 100.0;
          g_PCParamDB.SetRegenMoveModeBonus(mode, value);
      }
      LOW '=' PERCENT
      {
          const auto mode = static_cast<MoveModeEnum>(2);
          const double value = static_cast<double>(grammar::SemanticInt64($12) + 100) / 100.0;
          g_PCParamDB.SetRegenMoveModeBonus(mode, value);
      }
      HIGH '=' PERCENT
      {
          const auto mode = static_cast<MoveModeEnum>(3);
          const double value = static_cast<double>(grammar::SemanticInt64($16) + 100) / 100.0;
          g_PCParamDB.SetRegenMoveModeBonus(mode, value);
      }
      REGEN_MOVE_MODE_BONUS_END
    ;

org_hp_regen_weight
    : ORG_HP_REGEN_WEIGHT_BEGIN SIT '=' '{' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';'
      none_group_def ';' none_group_def '}'
      {
          const auto mode = static_cast<MoveModeEnum>((0) % 4);
          const double values[6] = {
              grammar::SemanticDouble($5),
              grammar::SemanticDouble($7),
              grammar::SemanticDouble($9),
              grammar::SemanticDouble($11),
              grammar::SemanticDouble($13),
              grammar::SemanticDouble($15),
          };
          g_PCParamDB.SetRegenMoveModeHPBonus(mode, values[0], values[1], values[2], values[3], values[4], values[5]);
      }
      STAND '=' '{' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';'
      none_group_def '}'
      {
          const auto mode = static_cast<MoveModeEnum>((1) % 4);
          const double values[6] = {
              grammar::SemanticDouble($21),
              grammar::SemanticDouble($23),
              grammar::SemanticDouble($25),
              grammar::SemanticDouble($27),
              grammar::SemanticDouble($29),
              grammar::SemanticDouble($31),
          };
          g_PCParamDB.SetRegenMoveModeHPBonus(mode, values[0], values[1], values[2], values[3], values[4], values[5]);
      }
      LOW '=' '{' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';'
      none_group_def '}'
      {
          const auto mode = static_cast<MoveModeEnum>((2) % 4);
          const double values[6] = {
              grammar::SemanticDouble($37),
              grammar::SemanticDouble($39),
              grammar::SemanticDouble($41),
              grammar::SemanticDouble($43),
              grammar::SemanticDouble($45),
              grammar::SemanticDouble($47),
          };
          g_PCParamDB.SetRegenMoveModeHPBonus(mode, values[0], values[1], values[2], values[3], values[4], values[5]);
      }
      HIGH '=' '{' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';'
      none_group_def '}'
      {
          const auto mode = static_cast<MoveModeEnum>((3) % 4);
          const double values[6] = {
              grammar::SemanticDouble($53),
              grammar::SemanticDouble($55),
              grammar::SemanticDouble($57),
              grammar::SemanticDouble($59),
              grammar::SemanticDouble($61),
              grammar::SemanticDouble($63),
          };
          g_PCParamDB.SetRegenMoveModeHPBonus(mode, values[0], values[1], values[2], values[3], values[4], values[5]);
      }
      ORG_HP_REGEN_WEIGHT_END
    ;

org_mp_regen_weight
    : ORG_MP_REGEN_WEIGHT_BEGIN SIT '=' '{' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';'
      none_group_def ';' none_group_def '}'
      {
          const auto mode = static_cast<MoveModeEnum>((4) % 4);
          const double values[6] = {
              grammar::SemanticDouble($5),
              grammar::SemanticDouble($7),
              grammar::SemanticDouble($9),
              grammar::SemanticDouble($11),
              grammar::SemanticDouble($13),
              grammar::SemanticDouble($15),
          };
          g_PCParamDB.SetRegenMoveModeMPBonus(mode, values[0], values[1], values[2], values[3], values[4], values[5]);
      }
      STAND '=' '{' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';'
      none_group_def '}'
      {
          const auto mode = static_cast<MoveModeEnum>((5) % 4);
          const double values[6] = {
              grammar::SemanticDouble($21),
              grammar::SemanticDouble($23),
              grammar::SemanticDouble($25),
              grammar::SemanticDouble($27),
              grammar::SemanticDouble($29),
              grammar::SemanticDouble($31),
          };
          g_PCParamDB.SetRegenMoveModeMPBonus(mode, values[0], values[1], values[2], values[3], values[4], values[5]);
      }
      LOW '=' '{' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';'
      none_group_def '}'
      {
          const auto mode = static_cast<MoveModeEnum>((6) % 4);
          const double values[6] = {
              grammar::SemanticDouble($37),
              grammar::SemanticDouble($39),
              grammar::SemanticDouble($41),
              grammar::SemanticDouble($43),
              grammar::SemanticDouble($45),
              grammar::SemanticDouble($47),
          };
          g_PCParamDB.SetRegenMoveModeMPBonus(mode, values[0], values[1], values[2], values[3], values[4], values[5]);
      }
      HIGH '=' '{' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';'
      none_group_def '}'
      {
          const auto mode = static_cast<MoveModeEnum>((7) % 4);
          const double values[6] = {
              grammar::SemanticDouble($53),
              grammar::SemanticDouble($55),
              grammar::SemanticDouble($57),
              grammar::SemanticDouble($59),
              grammar::SemanticDouble($61),
              grammar::SemanticDouble($63),
          };
          g_PCParamDB.SetRegenMoveModeMPBonus(mode, values[0], values[1], values[2], values[3], values[4], values[5]);
      }
      ORG_MP_REGEN_WEIGHT_END
    ;

org_cp_regen_weight
    : ORG_CP_REGEN_WEIGHT_BEGIN SIT '=' '{' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';'
      none_group_def ';' none_group_def '}'
      {
          const auto mode = static_cast<MoveModeEnum>((8) % 4);
          const double values[6] = {
              grammar::SemanticDouble($5),
              grammar::SemanticDouble($7),
              grammar::SemanticDouble($9),
              grammar::SemanticDouble($11),
              grammar::SemanticDouble($13),
              grammar::SemanticDouble($15),
          };
          g_PCParamDB.SetRegenMoveModeCPBonus(mode, values[0], values[1], values[2], values[3], values[4], values[5]);
      }
      STAND '=' '{' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';'
      none_group_def '}'
      {
          const auto mode = static_cast<MoveModeEnum>((9) % 4);
          const double values[6] = {
              grammar::SemanticDouble($21),
              grammar::SemanticDouble($23),
              grammar::SemanticDouble($25),
              grammar::SemanticDouble($27),
              grammar::SemanticDouble($29),
              grammar::SemanticDouble($31),
          };
          g_PCParamDB.SetRegenMoveModeCPBonus(mode, values[0], values[1], values[2], values[3], values[4], values[5]);
      }
      LOW '=' '{' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';'
      none_group_def '}'
      {
          const auto mode = static_cast<MoveModeEnum>((10) % 4);
          const double values[6] = {
              grammar::SemanticDouble($37),
              grammar::SemanticDouble($39),
              grammar::SemanticDouble($41),
              grammar::SemanticDouble($43),
              grammar::SemanticDouble($45),
              grammar::SemanticDouble($47),
          };
          g_PCParamDB.SetRegenMoveModeCPBonus(mode, values[0], values[1], values[2], values[3], values[4], values[5]);
      }
      HIGH '=' '{' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';' none_group_def ';'
      none_group_def '}'
      {
          const auto mode = static_cast<MoveModeEnum>((11) % 4);
          const double values[6] = {
              grammar::SemanticDouble($53),
              grammar::SemanticDouble($55),
              grammar::SemanticDouble($57),
              grammar::SemanticDouble($59),
              grammar::SemanticDouble($61),
              grammar::SemanticDouble($63),
          };
          g_PCParamDB.SetRegenMoveModeCPBonus(mode, values[0], values[1], values[2], values[3], values[4], values[5]);
      }
      ORG_CP_REGEN_WEIGHT_END
    ;

noise_move_mode_bonus
    : NOISE_MOVE_MODE_BONUS_BEGIN SIT '=' PERCENT { } STAND '=' PERCENT { } LOW '=' PERCENT { } HIGH '=' PERCENT { }
      NOISE_MOVE_MODE_BONUS_END
    ;

hit_cond_bonus
    : HIT_COND_BONUS_BEGIN AHEAD '=' PERCENT { } SIDE '=' PERCENT { } BACK '=' PERCENT { } HIGH '=' PERCENT { } LOW '='
      PERCENT { } DARK '=' PERCENT { } RAIN '=' PERCENT { } HIT_COND_BONUS_END
    ;

pc_karma_increase_constant
    : PC_KARMA_INCREASE_CONSTANT_BEGIN PENALTY_START_KARMA '=' none_group_def { } PENALTY_DURATION_DEFAULT '=' none_group_def
      { } PENALTY_DURATION_INCREASE '=' none_group_def { } DOWN_TIME_MULTIPLE '=' none_group_def { }
      CRIMINAL_DURATION_MULTIPLE '=' none_group_def { } PC_KARMA_INCREASE_CONSTANT_END
    ;

pc_stat_max
    : PC_STAT_MAX_BEGIN PC_STAT_MAX_STR '=' none_group_def { } PC_STAT_MAX_DEX '=' none_group_def { } PC_STAT_MAX_CON '='
      none_group_def { } PC_STAT_MAX_INT '=' none_group_def { } PC_STAT_MAX_WIT '=' none_group_def { } PC_STAT_MAX_MEN '='
      none_group_def { } PC_STAT_MAX_END
    ;

none_group_def
    : NONE_GROUP
      {
          const double converted = static_cast<double>(grammar::SemanticInt64($1));
          $$ = DoubleSemantic(converted);
      }
    | FLOAT_NUMBER
      { $$ = $1; }
    ;

%%
