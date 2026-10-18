// CParserForEnsoul grammar.
// Bison generates EnsoulParser.cpp and EnsoulParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2Server/src/EnsoulParser.h"

#include "L2Server/src/Item.h"
#include "L2Server/src/ObjectDB.h"

#include "Shared/src/Log.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>

#include "EnsoulParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForEnsoul::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, (void)(message), yysyntaxerror())
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"EnsoulParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

std::u16string MakePchLine(const char16_t* name, std::int32_t id) {
    const std::string digits = std::to_string(id);
    std::u16string line;
    line.reserve(std::char_traits<char16_t>::length(name) + digits.size() + 5);
    line.push_back(u'[');
    line.append(name);
    line.append(u"]\t");
    for (const char digit : digits) {
        line.push_back(static_cast<char16_t>(digit));
    }
    line.append(u"\r\n");
    return line;
}

} // namespace

CParserForEnsoul::CParserForEnsoul() :
    m_lexer(),
    m_nLine(0),
    m_bOnlyPrecompiledHeader(false),
    m_bReload(false),
    m_vEnsoulOptionData(),
    m_pEnsoulFeeItemData(nullptr),
    m_vEnsoulFeeItemData(),
    m_pEnsoulFeeData(nullptr),
    m_pEnsoulOptionData(nullptr),
    m_pEnsoulStoneData(nullptr),
    m_pEmptyEnsoulOptionData(new EnsoulOptionData) {
    m_vEnsoulOptionData.clear();
    m_vEnsoulFeeItemData.clear();
    g_mEnsoulOptionData.clear();
    g_mEnsoulOptionData.emplace(0, m_pEmptyEnsoulOptionData);
}

CParserForEnsoul::~CParserForEnsoul() = default;

int CParserForEnsoul::Create(char16_t* fileBuffer, int fileSize) {
    if (!m_lexer.Create(this, fileBuffer, fileSize)) {
        return 0;
    }
    m_lexer.Initialize();
    return 1;
}

void CParserForEnsoul::yysyntaxerror() {
    char16_t message[60]{};
    FormatUtf16To(message, std::size(message), u"Ensoul Checker[%d] : syntax error", m_lexer.GetLine() + 1);
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

%token ENSOUL_OPTION_BEGIN
%token ENSOUL_OPTION_END
%token ENSOUL_STONE_BEGIN
%token ENSOUL_STONE_END
%token ENSOUL_FEE_BEGIN
%token ENSOUL_FEE_END
%token OPTION_ID
%token OPTION_NAME
%token OPTION_TYPE
%token OPTION_SKILL_LIST
%token EXTRACTION_STONE_NAME
%token STONE_NAME
%token SLOT_TYPE
%token NORMAL_BM
%token ENSOUL_OPTION_LIST
%token CRYSTAL_TYPE
%token ENSOUL_FEE_NORMAL
%token ENSOUL_FEE_BM
%token ENSOUL_REFEE_NORMAL
%token ENSOUL_REFEE_BM
%token EXTRACTION_FEE_NORMAL
%token EXTRACTION_FEE_BM
%token IDENTIFIER
%token INTEGER
%start n1

%%

n1
    : ensoul_option_list ensoul_stone_list ensoul_fee_list
    ;

ensoul_option_list
    : ensoul_option_list ensoul_option
      {
          if (!m_bOnlyPrecompiledHeader) {
              g_mEnsoulOptionData.emplace(m_pEnsoulOptionData->m_nId, m_pEnsoulOptionData);
          }
      }
    | ensoul_option
      {
          if (!m_bOnlyPrecompiledHeader) {
              g_mEnsoulOptionData.emplace(m_pEnsoulOptionData->m_nId, m_pEnsoulOptionData);
          }
      }
    ;

ensoul_option
    : ENSOUL_OPTION_BEGIN option_type_def n8 ENSOUL_OPTION_END
    ;

option_type_def
    : OPTION_TYPE '=' IDENTIFIER
      {
          m_pEnsoulOptionData = new EnsoulOptionData;
          const char16_t* const name = grammar::SemanticString($3);
          const std::wstring hostName = Utf16ToWide(name);
          m_pEnsoulOptionData->m_optionType = EnsoulOptionData::GetEnsoulOptionType(hostName.c_str());
          if (m_pEnsoulOptionData->m_optionType == EnsoulItem::EOT_INVALID) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined OptionType name[%s] in line [%d]",
              //     hostName.c_str(),
              //     m_lexer.GetLine() + 1)
              L2_LOG_ERROR(Log, u"Undefined OptionType name[%s] in line [%d]", hostName.c_str(), m_lexer.GetLine() + 1);
          }
      }
      OPTION_ID '=' INTEGER
      { m_pEnsoulOptionData->m_nId = grammar::SemanticInt32($7); }
      OPTION_NAME '=' IDENTIFIER
      {
          if (m_bOnlyPrecompiledHeader) {
              std::u16string line = MakePchLine(grammar::SemanticString($11), grammar::SemanticInt32($7));
              g_EnsoulManager.Uniputs(line.data());
          }
      }
      OPTION_SKILL_LIST '=' '{' n10 '}'
      {
          if (!m_bOnlyPrecompiledHeader) {
              m_pEnsoulOptionData->SetEnsoulOptionSkills(m_vEnsoulOptionData);
          }
      }
    ;

