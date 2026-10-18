// CParserForFlyMove grammar (original L2Server/parser_flymove.y).
// Bison generates ParserFlyMove.cpp and ParserFlyMoveTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserFlyMove.h"

#include "L2Server/src/Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <new>
#include <string_view>

#include "ParserFlyMoveTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForFlyMove::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserFlyMoveTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForFlyMove::CParserForFlyMove() : m_Pos(), m_lexer() {}

CParserForFlyMove::~CParserForFlyMove() = default;

int CParserForFlyMove::create() {
    return m_lexer.create();
}

void CParserForFlyMove::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"FlyMove[%d] : syntax error", m_lexer.line + 1);
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
%token NAME
%token FLYMOVE_DATA_BEGIN
%token FLYMOVE_DATA_END
%token FLYMOVE_PATH_ID
%token NODE_LIST_BEGIN
%token NODE_LIST_END
%token ID
%token POS
%token LANDING_POS
%token RELATED_NODE
%token FLYING_HEIGHT
%start flymove_data

%%

flymove_data
    : FLYMOVE_DATA_BEGIN flymove_path_id_def_list FLYMOVE_DATA_END
      {
          // Log.Add(LOG_NORMAL, L"flymove data input ended")
          L2_LOG_INFO(Log, u"flymove data input ended");
      }
    ;

flymove_path_id_def_list
    : flymove_path_id_def_list flymove_path_id_def
    | flymove_path_id_def
    ;

flymove_path_id_def
    : FLYMOVE_PATH_ID '=' INTEGER
      {
          const int pathId = grammar::SemanticInt32($3);
          CFlyMovePath* const path = new (std::nothrow) CFlyMovePath(pathId);
          if (path == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"path making failed! id [%d]",
              //     pathId)
              L2_LOG_ERROR(Log, u"path making failed! id [%d]", pathId);
          }
          m_path = path;
          m_nNodeCount = 0;
      }
      NODE_LIST_BEGIN id_def_list NODE_LIST_END
      { CFlyMoveManager::Inst().InsertPath(m_path); }
    ;

id_def_list
    : id_def_list id_def
    | id_def
    ;

id_def
    : ID '=' INTEGER FLYING_HEIGHT '=' INTEGER POS '=' n12
      {
          const int nodeId = grammar::SemanticInt32($3);
          if (m_nNodeCount != nodeId) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"flymove node count invalid! id [%d]",
              //     nodeId)
              L2_LOG_ERROR(Log, u"flymove node count invalid! id [%d]", nodeId);
          }
          CFlyMovePathNode* const node = new (std::nothrow) CFlyMovePathNode(nodeId);
          if (node == nullptr) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"node making failed! id [%d]",
              //     nodeId)
              L2_LOG_ERROR(Log, u"node making failed! id [%d]", nodeId);
              break;
          }
          m_node = node;
          m_node->SetPos(m_Pos);
          m_path->InsertNode(m_node);
          m_node->SetFlyingHeight(grammar::SemanticInt32($6));
          ++m_nNodeCount;
      }
      LANDING_POS '=' n12
      {
          if (!m_node->IsNonLanding()) {
              m_node->SetLandingPos(m_Pos);
          }
      }
      RELATED_NODE '=' n9
    ;

n9
    : '{' n10 '}'
    ;

n10
    : n10 ';' INTEGER
    | INTEGER
      { m_node->InsertRelatedNode(grammar::SemanticInt32($1)); }
    | %empty
    ;

n12
    : '{' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          const FVector position(
              grammar::SemanticInt32($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6)
          );
          m_Pos = position;
          if (static_cast<int>(m_Pos.x) == 0 && static_cast<int>(m_Pos.y) == 0 && static_cast<int>(m_Pos.z) == 0) {
              m_node->SetNonLanding();
          }
      }
    ;

// The original tables have rules 15 and 16 that no parser state reduces.
// They are left out.

%%
