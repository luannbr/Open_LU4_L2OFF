// CParserForProduct grammar (original L2Server/parser_product.y).
// Bison generates ParserProduct.cpp and ParserProductTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/ParserProduct.h"
#include "Shared/src/TextRuntime.h"

#include "L2Server/src/BRShopServer.h"
#include "L2Server/src/Item.h"
#include "L2Server/src/Object.h"
#include "L2Server/src/ObjectDB.h"
#include "Shared/src/MemorySub.h"

#include "L2Server/src/Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <string_view>

#include "ParserProductTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForProduct::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"ParserProductTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

bool EqualsAsciiInsensitive(const char16_t* value, std::u16string_view expected) {
    if (value == nullptr) {
        return false;
    }
    std::size_t index = 0;
    for (; index < expected.size() && value[index] != u'\0'; ++index) {
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
    }
    return index == expected.size() && value[index] == u'\0';
}

} // namespace

CParserForProduct::CParserForProduct() : m_product(nullptr), m_lexer() {}

CParserForProduct::~CParserForProduct() = default;

int CParserForProduct::create() {
    return 1;
}
}

%token EQUAL
%token COLON
%token SLASH
%token TILDE
%token PRODUCT_BEGIN
%token ID
%token NAME
%token CATEGORY
%token PRICE
%token ITEM_PANEL_TYPE
%token IS_EVENT_PRODUCT
%token IS_BEST_PRODUCT
%token IS_NEW_PRODUCT
%token IS_FEATURED_PRODUCT
%token IS_ADVERTISING
%token IS_RECOMMEND
%token IS_POPULAR
%token BUYABLE
%token BUY_CONDITION
%token SALE_START_DATE
%token SALE_END_DATE
%token LOCATION_ID
%token ITEMS
%token ITEMIDS
%token SERVER_TYPE
%token SALE_PERCENT
%token DAY_OF_WEEK
%token TIME_OF_DAY
%token STOCK
%token MIN_LEVEL
%token MAX_LEVEL
%token BIRTHDAY_START
%token BIRTHDAY_END
%token SALE_LIMIT
%token PRODUCT_END
%token NAME_2
%token IDENTIFIER
%token INTEGER
%token FLOAT_NUMBER
%start n1

%%

n1
    : product_list
    ;

product_list
    : product_list product
    | %empty
    ;

product
    : PRODUCT_BEGIN ID EQUAL INTEGER
      {
          m_product = new CProduct(
              grammar::SemanticInt32($4), u"", 0, 0, 0, false, false, false, false, u"", m_saleStartDate, m_saleEndDate,
              0, -1, u""
          );
      }
      category_def_list PRODUCT_END
      {
          m_product->SetSaleStartDate(m_saleStartDate);
          m_product->SetSaleEndDate(m_saleEndDate);
          if (m_product->m_location == g_nCountryCode) {
              g_ShopServer.PushProduct(m_product);
          } else {
              L2_RELEASE(m_product, ObjectRefType::Anonymous);
          }
          m_product = nullptr;
      }
    ;

category_def_list
    : category_def_list category_def
    | %empty
    ;

