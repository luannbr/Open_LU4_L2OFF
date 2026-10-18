// PetDataParser grammar (original L2Server/petdataparser.y).
// Bison generates PetDataParser.cpp and PetDataParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/PetDataParser.h"

#include "L2Server/src/PetDataLexer.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/TextRuntime.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "PetDataParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse PetDataParser::yyparse
#define yylex(value) mpLexer->Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"PetDataParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {

PetDataParser::PetDataParser() : mPetData(), mIntList(), intList(), m_NpcName(), m_PetItemName(), m_LastInsertedRecord() {
    mpLexer = nullptr;
    mpPetDataSheet = nullptr;
    mpResult = nullptr;
    mPetData.Init();
    m_pLastInsertedSheet = nullptr;
    m_LastInsertedRecord.Init();
}

PetDataParser::~PetDataParser() = default;

int PetDataParser::Create(PetDataLexer* lexer, PetDataSheetTable* result) {
    mpLexer = lexer;
    mpResult = result;
    return 1;
}

void PetDataParser::yysyntaxerror() {
    // Log.Add(
    //     LOG_ERROR,
    //     L"Error   : syntax error, line=%d, file=%s",
    //     mpLexer->yylineno,
    //     target_utf16::ToHostCodeUnitsBounded(
    //         mpLexer->GetFileName(), 132).c_str())
    // L2_LOG_ERROR(Log,
    //     L"Error   : syntax error, line=%d, file=%s",
    //     mpLexer->yylineno,
    //     target_utf16::ToHostCodeUnitsBounded(
    //         mpLexer->GetFileName(), 132).c_str())
    L2_LOG_ERROR(
        Log, "Pet data syntax error in '%s' at line %d.",
        Utf16ToUtf8(target_utf16::FromHostCodeUnits(target_utf16::ToHostCodeUnitsBounded(mpLexer->GetFileName(), 132)))
            .c_str(),
        mpLexer->yylineno
    );
}
}

%token PET_STAT_BEGIN
%token PET_STAT_END
%token STAT_BEGIN
%token STAT_END
%token PET_INDEX
%token NPC_NAME
%token EVOLVE
%token ITEM
%token SYNC_LEVEL
%token IDENTIFIER
%token NAME
%token INTEGER
%token FLOAT_NUMBER
%start pet_stat_list

%%

pet_stat_list
    : pet_stat_list pet_stat
    | %empty
    ;

pet_stat
    : PET_STAT_BEGIN
      {
          m_nNpcId = -1;
          m_ItemClassID = -1;
          m_bSyncLevel = false;
          intList.clear();
      }
      pet_index_def_list
      {
          if (mpResult != nullptr) {
              mpPetDataSheet = new PetDataSheet;
              mpPetDataSheet->SetName(m_NpcName.c_str());
              mpPetDataSheet->mNpcClassId = m_nNpcId;
              mpPetDataSheet->m_itemClassID = m_ItemClassID;
              mpPetDataSheet->m_Index = m_nPetIndex;
              mpPetDataSheet->m_bSyncLevel = m_bSyncLevel;
              if (!intList.empty()) {
                  mpPetDataSheet->SetEvolutionInfo(intList);
              }
              break;
          }
          // Log.Add(
          //     LOG_ERROR,
          //     L"Warning : mpResult == NULL '%s' invalid NPC name, line=%d, file=%s",
          //     target_utf16::ToHostCodeUnits(m_NpcName.c_str()).c_str(),
          //     mpLexer->yylineno,
          //     target_utf16::ToHostCodeUnitsBounded(
          //         mpLexer->GetFileName(), 132).c_str())
          // L2_LOG_ERROR(Log,
          //     L"Warning : mpResult == NULL '%s' invalid NPC name, line=%d, file=%s",
          //     target_utf16::ToHostCodeUnits(m_NpcName.c_str()).c_str(),
          //     mpLexer->yylineno,
          //     target_utf16::ToHostCodeUnitsBounded(
          //         mpLexer->GetFileName(), 132).c_str())
          L2_LOG_ERROR(
              Log, "Cannot create pet data sheet for NPC '%s' in '%s' at line %d: the result table is null.",
              Utf16ToUtf8(std::u16string_view(m_NpcName.c_str() != nullptr ? m_NpcName.c_str() : u"<null>")).c_str(),
              Utf16ToUtf8(
                  target_utf16::FromHostCodeUnits(target_utf16::ToHostCodeUnitsBounded(mpLexer->GetFileName(), 132))
              )
                  .c_str(),
              mpLexer->yylineno
          );
      }
      stat_list PET_STAT_END
      {
          if (mpResult != nullptr) {
              mpResult->Insert(mpPetDataSheet);
          }
          m_pLastInsertedSheet = mpPetDataSheet;
      }
    ;

pet_index_def_list
    : pet_index_def_list pet_index_def
    | %empty
    ;

