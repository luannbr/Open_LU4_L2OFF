// multisell.txt grammar (original L2Server/multisell_parser.y).
// Bison generates MultiSellParser.cpp and MultiSellParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/MultiSellParser.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/MultiSell.h"
#include "L2Server/src/ObjectDB.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "MultiSellParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForMultiSell::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"MultiSellParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

char16_t* SemanticText(grammar::SemanticBits value) {
    return grammar::SemanticString(value);
}

} // namespace

CParserForMultiSell::CParserForMultiSell() : m_lexer(), bExistMultiSell(false) {}

CParserForMultiSell::~CParserForMultiSell() = default;

int CParserForMultiSell::Create() {
    return m_lexer.Create();
}

int CParserForMultiSell::Create(char16_t* fileBuffer, int fileSize) {
    return m_lexer.Create(fileBuffer, fileSize);
}

int CParserForMultiSell::FindItemClassId(const char16_t* itemName) {
    int itemClassId = static_cast<int>(g_PrecompiledHeaderDB.Search(itemName));
    if (itemClassId == -1) {
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
            Utf16ToUtf8(std::u16string_view(itemName != nullptr ? itemName : u"<null>")).c_str(),
            static_cast<long long>(m_lexer.line) + 1
        );
    } else if (itemClassId > 0) {
        itemClassId = g_ObjectDB.GetClassIdFromName(itemName, false);
    }
    return itemClassId;
}
}

%token EQUAL
%token MULTISELL_BEGIN
%token SELLLIST
%token IS_SHOW_ALL
%token KEEP_ENCHANTED
%token SHOW_VARIATION_ITEM
%token KEEP_SHAPE_SHIFTED
%token IS_DUTYFREE
%token SHOW_TYPE
%token SSQ_FLAG
%token REQUIRED_NPC
%token IS_WITHOUT_NPC
%token MULTISELL_END
%token SHOW_ENSOUL_ITEM
%token NAME
%token INTEGER
%token FLOAT_NUMBER
%start n1

%%

n1
    : multisell_list
    ;

multisell_list
    : multisell_list multisell
    | %empty
    ;

multisell
    : MULTISELL_BEGIN NAME INTEGER
      {
          const std::int32_t id = grammar::SemanticInt32($3);
          m_pMultiSellList = g_MultiSellDB.find(id);
          if (m_pMultiSellList == nullptr) {
              m_pMultiSellList = new CMultiSellList;
              m_pMultiSellList->m_nId = id;
          } else {
              m_pMultiSellList->ClearMultiSellInfo();
              m_pMultiSellList->ClearNPCList();
              bExistMultiSell = true;
          }
      }
      selllist_def_list MULTISELL_END
      {
          if (!bExistMultiSell) {
              g_MultiSellDB.Add(m_pMultiSellList);
          }
          m_pMultiSellList = nullptr;
          bExistMultiSell = false;
      }
    ;

selllist_def_list
    : selllist_def_list selllist_def
    | %empty
    ;

