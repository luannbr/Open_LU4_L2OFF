// superpointinfo.txt grammar (original L2NPC/parser_superpointinfo.y).
// Bison generates ParserSuperPointInfo.cpp and ParserSuperPointInfoTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2NPC/src/ParserSuperPointInfo.h"

#include "L2NPC/src/SuperPoint.h"
#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "TextRuntime.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <string_view>

#include "ParserSuperPointInfoTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse l2geo::CParserForSuperPointInfo::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserSuperPointInfoTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace l2geo {

CParserForSuperPointInfo::CParserForSuperPointInfo() {
    // Current 0x0054d91c leaves classType and pInfo untouched until their
    // grammar actions assign them.
    class_id = 0;
    nodeid = 0;
}

CParserForSuperPointInfo::~CParserForSuperPointInfo() = default;

int CParserForSuperPointInfo::create() { return m_lexer.create(); }

} // namespace l2geo
}

%token NAME INTEGER
%token SUPERPOINT_BEGIN SUPERPOINT_END SUPERPOINT_NAME
%token NPC_BEGIN NPC_END NPC_NAME MOVE_TYPE
%token NODE_BEGIN NODE_END NODE_ID FSTRING_INDEX SOCIAL_NUMBER DELAY

%%

superpoint_list
    : superpoint_list superpoint
    | superpoint
    ;

superpoint
    : superpoint_begin superpoint_name npc_list superpoint_end
    ;

superpoint_begin
    : SUPERPOINT_BEGIN
      { pInfo = new (std::nothrow) CSuperPointInfo; }
    ;

superpoint_name
    : SUPERPOINT_NAME '=' NAME
      {
          if (pInfo) {
              pInfo->SetSuperpointName(grammar::SemanticString($3));
          }
      }
    ;

superpoint_end
    : SUPERPOINT_END
      {
          if (!CSuperPointInfoManager::Inst().AddSuperPointInfo(pInfo)) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d] : superpoint duplicated!! npc[%d]",
              //     L"..\\Script\\superpointinfo.txt",
              //     m_lexer.line + 1,
              //     class_id)
              // Log.Add(
              //     LOG_ERROR,
              //     "[NPC route] superpointinfo.txt line %lld: duplicate SuperPoint settings rejected (last NPC class ID
              //     %d), the existing entry is retained.", static_cast<long long>(m_lexer.line) + 1, class_id)
              L2_LOG_ERROR(
                  Log,
                  "[NPC route] superpointinfo.txt line %lld: duplicate SuperPoint settings rejected (last NPC class ID "
                  "%d); the existing entry is retained.",
                  static_cast<long long>(m_lexer.line) + 1, class_id
              );
          }
          pInfo = nullptr;
      }
    ;

npc_list
    : npc_list npc
    | npc
    ;

npc
    : NPC_BEGIN NPC_NAME '=' NAME MOVE_TYPE '=' NAME
      {
          class_id = static_cast<int>(g_PrecompiledHeaderDB.Search(grammar::SemanticString($4)));
          const int moveType = static_cast<int>(g_PrecompiledHeaderDB.Search(grammar::SemanticString($7)));
          if (moveType == -1) {
              const std::string printable = Utf16ToUtf8(std::u16string_view(grammar::SemanticString($7)));
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d] : Undefined MOVE_TYPE[%s]",
              //     L"..\\Script\\superpointinfo.txt",
              //     m_lexer.line + 1,
              //     printable.c_str())
              // Log.Add(
              //     LOG_ERROR,
              //     "[NPC route] superpointinfo.txt line %lld: unknown movement type \"%s\" for NPC class ID %d.
              //     continuing with movement type -1.", static_cast<long long>(m_lexer.line) + 1,
              //     WideToUtf8(printable).c_str(), class_id)
              L2_LOG_ERROR(
                  Log,
                  "[NPC route] superpointinfo.txt line %lld: unknown movement type \"%s\" for NPC class ID %d; continuing "
                  "with movement type -1.",
                  static_cast<long long>(m_lexer.line) + 1, printable.c_str(), class_id
              );
          }
          pInfo->SetMoveType(class_id, moveType);
      }
      node_list NPC_END
    ;

node_list
    : node_list node
    | node
    ;

node
    : NODE_BEGIN NODE_ID '=' INTEGER FSTRING_INDEX '=' INTEGER SOCIAL_NUMBER '=' INTEGER DELAY '=' INTEGER
      {
          nodeid = grammar::SemanticInt32($4);
          pInfo->SetFStringIndex(class_id, nodeid, grammar::SemanticInt32($7));
          pInfo->SetSocialNumber(class_id, nodeid, grammar::SemanticInt32($10));
          pInfo->SetDelay(class_id, nodeid, grammar::SemanticInt32($13));
      }
      NODE_END
    ;

%%
