// fishingdata.txt grammar (original L2Server/fishing_parser.y).
// Bison generates FishingParser.cpp and FishingParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/FishingParser.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

#include "FishingParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForFishing::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"FishingParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

char16_t* SemanticText(grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

} // namespace

CParserForFishing::CParserForFishing() : m_vPreference(), m_lexer() {}

CParserForFishing::~CParserForFishing() = default;

int CParserForFishing::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForFishing::yysyntaxerror() {
    char16_t message[56]{};
    FormatUtf16To(message, std::size(message), u"FishingDataChecker[%d] : syntax error", m_lexer.line + 1);
    // Log.Add(LOG_ERROR, message)
    // L2_LOG_ERROR(Log, message)
    L2_LOG_ERROR(Log, u"%ls", message);

    std::wstring word;
    if (m_lexer.yytext != nullptr && m_lexer.yyleng > 0) {
        // word.reserve(
        //     static_cast<std::size_t>(m_lexer.yyleng))
        // for (int index = 0.
        //      index < m_lexer.yyleng.
        //      ++index) {
        //     word.push_back(
        //         static_cast<wchar_t>(
        //             m_lexer.yytext[index]))
        // }
        // Lexer text is UTF-16. Decode pairs before native-wide formatting.
        word = Utf16ToWide(std::u16string_view(m_lexer.yytext, static_cast<std::size_t>(m_lexer.yyleng)));
    }
    // Log.Add(LOG_ERROR, word.c_str())
    // L2_LOG_ERROR(Log, word.c_str())
    L2_LOG_ERROR(Log, u"%ls", word.c_str());
}
}

%token DISTRIBUTION_BEGIN
%token DISTRIBUTION_ID
%token DEFAULT_DISTRIBUTION
%token NIGHT_FISHING_DISTRIBUTION
%token DISTRIBUTION_END
%token FISHING_PLACE_BEGIN
%token FISHING_PLACE_ID
%token TERRITORY_GROUP
%token TERRITORY
%token LIMIT_GRID
%token FISHING_PLACE_TYPE
%token MAINTAIN_DISTRIBUTION_TIME
%token FISHING_SPECIES
%token FISHING_PLACE_END
%token LURE_BEGIN
%token LURE_ID
%token LURE_ITEM_ID
%token REVISION_NUMBER
%token FISH_GROUP_PREFERENCE
%token LENGTH_BONUS
%token LENGTH_RATE_BONUS
%token LURE_TYPE
%token LURE_GRADE
%token NONE
%token LURE_END
%token FISH_BEGIN
%token FISH_ID
%token ITEM_ID
%token ITEM_NAME
%token FISH_GROUP
%token FISH_LEVEL
%token FISH_GUTS
%token FISH_HP
%token HP_REGEN
%token START_COMBAT_TIME
%token COMBAT_DURATION
%token GUTS_CHECK_TIME
%token GUTS_CHECK_PROBABILITY
%token CHEATING_PROB
%token FISH_MAX_LENGTH
%token FISH_LENGTH_RATE
%token FISH_BITE_RATE
%token FISH_GRADE
%token FISH_SPECIES
%token FISH_EXP
%token FISH_SP
%token FISH_END
%token FISHINGROD_BEGIN
%token FISHINGROD_ID
%token FISHINGROD_ITEM_ID
%token FISHINGROD_LEVEL
%token FISHINGROD_NAME
%token FISHINGROD_DAMAGE
%token FISHING_DURATION_BONUS
%token FISHINGROD_END
%token FISHINGMONSTER_BEGIN
%token USER_LEVEL
%token MONSTER_PROBABILITY
%token FISHINGMONSTERS
%token FISHINGMONSTER_END
%token FISHING_SKILL_BEGIN
%token FISHINGSKILL_LEVEL
%token FISHINGSKILL_FISHID
%token FISHING_SKILL_END
%token FISHING_PROBABLITY_BEGIN
%token PROBABLITY_PERCENT
%token FISHING_PROBABLITY_END
%token FISHING_SETTING_BEGIN
%token WAIT_NEXT_FISHING
%token FISHING_SETTING_END
%token NAME
%token IDENTIFIER
%token INTEGER
%token FLOAT_NUMBER
%start n1

%%

n1
    : n2 n12 n20 n30 n36 n42 n50 fishing_probablity fishing_setting
    ;

n2
    : distribution_list
    ;

distribution_list
    : distribution_list distribution
    | %empty
    ;

