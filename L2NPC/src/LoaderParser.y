// AI object grammar (original L2NPC/loader_parser.y).
// Bison generates LoaderParser.cpp and LoaderParserTokens.h.

%code requires {
#include "Shared/src/Runtime/GrammarRuntime.h"
}

%code top {
#include "L2NPC/src/LoaderParser.h"

#include "L2NPC/src/AIManager.h"
#include "L2NPC/src/AIScriptCompiler.h"
#include "L2NPC/src/NPCEventHandler.h"
#include "L2NPC/src/ParamProperty.h"
#include "L2NPC/src/ReloadableNPCEventHandler.h"
#include "L2NPC/src/ScriptBinCode.h"
#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "TextRuntime.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string_view>
#include <utility>

#include "LoaderParserTokens.h"

// The generated parse function is a member, so actions use the parser fields.
#define yyparse CParserForLoader::yyparse
#define yylex(value) m_lexer.Lex(value)
#define yyerror(message) ((void)yynerrs, ++yyerrorcount, grammar::ReportSyntaxError(m_lexer.yylineno, message))
}

%define api.pure full
%define api.value.type {grammar::SemanticBits}
%define api.header.include {"LoaderParserTokens.h"}
%define api.token.prefix {TK_}
%define parse.error verbose

%initial-action { yyerrorcount = 0; }

%code {
namespace {

constexpr int kExpectedPointerSize = 8;
constexpr int kExpectedSharedFactoryVersion = 289;
constexpr int kExpectedNpchVersion = 317;
constexpr int kExpectedNpcEventVersion = 10;

AIConstantData Constant(ScriptTypeId type) {
    AIConstantData result;
    result.type = &GetBuiltinScriptType(type);
    return result;
}

} // namespace

CParserForLoader::CParserForLoader() {
    // Current constructor 0x0054c0d4.
    m_pEventHandler = nullptr;
    m_pCode = nullptr;
    m_pParam = nullptr;
    m_pProp = nullptr;
    m_pTelPosList = nullptr;
    m_pBuySellList = nullptr;
    m_ServerType = 0;
    m_AIType = 0;
    m_pClassName = nullptr;
    m_pSuperClassName = nullptr;
    m_isLiveReloading = false;
}

CParserForLoader::~CParserForLoader() = default;

int CParserForLoader::Create(char16_t* fileName, bool isLiveReloading) {
    // Current 0x0054a0f0.
    if (m_lexer.create(fileName) == 0) {
        return 0;
    }
    m_isLiveReloading = isLiveReloading;
    return 1;
}

bool CParserForLoader::CompileSucceeded() {
    // Current 0x0054bb68.
    if (m_superclass_map.empty()) {
        return true;
    }
    for (auto& entry : m_superclass_map) {
        const std::string printable = Utf16ToUtf8(entry.first);
        // Log.Add(LOG_ERROR, L"Super class [%s] is not defined", printable.c_str())
        // Log.Add(LOG_ERROR,
        //     "[AI load] Undefined parent class '%s'. %zu handlers remain unresolved.",
        //     WideToUtf8(printable).c_str(), entry.second.size())
        L2_LOG_ERROR(
            Log, "[AI load] Undefined parent class '%s'; %zu handlers remain unresolved.", printable.c_str(),
            entry.second.size()
        );
        entry.second.clear();
    }
    m_superclass_map.clear();
    return false;
}

void CParserForLoader::SetEventHandler(int aiType, const char16_t* className, const char16_t* superClassName) {
    // Current 0x0054c1c8.
    CReloadableNPCEventHandler* superWrapper = nullptr;
    CNPCEventHandler* superHandler = nullptr;
    if (superClassName) {
        superWrapper = CNPCAIManager::FindClass(aiType, superClassName);
        if (superWrapper) {
            superHandler = superWrapper->InterlockedGetEventHandler();
        }
    }

    m_pEventHandler = new CNPCEventHandler(superHandler, m_pParam, m_pProp);
    m_pEventHandler->SetAIClassName(className);

    m_pParam->m_pSuper = superHandler ? superHandler->m_pParameter : nullptr;
    m_pProp->m_pSuper = superHandler ? superHandler->m_pProperty : nullptr;
    // Both stores are compiled after the constructor in the current image,
    // even though the constructor received the same two pointers.
    m_pEventHandler->m_pParameter = m_pParam;
    m_pEventHandler->m_pProperty = m_pProp;

    if (!superWrapper && superClassName) {
        const auto pending = m_superclass_map.find(std::u16string(superClassName));
        if (pending == m_superclass_map.end()) {
            std::list<CNPCEventHandler*> children;
            children.push_back(m_pEventHandler);
            m_superclass_map[std::u16string(superClassName)] = children;
        } else {
            pending->second.push_back(m_pEventHandler);
        }
    }
}

void CParserForLoader::AddClass(int aiType, const char16_t* className, int serverType) {
    // Current 0x0054b5dc.
    const int added = m_isLiveReloading ? CNPCAIManager::ReplaceClass(aiType, className, m_pEventHandler, serverType)
                                        : CNPCAIManager::AddClass(aiType, className, m_pEventHandler, serverType);
    if (added == 0) {
        // Log.Add(LOG_ERROR, L"[%s][%d] fail to add class", L"loader_parser.y", 0x291)
        // Log.Add(LOG_ERROR,
        //     "[AI load] Cannot %s class '%s' (AI type %d, server type %d).",
        //     (m_isLiveReloading ? "replace" : "register"),
        //     className ? Utf16ToUtf8(className).c_str() : "<unnamed>", aiType, serverType)
        L2_LOG_ERROR(
            Log, "[AI load] Cannot %s class '%s' (AI type %d, server type %d).",
            (m_isLiveReloading ? "replace" : "register"), className ? Utf16ToUtf8(className).c_str() : "<unnamed>", aiType,
            serverType
        );
        return;
    }

    const auto pending = m_superclass_map.find(std::u16string(className));
    if (pending != m_superclass_map.end()) {
        for (CNPCEventHandler* child : pending->second) {
            child->m_pParameter->m_pSuper = m_pParam;
            child->m_pProperty->m_pSuper = m_pProp;
            child->SuperEventHandler(m_pEventHandler);
        }
        pending->second.clear();
        m_superclass_map.erase(pending);
    }

    m_pEventHandler = nullptr;
    m_pParam = nullptr;
    m_pProp = nullptr;
}
}

