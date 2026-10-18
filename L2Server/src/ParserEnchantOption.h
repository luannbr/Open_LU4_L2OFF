#pragma once

#include "LexerEnchantOption.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>

class CEnchantOption;
class CItem;
class CItemEnchantOption;

class CParserForEnchantOption {
public:
    CParserForEnchantOption();
    ~CParserForEnchantOption();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForEnchantOption m_lexer; // +0x0f0
    CEnchantOption* m_pEnchantOption; // +0x1c8
    CItemEnchantOption* m_pItemEnchantOption; // +0x1d0
    CItem* m_pItem; // +0x1d8
    int m_Enchant; // +0x1e0
    std::vector<std::u16string> m_itemNameList; // +0x1e8
    std::vector<CItem*> m_itemList; // +0x208
};
