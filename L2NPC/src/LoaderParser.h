#pragma once

#include "LoaderLex.h"
#include "ScriptTypeInfo.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <list>
#include <string>


class CAIParameter;
class CAIProperty;
class CNPCEventHandler;
class CScriptBinCode;

// L2NPC/loader_parser.h. The grammar is LoaderParser.y. Bison generates
// yyparse as a member, so the grammar actions use these fields. The offset
// comments give the layout in the original image.
class CParserForLoader {
public:
    CParserForLoader();
    ~CParserForLoader();

    int Create(char16_t* fileName, bool isLiveReloading = false);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    bool CompileSucceeded();

    CLexerForLoader m_lexer; // +0x0f0

private:
    void SetEventHandler(int aiType, const char16_t* className, const char16_t* superClassName);
    void AddClass(int aiType, const char16_t* className, int serverType);

    CNPCEventHandler* m_pEventHandler; // +0x1d0
    CScriptBinCode* m_pCode; // +0x1d8
    CAIParameter* m_pParam; // +0x1e0
    CAIProperty* m_pProp; // +0x1e8
    std::vector<TelPos>* m_pTelPosList; // +0x1f0
    std::vector<BuySell>* m_pBuySellList; // +0x1f8
    int m_ServerType; // +0x200
    int m_AIType; // +0x204
    char16_t* m_pClassName; // +0x208
    char16_t* m_pSuperClassName; // +0x210
    std::vector<std::u16string> m_vecVariableList; // +0x218
    std::map<std::u16string, std::list<CNPCEventHandler*>> m_superclass_map; // +0x238
    bool m_isLiveReloading; // +0x258
};
