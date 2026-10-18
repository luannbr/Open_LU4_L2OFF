// convertdata.txt grammar (original L2Server/parser_convertdata.y).
// Bison generates ParserConvertData.cpp and ParserConvertDataTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserConvertData.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/Item.h"
#include "L2Server/src/ObjectDB.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

#include "ParserConvertDataTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForConvertData::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserConvertDataTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForConvertData::CParserForConvertData() : m_nInput(0), m_nOutput(0), m_lexer() {}

CParserForConvertData::~CParserForConvertData() = default;

int CParserForConvertData::Create() {
    return m_lexer.Create();
}

void CParserForConvertData::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"ConvertDataChecker[%d] : syntax error", m_lexer.line + 1);
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

%token CONVERT_BEGIN
%token CONVERT_END
%token INPUT_ITEM
%token OUTPUT_ITEM
%token NAME
%start n1

%%

n1
    : convert_list
    ;

convert_list
    : convert_list convert
    | %empty
    ;

convert
    : CONVERT_BEGIN
      {
          m_nInput = 0;
          m_nOutput = 0;
      }
      input_item_def_list CONVERT_END
      { g_ConvertItemDB.AddItem(m_nInput, m_nOutput); }
    ;

input_item_def_list
    : input_item_def_list input_item_def
    | %empty
    ;

input_item_def
    : INPUT_ITEM '=' NAME
      {
          const char16_t* const itemName = grammar::SemanticString($3);
          const int classId = g_ObjectDB.GetClassIdFromName(itemName, false);
          m_nInput = classId;
          if (classId == -1) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[conversion parser] Item '%s' is undefined at line %d; "
                  u"class ID -1 is retained.",
                  hostName.c_str(), m_lexer.line + 1
              );
          }
      }
    | OUTPUT_ITEM '=' NAME
      {
          const char16_t* const itemName = grammar::SemanticString($3);
          const int classId = g_ObjectDB.GetClassIdFromName(itemName, false);
          m_nOutput = classId;
          if (classId == -1) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(itemName);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[conversion parser] Item '%s' is undefined at line %d; "
                  u"class ID -1 is retained.",
                  hostName.c_str(), m_lexer.line + 1
              );
          }
      }
    ;

%%