n8
    : extraction_stone_name_def
    | %empty
    ;

extraction_stone_name_def
    : EXTRACTION_STONE_NAME '=' IDENTIFIER
      {
          const char16_t* const name = grammar::SemanticString($3);
          std::int32_t classId = 0;
          if (std::u16string_view(name) != u"none") {
              classId = static_cast<std::int32_t>(g_PrecompiledHeaderDB.Search(name));
              if (classId == -1 && !m_bOnlyPrecompiledHeader) {
                  const std::wstring hostName = Utf16ToWide(name);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined Extraction StoneItem name[%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.GetLine() + 1)
                  L2_LOG_ERROR(
                      Log, u"Undefined Extraction StoneItem name[%s] in line [%d]", hostName.c_str(), m_lexer.GetLine() + 1
                  );
              }
              CObject* const object = g_ObjectDB.GetObjectW(classId, false);
              if (object == nullptr || !object->IsItem()) {
                  const std::wstring hostName = Utf16ToWide(name);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined Extraction StoneItem name[%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.GetLine() + 1)
                  L2_LOG_ERROR(
                      Log, u"Undefined Extraction StoneItem name[%s] in line [%d]", hostName.c_str(), m_lexer.GetLine() + 1
                  );
              }
              CItem* const item = static_cast<CItem*>(object);
              if (item->m_const->m_EtcitemType != 0x3c) {
                  const std::wstring hostName = Utf16ToWide(name);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined Extraction StoneItem name[%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.GetLine() + 1)
                  L2_LOG_ERROR(
                      Log, u"Undefined Extraction StoneItem name[%s] in line [%d]", hostName.c_str(), m_lexer.GetLine() + 1
                  );
              }
          }
          m_pEnsoulOptionData->SetExtractionStoneClassId(classId);
      }
    ;

n10
    : n10 ';' IDENTIFIER
      {
          const auto search = [this](const grammar::SemanticBits value) {
              const char16_t* const name = grammar::SemanticString(value);
              const std::int64_t id = g_PrecompiledHeaderDB.Search(name);
              if (id == -1 && !m_bOnlyPrecompiledHeader) {
                  const std::wstring hostName = Utf16ToWide(name);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill name[%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.GetLine() + 1)
                  L2_LOG_ERROR(Log, u"Undefined skill name[%s] in line [%d]", hostName.c_str(), m_lexer.GetLine() + 1);
              }
              return id;
          };
          m_vEnsoulOptionData.push_back(search($3));
      }
    | IDENTIFIER
      {
          const auto search = [this](const grammar::SemanticBits value) {
              const char16_t* const name = grammar::SemanticString(value);
              const std::int64_t id = g_PrecompiledHeaderDB.Search(name);
              if (id == -1 && !m_bOnlyPrecompiledHeader) {
                  const std::wstring hostName = Utf16ToWide(name);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill name[%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.GetLine() + 1)
                  L2_LOG_ERROR(Log, u"Undefined skill name[%s] in line [%d]", hostName.c_str(), m_lexer.GetLine() + 1);
              }
              return id;
          };
          m_vEnsoulOptionData.clear();
          m_vEnsoulOptionData.push_back(search($1));
      }
    ;

