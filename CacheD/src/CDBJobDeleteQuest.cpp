#include "Shared/src/Log.h"

#include "CDBJobDeleteQuest.h"

#include "Cached.h"
#include "DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Runtime.h"

#include <string>

namespace {

bool IsQuestBitSet(const CDBJobDeleteQuest::QuestFlag& flag, int questId) {
    // Cached.exe 0x004a7b18 is the compiled
    // CBitStorage<2048,int,64>::GetFlag body, including its exact bounds
    // diagnostics from Shared/Utils.h.
    return flag.GetFlag(questId);
}

void ClearQuestBit(CDBJobDeleteQuest::QuestFlag& flag, int questId) {
    // Cached.exe 0x004a7b90 is the matching SetFlag instantiation.
    flag.SetFlag(questId, false);
}

int NormalizeQuestId(int questId) { return questId > 10000 ? questId - 10000 : questId; }

CDBJobDeleteQuest::QuestIds NormalizeQuestIds(const CDBJobDeleteQuest::QuestIds& questIds) {
    // Current Cached.exe unnamed body 0x004a94ac constructs this exact
    // normalized set. NormalizeQuestIds is a portable semantic name, not a
    // recovered PDB identity.
    CDBJobDeleteQuest::QuestIds normalized;
    for (int questId : questIds)
        normalized.insert(NormalizeQuestId(questId));
    return normalized;
}

std::string JoinQuestIds(const CDBJobDeleteQuest::QuestIds& questIds) {
    // Current Cached.exe unnamed body 0x004a97e8 converts the first ordered
    // ID to decimal, then appends every remaining value after the exact
    // delimiter ", ". JoinQuestIds is a portable semantic name. No original
    // identifier survives for this helper.
    auto current = questIds.begin();
    std::string result = std::to_string(*current);
    for (++current; current != questIds.end(); ++current) {
        result += ", ";
        result += std::to_string(*current);
    }
    return result;
}

bool HasAnySelectedQuest(const CDBJobDeleteQuest::QuestFlag& flag, const CDBJobDeleteQuest::QuestIds& normalizedQuestIds) {
    // Current unnamed body 0x004a7ef8 performs this ordered traversal and
    // returns immediately after the first matching quest bit. No original
    // helper name survives.
    for (int questId : normalizedQuestIds) {
        if (IsQuestBitSet(flag, questId))
            return true;
    }
    return false;
}

CDBJobDeleteQuest::QuestIds LoadQuestListForDBJob() {
    CDBJobDeleteQuest::QuestIds result;
    int questId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&questId);
    connection.Prepare(L"{Call lin_LoadOneTimeQuestListForDBJob}");
    if (!connection.ExecutePrepared(L"{Call lin_LoadOneTimeQuestListForDBJob}")) {
        L2_LOG_ERROR(Log,
            "[quest cleanup] Could not load the one-time quest job list. Cleanup is skipped; this is not an empty successful query result.");
        return result;
    }

    while (connection.Fetch()) {
        // Exact unsigned range test at 0x004a9aaa rejects 255..10000.
        if (questId >= 255 && questId <= 10000) {
            // Log.Add(LOG_ERROR, "[DBJOB] questID %d is not onetime!", questId)
            // Log.Add(LOG_ERROR,
            //     "[quest cleanup] Skipping quest ID %d: IDs 255..10000 are not supported by the one-time completion-flag cleanup job.",
            //     questId)
            L2_LOG_ERROR(Log,
                "[quest cleanup] Skipping quest ID %d: IDs 255..10000 are not supported by the one-time completion-flag cleanup job.",
                questId);
        } else {
            result.insert(questId);
        }
    }
    return result;
}