selllist_def
    : SELLLIST EQUAL '{' n9 '}'
    | IS_SHOW_ALL EQUAL INTEGER
      {
          const auto ensureInfo = [this]() {
              CMultiSellInfo* info = m_pMultiSellList->find(m_pMultiSellList->m_nLastInfoId);
              if (info == nullptr) {
                  info = new CMultiSellInfo;
                  m_pMultiSellList->Add(info);
              }
              return info;
          };
          const auto setCheckedFlag = [&](std::int32_t& target, const wchar_t* name) {
              ensureInfo();
              const std::int32_t value = grammar::SemanticInt32($3);
              target = value;
              if (value < 0 || value >= 2) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined type %s[%d] in line [%d]",
                  //     name,
                  //     value,
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Undefined type %s[%d] in line [%d]",
                  //     name,
                  //     value,
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "Multisell flag %s has value %d at script line %lld; expected 0 or 1, but the supplied value is "
                      "retained.",
                      WideToUtf8(name).c_str(), value, static_cast<long long>(m_lexer.line) + 1
                  );
              }
          };
          setCheckedFlag(m_pMultiSellList->m_nShowAll, L"is_show_all");
      }
    | SHOW_VARIATION_ITEM EQUAL INTEGER
      {
          const auto ensureInfo = [this]() {
              CMultiSellInfo* info = m_pMultiSellList->find(m_pMultiSellList->m_nLastInfoId);
              if (info == nullptr) {
                  info = new CMultiSellInfo;
                  m_pMultiSellList->Add(info);
              }
              return info;
          };
          ensureInfo();
          const std::int32_t value = grammar::SemanticInt32($3);
          m_pMultiSellList->m_bShowVariationItem = value == 1;
          if (value < 0 || value >= 2) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined type show_variation_item[%d] in line [%d]",
              //     value,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined type show_variation_item[%d] in line [%d]",
              //     value,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log, "Multisell show_variation_item has value %d at script line %lld; expected 0 or 1, using false.",
                  value, static_cast<long long>(m_lexer.line) + 1
              );
          }
      }
    | KEEP_ENCHANTED EQUAL INTEGER
      {
          const auto ensureInfo = [this]() {
              CMultiSellInfo* info = m_pMultiSellList->find(m_pMultiSellList->m_nLastInfoId);
              if (info == nullptr) {
                  info = new CMultiSellInfo;
                  m_pMultiSellList->Add(info);
              }
              return info;
          };
          const auto setCheckedFlag = [&](std::int32_t& target, const wchar_t* name) {
              ensureInfo();
              const std::int32_t value = grammar::SemanticInt32($3);
              target = value;
              if (value < 0 || value >= 2) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined type %s[%d] in line [%d]",
                  //     name,
                  //     value,
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Undefined type %s[%d] in line [%d]",
                  //     name,
                  //     value,
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "Multisell flag %s has value %d at script line %lld; expected 0 or 1, but the supplied value is "
                      "retained.",
                      WideToUtf8(name).c_str(), value, static_cast<long long>(m_lexer.line) + 1
                  );
              }
          };
          setCheckedFlag(m_pMultiSellList->m_nKeepEnchanted, L"keep_enchanted");
      }
    | KEEP_SHAPE_SHIFTED EQUAL INTEGER
      {
          const auto ensureInfo = [this]() {
              CMultiSellInfo* info = m_pMultiSellList->find(m_pMultiSellList->m_nLastInfoId);
              if (info == nullptr) {
                  info = new CMultiSellInfo;
                  m_pMultiSellList->Add(info);
              }
              return info;
          };
          const auto setCheckedFlag = [&](std::int32_t& target, const wchar_t* name) {
              ensureInfo();
              const std::int32_t value = grammar::SemanticInt32($3);
              target = value;
              if (value < 0 || value >= 2) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined type %s[%d] in line [%d]",
                  //     name,
                  //     value,
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Undefined type %s[%d] in line [%d]",
                  //     name,
                  //     value,
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "Multisell flag %s has value %d at script line %lld; expected 0 or 1, but the supplied value is "
                      "retained.",
                      WideToUtf8(name).c_str(), value, static_cast<long long>(m_lexer.line) + 1
                  );
              }
          };
          setCheckedFlag(m_pMultiSellList->m_nKeepShapeShifted, L"keep_shape_shifted");
      }
    | SHOW_ENSOUL_ITEM EQUAL INTEGER
      {
          const auto ensureInfo = [this]() {
              CMultiSellInfo* info = m_pMultiSellList->find(m_pMultiSellList->m_nLastInfoId);
              if (info == nullptr) {
                  info = new CMultiSellInfo;
                  m_pMultiSellList->Add(info);
              }
              return info;
          };
          m_pMultiSellList->m_bShowEnsoulItem = true;
          ensureInfo();
          const std::uint32_t value = static_cast<std::uint32_t>($3);
          if (value < 2) {
              m_pMultiSellList->m_bKeepEnsoul = value == 1;
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined type show_ensoul_item[%d] in line [%d]",
              //     static_cast<int>(value),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined type show_ensoul_item[%d] in line [%d]",
              //     static_cast<int>(value),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Multisell show_ensoul_item has value %u at script line %lld; expected 0 or 1. Showing ensoul items is "
                  "enabled; keep-ensoul remains unchanged.",
                  static_cast<unsigned>(value), static_cast<long long>(m_lexer.line) + 1
              );
          }
      }
    | IS_DUTYFREE EQUAL INTEGER
      {
          const auto ensureInfo = [this]() {
              CMultiSellInfo* info = m_pMultiSellList->find(m_pMultiSellList->m_nLastInfoId);
              if (info == nullptr) {
                  info = new CMultiSellInfo;
                  m_pMultiSellList->Add(info);
              }
              return info;
          };
          const auto setCheckedFlag = [&](std::int32_t& target, const wchar_t* name) {
              ensureInfo();
              const std::int32_t value = grammar::SemanticInt32($3);
              target = value;
              if (value < 0 || value >= 2) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined type %s[%d] in line [%d]",
                  //     name,
                  //     value,
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Undefined type %s[%d] in line [%d]",
                  //     name,
                  //     value,
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "Multisell flag %s has value %d at script line %lld; expected 0 or 1, but the supplied value is "
                      "retained.",
                      WideToUtf8(name).c_str(), value, static_cast<long long>(m_lexer.line) + 1
                  );
              }
          };
          setCheckedFlag(m_pMultiSellList->m_nIsDutyfree, L"is_dutyfree");
      }
    | SHOW_TYPE EQUAL INTEGER
      {
          const auto ensureInfo = [this]() {
              CMultiSellInfo* info = m_pMultiSellList->find(m_pMultiSellList->m_nLastInfoId);
              if (info == nullptr) {
                  info = new CMultiSellInfo;
                  m_pMultiSellList->Add(info);
              }
              return info;
          };
          const auto setCheckedFlag = [&](std::int32_t& target, const wchar_t* name) {
              ensureInfo();
              const std::int32_t value = grammar::SemanticInt32($3);
              target = value;
              if (value < 0 || value >= 2) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined type %s[%d] in line [%d]",
                  //     name,
                  //     value,
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Undefined type %s[%d] in line [%d]",
                  //     name,
                  //     value,
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "Multisell flag %s has value %d at script line %lld; expected 0 or 1, but the supplied value is "
                      "retained.",
                      WideToUtf8(name).c_str(), value, static_cast<long long>(m_lexer.line) + 1
                  );
              }
          };
          setCheckedFlag(m_pMultiSellList->m_nShowType, L"show_type");
      }
    | SSQ_FLAG EQUAL INTEGER
      {
          const auto ensureInfo = [this]() {
              CMultiSellInfo* info = m_pMultiSellList->find(m_pMultiSellList->m_nLastInfoId);
              if (info == nullptr) {
                  info = new CMultiSellInfo;
                  m_pMultiSellList->Add(info);
              }
              return info;
          };
          const auto setCheckedFlag = [&](std::int32_t& target, const wchar_t* name) {
              ensureInfo();
              const std::int32_t value = grammar::SemanticInt32($3);
              target = value;
              if (value < 0 || value >= 2) {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined type %s[%d] in line [%d]",
                  //     name,
                  //     value,
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Undefined type %s[%d] in line [%d]",
                  //     name,
                  //     value,
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "Multisell flag %s has value %d at script line %lld; expected 0 or 1, but the supplied value is "
                      "retained.",
                      WideToUtf8(name).c_str(), value, static_cast<long long>(m_lexer.line) + 1
                  );
              }
          };
          setCheckedFlag(m_pMultiSellList->m_nSSQFlag, L"ssq_flag");
      }
    | REQUIRED_NPC EQUAL '{' n7 '}'
    | IS_WITHOUT_NPC EQUAL INTEGER
      {
          if (!m_pMultiSellList->HasRequiredNPC()) {
              m_pMultiSellList->m_nIsWithoutNPC = grammar::SemanticInt32($3);
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Required NPC is not Empty! in line [%d]",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Required NPC is not Empty! in line [%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot change multisell without-NPC flag at script line %lld: required NPCs are already registered; "
                  "the flag remains unchanged.",
                  static_cast<long long>(m_lexer.line) + 1
              );
          }
      }
    ;