category_def
    : CATEGORY EQUAL INTEGER
      { m_product->SetCategory(grammar::SemanticInt32($3)); }
    | NAME EQUAL '{' IDENTIFIER '}'
      {
          m_product->SetName(grammar::SemanticString($4));
      }
    | PRICE EQUAL '{' integer_def_8 '}'
    | ITEM_PANEL_TYPE EQUAL INTEGER
      { m_product->SetItemPanelType(grammar::SemanticInt32($3)); }
    | IS_EVENT_PRODUCT EQUAL INTEGER
    | IS_BEST_PRODUCT EQUAL INTEGER
    | IS_NEW_PRODUCT EQUAL INTEGER
    | IS_FEATURED_PRODUCT EQUAL INTEGER
    | IS_ADVERTISING EQUAL INTEGER
      { m_product->SetEventProduct(grammar::SemanticInt32($3) != 0); }
    | IS_RECOMMEND EQUAL INTEGER
      { m_product->SetBestProduct(grammar::SemanticInt32($3) != 0); }
    | IS_POPULAR EQUAL INTEGER
      { m_product->SetNewProduct(grammar::SemanticInt32($3) != 0); }
    | BUYABLE EQUAL INTEGER
      { m_product->SetBuyable(grammar::SemanticInt32($3) != 0); }
    | BUY_CONDITION EQUAL '{' identifier_def '}'
    | SALE_START_DATE EQUAL '{' integer_def integer_def_2 '}'
    | SALE_END_DATE EQUAL '{' integer_def_3 integer_def_4 '}'
    | LOCATION_ID EQUAL INTEGER
      { m_product->SetLocation(grammar::SemanticInt32($3)); }
    | SERVER_TYPE EQUAL IDENTIFIER
      {
          char16_t* const type = grammar::SemanticString($3);
          if (EqualsAsciiInsensitive(type, u"free")) {
              m_product->SetServerType(0);
          } else if (EqualsAsciiInsensitive(type, u"pay")) {
              m_product->SetServerType(1);
          } else {
              m_product->SetServerType(-1);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Product Has Invalid ServerType[%s] in line [%d]",
              //     hostType.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Product Has Invalid ServerType[%s] in line [%d]",
              //     hostType.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "ProductData line %lld has unsupported server type '%s'; using -1 and continuing. Expected free or pay.",
                  static_cast<long long>(m_lexer.line) + 1,
                  type != nullptr ? Utf16ToUtf8(std::u16string_view(type)).c_str() : "<null>"
              );
          }
      }
    | ITEMS EQUAL '{' n12 '}'
    | ITEMIDS EQUAL '{' n13 '}'
    | DAY_OF_WEEK EQUAL '{' integer_def_6 '}'
    | TIME_OF_DAY EQUAL '{' integer_def_7 '}'
    | STOCK EQUAL INTEGER
      { m_product->SetStock(grammar::SemanticInt32($3)); }
    | SALE_PERCENT EQUAL INTEGER
      { m_product->SetSalePercent(grammar::SemanticInt32($3)); }
    | MIN_LEVEL EQUAL INTEGER
      { m_product->SetMinLevel(grammar::SemanticInt32($3)); }
    | MAX_LEVEL EQUAL INTEGER
      { m_product->SetMaxLevel(grammar::SemanticInt32($3)); }
    | BIRTHDAY_START EQUAL '{' INTEGER SLASH INTEGER SLASH INTEGER '}'
      {
          m_birthdayStart.wYear = static_cast<std::uint16_t>(grammar::SemanticInt32($4));
          m_birthdayStart.wMonth = static_cast<std::uint16_t>(grammar::SemanticInt32($6));
          m_birthdayStart.wDay = static_cast<std::uint16_t>(grammar::SemanticInt32($8));
          m_product->SetBirthdayStart(m_birthdayStart);
      }
    | BIRTHDAY_END EQUAL '{' INTEGER SLASH INTEGER SLASH INTEGER '}'
      {
          m_birthdayEnd.wYear = static_cast<std::uint16_t>(grammar::SemanticInt32($4));
          m_birthdayEnd.wMonth = static_cast<std::uint16_t>(grammar::SemanticInt32($6));
          m_birthdayEnd.wDay = static_cast<std::uint16_t>(grammar::SemanticInt32($8));
          m_product->SetBirthdayEnd(m_birthdayEnd);
      }
    | SALE_LIMIT EQUAL '{' IDENTIFIER ';' INTEGER '}'
      {
          char16_t* const type = grammar::SemanticString($4);
          int period = 0;
          if (EqualsAsciiInsensitive(type, u"day")) {
              period = 1;
          } else if (EqualsAsciiInsensitive(type, u"week")) {
              period = 7;
          } else if (EqualsAsciiInsensitive(type, u"month")) {
              period = 30;
          } else if (EqualsAsciiInsensitive(type, u"period")) {
              period = -1;
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Product Has Invalid sale_limit Type[%s] in line [%d]",
              //     hostType.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Product Has Invalid sale_limit Type[%s] in line [%d]",
              //     hostType.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "ProductData line %lld has unsupported sale-limit type '%s'; the sale limit is not updated. Expected "
                  "day, week, month or period.",
                  static_cast<long long>(m_lexer.line) + 1,
                  type != nullptr ? Utf16ToUtf8(std::u16string_view(type)).c_str() : "<null>"
              );
              break;
          }
          m_product->SetSaleLimit(period, grammar::SemanticInt32($6));
      }
    ;

