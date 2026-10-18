// CParserForTransform grammar (original L2Server/parser_transform.y).
// Bison generates ParserTransform.cpp and ParserTransformTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserTransform.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ObjectDB.h"
#include "L2Server/src/Skill.h"

#include "Shared/src/Log.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserTransformTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForTransform::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserTransformTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

const char16_t* SemanticText(const grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

bool EqualsTargetText(const char16_t* const value, const std::u16string_view expected) {
    if (value == nullptr) {
        return false;
    }

    std::size_t index = 0;
    while (index < expected.size() && value[index] != u'\0') {
        if (value[index] != expected[index]) {
            return false;
        }
        ++index;
    }
    return index == expected.size() && value[index] == u'\0';
}

} // namespace

CParserForTransform::CParserForTransform() : m_lexer(), doubleList(), intList(), skillList() {}

CParserForTransform::~CParserForTransform() = default;

int CParserForTransform::Create(char16_t* const fileBuffer, const int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForTransform::ReportError(
    const ErrorType error, const std::uint64_t currentSize, const std::uint64_t maximumSize, const char16_t* const text
) {
    // const std::wstring hostText = target_utf16::ToHostCodeUnits(text)
    // const wchar_t* const source = L"parser_transform.y"

    switch (error) {
        case ERROR_COLLISION:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] Invalid parameters(COLLISION_BOX) while loading "
            //     L"TransformData. (id[%d] nowSize[%d], maxSize[%d])",
            //     source,
            //     id,
            //     static_cast<int>(currentSize),
            //     static_cast<int>(maximumSize))
            // L2_LOG_ERROR(Log,
            //     L"[%s] Invalid parameters(COLLISION_BOX) while loading "
            //     L"TransformData. (id[%d] nowSize[%d], maxSize[%d])",
            //     source,
            //     id,
            //     static_cast<int>(currentSize),
            //     static_cast<int>(maximumSize))
            L2_LOG_ERROR(
                Log, "Transform %d has invalid COLLISION_BOX size %llu (reference count %llu) at script line %lld.", id,
                static_cast<unsigned long long>(currentSize), static_cast<unsigned long long>(maximumSize),
                static_cast<long long>(m_lexer.line) + 1
            );
            return;
        case ERROR_SPEED:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] Invalid parameters(MOVING_SPEED) while loading "
            //     L"TransformData. (id[%d])",
            //     source,
            //     id)
            // L2_LOG_ERROR(Log,
            //     L"[%s] Invalid parameters(MOVING_SPEED) while loading "
            //     L"TransformData. (id[%d])",
            //     source,
            //     id)
            L2_LOG_ERROR(
                Log,
                "Transform %d MOVING_SPEED has %zu values at script line %lld; expected 8, the speed setter is still "
                "called.",
                id, static_cast<std::size_t>(doubleList.size()), static_cast<long long>(m_lexer.line) + 1
            );
            return;
        case ERROR_NON_COMBAT:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] Transform for Non-Combat has combat data while loading "
            //     L"TransformData. (id[%d])",
            //     source,
            //     id)
            // L2_LOG_ERROR(Log,
            //     L"[%s] Transform for Non-Combat has combat data while loading "
            //     L"TransformData. (id[%d])",
            //     source,
            //     id)
            L2_LOG_ERROR(
                Log, "Non-combat transform %d contains combat data at script line %lld; parsing continues.", id,
                static_cast<long long>(m_lexer.line) + 1
            );
            return;
        case ERROR_WEAPON:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] Invalid WEAPON while loading TransformData. "
            //     L"(id[%d] weapon[%s])",
            //     source,
            //     id,
            //     hostText.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[%s] Invalid WEAPON while loading TransformData. "
            //     L"(id[%d] weapon[%s])",
            //     source,
            //     id,
            //     hostText.c_str())
            L2_LOG_ERROR(
                Log, "Transform %d has invalid weapon '%s' at script line %lld.", id,
                Utf16ToUtf8(std::u16string_view(text != nullptr ? text : u"<null>")).c_str(),
                static_cast<long long>(m_lexer.line) + 1
            );
            return;
        case ERROR_COMBAT:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] Transform for Combat has no combat data while loading "
            //     L"TransformData. (id[%d])",
            //     source,
            //     id)
            // L2_LOG_ERROR(Log,
            //     L"[%s] Transform for Combat has no combat data while loading "
            //     L"TransformData. (id[%d])",
            //     source,
            //     id)
            L2_LOG_ERROR(
                Log, "Combat transform %d has no combat data at script line %lld; parsing continues.", id,
                static_cast<long long>(m_lexer.line) + 1
            );
            return;
        case ERROR_SKILL:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] Invalid skill while loading TransformData. "
            //     L"(id[%d] skill[%s])",
            //     source,
            //     id,
            //     hostText.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[%s] Invalid skill while loading TransformData. "
            //     L"(id[%d] skill[%s])",
            //     source,
            //     id,
            //     hostText.c_str())
            L2_LOG_ERROR(
                Log, "Transform %d references undefined skill '%s' at script line %lld; the skill is not added.", id,
                Utf16ToUtf8(std::u16string_view(text != nullptr ? text : u"<null>")).c_str(),
                static_cast<long long>(m_lexer.line) + 1
            );
            return;
        case ERROR_CANCEL_SKILL:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] Invalid cancel skill while loading TransformData. "
            //     L"(skill[%s])",
            //     source,
            //     hostText.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[%s] Invalid cancel skill while loading TransformData. "
            //     L"(skill[%s])",
            //     source,
            //     hostText.c_str())
            L2_LOG_ERROR(
                Log, "Undefined transform cancellation skill '%s' at script line %lld.",
                Utf16ToUtf8(std::u16string_view(text != nullptr ? text : u"<null>")).c_str(),
                static_cast<long long>(m_lexer.line) + 1
            );
            return;
        default:
            break;
    }

    const wchar_t* parameterName = L"";
    switch (error) {
        case ERROR_STAT:
            parameterName = L"STAT";
            break;
        case ERROR_HP_REGEN:
            parameterName = L"HP_REGEN";
            break;
        case ERROR_MP_REGEN:
            parameterName = L"MP_REGEN";
            break;
        case ERROR_CP_REGEN:
            parameterName = L"CP_REGEN";
            break;
        case ERROR_DEFEND:
            parameterName = L"DEFEND";
            break;
        case ERROR_MAGIC_DEFEND:
            parameterName = L"MAGIC_DEFEND";
            break;
        case ERROR_LEVEL_BONUS:
            parameterName = L"LEVEL_BONUS";
            break;
        case ERROR_STR_BONUS:
            parameterName = L"STR_BONUS";
            break;
        case ERROR_INT_BONUS:
            parameterName = L"INT_BONUS";
            break;
        case ERROR_CON_BONUS:
            parameterName = L"CON_BONUS";
            break;
        case ERROR_DEX_BONUS:
            parameterName = L"DEX_BONUS";
            break;
        case ERROR_WIT_BONUS:
            parameterName = L"WIT_BONUS";
            break;
        case ERROR_MEN_BONUS:
            parameterName = L"MEN_BONUS";
            break;
        case ERROR_HP_TABLE:
            parameterName = L"HP_TABLE";
            break;
        case ERROR_MP_TABLE:
            parameterName = L"MP_TABLE";
            break;
        case ERROR_CP_TABLE:
            parameterName = L"CP_TABLE";
            break;
        case ERROR_LUC_BONUS:
            parameterName = L"LUC_BONUS";
            break;
        case ERROR_CHA_BONUS:
            parameterName = L"CHA_BONUS";
            break;
        default:
            return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s] Invalid parameters(%s) while loading TransformData. "
    //     L"(id[%d] size[%d] max[%d])",
    //     source,
    //     parameterName,
    //     id,
    //     static_cast<int>(currentSize),
    //     static_cast<int>(maximumSize))
    // L2_LOG_ERROR(Log,
    //     L"[%s] Invalid parameters(%s) while loading TransformData. "
    //     L"(id[%d] size[%d] max[%d])",
    //     source,
    //     parameterName,
    //     id,
    //     static_cast<int>(currentSize),
    //     static_cast<int>(maximumSize))
    L2_LOG_ERROR(
        Log, "Transform %d %s has %llu values at script line %lld; %s %llu, the stat setter is still called.", id,
        WideToUtf8(parameterName).c_str(), static_cast<unsigned long long>(currentSize),
        static_cast<long long>(m_lexer.line) + 1,
        error == ERROR_STAT || error == ERROR_DEFEND || error == ERROR_MAGIC_DEFEND ? "expected" : "maximum",
        static_cast<unsigned long long>(maximumSize)
    );
}
}

