#pragma once

#include "CardCollectSystem.h"
#include "CardCollectSystemLexer.h"

#include <cstddef>
#include <cstdint>

class CParserForCardCollectSystem {
public:
    CParserForCardCollectSystem();
    ~CParserForCardCollectSystem();

    int Create(char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;
    void yysyntaxerror();

private:
    friend class CLexerForCardCollectSystem;

    CCardCollectSystemData* m_pCCardCollectSystemData; // +0x0f0
    CCardCollectSystemData::ItemList m_lFixedManipItemDataList; // +0x0f8
    CCardCollectSystemData::RandomItemList m_lRandomManipItemDataList; // +0x110

public:
    CLexerForCardCollectSystem m_lexer; // +0x128

private:
    CardCollectEventInfo* m_pEventInfo; // +0x208
    std::int32_t m_nLineNumber; // +0x210
    bool m_bOnlyPrecompiledHeader; // +0x214
    bool m_bReload; // +0x215
    [[maybe_unused]] std::byte m_alignment216To218[2]; // +0x216
};