n7
    : n7 ';' name_def
    | name_def
    ;

name_def
    : NAME
      {
          char16_t* const npcName = SemanticText($1);
          const int npcId = static_cast<int>(g_PrecompiledHeaderDB.Search(npcName));
          if (!m_pMultiSellList->IsWithoutNPC()) {
              if (npcId > 0) {
                  m_pMultiSellList->AddRequiredNPC(static_cast<std::uint32_t>(npcId));
              } else {
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined NPC[%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1)
                  // L2_LOG_ERROR(Log,
                  //     L"Undefined NPC[%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.line + 1)
                  L2_LOG_ERROR(
                      Log,
                      "Cannot add required multisell NPC '%s' at script line %lld: precompiled-header ID %d is not "
                      "positive; the NPC is skipped.",
                      Utf16ToUtf8(std::u16string_view(npcName != nullptr ? npcName : u"<null>")).c_str(),
                      static_cast<long long>(m_lexer.line) + 1, npcId
                  );
              }
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"ERROR! WithoutNPC Flag is ON!!!! in line [%d]",
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"ERROR! WithoutNPC Flag is ON!!!! in line [%d]",
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot add required multisell NPC '%s' at script line %lld: without-NPC mode is enabled; the NPC is "
                  "skipped.",
                  Utf16ToUtf8(std::u16string_view(npcName != nullptr ? npcName : u"<null>")).c_str(),
                  static_cast<long long>(m_lexer.line) + 1
              );
          }
      }
    ;