integer_def
    : INTEGER SLASH INTEGER SLASH INTEGER
      {
          m_saleStartDate.wYear = static_cast<std::uint16_t>(grammar::SemanticInt32($1));
          m_saleStartDate.wMonth = static_cast<std::uint16_t>(grammar::SemanticInt32($3));
          m_saleStartDate.wDay = static_cast<std::uint16_t>(grammar::SemanticInt32($5));
      }
    ;

integer_def_2
    : INTEGER COLON INTEGER
      {
          m_saleStartDate.wHour = static_cast<std::uint16_t>(grammar::SemanticInt32($1));
          m_saleStartDate.wMinute = static_cast<std::uint16_t>(grammar::SemanticInt32($3));
          m_saleStartDate.wSecond = 0;
          m_saleStartDate.wMilliseconds = 0;
      }
    ;

integer_def_3
    : INTEGER SLASH INTEGER SLASH INTEGER
      {
          m_saleEndDate.wYear = static_cast<std::uint16_t>(grammar::SemanticInt32($1));
          m_saleEndDate.wMonth = static_cast<std::uint16_t>(grammar::SemanticInt32($3));
          m_saleEndDate.wDay = static_cast<std::uint16_t>(grammar::SemanticInt32($5));
      }
    ;

integer_def_4
    : INTEGER COLON INTEGER
      {
          m_saleEndDate.wHour = static_cast<std::uint16_t>(grammar::SemanticInt32($1));
          m_saleEndDate.wMinute = static_cast<std::uint16_t>(grammar::SemanticInt32($3));
          m_saleEndDate.wSecond = 0;
          m_saleEndDate.wMilliseconds = 0;
      }
    ;

identifier_def
    : IDENTIFIER
    | %empty
    ;

n12
    : n12 ';' '{' name_2_def '}'
    | '{' name_2_def '}'
    ;

n13
    : n13 ';' '{' integer_def_5 '}'
    | '{' integer_def_5 '}'
    ;

name_2_def
    : NAME_2 ';' INTEGER
      {
          char16_t* const itemName = grammar::SemanticString($1);
          CObject* const object = g_ObjectDB.GetObjectByName(itemName);
          if (object != nullptr && object->IsItem()) {
              CItem* const item = object->CastItem();
              m_product->AddItem(item->m_data->m_nClassID, grammar::SemanticInt32($3));
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined Item[%s] in line[%d]",
              //     hostName.c_str(),
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "ProductData line %lld references missing or non-item name '%s'; the item is not added to the product.",
                  static_cast<long long>(m_lexer.line) + 1,
                  itemName != nullptr ? Utf16ToUtf8(std::u16string_view(itemName)).c_str() : "<null>"
              );
          }
      }
    ;

integer_def_5
    : INTEGER ';' INTEGER
      {
          const int itemId = grammar::SemanticInt32($1);
          CObject* const object = g_ObjectDB.GetObjectW(itemId, false);
          if (object != nullptr && object->IsItem()) {
              m_product->AddItem(itemId, grammar::SemanticInt32($3));
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined Item[%d] in line[%d]",
              //     itemId,
              //     m_lexer.line + 1)
              // L2_LOG_ERROR(Log,
              //     L"Undefined Item[%d] in line[%d]",
              //     itemId,
              //     m_lexer.line + 1)
              L2_LOG_ERROR(
                  Log,
                  "ProductData line %lld references missing or non-item prototype %d; the item is not added to the "
                  "product.",
                  static_cast<long long>(m_lexer.line) + 1, itemId
              );
          }
      }
    ;

integer_def_6
    : INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER
      {
          m_product->SetDayOfWeek(
              grammar::SemanticInt32($1), grammar::SemanticInt32($3), grammar::SemanticInt32($5),
              grammar::SemanticInt32($7), grammar::SemanticInt32($9), grammar::SemanticInt32($11),
              grammar::SemanticInt32($13)
          );
      }
    ;

integer_def_7
    : INTEGER COLON INTEGER TILDE INTEGER COLON INTEGER
      {
          m_product->SetTimeOfDay(
              grammar::SemanticInt32($1), grammar::SemanticInt32($3), grammar::SemanticInt32($5),
              grammar::SemanticInt32($7)
          );
      }
    ;

integer_def_8
    : INTEGER ';' INTEGER
      { m_product->SetPrice(grammar::SemanticInt32($1), grammar::SemanticInt32($3)); }
    ;

// The original tables have rule 51 that no parser state reduces.
// It is left out.

%%
