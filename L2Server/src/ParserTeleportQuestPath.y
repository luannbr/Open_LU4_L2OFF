// teleportquestpath.txt grammar.
// Bison generates ParserTeleportQuestPath.cpp and ParserTeleportQuestPathTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserTeleportQuestPath.h"

#include "L2Server/src/QuestManager.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string_view>

#include "ParserTeleportQuestPathTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForTeleportQuestPath::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserTeleportQuestPathTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForTeleportQuestPath::CParserForTeleportQuestPath() : m_telPositionList(), m_lexer() {
    m_nNpcID = 0;
    m_nNpcLocZoneID = 0;
}

CParserForTeleportQuestPath::~CParserForTeleportQuestPath() = default;

int CParserForTeleportQuestPath::Create() {
    return m_lexer.Create();
}

void CParserForTeleportQuestPath::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"TelQuestPath[%d] : syntax error", m_lexer.line + 1);
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
%token TELEPORT_QUEST_PATH_BEGIN
%token TELEPORT_QUEST_PATH_END
%token NPC_ID
%token NPC_LOC_ZONEID
%token TELPOSLIST
%start teleport_quest_path_list

%%

teleport_quest_path_list
    : teleport_quest_path_list teleport_quest_path
    | teleport_quest_path
    ;

teleport_quest_path
    : TELEPORT_QUEST_PATH_BEGIN npc_id_def TELEPORT_QUEST_PATH_END
      {
          QuestManager& manager = Singleton<QuestManager>::inst();
          manager.m_mapTelPath.emplace(m_nNpcID, m_telPositionList);
          m_telPositionList.clear();
          m_nNpcID = 0;
      }
    ;

npc_id_def
    : NPC_ID '=' INTEGER NPC_LOC_ZONEID '=' INTEGER TELPOSLIST '=' '{' n4 '}'
      {
          m_nNpcID = grammar::SemanticInt32($3);
          m_nNpcLocZoneID = grammar::SemanticInt32($6);
          QuestManager& manager = Singleton<QuestManager>::inst();
          manager.m_mmmapNpcZone.emplace(m_nNpcLocZoneID, m_nNpcID);
      }
    ;

n4
    : n4 ';' n5
    | n5
    ;

n5
    : '{' INTEGER ';' INTEGER '}'
      { m_telPositionList.emplace(grammar::SemanticInt32($2), grammar::SemanticInt32($4)); }
    ;

%%
