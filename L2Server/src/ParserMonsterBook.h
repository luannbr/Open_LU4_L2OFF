#pragma once

#include "LexerMonsterBook.h"
#include "MonsterBook.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

// The grammar is ParserMonsterBook.y. Bison generates yyparse as a member, so
// the grammar actions use these fields. The offset comments give the layout in
// the original image.
class CParserForMonsterBook {
public:
    CParserForMonsterBook();
    ~CParserForMonsterBook();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

    CLexerForMonsterBook m_lexer; // +0xf0

private:
    [[maybe_unused]] char16_t* m_pszDesc; // +0x1d8, unused by current actions
    MonsterBook::Script::Card* m_pCard; // +0x1e0
    std::vector<std::int64_t> m_vIntList; // +0x1e8
    std::vector<ItemAmountData> m_vItemList; // +0x208
    std::u16string m_tableName; // +0x228
    MonsterBook::Script::TrophyTable* m_pTrophyTable; // +0x250
};
