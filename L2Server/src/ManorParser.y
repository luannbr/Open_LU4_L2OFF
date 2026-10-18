// manordata.txt grammar (original L2Server/manor_parser.y).
// Bison generates ManorParser.cpp and ManorParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ManorParser.h"

#include "L2Server/src/Manor.h"
#include "L2Server/src/Object.h"
#include "L2Server/src/ObjectDB.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "ManorParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForManor::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ManorParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForManor::CParserForManor() : m_lexer() {
    m_pManor = nullptr;
}

CParserForManor::~CParserForManor() = default;

int CParserForManor::Create(char16_t* const fileBuffer, const int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token NAME
%token IDENTIFIER
%token INTEGER
%token EQUAL
%token MANOR_BEGIN
%token MANOR_ID
%token RESIDENCE_ID
%token MANOR_NAME
%token CROP_LIST
%token PROCURE_LIST
%token MANOR_END
%start manor_list

%%

manor_list
    : manor_list manor
    | manor
    ;

manor
    : MANOR_BEGIN manor_id_def_list MANOR_END
      {
          g_ManorDB.Add(m_pManor);
          m_pManor = nullptr;
      }
    ;

manor_id_def_list
    : manor_id_def_list manor_id_def
    | manor_id_def
    ;

manor_id_def
    : MANOR_ID EQUAL INTEGER
      {
          if (m_pManor == nullptr) {
              m_pManor = new CManor;
          }
          m_pManor->Set(MANOR_ID, grammar::SemanticInt32($3));
          nManorID = grammar::SemanticInt32($3);
      }
    | RESIDENCE_ID EQUAL INTEGER
      { m_pManor->Set(RESIDENCE_ID, grammar::SemanticInt32($3)); }
    | MANOR_NAME EQUAL NAME
      {
          m_pManor->Set(OBJECT_FIELD_MANOR_NAME, grammar::SemanticPointer<char16_t>($3));
      }
    | CROP_LIST EQUAL '{' n5 '}'
    | PROCURE_LIST EQUAL '{' n7 '}'
    ;

n5
    : n5 ';' n6
    | n6
    | %empty
    ;

n6
    : '{' IDENTIFIER ';' IDENTIFIER ';' IDENTIFIER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          m_pManor->AddCrop(
              g_ObjectDB.GetClassIdFromName(grammar::SemanticPointer<char16_t>($2), false),
              g_ObjectDB.GetClassIdFromName(grammar::SemanticPointer<char16_t>($4), false),
              g_ObjectDB.GetClassIdFromName(grammar::SemanticPointer<char16_t>($6), false),
              grammar::SemanticInt32($8), grammar::SemanticInt32($10), grammar::SemanticInt32($12)
          );
      }
    ;

n7
    : n7 ';' '{' IDENTIFIER ';' n8 '}'
      {
          auto* const procure = static_cast<ProcureStruct*>(std::malloc(sizeof(ProcureStruct)));
          procure->nCropId = g_ObjectDB.GetClassIdFromName(grammar::SemanticPointer<char16_t>($4), false);
          procure->m_pProcureList = grammar::SemanticPointer<std::vector<ProcureStructEach*>>($6);
          $$ = grammar::PointerSemantic(procure);
          m_pManor->Set(PROCURE_LIST, procure);
      }
    | '{' IDENTIFIER ';' n8 '}'
      {
          auto* const procure = static_cast<ProcureStruct*>(std::malloc(sizeof(ProcureStruct)));
          procure->nCropId = g_ObjectDB.GetClassIdFromName(grammar::SemanticPointer<char16_t>($2), false);
          procure->m_pProcureList = grammar::SemanticPointer<std::vector<ProcureStructEach*>>($4);
          $$ = grammar::PointerSemantic(procure);
          m_pManor->Set(PROCURE_LIST, procure);
      }
    | %empty
    ;

n8
    : n8 ';' n9
      {
          auto* const values =
              grammar::SemanticPointer<std::vector<ProcureStructEach*>>($$);
          values->push_back(grammar::SemanticPointer<ProcureStructEach>($3));
      }
    | n9
      {
          auto* const values = new std::vector<ProcureStructEach*>;
          values->push_back(grammar::SemanticPointer<ProcureStructEach>($1));
          $$ = grammar::PointerSemantic(values);
      }
    ;

n9
    : '{' INTEGER ';' n10 '}'
      {
          auto* const entry = static_cast<ProcureStructEach*>(std::malloc(sizeof(ProcureStructEach)));
          entry->nTypeId = grammar::SemanticInt32($2);
          entry->pProcureItemList = grammar::SemanticPointer<std::vector<int>>($4);
          $$ = grammar::PointerSemantic(entry);
      }
    ;

n10
    : n10 ';' IDENTIFIER
      {
          auto* const values = grammar::SemanticPointer<std::vector<int>>($1);
          values->push_back(g_ObjectDB.GetClassIdFromName(grammar::SemanticPointer<char16_t>($3), false));
          $$ = grammar::PointerSemantic(values);
      }
    | IDENTIFIER
      {
          auto* const values = new std::vector<int>;
          values->push_back(g_ObjectDB.GetClassIdFromName(grammar::SemanticPointer<char16_t>($1), false));
          $$ = grammar::PointerSemantic(values);
      }
    ;

%%
