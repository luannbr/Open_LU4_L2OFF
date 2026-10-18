#pragma once

#include <cstdint>

class CCreature;
class FieldCycleMissionDB;
class FieldCycleMissionQuest;
class User;

namespace FieldCycleStuff {

namespace MissionCancelReason {

enum Value : std::int32_t {
    Manual = 0,
    WrongFieldCycleOfQuest = 1,
    SyncMissionQuest = 2,
    MissionOver = 3,
};

} // namespace MissionCancelReason

void Write_LOG_FIELD_MISSION_CANCEL(
    CCreature* creature, int fieldCycleId, int step, int questId, int currentFieldCycleId, MissionCancelReason::Value reason
);

void OnUserDestroyQuest(User* user, int questId);

const FieldCycleMissionQuest* FindNextQuestWithDailyFlag(
    const FieldCycleMissionDB* database, int fieldCycleId, int step, User* user, int currentQuestId
);

} // namespace FieldCycleStuff
