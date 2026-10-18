#pragma once

#include "Shared/src/Utils.h"
#include "Shared/src/StandardContainers.h"

#include <cstdint>

class CDBJobDeleteQuest {
public:
    // Current guard signatures name these exact L2Core/xstd owners. The
    // 2048-bit value is the 0x100-byte CBitStorage compiled from Utils.h, not
    // an interchangeable host byte array.
    using QuestFlag = CBitStorage<2048, std::int32_t, 64>;
    using QuestIds = std::set<int>;
    using QuestFlags = std::map<int, QuestFlag>;

    // Exact current owners and signatures recovered from guard literals.
    static void SaveQuestFlag(const QuestFlags& questFlags);
    static QuestFlags ModifyQuestFlag(const QuestIds& normalizedQuestIds, const QuestFlags& questFlags);
    static QuestFlags LoadQuestFlags(const QuestIds& normalizedQuestIds);
    static void PrintDeletedQuests(const QuestIds& originalQuestIds, const QuestFlags& questFlags);
    static void DoInternal(const QuestIds& originalQuestIds);

    // Semantic name for unnamed current wrapper 0x004aa3a0. It loads
    // lin_LoadOneTimeQuestListForDBJob and calls DoInternal only when the
    // returned set is non-empty.
    static void ExecuteOneTimeQuestDeletion();
};
