#pragma once

#include <cstddef>
#include <cstdint>

int GetSharedInfoVersion();
void ReportShareInfo();

template <typename T, int RecordsPerSegment, int SegmentGrowth, int MaximumSegments>
class CSharedVector;

// Shared/ShareInfo.h. The adjacent VS2015 PDB fixes this complete record,
// including its global name, field order and 0x4c layout. L2Server publishes
// the one-record vector and L2NPC consumes it through the same named mapping.
struct NpcSpawnInfo;
using NpcSpawnInfoVector = CSharedVector<NpcSpawnInfo, 1, 1, 1>;

struct NpcSpawnInfo {
    static NpcSpawnInfo* Inst();
    void Init();

    std::int32_t Server_Create = 0;
    std::int32_t Server_Delete = 0;
    std::int32_t Server_EnterWorld = 0;
    std::int32_t Server_LeaveWorld = 0;
    std::int32_t Server_EnterWorldQueueSize = 0;
    std::int32_t Server_CreatePrivate = 0;
    std::int32_t Server_DeletePrivate = 0;
    std::int32_t Npc_Create = 0;
    std::int32_t Npc_Delete = 0;
    std::int32_t Npc_EnterWorld = 0;
    std::int32_t NpcMaker_RequestCreate = 0;
    std::int32_t NpcMaker_OnCreate = 0;
    std::int32_t NpcMaker_OnDelete = 0;
    std::int32_t NpcMakerWorld_RequestCreate = 0;
    std::int32_t NpcMakerWorld_OnCreate = 0;
    std::int32_t NpcMakerWorld_OnDelete = 0;
    std::int32_t Npc_CreatePrivate = 0;
    std::int32_t Npc_OnPrivateDie = 0;
    std::int32_t Npc_RandPresetValue = -1;
};


namespace PledgeGameFieldStatus {

enum type : std::int32_t {
    NO_MATCH = 0,
    OPEN_MATCH = 1,
    PREPARE_MATCH = 2,
    WAIT_MATCH = 3,
    NO_UNRESIST_MATCH = 4,
    START_MATCH = 5,
    PROCESS_MATCH = 6,
    FINISH_MATCH = 7,
    CLEAR_MATCH = 8,
};

} // namespace PledgeGameFieldStatus
