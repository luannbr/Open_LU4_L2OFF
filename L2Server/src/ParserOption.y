// optiondata.txt grammar (original L2Server/parser_option.y).
// Bison generates ParserOption.cpp and ParserOptionTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserOption.h"
#include "L2Server/src/ParserOptionTables.inc"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/Inventory.h"
#include "L2Server/src/Item.h"
#include "L2Server/src/OptionDB.h"
#include "L2Server/src/Skill.h"
#include "L2Server/src/SkillFxFunc.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/StandardContainers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include "ParserOptionTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForOption::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserOptionTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

std::int8_t SemanticInt8(grammar::SemanticBits value) {
    const std::uint8_t bits = static_cast<std::uint8_t>(value);
    std::int8_t result = 0;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

const char16_t* SemanticWord(grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

bool TargetWordEquals(const char16_t* value, std::u16string_view expected) {
    return value != nullptr && std::u16string_view(value) == expected;
}

wchar_t* DuplicateSkillName(const char16_t* source) {
    if (source == nullptr) {
        return nullptr;
    }
    const std::size_t length = std::char_traits<char16_t>::length(source);
    auto* const result = static_cast<wchar_t*>(std::calloc(length + 1, sizeof(wchar_t)));
    if (result == nullptr) {
        return nullptr;
    }
    for (std::size_t index = 0; index < length; ++index) {
        result[index] = static_cast<wchar_t>(source[index]);
    }
    return result;
}

} // namespace

CParserForOption::CParserForOption() :
    m_lexer(), m_vMaterial(), optionNoSet(), optionNameSet(), m_sMagicType(), tempOptionName() {
    m_pSkillInfo = nullptr;
    m_bOnlyPrecompiledHeader = false;
}

CParserForOption::~CParserForOption() = default;

int CParserForOption::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token EQUAL
%token OPTION_BEGIN
%token OPTION_END
%token STR_INC
%token CON_INC
%token DEX_INC
%token INT_INC
%token MEN_INC
%token WIT_INC
%token ALL_STAT_INC
%token EFFECT
%token P_PHYSICAL_ATTACK
%token P_PHYSICAL_DEFENCE
%token P_HP_REGEN
%token P_MP_REGEN
%token P_SPEED
%token P_CREATE_ITEM
%token P_CREATE_COMMON_ITEM
%token P_ATTACK_SPEED
%token P_MAGIC_SPEED
%token T_10015
%token P_REMOVE_EQUIP_PENALTY
%token P_CRYSTALLIZE
%token P_AVOID
%token P_LUCK
%token P_MAGICAL_DEFENCE
%token P_PHYSICAL_ATTACK_BY_MATERIAL
%token P_PHYSICAL_DEFENCE_BY_MATERIAL
%token P_ATTACK_ATTRIBUTE
%token T_1001E
%token P_DEFENCE_ATTRIBUTE
%token P_ATTACK_TRAIT
%token P_DEFENCE_TRAIT
%token P_BLOCK_MOVE
%token P_BLOCK_ACT
%token P_BLOCK_CONTROLL
%token P_BLOCK_SPELL
%token P_BLOCK_GETDAMAGE
%token P_FEAR
%token P_MAX_HP
%token P_MAX_MP
%token P_MAX_CP
%token P_HIT
%token P_MAGICAL_ATTACK
%token P_ATTACK_RANGE
%token P_CRITICAL_RATE
%token P_CRITICAL_DAMAGE
%token P_REUSE_DELAY
%token P_REDUCE_CANCEL
%token P_ATTACK_SPEED_BY_WEAPON
%token P_MAGIC_SPEED_BY_WEAPON
%token P_SHIELD_DEFENCE_RATE
%token P_HP_REGEN_BY_MOVE_MODE
%token P_MP_REGEN_BY_MOVE_MODE
%token P_AVOID_BY_MOVE_MODE
%token P_SAFE_FALL_HEIGHT
%token P_BREATH
%token P_DAMAGE_SHIELD
%token P_PASSIVE
%token P_AVOID_AGRO
%token P_WEIGHT_LIMIT
%token P_TRADE
%token P_SET_COLLECTED
%token P_SET_CLOAK_SLOT
%token P_CUBIC_MASTERY
%token P_WEIGHT_PENALTY
%token P_MANA_CHARGE
%token P_TRANSFER_DAMAGE_SUMMON
%token P_HEAL_EFFECT
%token P_AREA_DAMAGE
%token P_HIT_AT_NIGHT
%token P_VAMPIRIC_ATTACK
%token P_VAMPIRIC_DEFENCE
%token P_CRITICAL_RATE_BY_HP1
%token P_CRITICAL_RATE_BY_HP2
%token P_PHYSICAL_ATTACK_BY_HP1
%token P_PHYSICAL_ATTACK_BY_HP2
%token P_PHYSICAL_DEFENCE_BY_HP1
%token P_PHYSICAL_DEFENCE_BY_HP2
%token P_AVOID_RATE_BY_HP1
%token P_AVOID_RATE_BY_HP2
%token P_ATTACK_SPEED_BY_HP1
%token P_ATTACK_SPEED_BY_HP2
%token P_FATAL_BLOW_RATE
%token P_CRITICAL_RATE_POSITION_BONUS
%token P_REDUCE_DROP_PENALTY
%token P_PHYSICAL_SHIELD_DEFENCE
%token P_PHYSICAL_ARMOR_HIT
%token P_PHYSICAL_POLARM_TARGET_SINGLE
%token P_MAGIC_CRITICAL_RATE
%token P_MAGIC_MP_COST
%token P_AFTER_MP_COST
%token P_PHYSICAL_SHIELD_DEFENCE_ANGLE_ALL
%token P_PVP_PHYSICAL_ATTACK_DMG_BONUS
%token P_PVP_PHYSICAL_SKILL_DMG_BONUS
%token P_PVP_MAGICAL_SKILL_DMG_BONUS
%token P_FISHING_MASTERY
%token P_ABNORMAL_RATE_LIMIT
%token P_ABNORMAL_REMOVE_BY_HIT
%token P_ABNORMAL_REMOVE_BY_DMG
%token P_BLOCK_SKILL_PHYSICAL
%token P_BLOCK_SKILL_SPECIAL
%token P_BLOCK_BUFF
%token P_BLOCK_DEBUFF
%token P_BLOCK_SKILL
%token P_SKILL_CRITICAL
%token P_SKILL_CRITICAL_PROBABILITY
%token P_CRITICAL_DAMAGE_POSITION
%token P_RESIST_DISPEL_BY_CATEGORY
%token P_RESIST_ABNORMAL_BY_CATEGORY
%token P_PRESERVE_ABNORMAL
%token P_HIT_NUMBER
%token P_CP_REGEN
%token P_CP_REGEN_BY_MOVE_MODE
%token P_BETRAY
%token P_CHANNEL_CLAN
%token P_CHANGE_FISHING_MASTERY
%token P_SKILL_POWER
%token P_LUCKY_ENCHANT_RATE
%token P_DAMAGE_SHIELD_RESIST
%token P_INSTANT_KILL_RESIST
%token P_STAT_UP
%token P_GET_ITEM_BY_EXP
%token P_MAGIC_HIT
%token P_PHYSICAL_SKILL_CRITICAL_DAMAGE
%token P_MAGIC_CRITICAL_DMG
%token P_MAGIC_AVOID
%token IS_VARIATION_ITEM_EQUIP_REUSEDELAY
%token ALL
%token PASSIVE_SKILL
%token ATTACK_SKILL
%token CRITICAL_SKILL
%token MAGIC_SKILL
%token ACTIVE_SKILL
%token NAME
%token IDENTIFIER
%token INTEGER
%token FLOAT_NUMBER
%token PER_ABS_DIFF
%token FRONT_SIDE_BACK
%token ATTR_VALUE
%token TRAIT_VALUE
%token SWORD_GROUP
%token ARMOR_VALUE
%token PK_MOB_BOSS
%start option_list

%%

option_list
    : option_list option
    | option
    ;

option
    : OPTION_BEGIN NAME INTEGER
      {
          const int optionNumber = grammar::SemanticInt32($3);
          const char16_t* const optionNameValue = SemanticWord($2);
          const std::u16string optionName(optionNameValue);
          if (optionNoSet.find(optionNumber) != optionNoSet.end()) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"duplicated option no [%d]",
              //     optionNumber)
              // L2_LOG_ERROR(Log,
              //     L"duplicated option no [%d]",
              //     optionNumber)
              L2_LOG_ERROR(
                  Log, u"Duplicate option number %d for '%ls' at optiondata.txt line %lld; parsing continues.",
                  optionNumber, optionNameValue, static_cast<long long>(m_lexer.line) + 1
              );
          }
          if (optionNameSet.find(optionName) != optionNameSet.end()) {
              // const std::wstring hostName = target_utf16::ToHostCodeUnits(optionNameValue)
              const std::wstring hostName = Utf16ToWide(optionNameValue);
              // Log.Add(
              //     LOG_ERROR,
              //     L"duplicated option name [%s]",
              //     hostName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"duplicated option name [%s]",
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"Duplicate option name '%ls' (number %d) at optiondata.txt line %lld; parsing continues.",
                  hostName.c_str(), optionNumber, static_cast<long long>(m_lexer.line) + 1
              );
          }
          optionNoSet.insert(optionNumber);
          optionNameSet.insert(optionName);
          tempOptionName.assign(optionNameValue);
          m_pTempOption = new Option(optionNumber);
          m_pSkillInfo = new CSkillInfo;
          m_pSkillInfo->m_nSkillId = optionNumber;
          m_pSkillInfo->m_nLevel = 0;
          m_pSkillInfo->m_pszName = DuplicateSkillName(optionNameValue);
      }
      n4 OPTION_END
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          g_OptionSkillDB.Add(m_pSkillInfo);
          m_pTempOption->skillData.effectSkills.push_back(m_pSkillInfo);
          m_pSkillInfo = nullptr;
          OptionDB::inst().RegisterNoToOption(m_pTempOption->optionNo, m_pTempOption);
          OptionDB::inst().RegisterNameToOption(tempOptionName, m_pTempOption);
      }
    ;

