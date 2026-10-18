#pragma once

#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstdint>

// Shipping owner: CacheD/BattleCampDb.{h,cpp}. Current Cached.exe keeps
// only the shared RWLock in this global database interface.
class CBattleCampDb {
public:
    CBattleCampDb();
    ~CBattleCampDb();

    std::uint32_t InstallBattleCamp(
        std::uint32_t castleId,
        std::uint32_t pledgeId,
        std::uint32_t pledgeWorldId,
        int maxHp,
        int x,
        int y,
        int z,
        int baseType
    );
    std::uint32_t UninstallBattleCamp(std::uint32_t pledgeId, std::uint32_t pledgeWorldId, int baseType);
    void UninstallAllBattleCamp(std::uint32_t castleId, std::vector<std::uint32_t>* battleCamps);

private:
    RWLock m_lock;
};


extern CBattleCampDb g_battleCampDb;
