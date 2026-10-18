// CParserForDecoData grammar (original L2Server/decodata_parser.y).
// Bison generates DecoDataParser.cpp and DecoDataParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/DecoDataParser.h"

#include "L2Server/src/AgitDecoDb.h"

#include "Shared/src/SharedType.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "DecoDataParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForDecoData::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"DecoDataParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

std::uint32_t SemanticUInt32(grammar::SemanticBits value) { return static_cast<std::uint32_t>(value); }

} // namespace

CParserForDecoData::CParserForDecoData() :
    m_pStringVector(nullptr), m_pString(nullptr), m_pResidenceDeco(nullptr), m_lexer() {
}

CParserForDecoData::~CParserForDecoData() = default;

int CParserForDecoData::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token EQUAL
%token COLON
%token LPAREN
%token RPAREN
%token NONE
%token DECO_BEGIN
%token DECO_END
%token ID
%token NAME
%token TYPE
%token LEVEL
%token DEPTH
%token FUNC
%token COST
%token NAME_2
%token INTEGER
%token HP_REGEN_GROUP
%token FLOAT_NUMBER
%start n1

%%

n1
    : deco_list
    ;

deco_list
    : deco_list deco
    | %empty
    ;

deco
    : DECO_BEGIN
      { m_pResidenceDeco = new CResidenceDeco; }
      id_def_list DECO_END
      {
          if (m_pResidenceDeco == nullptr) {
              break;
          }
          CResidenceDecoDb::Inst().AddResidenceDeco(m_pResidenceDeco);
          if (m_pResidenceDeco->m_nType > 0 && m_pResidenceDeco->m_nType <= 32 && m_pResidenceDeco->m_nLevel > 0 &&
              m_pResidenceDeco->m_nLevel <= 32) {
              const auto& cost = m_pResidenceDeco->m_lstCost.front();
              auto& dayFee =
                  CSharedAgitDecoInfo::Inst()->m_DayFee[static_cast<std::size_t>(m_pResidenceDeco->m_nType - 1)]
                                                       [static_cast<std::size_t>(m_pResidenceDeco->m_nLevel - 1)];
              dayFee.nDay = static_cast<int>(cost.first);
              dayFee.nFee = cost.second;
          }
          m_pResidenceDeco = nullptr;
      }
    ;

id_def_list
    : id_def_list id_def
    | %empty
    ;

id_def
    : ID EQUAL INTEGER
      {
          if (m_pResidenceDeco != nullptr) {
              m_pResidenceDeco->m_nDecoId = grammar::SemanticInt32($3);
          }
      }
    | NAME EQUAL NAME_2
      {
          if (m_pResidenceDeco != nullptr) {
              const auto* const source =
                  grammar::SemanticString($3);
              if (source != nullptr) {
                  std::size_t length = 0;
                  while (source[length] != u'\0') {
                      ++length;
                  }
                  if (length <= 0xff) {
                      std::copy_n(source, length + 1, m_pResidenceDeco->m_sDecoName);
                  }
              }
          }
      }
    | TYPE EQUAL INTEGER
      {
          if (m_pResidenceDeco != nullptr) {
              m_pResidenceDeco->m_nType = grammar::SemanticInt32($3);
          }
      }
    | LEVEL EQUAL INTEGER
      {
          if (m_pResidenceDeco != nullptr) {
              m_pResidenceDeco->m_nLevel = grammar::SemanticInt32($3);
          }
      }
    | DEPTH EQUAL INTEGER
      {
          if (m_pResidenceDeco != nullptr) {
              m_pResidenceDeco->m_nDepth = grammar::SemanticInt32($3);
          }
      }
    | func_def
    | COST EQUAL n8
    ;

func_def
    : FUNC EQUAL HP_REGEN_GROUP LPAREN FLOAT_NUMBER RPAREN
      {
          if (m_pResidenceDeco != nullptr) {
              m_pResidenceDeco->m_nFunction = grammar::SemanticInt32($3);
              m_pResidenceDeco->m_dArgument = grammar::SemanticDouble($5);
          }
      }
    | FUNC EQUAL HP_REGEN_GROUP LPAREN INTEGER RPAREN
      {
          if (m_pResidenceDeco != nullptr) {
              m_pResidenceDeco->m_nFunction = grammar::SemanticInt32($3);
              m_pResidenceDeco->m_dArgument = static_cast<double>(grammar::SemanticInt32($5));
          }
      }
    | FUNC EQUAL HP_REGEN_GROUP LPAREN RPAREN
      {
          if (m_pResidenceDeco != nullptr) {
              m_pResidenceDeco->m_nFunction = grammar::SemanticInt32($3);
              m_pResidenceDeco->m_dArgument = 0.0;
          }
      }
    | FUNC EQUAL NONE
      {
          if (m_pResidenceDeco != nullptr) {
              m_pResidenceDeco->m_nFunction = 0;
              m_pResidenceDeco->m_dArgument = 0.0;
          }
      }
    ;

n8
    : '{' n9 '}'
    | '{' '}'
    ;

n9
    : n9 ';' integer_def
    | integer_def
    ;

integer_def
    : INTEGER COLON INTEGER
      {
          if (m_pResidenceDeco != nullptr) {
              m_pResidenceDeco->m_lstCost.push_back(
                  CResidenceDeco::ResidenceDecoCost{
                      SemanticUInt32($1), static_cast<std::int64_t>(SemanticUInt32($3))}
              );
          }
      }
    ;

%%
