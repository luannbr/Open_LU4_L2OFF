// CParserForExDrop grammar.
// Bison generates ParserExDrop.cpp and ParserExDropTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserExDrop.h"

#include "L2Server/src/ExDrop.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string_view>

#include "ParserExDropTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForExDrop::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserExDropTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForExDrop::CParserForExDrop() : m_lexer(), m_nLineNumber(0), m_bOnlyPrecompiledHeader(false), m_bReload(false) {
}

CParserForExDrop::~CParserForExDrop() = default;

int CParserForExDrop::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(this, fileBuffer, fileSize);
}

void CParserForExDrop::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"ExDropItem Checker[%d] : syntax error", m_lexer.getLine() + 1);
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

%token EQUAL
%token EX_DROP_BEGIN
%token EX_DROP_ID
%token EX_DROP_END
%token ITEM_ID
%token COUNT
%token PROBABLITY
%token PC_LEVEL
%token CONDITION
%token COUNT_ID
%token ADDITIONAL_PROB
%token LESS_GREATER
%token GREATER
%token GREATER_EQUAL
%token NAME
%token INTEGER
%token FLOAT_NUMBER
%start n1

%%

n1
    : ex_drop_list
    ;

ex_drop_list
    : ex_drop_list ex_drop
    | %empty
    ;

ex_drop
    : EX_DROP_BEGIN EX_DROP_ID EQUAL INTEGER
      { m_pExDropData = new ExDropItemData(grammar::SemanticInt32($4)); }
      item_id_def_list EX_DROP_END
      {
          ExDropItemSystem::inst().exDropDataMap.emplace(m_pExDropData->getId(), m_pExDropData);
          ++m_nLineNumber;
      }
    ;

item_id_def_list
    : item_id_def_list item_id_def
    | %empty
    ;

item_id_def
    : ITEM_ID EQUAL INTEGER
      { m_pExDropData->setItemId(grammar::SemanticInt32($3)); }
    | COUNT EQUAL '{' INTEGER ';' INTEGER '}'
      {
          m_pExDropData->setGiveCount(grammar::SemanticInt32($4));
          m_pExDropData->setMaxCount(grammar::SemanticInt32($6));
      }
    | PROBABLITY EQUAL '{' FLOAT_NUMBER '}'
      {
          m_pExDropData->setProb(runtime::Win64CvttDoubleToInt32(grammar::SemanticDouble($4)));
      }
    | PC_LEVEL EQUAL '{' INTEGER ';' INTEGER '}'
      { m_pExDropData->setPcLevelRange(grammar::SemanticInt32($4), grammar::SemanticInt32($6)); }
    | CONDITION EQUAL '{' n7 '}'
      {
          m_pExDropData->setCategory(grammar::SemanticPointer<std::vector<int>>($4));
      }
    | COUNT_ID EQUAL INTEGER
      { m_pExDropData->setCountId(grammar::SemanticInt32($3)); }
    | ADDITIONAL_PROB EQUAL '{' FLOAT_NUMBER '}'
      { m_pExDropData->setAdditionalProb(grammar::SemanticDouble($4)); }
    ;

n7
    : n7 ';' INTEGER
      {
          std::vector<int>* const values = grammar::SemanticPointer<std::vector<int>>($1);
          values->push_back(grammar::SemanticInt32($3));
          $$ = $1;
      }
    | INTEGER
      {
          auto* const values = new std::vector<int>;
          $$ =
              grammar::PointerSemantic(values);
          values->push_back(grammar::SemanticInt32($1));
      }
    ;

%%
