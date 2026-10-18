// CParserForMatchingPoint grammar (original L2Server/parser_matchingpoint.y).
// Bison generates ParserMatchingPoint.cpp and ParserMatchingPointTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserMatchingPoint.h"

#include "L2Server/src/LexerMatchingPoint.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/MatchingPointDB.h"

#include "Shared/src/Log.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "ParserMatchingPointTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForMatchingPoint::yyparse
#define yylex(value) m_pLexer->Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_pLexer->yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserMatchingPointTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

bool EqualsAsciiCaseInsensitive(const char16_t* value, const std::u16string_view expected) {
    if (value == nullptr) {
        return false;
    }
    std::size_t index = 0;
    while (value[index] != u'\0' && index < expected.size()) {
        char16_t left = value[index];
        char16_t right = expected[index];
        if (left >= u'A' && left <= u'Z') {
            left += u'a' - u'A';
        }
        if (right >= u'A' && right <= u'Z') {
            right += u'a' - u'A';
        }
        if (left != right) {
            return false;
        }
        ++index;
    }
    return value[index] == u'\0' && index == expected.size();
}

} // namespace

CParserForMatchingPoint::CParserForMatchingPoint() : m_nGameType(static_cast<MatchingPoint::MATCHINGPOINT_TYPE>(-1)) {
}

CParserForMatchingPoint::~CParserForMatchingPoint() = default;

int CParserForMatchingPoint::Create(CLexerForMatchingPoint* const lexer) {
    m_pLexer = lexer;
    return 1;
}
}

%token INTEGER
%token IDENTIFIER
%token FLOAT_NUMBER
%token LBRACKET
%token RBRACKET
%token EQUAL
%token SEMICOLON
%start n1

%%

n1
    : n2
    ;

n2
    : n2 n3
    | n3
    ;

n3
    : lbracket_def n5
    ;

lbracket_def
    : LBRACKET IDENTIFIER RBRACKET
      {
          char16_t* const name = grammar::SemanticString($2);
          if (EqualsAsciiCaseInsensitive(name, u"PledgeGame_RatingPoint")) {
              m_nGameType = MatchingPoint::MPT_BASE;
          } else if (EqualsAsciiCaseInsensitive(name, u"Olympiad_RatingPoint")) {
              m_nGameType = MatchingPoint::MPT_OLYMPIAD;
          }
          if (m_nGameType >= MatchingPoint::MPT_BASE && m_nGameType < MatchingPoint::MPT_MAX) {
              break;
          }
          // Log.Add(
          //     LOG_ERROR,
          //     L"[%s][%d] invalid game type: %s",
          //     L"parser_matchingpoint.y",
          //     0x3c,
          //     hostName.c_str())
          // L2_LOG_ERROR(Log,
          //     L"[%s][%d] invalid game type: %s",
          //     L"parser_matchingpoint.y",
          //     0x3c,
          //     hostName.c_str())
          L2_LOG_ERROR(
              Log, "Unknown matching-point section '%s'; current game type %d is invalid and remains unchanged.",
              Utf16ToUtf8(std::u16string_view(name != nullptr ? name : u"<null>")).c_str(), static_cast<int>(m_nGameType)
          );
      }
    ;

n5
    : n5 n6
    | n6
    ;

n6
    : identifier_def
    | integer_def
    | identifier_def_2
    | identifier_def_3
    ;