%token CLASS
%token CLASS_END
%token HANDLER
%token HANDLER_END
%token NULL_NAME
%token STRING_LITERAL
%token IDENTIFIER
%token INTEGER
%token STRING_INDEX
%token LABEL
%token FLOAT_NUMBER
%token OPCODE
%token OPCODE_WITH_OPERAND
%token PARAMETER_DEFINE_BEGIN
%token PARAMETER_DEFINE_END
%token INT_KW
%token FLOAT_KW
%token TS_CLASS
%token BC_CLASS
%token DEV_CLASS
%token STRING
%token PROPERTY_DEFINE_BEGIN
%token PROPERTY_DEFINE_END
%token TELPOSLIST_BEGIN
%token TELPOSLIST_END
%token BUYSELLLIST_BEGIN
%token BUYSELLLIST_END
%token WAYPOINTSTYPE
%token WAYPOINTDELAYSTYPE
%token VARIABLE_BEGIN
%token VARIABLE_END
%token SHAREDFACTORYVERSION
%token NPCHVERSION
%token NASCVERSION
%token NPCEVENTHVERSION
%token DEBUG_KW
%token SIZEOFPOINTER
%start ai_object

%%

ai_object
    : SIZEOFPOINTER INTEGER SHAREDFACTORYVERSION INTEGER NPCHVERSION INTEGER NASCVERSION INTEGER NPCEVENTHVERSION INTEGER
      DEBUG_KW INTEGER
      {
          const int pointerSize = grammar::SemanticInt32($2);
          const int sharedFactoryVersion = grammar::SemanticInt32($4);
          const int npchVersion = grammar::SemanticInt32($6);
          const int nascVersion = grammar::SemanticInt32($8);
          const int npcEventVersion = grammar::SemanticInt32($10);
          const int debug = grammar::SemanticInt32($12);

          if (pointerSize != kExpectedPointerSize) {
              const int compilerBitness = static_cast<int>(static_cast<std::uint32_t>(pointerSize) << 3U);
              // Log.Add(LOG_ERROR, L"Invalid AI.obj compiled from %d bit nasc", compilerBitness)
              // Log.Add(LOG_ERROR,
              //     "[AI load] AI object targets %d-bit pointers. Expected %d-bit. Continuing header validation.",
              //     compilerBitness, kExpectedPointerSize * 8)
              L2_LOG_ERROR(
                  Log, "[AI load] AI object targets %d-bit pointers; expected %d-bit. Continuing header validation.",
                  compilerBitness, kExpectedPointerSize * 8
              );
          }
          if (sharedFactoryVersion != kExpectedSharedFactoryVersion) {
              // Log.Add(LOG_ERROR,
              //                L"SharedFactory version is different! "
              //                L"npcserver[%d] ai.obj[%d]",
              //                kExpectedSharedFactoryVersion, sharedFactoryVersion)
              // Log.Add(LOG_ERROR,
              //     "[AI load] Fatal SharedFactory version mismatch: server expects %d, AI object has %d. Terminating.",
              //     kExpectedSharedFactoryVersion, sharedFactoryVersion)
              L2_LOG_ERROR(
                  Log, "[AI load] Fatal SharedFactory version mismatch: server expects %d, AI object has %d. Terminating.",
                  kExpectedSharedFactoryVersion, sharedFactoryVersion
              );
              ForceCrash(CrashInfo::MISMATCH_SHARED_FACTORY_VERSION);
          }
          if (npchVersion != kExpectedNpchVersion) {
              // Log.Add(LOG_ERROR,
              //                L"npc.h version is different! "
              //                L"npcserver[%d] ai.obj[%d]",
              //                kExpectedNpchVersion, npchVersion)
              // Log.Add(LOG_ERROR,
              //     "[AI load] Fatal NPC header version mismatch: server expects %d, AI object has %d. Terminating.",
              //     kExpectedNpchVersion, npchVersion)
              L2_LOG_ERROR(
                  Log, "[AI load] Fatal NPC header version mismatch: server expects %d, AI object has %d. Terminating.",
                  kExpectedNpchVersion, npchVersion
              );
              ForceCrash(CrashInfo::MISMATCH_NPC_HEADER_VERSION);
          }
          if (nascVersion != GetAIScriptCompilerVersion()) {
              // Log.Add(LOG_ERROR,
              //                L"NASC Version is different! "
              //                L"npcserver[%d] ai.obj[%d]",
              //                GetAIScriptCompilerVersion(), nascVersion)
              // Log.Add(LOG_ERROR,
              //     "[AI load] NASC compiler version mismatch: server expects %d, AI object has %d. Continuing header
              //     validation.", GetAIScriptCompilerVersion(), nascVersion)
              L2_LOG_ERROR(
                  Log,
                  "[AI load] NASC compiler version mismatch: server expects %d, AI object has %d. Continuing header "
                  "validation.",
                  GetAIScriptCompilerVersion(), nascVersion
              );
          }
          if (npcEventVersion != kExpectedNpcEventVersion) {
              // Log.Add(LOG_ERROR,
              //                L"NPCEvent.h version is different! "
              //                L"npcserver[%d] ai.obj[%d]",
              //                kExpectedNpcEventVersion, npcEventVersion)
              // Log.Add(LOG_ERROR,
              //     "[AI load] NPC event version mismatch: server expects %d, AI object has %d. Continuing header
              //     validation.", kExpectedNpcEventVersion, npcEventVersion)
              L2_LOG_ERROR(
                  Log,
                  "[AI load] NPC event version mismatch: server expects %d, AI object has %d. Continuing header "
                  "validation.",
                  kExpectedNpcEventVersion, npcEventVersion
              );
          }
          if (debug != 0) {
              // Log.Add(LOG_ERROR,
              //                L"Debug/Release mode is differenct "
              //                L"npcserver[%s] ai.obj[%s]",
              //                L"release", L"debug")
              // Log.Add(LOG_WARN,
              //     "[AI load] AI object uses debug mode while the server expects release mode, continuing loading.")
              L2_LOG_WARN(
                  Log, "[AI load] AI object uses debug mode while the server expects release mode; continuing loading."
              );
          }
      }
      class_list
    ;