void MarkOneTimeQuestJobExecuted() {
    int bookingTime = 0;
    int jobType = 6;
    int jobValue = 3;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Prepare(L"{Call lin_ExecuteDBJob(?,?,?)}");
    connection.BindParameter(&bookingTime);
    connection.BindParameter(&jobType);
    connection.BindParameter(&jobValue);
    // Original: connection.ExecutePrepared(L"{Call lin_ExecuteDBJob(?,?,?)}")
    if (!connection.ExecutePrepared(L"{Call lin_ExecuteDBJob(?,?,?)}")) {
        L2_LOG_ERROR(Log,
            "[quest cleanup] Could not record completion of the one-time quest job (booking time 0, type 6, value 3). Earlier cleanup writes are not rolled back by this failure.");
    }
}

} // namespace

void CDBJobDeleteQuest::SaveQuestFlag(const QuestFlags& questFlags) {
    L2_GUARD_FUNCTION();
    QuestFlag flag{};
    int characterId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Prepare(L"{Call lin_UpdateOneTimeQuest (?, ?)}");
    connection.BindParameterBinary(&flag, sizeof(flag));
    connection.BindParameter(&characterId);

    std::size_t attempted = 0;
    std::size_t failed = 0;
    for (const auto& [currentCharacterId, currentFlag] : questFlags) {
        characterId = currentCharacterId;
        flag = currentFlag;
        ++attempted;
        if (!connection.ExecutePrepared(L"{Call lin_UpdateOneTimeQuest (?, ?)}")) {
            ++failed;
            // Log.Add(LOG_ERROR, "[DBJOB] saving modified quest_flag fail, char_id[%d] ", characterId)
            // Log.Add(LOG_ERROR,
            //     "[quest cleanup] Cannot save cleared one-time completion flags for character %d.",
            //     characterId)
            L2_LOG_ERROR(Log,
                "[quest cleanup] Cannot save cleared one-time completion flags for character %d.",
                characterId);
        }
        flag = QuestFlag{};
    }
    if (attempted != 0) {
        L2_LOG(Log, failed != 0 ? LOG_ERROR : LOG_NORMAL,
            "[quest cleanup] Character flag updates: %zu attempts, %zu succeeded, %zu failed.",
            attempted, attempted - failed, failed);
    }
}

CDBJobDeleteQuest::QuestFlags CDBJobDeleteQuest::ModifyQuestFlag(
    const QuestIds& normalizedQuestIds, const QuestFlags& questFlags
) {
    L2_GUARD_FUNCTION();
    QuestFlags result;
    for (const auto& [characterId, currentFlag] : questFlags) {
        QuestFlag modified = currentFlag;
        for (int questId : normalizedQuestIds) {
            ClearQuestBit(modified, questId);
        }
        result.emplace(characterId, std::move(modified));
    }
    return result;
}

CDBJobDeleteQuest::QuestFlags CDBJobDeleteQuest::LoadQuestFlags(const QuestIds& normalizedQuestIds) {
    L2_GUARD_FUNCTION();
    QuestFlags result;
    int characterId = 0;
    QuestFlag flag{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&characterId);
    connection.BindBinary(reinterpret_cast<char*>(&flag), static_cast<int>(sizeof(flag)));
    connection.Prepare(L"{Call lin_LoadAllOneTimeQuest}");
    if (!connection.ExecutePrepared(L"{Call lin_LoadAllOneTimeQuest}")) {
        L2_LOG_ERROR(Log,
            "[quest cleanup] Could not load character completion flags. No character flags are returned; the caller will still attempt to record the job as completed.");
        return result;
    }

    while (connection.Fetch()) {
        if (HasAnySelectedQuest(flag, normalizedQuestIds)) {
            result.emplace(characterId, flag);
        }
        flag = QuestFlag{};
    }
    return result;
}