distribution
    : DISTRIBUTION_BEGIN
      { m_pDistributionInfo = new CFishingDistributionInfo; }
      distribution_id_def_list DISTRIBUTION_END
      {
          g_FishingDB.Add(m_pDistributionInfo);
          m_pDistributionInfo = nullptr;
      }
    ;

distribution_id_def_list
    : distribution_id_def_list distribution_id_def
    | %empty
    ;

distribution_id_def
    : DISTRIBUTION_ID '=' INTEGER
      { m_pDistributionInfo->m_nId = grammar::SemanticInt32($3); }
    | DEFAULT_DISTRIBUTION '=' '{' n8 '}'
    | NIGHT_FISHING_DISTRIBUTION '=' '{' n10 '}'
    ;

n8
    : n8 ';' '{' identifier_def '}'
    | '{' identifier_def '}'
    ;

identifier_def
    : IDENTIFIER ';' INTEGER
      {
          auto* const distribution = new DistributionStruct{
              static_cast<int>(g_PrecompiledHeaderDB.Search(SemanticText($1))), grammar::SemanticInt32($3)
          };
          m_pDistributionInfo->m_vDefaultDistribution.push_back(distribution);
          std::free(SemanticText($1));
      }
    ;

n10
    : n10 ';' '{' identifier_def_2 '}'
    | '{' identifier_def_2 '}'
    | %empty
    ;

identifier_def_2
    : IDENTIFIER ';' INTEGER
      {
          auto* const distribution = new DistributionStruct{
              static_cast<int>(g_PrecompiledHeaderDB.Search(SemanticText($1))), grammar::SemanticInt32($3)
          };
          m_pDistributionInfo->m_vEventDistribution.push_back(distribution);
          std::free(SemanticText($1));
      }
    ;

n12
    : fishing_place_list
    ;

fishing_place_list
    : fishing_place_list fishing_place
    | %empty
    ;

fishing_place
    : FISHING_PLACE_BEGIN
      {
          m_pPlaceInfo = new CFishingPlaceInfo;
          m_pTerritory = new CTerritory;
      }
      fishing_place_id_def_list FISHING_PLACE_END
      {
          m_pPlaceInfo->m_pTerritory = m_pTerritory;
          g_FishingDB.Add(m_pPlaceInfo);
          m_pPlaceInfo = nullptr;
      }
    ;

fishing_place_id_def_list
    : fishing_place_id_def_list fishing_place_id_def
    | %empty
    ;

fishing_place_id_def
    : FISHING_PLACE_ID '=' INTEGER
      { m_pPlaceInfo->m_nId = grammar::SemanticInt32($3); }
    | TERRITORY_GROUP '=' INTEGER
      { m_pPlaceInfo->m_nTerritoryGroup = grammar::SemanticInt32($3); }
    | TERRITORY '=' '{' n18 '}'
    | LIMIT_GRID '=' INTEGER
      { m_pPlaceInfo->m_nLimitGrid = grammar::SemanticInt32($3); }
    | DISTRIBUTION_ID '=' INTEGER
      { m_pPlaceInfo->m_nDistributionId = grammar::SemanticInt32($3); }
    | FISHING_PLACE_TYPE '=' IDENTIFIER
      {
          m_pPlaceInfo->m_nPlaceType = static_cast<int>(g_PrecompiledHeaderDB.Search(SemanticText($3)));
          std::free(SemanticText($3));
      }
    | MAINTAIN_DISTRIBUTION_TIME '=' INTEGER
      { m_pPlaceInfo->m_nMaintainTime = grammar::SemanticInt32($3); }
    | FISHING_SPECIES '=' INTEGER
      { m_pPlaceInfo->m_nFishSpecies = grammar::SemanticInt32($3); }
    ;

n18
    : n18 ';' '{' integer_def '}'
    | '{' integer_def '}'
    ;

integer_def
    : INTEGER ';' INTEGER ';' INTEGER ';' INTEGER
      {
          const int bottom = grammar::SemanticInt32($5);
          const int top = grammar::SemanticInt32($7);
          if (bottom < m_pTerritory->GetBottom()) {
              m_pTerritory->SetBottom(bottom);
          }
          if (m_pTerritory->GetTop() < top) {
              m_pTerritory->SetTop(top);
          }
          m_pPoint = new FVector(
              static_cast<double>(grammar::SemanticInt32($1)), static_cast<double>(grammar::SemanticInt32($3)), 0.0
          );
          m_pTerritory->AddTerritoryPoint(m_pPoint);
      }
    ;

n20
    : lure_list
    ;

lure_list
    : lure_list lure
    | %empty
    ;