%token TRANSFORM_BEGIN
%token TRANSFORM_END
%token ID
%token TYPE
%token NON_COMBAT
%token COMBAT
%token CURSED
%token MODE_CHANGE
%token RIDING_MODE
%token FLYING
%token PURE_STAT
%token CAN_SWIM
%token SPAWN_HEIGHT
%token NORMAL_ATTACKABLE
%token CAN_MOVE
%token FEMALE_BEGIN
%token FEMALE_END
%token MALE_BEGIN
%token MALE_END
%token COMMON_BEGIN
%token COMMON_END
%token COLLISION_BOX
%token MOVING_SPEED
%token SKILL
%token ADDITIONAL_SKILL
%token ITEM_CHECK
%token ACTION
%token COMBAT_BEGIN
%token COMBAT_END
%token BASIC_STAT
%token BASE_DEFEND
%token BASE_MAGIC_DEFEND
%token BASE_ATTACK_TYPE
%token NONE
%token SWORD
%token BLUNT
%token DAGGER
%token POLE
%token FIST
%token BOW
%token ETC
%token DUAL
%token DUALFIST
%token FISHINGROD
%token FLAG
%token OWNTHING
%token BASE_ATTACK_RANGE
%token BASE_RANDOM_DAMAGE
%token BASE_ATTACK_SPEED
%token BASE_CRITICAL_PROB
%token BASE_PHYSICAL_ATTACK
%token BASE_MAGICAL_ATTACK
%token ORG_HP_REGEN
%token ORG_MP_REGEN
%token ORG_CP_REGEN
%token LEVEL_BONUS
%token STR_BONUS
%token INT_BONUS
%token CON_BONUS
%token DEX_BONUS
%token WIT_BONUS
%token MEN_BONUS
%token LUC_BONUS
%token CHA_BONUS
%token HP_TABLE
%token MP_TABLE
%token CP_TABLE
%token NAME
%token IDENTIFIER
%token INTEGER
%token FLOAT_NUMBER
%start transform_list

