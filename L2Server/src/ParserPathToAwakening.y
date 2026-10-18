// CParserForPathToAwakening grammar.
// Bison generates ParserPathToAwakening.cpp and ParserPathToAwakeningTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserPathToAwakening.h"

#include "L2Server/src/PathToAwakeningNotice.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string_view>

#include "ParserPathToAwakeningTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForPathToAwakening::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserPathToAwakeningTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

struct CountryCodeEntry {
    std::u16string_view name;
    int id;
};

constexpr std::array<CountryCodeEntry, 14> kCountryCodeTable{{
    {u"k", 0},
    {u"e", 1},
    {u"j", 2},
    {u"tw", 3},
    {u"cn", 4},
    {u"th", 5},
    {u"ph", 6},
    {u"id", 7},
    {u"ru", 8},
    {u"eu", 9},
    {u"de", 10},
    {u"fr", 11},
    {u"pl", 12},
    {u"tr", 13},
}};

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

CParserForPathToAwakening::CParserForPathToAwakening() : m_lexer() {
    // nCountryCode is assigned only by grammar action 0 in
    // the current constructor/body pair.
}

CParserForPathToAwakening::~CParserForPathToAwakening() = default;

int CParserForPathToAwakening::Create() {
    return m_lexer.Create();
}

void CParserForPathToAwakening::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"PathToAwakening Checker[%d] : syntax error", m_lexer.line + 1);
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
%token NAME
%token INTEGER
%token PATH_BEGIN
%token PATH_END
%token LEVEL_TYPE
%token AREA_TYPE
%token LEVEL
%token ITEM
%start path_list

%%

path_list
    : path_list path
    | path
    ;

path
    : PATH_BEGIN NAME
      {
          nCountryCode = -1;
          char16_t* const country = grammar::SemanticString($2);
          for (const CountryCodeEntry& entry : kCountryCodeTable) {
              if (EqualsAsciiCaseInsensitive(country, entry.name)) {
                  nCountryCode = entry.id;
                  break;
              }
          }
      }
      level_type_def_list PATH_END
    ;

level_type_def_list
    : level_type_def_list level_type_def
    | level_type_def
    ;

level_type_def
    : LEVEL_TYPE LEVEL EQUAL INTEGER ITEM EQUAL '{' NAME ';' INTEGER '}'
      {
          if (nCountryCode != ::g_nCountryCode) {
              break;
          }
          CPathToAwakeningNotice* const manager = CPathToAwakeningNotice::Inst();
          auto* notice = static_cast<CNotice_LevelCond*>(manager->GetNotice(PathToAwakening::PTA_LEVEL));
          if (notice == nullptr) {
              notice = new CNotice_LevelCond;
              manager->AddAwakeningNotice(PathToAwakening::PTA_LEVEL, notice);
          }
          const int itemClassId = static_cast<int>(g_PrecompiledHeaderDB.Search(grammar::SemanticString($8)));
          std::int64_t count = 0;
          if (itemClassId != -1) {
              count = static_cast<std::int64_t>(grammar::SemanticInt32($10));
          }
          const int level = grammar::SemanticInt32($4);
          notice->AddLevelCond(level, {itemClassId, count});
      }
    | AREA_TYPE
    ;

%%