lure
    : LURE_BEGIN
      { m_pLureInfo = new CFishingLureInfo; }
      lure_id_def_list LURE_END
      {
          g_FishingDB.Add(m_pLureInfo);
          m_pLureInfo = nullptr;
      }
    ;

lure_id_def_list
    : lure_id_def_list lure_id_def
    | %empty
    ;

lure_id_def
    : LURE_ID '=' INTEGER
      { m_pLureInfo->m_nId = grammar::SemanticInt32($3); }
    | LURE_ITEM_ID '=' INTEGER
      { m_pLureInfo->m_nItemId = grammar::SemanticInt32($3); }
    | REVISION_NUMBER '=' FLOAT_NUMBER
      { m_pLureInfo->m_dRevision = grammar::SemanticDouble($3); }
    | FISH_GROUP_PREFERENCE '=' '{' n26 '}'
    | LURE_TYPE '=' IDENTIFIER
      {
          m_pLureInfo->m_nLureType = static_cast<int>(g_PrecompiledHeaderDB.Search(SemanticText($3)));
          if (m_pLureInfo->m_nLureType == -1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined lure type name[%s] in line [%d]",
              //     SemanticText($3),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(Log, u"Undefined lure type name[%s] in line [%d]", SemanticText($3), m_lexer.line + 1);
          }
          std::free(SemanticText($3));
      }
    | LURE_GRADE '=' INTEGER
      { m_pLureInfo->m_nLureGrade = grammar::SemanticInt32($3); }
    | LENGTH_BONUS '=' INTEGER
      { m_pLureInfo->m_nLengthBonus = grammar::SemanticInt32($3); }
    | LENGTH_RATE_BONUS '=' FLOAT_NUMBER
      { m_pLureInfo->m_dLengthRateBonus = grammar::SemanticDouble($3); }
    ;

n26
    : n26 ';' '{' integer_def_2 '}'
    | '{' integer_def_2 '}'
    ;

integer_def_2
    : INTEGER ';' n28
      {
          const int group = grammar::SemanticInt32($1);
          if (m_pLureInfo->m_mPreference.find(group) == m_pLureInfo->m_mPreference.end()) {
              if (!m_vPreference.empty()) {
                  m_pLureInfo->m_mPreference.emplace(group, m_vPreference);
              }
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Duplicate fish group number[%d] in line [%d]",
              //     group,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(Log, u"Duplicate fish group number[%d] in line [%d]", group, m_lexer.line + 1);
          }
          m_vPreference.clear();
      }
    ;

n28
    : n28 ';' '{' identifier_def_3 '}'
    | '{' identifier_def_3 '}'
    ;

identifier_def_3
    : IDENTIFIER ';' INTEGER
      {
          char16_t* const name = SemanticText($1);
          auto* const distribution =
              new DistributionStruct{static_cast<int>(g_PrecompiledHeaderDB.Search(name)), grammar::SemanticInt32($3)};
          if (distribution->GroupID == -1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined fish name[%s] in line [%d]",
              //     name,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(Log, u"Undefined fish name[%s] in line [%d]", name, m_lexer.line + 1);
          }
          m_vPreference.push_back(distribution);
          std::free(name);
      }
    | NONE
    ;

n30
    : fish_list
    ;

fish_list
    : fish_list fish
    | %empty
    ;

fish
    : FISH_BEGIN
      { m_pFishInfo = new CFishInfo; }
      fish_id_def_list FISH_END
      {
          g_FishingDB.Add(m_pFishInfo);
          m_pFishInfo = nullptr;
      }
    ;

fish_id_def_list
    : fish_id_def_list fish_id_def
    | %empty
    ;