identifier_def
    : IDENTIFIER EQUAL INTEGER
      {
          if (m_nGameType < MatchingPoint::MPT_BASE || m_nGameType >= MatchingPoint::MPT_MAX) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s][%d] section name is not specified",
              //     L"parser_matchingpoint.y",
              //     0x48)
              // L2_LOG_ERROR(Log,
              //     L"[%s][%d] section name is not specified",
              //     L"parser_matchingpoint.y",
              //     0x48)
              L2_LOG_ERROR(
                  Log,
                  "Cannot apply integer matching-point setting: no valid section is active (game type %d); the setting is "
                  "skipped.",
                  static_cast<int>(m_nGameType)
              );
              break;
          }
          char16_t* const key = grammar::SemanticString($1);
          const std::int64_t value = grammar::SemanticInt64($3);
          if (m_nGameType == MatchingPoint::MPT_OLYMPIAD) {
              if (EqualsAsciiCaseInsensitive(key, u"PointMin")) {
                  g_MatchingPointDB.m_Olympiad.nPointMin = value;
              } else if (EqualsAsciiCaseInsensitive(key, u"PointMax")) {
                  g_MatchingPointDB.m_Olympiad.nPointMax = value;
              } else if (EqualsAsciiCaseInsensitive(key, u"Grade0")) {
                  g_MatchingPointDB.m_Olympiad.anGrades[0] = value;
              } else if (EqualsAsciiCaseInsensitive(key, u"Grade1")) {
                  g_MatchingPointDB.m_Olympiad.anGrades[1] = value;
              } else if (EqualsAsciiCaseInsensitive(key, u"Grade2")) {
                  g_MatchingPointDB.m_Olympiad.anGrades[2] = value;
              } else if (EqualsAsciiCaseInsensitive(key, u"Grade3")) {
                  g_MatchingPointDB.m_Olympiad.anGrades[3] = value;
              } else if (EqualsAsciiCaseInsensitive(key, u"K0")) {
                  g_MatchingPointDB.m_Olympiad.anKs[0] = static_cast<int>(value);
              } else if (EqualsAsciiCaseInsensitive(key, u"K1")) {
                  g_MatchingPointDB.m_Olympiad.anKs[1] = static_cast<int>(value);
              } else if (EqualsAsciiCaseInsensitive(key, u"K2")) {
                  g_MatchingPointDB.m_Olympiad.anKs[2] = static_cast<int>(value);
              } else if (EqualsAsciiCaseInsensitive(key, u"K3")) {
                  g_MatchingPointDB.m_Olympiad.anKs[3] = static_cast<int>(value);
              } else {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"[%s][%d] invalid key name:%s, %d",
                  //     L"parser_matchingpoint.y",
                  //     0x80,
                  //     hostKey.c_str(),
                  //     static_cast<int>(m_nGameType))
                  // L2_LOG_ERROR(Log,
                  //     L"[%s][%d] invalid key name:%s, %d",
                  //     L"parser_matchingpoint.y",
                  //     0x80,
                  //     hostKey.c_str(),
                  //     static_cast<int>(m_nGameType))
                  L2_LOG_ERROR(
                      Log, "Unknown Olympiad integer setting '%s' with value %lld (game type %d); the setting is skipped.",
                      Utf16ToUtf8(std::u16string_view(key != nullptr ? key : u"<null>")).c_str(),
                      static_cast<long long>(value), static_cast<int>(m_nGameType)
                  );
              }
              break;
          }
          const int type = static_cast<int>(m_nGameType);
          if (EqualsAsciiCaseInsensitive(key, u"PvPRatingAddConstant")) {
              g_MatchingPointDB.m_nConstK[type] = value;
              break;
          }
          if (EqualsAsciiCaseInsensitive(key, u"RateMax")) {
              g_MatchingPointDB.m_nMatchingPointMax[type] = value;
              break;
          }
          if (EqualsAsciiCaseInsensitive(key, u"RateMin")) {
              g_MatchingPointDB.m_nMatchingPointMin[type] = value;
              break;
          }
          // Log.Add(
          //     LOG_ERROR,
          //     L"[%s][%d] invalid key name:%s",
          //     L"parser_matchingpoint.y",
          //     0x96,
          //     hostKey.c_str())
          // L2_LOG_ERROR(Log,
          //     L"[%s][%d] invalid key name:%s",
          //     L"parser_matchingpoint.y",
          //     0x96,
          //     hostKey.c_str())
          L2_LOG_ERROR(
              Log, "Unknown matching-point integer setting '%s' with value %lld (game type %d); the setting is skipped.",
              Utf16ToUtf8(std::u16string_view(key != nullptr ? key : u"<null>")).c_str(), static_cast<long long>(value),
              static_cast<int>(m_nGameType)
          );
      }
    ;

