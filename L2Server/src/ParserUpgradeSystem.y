// CParserForUpgradeSystem grammar.
// Bison generates ParserUpgradeSystem.cpp and ParserUpgradeSystemTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserUpgradeSystem.h"

#include "L2Server/src/ObjectDB.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string_view>

#include "ParserUpgradeSystemTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForUpgradeSystem::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserUpgradeSystemTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForUpgradeSystem::CParserForUpgradeSystem() : m_lexer(), m_tmpIntVector() {}

CParserForUpgradeSystem::~CParserForUpgradeSystem() = default;

int CParserForUpgradeSystem::Create(CObjectDB* objectDb, char16_t* fileBuffer, int fileSize) {
    m_pObjDB = objectDb;
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForUpgradeSystem::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"UpgradeSystemDataChecker[%d] : syntax error", m_lexer.line + 1);
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

%token UPGRADESYSTEM_BEGIN
%token UPGRADESYSTEM_END
%token UPGRADE_ID
%token UPGRADE_ITEM
%token MATERIAL_ITEMS
%token COMMISSION
%token RESULT_ITEM
%token INTEGER
// The original grammar has these conflicts. Bison resolves them as the
// original tables did. A shift wins over a reduce.
%expect 1
%start n1

%%

n1
    : n1 n2
    | n2
    ;

n2
    : upgradesystem upgrade_id_def_list UPGRADESYSTEM_END
      {
          m_pObjDB->m_mapUpgradeSystem.emplace(m_pUpgradeSystem->m_nID, m_pUpgradeSystem);
      }
    ;

upgradesystem
    : UPGRADESYSTEM_BEGIN
      { m_pUpgradeSystem = new CUpgradeSystem; }
    ;

upgrade_id_def_list
    : upgrade_id_def_list upgrade_id_def
    | upgrade_id_def
    ;

upgrade_id_def
    : UPGRADE_ID '=' INTEGER
      { m_pUpgradeSystem->SetID(grammar::SemanticInt32($3)); }
    | UPGRADE_ITEM '=' '{' INTEGER ';' INTEGER '}'
      {
          m_pUpgradeSystem->SetUpgradeItem(grammar::SemanticInt32($4), grammar::SemanticInt32($6));
      }
    | MATERIAL_ITEMS '=' '{' n6 '}'
    | COMMISSION '=' INTEGER
      { m_pUpgradeSystem->SetCommission(grammar::SemanticInt64($3)); }
    | RESULT_ITEM '=' '{' INTEGER ';' INTEGER '}'
      {
          m_pUpgradeSystem->SetResultItem(grammar::SemanticInt32($4), grammar::SemanticInt32($6));
      }
    ;

n6
    : n6 ';' n6
    | '{' INTEGER ';' INTEGER '}'
      {
          m_pUpgradeSystem->SetMaterialItem(grammar::SemanticInt32($2), grammar::SemanticInt64($4));
      }
    | %empty
    ;

// The original tables have rule 15 that no parser state reduces.
// It is left out.

%%
