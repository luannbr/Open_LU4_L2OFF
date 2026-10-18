#pragma once

#include "PetDataSheet.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class PetDataLexer;

class PetDataParser {
public:
    PetDataParser();
    ~PetDataParser();

    int Create(PetDataLexer* lexer, PetDataSheetTable* result);
    int yyparse();

    // Syntax errors seen by the last yyparse call.
    int yyerrorcount = 0;

protected:
    void yysyntaxerror();

private:
    PetDataLexer* mpLexer; // +0x0f0
    PetDataSheet::Record mPetData; // +0x0f8
    PetDataSheet* mpPetDataSheet; // +0x1d0
    PetDataSheetTable* mpResult; // +0x1d8
    std::vector<std::int64_t> mIntList; // +0x1e0
    std::vector<std::int32_t> intList; // +0x200
    std::u16string m_NpcName; // +0x220
    std::u16string m_PetItemName; // +0x248
    std::int32_t m_nPetIndex; // +0x270
    std::int32_t m_nNpcId; // +0x274
    std::int32_t m_ItemClassID; // +0x278
    bool m_bSyncLevel; // +0x27c
    PetDataSheet* m_pLastInsertedSheet; // +0x280
    PetDataSheet::Record m_LastInsertedRecord; // +0x288
};