n4
    : n4 n5
    | n5
    ;

n5
    : str_inc_def
    | passive_skill_def
    | effect_def
    | is_variation_item_equip_reusedelay_def
    ;

str_inc_def
    : STR_INC EQUAL '{' INTEGER '}'
      { m_pTempOption->baseValue.m_N[1] = SemanticInt8($4); }
    | CON_INC EQUAL '{' INTEGER '}'
      { m_pTempOption->baseValue.m_N[2] = SemanticInt8($4); }
    | DEX_INC EQUAL '{' INTEGER '}'
      { m_pTempOption->baseValue.m_N[4] = SemanticInt8($4); }
    | INT_INC EQUAL '{' INTEGER '}'
      { m_pTempOption->baseValue.m_N[0] = SemanticInt8($4); }
    | MEN_INC EQUAL '{' INTEGER '}'
      { m_pTempOption->baseValue.m_N[3] = SemanticInt8($4); }
    | WIT_INC EQUAL '{' INTEGER '}'
      { m_pTempOption->baseValue.m_N[5] = SemanticInt8($4); }
    | ALL_STAT_INC EQUAL '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          m_pTempOption->baseValue.m_N[0] = SemanticInt8($4);
          m_pTempOption->baseValue.m_N[1] = SemanticInt8($6);
          m_pTempOption->baseValue.m_N[2] = SemanticInt8($8);
          m_pTempOption->baseValue.m_N[3] = SemanticInt8($10);
          m_pTempOption->baseValue.m_N[4] = SemanticInt8($12);
          m_pTempOption->baseValue.m_N[5] = SemanticInt8($14);
      }
    ;

effect_def
    : EFFECT EQUAL '{' n8 '}'
    ;

n8
    : %empty
    | '{' p_physical_attack_def '}'
    | n8 ';' '{' p_physical_attack_def '}'
    ;