fish_id_def
    : FISH_ID '=' INTEGER
      { m_pFishInfo->m_nId = grammar::SemanticInt32($3); }
    | ITEM_ID '=' INTEGER
      { m_pFishInfo->m_nItemId = grammar::SemanticInt32($3); }
    | ITEM_NAME '=' NAME
    | FISH_GROUP '=' IDENTIFIER
      {
          m_pFishInfo->m_nFishGroupId = static_cast<int>(g_PrecompiledHeaderDB.Search(SemanticText($3)));
          if (m_pFishInfo->m_nFishGroupId == -1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined fish name[%s] in line [%d]",
              //     SemanticText($3),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(Log, u"Undefined fish name[%s] in line [%d]", SemanticText($3), m_lexer.line + 1);
          }
          std::free(SemanticText($3));
      }
    | FISH_LEVEL '=' INTEGER
      { m_pFishInfo->m_nFishLevel = grammar::SemanticInt32($3); }
    | FISH_GUTS '=' FLOAT_NUMBER
      { m_pFishInfo->m_dFishGuts = grammar::SemanticDouble($3); }
    | FISH_HP '=' INTEGER
      { m_pFishInfo->m_nHp = grammar::SemanticInt32($3); }
    | HP_REGEN '=' FLOAT_NUMBER
      { m_pFishInfo->m_dHpRegen = grammar::SemanticDouble($3); }
    | START_COMBAT_TIME '=' INTEGER
      { m_pFishInfo->m_nStartCombatTime = grammar::SemanticInt32($3); }
    | COMBAT_DURATION '=' INTEGER
      { m_pFishInfo->m_nCombatDuration = grammar::SemanticInt32($3); }
    | GUTS_CHECK_TIME '=' INTEGER
      { m_pFishInfo->m_nGutsCheckTime = grammar::SemanticInt32($3); }
    | GUTS_CHECK_PROBABILITY '=' FLOAT_NUMBER
      { m_pFishInfo->m_dGutsCheckProbability = grammar::SemanticDouble($3); }
    | CHEATING_PROB '=' FLOAT_NUMBER
      { m_pFishInfo->m_dCheatingProbability = grammar::SemanticDouble($3); }
    | FISH_MAX_LENGTH '=' INTEGER
      { m_pFishInfo->m_nLengthMax = grammar::SemanticInt32($3); }
    | FISH_LENGTH_RATE '=' FLOAT_NUMBER
      { m_pFishInfo->m_dLengthRate = grammar::SemanticDouble($3); }
    | FISH_BITE_RATE '=' FLOAT_NUMBER
      { m_pFishInfo->m_dBiteRate = grammar::SemanticDouble($3); }
    | FISH_GRADE '=' IDENTIFIER
      {
          m_pFishInfo->m_nFishGrade = static_cast<int>(g_PrecompiledHeaderDB.Search(SemanticText($3)));
          if (m_pFishInfo->m_nFishGrade == -1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined fish grade[%s] in line [%d]",
              //     SemanticText($3),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(Log, u"Undefined fish grade[%s] in line [%d]", SemanticText($3), m_lexer.line + 1);
          }
          std::free(SemanticText($3));
      }
    | FISH_SPECIES '=' INTEGER
      { m_pFishInfo->m_nFishSpecies = grammar::SemanticInt32($3); }
    | FISH_EXP '=' INTEGER
      { m_pFishInfo->m_nExp = grammar::SemanticInt64($3); }
    | FISH_SP '=' INTEGER
      { m_pFishInfo->m_nSp = grammar::SemanticInt64($3); }
    ;

n36
    : fishingrod_list
    ;

fishingrod_list
    : fishingrod_list fishingrod
    | %empty
    ;

fishingrod
    : FISHINGROD_BEGIN
      { m_pRodInfo = new CFishingRodInfo; }
      fishingrod_id_def_list FISHINGROD_END
      {
          g_FishingDB.Add(m_pRodInfo);
          m_pRodInfo = nullptr;
      }
    ;

fishingrod_id_def_list
    : fishingrod_id_def_list fishingrod_id_def
    | %empty
    ;

fishingrod_id_def
    : FISHINGROD_ID '=' INTEGER
      { m_pRodInfo->m_nId = grammar::SemanticInt32($3); }
    | FISHINGROD_ITEM_ID '=' INTEGER
      { m_pRodInfo->m_nItemId = grammar::SemanticInt32($3); }
    | FISHINGROD_LEVEL '=' INTEGER
      { m_pRodInfo->m_nLevel = grammar::SemanticInt32($3); }
    | FISHINGROD_NAME '=' NAME
    | FISHINGROD_DAMAGE '=' FLOAT_NUMBER
      { m_pRodInfo->m_dDamage = grammar::SemanticDouble($3); }
    | FISHING_DURATION_BONUS '=' INTEGER
      {
          m_pRodInfo->m_nFishingDurationBonus = grammar::SemanticInt32($3);
          if (m_pRodInfo->m_nFishingDurationBonus < 3601 && m_pRodInfo->m_nFishingDurationBonus > -3601) {
              break;
          }
          // Log.Add(
          //     LOG_ERROR,
          //     L"invalid fishing_duration_bonus [%d] in line [%d]",
          //     m_pRodInfo->m_nFishingDurationBonus,
          //     m_lexer.line + 1)
          L2_LOG_ERROR(
              Log, u"invalid fishing_duration_bonus [%d] in line [%d]", m_pRodInfo->m_nFishingDurationBonus,
              m_lexer.line + 1
          );
          ForceCrash(CrashInfo::LEX_YACC_FAILED);
      }
    ;

