// CParserForSetItem grammar (original L2Server/parser_setitem.y).
// Bison generates ParserSetItem.cpp and ParserSetItemTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserSetItem.h"

#include "L2Server/src/Item.h"
#include "L2Server/src/ObjectDB.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string_view>
#include <utility>

#include "ParserSetItemTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForSetItem::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserSetItemTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

using SlotItemEntry = std::pair<SlotType, std::vector<int>>;

// Current action 8 allocates this complete target entry, then constructs only
// the xvector at +0x08. The SlotType is left indeterminate
// unless one of the nine machine-recognized names matches. In particular,
// assigning ST_ALLDRESS for the shipped `alldress` spelling would be an
// invented correction.
struct ShapeShiftedSlotItemEntry {
    SlotType slot;
    std::uint32_t padding;
    std::vector<int> itemIds;

    ShapeShiftedSlotItemEntry() : itemIds() {}
};

SlotType ParseSetItemSlot(const char16_t* value) {
    const std::u16string_view name(value == nullptr ? u"" : value);
    // Source bug: the original left slot uninitialized for an unknown name.
    // SlotType slot;
    SlotType slot = ST_NONE;
    if (name == u"chest") {
        slot = ST_CHEST;
    } else if (name == u"head") {
        slot = ST_HEAD;
    } else if (name == u"gloves") {
        slot = ST_GLOVES;
    } else if (name == u"legs") {
        slot = ST_LEGS;
    } else if (name == u"feet") {
        slot = ST_FEET;
    }
    return slot;
}

} // namespace

CParserForSetItem::CParserForSetItem() : m_lexer() {}

CParserForSetItem::~CParserForSetItem() = default;

int CParserForSetItem::Create(CObjectDB* objectDb, char16_t* fileBuffer, int fileSize) {
    m_pObjDB = objectDb;
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForSetItem::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"SetItemDataChecker[%d] : syntax error", m_lexer.line + 1);
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

%token SETITEM_BEGIN
%token SETITEM_END
%token SETITEM_ENCHANT_NUM
%token SETITEM_MAX_PARTS_NUM
%token SETITEM_SLOT
%token SETITEM_SKILL
%token SETITEM_ADDITIONAL_SLOT
%token SETITEM_ADDITIONAL_SKILL
%token SETITEM_CHEST_ADDITIONAL_SKILL
%token SETITEM_ENCHANTED_SKILL
%token SETITEM_ENCHANTED_SKILL2
%token SETITEM_ENCHANTED_SKILL3
%token SETITEM_ENCHANTED_SKILL4
%token SETITEM_ENCHANTED_SKILL5
%token INTEGER
%token NAME
%start setitem_list

%%

setitem_list
    : setitem_list setitem
    | setitem
    ;

setitem
    : SETITEM_BEGIN integer_def setitem_enchant_num_def_list SETITEM_END
      { m_pObjDB->SetRegisterSetObject(m_pNewSetItem); }
    ;

integer_def
    : INTEGER
      {
          m_pNewSetItem = new CNewSetItem;
          if (m_pNewSetItem != nullptr) {
              m_pNewSetItem->Set(grammar::SemanticInt32($1));
          }
      }
    ;

setitem_enchant_num_def_list
    : setitem_enchant_num_def_list setitem_enchant_num_def
    | %empty
    ;

setitem_enchant_num_def
    : SETITEM_ENCHANT_NUM '=' INTEGER
      { m_pNewSetItem->m_nEnchantNum = grammar::SemanticInt32($3); }
    | SETITEM_MAX_PARTS_NUM '=' INTEGER
      { m_pNewSetItem->m_nMaxPartsNum = grammar::SemanticInt32($3); }
    | SETITEM_SLOT '=' '{' n6 '}'
      {
          auto* const slots = grammar::SemanticPointer<NewSetItemSlotMap>($4);
          m_pNewSetItem->SetSlotTypeItemId(*slots);
      }
    | SETITEM_SKILL '=' NAME
      { m_pNewSetItem->SetSkill(grammar::SemanticPointer<char16_t>($3)); }
    | SETITEM_ADDITIONAL_SLOT '=' '{' n8 '}'
      {
          m_pNewSetItem->SetAdditionalItemId(grammar::SemanticPointer<std::vector<int>>($4));
      }
    | SETITEM_ADDITIONAL_SKILL '=' NAME
      { m_pNewSetItem->SetAdditionalSkill(grammar::SemanticPointer<char16_t>($3)); }
    | SETITEM_ENCHANTED_SKILL '=' NAME
      { m_pNewSetItem->AddEnchantSkill(6, grammar::SemanticPointer<char16_t>($3)); }
    | SETITEM_ENCHANTED_SKILL2 '=' NAME
      { m_pNewSetItem->AddEnchantSkill(7, grammar::SemanticPointer<char16_t>($3)); }
    | SETITEM_ENCHANTED_SKILL3 '=' NAME
      { m_pNewSetItem->AddEnchantSkill(8, grammar::SemanticPointer<char16_t>($3)); }
    | SETITEM_ENCHANTED_SKILL4 '=' NAME
      { m_pNewSetItem->AddEnchantSkill(9, grammar::SemanticPointer<char16_t>($3)); }
    | SETITEM_ENCHANTED_SKILL5 '=' NAME
      { m_pNewSetItem->AddEnchantSkill(10, grammar::SemanticPointer<char16_t>($3)); }
    | SETITEM_CHEST_ADDITIONAL_SKILL '=' NAME
      { m_pNewSetItem->SetAdditional3Skill(grammar::SemanticPointer<char16_t>($3)); }
    ;

n6
    : n6 ';' n7
      {
          auto* const slots = grammar::SemanticPointer<NewSetItemSlotMap>($1);
          auto* const entry = grammar::SemanticPointer<SlotItemEntry>($3);
          slots->emplace(entry->first, entry->second);
          $$ = grammar::PointerSemantic(slots);
      }
    | n7
      {
          auto* const slots = new NewSetItemSlotMap;
          auto* const entry = grammar::SemanticPointer<SlotItemEntry>($1);
          slots->emplace(entry->first, entry->second);
          $$ = grammar::PointerSemantic(slots);
      }
    ;

n7
    : '{' NAME ';' '{' n8 '}' '}'
      {
          const SlotType slot = ParseSetItemSlot(grammar::SemanticPointer<char16_t>($2));
          auto* const itemIds = grammar::SemanticPointer<std::vector<int>>($5);
          auto* const entry = new SlotItemEntry(slot, *itemIds);
          m_pNewSetItem->SetSlotItemToSetID(itemIds);
          $$ = grammar::PointerSemantic(entry);
      }
    ;

n8
    : n8 ';' INTEGER
      {
          auto* const values = grammar::SemanticPointer<std::vector<int>>($1);
          values->push_back(grammar::SemanticInt32($3));
          $$ = grammar::PointerSemantic(values);
      }
    | INTEGER
      {
          auto* const values = new std::vector<int>;
          values->push_back(grammar::SemanticInt32($1));
          $$ = grammar::PointerSemantic(values);
      }
    ;

%%
