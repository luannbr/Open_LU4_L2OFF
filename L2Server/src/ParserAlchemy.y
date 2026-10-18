// CParserForAlchemy grammar.
// Bison generates ParserAlchemy.cpp and ParserAlchemyTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserAlchemy.h"

#include "L2Server/src/AlchemySystem.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string_view>

#include "ParserAlchemyTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForAlchemy::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserAlchemyTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForAlchemy::CParserForAlchemy() :
    m_lexer(),
    m_pAlchemyOutputList(nullptr),
    m_pConvResult(nullptr),
    m_pCondition(nullptr),
    m_pConversionCondition(nullptr),
    m_pConversionRecipe(nullptr),
    m_pMixRecipe(nullptr) {
}

CParserForAlchemy::~CParserForAlchemy() = default;

int CParserForAlchemy::Create(char16_t* fileBuffer, const int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForAlchemy::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"CParserForAlchemyDataChecker[%d] : syntax error", m_lexer.line + 1);
    // Log.Add(LOG_ERROR, message)
    // Log.Add(LOG_ERROR, L"%ls", message)
    L2_LOG_ERROR(Log, u"%ls", message);
    // Noise: The error is already logged above. Only Windows needs a modal copy.
    // PopUpWindow(message)
#if defined(_WIN32)
    PopUpWindow(L"%ls", message);
#endif
}
}

%token INTEGER
%token FLOAT_NUMBER
%token IDENTIFIER
%token CONVERSION_RECIPE_BEGIN
%token CONVERSION_RECIPE_END
%token SKILL_ID
%token LEVEL
%token REWARD
%token MATERIAL_LIST
%token MATERIAL_COND_BEGIN
%token MATERIAL_COND_END
%token SUCCESS_REWARD
%token FAIL_REWARD
%token CONV_PROB
%token CONV_SUCCESS_EFFECT
%token CONV_FAIL_EFFECT
%token FIXED_ITEM
%token ROLLY_SUCCESS_EFFECT
%token ROLLY_FIXED_BEGIN
%token ROLLY_FIXED_END
%token LEVEL_COND_BEGIN
%token LEVEL_COND_END
%token ROLLY_PROB
%token ROLLY_REWARD
%token ROLLY_LEVEL
%token ROLLY_BASE_ID
%token ROLLY_BASE_DP
%token ROLLY_CRITERION
%token GLOBAL_BEGIN
%token GLOBAL_END
// The original grammar has 7 shift/reduce and 4 reduce/reduce conflicts. Bison
// resolves them as the original tables did. A shift wins over a reduce,
// and the earlier rule wins over a later rule.
%start n1

%%

n1
    : n1 n2
    | n2
    | %empty
    ;

n2
    : rolly_fixed
    | conversion_recipe
    | global
    ;

global
    : GLOBAL_BEGIN ROLLY_BASE_ID '=' INTEGER ROLLY_BASE_DP '=' INTEGER ROLLY_CRITERION '=' INTEGER
      {
          g_AlchemyDB.m_nRollyBaseItemID = grammar::SemanticInt32($4);
          g_AlchemyDB.m_nRollyBaseItemDP = grammar::SemanticInt32($7);
          g_AlchemyDB.m_nRollyAmountCriterion = grammar::SemanticInt64($10);
      }
      GLOBAL_END
    | %empty
    ;

rolly_fixed
    : ROLLY_FIXED_BEGIN n6 fixed_item_def rolly_success_effect_def level_cond_list ROLLY_FIXED_END
      { g_AlchemyDB.SetRollyRecipe(m_pMixRecipe); }
    ;

n6
    : %empty
      { m_pMixRecipe = new AlchemyRecipeStructures::AlchemyRollyRecipe; }
    ;

fixed_item_def
    : FIXED_ITEM '=' '{' n11 '}'
    ;

rolly_success_effect_def
    : ROLLY_SUCCESS_EFFECT '=' INTEGER
      { m_pMixRecipe->m_nSuccessEffectId = grammar::SemanticInt32($3); }
    ;

level_cond_list
    : level_cond_list level_cond
    | level_cond
    ;