n9
    : n9 ';' '{' n10 '}'
    | '{' n10 '}'
    ;

n10
    : '{' n11 '}' ';' '{' n13 '}'
      {
          CMultiSellInfo* const info = m_pMultiSellList->find(m_pMultiSellList->m_nLastInfoId);
          if (info != nullptr) {
              ++m_pMultiSellList->m_nLastInfoId;
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Cannot find price multisell[%d] in line [%d]",
              //     m_pMultiSellList->m_nLastInfoId,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Cannot find price multisell[%d] in line [%d]",
              //     m_pMultiSellList->m_nLastInfoId,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot finish multisell entry %d at script line %lld: its price record is missing; the entry index is "
                  "not advanced.",
                  m_pMultiSellList->m_nLastInfoId, static_cast<long long>(m_lexer.line) + 1
              );
          }
      }
    | '{' n11 '}' ';' '{' n13 '}' ';' '{' INTEGER '}'
      {
          CMultiSellInfo* const info = m_pMultiSellList->find(m_pMultiSellList->m_nLastInfoId);
          if (info != nullptr) {
              info->m_nTaxBase = grammar::SemanticInt32($10);
              ++m_pMultiSellList->m_nLastInfoId;
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Cannot find price multisell[%d] in line [%d]",
              //     m_pMultiSellList->m_nLastInfoId,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Cannot find price multisell[%d] in line [%d]",
              //     m_pMultiSellList->m_nLastInfoId,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "Cannot set tax for multisell entry %d at script line %lld: its price record is missing; tax and entry "
                  "index remain unchanged.",
                  m_pMultiSellList->m_nLastInfoId, static_cast<long long>(m_lexer.line) + 1
              );
          }
      }
    ;

n11
    : n11 ';' '{' name_def_2 '}'
    | '{' name_def_2 '}'
    ;