%%

transform_list
    : transform_list transform
    | transform
    ;

transform
    : TRANSFORM_BEGIN id_def type_def can_swim_def_list n7 TRANSFORM_END
    ;

id_def
    : ID '=' INTEGER
      { id = grammar::SemanticInt32($3); }
    ;

type_def
    : TYPE '=' NON_COMBAT
      {
          const auto allocatePair = [this](const TransformType::Enum type) {
              switch (type) {
                  case TransformType::NonCombat:
                      pTransformFemale = new CTransformForNonCombat(id);
                      pTransformMale = new CTransformForNonCombat(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Combat:
                      pTransformFemale = new CTransformForCombat(id);
                      pTransformMale = new CTransformForCombat(id);
                      forNonCombat = false;
                      return;
                  case TransformType::Cursed:
                      pTransformFemale = new CTransformForCursed(id);
                      pTransformMale = new CTransformForCursed(id);
                      forNonCombat = false;
                      return;
                  case TransformType::ModeChange:
                      pTransformFemale = new CTransformForModeChange(id);
                      pTransformMale = new CTransformForModeChange(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Riding:
                      pTransformFemale = new CTransformForRiding(id);
                      pTransformMale = new CTransformForRiding(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Flying:
                      pTransformFemale = new CTransformForFlying(id);
                      pTransformMale = new CTransformForFlying(id);
                      forNonCombat = false;
                      return;
                  case TransformType::PureStat:
                      pTransformFemale = new CTransformForPureStat(id);
                      pTransformMale = new CTransformForPureStat(id);
                      forNonCombat = false;
                      return;
                  default:
                      return;
              }
          };
          allocatePair(TransformType::NonCombat);
      }
    | TYPE '=' COMBAT
      {
          const auto allocatePair = [this](const TransformType::Enum type) {
              switch (type) {
                  case TransformType::NonCombat:
                      pTransformFemale = new CTransformForNonCombat(id);
                      pTransformMale = new CTransformForNonCombat(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Combat:
                      pTransformFemale = new CTransformForCombat(id);
                      pTransformMale = new CTransformForCombat(id);
                      forNonCombat = false;
                      return;
                  case TransformType::Cursed:
                      pTransformFemale = new CTransformForCursed(id);
                      pTransformMale = new CTransformForCursed(id);
                      forNonCombat = false;
                      return;
                  case TransformType::ModeChange:
                      pTransformFemale = new CTransformForModeChange(id);
                      pTransformMale = new CTransformForModeChange(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Riding:
                      pTransformFemale = new CTransformForRiding(id);
                      pTransformMale = new CTransformForRiding(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Flying:
                      pTransformFemale = new CTransformForFlying(id);
                      pTransformMale = new CTransformForFlying(id);
                      forNonCombat = false;
                      return;
                  case TransformType::PureStat:
                      pTransformFemale = new CTransformForPureStat(id);
                      pTransformMale = new CTransformForPureStat(id);
                      forNonCombat = false;
                      return;
                  default:
                      return;
              }
          };
          allocatePair(TransformType::Combat);
      }
    | TYPE '=' CURSED
      {
          const auto allocatePair = [this](const TransformType::Enum type) {
              switch (type) {
                  case TransformType::NonCombat:
                      pTransformFemale = new CTransformForNonCombat(id);
                      pTransformMale = new CTransformForNonCombat(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Combat:
                      pTransformFemale = new CTransformForCombat(id);
                      pTransformMale = new CTransformForCombat(id);
                      forNonCombat = false;
                      return;
                  case TransformType::Cursed:
                      pTransformFemale = new CTransformForCursed(id);
                      pTransformMale = new CTransformForCursed(id);
                      forNonCombat = false;
                      return;
                  case TransformType::ModeChange:
                      pTransformFemale = new CTransformForModeChange(id);
                      pTransformMale = new CTransformForModeChange(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Riding:
                      pTransformFemale = new CTransformForRiding(id);
                      pTransformMale = new CTransformForRiding(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Flying:
                      pTransformFemale = new CTransformForFlying(id);
                      pTransformMale = new CTransformForFlying(id);
                      forNonCombat = false;
                      return;
                  case TransformType::PureStat:
                      pTransformFemale = new CTransformForPureStat(id);
                      pTransformMale = new CTransformForPureStat(id);
                      forNonCombat = false;
                      return;
                  default:
                      return;
              }
          };
          allocatePair(TransformType::Cursed);
      }
    | TYPE '=' MODE_CHANGE
      {
          const auto allocatePair = [this](const TransformType::Enum type) {
              switch (type) {
                  case TransformType::NonCombat:
                      pTransformFemale = new CTransformForNonCombat(id);
                      pTransformMale = new CTransformForNonCombat(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Combat:
                      pTransformFemale = new CTransformForCombat(id);
                      pTransformMale = new CTransformForCombat(id);
                      forNonCombat = false;
                      return;
                  case TransformType::Cursed:
                      pTransformFemale = new CTransformForCursed(id);
                      pTransformMale = new CTransformForCursed(id);
                      forNonCombat = false;
                      return;
                  case TransformType::ModeChange:
                      pTransformFemale = new CTransformForModeChange(id);
                      pTransformMale = new CTransformForModeChange(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Riding:
                      pTransformFemale = new CTransformForRiding(id);
                      pTransformMale = new CTransformForRiding(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Flying:
                      pTransformFemale = new CTransformForFlying(id);
                      pTransformMale = new CTransformForFlying(id);
                      forNonCombat = false;
                      return;
                  case TransformType::PureStat:
                      pTransformFemale = new CTransformForPureStat(id);
                      pTransformMale = new CTransformForPureStat(id);
                      forNonCombat = false;
                      return;
                  default:
                      return;
              }
          };
          allocatePair(TransformType::ModeChange);
      }
    | TYPE '=' RIDING_MODE
      {
          const auto allocatePair = [this](const TransformType::Enum type) {
              switch (type) {
                  case TransformType::NonCombat:
                      pTransformFemale = new CTransformForNonCombat(id);
                      pTransformMale = new CTransformForNonCombat(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Combat:
                      pTransformFemale = new CTransformForCombat(id);
                      pTransformMale = new CTransformForCombat(id);
                      forNonCombat = false;
                      return;
                  case TransformType::Cursed:
                      pTransformFemale = new CTransformForCursed(id);
                      pTransformMale = new CTransformForCursed(id);
                      forNonCombat = false;
                      return;
                  case TransformType::ModeChange:
                      pTransformFemale = new CTransformForModeChange(id);
                      pTransformMale = new CTransformForModeChange(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Riding:
                      pTransformFemale = new CTransformForRiding(id);
                      pTransformMale = new CTransformForRiding(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Flying:
                      pTransformFemale = new CTransformForFlying(id);
                      pTransformMale = new CTransformForFlying(id);
                      forNonCombat = false;
                      return;
                  case TransformType::PureStat:
                      pTransformFemale = new CTransformForPureStat(id);
                      pTransformMale = new CTransformForPureStat(id);
                      forNonCombat = false;
                      return;
                  default:
                      return;
              }
          };
          allocatePair(TransformType::Riding);
      }
    | TYPE '=' FLYING
      {
          const auto allocatePair = [this](const TransformType::Enum type) {
              switch (type) {
                  case TransformType::NonCombat:
                      pTransformFemale = new CTransformForNonCombat(id);
                      pTransformMale = new CTransformForNonCombat(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Combat:
                      pTransformFemale = new CTransformForCombat(id);
                      pTransformMale = new CTransformForCombat(id);
                      forNonCombat = false;
                      return;
                  case TransformType::Cursed:
                      pTransformFemale = new CTransformForCursed(id);
                      pTransformMale = new CTransformForCursed(id);
                      forNonCombat = false;
                      return;
                  case TransformType::ModeChange:
                      pTransformFemale = new CTransformForModeChange(id);
                      pTransformMale = new CTransformForModeChange(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Riding:
                      pTransformFemale = new CTransformForRiding(id);
                      pTransformMale = new CTransformForRiding(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Flying:
                      pTransformFemale = new CTransformForFlying(id);
                      pTransformMale = new CTransformForFlying(id);
                      forNonCombat = false;
                      return;
                  case TransformType::PureStat:
                      pTransformFemale = new CTransformForPureStat(id);
                      pTransformMale = new CTransformForPureStat(id);
                      forNonCombat = false;
                      return;
                  default:
                      return;
              }
          };
          allocatePair(TransformType::Flying);
      }
    | TYPE '=' PURE_STAT
      {
          const auto allocatePair = [this](const TransformType::Enum type) {
              switch (type) {
                  case TransformType::NonCombat:
                      pTransformFemale = new CTransformForNonCombat(id);
                      pTransformMale = new CTransformForNonCombat(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Combat:
                      pTransformFemale = new CTransformForCombat(id);
                      pTransformMale = new CTransformForCombat(id);
                      forNonCombat = false;
                      return;
                  case TransformType::Cursed:
                      pTransformFemale = new CTransformForCursed(id);
                      pTransformMale = new CTransformForCursed(id);
                      forNonCombat = false;
                      return;
                  case TransformType::ModeChange:
                      pTransformFemale = new CTransformForModeChange(id);
                      pTransformMale = new CTransformForModeChange(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Riding:
                      pTransformFemale = new CTransformForRiding(id);
                      pTransformMale = new CTransformForRiding(id);
                      forNonCombat = true;
                      return;
                  case TransformType::Flying:
                      pTransformFemale = new CTransformForFlying(id);
                      pTransformMale = new CTransformForFlying(id);
                      forNonCombat = false;
                      return;
                  case TransformType::PureStat:
                      pTransformFemale = new CTransformForPureStat(id);
                      pTransformMale = new CTransformForPureStat(id);
                      forNonCombat = false;
                      return;
                  default:
                      return;
              }
          };
          allocatePair(TransformType::PureStat);
      }
    ;

can_swim_def_list
    : can_swim_def_list can_swim_def
    | %empty
    ;

can_swim_def
    : CAN_SWIM '=' INTEGER
      {
          const bool swimable = grammar::SemanticInt32($3) != 0;
          pTransformFemale->m_swimable = swimable;
          pTransformMale->m_swimable = swimable;
      }
    | SPAWN_HEIGHT '=' INTEGER
      {
          pTransformFemale->m_SpawnHeight = grammar::SemanticInt32($3);
          pTransformMale->m_SpawnHeight = grammar::SemanticInt32($3);
      }
    | NORMAL_ATTACKABLE '=' INTEGER
      {
          const bool attackable = grammar::SemanticInt32($3) == 1;
          pTransformFemale->m_bNormalAttakable = attackable;
          pTransformMale->m_bNormalAttakable = attackable;
      }
    | CAN_MOVE '=' INTEGER
      {
          const bool canMove = grammar::SemanticInt32($3) == 1;
          pTransformFemale->m_bCanMove = canMove;
          pTransformMale->m_bCanMove = canMove;
      }
    ;

n7
    : female male
    | male female
    ;

female
    : FEMALE_BEGIN n10 FEMALE_END
      {
          const auto publishTransform = [this](CTransform* const transform, const SexType sex) {
              transform->m_commonData = pCommonData;
              if (transform->IsCombat()) {
                  transform->SetCombatData(pCombatData);
              }
              g_TransformDB.Add(id, sex, transform);
          };
          publishTransform(pTransformFemale, ST_FEMALE);
      }
    ;

male
    : MALE_BEGIN n10 MALE_END
      {
          const auto publishTransform = [this](CTransform* const transform, const SexType sex) {
              transform->m_commonData = pCommonData;
              if (transform->IsCombat()) {
                  transform->SetCombatData(pCombatData);
              }
              g_TransformDB.Add(id, sex, transform);
          };
          publishTransform(pTransformMale, ST_MALE);
      }
    ;

n10
    : %empty
    | common combat
      {
          if (forNonCombat) {
              ReportError(ERROR_NON_COMBAT, 0, 0, nullptr);
          }
      }
    | common
      {
          if (!forNonCombat) {
              ReportError(ERROR_COMBAT, 0, 0, nullptr);
          }
      }
    ;

common
    : COMMON_BEGIN
      {
          intList.clear();
          doubleList.clear();
          pCommonData = new CTransformCommonData;
      }
      collision_box_def_list COMMON_END
    ;

collision_box_def_list
    : collision_box_def_list collision_box_def
    | collision_box_def
    ;

collision_box_def
    : COLLISION_BOX '=' '{' integer_def ';' integer_def '}'
      {
          pCommonData->collisionRadius = grammar::SemanticDouble($4);
          pCommonData->collisionHeight = grammar::SemanticDouble($6);
          doubleList.clear();
      }
    | MOVING_SPEED '=' '{' n42 '}'
      {
          if (doubleList.size() != 8) {
              ReportError(ERROR_SPEED, 0, 0, nullptr);
          }
          pCommonData->SetSpeed(doubleList);
          // Exact shipped action 19 clears skillList, not doubleList.
          skillList.clear();
      }
    | skill_def
    | additional_skill_def
    | item_check_def
    | action_def
    | base_attack_type_def
    | base_attack_range_def
      { doubleList.clear(); }
    ;

skill_def
    : SKILL '=' '{' n45 '}'
      {
          pCommonData->SetSkill(skillList);
          skillList.clear();
      }
    | SKILL '=' '{' '}'
    ;

additional_skill_def
    : ADDITIONAL_SKILL '=' '{' n17 '}'
    | ADDITIONAL_SKILL '=' '{' '}'
    ;

n17
    : n17 ';' additional_skill_def
    | additional_skill_def
    ;

additional_skill_def
    : '{' INTEGER ';' n45 '}'
      {
          pCommonData->SetAdditionalSkill(grammar::SemanticInt32($2), skillList);
          skillList.clear();
      }
    ;

item_check_def
    : ITEM_CHECK '=' '{' IDENTIFIER ';' n47 '}'
      {
          const char16_t* const type = SemanticText($4);
          bool allow = true;
          if (EqualsTargetText(type, u"ban")) {
              allow = false;
          } else if (!EqualsTargetText(type, u"allow")) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Invalid type. use 'ban' or 'allow' (id[%d] type[%s])",
              //     id,
              //     hostType.c_str())
              // L2_LOG_ERROR(Log,
              //     L"Invalid type. use 'ban' or 'allow' (id[%d] type[%s])",
              //     id,
              //     hostType.c_str())
              L2_LOG_ERROR(
                  Log,
                  "Transform %d has invalid item-check mode '%s' at script line %lld; expected 'ban' or 'allow', using "
                  "'allow'.",
                  id, Utf16ToUtf8(std::u16string_view(type != nullptr ? type : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1
              );
          }
          pCommonData->SetItemCheck(allow, intList);
          intList.clear();
      }
    | ITEM_CHECK '=' '{' '}'
    ;

action_def
    : ACTION '=' '{' n49 '}'
      {
          pCommonData->SetAction(intList);
          intList.clear();
      }
    | ACTION '=' '{' '}'
    ;

base_attack_type_def
    : BASE_ATTACK_TYPE '=' NONE
      { pCommonData->baseAttackType = AT_NONE; }
    | BASE_ATTACK_TYPE '=' SWORD
      { pCommonData->baseAttackType = AT_SWORD; }
    | BASE_ATTACK_TYPE '=' BLUNT
      { pCommonData->baseAttackType = AT_BLUNT; }
    | BASE_ATTACK_TYPE '=' DAGGER
      { pCommonData->baseAttackType = AT_DAGGER; }
    | BASE_ATTACK_TYPE '=' POLE
      { pCommonData->baseAttackType = AT_POLE; }
    | BASE_ATTACK_TYPE '=' FIST
      { pCommonData->baseAttackType = AT_FIST; }
    | BASE_ATTACK_TYPE '=' BOW
      { pCommonData->baseAttackType = AT_BOW; }
    | BASE_ATTACK_TYPE '=' ETC
      { pCommonData->baseAttackType = AT_ETC; }
    | BASE_ATTACK_TYPE '=' DUAL
      { pCommonData->baseAttackType = AT_DUAL; }
    | BASE_ATTACK_TYPE '=' DUALFIST
      { pCommonData->baseAttackType = AT_DUALFIST; }
    | BASE_ATTACK_TYPE '=' FISHINGROD
      { pCommonData->baseAttackType = AT_FISHINGROD; }
    | BASE_ATTACK_TYPE '=' FLAG
      { pCommonData->baseAttackType = AT_FLAG; }
    | BASE_ATTACK_TYPE '=' OWNTHING
      { pCommonData->baseAttackType = AT_OWNTHING; }
    ;

base_attack_range_def
    : BASE_ATTACK_RANGE '=' INTEGER BASE_RANDOM_DAMAGE '=' INTEGER BASE_ATTACK_SPEED '=' INTEGER BASE_CRITICAL_PROB '='
      INTEGER BASE_PHYSICAL_ATTACK '=' INTEGER BASE_MAGICAL_ATTACK '=' INTEGER
      {
          pCommonData->bareHandsData.attackRange = grammar::SemanticInt32($3);
          pCommonData->bareHandsData.randomDamage = grammar::SemanticInt32($6);
          pCommonData->bareHandsData.attackSpeed = grammar::SemanticInt32($9);
          pCommonData->bareHandsData.criticalProb = grammar::SemanticInt32($12);
          pCommonData->bareHandsData.pAttack = grammar::SemanticInt32($15);
          pCommonData->bareHandsData.mAttack = grammar::SemanticInt32($18);
      }
    ;

combat
    : COMBAT_BEGIN
      {
          pCombatData = new CTransformCombatData;
          pCombatData->Init();
          intList.clear();
      }
      basic_stat_def COMBAT_END
    ;

basic_stat_def
    : BASIC_STAT '=' '{' n49 '}'
      {
          if (intList.size() != 8) {
              ReportError(ERROR_STAT, intList.size(), 8, nullptr);
          }
          pCombatData->SetStat(intList);
          intList.clear();
      }
      BASE_DEFEND '=' '{' n42 '}'
      {
          if (doubleList.size() != 7) {
              ReportError(ERROR_DEFEND, doubleList.size(), 7, nullptr);
          }
          pCombatData->SetPDefend(doubleList);
          doubleList.clear();
      }
      BASE_MAGIC_DEFEND '=' '{' n42 '}'
      {
          if (doubleList.size() != 5) {
              ReportError(ERROR_MAGIC_DEFEND, doubleList.size(), 5, nullptr);
          }
          pCombatData->SetMDefend(doubleList);
          doubleList.clear();
      }
      ORG_HP_REGEN '=' '{' n42 '}'
      {
          const auto publishLevelStat =
              [this](const TransformLevelStat stat, const ErrorType error, const std::size_t maximum) {
                  if (doubleList.size() > maximum) {
                      ReportError(error, doubleList.size(), maximum, nullptr);
                  }
                  pCombatData->SetStatByLevel(stat, doubleList);
                  doubleList.clear();
              };
          publishLevelStat(TR_HP_REGEN, ERROR_HP_REGEN, 122);
      }
      ORG_MP_REGEN '=' '{' n42 '}'
      {
          const auto publishLevelStat =
              [this](const TransformLevelStat stat, const ErrorType error, const std::size_t maximum) {
                  if (doubleList.size() > maximum) {
                      ReportError(error, doubleList.size(), maximum, nullptr);
                  }
                  pCombatData->SetStatByLevel(stat, doubleList);
                  doubleList.clear();
              };
          publishLevelStat(TR_MP_REGEN, ERROR_MP_REGEN, 122);
      }
      ORG_CP_REGEN '=' '{' n42 '}'
      {
          const auto publishLevelStat =
              [this](const TransformLevelStat stat, const ErrorType error, const std::size_t maximum) {
                  if (doubleList.size() > maximum) {
                      ReportError(error, doubleList.size(), maximum, nullptr);
                  }
                  pCombatData->SetStatByLevel(stat, doubleList);
                  doubleList.clear();
              };
          publishLevelStat(TR_CP_REGEN, ERROR_CP_REGEN, 122);
      }
      LEVEL_BONUS '=' '{' n42 '}'
      {
          const auto publishLevelStat =
              [this](const TransformLevelStat stat, const ErrorType error, const std::size_t maximum) {
                  if (doubleList.size() > maximum) {
                      ReportError(error, doubleList.size(), maximum, nullptr);
                  }
                  pCombatData->SetStatByLevel(stat, doubleList);
                  doubleList.clear();
              };
          publishLevelStat(TR_LEVEL_BONUS, ERROR_LEVEL_BONUS, 122);
      }
      STR_BONUS '=' '{' n42 '}'
      {
          const auto publishLevelStat =
              [this](const TransformLevelStat stat, const ErrorType error, const std::size_t maximum) {
                  if (doubleList.size() > maximum) {
                      ReportError(error, doubleList.size(), maximum, nullptr);
                  }
                  pCombatData->SetStatByLevel(stat, doubleList);
                  doubleList.clear();
              };
          publishLevelStat(TR_STR_BONUS, ERROR_STR_BONUS, 244);
      }
      INT_BONUS '=' '{' n42 '}'
      {
          const auto publishLevelStat =
              [this](const TransformLevelStat stat, const ErrorType error, const std::size_t maximum) {
                  if (doubleList.size() > maximum) {
                      ReportError(error, doubleList.size(), maximum, nullptr);
                  }
                  pCombatData->SetStatByLevel(stat, doubleList);
                  doubleList.clear();
              };
          publishLevelStat(TR_INT_BONUS, ERROR_INT_BONUS, 244);
      }
      CON_BONUS '=' '{' n42 '}'
      {
          const auto publishLevelStat =
              [this](const TransformLevelStat stat, const ErrorType error, const std::size_t maximum) {
                  if (doubleList.size() > maximum) {
                      ReportError(error, doubleList.size(), maximum, nullptr);
                  }
                  pCombatData->SetStatByLevel(stat, doubleList);
                  doubleList.clear();
              };
          publishLevelStat(TR_CON_BONUS, ERROR_CON_BONUS, 244);
      }
      DEX_BONUS '=' '{' n42 '}'
      {
          const auto publishLevelStat =
              [this](const TransformLevelStat stat, const ErrorType error, const std::size_t maximum) {
                  if (doubleList.size() > maximum) {
                      ReportError(error, doubleList.size(), maximum, nullptr);
                  }
                  pCombatData->SetStatByLevel(stat, doubleList);
                  doubleList.clear();
              };
          publishLevelStat(TR_DEX_BONUS, ERROR_DEX_BONUS, 244);
      }
      WIT_BONUS '=' '{' n42 '}'
      {
          const auto publishLevelStat =
              [this](const TransformLevelStat stat, const ErrorType error, const std::size_t maximum) {
                  if (doubleList.size() > maximum) {
                      ReportError(error, doubleList.size(), maximum, nullptr);
                  }
                  pCombatData->SetStatByLevel(stat, doubleList);
                  doubleList.clear();
              };
          publishLevelStat(TR_WIT_BONUS, ERROR_WIT_BONUS, 244);
      }
      MEN_BONUS '=' '{' n42 '}'
      {
          const auto publishLevelStat =
              [this](const TransformLevelStat stat, const ErrorType error, const std::size_t maximum) {
                  if (doubleList.size() > maximum) {
                      ReportError(error, doubleList.size(), maximum, nullptr);
                  }
                  pCombatData->SetStatByLevel(stat, doubleList);
                  doubleList.clear();
              };
          publishLevelStat(TR_MEN_BONUS, ERROR_MEN_BONUS, 244);
      }
      LUC_BONUS '=' '{' n42 '}'
      {
          const auto publishLevelStat =
              [this](const TransformLevelStat stat, const ErrorType error, const std::size_t maximum) {
                  if (doubleList.size() > maximum) {
                      ReportError(error, doubleList.size(), maximum, nullptr);
                  }
                  pCombatData->SetStatByLevel(stat, doubleList);
                  doubleList.clear();
              };
          publishLevelStat(TR_LUC_BONUS, ERROR_LUC_BONUS, 244);
      }
      CHA_BONUS '=' '{' n42 '}'
      {
          const auto publishLevelStat =
              [this](const TransformLevelStat stat, const ErrorType error, const std::size_t maximum) {
                  if (doubleList.size() > maximum) {
                      ReportError(error, doubleList.size(), maximum, nullptr);
                  }
                  pCombatData->SetStatByLevel(stat, doubleList);
                  doubleList.clear();
              };
          publishLevelStat(TR_CHA_BONUS, ERROR_CHA_BONUS, 244);
      }
      HP_TABLE '=' '{' n42 '}'
      {
          const auto publishLevelStat =
              [this](const TransformLevelStat stat, const ErrorType error, const std::size_t maximum) {
                  if (doubleList.size() > maximum) {
                      ReportError(error, doubleList.size(), maximum, nullptr);
                  }
                  pCombatData->SetStatByLevel(stat, doubleList);
                  doubleList.clear();
              };
          publishLevelStat(TR_HP_TABLE, ERROR_HP_TABLE, 122);
      }
      MP_TABLE '=' '{' n42 '}'
      {
          const auto publishLevelStat =
              [this](const TransformLevelStat stat, const ErrorType error, const std::size_t maximum) {
                  if (doubleList.size() > maximum) {
                      ReportError(error, doubleList.size(), maximum, nullptr);
                  }
                  pCombatData->SetStatByLevel(stat, doubleList);
                  doubleList.clear();
              };
          publishLevelStat(TR_MP_TABLE, ERROR_MP_TABLE, 122);
      }
      CP_TABLE '=' '{' n42 '}'
      {
          const auto publishLevelStat =
              [this](const TransformLevelStat stat, const ErrorType error, const std::size_t maximum) {
                  if (doubleList.size() > maximum) {
                      ReportError(error, doubleList.size(), maximum, nullptr);
                  }
                  pCombatData->SetStatByLevel(stat, doubleList);
                  doubleList.clear();
              };
          publishLevelStat(TR_CP_TABLE, ERROR_CP_TABLE, 122);
      }
    ;

n42
    : n42 ';' n43
    | n43
    ;

n43
    : integer_def
      { doubleList.push_back(grammar::SemanticDouble($1)); }
    ;

integer_def
    : INTEGER
      {
          $$ = grammar::DoubleSemantic(static_cast<double>(grammar::SemanticInt64($1)));
      }
    | FLOAT_NUMBER
      { $$ = $1; }
    ;

n45
    : n45 ';' name_def
    | name_def
    ;

name_def
    : NAME
      {
          const char16_t* const name = SemanticText($1);
          const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
          const CSkillInfo* const skill = g_SkillDB.GetSkillInfoByName(hostName.c_str());
          if (skill != nullptr) {
              skillList.push_back(skill);
          } else {
              ReportError(ERROR_SKILL, 0, 0, name);
          }
      }
    ;

n47
    : n47 ';' name_def_2
    | name_def_2
    ;

name_def_2
    : NAME
      {
          const char16_t* const name = SemanticText($1);
          const int itemId = g_ObjectDB.GetClassIdFromName(name, false);
          if (itemId >= 0) {
              intList.push_back(itemId);
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s] Invalid item. (id[%d] item[%s])",
              //     L"parser_transform.y",
              //     id,
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"[%s] Invalid item. (id[%d] item[%s])",
              //     L"parser_transform.y",
              //     id,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, "Transform %d references undefined item '%s' at script line %lld; the item is not added.", id,
                  Utf16ToUtf8(std::u16string_view(name != nullptr ? name : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1
              );
          }
      }
    ;

n49
    : n49 ';' integer_def_2
    | integer_def_2
    ;

integer_def_2
    : INTEGER
      { intList.push_back(grammar::SemanticInt32($1)); }
    ;

%%