p_physical_attack_def
    : P_PHYSICAL_ATTACK ';' '{' identifier_def '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          auto* const effect = new CSkillEffect_p_physical_attack(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_PHYSICAL_DEFENCE ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_physical_defence(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_HP_REGEN ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_hp_regen(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_MP_REGEN ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_mp_regen(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_SPEED ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_speed(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_CREATE_ITEM ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_create_item(grammar::SemanticInt32($3)));
      }
    | P_CREATE_COMMON_ITEM ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_create_common_item(grammar::SemanticInt32($3)));
      }
    | P_ATTACK_SPEED ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_attack_speed(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_MAGIC_SPEED ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_magic_speed(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | T_10015 ';' '{' identifier_def_2 '}' ';' '{' n15 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          std::set<int> magicTypes;
          for (const int magicType : m_sMagicType) {
              magicTypes.insert(magicType);
          }
          auto* const effect = new CSkillEffect_p_magic_speed_by_magic_type(
              grammar::SemanticDouble($11), static_cast<SkillFxChangeType>(grammar::SemanticInt32($13)),
              std::move(magicTypes)
          );
          effect->SetArmorTypeArray(m_ArmorTypeArray);
          publishPump(effect);
      }
    | P_REMOVE_EQUIP_PENALTY ';' IDENTIFIER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto crystalTypeAt = [this](const grammar::SemanticBits value) {
              const std::wstring name = target_utf16::ToHostCodeUnits(SemanticWord(value));
              CrystalType crystalType = CItem::GetCrysalType(name.c_str());
              if (crystalType == static_cast<CrystalType>(-1)) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"CrystalType [%s] isn't defined. line = [%d]",
                  //     name.c_str(),
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"CrystalType [%s] isn't defined. line = [%d]",
                  //     name.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"Option '%ls', optiondata.txt line %lld: unknown crystal type '%ls'; using crystal type 0 (none).",
                      tempOptionName, static_cast<long long>(m_lexer.line) + 1,
                      target_utf16::FromHostCodeUnits(name)
                  );
                  crystalType = static_cast<CrystalType>(0);
              }
              return crystalType;
          };
          auto* const effect = new CSkillEffect_p_remove_equip_penalty;
          effect->eCrystalType = crystalTypeAt($3);
          publishPump(effect);
      }
    | P_CRYSTALLIZE ';' IDENTIFIER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto crystalTypeAt = [this](const grammar::SemanticBits value) {
              const std::wstring name = target_utf16::ToHostCodeUnits(SemanticWord(value));
              CrystalType crystalType = CItem::GetCrysalType(name.c_str());
              if (crystalType == static_cast<CrystalType>(-1)) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"CrystalType [%s] isn't defined. line = [%d]",
                  //     name.c_str(),
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"CrystalType [%s] isn't defined. line = [%d]",
                  //     name.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"Option '%ls', optiondata.txt line %lld: unknown crystal type '%ls'; using crystal type 0 (none).",
                      tempOptionName, static_cast<long long>(m_lexer.line) + 1,
                      target_utf16::FromHostCodeUnits(name)
                  );
                  crystalType = static_cast<CrystalType>(0);
              }
              return crystalType;
          };
          auto* const effect = new CSkillEffect_p_crystallize;
          effect->eCrystalType = crystalTypeAt($3);
          publishPump(effect);
      }
    | P_AVOID ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_avoid(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_LUCK
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_luck);
      }
    | P_MAGICAL_DEFENCE ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_magical_defence(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_PHYSICAL_ATTACK_BY_MATERIAL ';' '{' n13 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          auto* const effect = new CSkillEffect_p_physical_attack_by_material(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->m_MaterialType = m_vMaterial.empty() ? MATT_MITHRIL : runtime::CheckedVectorAt(m_vMaterial, 0);
          publishPump(effect);
      }
    | P_PHYSICAL_DEFENCE_BY_MATERIAL ';' '{' n13 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          auto* const effect = new CSkillEffect_p_physical_defence_by_material(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          );
          effect->m_MaterialType = m_vMaterial.empty() ? MATT_MITHRIL : runtime::CheckedVectorAt(m_vMaterial, 0);
          publishPump(effect);
      }
    | P_ATTACK_ATTRIBUTE ';' ATTR_VALUE
    | P_ATTACK_ATTRIBUTE ';' ATTR_VALUE ';' integer_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_attack_attribute(
              static_cast<AttributeTypeEnum>(grammar::SemanticInt32($3)), grammar::SemanticDouble($5)
          ));
      }
    | T_1001E ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_attack_attribute_add(grammar::SemanticInt32($3)));
      }
    | P_DEFENCE_ATTRIBUTE ';' ATTR_VALUE ';' integer_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_defence_attribute(
              static_cast<AttributeTypeEnum>(grammar::SemanticInt32($3)), grammar::SemanticDouble($5)
          ));
      }
    | P_ATTACK_TRAIT ';' '{' identifier_def '}' ';' TRAIT_VALUE ';' integer_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          auto* const effect = new CSkillEffect_p_attack_trait(
              static_cast<TraitTypeEnum>(grammar::SemanticInt32($7)), grammar::SemanticDouble($9)
          );
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_DEFENCE_TRAIT ';' TRAIT_VALUE ';' integer_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const double value = grammar::SemanticDouble($5);
          if (value < -100.0 || value > 100.0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Trait Value [%d] should be between -100 ~ 100. line = [%d]",
              //     value,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Trait Value [%d] should be between -100 ~ 100. line = [%d]",
              //     value,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"Option '%ls', optiondata.txt line %lld: defence trait value %.17g is outside -100..100; the effect is "
                  u"still registered with this value.",
                  tempOptionName, static_cast<long long>(m_lexer.line) + 1, value
              );
          }
          publishPump(new CSkillEffect_p_defence_trait(static_cast<TraitTypeEnum>(grammar::SemanticInt32($3)), value));
      }
    | P_BLOCK_MOVE
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_block_move);
      }
    | P_BLOCK_ACT
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_block_act);
      }
    | P_BLOCK_CONTROLL
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_block_controll);
      }
    | P_BLOCK_SPELL
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_block_spell);
      }
    | P_BLOCK_GETDAMAGE ';' IDENTIFIER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          std::wstring blockType = target_utf16::ToHostCodeUnits(SemanticWord($3));
          publishPump(new CSkillEffect_p_block_getdamage(blockType.data()));
      }
    | P_FEAR
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_fear);
      }
    | P_MAX_HP ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_max_hp(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)), false
          ));
      }
    | P_MAX_MP ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_max_mp(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)), false
          ));
      }
    | P_MAX_CP ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_max_cp(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9)), false
          ));
      }
    | P_HIT ';' '{' identifier_def '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_hit(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_MAGICAL_ATTACK ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_magical_attack(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_ATTACK_RANGE ';' '{' identifier_def '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_attack_range(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_CRITICAL_RATE ';' '{' identifier_def '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_critical_rate(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_REUSE_DELAY ';' INTEGER ';' INTEGER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_reuse_delay(
              grammar::SemanticInt32($3), grammar::SemanticDouble($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_REDUCE_CANCEL ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_reduce_cancel(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_ATTACK_SPEED_BY_WEAPON ';' '{' identifier_def '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_attack_speed_by_weapon(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_MAGIC_SPEED_BY_WEAPON ';' '{' identifier_def '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_magic_speed_by_weapon(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_SHIELD_DEFENCE_RATE ';' FLOAT_NUMBER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_shield_defence_rate(grammar::SemanticDouble($3)));
      }
    | P_HP_REGEN_BY_MOVE_MODE ';' IDENTIFIER ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto moveModeAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"sit")) {
                  return MVMD_SIT;
              }
              if (TargetWordEquals(value, u"stand")) {
                  return MVMD_STAND;
              }
              if (TargetWordEquals(value, u"walk")) {
                  return MVMD_WALK;
              }
              if (TargetWordEquals(value, u"run")) {
                  return MVMD_RUN;
              }
              return MVMD_MAX;
          };
          auto* const effect = new CSkillEffect_p_hp_regen_by_move_mode(
              grammar::SemanticDouble($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          );
          effect->eMoveModeType = moveModeAt($3);
          publishPump(effect);
      }
    | P_MP_REGEN_BY_MOVE_MODE ';' IDENTIFIER ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto moveModeAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"sit")) {
                  return MVMD_SIT;
              }
              if (TargetWordEquals(value, u"stand")) {
                  return MVMD_STAND;
              }
              if (TargetWordEquals(value, u"walk")) {
                  return MVMD_WALK;
              }
              if (TargetWordEquals(value, u"run")) {
                  return MVMD_RUN;
              }
              return MVMD_MAX;
          };
          auto* const effect = new CSkillEffect_p_mp_regen_by_move_mode(
              grammar::SemanticDouble($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          );
          effect->eMoveModeType = moveModeAt($3);
          publishPump(effect);
      }
    | P_AVOID_BY_MOVE_MODE ';' IDENTIFIER ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto moveModeAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"sit")) {
                  return MVMD_SIT;
              }
              if (TargetWordEquals(value, u"stand")) {
                  return MVMD_STAND;
              }
              if (TargetWordEquals(value, u"walk")) {
                  return MVMD_WALK;
              }
              if (TargetWordEquals(value, u"run")) {
                  return MVMD_RUN;
              }
              return MVMD_MAX;
          };
          auto* const effect = new CSkillEffect_p_avoid_by_move_mode(
              grammar::SemanticDouble($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          );
          effect->eMoveModeType = moveModeAt($3);
          publishPump(effect);
      }
    | P_SAFE_FALL_HEIGHT ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_safe_fall_height(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_BREATH ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_breath(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_DAMAGE_SHIELD ';' FLOAT_NUMBER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_damage_shield(grammar::SemanticDouble($3)));
      }
    | P_PASSIVE
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_passive);
      }
    | P_AVOID_AGRO ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_avoid_agro(grammar::SemanticInt32($3)));
      }
    | P_WEIGHT_LIMIT ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_weight_limit(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_TRADE ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_trade(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              grammar::SemanticInt32($9), grammar::SemanticInt32($11), grammar::SemanticInt32($13)
          ));
      }
    | P_SET_COLLECTED
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_set_collected);
      }
    | P_SET_CLOAK_SLOT
    | P_CUBIC_MASTERY ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_cubic_mastery(grammar::SemanticInt32($3)));
      }
    | P_WEIGHT_PENALTY ';' integer_def
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_weight_penalty(static_cast<double>(grammar::SemanticInt64($3)), SFCT_DIFF));
      }
    | P_MANA_CHARGE ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_mana_charge(grammar::SemanticInt32($3)));
      }
    | P_TRANSFER_DAMAGE_SUMMON ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_transfer_damage_summon(grammar::SemanticInt32($3)));
      }
    | P_HEAL_EFFECT ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_heal_effect(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_AREA_DAMAGE ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_area_damage(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_HIT_AT_NIGHT ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_hit_at_night(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_VAMPIRIC_ATTACK ';' INTEGER ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_vampiric_attack(grammar::SemanticDouble($3), grammar::SemanticDouble($5)));
      }
    | P_VAMPIRIC_ATTACK ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_vampiric_attack(grammar::SemanticDouble($3), 100.0));
      }
    | P_VAMPIRIC_DEFENCE ';' FLOAT_NUMBER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_vampiric_defence(grammar::SemanticDouble($3)));
      }
    | P_CRITICAL_RATE_BY_HP1 ';' FLOAT_NUMBER ';' PER_ABS_DIFF
    | P_CRITICAL_RATE_BY_HP2 ';' FLOAT_NUMBER ';' PER_ABS_DIFF
    | P_PHYSICAL_ATTACK_BY_HP1 ';' FLOAT_NUMBER ';' PER_ABS_DIFF
    | P_PHYSICAL_ATTACK_BY_HP2 ';' FLOAT_NUMBER ';' PER_ABS_DIFF
    | P_CRITICAL_RATE_BY_HP1 ';' '{' identifier_def '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_critical_rate_by_hp1(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_CRITICAL_RATE_BY_HP2 ';' '{' identifier_def '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_critical_rate_by_hp2(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_PHYSICAL_ATTACK_BY_HP1 ';' '{' identifier_def '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_physical_attack_by_hp1(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_PHYSICAL_ATTACK_BY_HP2 ';' '{' identifier_def '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_physical_attack_by_hp2(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_PHYSICAL_DEFENCE_BY_HP1 ';' FLOAT_NUMBER ';' PER_ABS_DIFF
    | P_PHYSICAL_DEFENCE_BY_HP2 ';' FLOAT_NUMBER ';' PER_ABS_DIFF
    | P_PHYSICAL_DEFENCE_BY_HP1 ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_physical_defence_by_hp1(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_PHYSICAL_DEFENCE_BY_HP2 ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_physical_defence_by_hp2(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_AVOID_RATE_BY_HP1 ';' FLOAT_NUMBER ';' PER_ABS_DIFF
    | P_AVOID_RATE_BY_HP2 ';' FLOAT_NUMBER ';' PER_ABS_DIFF
    | P_AVOID_RATE_BY_HP1 ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_avoid_rate_by_hp1(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_AVOID_RATE_BY_HP2 ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_avoid_rate_by_hp2(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_ATTACK_SPEED_BY_HP1 ';' FLOAT_NUMBER ';' PER_ABS_DIFF
    | P_ATTACK_SPEED_BY_HP2 ';' FLOAT_NUMBER ';' PER_ABS_DIFF
    | P_ATTACK_SPEED_BY_HP1 ';' '{' identifier_def '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_attack_speed_by_hp1(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_ATTACK_SPEED_BY_HP2 ';' '{' identifier_def '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_attack_speed_by_hp2(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_FATAL_BLOW_RATE ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_fatal_blow_rate(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_CRITICAL_RATE_POSITION_BONUS ';' FRONT_SIDE_BACK ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_critical_rate_position_bonus(
              static_cast<SkillFxPositionType>(grammar::SemanticInt32($3)), grammar::SemanticDouble($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_REDUCE_DROP_PENALTY
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_reduce_drop_penalty(ENT_PLAYER, 0.0, 0.0));
      }
    | P_REDUCE_DROP_PENALTY ';' PK_MOB_BOSS ';' INTEGER ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_reduce_drop_penalty(
              static_cast<EnemyType>(grammar::SemanticInt32($3)), static_cast<double>(grammar::SemanticInt64($5)),
              static_cast<double>(grammar::SemanticInt64($7))
          ));
      }
    | P_PHYSICAL_SHIELD_DEFENCE_ANGLE_ALL
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_physical_shield_defence_angle_all);
      }
    | P_MAGIC_MP_COST ';' INTEGER ';' INTEGER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_magic_mp_cost(
              grammar::SemanticInt32($3), static_cast<double>(grammar::SemanticInt64($5)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_AFTER_MP_COST ';' INTEGER ';' INTEGER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_after_mp_cost(
              grammar::SemanticInt32($3), static_cast<double>(grammar::SemanticInt64($5)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_PVP_PHYSICAL_ATTACK_DMG_BONUS ';' integer_def ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_pvp_physical_attack_dmg_bonus(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_PVP_PHYSICAL_SKILL_DMG_BONUS ';' integer_def ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_pvp_physical_skill_dmg_bonus(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_PVP_MAGICAL_SKILL_DMG_BONUS ';' integer_def ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_pvp_magical_skill_dmg_bonus(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_PHYSICAL_ARMOR_HIT ';' '{' identifier_def_2 '}' ';' INTEGER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_physical_armor_hit(
              static_cast<double>(grammar::SemanticInt64($7)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_PHYSICAL_SHIELD_DEFENCE ';' INTEGER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_physical_shield_defence(
              static_cast<double>(grammar::SemanticInt64($3)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_PHYSICAL_POLARM_TARGET_SINGLE
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_physical_polarm_target_single);
      }
    | P_FISHING_MASTERY ';' FLOAT_NUMBER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_fishing_mastery(grammar::SemanticDouble($3)));
      }
    | P_ABNORMAL_RATE_LIMIT
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_abnormal_rate_limit);
      }
    | P_ABNORMAL_REMOVE_BY_HIT ';' INTEGER ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(
              new CSkillEffect_p_abnormal_remove_by_hit(grammar::SemanticInt32($3), grammar::SemanticInt32($5))
          );
      }
    | P_ABNORMAL_REMOVE_BY_DMG ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_abnormal_remove_by_dmg(grammar::SemanticInt32($3)));
      }
    | P_BLOCK_SKILL_PHYSICAL
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_block_skill_physical);
      }
    | P_BLOCK_SKILL_SPECIAL
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_block_skill_special);
      }
    | P_BLOCK_BUFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_block_buff);
      }
    | P_BLOCK_DEBUFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_block_debuff);
      }
    | P_BLOCK_DEBUFF ';' FLOAT_NUMBER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_block_debuff(grammar::SemanticDouble($3)));
      }
    | P_BLOCK_SKILL ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_block_skill(static_cast<SkillIsMagicType>(grammar::SemanticInt32($3))));
      }
    | P_SKILL_CRITICAL ';' '{' identifier_def '}' ';' IDENTIFIER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto parameterTypeAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"str")) {
                  return STE_STR;
              }
              if (TargetWordEquals(value, u"dex")) {
                  return STE_DEX;
              }
              if (TargetWordEquals(value, u"con")) {
                  return STE_CON;
              }
              if (TargetWordEquals(value, u"int")) {
                  return STE_INT;
              }
              if (TargetWordEquals(value, u"men")) {
                  return STE_MEN;
              }
              if (TargetWordEquals(value, u"wit")) {
                  return STE_WIT;
              }
              return STE_MAX;
          };
          auto* const effect = new CSkillEffect_p_skill_critical;
          effect->m_eParamType = parameterTypeAt($7);
          effect->SetAttackTypeArray(m_AttackTypeArray);
          publishPump(effect);
      }
    | P_SKILL_CRITICAL_PROBABILITY ';' INTEGER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_skill_critical_probability(
              grammar::SemanticInt32($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_CRITICAL_DAMAGE_POSITION ';' FRONT_SIDE_BACK ';' INTEGER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_critical_damage_position(
              static_cast<SkillFxPositionType>(grammar::SemanticInt32($3)), grammar::SemanticInt32($5),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          ));
      }
    | P_RESIST_DISPEL_BY_CATEGORY ';' IDENTIFIER ';' INTEGER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto debuffTypeAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"slot_buff")) {
                  return DB_GOOD;
              }
              if (TargetWordEquals(value, u"slot_debuff")) {
                  return DB_BAD;
              }
              return DB_MAX;
          };
          auto* const effect = new CSkillEffect_p_resist_dispel_by_category(
              grammar::SemanticInt32($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          );
          effect->eDeBuffType = debuffTypeAt($3);
          publishPump(effect);
      }
    | P_RESIST_ABNORMAL_BY_CATEGORY ';' IDENTIFIER ';' INTEGER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto debuffTypeAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"slot_buff")) {
                  return DB_GOOD;
              }
              if (TargetWordEquals(value, u"slot_debuff")) {
                  return DB_BAD;
              }
              return DB_MAX;
          };
          auto* const effect = new CSkillEffect_p_resist_abnormal_by_category(
              grammar::SemanticInt32($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          );
          effect->eDeBuffType = debuffTypeAt($3);
          publishPump(effect);
      }
    | P_PRESERVE_ABNORMAL
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_preserve_abnormal(0));
      }
    | P_HIT_NUMBER ';' '{' identifier_def '}' ';' INTEGER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_hit_number(
              grammar::SemanticInt32($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_BETRAY
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_betray);
      }
    | P_CP_REGEN ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_cp_regen(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_CP_REGEN_BY_MOVE_MODE ';' IDENTIFIER ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto moveModeAt = [](const grammar::SemanticBits slot) {
              const char16_t* const value = SemanticWord(slot);
              if (TargetWordEquals(value, u"sit")) {
                  return MVMD_SIT;
              }
              if (TargetWordEquals(value, u"stand")) {
                  return MVMD_STAND;
              }
              if (TargetWordEquals(value, u"walk")) {
                  return MVMD_WALK;
              }
              if (TargetWordEquals(value, u"run")) {
                  return MVMD_RUN;
              }
              return MVMD_MAX;
          };
          auto* const effect = new CSkillEffect_p_cp_regen_by_move_mode(
              grammar::SemanticDouble($5), static_cast<SkillFxChangeType>(grammar::SemanticInt32($7))
          );
          effect->eMoveModeType = moveModeAt($3);
          publishPump(effect);
      }
    | P_CHANNEL_CLAN
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_channel_clan);
      }
    | P_CHANGE_FISHING_MASTERY ';' INTEGER ';' FLOAT_NUMBER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(
              new CSkillEffect_p_change_fishing_mastery(grammar::SemanticInt32($3), grammar::SemanticDouble($5))
          );
      }
    | P_SKILL_POWER ';' INTEGER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_skill_power(
              static_cast<double>(grammar::SemanticInt64($3)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_LUCKY_ENCHANT_RATE ';' INTEGER ';' INTEGER ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_lucky_enchant_rate(
              grammar::SemanticInt32($3), grammar::SemanticInt32($5), grammar::SemanticInt32($7),
              static_cast<SkillFxChangeType>(6)
          ));
      }
    | P_STAT_UP ';' INTEGER ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_stat_up(grammar::SemanticInt32($3), grammar::SemanticInt32($5)));
      }
    | ALL_STAT_INC EQUAL '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          m_pTempOption->baseValue.m_N[0] = SemanticInt8($4);
          m_pTempOption->baseValue.m_N[1] = SemanticInt8($6);
          m_pTempOption->baseValue.m_N[2] = SemanticInt8($8);
          m_pTempOption->baseValue.m_N[3] = SemanticInt8($10);
          m_pTempOption->baseValue.m_N[4] = SemanticInt8($12);
          m_pTempOption->baseValue.m_N[5] = SemanticInt8($14);
      }
    | P_DAMAGE_SHIELD_RESIST ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_damage_shield_resist(grammar::SemanticInt32($3)));
      }
    | P_INSTANT_KILL_RESIST ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_instant_kill_resist(static_cast<double>(grammar::SemanticInt32($3))));
      }
    | P_GET_ITEM_BY_EXP ';' INTEGER ';' INTEGER ';' INTEGER
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_get_item_by_exp(
              grammar::SemanticInt64($3), grammar::SemanticInt32($5), grammar::SemanticInt64($7)
          ));
      }
    | P_PHYSICAL_SKILL_CRITICAL_DAMAGE ';' integer_def ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_physical_skill_critical_damage(
              grammar::SemanticDouble($3), static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_MAGIC_CRITICAL_RATE ';' '{' identifier_def '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_magic_critical_rate(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_MAGIC_AVOID ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          publishPump(new CSkillEffect_p_magic_avoid(
              runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($3)),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($5))
          ));
      }
    | P_MAGIC_HIT ';' '{' identifier_def_2 '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishArmorPump = [this, &publishPump](CSkillEffectByArmorType* effect) {
              effect->SetArmorTypeArray(m_ArmorTypeArray);
              publishPump(effect);
          };
          publishArmorPump(new CSkillEffect_p_magic_hit(
              static_cast<double>(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($7))),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_CRITICAL_DAMAGE ';' '{' identifier_def '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_critical_damage(
              grammar::SemanticDouble($7), static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    | P_MAGIC_CRITICAL_DMG ';' '{' identifier_def '}' ';' FLOAT_NUMBER ';' PER_ABS_DIFF
      {
          const auto publishPump = [this](CSkillEffect* effect) {
              m_pSkillInfo->PushPumpEffect(effect, CSkillInfo::EAT_SUCCESS);
          };
          const auto publishWeaponPump = [this, &publishPump](CSkillEffectByWeaponType* effect) {
              effect->SetAttackTypeArray(m_AttackTypeArray);
              publishPump(effect);
          };
          publishWeaponPump(new CSkillEffect_p_magic_critical_dmg(
              static_cast<double>(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($7))),
              static_cast<SkillFxChangeType>(grammar::SemanticInt32($9))
          ));
      }
    ;

