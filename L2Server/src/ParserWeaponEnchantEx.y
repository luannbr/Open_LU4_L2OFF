// CParserForWeaponEnchantExtraBonus grammar.
// Bison generates ParserWeaponEnchantEx.cpp and ParserWeaponEnchantExTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserWeaponEnchantEx.h"

#include "L2Server/src/Item.h"

#include "Shared/src/Log.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string_view>

#include "ParserWeaponEnchantExTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForWeaponEnchantExtraBonus::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserWeaponEnchantExTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
extern double g_dEnchantWeaponExBonus[CRT_MAX][AT_MAX][2][VALIDATE::EXTRA_MAX];

namespace {

char16_t AsciiLower(char16_t value) {
    return value >= u'A' && value <= u'Z' ? static_cast<char16_t>(value + (u'a' - u'A')) : value;
}

bool EqualsAsciiCaseInsensitive(const char16_t* value, std::u16string_view expected) {
    if (value == nullptr) {
        return false;
    }
    for (const char16_t expectedUnit : expected) {
        if (*value == u'\0' || AsciiLower(*value) != AsciiLower(expectedUnit)) {
            return false;
        }
        ++value;
    }
    return *value == u'\0';
}

struct AttackTypeName {
    std::u16string_view name;
    AttackType type;
};

constexpr std::array<AttackTypeName, 20> kAttackTypeNames{{
    {u"ancientsword", AT_ANCIENTSWORD},
    {u"blunt", AT_BLUNT},
    {u"twohandblunt", AT_TWOHANDBLUNT},
    {u"staff", AT_STAFF},
    {u"twohandstaff", AT_TWOHANDSTAFF},
    {u"bow", AT_BOW},
    {u"crossbow", AT_CROSSBOW},
    {u"dagger", AT_DAGGER},
    {u"dual", AT_DUAL},
    {u"dualblunt", AT_DUALBLUNT},
    {u"dualdagger", AT_DUALDAGGER},
    {u"dualfist", AT_DUALFIST},
    {u"fist", AT_FIST},
    {u"pole", AT_POLE},
    {u"rapier", AT_RAPIER},
    {u"sword", AT_SWORD},
    {u"twohandsword", AT_TWOHANDSWORD},
    {u"buster", AT_BUSTER},
    {u"twohandcrossbow", AT_TWOHANDCROSSBOW},
    {u"etc", AT_ETC},
}};

} // namespace

CParserForWeaponEnchantExtraBonus::CParserForWeaponEnchantExtraBonus() : m_lexer(), m_vAttackType() {
    m_vAttackType.clear();
}

CParserForWeaponEnchantExtraBonus::~CParserForWeaponEnchantExtraBonus() = default;

int CParserForWeaponEnchantExtraBonus::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForWeaponEnchantExtraBonus::InitMemberValues() {
    m_vAttackType.clear();
    m_grade = CRT_NONE;
    m_dSoulPower = 0.0;
    m_dSpiritPower = 0.0;
    m_nIsMagic = 0;
}

// void CParserForWeaponEnchantExtraBonus::yysyntaxerror() {
//     wchar_t message[60]{}
//     std::swprintf(
//         message,
//         std::size(message),
//         L"CParserForWeaponEnchantExtraBonusDataChecker[%d] : syntax error",
//         m_lexer.line + 1)
//     Log.Add(LOG_ERROR, message)
//     PopUpWindow(message)
// }
void CParserForWeaponEnchantExtraBonus::yysyntaxerror() {
    // The former 60-character scratch truncated even ordinary line numbers.
    // Log.Add(LOG_ERROR,
    //     "[scripts] Weapon enchantment bonuses: syntax error at line %lld.",
    //     static_cast<long long>(m_lexer.line) + 1)
    L2_LOG_ERROR(
        Log, "[scripts] Weapon enchantment bonuses: syntax error at line %lld.", static_cast<long long>(m_lexer.line) + 1
    );
    // Noise: The error is logged above. Only Windows needs a modal copy.
#if defined(_WIN32)
    PopUpWindow(L"Weapon enchantment bonuses: syntax error at line %lld.", static_cast<long long>(m_lexer.line) + 1);
#endif
}
}

%token INTEGER
%token FLOAT_NUMBER
%token IDENTIFIER
%token WEAPON_ENCHANT_STAT_BEGIN
%token WEAPON_ENCHANT_STAT_END
%token WEAPON_GRADE
%token WEAPON_TYPE
%token SOULSHOTPOWER
%token SPIRITSHOTPOWER
%token IS_MAGIC_WEAPON
// The original grammar has 5 shift/reduce and 12 reduce/reduce conflicts. Bison
// resolves them as the original tables did. A shift wins over a reduce,
// and the earlier rule wins over a later rule.
%start weapon_enchant_stat_list_list

