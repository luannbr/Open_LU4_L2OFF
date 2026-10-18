#include "NpcMakerCommon.h"

#include "NPCServer.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/ShareInfo.h"

#include <cstdint>

namespace l2npc {

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
) {
    g_ns.Send(
        "cSpddddddddddcddddcddcddd", 0x01, name, static_cast<long long>(reinterpret_cast<std::uintptr_t>(define)), pledgeId,
        x, y, z, direction, hp, mp, dbValue, static_cast<int>(inZoneId), static_cast<int>(airShipServerId),
        static_cast<int>(argumentFlag1), argument1, makerParam1, makerParam2, makerParam3, static_cast<int>(argumentFlag2),
        static_cast<int>(argument2a), static_cast<int>(argument2b), static_cast<int>(argumentFlag3), argument3a, argument3b,
        finalArgument
    );
    base_structure_detail::AtomicIncrement(&NpcSpawnInfo::Inst()->NpcMaker_RequestCreate);
}

} // namespace l2npc