class_list
    : class_list class_declaration
    | %empty
    ;

class_body
    : INTEGER IDENTIFIER super_class
      {
          m_pParam = new CAIParameter();
          m_pProp = new CAIProperty();
          m_AIType = grammar::SemanticInt32($1);
          m_pClassName = grammar::SemanticString($2);
          m_pSuperClassName = grammar::SemanticString($3);
          SetEventHandler(m_AIType, m_pClassName, m_pSuperClassName);
      }
      parameter_block property_block handler_list CLASS_END
      { AddClass(m_AIType, m_pClassName, m_ServerType); }
    ;

class_declaration
    : CLASS
      { m_ServerType = 0; }
      class_body
    | DEV_CLASS
      { m_ServerType = 1; }
      class_body
    | TS_CLASS
      { m_ServerType = 2; }
      class_body
    | BC_CLASS
      { m_ServerType = 3; }
      class_body
    ;

super_class
    : ':' NULL_NAME
      { $$ = 0; }
    | ':' IDENTIFIER
      { $$ = $2; }
    | %empty
      { $$ = 0; }
    ;

parameter_block
    : PARAMETER_DEFINE_BEGIN parameter_list PARAMETER_DEFINE_END
    | %empty
    ;

parameter_list
    : parameter_list parameter
    | %empty
    ;

