// CParserForMonRace grammar (original L2Server/monrace_parser.y).
// Bison generates MonRaceParser.cpp and MonRaceParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/MonRaceParser.h"

#include "Shared/src/TextRuntime.h"

#include "L2Server/src/MonRaceData.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "MonRaceParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForMonRace::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"MonRaceParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

void CopyUtf16Name(char16_t (&destination)[128], const char16_t* source) {
    if (source == nullptr) {
        destination[0] = u'\0';
        return;
    }
    std::size_t count = 0;
    while (source[count] != u'\0' && count < 127) {
        ++count;
    }
    std::copy_n(source, count, destination);
    destination[count] = u'\0';
}

} // namespace

CParserForMonRace::CParserForMonRace() : m_lexer(), m_monrace(nullptr), m_point{}, m_pointlist(), m_feerate(0) {
}

CParserForMonRace::~CParserForMonRace() = default;

int CParserForMonRace::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token MON_BEGIN
%token MON_END
%token TRACK_BEGIN
%token TRACK_END
%token SYS_STRING
%token MIN_SPEED
%token MAX_SPEED
%token GUTS
%token INITIAL_COND
%token INITIAL_WIN
%token INITIAL_RUNCOUNT
%token INITIAL_WINCOUNT
%token RACE_BEGIN
%token RACE_END
%token RETURN_RATE
%token BEGIN_MUSIC
%token BEGIN_SOUND
%token RESIDENCEID
%token RACE_AREA
%token NAME
%token INTEGER
%start n1

%%

n1
    : n1 n4
    | n1 n2
    | n1 n10
    | %empty
    ;

n2
    : track
    ;

track
    : TRACK_BEGIN INTEGER '{' INTEGER ',' INTEGER ',' INTEGER ',' INTEGER ',' INTEGER ',' INTEGER '}' TRACK_END
      {
          g_MonRaceDB.SetTrackBegin(
              grammar::SemanticInt32($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6),
              grammar::SemanticInt32($8)
          );
          g_MonRaceDB.SetTrackEnd(
              grammar::SemanticInt32($2), grammar::SemanticInt32($10), grammar::SemanticInt32($12),
              grammar::SemanticInt32($14)
          );
      }
    ;

n4
    : race
    ;

race
    : RACE_BEGIN
    | RETURN_RATE '=' INTEGER
      { g_MonRaceDB.m_nReturnRate = grammar::SemanticInt32($3); }
    | RESIDENCEID '=' INTEGER
      { g_MonRaceDB.m_nResidenceID = grammar::SemanticInt32($3); }
    | BEGIN_MUSIC '=' NAME
      { CopyUtf16Name(g_MonRaceDB.m_szMusic, grammar::SemanticPointer<char16_t>($3)); }
    | BEGIN_SOUND '=' NAME
      { CopyUtf16Name(g_MonRaceDB.m_szSound, grammar::SemanticPointer<char16_t>($3)); }
    | RACE_AREA '=' '{' n6 '}'
    | RACE_END
      {
          if (g_MonRaceDB.m_nReturnRate <= 0 || g_MonRaceDB.m_nReturnRate >= 101) {
              // Current generated code omits the value for its %d placeholder.
              // Log.Add(LOG_ERROR, L"invalid raturnrate(%d)")
              // L2_LOG_ERROR(Log, L"invalid raturnrate(%d)")
              L2_LOG_ERROR(
                  Log,
                  "Monster race return rate %d at script line %lld is outside 1 through 100; the supplied value is "
                  "retained.",
                  g_MonRaceDB.m_nReturnRate, static_cast<long long>(m_lexer.line) + 1
              );
          }
      }
    ;

n6
    : n6 ';' n7
      {
          g_MonRaceDB.AddRaceAreaInfo(m_feerate, m_pointlist);
          m_feerate = 0;
          m_pointlist.clear();
      }
    | n7
      {
          g_MonRaceDB.AddRaceAreaInfo(m_feerate, m_pointlist);
          m_feerate = 0;
          m_pointlist.clear();
      }
    ;

n7
    : '{' INTEGER ';' n8 '}'
      { m_feerate = grammar::SemanticInt32($2); }
    ;

n8
    : n8 ';' n9
      { m_pointlist.push_back(m_point); }
    | n9
      {
          m_pointlist.clear();
          m_pointlist.push_back(m_point);
      }
    ;

n9
    : '{' INTEGER ',' INTEGER ',' INTEGER '}'
      {
          m_point = FVector{
              static_cast<double>(grammar::SemanticInt32($2)),
              static_cast<double>(grammar::SemanticInt32($4)),
              static_cast<double>(grammar::SemanticInt32($6)),
          };
      }
    ;

n10
    : mon
    ;

mon
    : MON_BEGIN INTEGER INTEGER NAME
      {
          m_monrace = new CMonRaceData;
          m_monrace->Init();
          m_monrace->runner_id = grammar::SemanticInt32($2);
          m_monrace->npc_classid = grammar::SemanticInt32($3);
          CopyUtf16Name(m_monrace->runner_name, grammar::SemanticPointer<char16_t>($4));
      }
    | SYS_STRING '=' INTEGER
      { m_monrace->sys_string = grammar::SemanticInt32($3); }
    | MIN_SPEED '=' INTEGER
      { m_monrace->min_speed = grammar::SemanticInt32($3); }
    | MAX_SPEED '=' INTEGER
      { m_monrace->max_speed = grammar::SemanticInt32($3); }
    | GUTS '=' '{' n12 '}'
      {
          auto* const values = grammar::SemanticPointer<std::vector<std::int64_t>>($4);
          if (values->size() != 20) {
              // Log.Add(LOG_ERROR, L"invalid guts fields")
              // L2_LOG_ERROR(Log, L"invalid guts fields")
              L2_LOG_ERROR(
                  Log,
                  "Monster race runner %d has %zu guts fields at script line %lld; expected 20, but processing continues "
                  "with the supplied list.",
                  m_monrace->runner_id, static_cast<std::size_t>(values->size()), static_cast<long long>(m_lexer.line) + 1
              );
          }
          m_monrace->SetGuts(values);
          delete values;
      }
    | INITIAL_COND '=' INTEGER
      { m_monrace->initial_cond = grammar::SemanticInt32($3); }
    | INITIAL_WIN '=' INTEGER
      { m_monrace->initial_win = grammar::SemanticInt32($3); }
    | INITIAL_RUNCOUNT '=' INTEGER
      { m_monrace->m_runcount = grammar::SemanticInt32($3); }
    | INITIAL_WINCOUNT '=' INTEGER
      { m_monrace->m_wincount = grammar::SemanticInt32($3); }
    | MON_END
      {
          m_monrace->Debug();
          g_MonRaceDB.Insert(m_monrace);
      }
    ;

n12
    : n12 ',' INTEGER
      {
          auto* const values = grammar::SemanticPointer<std::vector<std::int64_t>>($1);
          values->push_back(static_cast<std::int64_t>($3));
          $$ = grammar::PointerSemantic(values);
      }
    | INTEGER
      {
          auto* const values = new std::vector<std::int64_t>;
          values->push_back(static_cast<std::int64_t>($1));
          $$ = grammar::PointerSemantic(values);
      }
    ;

%%
