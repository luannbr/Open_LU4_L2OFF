#pragma once

#include <cstdint>

class BaseSpawnDefine;

namespace l2npc {

// l2npc/NpcMakerCommon.cpp:21-40. This is the process-wide packet boundary
// shared by CNpcSpawnDefine, NpcSpawnDefineEx and DelayedCreator.
void SendCreateNpcPacket(
    const char16_t* name,
    BaseSpawnDefine* define,
    int pledgeId,
    int x,
    int y,
    int z,
    int direction,
    int hp,
    int mp,
    int dbValue,
    std::uint32_t inZoneId,
    std::uint32_t airShipServerId,
    char argumentFlag1,
    int argument1,
    int makerParam1,
    int makerParam2,
    int makerParam3,
    char argumentFlag2,
    std::uint32_t argument2a,
    std::uint32_t argument2b,
    char argumentFlag3,
    int argument3a,
    int argument3b,
    int finalArgument
);

} // namespace l2npc