pet_index_def
    : PET_INDEX '=' INTEGER
      {
          m_nPetIndex = static_cast<std::int32_t>($3);
          if (m_nPetIndex <= 0) {
              // Log.Add(LOG_ERROR, L"Invalid pet index")
              // L2_LOG_ERROR(Log, L"Invalid pet index")
              L2_LOG_ERROR(
                  Log, "Pet index %d in '%s' at line %d must be positive; the supplied index is retained.", m_nPetIndex,
                  Utf16ToUtf8(
                      target_utf16::FromHostCodeUnits(target_utf16::ToHostCodeUnitsBounded(mpLexer->GetFileName(), 132))
                  )
                      .c_str(),
                  mpLexer->yylineno
              );
          }
      }
    | NPC_NAME '=' NAME
      {
          char16_t* const name = grammar::SemanticString($3);
          m_NpcName.assign(name);
          m_nNpcId = static_cast<std::int32_t>(g_PrecompiledHeaderDB.Search(m_NpcName.c_str()));
          if (m_nNpcId < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Warning : '%s' invalid NPC name, line=%d, file=%s",
              //     target_utf16::ToHostCodeUnits(name).c_str(),
              //     mpLexer->yylineno,
              //     target_utf16::ToHostCodeUnitsBounded(
              //         mpLexer->GetFileName(), 132).c_str())
              // L2_LOG_ERROR(Log,
              //     L"Warning : '%s' invalid NPC name, line=%d, file=%s",
              //     target_utf16::ToHostCodeUnits(name).c_str(),
              //     mpLexer->yylineno,
              //     target_utf16::ToHostCodeUnitsBounded(
              //         mpLexer->GetFileName(), 132).c_str())
              L2_LOG_ERROR(
                  Log, "Pet NPC '%s' in '%s' at line %d has unresolved precompiled-header ID %d; the ID is retained.",
                  Utf16ToUtf8(std::u16string_view(name != nullptr ? name : u"<null>")).c_str(),
                  Utf16ToUtf8(
                      target_utf16::FromHostCodeUnits(target_utf16::ToHostCodeUnitsBounded(mpLexer->GetFileName(), 132))
                  )
                      .c_str(),
                  mpLexer->yylineno, m_nNpcId
              );
          }
      }
    | ITEM '=' NAME
      {
          char16_t* const name = grammar::SemanticString($3);
          if (*name == u'\0') {
              break;
          }
          m_PetItemName.assign(name);
          m_ItemClassID = static_cast<std::int32_t>(g_PrecompiledHeaderDB.Search(m_PetItemName.c_str()));
          if (m_ItemClassID < 0) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Warning : '%s' invalid item name, line=%d, file=%s",
              //     target_utf16::ToHostCodeUnits(name).c_str(),
              //     mpLexer->yylineno,
              //     target_utf16::ToHostCodeUnitsBounded(
              //         mpLexer->GetFileName(), 132).c_str())
              // L2_LOG_ERROR(Log,
              //     L"Warning : '%s' invalid item name, line=%d, file=%s",
              //     target_utf16::ToHostCodeUnits(name).c_str(),
              //     mpLexer->yylineno,
              //     target_utf16::ToHostCodeUnitsBounded(
              //         mpLexer->GetFileName(), 132).c_str())
              L2_LOG_ERROR(
                  Log, "Pet item '%s' in '%s' at line %d has unresolved precompiled-header ID %d; the ID is retained.",
                  Utf16ToUtf8(std::u16string_view(name != nullptr ? name : u"<null>")).c_str(),
                  Utf16ToUtf8(
                      target_utf16::FromHostCodeUnits(target_utf16::ToHostCodeUnitsBounded(mpLexer->GetFileName(), 132))
                  )
                      .c_str(),
                  mpLexer->yylineno, m_ItemClassID
              );
          }
      }
    | SYNC_LEVEL '=' INTEGER
      { m_bSyncLevel = $3 == 1; }
    | evolve_def
    ;

evolve_def
    : EVOLVE '=' '{' n8 '}'
    | EVOLVE '=' '{' '}'
    ;

n8
    : n8 ';' integer_def
    | integer_def
    ;

integer_def
    : INTEGER
      { intList.push_back(static_cast<std::int32_t>($1)); }
    ;

stat_list
    : stat_list stat
    | stat
    ;

stat
    : STAT_BEGIN identifier_def_list STAT_END
      {
          if (mpResult != nullptr && mpPetDataSheet != nullptr) {
              mpPetDataSheet->SetRecord(mPetData);
              std::memcpy(&m_LastInsertedRecord, &mPetData, sizeof(mPetData));
              mPetData.Init();
          }
      }
    ;

identifier_def_list
    : identifier_def_list identifier_def
    | identifier_def
    ;

