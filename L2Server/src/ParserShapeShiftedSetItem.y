// CParserForShapeShiftedSetItem grammar.
// Bison generates ParserShapeShiftedSetItem.cpp and ParserShapeShiftedSetItemTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserShapeShiftedSetItem.h"

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

#include "ParserShapeShiftedSetItemTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForShapeShiftedSetItem::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserShapeShiftedSetItemTokens.h"}
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

void SetShapeShiftedSlotIfRecognized(ShapeShiftedSlotItemEntry& entry, const char16_t* value) {
    const std::u16string_view name(value == nullptr ? u"" : value);
    if (name == u"chest") {
        entry.slot = ST_CHEST;
    } else if (name == u"head") {
        entry.slot = ST_HEAD;
    } else if (name == u"gloves") {
        entry.slot = ST_GLOVES;
    } else if (name == u"legs") {
        entry.slot = ST_LEGS;
    } else if (name == u"feet") {
        entry.slot = ST_FEET;
    } else if (name == u"rhand") {
        entry.slot = ST_RHAND;
    } else if (name == u"lhand") {
        entry.slot = ST_LHAND;
    } else if (name == u"rlhand") {
        entry.slot = ST_RLHAND;
    } else if (name == u"hairall") {
        entry.slot = ST_HAIRALL;
    }
}

} // namespace

CParserForShapeShiftedSetItem::CParserForShapeShiftedSetItem() : m_lexer() {}

CParserForShapeShiftedSetItem::~CParserForShapeShiftedSetItem() = default;

int CParserForShapeShiftedSetItem::Create(CObjectDB* objectDb, char16_t* fileBuffer, int fileSize) {
    m_pObjDB = objectDb;
    return m_lexer.Create(fileBuffer, fileSize);
}

void CParserForShapeShiftedSetItem::yysyntaxerror() {
    char16_t message[76]{};
    FormatUtf16To(message, std::size(message), u"ShapeShiftedSetItemDataChecker[%d] : syntax error", m_lexer.line + 1);
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

%token SHAPESHIFTED_SETITEM_BEGIN
%token SHAPESHIFTED_SETITEM_END
%token SHAPESHIFTED_SETITEM_SLOT
%token SHAPESHIFTED_SETITEM_SKILL
%token INTEGER
%token NAME
// The original grammar has these conflicts. Bison resolves them as the
// original tables did. A shift wins over a reduce.
%expect 1
%start shapeshifted_setitem_list

%%

shapeshifted_setitem_list
    : shapeshifted_setitem_list shapeshifted_setitem
    | shapeshifted_setitem
    | %empty
    ;

shapeshifted_setitem
    : SHAPESHIFTED_SETITEM_BEGIN integer_def shapeshifted_setitem_slot_def_list SHAPESHIFTED_SETITEM_END
      { m_pObjDB->SetRegisterSetObject(m_pShapeShiftedSetItem); }
    ;

integer_def
    : INTEGER
      {
          m_pShapeShiftedSetItem = new CShapeShiftedSetItem;
          if (m_pShapeShiftedSetItem != nullptr) {
              m_pShapeShiftedSetItem->Set(grammar::SemanticInt32($1));
          }
      }
    ;

shapeshifted_setitem_slot_def_list
    : shapeshifted_setitem_slot_def_list shapeshifted_setitem_slot_def
    | %empty
    ;

shapeshifted_setitem_slot_def
    : SHAPESHIFTED_SETITEM_SLOT '=' '{' n6 '}'
      {
          auto* const slots = grammar::SemanticPointer<ShapeShiftedSetItemSlotMap>($4);
          m_pShapeShiftedSetItem->SetSlotTypeItemId(*slots);
      }
    | SHAPESHIFTED_SETITEM_SKILL '=' NAME
      { m_pShapeShiftedSetItem->SetSkill(grammar::SemanticPointer<char16_t>($3)); }
    ;

n6
    : n6 ';' n7
      {
          auto* const slots = grammar::SemanticPointer<ShapeShiftedSetItemSlotMap>($1);
          auto* const entry = grammar::SemanticPointer<ShapeShiftedSlotItemEntry>($3);
          slots->emplace(entry->slot, entry->itemIds);
          $$ = grammar::PointerSemantic(slots);
      }
    | n7
      {
          auto* const slots = new ShapeShiftedSetItemSlotMap;
          auto* const entry = grammar::SemanticPointer<ShapeShiftedSlotItemEntry>($1);
          slots->emplace(entry->slot, entry->itemIds);
          $$ = grammar::PointerSemantic(slots);
      }
    ;

n7
    : '{' NAME ';' '{' n8 '}' '}'
      {
          auto* const entry = new ShapeShiftedSlotItemEntry;
          SetShapeShiftedSlotIfRecognized(*entry, grammar::SemanticPointer<char16_t>($2));
          auto* const itemIds = grammar::SemanticPointer<std::vector<int>>($5);
          entry->itemIds = *itemIds;
          m_pShapeShiftedSetItem->SetSlotItemToSetID(itemIds);
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
