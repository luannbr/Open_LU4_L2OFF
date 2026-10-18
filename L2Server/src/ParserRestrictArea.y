// CParserForRestrictArea grammar (original L2Server/parser_restrict_area.y).
// Bison generates ParserRestrictArea.cpp and ParserRestrictAreaTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserRestrictArea.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/CWorld.h"
#include "L2Server/src/RestrictArea.h"
#include "L2Server/src/WorldCollision.h"

#include "Shared/src/GeoData.h"
#include "Shared/src/Log.h"
#include "Shared/src/Territory.h"

#include <cstdint>
#include <cstring>
#include <string_view>

#include "ParserRestrictAreaTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForRestrictArea::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserRestrictAreaTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForRestrictArea::CParserForRestrictArea() : m_lexer() {
    m_pGeoData = CWorld::Inst().GetGeo();
    pTempTerritory = nullptr;
    m_pServerBlock = nullptr;
}

CParserForRestrictArea::~CParserForRestrictArea() = default;

int CParserForRestrictArea::Create(char16_t* const fileBuffer, const int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token NOFLY_BEGIN
%token NOFLY_END
%token NO_CALL_PC_BEGIN
%token NO_CALL_PC_END
%token NO_CHANGE_PC_BEGIN
%token NO_CHANGE_PC_END
%token NO_DROP_ITEM_BEGIN
%token NO_DROP_ITEM_END
%token NO_SAVE_BOOKMARK_BEGIN
%token NO_SAVE_BOOKMARK_END
%token NO_USE_BOOKMARK_BEGIN
%token NO_USE_BOOKMARK_END
%token TRANSFORMABLE_BEGIN
%token TRANSFORMABLE_END
%token ANNOUNCE_AREA_BEGIN
%token ANNOUNCE_AREA_END
%token NAME
%token SYSTEM_MSG_ID
%token RANGE
%token TERRITORY_BEGIN
%token TERRITORY_END
%token SERVER_BLOCK_BEGIN
%token SERVER_BLOCK_END
%token MIN_KW
%token MAX_KW
%token INTEGER
%token NAME_2
%start n1

%%

n1
    : n2
    ;

n2
    : n2 n3
    | n3
    ;

n3
    : nofly
    | no_call_pc
    | no_drop_item
    | no_save_bookmark
    | no_use_bookmark
    | transformable
    | territory
    | server_block
    | announce_area
    | no_change_pc
    ;

announce_area
    : ANNOUNCE_AREA_BEGIN NAME_2
      {
          const auto beginTerritory = [&]() {
              pTempTerritory = new CTerritory;
              pTempTerritory->SetName(grammar::SemanticPointer<char16_t>($2));
          };
          beginTerritory();
      }
      '{' n25 '}' ANNOUNCE_AREA_END
      { g_NpcAnnounceDB.AddNpcPos(pTempTerritory); }
    ;

nofly
    : NOFLY_BEGIN NAME '=' NAME_2
      {
          const auto beginTerritory = [&]() {
              pTempTerritory = new CTerritory;
              pTempTerritory->SetName(grammar::SemanticPointer<char16_t>($4));
          };
          beginTerritory();
      }
      SYSTEM_MSG_ID '=' INTEGER RANGE '=' '{' n25 '}' NOFLY_END
      {
          g_RestrictedAreaDB.AddNpcPos(pTempTerritory);
          m_pGeoData->AddNoFlyArea(pTempTerritory);
      }
    ;

no_call_pc
    : NO_CALL_PC_BEGIN NAME '=' NAME_2
      {
          const auto beginTerritory = [&]() {
              pTempTerritory = new CTerritory;
              pTempTerritory->SetName(grammar::SemanticPointer<char16_t>($4));
          };
          beginTerritory();
      }
      SYSTEM_MSG_ID '=' INTEGER RANGE '=' '{' n25 '}' NO_CALL_PC_END
      {
          g_RestrictedAreaDB.AddNpcPos(pTempTerritory);
          m_pGeoData->AddNoCallPCArea(pTempTerritory);
      }
    ;

no_change_pc
    : NO_CHANGE_PC_BEGIN NAME '=' NAME_2
      {
          const auto beginTerritory = [&]() {
              pTempTerritory = new CTerritory;
              pTempTerritory->SetName(grammar::SemanticPointer<char16_t>($4));
          };
          beginTerritory();
      }
      SYSTEM_MSG_ID '=' INTEGER RANGE '=' '{' n25 '}' NO_CHANGE_PC_END
      {
          g_RestrictedAreaDB.AddNpcPos(pTempTerritory);
          m_pGeoData->AddNoChangePCArea(pTempTerritory);
      }
    ;

no_drop_item
    : NO_DROP_ITEM_BEGIN NAME '=' NAME_2
      {
          const auto beginTerritory = [&]() {
              pTempTerritory = new CTerritory;
              pTempTerritory->SetName(grammar::SemanticPointer<char16_t>($4));
          };
          beginTerritory();
      }
      RANGE '=' '{' n25 '}' NO_DROP_ITEM_END
      {
          g_RestrictedAreaDB.AddNpcPos(pTempTerritory);
          m_pGeoData->AddNoDropItemArea(pTempTerritory);
      }
    ;

no_save_bookmark
    : NO_SAVE_BOOKMARK_BEGIN NAME '=' NAME_2
      {
          const auto beginTerritory = [&]() {
              pTempTerritory = new CTerritory;
              pTempTerritory->SetName(grammar::SemanticPointer<char16_t>($4));
          };
          beginTerritory();
      }
      RANGE '=' '{' n25 '}' NO_SAVE_BOOKMARK_END
      {
          g_RestrictedAreaDB.AddNpcPos(pTempTerritory);
          m_pGeoData->AddNoSaveBookmarkArea(pTempTerritory);
      }
    ;

no_use_bookmark
    : NO_USE_BOOKMARK_BEGIN NAME '=' NAME_2
      {
          const auto beginTerritory = [&]() {
              pTempTerritory = new CTerritory;
              pTempTerritory->SetName(grammar::SemanticPointer<char16_t>($4));
          };
          beginTerritory();
      }
      RANGE '=' '{' n25 '}' NO_USE_BOOKMARK_END
      {
          g_RestrictedAreaDB.AddNpcPos(pTempTerritory);
          m_pGeoData->AddNoUseBookmarkArea(pTempTerritory);
      }
    ;

transformable
    : TRANSFORMABLE_BEGIN NAME '=' NAME_2
      {
          const auto beginTerritory = [&]() {
              pTempTerritory = new CTerritory;
              pTempTerritory->SetName(grammar::SemanticPointer<char16_t>($4));
          };
          beginTerritory();
      }
      RANGE '=' '{' n25 '}' TRANSFORMABLE_END
      {
          g_RestrictedAreaDB.AddNpcPos(pTempTerritory);
          m_pGeoData->AddTransformableArea(pTempTerritory);
      }
    ;

server_block
    : SERVER_BLOCK_BEGIN NAME '=' NAME_2
      {
          const auto beginTerritory = [&]() {
              pTempTerritory = new CTerritory;
              pTempTerritory->SetName(grammar::SemanticPointer<char16_t>($4));
          };
          beginTerritory();
          m_pServerBlock = new CAxisAlgnBB;
      }
      MIN_KW '=' n27 MAX_KW '=' n27
      {
          m_pServerBlock->Set(
              *grammar::SemanticPointer<FVector>($8), *grammar::SemanticPointer<FVector>($11)
          );
      }
      SERVER_BLOCK_END
      {
          g_RestrictedAreaDB.AddNpcPos(pTempTerritory);
          g_WorldPlaneCollision.AddServerBlock(m_pServerBlock);
      }
    ;

territory
    : TERRITORY_BEGIN NAME_2
      {
          const auto beginTerritory = [&]() {
              pTempTerritory = new CTerritory;
              pTempTerritory->SetName(grammar::SemanticPointer<char16_t>($2));
          };
          beginTerritory();
      }
      '{' n25 '}' TERRITORY_END
      {
          g_RestrictedAreaDB.AddNpcPos(pTempTerritory);
          g_NpcPosDB.AddNpcPos(pTempTerritory);
      }
    ;

n25
    : n25 ';' n26
      { pTempTerritory->AddTerritoryPoint(grammar::SemanticPointer<FVector>($3)); }
    | n26
      { pTempTerritory->AddTerritoryPoint(grammar::SemanticPointer<FVector>($1)); }
    ;

n26
    : '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          const std::int64_t firstZ = grammar::SemanticInt64($6);
          const std::int64_t secondZ = grammar::SemanticInt64($8);
          if (secondZ <= firstZ) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"territory [%s] z1<z2",
              //     pTempTerritory->sName.c_str())
              // L2_LOG_ERROR(Log,
              //     L"territory [%s] z1<z2",
              //     pTempTerritory->sName.c_str())
              L2_LOG_ERROR(
                  Log,
                  "Restricted territory '%s' at script line %lld has invalid height bounds: z2 (%lld) must exceed z1 "
                  "(%lld); coordinate processing continues.",
                  Utf16ToUtf8(
                      std::u16string_view(
                          pTempTerritory->sName.c_str() != nullptr ? pTempTerritory->sName.c_str() : u"<null>"
                      )
                  )
                      .c_str(),
                  static_cast<long long>(m_lexer.line) + 1, static_cast<long long>(secondZ), static_cast<long long>(firstZ)
              );
          }
          if (firstZ < static_cast<std::int64_t>(pTempTerritory->m_Bottom)) {
              pTempTerritory->m_Bottom = grammar::SemanticInt32($6);
          }
          if (static_cast<std::int64_t>(pTempTerritory->m_Top) < secondZ) {
              pTempTerritory->m_Top = grammar::SemanticInt32($8);
          }
          auto* const point = new FVector(grammar::SemanticInt32($2), grammar::SemanticInt32($4), 0);
          $$ =
              grammar::PointerSemantic(point);
      }
    ;

n27
    : '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          auto* const point = new FVector(
              grammar::SemanticInt32($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6)
          );
          $$ =
              grammar::PointerSemantic(point);
      }
    ;

%%