name_def_2
    : NAME ';' INTEGER
      {
          const auto ensureInfo = [this]() {
              CMultiSellInfo* info = m_pMultiSellList->find(m_pMultiSellList->m_nLastInfoId);
              if (info == nullptr) {
                  info = new CMultiSellInfo;
                  m_pMultiSellList->Add(info);
              }
              return info;
          };
          CMultiSellInfo* const info = ensureInfo();
          char16_t* const itemName = SemanticText($1);
          auto* const item = new MultiSellItemStruct;
          item->ItemClassId = FindItemClassId(itemName);
          item->Count = grammar::SemanticInt32($3);
          item->Probability = 100.0;
          item->nEnchant = -1;
          info->m_vOutputList.push_back(item);
          std::free(itemName);
      }
    | NAME ';' INTEGER ';' integer_def
      {
          const auto ensureInfo = [this]() {
              CMultiSellInfo* info = m_pMultiSellList->find(m_pMultiSellList->m_nLastInfoId);
              if (info == nullptr) {
                  info = new CMultiSellInfo;
                  m_pMultiSellList->Add(info);
              }
              return info;
          };
          CMultiSellInfo* const info = ensureInfo();
          char16_t* const itemName = SemanticText($1);
          auto* const item = new MultiSellItemStruct;
          item->ItemClassId = FindItemClassId(itemName);
          item->Count = grammar::SemanticInt32($3);
          item->Probability = grammar::SemanticDouble($5);
          item->nEnchant = -1;
          info->m_vOutputList.push_back(item);
          m_pMultiSellList->m_nType = 1;
          std::free(itemName);
      }
    | NAME ';' INTEGER ';' integer_def ';' INTEGER
      {
          const auto ensureInfo = [this]() {
              CMultiSellInfo* info = m_pMultiSellList->find(m_pMultiSellList->m_nLastInfoId);
              if (info == nullptr) {
                  info = new CMultiSellInfo;
                  m_pMultiSellList->Add(info);
              }
              return info;
          };
          CMultiSellInfo* const info = ensureInfo();
          char16_t* const itemName = SemanticText($1);
          auto* const item = new MultiSellItemStruct;
          item->ItemClassId = FindItemClassId(itemName);
          item->Count = grammar::SemanticInt32($3);
          item->Probability = grammar::SemanticDouble($5);
          item->nEnchant = grammar::SemanticInt32($7);
          info->m_vOutputList.push_back(item);
          m_pMultiSellList->m_nType = 1;
          std::free(itemName);
      }
    ;

n13
    : n13 ';' '{' name_def_3 '}'
    | '{' name_def_3 '}'
    ;

name_def_3
    : NAME ';' INTEGER
      {
          const auto ensureInfo = [this]() {
              CMultiSellInfo* info = m_pMultiSellList->find(m_pMultiSellList->m_nLastInfoId);
              if (info == nullptr) {
                  info = new CMultiSellInfo;
                  m_pMultiSellList->Add(info);
              }
              return info;
          };
          CMultiSellInfo* const info = ensureInfo();
          char16_t* const itemName = SemanticText($1);
          auto* const item = new MultiSellItemStruct;
          item->ItemClassId = FindItemClassId(itemName);
          item->Count = grammar::SemanticInt64($3);
          if (item->ItemClassId == -200) {
              item->Count = 0;
          }
          if (item->Count > 999999999999LL) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"itemclassid[%d]'s amount is invalid.[%I64d], in line [%d]",
              //     item->ItemClassId,
              //     item->Count,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"itemclassid[%d]'s amount is invalid.[%I64d], in line [%d]",
              //     item->ItemClassId,
              //     item->Count,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log, "Multisell input item class %d has count %lld at script line %lld; limiting it to 999999999999.",
                  item->ItemClassId, static_cast<long long>(item->Count), static_cast<long long>(m_lexer.line) + 1
              );
              item->Count = 999999999999LL;
          }
          info->m_vInputList.push_back(item);
          std::free(itemName);
      }
    ;

integer_def
    : INTEGER
      {
          const double value = static_cast<double>(grammar::SemanticInt64($1));
          std::memcpy(&$$, &value, sizeof(value));
      }
    | FLOAT_NUMBER
      { $$ = $1; }
    ;

%%
