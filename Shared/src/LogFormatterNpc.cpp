#include "Shared/src/LogFormatter.h"

#include "L2NPC/src/NPC.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/SharedFactory.h"

#include <cmath>

namespace {

struct NpcActorLogFields {
    std::int64_t databaseId = 0;
    int race = -1;
    int sex = 2;
    int classType = -1;
    int level = 0;
    FVector position;
    const char16_t* name = nullptr;
};

NpcActorLogFields ReadActorFields(CSharedCreatureData* talker, const FVector& sourcePosition) {
    NpcActorLogFields fields;
    fields.position = sourcePosition;
    if (!talker) {
        return fields;
    }

    fields.databaseId = talker->m_nDBID;
    fields.race = talker->m_Race;
    fields.sex = talker->m_Sex;
    fields.classType = static_cast<int>(talker->m_nClass);
    fields.level = talker->m_nLevel;
    fields.position = talker->GetPos();
    fields.name = talker->Name();
    return fields;
}

} // namespace

void CLogFormatter::WriteLogNpc1(
    int nLogId,
    CNPC& actor,
    CSharedCreatureData* pTalker,
    char16_t* strEtcStr2_50,
    char16_t* strEtcStr3_50,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10,
    std::int64_t nItemId
) {
    // Current L2NPC.exe 0x0046058c. The source pointer is deliberately
    // loaded twice: the target keeps the first position/class reads, then
    // reloads CNPC::m_data immediately before formatting the live DBID/AI.
    CSharedCreatureData* const initialSource = actor.m_data;
    const FVector sourcePosition = initialSource->GetPos();
    const int sourceClassId = initialSource->m_nClassID;
    const NpcActorLogFields talkerFields = ReadActorFields(pTalker, sourcePosition);
    const std::u16string databaseName = actor.GetDBName();
    CSharedCreatureData* const source = actor.m_data;
    const char16_t* const actorName = actor.GetName();

    WriteLog(
        nLogId, source->m_nDBID, static_cast<std::int64_t>(sourceClassId) - 1000000, talkerFields.databaseId, 0,
        runtime::Win64CvttDoubleToInt32(talkerFields.position.x),
        runtime::Win64CvttDoubleToInt32(talkerFields.position.y),
        runtime::Win64CvttDoubleToInt32(talkerFields.position.z), source->m_sAI, strEtcStr2_50, strEtcStr3_50,
        talkerFields.race, talkerFields.sex, talkerFields.classType, talkerFields.level, nEtcNum5, nEtcNum6, nEtcNum7,
        nEtcNum8, nEtcNum9, nEtcNum10, actorName, databaseName.c_str(), talkerFields.name, nullptr, 0
    );
}

void CLogFormatter::WriteLogNpc2(
    int nLogId,
    CNPC& actor,
    CSharedCreatureData* pTalker,
    char16_t* strEtcStr2_50,
    char16_t* strEtcStr3_50,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10,
    std::int64_t nItemId
) {
    // Current L2NPC.exe 0x004607c4. Preserve the same two m_data reads and
    // pass the talker name as a direct pointer into shared memory.
    CSharedCreatureData* const initialSource = actor.m_data;
    const FVector sourcePosition = initialSource->GetPos();
    const int sourceClassId = initialSource->m_nClassID;
    const NpcActorLogFields talkerFields = ReadActorFields(pTalker, sourcePosition);
    const std::u16string databaseName = actor.GetDBName();
    CSharedCreatureData* const source = actor.m_data;
    const char16_t* const actorName = actor.GetName();

    WriteLog(
        nLogId, talkerFields.databaseId, 0, source->m_nDBID, static_cast<std::int64_t>(sourceClassId) - 1000000,
        runtime::Win64CvttDoubleToInt32(talkerFields.position.x),
        runtime::Win64CvttDoubleToInt32(talkerFields.position.y),
        runtime::Win64CvttDoubleToInt32(talkerFields.position.z), source->m_sAI, strEtcStr2_50, strEtcStr3_50,
        talkerFields.race, talkerFields.sex, talkerFields.classType, talkerFields.level, nEtcNum5, nEtcNum6, nEtcNum7,
        nEtcNum8, nEtcNum9, nEtcNum10, talkerFields.name, nullptr, actorName, databaseName.c_str(), 0
    );
}

void CLogFormatter::WriteLogNpc3(
    int nLogId,
    CNPC& actor,
    CSharedCreatureData* pTalker,
    char16_t* strEtcStr2_50,
    char16_t* strEtcStr3_50,
    std::int64_t nEtcNum5,
    std::int64_t nEtcNum6,
    std::int64_t nEtcNum7,
    std::int64_t nEtcNum8,
    std::int64_t nEtcNum9,
    std::int64_t nEtcNum10,
    std::int64_t nItemId
) {
    // Current L2NPC.exe 0x004602f4. Unlike WriteLogNpc1/2 this layout has no
    // DB-name temporary, but it keeps the same initial/readback split.
    CSharedCreatureData* const initialSource = actor.m_data;
    const FVector sourcePosition = initialSource->GetPos();
    const int sourceClassId = initialSource->m_nClassID;
    const NpcActorLogFields talkerFields = ReadActorFields(pTalker, sourcePosition);
    CSharedCreatureData* const source = actor.m_data;
    const char16_t* const actorName = actor.GetName();

    WriteLog(
        nLogId, talkerFields.databaseId, 0, static_cast<std::int64_t>(sourceClassId) - 1000000, source->m_nDBID,
        runtime::Win64CvttDoubleToInt32(talkerFields.position.x),
        runtime::Win64CvttDoubleToInt32(talkerFields.position.y),
        runtime::Win64CvttDoubleToInt32(talkerFields.position.z), source->m_sAI, strEtcStr2_50, strEtcStr3_50,
        talkerFields.race, talkerFields.sex, talkerFields.classType, talkerFields.level, nEtcNum5, nEtcNum6, nEtcNum7,
        nEtcNum8, nEtcNum9, nEtcNum10, talkerFields.name, nullptr, nullptr, actorName, 0
    );
}
