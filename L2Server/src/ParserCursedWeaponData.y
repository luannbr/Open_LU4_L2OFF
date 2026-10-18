// cursedweapondata.txt grammar (original L2Server/parser_cursedweapondata.y).
// Bison generates ParserCursedWeaponData.cpp and ParserCursedWeaponDataTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserCursedWeaponData.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/ObjectDB.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

#include "ParserCursedWeaponDataTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForCursedWeapon::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserCursedWeaponDataTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

CParserForCursedWeapon::CParserForCursedWeapon() : m_lexer() {}

CParserForCursedWeapon::~CParserForCursedWeapon() = default;

int CParserForCursedWeapon::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForCursedWeapon::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"CursedWeaponDataChecker[%d] : syntax error", m_lexer.line + 1);
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

%token NAME
%token INTEGER
%token CURSED_WEAPON_BEGIN
%token CURSED_WEAPON_END
%token ITEM_NAME
%token CREATE_RATE_PER_10MIN
%token CREATE_RATE_PER_NPC
%token DROP_RATE_ONDYING
%token LIFE_TIME
%token EXPIRE_BY_NONPK
%token EXPIRE_BY_DROP
%token TRANSFORM_ID
%start cursed_weapon_list

%%

cursed_weapon_list
    : cursed_weapon_list cursed_weapon
    | cursed_weapon
    ;

cursed_weapon
    : CURSED_WEAPON_BEGIN
      {
          pInfo = new CursedWeaponInfo;
          pInfo->nItemClassId = -1;
      }
      item_name_def_list CURSED_WEAPON_END
      {
          if (pInfo->nItemClassId < 0) {
              // Log.Add(LOG_ERROR, L"invalid cursed_weapon_info")
              L2_LOG_ERROR(Log, u"invalid cursed_weapon_info");
          } else {
              CCursedWeaponMgr::inst().Add(pInfo);
          }
          pInfo = nullptr;
      }
    ;

item_name_def_list
    : item_name_def_list item_name_def
    | item_name_def
    ;

item_name_def
    : ITEM_NAME '=' NAME
      {
          char16_t* const name = grammar::SemanticString($3);
          const int classId = g_ObjectDB.GetClassIdFromName(name, false);
          if (classId == -1) {
              const std::wstring hostName = target_utf16::ToHostCodeUnits(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"%s[%d] : Undefined word[%s]",
              //     L"..\\Script\\cursedweapondata.txt",
              //     m_lexer.line + 1,
              //     hostName.c_str())
              L2_LOG_ERROR(
                  Log, u"%s[%d] : Undefined word[%s]", u"..\\Script\\cursedweapondata.txt", m_lexer.line + 1,
                  hostName.c_str()
              );
          }
          pInfo->nItemClassId = classId;
      }
    | CREATE_RATE_PER_10MIN '=' INTEGER
      { pInfo->nCreateRatePer10Min = grammar::SemanticInt32($3); }
    | CREATE_RATE_PER_NPC '=' INTEGER
      { pInfo->nCreateRatePerNPC = grammar::SemanticInt32($3); }
    | DROP_RATE_ONDYING '=' INTEGER
      { pInfo->nDropRateOnDying = grammar::SemanticInt32($3); }
    | LIFE_TIME '=' INTEGER
      { pInfo->nLifeTime = grammar::SemanticInt32($3); }
    | EXPIRE_BY_NONPK '=' INTEGER
      { pInfo->nExpireByNonpk = grammar::SemanticInt32($3); }
    | EXPIRE_BY_DROP '=' INTEGER
      { pInfo->nExpireByDrop = grammar::SemanticInt32($3); }
    | TRANSFORM_ID '=' INTEGER
      { pInfo->transformID = grammar::SemanticInt32($3); }
    ;

%%