void CDBJobDeleteQuest::PrintDeletedQuests(const QuestIds& originalQuestIds, const QuestFlags& questFlags) {
    L2_GUARD_FUNCTION();
    for (const auto& [characterId, flag] : questFlags) {
        QuestIds deletedQuestIds;
        for (int questId : originalQuestIds) {
            if (!IsQuestBitSet(flag, NormalizeQuestId(questId)))
                continue;
            deletedQuestIds.insert(questId);
        }
        const std::string deleted = JoinQuestIds(deletedQuestIds);
        // Log.Add(
        //     LOG_NORMAL, "[NO_ERROR] [DBJOB] char_id[%d] deleted onetime quest ID [%s]", characterId, deleted.c_str()
        // )
        // Log.Add(LOG_NORMAL,
        //     "[quest cleanup] Selected one-time completion flags for character %d: %s.",
        //     characterId, deleted.c_str())
        L2_LOG_INFO(Log,
            "[quest cleanup] Selected one-time completion flags for character %d: %s.",
            characterId, deleted.c_str());
    }
}

void CDBJobDeleteQuest::DoInternal(const QuestIds& originalQuestIds) {
    L2_GUARD_FUNCTION();
    const QuestIds normalizedQuestIds = NormalizeQuestIds(originalQuestIds);
    // Log.Add(LOG_NORMAL, "[NO_ERROR] [DBJOB] onetime quests deletion begins...")
    // Log.Add(LOG_NORMAL,
    //     "[quest cleanup] Clearing completion flags for %zu requested one-time quest IDs.",
    //     originalQuestIds.size())
    L2_LOG_INFO(Log,
        "[quest cleanup] Clearing completion flags for %zu requested one-time quest IDs.",
        originalQuestIds.size());

    const QuestFlags questFlags = LoadQuestFlags(normalizedQuestIds);
    if (questFlags.empty()) {
        // Log.Add(LOG_NORMAL, "[NO_ERROR] [DBJOB] onetime quest deletion done. (0 rows affected)")
        // Log.Add(LOG_NORMAL,
        //     "[quest cleanup] No matching character rows were loaded, no completion-flag updates submitted.")
        L2_LOG_INFO(Log,
            "[quest cleanup] No matching character rows were loaded; no completion-flag updates submitted.");
    } else {
        const QuestFlags modified = ModifyQuestFlag(normalizedQuestIds, questFlags);
        SaveQuestFlag(modified);
        // Noise: Per-character previews during bulk cleanup duplicate the
        // summary and are not evidence of successful saves. Save failures
        // already identify the affected character. Skip preview preparation.
        // PrintDeletedQuests(originalQuestIds, questFlags)
        // Log.Add(
        //     LOG_NORMAL,
        //     "[NO_ERROR] [DBJOB] onetime quest deletion done. "
        //     "number of deleted quests: %d, affected rows: %d",
        //     static_cast<int>(originalQuestIds.size()), static_cast<int>(questFlags.size())
        // )
        // Log.Add(LOG_NORMAL,
        //     "[quest cleanup] Processed %zu requested quest IDs across %zu loaded character rows. Failed saves are reported separately.",
        //     originalQuestIds.size(), questFlags.size())
        L2_LOG_INFO(Log,
            "[quest cleanup] Processed %zu requested quest IDs across %zu loaded character rows; failed saves are reported separately.",
            originalQuestIds.size(), questFlags.size());
    }

    // Current 0x004aa0e8 records this marker even when no character row was
    // affected. The PostgreSQL function owns idempotence.
    MarkOneTimeQuestJobExecuted();
    // Noise: The row summary already reports processing. The unchecked completion-marker write does not prove success.
    // Updated Noise rationale: marker execution failures are now reported in
    // MarkOneTimeQuestJobExecuted, but a successful marker still does not prove
    // that every preceding character update succeeded. Keep the generic success
    // message disabled. SaveQuestFlag reports the actual update counts.
    // Log.Add(LOG_NORMAL, "[NO_ERROR] [DBJOB] Execute DBJOB Finished!")
}

void CDBJobDeleteQuest::ExecuteOneTimeQuestDeletion() {
    const QuestIds questIds = LoadQuestListForDBJob();
    if (!questIds.empty())
        DoInternal(questIds);
}