level_cond
    : LEVEL_COND_BEGIN level_def_2 rolly_prob_def LEVEL_COND_END
    ;

n11
    : n11 ';' INTEGER
      { m_pMixRecipe->AddFixedItem(grammar::SemanticInt32($3)); }
    | INTEGER
      { m_pMixRecipe->AddFixedItem(grammar::SemanticInt32($1)); }
    ;

level_def
    : LEVEL '=' INTEGER
      { m_nLevel = grammar::SemanticInt32($3); }
    ;

level_def_2
    : LEVEL '=' INTEGER
      { m_pMixRecipe->AddLevelCond(grammar::SemanticInt32($3)); }
    ;

rolly_prob_def
    : ROLLY_PROB '=' FLOAT_NUMBER ROLLY_REWARD '=' '{' INTEGER ';' INTEGER '}'
      {
          m_pMixRecipe->AddOutput(
              grammar::SemanticInt32($7), grammar::SemanticInt64($9), grammar::SemanticDouble($3)
          );
      }
    ;

conversion_recipe
    : CONVERSION_RECIPE_BEGIN n16 n17 n19 CONVERSION_RECIPE_END
      {
          g_AlchemyDB.AddConversionRecipe(m_nKeyId, m_nLevel, m_pConversionRecipe);
      }
    ;

n16
    : %empty
      {
          m_pConversionRecipe = new AlchemyRecipeStructures::AlchemyConversionRecipe;
          m_nKeyId = 0;
          m_nLevel = 0;
      }
    ;

n17
    : skill_id_def level_def
    ;

skill_id_def
    : SKILL_ID '=' INTEGER
      { m_nKeyId = grammar::SemanticInt32($3); }
    ;

n19
    : material_cond_list
    ;

material_cond_list
    : material_cond_list material_cond
    | material_cond
    ;

material_cond
    : MATERIAL_COND_BEGIN n22 n23 conv_success_effect_def MATERIAL_COND_END
      {
          m_pConversionRecipe->AddOutputAndCond(m_pConversionCondition, m_pConvResult);
      }
    ;

n22
    : %empty
      {
          m_pConversionCondition = new AlchemyConditionStructures::AlchemyConvCond;
          m_pConvResult = new AlchemyResultStructures::AlchemyConvOutput;
      }
    ;

n23
    : material_list_def n27
    ;

material_list_def
    : MATERIAL_LIST '=' '{' n25 '}'
    ;

n25
    : n25 ';' n26
    | n26
    ;

n26
    : '{' INTEGER ';' INTEGER '}'
      {
          m_pConversionCondition->m_mapMaterialCond.emplace(
              AlchemyConditionStructures::AlchemyMaterialKey(grammar::SemanticInt32($2), grammar::SemanticInt64($4)),
              true
          );
      }
    ;

n27
    : success_reward_def fail_reward_def conv_prob_def
    ;

success_reward_def
    : SUCCESS_REWARD '=' '{' INTEGER ';' INTEGER '}'
      {
          m_pConvResult->m_successItem.nItemClassId = grammar::SemanticInt32($4);
          m_pConvResult->m_successItem.nAmount = grammar::SemanticInt64($6);
      }
    ;

fail_reward_def
    : FAIL_REWARD '=' '{' INTEGER ';' INTEGER '}'
      {
          m_pConvResult->m_failItem.nItemClassId = grammar::SemanticInt32($4);
          m_pConvResult->m_failItem.nAmount = grammar::SemanticInt64($6);
      }
    ;

conv_prob_def
    : CONV_PROB '=' FLOAT_NUMBER
      { m_pConvResult->m_dProb = grammar::SemanticDouble($3); }
    | %empty
    ;

conv_success_effect_def
    : CONV_SUCCESS_EFFECT '=' INTEGER CONV_FAIL_EFFECT '=' INTEGER
      {
          m_pConvResult->m_nSuccessEffect = grammar::SemanticInt32($3);
          m_pConvResult->m_nFailEffect = grammar::SemanticInt32($6);
      }
    | %empty
    ;

%%
