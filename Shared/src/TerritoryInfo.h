#pragma once

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/Territory.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>


// Recovered from Shared/TerritoryInfo.h and the shipping functions at
// 0x004887d0 and 0x0053fbec..0x005413cc. Territory pointers are non-owning:
// parser_spawn creates CTerritory objects and CNpcMakerDB keeps them.
class TerritoryInfo {
public:
    TerritoryInfo();
    TerritoryInfo(const TerritoryInfo& other);
    ~TerritoryInfo() {
        mTerritoryList.clear();
        mBannedTerritoryList.clear();
    }

    TerritoryInfo& operator=(const TerritoryInfo& other) = default;

    void Clear() {
        mTerritoryList.clear();
        mBannedTerritoryList.clear();
    }

    void AddBannedTerritory(CTerritory* territory);
    void AddTerritory(CTerritory* territory);
    CTerritory* GetBannedTerritory(int index);
    int GetBannedTerritorySize();
    const char16_t* GetFirstTerritoryName();
    const char16_t* GetLastTerritoryName();
    CTerritory* GetTerritory(int index);
    int GetTerritorySize();

    bool InitializeRandomPos(bool sky, int sampleTarget);
    bool IsInBannedTerritory(const FVector& position, bool includeZ);
    bool IsInTerritory(const FVector& position, bool includeZ);
    bool IsInTerritoryWithLogVisitor(const FVector& position, ZLogVisitor& visitor, bool includeZ);
    FVector PickRandomPos(bool sky);
    bool ValidRandomPos(CTerritory* territory, const FVector& position, bool sky);
    bool ValidRandomPos1(CTerritory* territory, const FVector& position);
    bool ValidRandomPos2(CTerritory& territory, const FVector& position, bool sky);
    bool RandomposGenerated() { return mRandomPosGenerated; }

private:
    // The order is corroborated by direct accesses in the current EXE. The
    // adjacent PDB uses an older, larger xvector ABI but records the same
    // members and private access.
    std::vector<CTerritory*> mTerritoryList;
    std::vector<CTerritory*> mBannedTerritoryList;
    bool mRandomPosGenerated;
    std::uint8_t mRandomPosGeneratedPadding[3];
    CYieldLock m_RandomPosLock;
    std::uint8_t mRandomPosLockPadding[4];
    std::vector<FVector> mRandomPosList;
    std::vector<FVector> mRandomPosWithSkyList;
};
