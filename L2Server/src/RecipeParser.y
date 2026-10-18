// recipe.txt grammar (original L2Server/recipe_parser.y).
// Bison generates RecipeParser.cpp and RecipeParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/RecipeParser.h"

#include "L2Server/src/ObjectDB.h"
#include "L2Server/src/Recipe.h"

#include "Shared/src/Log.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "RecipeParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForRecipe::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"RecipeParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

char16_t* SemanticText(grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

} // namespace

CParserForRecipe::CParserForRecipe() : m_lexer() {
    nProb = 0;
}

CParserForRecipe::~CParserForRecipe() = default;

int CParserForRecipe::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token EQUAL
%token RECIPE_BEGIN
%token LEVEL
%token MATERIAL
%token CATALYST
%token PRODUCT
%token MP_CONSUME
%token SUCCESS_RATE
%token NPC_FEE
%token ITEM_ID
%token ISCOMMONRECIPE
%token CREATE_CRITICAL
%token RECIPE_END
%token NONE
%token RARE_CONDITION
%token RARE_ANNOUNCE
%token NAME
%token IDENTIFIER
%token INTEGER
%token FLOAT_NUMBER
%start n1

%%

n1
    : recipe_list
    ;

recipe_list
    : recipe_list recipe
    | %empty
    ;

recipe
    : RECIPE_BEGIN NAME INTEGER
      {
          m_pRecipeInfo = new CRecipeInfo;
          m_pRecipeInfo->m_nId = grammar::SemanticInt32($3);
          nProb = 0;
      }
      level_def_list RECIPE_END
      {
          g_RecipeDB.Add(m_pRecipeInfo);
          m_pRecipeInfo = nullptr;
          if (nProb != 100) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Probability is invalid[%d] in line [%d]",
              //     nProb,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Probability is invalid[%d] in line [%d]",
              //     nProb,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "recipe.txt line %lld: product probabilities total %d%%; expected 100%%. Parsing continues without "
                  "adjusting probabilities.",
                  static_cast<long long>(m_lexer.line) + 1, nProb
              );
          }
      }
    ;

level_def_list
    : level_def_list level_def
    | %empty
    ;

level_def
    : LEVEL EQUAL INTEGER
      { m_pRecipeInfo->m_nLevel = grammar::SemanticInt32($3); }
    | MATERIAL EQUAL '{' n7 '}'
    | NPC_FEE EQUAL '{' n9 '}'
    | CATALYST EQUAL '{' n11 '}'
    | PRODUCT EQUAL '{' n13 '}'
    | MP_CONSUME EQUAL INTEGER
      { m_pRecipeInfo->m_nMpConsume = grammar::SemanticInt32($3); }
    | SUCCESS_RATE EQUAL INTEGER
      { m_pRecipeInfo->m_nSuccessRate = grammar::SemanticInt32($3); }
    | ITEM_ID EQUAL INTEGER
      { m_pRecipeInfo->m_nItemId = grammar::SemanticInt32($3); }
    | ISCOMMONRECIPE EQUAL INTEGER
      { m_pRecipeInfo->m_nCommonRecipe = grammar::SemanticInt32($3); }
    | CREATE_CRITICAL EQUAL INTEGER
      { m_pRecipeInfo->m_bCreateCritical = grammar::SemanticInt32($3) == 1; }
    | RARE_CONDITION EQUAL INTEGER
      {
          const std::int64_t offeringDP = grammar::SemanticInt64($3);
          if (offeringDP > 0) {
              m_pRecipeInfo->m_bUseOffering = true;
              m_pRecipeInfo->m_nOfferingDP = offeringDP;
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"rare_condition DP is invalid[%d] in line [%d]",
              //     static_cast<int>(offeringDP),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"rare_condition DP is invalid[%d] in line [%d]",
              //     static_cast<int>(offeringDP),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log, "recipe.txt line %lld: rare_condition DP %lld must be positive; this value is ignored.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<long long>(offeringDP)
              );
          }
      }
    | RARE_ANNOUNCE EQUAL INTEGER
      {
          if (grammar::SemanticInt32($3) == 1) {
              m_pRecipeInfo->m_bAnnounce = true;
          }
      }
    ;

n7
    : n7 ';' '{' name_def '}'
    | '{' name_def '}'
    ;

name_def
    : NAME ';' INTEGER
      {
          auto* const item = new RecipeItemStruct;
          char16_t* const itemName = SemanticText($1);
          item->ItemClassId = g_ObjectDB.GetClassIdFromName(itemName, false);
          item->Count = grammar::SemanticInt64($3);
          m_pRecipeInfo->m_vMaterial.push_back(item);
          std::free(itemName);
      }
    ;

n9
    : n9 ';' '{' name_def_2 '}'
    | '{' name_def_2 '}'
    | %empty
    ;

name_def_2
    : NAME ';' INTEGER
      {
          auto* const item = new RecipeItemStruct;
          char16_t* const itemName = SemanticText($1);
          item->ItemClassId = g_ObjectDB.GetClassIdFromName(itemName, false);
          item->Count = grammar::SemanticInt64($3);
          m_pRecipeInfo->m_vNPCFee.push_back(item);
          std::free(itemName);
      }
    ;

n11
    : n11 ';' '{' name_def_3 '}'
    | '{' name_def_3 '}'
    | %empty
    ;

name_def_3
    : NAME ';' INTEGER
      {
          auto* const item = new RecipeItemStruct;
          char16_t* const itemName = SemanticText($1);
          item->ItemClassId = g_ObjectDB.GetClassIdFromName(itemName, false);
          item->Count = grammar::SemanticInt64($3);
          m_pRecipeInfo->m_vCatalyst.push_back(item);
          std::free(itemName);
      }
    ;

n13
    : n13 ';' '{' name_def_4 '}'
    | '{' name_def_4 '}'
    | %empty
    ;

name_def_4
    : NAME ';' INTEGER
      {
          auto* const product = new RecipeProductItemStruct;
          char16_t* const itemName = SemanticText($1);
          product->ItemClassId = g_ObjectDB.GetClassIdFromName(itemName, false);
          product->Count = grammar::SemanticInt32($3);
          product->Prob = 100;
          m_pRecipeInfo->m_vProduct.push_back(product);
          nProb = 100;
          std::free(itemName);
      }
    | NAME ';' INTEGER ';' INTEGER
      {
          auto* const product = new RecipeProductItemStruct;
          char16_t* const itemName = SemanticText($1);
          product->ItemClassId = g_ObjectDB.GetClassIdFromName(itemName, false);
          product->Count = grammar::SemanticInt32($3);
          product->Prob = grammar::SemanticInt32($5);
          m_pRecipeInfo->m_vProduct.push_back(product);
          nProb += product->Prob;
          std::free(itemName);
      }
    ;

%%