ensoul_stone_list
    : ensoul_stone_list ensoul_stone
      {
          if (!m_bOnlyPrecompiledHeader) {
              g_mEnsoulStoneData.emplace(m_pEnsoulStoneData->m_nItemType, m_pEnsoulStoneData);
          }
      }
    | ensoul_stone
      {
          if (!m_bOnlyPrecompiledHeader) {
              g_mEnsoulStoneData.clear();
              g_mEnsoulStoneData.emplace(m_pEnsoulStoneData->m_nItemType, m_pEnsoulStoneData);
          }
      }
    ;

ensoul_stone
    : ENSOUL_STONE_BEGIN STONE_NAME '=' IDENTIFIER
      {
          m_pEnsoulStoneData = new EnsoulStoneData;
          const char16_t* const name = grammar::SemanticString($4);
          const std::int64_t classId = g_PrecompiledHeaderDB.Search(name);
          if (classId == -1 && !m_bOnlyPrecompiledHeader) {
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined StoneItem name[%s] in line [%d]",
              //     hostName.c_str(),
              //     m_lexer.GetLine() + 1)
              L2_LOG_ERROR(Log, u"Undefined StoneItem name[%s] in line [%d]", hostName.c_str(), m_lexer.GetLine() + 1);
          }
          CObject* const object = g_ObjectDB.GetObjectW(static_cast<std::int32_t>(classId), false);
          if (object == nullptr || !object->IsItem()) {
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined StoneItem name[%s] in line [%d]",
              //     hostName.c_str(),
              //     m_lexer.GetLine() + 1)
              L2_LOG_ERROR(Log, u"Undefined StoneItem name[%s] in line [%d]", hostName.c_str(), m_lexer.GetLine() + 1);
          }
          CItem* const item = static_cast<CItem*>(object);
          if (item->m_const->m_EtcitemType != 0x3c) {
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined StoneItem name[%s] in line [%d]",
              //     hostName.c_str(),
              //     m_lexer.GetLine() + 1)
              L2_LOG_ERROR(Log, u"Undefined StoneItem name[%s] in line [%d]", hostName.c_str(), m_lexer.GetLine() + 1);
          }
          m_pEnsoulStoneData->m_nItemType = classId;
      }
      SLOT_TYPE '=' NORMAL_BM
      {
          m_pEnsoulStoneData->m_slotType = static_cast<EnsoulItem::EnsoulItemSlotType>(grammar::SemanticInt32($8));
      }
      ENSOUL_OPTION_LIST '=' '{' n16 '}'
      { m_pEnsoulStoneData->SetEnsoulOptions(m_vEnsoulOptionData); }
      ENSOUL_STONE_END
    ;

n16
    : n16 ';' IDENTIFIER
      {
          const auto search = [this](const grammar::SemanticBits value) {
              const char16_t* const name = grammar::SemanticString(value);
              const std::int64_t id = g_PrecompiledHeaderDB.Search(name);
              if (id == -1 && !m_bOnlyPrecompiledHeader) {
                  const std::wstring hostName = Utf16ToWide(name);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill name[%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.GetLine() + 1)
                  L2_LOG_ERROR(Log, u"Undefined skill name[%s] in line [%d]", hostName.c_str(), m_lexer.GetLine() + 1);
              }
              return id;
          };
          m_vEnsoulOptionData.push_back(search($3));
      }
    | IDENTIFIER
      {
          const auto search = [this](const grammar::SemanticBits value) {
              const char16_t* const name = grammar::SemanticString(value);
              const std::int64_t id = g_PrecompiledHeaderDB.Search(name);
              if (id == -1 && !m_bOnlyPrecompiledHeader) {
                  const std::wstring hostName = Utf16ToWide(name);
                  // Log.Add(
                  //     LOG_ERROR,
                  //     L"Undefined skill name[%s] in line [%d]",
                  //     hostName.c_str(),
                  //     m_lexer.GetLine() + 1)
                  L2_LOG_ERROR(Log, u"Undefined skill name[%s] in line [%d]", hostName.c_str(), m_lexer.GetLine() + 1);
              }
              return id;
          };
          m_vEnsoulOptionData.clear();
          m_vEnsoulOptionData.push_back(search($1));
      }
    ;