parameter
    : INT_KW IDENTIFIER INTEGER
      {
          AIConstantData value = Constant(ScriptTypeId::Int);
          value.data.intData = grammar::SemanticInt64($3);
          m_pParam->m_mapData[std::u16string(grammar::SemanticString($2))] = value;
      }
    | FLOAT_KW IDENTIFIER FLOAT_NUMBER
      {
          AIConstantData value = Constant(ScriptTypeId::Float);
          value.data.floatData = grammar::SemanticFloat($3);
          m_pParam->m_mapData[std::u16string(grammar::SemanticString($2))] = value;
      }
    | STRING IDENTIFIER STRING_LITERAL
      {
          AIConstantData value = Constant(ScriptTypeId::String);
          value.data.stringData = grammar::SemanticString($3);
          m_pParam->m_mapData[std::u16string(grammar::SemanticString($2))] = value;
      }
    | WAYPOINTSTYPE IDENTIFIER
      {
          AIConstantData value = Constant(ScriptTypeId::WayPoints);
          value.data.intData = 0;
          m_pParam->m_mapData[std::u16string(grammar::SemanticString($2))] = value;
      }
    | WAYPOINTDELAYSTYPE IDENTIFIER
      {
          AIConstantData value = Constant(ScriptTypeId::WayPointDelays);
          value.data.intData = 0;
          m_pParam->m_mapData[std::u16string(grammar::SemanticString($2))] = value;
      }
    ;

property_block
    : PROPERTY_DEFINE_BEGIN property_list PROPERTY_DEFINE_END
    | %empty
    ;

property_list
    : property_list property
    | %empty
    ;