identifier_def_2
    : IDENTIFIER EQUAL FLOAT_NUMBER SEMICOLON FLOAT_NUMBER SEMICOLON FLOAT_NUMBER
      {
          if (m_nGameType < MatchingPoint::MPT_BASE || m_nGameType >= MatchingPoint::MPT_MAX) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s][%d] section name is not specified",
              //     L"parser_matchingpoint.y",
              //     0xa1)
              // L2_LOG_ERROR(Log,
              //     L"[%s][%d] section name is not specified",
              //     L"parser_matchingpoint.y",
              //     0xa1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot apply matching-point coefficient setting: no valid section is active (game type %d); the setting "
                  "is "
                  "skipped.",
                  static_cast<int>(m_nGameType)
              );
              break;
          }
          char16_t* const key = grammar::SemanticString($1);
          if (EqualsAsciiCaseInsensitive(key, u"PvpRatingPoint")) {
              const int type = static_cast<int>(m_nGameType);
              g_MatchingPointDB.m_fSaArray[type][0] = static_cast<float>(grammar::SemanticDouble($3));
              g_MatchingPointDB.m_fSaArray[type][1] = static_cast<float>(grammar::SemanticDouble($5));
              g_MatchingPointDB.m_fSaArray[type][2] = static_cast<float>(grammar::SemanticDouble($7));
              break;
          }
          // Log.Add(
          //     LOG_ERROR,
          //     L"[%s][%d] invalid key name:%s",
          //     L"parser_matchingpoint.y",
          //     0xa9,
          //     hostKey.c_str())
          // L2_LOG_ERROR(Log,
          //     L"[%s][%d] invalid key name:%s",
          //     L"parser_matchingpoint.y",
          //     0xa9,
          //     hostKey.c_str())
          L2_LOG_ERROR(
              Log,
              "Unknown matching-point coefficient setting '%s' (game type %d); expected PvpRatingPoint, the setting is "
              "skipped.",
              Utf16ToUtf8(std::u16string_view(key != nullptr ? key : u"<null>")).c_str(), static_cast<int>(m_nGameType)
          );
      }
    ;

integer_def
    : INTEGER EQUAL INTEGER
      {
          if (m_nGameType != MatchingPoint::MPT_OLYMPIAD) {
              break;
          }
          const int level = grammar::SemanticInt32($1);
          if (level >= 1 && level <= 120) {
              g_MatchingPointDB.m_Olympiad.anBasePoints[level] = grammar::SemanticInt64($3);
              break;
          }
          // Log.Add(
          //     LOG_ERROR,
          //     L"[%s][%d] invalid key level: %d",
          //     L"parser_matchingpoint.y",
          //     0xc3,
          //     level)
          // L2_LOG_ERROR(Log,
          //     L"[%s][%d] invalid key level: %d",
          //     L"parser_matchingpoint.y",
          //     0xc3,
          //     level)
          L2_LOG_ERROR(
              Log, "Cannot set Olympiad base points for level %d; expected 1 through 120, the setting is skipped.", level
          );
      }
    ;

identifier_def_3
    : IDENTIFIER EQUAL FLOAT_NUMBER SEMICOLON FLOAT_NUMBER SEMICOLON FLOAT_NUMBER SEMICOLON FLOAT_NUMBER
      {
          if (m_nGameType != MatchingPoint::MPT_OLYMPIAD) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s][%d] section name is not specified",
              //     L"parser_matchingpoint.y",
              //     0xd4)
              // L2_LOG_ERROR(Log,
              //     L"[%s][%d] section name is not specified",
              //     L"parser_matchingpoint.y",
              //     0xd4)
              L2_LOG_ERROR(
                  Log,
                  "Cannot apply Olympiad grade coefficients in game type %d; the Olympiad section is required, the setting "
                  "is "
                  "skipped.",
                  static_cast<int>(m_nGameType)
              );
              break;
          }
          char16_t* const key = grammar::SemanticString($1);
          int grade = -1;
          if (EqualsAsciiCaseInsensitive(key, u"S0")) {
              grade = 0;
          } else if (EqualsAsciiCaseInsensitive(key, u"S1")) {
              grade = 1;
          } else if (EqualsAsciiCaseInsensitive(key, u"S2")) {
              grade = 2;
          } else if (EqualsAsciiCaseInsensitive(key, u"S3")) {
              grade = 3;
          }
          if (grade < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"[%s][%d] invalid key name:%s",
              //     L"parser_matchingpoint.y",
              //     0xed,
              //     hostKey.c_str())
              // L2_LOG_ERROR(Log,
              //     L"[%s][%d] invalid key name:%s",
              //     L"parser_matchingpoint.y",
              //     0xed,
              //     hostKey.c_str())
              L2_LOG_ERROR(
                  Log, "Unknown Olympiad grade coefficient '%s'; expected S0 through S3, the setting is skipped.",
                  Utf16ToUtf8(std::u16string_view(key != nullptr ? key : u"<null>")).c_str()
              );
              break;
          }
          g_MatchingPointDB.m_Olympiad.SetS(
              grade, static_cast<float>(grammar::SemanticDouble($3)),
              static_cast<float>(grammar::SemanticDouble($5)), static_cast<float>(grammar::SemanticDouble($7)),
              static_cast<float>(grammar::SemanticDouble($9))
          );
      }
    ;

%%
