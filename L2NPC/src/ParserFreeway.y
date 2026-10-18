// freewayInfo.txt grammar (original L2NPC/parser_freeway.y).
// Bison generates ParserFreeway.cpp and ParserFreewayTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2NPC/src/ParserFreeway.h"

#include "L2NPC/src/Freeway.h"
#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <new>
#include <string_view>

#include "ParserFreewayTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForFreeway::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, (void)(message), ++yyerrorcount, yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserFreewayTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
CParserForFreeway::CParserForFreeway() {
    freewayID = -1;
    nodeID = -1;
    prevNodeID = -1;
    x = 0;
    y = 0;
    z = 0;
    delay = 0;
    social = -1;
    state = 0;
    // Current 0x005493c8 leaves freeway and node untouched until their
    // matching semantic actions run.
}

CParserForFreeway::~CParserForFreeway() = default;

int CParserForFreeway::create() { return m_lexer.create(); }

void CParserForFreeway::yysyntaxerror() {
    wchar_t message[60]{};
    std::swprintf(message, std::size(message), L"FreewayDataChecker[%d] : syntax error", m_lexer.line + 1);
    // Log.Add(
    //     LOG_ERROR,
    //     message)
    // Log.Add(
    //     LOG_ERROR,
    //     "[NPC route] freewayInfo.txt line %lld: syntax error.",
    //     static_cast<long long>(m_lexer.line) + 1)
    L2_LOG_ERROR(Log, "[NPC route] freewayInfo.txt line %lld: syntax error.", static_cast<long long>(m_lexer.line) + 1);
    // Noise: The error is already logged above. Only Windows needs a modal copy.
    // PopUpWindow(message)
#if defined(_WIN32)
    PopUpWindow(L"%ls", message);
#endif
}
}

%token FREEWAY_BEGIN FREEWAY_END FREEWAY_ID FREEWAY_NAME
%token NODE_BEGIN NODE_END NODE_ID POS DELAY SOCIAL STATE
%token NAME INTEGER

%%

freeway_list
    : freeway_list freeway
    | freeway
    ;

freeway
    : FREEWAY_BEGIN freeway_header node_list FREEWAY_END
      {
          // Current 0x005496bc checks the ID before publishing the pointer.
          if (freewayID >= 0) {
              g_FreewayManager.AddFreeway(freewayID, freeway);
          }
      }
    ;

freeway_header
    : FREEWAY_ID '=' INTEGER FREEWAY_NAME '=' NAME
      {
          freewayID = grammar::SemanticInt32($3);
          freeway = new (std::nothrow) CFreeway(freewayID, grammar::SemanticString($6));
          prevNodeID = -1;
      }
    ;

node_list
    : node_list node
    | node
    ;

node
    : NODE_BEGIN node_body NODE_END
    ;

node_body
    : NODE_ID '=' INTEGER POS '=' '{' INTEGER ',' INTEGER ',' INTEGER '}' DELAY '=' INTEGER SOCIAL '=' INTEGER STATE '='
      INTEGER
      {
          nodeID = grammar::SemanticInt32($3);
          const int expectedNodeID = prevNodeID + 1;
          if (expectedNodeID == nodeID) {
              prevNodeID = nodeID;
              x = grammar::SemanticInt32($7);
              y = grammar::SemanticInt32($9);
              z = grammar::SemanticInt32($11);
              delay = grammar::SemanticInt32($15);
              social = grammar::SemanticInt32($18);
              state = grammar::SemanticInt32($21);
              node = new (std::nothrow) CFreewayNode(nodeID, x, y, z, delay, social, state);
              freeway->AddNode(node);
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s] Invalid NODE ID while "
              //     L"loading FreewayData. "
              //     L"(prevNode[%d], nowNode[%d], "
              //     L"expectedNode[%d], freewayID[%d])",
              //     L"parser_freeway.y",
              //     prevNodeID,
              //     nodeID,
              //     expectedNodeID,
              //     freewayID)
              // Log.Add(
              //     LOG_ERROR,
              //     "[NPC route] freewayInfo.txt line %lld: node %d rejected for freeway %d. Expected %d after %d.",
              //     static_cast<long long>(m_lexer.line) + 1, nodeID, freewayID, expectedNodeID, prevNodeID)
              L2_LOG_ERROR(
                  Log, "[NPC route] freewayInfo.txt line %lld: node %d rejected for freeway %d; expected %d after %d.",
                  static_cast<long long>(m_lexer.line) + 1, nodeID, freewayID, expectedNodeID, prevNodeID
              );
          }
      }
    ;

%%
