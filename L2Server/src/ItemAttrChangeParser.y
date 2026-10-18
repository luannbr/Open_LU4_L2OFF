// CParserForItemAttrChange grammar (original L2Server/itemattrchange_parser.y).
// Bison generates ItemAttrChangeParser.cpp and ItemAttrChangeParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ItemAttrChangeParser.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ObjectDB.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "ItemAttrChangeParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForItemAttrChange::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ItemAttrChangeParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForItemAttrChange::CParserForItemAttrChange() : m_lexer() {}

CParserForItemAttrChange::~CParserForItemAttrChange() = default;

int CParserForItemAttrChange::create() {
    return m_lexer.create();
}

int CParserForItemAttrChange::FindItemClassId(const char16_t* name) {
    int classId = static_cast<int>(g_PrecompiledHeaderDB.Search(name));
    if (classId == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Undefined item name[%s] in line [%d]",
        //     hostName.c_str(),
        //     m_lexer.line + 1)
        // L2_LOG_ERROR(Log,
        //     L"Undefined item name[%s] in line [%d]",
        //     hostName.c_str(),
        //     m_lexer.line + 1)
        L2_LOG_ERROR(
            Log, "Undefined item '%s' in precompiled headers at script line %lld; returning class ID -1.",
            Utf16ToUtf8(std::u16string_view(name != nullptr ? name : u"<null>")).c_str(),
            static_cast<long long>(m_lexer.line) + 1
        );
    } else if (classId > 0) {
        classId = g_ObjectDB.GetClassIdFromName(name, false);
    }
    return classId;
}
}

%token EQUAL
%token ITEMATTRCHANGE_BEGIN
%token ITEMLIST
%token ITEMATTRCHANGE_END
%token NAME
%token INTEGER
%token IA_VALUE
%start n1

%%

n1
    : itemattrchange_list
    ;

itemattrchange_list
    : itemattrchange_list itemattrchange
    | %empty
    ;

itemattrchange
    : ITEMATTRCHANGE_BEGIN NAME INTEGER
      {
          m_pItemAttrChangeList = new CItemAttrChangeList;
          m_pItemAttrChangeList->m_nId = grammar::SemanticInt32($3);
          m_pItemAttrChangeStruct = new CItemAttrChangeStruct;
      }
      itemlist_def ITEMATTRCHANGE_END
      {
          g_ItemAttrChangeDB.Add(m_pItemAttrChangeList);
          m_pItemAttrChangeList = nullptr;
      }
    ;

itemlist_def
    : ITEMLIST EQUAL '{' n6 '}'
    ;

n6
    : n6 ';' '{' name_def '}'
    | '{' name_def '}'
      { m_pItemAttrChangeStruct = new CItemAttrChangeStruct; }
    ;

name_def
    : NAME ';' '{' n8 '}' ';' '{' n10 '}'
      {
          auto* const itemName = grammar::SemanticString($1);
          m_pItemAttrChangeStruct->m_nClassID = FindItemClassId(itemName);
          m_pItemAttrChangeList->AddList(m_pItemAttrChangeStruct);
          m_pItemAttrChangeStruct = new CItemAttrChangeStruct;
          std::free(itemName);
      }
    ;

n8
    : n8 ';' ia_value_def
    | ia_value_def
    ;

ia_value_def
    : IA_VALUE
      {
          m_pItemAttrChangeStruct->InputAttrTypeList.push_back(static_cast<AttributeTypeEnum>(grammar::SemanticInt32($1))
          );
      }
    ;

n10
    : n10 ';' ia_value_def_2
    | ia_value_def_2
    ;

ia_value_def_2
    : IA_VALUE
      {
          m_pItemAttrChangeStruct->OutputAttrTypeList.push_back(static_cast<AttributeTypeEnum>(grammar::SemanticInt32($1)
          ));
      }
    ;

%%
