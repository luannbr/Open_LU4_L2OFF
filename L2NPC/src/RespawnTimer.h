#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>
#include <cstdint>

class BaseSpawnDefine;

namespace l2npc {


// Recovered l2npc/RespawnTimer.cpp object. The target stores one live spawn
// definition, a copied DB name, a signed _time32 deadline and a builder flag,
// and queues the object itself through CIOObject::AddTimer.
class CRespawnTimer final : public CIOObject {
public:
    CRespawnTimer(BaseSpawnDefine* spawnDefinition, const std::u16string& dbName, int absoluteUnixSeconds);
    ~CRespawnTimer() override;

    void TimerExpired(int id) override;
    static void ResetRespawn(char16_t* dbName);

private:
    // Target fields, retaining their recovered spellings.
    BaseSpawnDefine* mpSpawnDefine;
    std::u16string mDBName;
    std::int32_t mAbsSpawnTime;
    bool m_bRespawnByBuilder;
    [[maybe_unused]] std::uint8_t reserved_[3];

    // PDB process statics:
    // CRespawnTimer::s_StaticMap and CRespawnTimer::s_Lock.
    static std::map<std::u16string, CRespawnTimer*, runtime::CStringLess<std::u16string>> s_StaticMap;
    static CIOCriticalSection s_Lock;
};

// The current EXE reads these members at fixed offsets. Keep the assertions
// beside the recovered class so a host STL/ABI change cannot silently move
// target state while retaining the same aggregate size.


} // namespace l2npc