integer_def
    : INTEGER
      {
          $$ = grammar::DoubleSemantic(static_cast<double>(grammar::SemanticInt64($1)));
      }
    | FLOAT_NUMBER
      { $$ = $1; }
    ;

identifier_def
    : IDENTIFIER
    | ALL
      { std::fill_n(std::begin(m_AttackTypeArray), 25, true); }
    | SWORD_GROUP
      { std::fill_n(std::begin(m_AttackTypeArray), 25, false); }
    | identifier_def ';' SWORD_GROUP
      { m_AttackTypeArray[grammar::SemanticInt32($3)] = true; }
    ;

identifier_def_2
    : IDENTIFIER
      {
          if (TargetWordEquals(SemanticWord($1), u"none")) {
              std::fill_n(std::begin(m_ArmorTypeArray), 5, false);
          }
      }
    | ALL
      { std::fill_n(std::begin(m_ArmorTypeArray), 5, true); }
    | ARMOR_VALUE
      { std::fill_n(std::begin(m_ArmorTypeArray), 5, false); }
    | identifier_def_2 ';' ARMOR_VALUE
      { m_ArmorTypeArray[grammar::SemanticInt32($3)] = true; }
    ;

n13
    : n13 ';' IDENTIFIER
      {
          const char16_t* const materialName = SemanticWord($3);
          if (TargetWordEquals(materialName, u"mithril")) {
              m_vMaterial.push_back(MATT_MITHRIL);
              break;
          }
          // const std::wstring hostName = target_utf16::ToHostCodeUnits(materialName)
          const std::wstring hostName = Utf16ToWide(materialName);
          // Log.Add(
          //     LOG_ERROR,
          //     L"%s[%d] : Undefined material(%s)",
          //     L"..\\Script\\optiondata.txt",
          //     m_lexer.line + 1,
          //     hostName.c_str())
          // L2_LOG_ERROR(Log,
          //     L"%s[%d] : Undefined material(%s)",
          //     L"..\\Script\\optiondata.txt",
          //     m_lexer.line + 1,
          //     hostName.c_str())
          L2_LOG_ERROR(
              Log, u"Option '%ls', optiondata.txt line %lld: unsupported material '%ls'; the material was not added.",
              tempOptionName, static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
          );
      }
    | IDENTIFIER
      {
          m_vMaterial.clear();
          const char16_t* const materialName = SemanticWord($1);
          if (TargetWordEquals(materialName, u"mithril")) {
              m_vMaterial.push_back(MATT_MITHRIL);
              break;
          }
          // const std::wstring hostName = target_utf16::ToHostCodeUnits(materialName)
          const std::wstring hostName = Utf16ToWide(materialName);
          // Log.Add(
          //     LOG_ERROR,
          //     L"%s[%d] : Undefined material(%s)",
          //     L"..\\Script\\optiondata.txt",
          //     m_lexer.line + 1,
          //     hostName.c_str())
          // L2_LOG_ERROR(Log,
          //     L"%s[%d] : Undefined material(%s)",
          //     L"..\\Script\\optiondata.txt",
          //     m_lexer.line + 1,
          //     hostName.c_str())
          L2_LOG_ERROR(
              Log,
              u"Option '%ls', optiondata.txt line %lld: unsupported material '%ls'; the material list was cleared and "
              u"remains empty.",
              tempOptionName, static_cast<long long>(m_lexer.line) + 1, hostName.c_str()
          );
      }
    ;