identifier_def
    : IDENTIFIER '=' INTEGER
      {
          char16_t* const attribute = grammar::SemanticString($1);
          AttrTypeInfo* const info = PetDataSheet::GetTypeInfo(attribute);
          if (info != nullptr) {
              mPetData.Set(info->mAttr, static_cast<double>(static_cast<std::int64_t>($3)));
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Warning : '%s' unknown attribute, line=%d, file=%s",
              //     target_utf16::ToHostCodeUnits(attribute).c_str(),
              //     mpLexer->yylineno,
              //     target_utf16::ToHostCodeUnitsBounded(
              //         mpLexer->GetFileName(), 132).c_str())
              // L2_LOG_ERROR(Log,
              //     L"Warning : '%s' unknown attribute, line=%d, file=%s",
              //     target_utf16::ToHostCodeUnits(attribute).c_str(),
              //     mpLexer->yylineno,
              //     target_utf16::ToHostCodeUnitsBounded(
              //         mpLexer->GetFileName(), 132).c_str())
              L2_LOG_ERROR(
                  Log, "Unknown pet attribute '%s' in '%s' at line %d; the attribute assignment is skipped.",
                  Utf16ToUtf8(std::u16string_view(attribute != nullptr ? attribute : u"<null>")).c_str(),
                  Utf16ToUtf8(
                      target_utf16::FromHostCodeUnits(target_utf16::ToHostCodeUnitsBounded(mpLexer->GetFileName(), 132))
                  )
                      .c_str(),
                  mpLexer->yylineno
              );
          }
          std::free(attribute);
      }
    | IDENTIFIER '=' FLOAT_NUMBER
      {
          char16_t* const attribute = grammar::SemanticString($1);
          AttrTypeInfo* const info = PetDataSheet::GetTypeInfo(attribute);
          if (info != nullptr) {
              {
                  const grammar::SemanticBits bits = $3;
                  double value = 0.0;
                  static_assert(sizeof(value) == sizeof(bits));
                  std::memcpy(&value, &bits, sizeof(value));
                  mPetData.Set(info->mAttr, value);
              }
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Warning : '%s' unknown attribute, line=%d, file=%s",
              //     target_utf16::ToHostCodeUnits(attribute).c_str(),
              //     mpLexer->yylineno,
              //     target_utf16::ToHostCodeUnitsBounded(
              //         mpLexer->GetFileName(), 132).c_str())
              // L2_LOG_ERROR(Log,
              //     L"Warning : '%s' unknown attribute, line=%d, file=%s",
              //     target_utf16::ToHostCodeUnits(attribute).c_str(),
              //     mpLexer->yylineno,
              //     target_utf16::ToHostCodeUnitsBounded(
              //         mpLexer->GetFileName(), 132).c_str())
              L2_LOG_ERROR(
                  Log, "Unknown pet attribute '%s' in '%s' at line %d; the attribute assignment is skipped.",
                  Utf16ToUtf8(std::u16string_view(attribute != nullptr ? attribute : u"<null>")).c_str(),
                  Utf16ToUtf8(
                      target_utf16::FromHostCodeUnits(target_utf16::ToHostCodeUnitsBounded(mpLexer->GetFileName(), 132))
                  )
                      .c_str(),
                  mpLexer->yylineno
              );
          }
          std::free(attribute);
      }
    | IDENTIFIER '=' n14
      {
          char16_t* const attribute = grammar::SemanticString($1);
          AttrTypeInfo* const info = PetDataSheet::GetTypeInfo(attribute);
          if (info != nullptr) {
              mPetData.Set(info->mAttr, mIntList);
          } else {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Warning : '%s' unknown attribute, line=%d, file=%s",
              //     target_utf16::ToHostCodeUnits(attribute).c_str(),
              //     mpLexer->yylineno,
              //     target_utf16::ToHostCodeUnitsBounded(
              //         mpLexer->GetFileName(), 132).c_str())
              // L2_LOG_ERROR(Log,
              //     L"Warning : '%s' unknown attribute, line=%d, file=%s",
              //     target_utf16::ToHostCodeUnits(attribute).c_str(),
              //     mpLexer->yylineno,
              //     target_utf16::ToHostCodeUnitsBounded(
              //         mpLexer->GetFileName(), 132).c_str())
              L2_LOG_ERROR(
                  Log, "Unknown pet attribute '%s' in '%s' at line %d; the attribute assignment is skipped.",
                  Utf16ToUtf8(std::u16string_view(attribute != nullptr ? attribute : u"<null>")).c_str(),
                  Utf16ToUtf8(
                      target_utf16::FromHostCodeUnits(target_utf16::ToHostCodeUnitsBounded(mpLexer->GetFileName(), 132))
                  )
                      .c_str(),
                  mpLexer->yylineno
              );
          }
          std::free(attribute);
      }
    ;

n14
    : '{'
      { mIntList.clear(); }
      n16 '}'
    ;

n16
    : n16 ';' INTEGER
      { mIntList.push_back(static_cast<std::int64_t>($3)); }
    | INTEGER
      { mIntList.push_back(static_cast<std::int64_t>($1)); }
    | %empty
    ;

%%
