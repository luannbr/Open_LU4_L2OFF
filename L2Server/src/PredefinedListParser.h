#pragma once

#include "PredefinedListDB.h"
#include "PredefinedListLexer.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>

class CTerritory;
class TerritoryElement;
enum SkillOperationTargetType : std::int32_t;

class CParserForPredefinedList {
public:
    CParserForPredefinedList();
    ~CParserForPredefinedList();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

private:
    void StartList(int predefinedListId, const char16_t* predefinedListName, PredefinedListType type);

public:
    CLexerForPredefinedList m_lexer; // +0x0f0
    bool m_bOnlyPrecompiledHeader; // +0x1d8
    [[maybe_unused]] std::byte m_targetPadding1d9[7]{};
    PredefinedSkillList* m_pPredefinedSkillList; // +0x1e0
    PredefinedItemList* m_pPredefinedItemList; // +0x1e8
    CTerritory* m_pTerritory; // +0x1f0
    SkillOperationTargetType m_eSkillOPTargetType; // +0x1f8
    [[maybe_unused]] std::int32_t m_targetPadding1fc{};
    TerritoryElement* m_pTerritoryElement; // +0x200
    PredefinedTerritoryList* m_pPredefinedTerritoryList; // +0x208
    std::set<int> m_setPredefinedListID; // +0x210
    std::set<std::u16string> m_setPredefinedListName; // +0x230
    std::u16string tempPredefinedListName; // +0x250
};