passive_skill_def
    : PASSIVE_SKILL EQUAL '{' n17 '}'
    | CRITICAL_SKILL EQUAL '{' NAME ';' FLOAT_NUMBER '}'
      {
          const auto publishRandomSkill = [&](std::size_t skillIndex, const wchar_t* skillKind) {
              if (m_bOnlyPrecompiledHeader) {
                  return;
              }

              const char16_t* const targetName = SemanticWord($4);
              const std::wstring hostName = target_utf16::ToHostCodeUnits(targetName);
              if (static_cast<int>(g_PrecompiledHeaderDB.Search(targetName)) == -1) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill name [%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Undefined skill name [%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"Option '%ls', optiondata.txt line %lld: random skill '%ls' is missing from the precompiled header; "
                      u"skill "
                      u"lookup and assignment still continue.",
                      tempOptionName, static_cast<long long>(m_lexer.line) + 1,
                      targetName
                  );
              }

              const double rate = grammar::SemanticDouble($6);
              const int integerRate = runtime::Win64CvttDoubleToInt32(rate);
              if (integerRate < 1 || integerRate > 100) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Invalid [%s] %s Skill exercise percent [%d] in line [%d]",
                  //     hostName.c_str(),
                  //     skillKind,
                  //     integerRate,
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Invalid [%s] %s Skill exercise percent [%d] in line [%d]",
                  //     hostName.c_str(),
                  //     skillKind,
                  //     integerRate,
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"Option '%ls', optiondata.txt line %lld: %ls skill '%ls' has rate %.17g (integer check %d, expected "
                      u"1..100); the original rate is still stored.",
                      tempOptionName, static_cast<long long>(m_lexer.line) + 1, skillKind,
                      targetName, rate, integerRate
                  );
              }

              m_pTempOption->skillData.skills[skillIndex].pSkillInfo = g_SkillDB.GetSkillInfoByName(hostName.c_str());
              m_pTempOption->skillData.skills[skillIndex].rate = rate;
          };
          publishRandomSkill(1, L"CRITICAL_SKILL");
      }
    | MAGIC_SKILL EQUAL '{' NAME ';' FLOAT_NUMBER '}'
      {
          const auto publishRandomSkill = [&](std::size_t skillIndex, const wchar_t* skillKind) {
              if (m_bOnlyPrecompiledHeader) {
                  return;
              }

              const char16_t* const targetName = SemanticWord($4);
              const std::wstring hostName = target_utf16::ToHostCodeUnits(targetName);
              if (static_cast<int>(g_PrecompiledHeaderDB.Search(targetName)) == -1) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill name [%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Undefined skill name [%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"Option '%ls', optiondata.txt line %lld: random skill '%ls' is missing from the precompiled header; "
                      u"skill "
                      u"lookup and assignment still continue.",
                      tempOptionName, static_cast<long long>(m_lexer.line) + 1,
                      targetName
                  );
              }

              const double rate = grammar::SemanticDouble($6);
              const int integerRate = runtime::Win64CvttDoubleToInt32(rate);
              if (integerRate < 1 || integerRate > 100) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Invalid [%s] %s Skill exercise percent [%d] in line [%d]",
                  //     hostName.c_str(),
                  //     skillKind,
                  //     integerRate,
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Invalid [%s] %s Skill exercise percent [%d] in line [%d]",
                  //     hostName.c_str(),
                  //     skillKind,
                  //     integerRate,
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"Option '%ls', optiondata.txt line %lld: %ls skill '%ls' has rate %.17g (integer check %d, expected "
                      u"1..100); the original rate is still stored.",
                      tempOptionName, static_cast<long long>(m_lexer.line) + 1, skillKind,
                      targetName, rate, integerRate
                  );
              }

              m_pTempOption->skillData.skills[skillIndex].pSkillInfo = g_SkillDB.GetSkillInfoByName(hostName.c_str());
              m_pTempOption->skillData.skills[skillIndex].rate = rate;
          };
          publishRandomSkill(2, L"MAGIC_SKILL");
      }
    | ATTACK_SKILL EQUAL '{' NAME ';' FLOAT_NUMBER '}'
      {
          const auto publishRandomSkill = [&](std::size_t skillIndex, const wchar_t* skillKind) {
              if (m_bOnlyPrecompiledHeader) {
                  return;
              }

              const char16_t* const targetName = SemanticWord($4);
              const std::wstring hostName = target_utf16::ToHostCodeUnits(targetName);
              if (static_cast<int>(g_PrecompiledHeaderDB.Search(targetName)) == -1) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill name [%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Undefined skill name [%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"Option '%ls', optiondata.txt line %lld: random skill '%ls' is missing from the precompiled header; "
                      u"skill "
                      u"lookup and assignment still continue.",
                      tempOptionName, static_cast<long long>(m_lexer.line) + 1,
                      targetName
                  );
              }

              const double rate = grammar::SemanticDouble($6);
              const int integerRate = runtime::Win64CvttDoubleToInt32(rate);
              if (integerRate < 1 || integerRate > 100) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Invalid [%s] %s Skill exercise percent [%d] in line [%d]",
                  //     hostName.c_str(),
                  //     skillKind,
                  //     integerRate,
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Invalid [%s] %s Skill exercise percent [%d] in line [%d]",
                  //     hostName.c_str(),
                  //     skillKind,
                  //     integerRate,
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      u"Option '%ls', optiondata.txt line %lld: %ls skill '%ls' has rate %.17g (integer check %d, expected "
                      u"1..100); the original rate is still stored.",
                      tempOptionName, static_cast<long long>(m_lexer.line) + 1, skillKind,
                      targetName, rate, integerRate
                  );
              }

              m_pTempOption->skillData.skills[skillIndex].pSkillInfo = g_SkillDB.GetSkillInfoByName(hostName.c_str());
              m_pTempOption->skillData.skills[skillIndex].rate = rate;
          };
          publishRandomSkill(0, L"ATTACK_SKILL");
      }
    | ACTIVE_SKILL EQUAL '{' n19 '}'
    ;