n42
    : fishingmonster_list
    ;

fishingmonster_list
    : fishingmonster_list fishingmonster
    | %empty
    ;

fishingmonster
    : FISHINGMONSTER_BEGIN
      { m_pMonsterInfo = new CFishingMonsterInfo; }
      user_level_def_list FISHINGMONSTER_END
      {
          g_FishingDB.Add(m_pMonsterInfo);
          m_pMonsterInfo = nullptr;
      }
    ;

user_level_def_list
    : user_level_def_list user_level_def
    | %empty
    ;

user_level_def
    : USER_LEVEL '=' '{' INTEGER ';' INTEGER '}'
      {
          m_pMonsterInfo->m_nBegin = grammar::SemanticInt32($4);
          m_pMonsterInfo->m_nEnd = grammar::SemanticInt32($6);
      }
    | MONSTER_PROBABILITY '=' FLOAT_NUMBER
      { m_pMonsterInfo->m_dProbability = grammar::SemanticDouble($3); }
    | FISHINGMONSTERS '=' '{' n48 '}'
    ;

n48
    : n48 ';' identifier_def_4
    | identifier_def_4
    ;

identifier_def_4
    : IDENTIFIER
      {
          char16_t* const name = SemanticText($1);
          const int monsterId = static_cast<int>(g_PrecompiledHeaderDB.Search(name));
          m_pMonsterInfo->m_vMonsterId.push_back(monsterId);
          if (monsterId == -1) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined monster name[%s] in line [%d]",
              //     name,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(Log, u"Undefined monster name[%s] in line [%d]", name, m_lexer.line + 1);
          }
          std::free(name);
      }
    ;

n50
    : fishing_skill_list
    ;

fishing_skill_list
    : fishing_skill_list fishing_skill
    | %empty
    ;

fishing_skill
    : FISHING_SKILL_BEGIN
      { m_pSkillInfo = new CFishingSkillInfo; }
      fishingskill_level_def_list FISHING_SKILL_END
      {
          g_FishingDB.Add(m_pSkillInfo);
          m_pSkillInfo = nullptr;
      }
    ;

fishingskill_level_def_list
    : fishingskill_level_def_list fishingskill_level_def
    | %empty
    ;

fishingskill_level_def
    : FISHINGSKILL_LEVEL '=' INTEGER
      { m_pSkillInfo->m_nID = grammar::SemanticInt32($3); }
    | FISHINGSKILL_FISHID '=' '{' n56 '}'
    ;

n56
    : n56 ';' INTEGER
      { m_pSkillInfo->m_vFishId.push_back(grammar::SemanticInt32($3)); }
    | INTEGER
      { m_pSkillInfo->m_vFishId.push_back(grammar::SemanticInt32($1)); }
    ;

fishing_probablity
    : FISHING_PROBABLITY_BEGIN PROBABLITY_PERCENT '=' FLOAT_NUMBER FISHING_PROBABLITY_END
      { g_FishingDB.m_dFishingProbablityProb = grammar::SemanticDouble($4); }
    | %empty
    ;

fishing_setting
    : FISHING_SETTING_BEGIN { } wait_next_fishing_def_list FISHING_SETTING_END
    ;

wait_next_fishing_def_list
    : wait_next_fishing_def_list wait_next_fishing_def
    | %empty
    ;

wait_next_fishing_def
    : WAIT_NEXT_FISHING '=' INTEGER
      {
          const int wait = grammar::SemanticInt32($3);
          if (wait > 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[NO_ERROR] wait_next_fishing[%d] in line [%d]",
              //     wait,
              //     m_lexer.line + 1)
              // Logging improvement: the positive setting is accepted, so this
              // is informational. The invalid-value error below is unchanged.
              L2_LOG_INFO(
                  Log, u"[fishing] Loaded wait_next_fishing=%d from fishingdata.txt line %d.", wait, m_lexer.line + 1
              );
              g_FishingDB.m_nWaitNextFishing = wait;
              break;
          }
          // Log.Add(
          //     LOG_ERROR,
          //     L"Invalid wait_next_fishing[%d] in line [%d]",
          //     wait,
          //     m_lexer.line + 1)
          L2_LOG_ERROR(Log, u"Invalid wait_next_fishing[%d] in line [%d]", wait, m_lexer.line + 1);
          ForceCrash(CrashInfo::LEX_YACC_FAILED);
      }
    ;

%%
