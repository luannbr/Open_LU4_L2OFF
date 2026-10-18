#pragma once

#include "Shared/src/IOCs.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <utility>


// Shipping owner: CacheD/NewCharDB.{h,cpp}. Current constructor 0x004f9a28
// builds CIOCriticalSection at +0x00 and an MSVC xstd set at +0x28.
class CNewCharDB : private CIOCriticalSection {
public:
    CNewCharDB();
    ~CNewCharDB();

    // Current 0x004f96a8. The original identifier is absent from the usable
    // symbols. The name describes its exact lin_LoadNewCharList operation.
    void LoadNewCharList();

    void Insert(int accountId, int characterId);

private:

    using NewCharacter = std::pair<int, int>;
    std::set<NewCharacter> m_newCharacters; // +0x28
};



extern CNewCharDB g_newCharDB;