n15
    : n15 ';' integer_def_2
    | integer_def_2
    ;

integer_def_2
    : INTEGER
      { m_sMagicType.insert(grammar::SemanticInt32($1)); }
    | ALL
      { m_sMagicType.insert(-1); }
    ;

n17
    : n17 ';' name_def
    | name_def
    ;

name_def
    : NAME
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const char16_t* const targetName = SemanticWord($1);
          const std::wstring hostName = target_utf16::ToHostCodeUnits(targetName);
          if (g_PrecompiledHeaderDB.Search(targetName) == -1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined skill name [%s] in line [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined skill name [%s] in line [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"Option '%ls', optiondata.txt line %lld: acquired skill '%ls' is missing from the precompiled header; "
                  u"skill lookup and assignment still continue.",
                  tempOptionName, static_cast<long long>(m_lexer.line) + 1,
                  targetName
              );
          }
          m_pTempOption->skillData.acquiredSkills.push_back(g_SkillDB.GetSkillInfoByName(hostName.c_str()));
      }
    ;

n19
    : n19 ';' name_def_2
    | name_def_2
    ;

name_def_2
    : NAME
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          const char16_t* const targetName = SemanticWord($1);
          const std::wstring hostName = target_utf16::ToHostCodeUnits(targetName);
          if (static_cast<int>(g_PrecompiledHeaderDB.Search(targetName)) == -1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined skill name [%s] in line [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined skill name [%s] in line [%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"Option '%ls', optiondata.txt line %lld: acquired skill '%ls' is missing from the precompiled header; "
                  u"skill lookup and assignment still continue.",
                  tempOptionName, static_cast<long long>(m_lexer.line) + 1,
                  targetName
              );
          }
          m_pTempOption->skillData.acquiredSkills.push_back(g_SkillDB.GetSkillInfoByName(hostName.c_str()));
      }
    ;

