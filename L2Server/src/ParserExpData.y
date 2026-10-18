// expdata.txt grammar (original L2Server/parser_expdata.y).
// Bison generates ParserExpData.cpp and ParserExpDataTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserExpData.h"

#include "Shared/src/Runtime/SourceLog.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string_view>

#include "ParserExpDataTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse ParserForExp::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserExpDataTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
std::int64_t ExpByLevel[122] = {
    0,
    68,
    363,
    1168,
    2884,
    6038,
    11287,
    19423,
    31378,
    48229,
    71202,
    101677,
    141193,
    191454,
    254330,
    331867,
    426288,
    540000,
    675596,
    835862,
    1023784,
    1242546,
    1495543,
    1786379,
    2118876,
    2497077,
    2925250,
    3407897,
    3949754,
    4555796,
    5231246,
    5981576,
    6812513,
    7730044,
    8740422,
    9850166,
    11066072,
    12395215,
    13844951,
    15422929,
    17137087,
    18995665,
    21007203,
    23180550,
    25524868,
    28049635,
    30764654,
    33680052,
    36806289,
    40154162,
    45525133,
    51262490,
    57383988,
    63907911,
    70853089,
    80700831,
    91162654,
    102265881,
    114038596,
    126509653,
    146308200,
    167244337,
    189364894,
    212717908,
    237352644,
    271975263,
    308443198,
    346827154,
    387199547,
    429634523,
    474207979,
    532694979,
    606322775,
    696381369,
    804225364,
    931275828,
    1151275834,
    1511275834,
    2099275834,
    4200000000,
    6300000000,
    8820000000,
    11844000000,
    15472800000,
    19827360000,
    25314105600,
    32211728640,
    40488876288,
    50421453466,
    63424099953,
    79027275737,
    97751086678,
    121155850355,
    149241566767,
    182944426462,
    225005595360,
    275478998038,
    336047081252,
    408728781109,
    2814389704470,
    13616607730552,
    24188622041092,
    49561456386388,
    80008857600743,
    153082620515195,
    537896893409308,
    1064027986393360,
    2326742609555090,
    3842000157349170,
    7478618272054960,
    24934385222642800,
    45881305563348200,
    71017609972194700,
    101181175262810000,
    137377453611549000,
    224248521648523000,
    328493803292891000,
    453588141266133000,
    603701346834023000,
    783837193515491000,
    1648489257586540000,
    2686071734471800000,
};

double ExpByLevelTraining[122]{};

namespace {

grammar::SemanticBits DoubleSemantic(double value) {
    grammar::SemanticBits result = 0;
    static_assert(sizeof(result) == sizeof(value));
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

} // namespace

ParserForExp::ParserForExp() : m_lexer() {}

ParserForExp::~ParserForExp() = default;

int ParserForExp::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}
}

%token EXP_TABLE_BEGIN
%token EXP_TABLE_END
%token LEVEL
%token EXP
%token TRAINING_RATE
%token INTEGER
%token FLOAT_NUMBER
%start exp_table

%%

exp_table
    : EXP_TABLE_BEGIN level_def_list EXP_TABLE_END
    | %empty
    ;

level_def_list
    : level_def_list level_def
    | %empty
    ;

level_def
    : LEVEL '=' INTEGER EXP '=' INTEGER TRAINING_RATE '=' integer_def
      {
          const std::int64_t level = grammar::SemanticInt64($3);
          const std::int64_t experience = grammar::SemanticInt64($6);
          const double trainingRate = grammar::SemanticDouble($9);
          if (static_cast<std::uint64_t>(level) >= std::size(ExpByLevel)) {
              // wchar_t message[128]{}
              // std::swprintf(
              //     message,
              //     std::size(message),
              //     L"Invalid level number [%lld]",
              //     static_cast<long long>(level))
              // yyerror(message)
              char16_t message[384]{};
              FormatUtf16To(
                  message, std::size(message),
                  u"expdata.txt line %lld: level %lld is outside 0..%llu "
                  u"(experience %lld, training_rate %.17g); this row is skipped.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<long long>(level),
                  static_cast<unsigned long long>(std::size(ExpByLevel) - 1), static_cast<long long>(experience),
                  trainingRate
              );
              const runtime::logging::SourceFileScope source(__FILE__);
              grammar::ReportError(message);
              break;
          }
          if (!(trainingRate >= 0.0 && trainingRate <= 1.0)) {
              // wchar_t message[128]{}
              // std::swprintf(
              //     message,
              //     std::size(message),
              //     L"Invalid training_rate [%f]",
              //     trainingRate)
              // yyerror(message)
              char16_t message[384]{};
              FormatUtf16To(
                  message, std::size(message),
                  u"expdata.txt line %lld, level %lld: training_rate must be finite "
                  u"and within 0..1 (got %.17g); this row is skipped.",
                  static_cast<long long>(m_lexer.line) + 1, static_cast<long long>(level), trainingRate
              );
              const runtime::logging::SourceFileScope source(__FILE__);
              grammar::ReportError(message);
              break;
          }
          const std::size_t index = static_cast<std::size_t>(level);
          ExpByLevel[index] = experience;
          // The current image places a zero qword immediately before
          // ExpByLevel and reads ExpByLevel[-1] for the level-zero row. Keep
          // the same observable value without invoking host-language UB.
          const std::int64_t previous = index == 0 ? 0 : ExpByLevel[index - 1];
          ExpByLevelTraining[index] =
              (static_cast<double>(runtime::SubtractInt64Wrapping(experience, previous)) / 60.0) * trainingRate;
      }
    ;

integer_def
    : INTEGER
      {
          const double converted = static_cast<double>(grammar::SemanticInt64($1));
          $$ = DoubleSemantic(converted);
      }
    | FLOAT_NUMBER
      { $$ = $1; }
    ;

%%