ensoul_fee_list
    : ensoul_fee_list ensoul_fee
      {
          if (!m_bOnlyPrecompiledHeader) {
              g_EnsoulFeeData.emplace(m_pEnsoulFeeData->m_crystalType, m_pEnsoulFeeData);
          }
      }
    | ensoul_fee
      {
          if (!m_bOnlyPrecompiledHeader) {
              g_EnsoulFeeData.clear();
              g_EnsoulFeeData.emplace(m_pEnsoulFeeData->m_crystalType, m_pEnsoulFeeData);
          }
      }
    ;

ensoul_fee
    : ENSOUL_FEE_BEGIN CRYSTAL_TYPE '=' IDENTIFIER
      {
          m_pEnsoulFeeData = new EnsoulFeeData;
          const char16_t* const name = grammar::SemanticString($4);
          const std::wstring hostName = Utf16ToWide(name);
          m_pEnsoulFeeData->m_crystalType = CItem::GetCrysalType(hostName.c_str());
          if (m_pEnsoulFeeData->m_crystalType == CRT_INVALID) {
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined CryustalType name[%s] in line [%d]",
              //     hostName.c_str(),
              //     m_lexer.GetLine() + 1)
              L2_LOG_ERROR(Log, u"Undefined CryustalType name[%s] in line [%d]", hostName.c_str(), m_lexer.GetLine() + 1);
          }
      }
      ENSOUL_FEE_NORMAL '=' '{' n26 '}'
      { m_pEnsoulFeeData->SetEnsoulFeeNormal(m_vEnsoulFeeItemData); }
      ENSOUL_FEE_BM '=' '{' n26 '}'
      { m_pEnsoulFeeData->SetEnsoulFeeBM(m_vEnsoulFeeItemData); }
      ENSOUL_REFEE_NORMAL '=' '{' n26 '}'
      { m_pEnsoulFeeData->SetEnsoulReFeeNormal(m_vEnsoulFeeItemData); }
      ENSOUL_REFEE_BM '=' '{' n26 '}'
      { m_pEnsoulFeeData->SetEnsoulReFeeBM(m_vEnsoulFeeItemData); }
      EXTRACTION_FEE_NORMAL '=' '{' n26 '}'
      { m_pEnsoulFeeData->SetExtractionFeeNormal(m_vEnsoulFeeItemData); }
      EXTRACTION_FEE_BM '=' '{' n26 '}'
      { m_pEnsoulFeeData->SetExtractionFeeBM(m_vEnsoulFeeItemData); }
      ENSOUL_FEE_END
    ;

n26
    : n26 ';' n27
      { m_vEnsoulFeeItemData.push_back(m_pEnsoulFeeItemData); }
    | n27
      {
          m_vEnsoulFeeItemData.clear();
          m_vEnsoulFeeItemData.push_back(m_pEnsoulFeeItemData);
      }
    | '{' '}'
      {
          m_vEnsoulFeeItemData.clear();
          m_pEnsoulFeeItemData = new EnsoulFeeItemData(-1, -1);
          m_vEnsoulFeeItemData.push_back(m_pEnsoulFeeItemData);
      }
    | %empty
      {
          m_vEnsoulFeeItemData.clear();
          m_pEnsoulFeeItemData = new EnsoulFeeItemData(-1, -1);
          m_vEnsoulFeeItemData.push_back(m_pEnsoulFeeItemData);
      }
    ;

n27
    : '{' IDENTIFIER ';' INTEGER '}'
      {
          const char16_t* const name = grammar::SemanticString($2);
          const std::int64_t classId = g_PrecompiledHeaderDB.Search(name);
          if (classId == -1 && !m_bOnlyPrecompiledHeader) {
              const std::wstring hostName = Utf16ToWide(name);
              // Log.Add(
              //     LOG_ERROR,
              //     L"Undefined item name[%s] in line [%d]",
              //     hostName.c_str(),
              //     m_lexer.GetLine() + 1)
              L2_LOG_ERROR(Log, u"Undefined item name[%s] in line [%d]", hostName.c_str(), m_lexer.GetLine() + 1);
          }
          m_pEnsoulFeeItemData = new EnsoulFeeItemData(static_cast<std::int32_t>(classId), grammar::SemanticInt32($4));
      }
    ;

%%
