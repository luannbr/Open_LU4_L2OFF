#pragma once

#include "Item.h"
#include "LexerObject.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CObjectDB;

// Exact current 0x288-byte physical declaration. No parser key function is
// defined in this bounded prerequisite slice, so this header does not emit
// the broad parser_object vtable or its condition-family dependencies.
class CParserForObject {
public:
    CParserForObject();
    ~CParserForObject();

    int Create(CObjectDB* objectDB, char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

protected:
    void yysyntaxerror();

public:
    CLexerForObject m_lexer; // +0x0f0
    CItemConstInfo* m_pItemConstInfo; // +0x1d8
    CItem* m_pItem; // +0x1e0
    std::int32_t m_nItemClassId; // +0x1e8
    std::int32_t nItemID; // +0x1ec
    std::int32_t nSetID; // +0x1f0
    bool m_bHasMultiSkills; // +0x1f4
    bool m_bImmediateItem; // +0x1f5
    bool m_bIsDuration; // +0x1f6

private:
    [[maybe_unused]] std::byte m_targetPadding1f7[1]; // +0x1f7

public:
    std::int32_t m_nSkillDistime; // +0x1f8
    bool m_bHaveItemSkill; // +0x1fc

private:
    [[maybe_unused]] std::byte m_targetPadding1fd[3]; // +0x1fd

public:
    std::int32_t m_nPremium; // +0x200
    std::int32_t m_nEnchantType; // +0x204
    std::int32_t m_nKeepType; // +0x208
    std::int32_t m_nCanMove; // +0x20c
    bool m_bIsTrade; // +0x210
    bool m_bIsNPCTrade; // +0x211

private:
    [[maybe_unused]] std::byte m_targetPadding212[2]; // +0x212

public:
    std::int32_t m_nAttribution; // +0x214
    bool m_bIsDrop; // +0x218
    bool m_bIsDestruct; // +0x219
    bool m_bIsPeelAction; // +0x21a

private:
    [[maybe_unused]] std::byte m_targetPadding21b[1]; // +0x21b

public:
    std::int32_t m_nCapsuledItemCount; // +0x21c
    bool m_bIsPeriodicItem; // +0x220
    bool m_bIsStackable; // +0x221
    bool m_bCanOlympiadUse; // +0x222
    bool m_bCanNotPledgeGameUse; // +0x223
    bool m_bOnlyPrecompiledHeader; // +0x224
    bool m_bReload; // +0x225
    bool m_bNewItem; // +0x226

private:
    [[maybe_unused]] std::byte m_targetPadding227[1]; // +0x227
    CObjectDB* m_pObjDB; // +0x228
    CSetItem* m_pSetItem; // +0x230
    TempCapsuledItem* m_pCapsuledItem; // +0x238
    char16_t* m_pszDesc; // +0x240
    std::vector<std::vector<CreateItemRandomStruct>> m_itemRandomListGroup; // +0x248
    std::vector<CreateItemRandomStruct> m_itemRandomList; // +0x268
};