property
    : INT_KW IDENTIFIER INTEGER
      {
          AIConstantData value = Constant(ScriptTypeId::Int);
          value.data.intData = grammar::SemanticInt64($3);
          m_pProp->m_mapData[std::u16string(grammar::SemanticString($2))] = value;
      }
    | FLOAT_KW IDENTIFIER FLOAT_NUMBER
      {
          AIConstantData value = Constant(ScriptTypeId::Float);
          value.data.floatData = grammar::SemanticFloat($3);
          m_pProp->m_mapData[std::u16string(grammar::SemanticString($2))] = value;
      }
    | STRING IDENTIFIER STRING_LITERAL
      {
          AIConstantData value = Constant(ScriptTypeId::String);
          value.data.stringData = grammar::SemanticString($3);
          m_pProp->m_mapData[std::u16string(grammar::SemanticString($2))] = value;
      }
    | TELPOSLIST_BEGIN IDENTIFIER
      { m_pTelPosList = new std::vector<TelPos>(); }
      telpos_list TELPOSLIST_END
      {
          AIConstantData value = Constant(ScriptTypeId::TelPosList);
          value.data.telPosListData = m_pTelPosList;
          m_pProp->m_mapData[std::u16string(grammar::SemanticString($2))] = value;
          m_pTelPosList = nullptr;
      }
    | BUYSELLLIST_BEGIN IDENTIFIER
      { m_pBuySellList = new std::vector<BuySell>(); }
      buysell_list BUYSELLLIST_END
      {
          AIConstantData value = Constant(ScriptTypeId::BuySellList);
          value.data.buySellListData = m_pBuySellList;
          m_pProp->m_mapData[std::u16string(grammar::SemanticString($2))] = value;
          m_pBuySellList = nullptr;
      }
    ;

telpos_list
    : telpos_list telpos
    | %empty
    ;

telpos
    : '{' STRING_LITERAL ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          auto* residenceIds = new std::vector<int>();
          residenceIds->push_back(grammar::SemanticInt32($12));
          TelPos value(
              grammar::SemanticString($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6),
              grammar::SemanticInt32($8), grammar::SemanticInt32($10), residenceIds
          );
          m_pTelPosList->push_back(value);
          delete residenceIds;
      }
    | '{' STRING_LITERAL ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          TelPos value(
              grammar::SemanticString($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6),
              grammar::SemanticInt32($8), grammar::SemanticInt32($10), nullptr
          );
          m_pTelPosList->push_back(value);
      }
    | '{' STRING_LITERAL ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' '{' telpos_number_list '}' '}'
      {
          std::vector<int>* const residenceIds = grammar::SemanticPointer<std::vector<int>>($13);
          TelPos value(
              grammar::SemanticString($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6),
              grammar::SemanticInt32($8), grammar::SemanticInt32($10), residenceIds
          );
          m_pTelPosList->push_back(value);
          delete residenceIds;
      }
    | '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          auto* residenceIds = new std::vector<int>();
          residenceIds->push_back(grammar::SemanticInt32($12));
          TelPos value(
              grammar::SemanticInt32($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6),
              grammar::SemanticInt32($8), grammar::SemanticInt32($10), residenceIds
          );
          m_pTelPosList->push_back(value);
          delete residenceIds;
      }
    | '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER '}'
      {
          TelPos value(
              grammar::SemanticInt32($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6),
              grammar::SemanticInt32($8), grammar::SemanticInt32($10), nullptr
          );
          m_pTelPosList->push_back(value);
      }
    | '{' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' INTEGER ';' '{' telpos_number_list '}' '}'
      {
          std::vector<int>* const residenceIds = grammar::SemanticPointer<std::vector<int>>($13);
          TelPos value(
              grammar::SemanticInt32($2), grammar::SemanticInt32($4), grammar::SemanticInt32($6),
              grammar::SemanticInt32($8), grammar::SemanticInt32($10), residenceIds
          );
          m_pTelPosList->push_back(value);
          delete residenceIds;
      }
    ;

