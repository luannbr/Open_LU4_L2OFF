#include "FieldCycleStuff.h"

#include "FieldCycle.h"
#include "FieldCycleMissionData.h"
#include "User.h"

#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"

namespace FieldCycleStuff {

void Write_LOG_FIELD_MISSION_CANCEL(
    CCreature* const creature,
    const int fieldCycleId,
    const int step,
    const int questId,
    const int currentFieldCycleId,
    const MissionCancelReason::Value reason
) {
    // Current L2Server 0x00438e98.
    CLogFormatter formatter;
    formatter.WriteLog4(
        0x157, creature, nullptr, nullptr, nullptr, nullptr, fieldCycleId, step, questId, currentFieldCycleId,
        static_cast<int>(reason), 0, 0, 0, 0, 0, 0
    );
}

void OnUserDestroyQuest(User* const user, const int questId) {
    // Current L2Server 0x00438f30.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycleStuff.cpp",
        //     0x24,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]assert falied: %s",
        //     L"FieldCycleStuff.cpp",
        //     0x24,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log,
            "Cannot process manual cancellation of field-cycle quest %d: the user is null; the cycle quest association is "
            "not updated.",
            questId
        );
        return;
    }
    if (user->GetFieldCycleQuestID() != questId) {
        return;
    }

    Write_LOG_FIELD_MISSION_CANCEL(
        user, user->GetFieldCycleIDOfQuest(), g_FieldCycleSystem.GetFieldCycleStep(user->GetFieldCycleIDOfQuest()), questId,
        user->GetFieldCycleID(), MissionCancelReason::Manual
    );
    user->SetFieldCycleQuest(0, 0);
}

const FieldCycleMissionQuest* FindNextQuestWithDailyFlag(
    const FieldCycleMissionDB* const database,
    const int fieldCycleId,
    const int step,
    User* const user,
    const int currentQuestId
) {
    // Current L2Server 0x00439050.
    L2_GUARD_FUNCTION();
    int previousQuestId = currentQuestId;
    int checkedCount = 0;
    do {
        const FieldCycleMissionQuest* const quest = FieldCycleMissionUtil::FindNextQuest(
            database, fieldCycleId, step, user->GetClass(), user->Level(), previousQuestId
        );
        if (quest == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][FIELD_CYCLE][%s][%d]No quest for: %d, %d, %d, %d, %d",
            //     L"FieldCycleStuff.cpp",
            //     0x59,
            //     fieldCycleId,
            //     step,
            //     user->GetClass(),
            //     user->Level(),
            //     previousQuestId)
            // Noise: No eligible class/level group or no quest is an ordinary selection outcome. Missing mission/step
            // definitions are diagnosed by FindGroupByClass. L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][FIELD_CYCLE][%s][%d]No quest for: %d, %d, %d, %d, %d",
            //     L"FieldCycleStuff.cpp",
            //     0x59,
            //     fieldCycleId,
            //     step,
            //     user->GetClass(),
            //     user->Level(),
            //     previousQuestId)
            return nullptr;
        }
        if (user->m_data->GetDailyQuestFlag(quest->m_nQuestID)) {
            return quest;
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][FIELD_CYCLE][%s][%d]Quest already completed: %d, %d, %d, %d, %d",
        //     L"FieldCycleStuff.cpp",
        //     0x65,
        //     fieldCycleId,
        //     step,
        //     user->GetClass(),
        //     user->Level(),
        //     quest->m_nQuestID)
        // Noise: Skipping a quest excluded by its daily flag is an ordinary candidate search, potentially repeated on every
        // user timer. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][FIELD_CYCLE][%s][%d]Quest already completed: %d, %d, %d, %d, %d",
        //     L"FieldCycleStuff.cpp",
        //     0x65,
        //     fieldCycleId,
        //     step,
        //     user->GetClass(),
        //     user->Level(),
        //     quest->m_nQuestID)
        if (previousQuestId == quest->m_nQuestID) {
            break;
        }
        previousQuestId = quest->m_nQuestID;
        ++checkedCount;
    } while (checkedCount < 6);

    // Log.Add(
    //     LOG_ERROR,
    //     L"[FIELD_CYCLE][%s][%d]No quest in row for: %d, %d, %d, %d, %d",
    //     L"FieldCycleStuff.cpp",
    //     0x79,
    //     fieldCycleId,
    //     step,
    //     user->GetClass(),
    //     user->Level(),
    //     currentQuestId)
    // Noise: The bounded daily-quest search exhausted its candidates. No available daily quest is an expected outcome, not
    // an error. L2_LOG_ERROR(Log,
    //     L"[FIELD_CYCLE][%s][%d]No quest in row for: %d, %d, %d, %d, %d",
    //     L"FieldCycleStuff.cpp",
    //     0x79,
    //     fieldCycleId,
    //     step,
    //     user->GetClass(),
    //     user->Level(),
    //     currentQuestId)
    return nullptr;
}

} // namespace FieldCycleStuff