is_variation_item_equip_reusedelay_def
    : IS_VARIATION_ITEM_EQUIP_REUSEDELAY EQUAL INTEGER
      {
          if (m_bOnlyPrecompiledHeader) {
              break;
          }
          if (!m_pTempOption->IsVariationValueUsable()) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"only option's active_skill can use [is_variation_item_equip_reusedelay] in line [%d]",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"only option's active_skill can use [is_variation_item_equip_reusedelay] in line [%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"Option '%ls', optiondata.txt line %lld: is_variation_item_equip_reusedelay requires an acquired "
                  u"active skill, but none is available; the setting is still applied.",
                  tempOptionName, static_cast<long long>(m_lexer.line) + 1
              );
          }
          const int value = grammar::SemanticInt32($3);
          if (value != 1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"invalid [is_variation_item_equip_reusedelay=%d] in line [%d]",
              //     value,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"invalid [is_variation_item_equip_reusedelay=%d] in line [%d]",
              //     value,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"Option '%ls', optiondata.txt line %lld: is_variation_item_equip_reusedelay is %d, expected 1; "
                  u"applying it as %ls.",
                  tempOptionName, static_cast<long long>(m_lexer.line) + 1, value,
                  value != 0 ? L"enabled" : L"disabled"
              );
          }
          m_pTempOption->SetIsVariationItemEquipReusedelay(value != 0);
      }
    ;

%%