%%

weapon_enchant_stat_list_list
    : weapon_enchant_stat_list_list weapon_enchant_stat_list
    | weapon_enchant_stat_list
    | %empty
    ;

weapon_enchant_stat_list
    : weapon_enchant_stat_list weapon_enchant_stat
    | weapon_enchant_stat
    | %empty
    ;

weapon_enchant_stat
    : WEAPON_ENCHANT_STAT_BEGIN
      { InitMemberValues(); }
      weapon_grade_def weapon_type_def is_magic_weapon_def soulshotpower_def spiritshotpower_def WEAPON_ENCHANT_STAT_END
      {
          for (const AttackType attackType : m_vAttackType) {
              g_dEnchantWeaponExBonus[static_cast<int>(m_grade)][static_cast<int>(attackType)][m_nIsMagic][0] =
                  m_dSoulPower / 100.0;
              g_dEnchantWeaponExBonus[static_cast<int>(m_grade)][static_cast<int>(attackType)][m_nIsMagic][1] =
                  m_dSpiritPower / 100.0;
          }
      }
    | %empty
    ;

weapon_grade_def
    : WEAPON_GRADE '=' '[' IDENTIFIER ']'
      {
          const char16_t* const grade = grammar::SemanticString($4);
          if (EqualsAsciiCaseInsensitive(grade, u"d")) {
              m_grade = CRT_D;
          } else if (EqualsAsciiCaseInsensitive(grade, u"c")) {
              m_grade = CRT_C;
          } else if (EqualsAsciiCaseInsensitive(grade, u"b")) {
              m_grade = CRT_B;
          } else if (EqualsAsciiCaseInsensitive(grade, u"a")) {
              m_grade = CRT_A;
          } else if (EqualsAsciiCaseInsensitive(grade, u"s")) {
              m_grade = CRT_S;
          } else if (EqualsAsciiCaseInsensitive(grade, u"s80")) {
              m_grade = CRT_S80;
          } else if (EqualsAsciiCaseInsensitive(grade, u"r")) {
              m_grade = CRT_R;
          } else if (EqualsAsciiCaseInsensitive(grade, u"r95")) {
              m_grade = CRT_R95;
          } else if (EqualsAsciiCaseInsensitive(grade, u"r99")) {
              m_grade = CRT_R99;
          } else if (EqualsAsciiCaseInsensitive(grade, u"none")) {
              m_grade = CRT_NONE;
          } else {
              // Log.Add(LOG_ERROR, L"enchant bonus grade error!!")
              L2_LOG_ERROR(Log, u"enchant bonus grade error!!");
          }
      }
    | %empty
    ;

weapon_type_def
    : WEAPON_TYPE '=' '{' n7 '}'
    | %empty
    ;

n7
    : n7 ';' n8
    | n8
    | %empty
    ;

n8
    : '[' IDENTIFIER ']'
      {
          [&] {
              const char16_t* const attackName = grammar::SemanticString($2);
              if (EqualsAsciiCaseInsensitive(attackName, u"all")) {
                  for (int value = AT_NONE; value < AT_MAX; ++value) {
                      if (value != AT_FISHINGROD && value != AT_FLAG && value != AT_OWNTHING) {
                          m_vAttackType.push_back(static_cast<AttackType>(value));
                      }
                  }
                  return;
              }
              for (const AttackTypeName& entry : kAttackTypeNames) {
                  if (EqualsAsciiCaseInsensitive(attackName, entry.name)) {
                      m_vAttackType.push_back(entry.type);
                      return;
                  }
              }
              // Log.Add(LOG_ERROR, L"weapon type has problem")
              L2_LOG_ERROR(Log, u"weapon type has problem");
          }();
      }
    | %empty
    ;

is_magic_weapon_def
    : IS_MAGIC_WEAPON '=' INTEGER
      {
          m_nIsMagic = grammar::SemanticInt32($3);
          if (m_nIsMagic < 0 || m_nIsMagic >= 2) {
              // Log.Add(LOG_ERROR, L"is magic setting has problem")
              L2_LOG_ERROR(Log, u"is magic setting has problem");
          }
      }
    | %empty
    ;

soulshotpower_def
    : SOULSHOTPOWER '=' FLOAT_NUMBER
      { m_dSoulPower = grammar::SemanticDouble($3); }
    | %empty
    ;

spiritshotpower_def
    : SPIRITSHOTPOWER '=' FLOAT_NUMBER
      { m_dSpiritPower = grammar::SemanticDouble($3); }
    | %empty
    ;

%%
