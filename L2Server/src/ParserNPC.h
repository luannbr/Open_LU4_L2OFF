#pragma once

#include "LexerNPC.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CNPC;
class CObjectDB;

// The grammar is ParserNPC.y. Bison generates yyparse as a member, so the
// grammar actions use these fields. The offset comments give the layout in the
// original image.
class CParserForNPC {
public:
    CParserForNPC();
    ~CParserForNPC();

    int Create(CObjectDB* objectDB, char16_t* fileBuffer, int fileSize);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

    CLexerForNPC m_lexer; // +0x0f0
    std::int32_t m_nNpcId; // +0x2a0
    bool m_bOnlyPrecompiledHeader; // +0x2a4
    bool m_bReload; // +0x2a5
    bool m_bNewNpc; // +0x2a6
    bool m_bCheckVitalityDropItem; // +0x2a7
    CNPC* m_pNpc; // +0x2a8
    CObjectDB* m_pObjDB; // +0x2b0

private:
    [[maybe_unused]] std::byte m_targetPadding2b8[8]; // +0x2b8

public:
    std::map<std::u16string, std::u16string,
        runtime::CStringLess<std::u16string>> m_mapParameter; // +0x2c0
    bool mCheckAI; // +0x2e0

private:
    [[maybe_unused]] std::byte m_targetPadding2e1[7]; // +0x2e1

public:
    std::u16string mNpcName; // +0x2e8
    std::u16string mAIName; // +0x310
    bool m_bIsCollection; // +0x338
    bool m_bIsgMultiList; // +0x339

private:
    [[maybe_unused]] std::byte m_targetPadding33a[6]; // +0x33a
};
