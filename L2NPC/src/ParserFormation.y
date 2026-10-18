// FormationInfo.txt grammar (original L2NPC/parser_formation.y).
// Bison generates ParserFormation.cpp and ParserFormationTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2NPC/src/ParserFormation.h"

#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"
#include "TextRuntime.h"

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <string>

#include "ParserFormationTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForFormation::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, (void)(message), ++yyerrorcount, yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserFormationTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

void CopyFormationName(char16_t (&destination)[24], const char16_t* source) {
    if (!source) {
        destination[0] = u'\0';
        myInvalidParameterHandler(L"(L\"Invalid string pointer\" && 0)", L"wcscpy_s", nullptr, 0, 0);
        return;
    }

    const std::size_t length = std::char_traits<char16_t>::length(source);
    if (length >= std::size(destination)) {
        destination[0] = u'\0';
        myInvalidParameterHandler(L"(L\"Buffer is too small\" && 0)", L"wcscpy_s", nullptr, 0, 0);
        return;
    }
    std::copy_n(source, length + 1, destination);
}

} // namespace

CParserForFormation::CParserForFormation() {
    nFormationID = -1;
    nPosID = -1;
    nPrevPosID = -1;
    x = 0;
    y = 0;
    z = 0;
}

CParserForFormation::~CParserForFormation() = default;

int CParserForFormation::create() { return m_lexer.create(); }

void CParserForFormation::yysyntaxerror() {
    wchar_t message[60]{};
    std::swprintf(message, std::size(message), L"FormationDataChecker[%d] : syntax error", m_lexer.line + 1);
    // Log.Add(
    //     LOG_ERROR,
    //     message)
    // Log.Add(
    //     LOG_ERROR,
    //     "[formation] Line %lld: syntax error.",
    //     static_cast<long long>(m_lexer.line) + 1)
    // L2_LOG_ERROR(Log,
    //     "[formation] Line %lld: syntax error.",
    //     static_cast<long long>(m_lexer.line) + 1)
    L2_LOG_ERROR(Log, "[formation] FormationInfo.txt line %lld: syntax error.", static_cast<long long>(m_lexer.line) + 1);
    // Noise: The error is already logged above. Only Windows needs a modal copy.
    // PopUpWindow(message)
#if defined(_WIN32)
    PopUpWindow(L"%ls", message);
#endif
}
}

%token FORMATION_BEGIN FORMATION_END FORMATION_ID FORMATION_NAME
%token POS_BEGIN POS_END POS_ID POS_VAR
%token NAME INTEGER

%%

formation_list
    : formation_list formation
    | formation
    ;

formation
    : FORMATION_BEGIN formation_header position_list FORMATION_END
      {
          sData.size = runtime::AddInt32Wrapping(nPosID, 1);
          g_FormationManager.InsertFormationConstData(sData);
          nPosID = -1;
          nPrevPosID = -1;
      }
    ;

formation_header
    : FORMATION_ID '=' INTEGER FORMATION_NAME '=' NAME
      {
          nFormationID = grammar::SemanticInt32($3);
          // The compiled body reads the same slot again for the by-value
          // FormationConstData field instead of retaining nFormationID.
          sData.id = grammar::SemanticInt32($3);
          CopyFormationName(sData.name, grammar::SemanticString($6));
          // Noise: Name conversion served only the per-formation loading trace below.
          // const std::wstring hostName =
          //     Utf16ToWide(
          //             std::u16string_view(
          //                 sData.name))
          // Noise: Per-formation trace repeats script contents. Parse failures remain visible.
          // Log.Add(
          //     LOG_NORMAL,
          //     L"formation %d: [%s]",
          //     sData.id,
          //     hostName.c_str())
      }
    ;

position_list
    : position_list position
    | position
    ;

position
    : POS_BEGIN position_body POS_END
    ;

position_body
    : POS_ID '=' INTEGER POS_VAR '=' '{' INTEGER ',' INTEGER ',' INTEGER '}'
      {
          nPosID = grammar::SemanticInt32($3);
          const int expectedPositionID = runtime::AddInt32Wrapping(nPrevPosID, 1);
          if (expectedPositionID == nPosID && nPosID < 30) {
              nPrevPosID = nPosID;
              x = grammar::SemanticInt32($7);
              y = grammar::SemanticInt32($9);
              z = grammar::SemanticInt32($11);
              // Noise: Per-position coordinates repeat script contents during normal startup.
              // Log.Add(
              //     LOG_NORMAL,
              //     L"pos %d: [%d] [%d] [%d] Loaded, ",
              //     nPosID,
              //     x,
              //     y,
              //     z)
              sData.pos_var[nPosID] = FVector{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s] Invalid Pos ID while loading "
              //     L"FormationData. (prevPos[%d], nowPos[%d], "
              //     L"expectedPos[%d], formationID[%d])",
              //     L"parser_formation.y",
              //     nPrevPosID,
              //     nPosID,
              //     expectedPositionID,
              //     nFormationID)
              // Log.Add(
              //     LOG_ERROR,
              //     "[formation] Line %lld: position %d rejected for formation %d. Expected %d after %d, with position ID
              //     below 30.", static_cast<long long>(m_lexer.line) + 1, nPosID, nFormationID, expectedPositionID,
              //     nPrevPosID)
              L2_LOG_ERROR(
                  Log,
                  "[formation] Line %lld: position %d rejected for formation %d; expected %d after %d, with position ID "
                  "below 30.",
                  static_cast<long long>(m_lexer.line) + 1, nPosID, nFormationID, expectedPositionID, nPrevPosID
              );
          }
      }
    ;

%%
