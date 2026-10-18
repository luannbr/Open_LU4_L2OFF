// CParserForSoulShotEffect grammar.
// Bison generates ParserSoulshotEffect.cpp and ParserSoulshotEffectTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserSoulshotEffect.h"

#include "L2Server/src/ObjectDB.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string_view>

#include "ParserSoulshotEffectTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForSoulShotEffect::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserSoulshotEffectTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForSoulShotEffect::CParserForSoulShotEffect() : m_lexer() {}

CParserForSoulShotEffect::~CParserForSoulShotEffect() = default;

int CParserForSoulShotEffect::Create(CObjectDB* objectDb, char16_t* fileBuffer, int fileSize, bool onlyPrecompiledHeader) {
    m_pObjDB = objectDb;
    return m_lexer.Create(fileBuffer, fileSize, onlyPrecompiledHeader);
}

void CParserForSoulShotEffect::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"SoulShotEffectDataChecker[%d] : syntax error", m_lexer.line + 1);
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

%token EFFECT_BEGIN
%token EFFECT_END
%token EFFECTTYPE
%token EFFECTLEVEL
%token ITEMID
%token SKILLID
%token INTEGER
%token NAME
%start effect_list

%%

effect_list
    : effect_list effect
    | effect
    ;

effect
    : EFFECT_BEGIN integer_def effecttype_def_list EFFECT_END
      { m_pObjDB->SetRegisterSoulShotEffectObject(m_pSoulShotEffect); }
    ;

integer_def
    : INTEGER
      {
          m_pSoulShotEffect = new CSoulShoteffect;
          m_pSoulShotEffect->Set($1);
      }
    ;

effecttype_def_list
    : effecttype_def_list effecttype_def
    | %empty
    ;

effecttype_def
    : EFFECTTYPE '=' INTEGER
      {
          const std::int32_t effectType = $3;
          if (effectType != 3 && effectType != 4 && effectType != 5 && effectType != 18) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"line[%d] effecttype only use 3[ITEM_ACTION_SOULSHOT] or 4[ITEM_ACTION_SPIRITSHOT] or
              //     5[ITEM_ACTION_BLESS_SPIRITSHOT] or 18[ITEM_ACTION_SUMMON_SOULSHOT]", m_lexer.line + 1)
              // Previous: L2_LOG_ERROR(Log,
              //     L"line[%d] effecttype only use 3[ITEM_ACTION_SOULSHOT] or 4[ITEM_ACTION_SPIRITSHOT] or
              //     5[ITEM_ACTION_BLESS_SPIRITSHOT] or 18[ITEM_ACTION_SUMMON_SOULSHOT]", m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  u"[soulshot parser] Effect type at line %d must be 3 "
                  u"(soulshot), 4 (spiritshot), 5 (blessed spiritshot), or 18 "
                  u"(summon soulshot); the supplied value is retained.",
                  m_lexer.line + 1
              );
          }
          m_pSoulShotEffect->SetType(effectType);
      }
    | EFFECTLEVEL '=' INTEGER
      { m_pSoulShotEffect->SetLevel($3); }
    | ITEMID '=' INTEGER
      { m_pSoulShotEffect->SetItemID($3); }
    | SKILLID '=' '{' INTEGER ';' INTEGER '}'
      { m_pSoulShotEffect->SetSkillID($4, $6); }
    ;

%%