telpos_number_list
    : telpos_number_list ';' INTEGER
      {
          const int value = grammar::SemanticInt32($3);
          grammar::SemanticPointer<std::vector<int>>($1)->push_back(value);
          $$ = $1;
      }
    | INTEGER
      {
          auto* const list = new std::vector<int>();
          $$ = (reinterpret_cast<std::uintptr_t>(list));
          const int value = grammar::SemanticInt32($1);
          reinterpret_cast<std::vector<int>*>(static_cast<std::uintptr_t>($$))
              ->push_back(value);
      }
    ;

buysell_list
    : buysell_list buysell
    | %empty
    ;

buysell
    : '{' INTEGER ';' INTEGER '}'
      {
          BuySell value;
          value.id = grammar::SemanticInt32($2);
          value.price = static_cast<std::int64_t>(grammar::SemanticInt32($4));
          value.rate = 0.0F;
          value.count = 0;
          m_pBuySellList->push_back(value);
      }
    | '{' INTEGER ';' INTEGER ';' FLOAT_NUMBER ';' INTEGER '}'
      {
          BuySell value;
          value.id = grammar::SemanticInt32($2);
          value.price = static_cast<std::int64_t>(grammar::SemanticInt32($4));
          value.rate = grammar::SemanticFloat($6);
          value.count = static_cast<std::int64_t>(grammar::SemanticInt32($8));
          m_pBuySellList->push_back(value);
      }
    ;

handler_list
    : handler_list handler
    | %empty
    ;

handler
    : HANDLER INTEGER INTEGER
      { m_pCode = new CScriptBinCode(grammar::SemanticInt32($3)); }
      variable_block opcode_def_list
      {
          m_pCode->SetVariableList(m_vecVariableList);
          m_pEventHandler->AddEventHandler(grammar::SemanticInt32($2), m_pCode);
      }
      HANDLER_END
      {
          m_pCode->GoPass2();
          m_pCode = nullptr;
      }
    ;

variable_block
    : VARIABLE_BEGIN variable_list VARIABLE_END
    ;

variable_list
    : variable_list STRING_LITERAL
      {
          m_vecVariableList.push_back(std::u16string(grammar::SemanticString($2)));
      }
    | %empty
      { m_vecVariableList.clear(); }
    ;

opcode_def_list
    : opcode_def_list opcode_def
    | %empty
    ;

opcode_def
    : OPCODE
      {
          m_pCode->AddCode(static_cast<OP_TYPE>(grammar::SemanticInt32($1)));
      }
    | OPCODE_WITH_OPERAND STRING_INDEX
      {
          m_pCode->AddCode2(static_cast<OP_TYPE>(grammar::SemanticInt32($1)), grammar::SemanticInt64($2));
      }
    | OPCODE_WITH_OPERAND LABEL
      {
          m_pCode->AddCode2(static_cast<OP_TYPE>(grammar::SemanticInt32($1)), grammar::SemanticInt64($2));
      }
    | OPCODE_WITH_OPERAND INTEGER
      {
          m_pCode->AddCode2(static_cast<OP_TYPE>(grammar::SemanticInt32($1)), grammar::SemanticInt64($2));
      }
    | OPCODE_WITH_OPERAND IDENTIFIER
      {
          m_pCode->AddCode2(static_cast<OP_TYPE>(grammar::SemanticInt32($1)), grammar::SemanticString($2));
      }
    | OPCODE_WITH_OPERAND FLOAT_NUMBER
      {
          m_pCode->AddCode2(static_cast<OP_TYPE>(grammar::SemanticInt32($1)), grammar::SemanticFloat($2));
      }
    | STRING_INDEX '.' STRING_LITERAL
      {
          m_pCode->DeclareStringLabel(grammar::SemanticInt32($1), grammar::SemanticString($3));
      }
    | LABEL
      { m_pCode->DeclareLabel(grammar::SemanticInt32($1)); }
    ;

%%
