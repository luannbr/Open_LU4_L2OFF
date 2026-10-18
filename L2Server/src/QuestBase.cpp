#include "QuestBase.h"
#include "AtomicJobServerSide.h"
#include "CConfig.h"
#include "Castle.h"
#include "FieldCycle.h"
#include "HTMLCache.h"
#include "Item.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Quest.h"
#include "Residence.h"
#include "SerializedMessage.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/CategoryDataDB.h"
#include "Shared/src/FHtml.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cwchar>

namespace {


template <typename TList>
void DeletePointers(TList& list) {
    for (auto* const value : list) {
        delete value;
    }
    list.clear();
}

const wchar_t* QuestCompareHostCode(const QuestCompareOperationType operation) noexcept {
    switch (operation) {
        case QCOT_NOT_EQUAL:
            return L"!=";
        case QCOT_GRATER_THEN_AND_EQUAL:
            return L">=";
        case QCOT_LESS_THEN_AND_EQUAL:
            return L"<=";
        case QCOT_GRATER_THEN:
            return L">";
        case QCOT_LESS_THEN:
            return L"<";
        case QCOT_EQUAL:
        default:
            return L"==";
    }
}

} // namespace

bool UserQuestCondition_False::Check(User&) { return false; }

void UserQuestCondition_False::ConvertToAICode(std::u16string&) {}

bool UserQuestCondition_OneTime::Check(User& user) {
    // Current L2Server.exe 0x00a06104.
    L2_GUARD_FUNCTION();
    return user.IsDoneQuest(static_cast<int>(m_questID)) == m_state;
}

void UserQuestCondition_OneTime::ConvertToAICode(std::u16string& code) {
    // Current L2Server.exe 0x00a0a260.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 100> buffer{};
    FormatUtf16To(
        buffer.data(), buffer.size(), u"GetOneTimeQuestFlag(talker, %lld) == %d", static_cast<long long>(m_questID),
        m_state ? 1 : 0
    );
    if (!code.empty()) {
        code += u" && ";
    }
    const std::u16string target = std::u16string(buffer.data());
    code.append(std::u16string_view(target));
}

bool UserQuestCondition_Item::Check(User& user) {
    // Current L2Server.exe 0x00a061b8 truncates HasItem's
    // 64-bit result before applying the generated comparison.
    L2_GUARD_FUNCTION();
    const int count = static_cast<int>(user.HasItem(static_cast<unsigned int>(m_itemClassID)));
    return QCOT_Compare(m_count, m_compareOperation, count);
}

void UserQuestCondition_Item::ConvertToAICode(std::u16string& code) {
    // Current L2Server.exe 0x00a0a37c.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 100> buffer{};
    FormatUtf16To(
        buffer.data(), buffer.size(), u"OwnItemCount(talker, %lld) %ls %d", static_cast<long long>(m_itemClassID),
        QuestCompareHostCode(m_compareOperation), m_count
    );
    if (!code.empty()) {
        code += u" && ";
    }
    const std::u16string target = std::u16string(buffer.data());
    code.append(std::u16string_view(target));
}

bool UserQuestCondition_LevelRange::Check(User& user) {
    // Current L2Server.exe 0x00a062b8 calls Level twice when an upper bound
    // is present.
    L2_GUARD_FUNCTION();
    if (user.Level() < m_from) {
        return false;
    }
    return m_up == 0 || user.Level() <= m_up;
}

void UserQuestCondition_LevelRange::ConvertToAICode(std::u16string& code) {
    // Current L2Server.exe 0x00a0a4a0 keeps the upper comparison even
    // when the runtime treats zero as an unbounded range.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 100> buffer{};
    FormatUtf16To(buffer.data(), buffer.size(), u"talker.level >= %d && talker.level <= %d", m_from, m_up);
    if (!code.empty()) {
        code += u" && ";
    }
    const std::u16string target = std::u16string(buffer.data());
    code.append(std::u16string_view(target));
}

bool UserQuestCondition_Class::Check(User& user) {
    // Current L2Server.exe 0x00a06384.
    L2_GUARD_FUNCTION();
    const std::int64_t occupation = user.GetClass();
    return m_classIDFrom <= occupation && occupation <= m_classIDTo;
}

void UserQuestCondition_Class::ConvertToAICode(std::u16string&) {
    // Current L2Server.exe 0x00a06440 is empty.
    L2_GUARD_FUNCTION();
}

bool UserQuestCondition_Party::Check(User& user) {
    // Current L2Server.exe 0x00a064bc.
    L2_GUARD_FUNCTION();
    return user.m_data->m_nPartyId != 0;
}

void UserQuestCondition_Party::ConvertToAICode(std::u16string&) {
    // Current L2Server.exe 0x00a0655c is empty.
    L2_GUARD_FUNCTION();
}

bool UserQuestCondition_KillNpc::Check(User& user) {
    // Current L2Server.exe 0x00a065d8.
    L2_GUARD_FUNCTION();
    const int count = user.GetQuestNPCLog(m_questID, USER_NPC_LOG_QUEST_HUNT, m_classID);
    return QCOT_Compare(m_count, m_compareOperation, count);
}

void UserQuestCondition_KillNpc::ConvertToAICode(std::u16string& code) {
    // Current L2Server.exe 0x00a0a5b4.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 100> buffer{};
    FormatUtf16To(
        buffer.data(), buffer.size(), u"GetNPCLogByID(talker, %d, %d) %ls %d", m_questID, m_classID,
        QuestCompareHostCode(m_compareOperation), m_count
    );
    if (!code.empty()) {
        code += u" && ";
    }
    const std::u16string target = std::u16string(buffer.data());
    code.append(std::u16string_view(target));
}

bool UserQuestCondition_Daily::Check(User& user) {
    // Current L2Server.exe 0x00a066dc.
    L2_GUARD_FUNCTION();
    return user.m_data->GetDailyQuestFlag(m_questID) == m_state;
}

void UserQuestCondition_Daily::ConvertToAICode(std::u16string& code) {
    // Current L2Server.exe 0x00a0a6dc.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 100> buffer{};
    FormatUtf16To(buffer.data(), buffer.size(), u"GetDailyQuestFlag(talker, %d) == %d", m_questID, m_state ? 1 : 0);
    if (!code.empty()) {
        code += u" && ";
    }
    const std::u16string target = std::u16string(buffer.data());
    code.append(std::u16string_view(target));
}

bool UserQuestCondition_HaveMemo::Check(User& user) {
    // Current L2Server.exe 0x00a0678c performs the query once for logging
    // and once again for the returned comparison.
    L2_GUARD_FUNCTION();
    const bool loggedState = user.m_data->HaveMemo(QUEST_NORMAL, m_questID);
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] cond_memo QuestID[%d] ScriptState[%s] UserState[%s] User[%s]",
        //     m_questID,
        //     m_state ? L"true" : L"false",
        //     loggedState ? L"true" : L"false",
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] cond_memo QuestID[%d] ScriptState[%s] UserState[%s] User[%s]",
        //     m_questID,
        //     m_state ? L"true" : L"false",
        //     loggedState ? L"true" : L"false",
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d memo check for character '%s' (DBID %u): script expects %s, observed memo presence %s.",
        //     m_questID, WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), m_state ? "present"
        //     : "absent", loggedState ? "present" : "absent")
        L2_LOG_INFO(
            Log, "Quest %d memo check for character '%s' (DBID %u): script expects %s, observed memo presence %s.",
            m_questID, Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
            m_state ? "present" : "absent", loggedState ? "present" : "absent"
        );
    }
    return user.m_data->HaveMemo(QUEST_NORMAL, m_questID) == m_state;
}

void UserQuestCondition_HaveMemo::ConvertToAICode(std::u16string& code) {
    // Current L2Server.exe 0x00a0a7f8.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 100> buffer{};
    FormatUtf16To(buffer.data(), buffer.size(), u"HaveMemo(talker, %d) == %d", m_questID, m_state ? 1 : 0);
    if (!code.empty()) {
        code += u" && ";
    }
    const std::u16string target = std::u16string(buffer.data());
    code.append(std::u16string_view(target));
}

bool UserQuestCondition_MemoState::Check(User& user) {
    // Current L2Server.exe 0x00a068b4 keeps the target's inverted
    // ScriptValue/UserValue logging lanes.
    // The operator-log overlay below labels the script and observed values correctly.
    L2_GUARD_FUNCTION();
    if (m_bLogToError) {
        const int current = user.m_data->GetMemoState(m_questID, m_slotID);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] cond_memo QuestID[%d] ScriptValue[%d] UserValue[%d] User[%s]",
        //     m_questID,
        //     current,
        //     m_value,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] cond_memo QuestID[%d] ScriptValue[%d] UserValue[%d] User[%s]",
        //     m_questID,
        //     current,
        //     m_value,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d memo slot %d check for character '%s' (DBID %u): script expects %d, observed value %d.",
        //     m_questID, m_slotID, WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), m_value,
        //     current)
        L2_LOG_INFO(
            Log, "Quest %d memo slot %d check for character '%s' (DBID %u): script expects %d, observed value %d.",
            m_questID, m_slotID, Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), m_value,
            current
        );
    }
    return user.m_data->GetMemoState(m_questID, m_slotID) == m_value;
}

void UserQuestCondition_MemoState::ConvertToAICode(std::u16string& code) {
    // Current L2Server.exe 0x00a0a914.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 100> buffer{};
    FormatUtf16To(buffer.data(), buffer.size(), u"GetMemoStateEx(talker, %d, %d) == %d", m_questID, m_slotID, m_value);
    if (!code.empty()) {
        code += u" && ";
    }
    const std::u16string target = std::u16string(buffer.data());
    code.append(std::u16string_view(target));
}

bool UserQuestCondition_Category::Check(User& user) {
    // Current L2Server.exe 0x00a0be08 supports only != and ==.
    L2_GUARD_FUNCTION();
    if (m_eCompareType == QCOT_NOT_EQUAL) {
        return !CCategoryDataDB::Inst().IsInCategory(user.GetClass(), m_nCategoryID);
    }
    if (m_eCompareType == QCOT_EQUAL) {
        return CCategoryDataDB::Inst().IsInCategory(user.GetClass(), m_nCategoryID);
    }
    return false;
}

void UserQuestCondition_Category::ConvertToAICode(std::u16string& code) {
    // Current L2Server.exe 0x00a0aa34 still appends an empty expression for
    // an unsupported comparison.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 100> buffer{};
    if (m_eCompareType == QCOT_NOT_EQUAL) {
        FormatUtf16To(buffer.data(), buffer.size(), u"IsInCategory(%d, talker.occupation) == 0", m_nCategoryID);
    } else if (m_eCompareType == QCOT_EQUAL) {
        FormatUtf16To(buffer.data(), buffer.size(), u"IsInCategory(%d, talker.occupation) == 1", m_nCategoryID);
    }
    if (!code.empty()) {
        code += u" && ";
    }
    const std::u16string target = std::u16string(buffer.data());
    code.append(std::u16string_view(target));
}

bool UserQuestCondition_Random::Check(User&) {
    // Current L2Server.exe 0x00a07980.
    L2_GUARD_FUNCTION();
    int roll = m_nHigh;
    if (roll < 1) {
        // Log.Add(LOG_ERROR, L"nHigh [%d] in Random", roll)
        // L2_LOG_ERROR(Log, L"nHigh [%d] in Random", roll)
        L2_LOG_WARN(
            Log, "Random quest condition has upper bound %d below 1; using fixed roll 1 and comparing it with threshold %d.",
            roll, m_nProb
        );
        roll = 1;
    } else {
        roll = static_cast<int>(genrand64_real2() * static_cast<double>(roll) + 1.0);
    }
    return roll <= m_nProb;
}

void UserQuestCondition_Random::ConvertToAICode(std::u16string& code) {
    // Current L2Server.exe 0x00a0ab5c keeps '<' although Check uses <=
    // after producing the one-based roll.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 100> buffer{};
    FormatUtf16To(buffer.data(), buffer.size(), u"Rand(%d) < %d", m_nHigh, m_nProb);
    if (!code.empty()) {
        code += u" && ";
    }
    const std::u16string target = std::u16string(buffer.data());
    code.append(std::u16string_view(target));
}

bool UserQuestCondition_AcquiredAbilityPoint::Check(User& user) {
    // Current L2Server.exe 0x00a069c0 sums the three acquired-count lanes
    // directly with 32-bit machine wraparound.
    L2_GUARD_FUNCTION();
    const int acquired = runtime::AddInt32Wrapping(
        user.m_data->m_stPotentialCount.nCount1,
        runtime::AddInt32Wrapping(user.m_data->m_stPotentialCount.nCount2, user.m_data->m_stPotentialCount.nCount3)
    );
    return QCOT_Compare(m_nPoint, m_compareOperation, acquired);
}

void UserQuestCondition_AcquiredAbilityPoint::ConvertToAICode(std::u16string& code) {
    // Current L2Server.exe 0x00a0ac70 keeps the target typo Aquired.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 100> buffer{};
    FormatUtf16To(
        buffer.data(), buffer.size(), u"GetAquiredAbilityPoint(talker) %ls %d", QuestCompareHostCode(m_compareOperation),
        m_nPoint
    );
    if (!code.empty()) {
        code += u" && ";
    }
    const std::u16string target = std::u16string(buffer.data());
    code.append(std::u16string_view(target));
}

bool UserQuestCondition_MonsterBookFactionAchievement::Check(User& user) {
    // Current L2Server.exe 0x00a06acc.
    L2_GUARD_FUNCTION();
    return user.CheckMonsterBookFactionTrophyQuest(m_nFactionId, m_nTrophyLevel, m_nCardCount);
}

void UserQuestCondition_MonsterBookFactionAchievement::ConvertToAICode(std::u16string&) {
    // Current L2Server.exe 0x00a06b7c is empty.
    L2_GUARD_FUNCTION();
}

UserQuestCondition_FieldCycleStep::UserQuestCondition_FieldCycleStep(
    const int fieldCycleId, const QuestCompareOperationType compareType, const int value
) noexcept :
    m_nFieldCycleID(fieldCycleId), m_eCompareType(compareType), m_nValue(value) {}

bool UserQuestCondition_FieldCycleStep::Check(User&) {
    // Current L2Server.exe 0x00a07a68.
    L2_GUARD_FUNCTION();
    CFieldCycle* const fieldCycle = g_FieldCycleSystem.GetFieldCycle(m_nFieldCycleID);
    if (fieldCycle == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"QuestBase.cpp",
        //     0x363)
        // L2_LOG_ERROR(Log,
        //     L"[FIELD_CYCLE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"QuestBase.cpp",
        //     0x363)
        L2_LOG_ERROR(
            Log,
            "Cannot evaluate the quest step condition: field cycle %d is unavailable (comparison code %d, configured step "
            "%d); the condition returns false.",
            m_nFieldCycleID, static_cast<int>(m_eCompareType), m_nValue
        );
        return false;
    }
    return QCOT_Compare(m_nValue, m_eCompareType, fieldCycle->GetMyInfo().nowStep);
}

void UserQuestCondition_FieldCycleStep::ConvertToAICode(std::u16string&) {}

bool QuestOperation_CompleteQuest::Do(User& user, AtomicJobList*, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a08c68 and serialized handler 0x00a07ffc.
    L2_GUARD_FUNCTION();
    const int questId = static_cast<int>(m_questID);
    const bool logToError = m_bLogToError;
    user.PushSerializedMessage(MakeLambdaMessage([questId, npcId, logToError](CCreature* const creature) {
        L2_GUARD_FUNCTION();
        User* const target = creature != nullptr ? creature->CastUser() : nullptr;
        if (target == nullptr) {
            return;
        }
        target->CompleteQuest(questId);
        if (logToError) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[UserQuestData] QuestOperation_CompleteQuest Called. QuestID(%d), NPC(%d), User(%s)",
            //     questId,
            //     npcId,
            //     target->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[UserQuestData] QuestOperation_CompleteQuest Called. QuestID(%d), NPC(%d), User(%s)",
            //     questId,
            //     npcId,
            //     target->GetName())
            // L2_LOG_INFO(Log,
            //     "Processed quest %d completion request for character '%s' (DBID %u), NPC %d.",
            //     questId, WideToUtf8(target->GetName()).c_str(), static_cast<unsigned int>(target->GetDBID()), npcId)
            L2_LOG_INFO(
                Log, "Processed quest %d completion request for character '%s' (DBID %u), NPC %d.", questId,
                Utf16ToUtf8(target->GetName()).c_str(), static_cast<unsigned int>(target->GetDBID()), npcId
            );
        }
        g_FieldCycleSystem.OnUserCompleteQuest(target, questId);
    }));
    return true;
}

bool QuestOperation_DeleteQuest::Do(User& user, AtomicJobList*, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a08d40 and serialized handler 0x00a08114.
    L2_GUARD_FUNCTION();
    const int questId = static_cast<int>(m_questID);
    const bool logToError = m_bLogToError;
    user.PushSerializedMessage(MakeLambdaMessage([questId, npcId, logToError](CCreature* const creature) {
        L2_GUARD_FUNCTION();
        User* const target = creature != nullptr ? creature->CastUser() : nullptr;
        if (target == nullptr) {
            return;
        }
        target->DeleteQuest(questId);
        if (logToError) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[UserQuestData] QuestOperation_DeleteQuest Called. QuestID(%d), NPC(%d), User(%s)",
            //     questId,
            //     npcId,
            //     target->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[UserQuestData] QuestOperation_DeleteQuest Called. QuestID(%d), NPC(%d), User(%s)",
            //     questId,
            //     npcId,
            //     target->GetName())
            // L2_LOG_INFO(Log,
            //     "Processed quest %d deletion request for character '%s' (DBID %u), NPC %d.",
            //     questId, WideToUtf8(target->GetName()).c_str(), static_cast<unsigned int>(target->GetDBID()), npcId)
            L2_LOG_INFO(
                Log, "Processed quest %d deletion request for character '%s' (DBID %u), NPC %d.", questId,
                Utf16ToUtf8(target->GetName()).c_str(), static_cast<unsigned int>(target->GetDBID()), npcId
            );
        }
    }));
    return true;
}

bool QuestOperation_CreateMemo::Do(User& user, AtomicJobList* const jobs, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a0ad8c.
    L2_GUARD_FUNCTION();
    jobs->AddAtomicJob(new MemoControl(user.Id(), MemoControl::SET_MEMO, static_cast<int>(m_questID), 0, 0));
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_CreateMemo Called. QuestID(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_CreateMemo Called. QuestID(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: added memo creation to the pending atomic jobs.",
        //     static_cast<int>(m_questID), WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
        //     npcId)
        L2_LOG_INFO(
            Log, "Quest %d for character '%s' (DBID %u), NPC %d: added memo creation to the pending atomic jobs.",
            static_cast<int>(m_questID), Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
            npcId
        );
    }
    return true;
}

bool QuestOperation_DestroyMemo::Do(User& user, AtomicJobList* const jobs, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a0af04.
    L2_GUARD_FUNCTION();
    jobs->AddAtomicJob(new MemoControl(user.Id(), MemoControl::REMOVE, static_cast<int>(m_questID), 0, 0));
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_DestroyMemo Called. QuestID(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_DestroyMemo Called. QuestID(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: added memo removal to the pending atomic jobs.",
        //     static_cast<int>(m_questID), WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
        //     npcId)
        L2_LOG_INFO(
            Log, "Quest %d for character '%s' (DBID %u), NPC %d: added memo removal to the pending atomic jobs.",
            static_cast<int>(m_questID), Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
            npcId
        );
    }
    return true;
}

bool QuestOperation_SetMemoFlag::Do(User& user, AtomicJobList* const jobs, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a0b080.
    L2_GUARD_FUNCTION();
    jobs->AddAtomicJob(
        new MemoControl(user.Id(), MemoControl::SET_JOURNAL, static_cast<int>(m_questID), static_cast<int>(m_value), 0)
    );
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_SetMemoFlag Called. QuestID(%d), Journal(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     static_cast<int>(m_value),
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_SetMemoFlag Called. QuestID(%d), Journal(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     static_cast<int>(m_value),
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: added journal value %d to the pending memo jobs.",
        //     static_cast<int>(m_questID), WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
        //     npcId, static_cast<int>(m_value))
        L2_LOG_INFO(
            Log, "Quest %d for character '%s' (DBID %u), NPC %d: added journal value %d to the pending memo jobs.",
            static_cast<int>(m_questID), Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
            npcId, static_cast<int>(m_value)
        );
    }
    return true;
}

bool QuestOperation_SetMemoState::Do(User& user, AtomicJobList* const jobs, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a0b204.
    L2_GUARD_FUNCTION();
    user.UpdateProgress(static_cast<int>(m_questID), static_cast<int>(m_value));
    jobs->AddAtomicJob(new MemoControl(
        user.Id(), MemoControl::SET_STATE, static_cast<int>(m_questID), static_cast<int>(m_slot), static_cast<int>(m_value)
    ));
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_SetMemoState Called. QuestID(%d), Slot(%d), Value(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     static_cast<int>(m_slot),
        //     static_cast<int>(m_value),
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_SetMemoState Called. QuestID(%d), Slot(%d), Value(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     static_cast<int>(m_slot),
        //     static_cast<int>(m_value),
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: requested progress value %d and added memo slot %d update to
        //     the pending jobs.", static_cast<int>(m_questID), WideToUtf8(user.GetName()).c_str(), static_cast<unsigned
        //     int>(user.GetDBID()), npcId, static_cast<int>(m_value), static_cast<int>(m_slot))
        L2_LOG_INFO(
            Log,
            "Quest %d for character '%s' (DBID %u), NPC %d: requested progress value %d and added memo slot %d update to "
            "the pending jobs.",
            static_cast<int>(m_questID), Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
            npcId, static_cast<int>(m_value), static_cast<int>(m_slot)
        );
    }
    return true;
}

bool QuestOperation_StartJournal::Do(User& user, AtomicJobList*, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a06d80.
    L2_GUARD_FUNCTION();
    user._StartJournal(static_cast<int>(m_questID), static_cast<int>(m_journal));
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_StartJournal Called. QuestID(%d), Journal(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     static_cast<int>(m_journal),
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_StartJournal Called. QuestID(%d), Journal(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     static_cast<int>(m_journal),
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Processed quest %d journal %d start request for character '%s' (DBID %u), NPC %d.",
        //     static_cast<int>(m_questID), static_cast<int>(m_journal), WideToUtf8(user.GetName()).c_str(),
        //     static_cast<unsigned int>(user.GetDBID()), npcId)
        L2_LOG_INFO(
            Log, "Processed quest %d journal %d start request for character '%s' (DBID %u), NPC %d.",
            static_cast<int>(m_questID), static_cast<int>(m_journal), Utf16ToUtf8(user.GetName()).c_str(),
            static_cast<unsigned int>(user.GetDBID()), npcId
        );
    }
    return true;
}

bool QuestOperation_CompleteJournal::Do(User& user, AtomicJobList*, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a06e78.
    L2_GUARD_FUNCTION();
    user._CompleteJournal(static_cast<int>(m_questID), static_cast<int>(m_journal));
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_CompleteJournal Called. QuestID(%d), Journal(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     static_cast<int>(m_journal),
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_CompleteJournal Called. QuestID(%d), Journal(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     static_cast<int>(m_journal),
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Processed quest %d journal %d completion request for character '%s' (DBID %u), NPC %d.",
        //     static_cast<int>(m_questID), static_cast<int>(m_journal), WideToUtf8(user.GetName()).c_str(),
        //     static_cast<unsigned int>(user.GetDBID()), npcId)
        L2_LOG_INFO(
            Log, "Processed quest %d journal %d completion request for character '%s' (DBID %u), NPC %d.",
            static_cast<int>(m_questID), static_cast<int>(m_journal), Utf16ToUtf8(user.GetName()).c_str(),
            static_cast<unsigned int>(user.GetDBID()), npcId
        );
    }
    return true;
}

bool QuestOperation_MonitorStart_KillNpc::Do(User& user, AtomicJobList*, ItemManipList&, int) const {
    // Current L2Server.exe 0x00a06c18.
    L2_GUARD_FUNCTION();
    user.m_EventMonitor.AddMonitorItem(EventMonitor::ET_KILL_NPC, m_npcClassID, 0);
    return true;
}

bool QuestOperation_MonitorEnd_KillNpc::Do(User& user, AtomicJobList*, ItemManipList&, int) const {
    // Current L2Server.exe 0x00a06ccc.
    L2_GUARD_FUNCTION();
    user.m_EventMonitor.RemoveMonitorItem(EventMonitor::ET_KILL_NPC, m_npcClassID, 0);
    return true;
}

bool QuestOperation_ShowHTML::Do(User& user, AtomicJobList*, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a08714.
    L2_GUARD_FUNCTION();
    const std::u16string fileName = SanitizeUtf16(m_wstrName);
    std::u16string html;
    if (!g_HTMLCache.GetHTMLFile(m_wstrName.c_str(), html, user.m_data->m_CountryCode)) {
        return false;
    }
    const std::u16string hostHtml(html);
    user.ShowHTML(fileName.c_str(), hostHtml.c_str(), 0, HTML::LAUNCH, true);
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_ShowHTML Called. QuestID(%d), HTML(%s), NPC(%d), User(%s)",
        //     m_questID,
        //     fileName.c_str(),
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_ShowHTML Called. QuestID(%d), HTML(%s), NPC(%d), User(%s)",
        //     m_questID,
        //     fileName.c_str(),
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: loaded HTML '%s' and submitted it for display.",
        //     m_questID, WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId,
        //     WideToUtf8(fileName).c_str())
        L2_LOG_INFO(
            Log, "Quest %d for character '%s' (DBID %u), NPC %d: loaded HTML '%s' and submitted it for display.", m_questID,
            Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId,
            Utf16ToUtf8(fileName).c_str()
        );
    }
    return true;
}

bool QuestOperation_ShowHTML_If::Do(User& user, AtomicJobList*, ItemManipList&, int) const {
    // Current L2Server.exe 0x00a07c28.
    L2_GUARD_FUNCTION();
    if (static_cast<std::int64_t>(user.m_questError) == m_cond) {
        const std::u16string fileName(m_wstrName);
        user.ShowHTML(fileName.c_str());
    }
    return true;
}

bool QuestOperation_GiveItem::Do(User& user, AtomicJobList*, ItemManipList& itemManipulations, const int npcId) const {
    // Current L2Server.exe 0x00a097e8.
    L2_GUARD_FUNCTION();
    std::int64_t amount = m_amount;
    if (g_bFreeOpenServer) {
        const int questId = static_cast<int>(m_questID);
        if (((questId < 0) || (questId > 0xfe)) && static_cast<std::uint32_t>(questId - 0x280f) > 0x700U) {
            if (m_itemClassID == 0x39) {
                int maximum = 0;
                if (user.m_data->m_nLevel < 40) {
                    maximum = 1000;
                } else if (user.m_data->m_nLevel < 52) {
                    maximum = 4000;
                } else if (user.m_data->m_nLevel <= 60) {
                    maximum = 7000;
                }
                if (maximum != 0) {
                    const int truncatedAmount = static_cast<int>(amount);
                    amount = truncatedAmount <= maximum ? truncatedAmount : maximum;
                }
            }
        } else if (m_itemClassID == 0x39 && g_mapF2PRewardPenaltyQuests.find(questId) != g_mapF2PRewardPenaltyQuests.end()) {
            amount = runtime::Win64CvttDoubleToInt64(static_cast<double>(amount) * g_dF2PQuestRewardPenaltyPer);
        }
    }

    if (amount > 0) {
        itemManipulations.Push(ItemManip(
            static_cast<int>(m_questID), static_cast<int>(m_itemClassID), amount, static_cast<int>(m_enchant), 0, 0, true
        ));
    }
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_GiveItem Called. QuestID(%d), ItemType(%d), Amount(%d), Enchant(%d), NPC(%d),
        //     User(%s)", static_cast<int>(m_questID), static_cast<int>(m_itemClassID), static_cast<int>(amount),
        //     static_cast<int>(m_enchant),
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_GiveItem Called. QuestID(%d), ItemType(%d), Amount(%d), Enchant(%d), NPC(%d),
        //     User(%s)", static_cast<int>(m_questID), static_cast<int>(m_itemClassID), static_cast<int>(amount),
        //     static_cast<int>(m_enchant),
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: give-item entry %s for item class %d, adjusted amount %lld
        //     (configured %lld), enchant %d.", static_cast<int>(m_questID), WideToUtf8(user.GetName()).c_str(),
        //     static_cast<unsigned int>(user.GetDBID()), npcId, amount > 0 ? "added to pending manipulations" : "omitted
        //     because the amount is not positive", static_cast<int>(m_itemClassID), static_cast<long long>(amount),
        //     static_cast<long long>(m_amount), static_cast<int>(m_enchant))
        L2_LOG_INFO(
            Log,
            "Quest %d for character '%s' (DBID %u), NPC %d: give-item entry %s for item class %d, adjusted amount %lld "
            "(configured %lld), enchant %d.",
            static_cast<int>(m_questID), Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
            npcId, amount > 0 ? "added to pending manipulations" : "omitted because the amount is not positive",
            static_cast<int>(m_itemClassID), static_cast<long long>(amount), static_cast<long long>(m_amount),
            static_cast<int>(m_enchant)
        );
    }
    return true;
}

bool QuestOperation_DeleteItem::Do(User& user, AtomicJobList*, ItemManipList& itemManipulations, const int npcId) const {
    // Current L2Server.exe 0x00a07088 ignores m_enchant.
    L2_GUARD_FUNCTION();
    itemManipulations.Push(
        ItemManip(0, static_cast<int>(m_itemClassID), runtime::NegateInt64Wrapping(m_amount), 0, 0, 0, true)
    );
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_DeleteItem Called. QuestID(%d), ItemType(%d), Amount(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     static_cast<int>(m_itemClassID),
        //     static_cast<int>(m_amount),
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_DeleteItem Called. QuestID(%d), ItemType(%d), Amount(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     static_cast<int>(m_itemClassID),
        //     static_cast<int>(m_amount),
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: added item class %d change %lld to pending manipulations
        //     (configured removal %lld, enchant 0).", static_cast<int>(m_questID), WideToUtf8(user.GetName()).c_str(),
        //     static_cast<unsigned int>(user.GetDBID()), npcId, static_cast<int>(m_itemClassID), static_cast<long
        //     long>(runtime::NegateInt64Wrapping(m_amount)), static_cast<long long>(m_amount))
        L2_LOG_INFO(
            Log,
            "Quest %d for character '%s' (DBID %u), NPC %d: added item class %d change %lld to pending manipulations "
            "(configured removal %lld; enchant 0).",
            static_cast<int>(m_questID), Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
            npcId, static_cast<int>(m_itemClassID), static_cast<long long>(runtime::NegateInt64Wrapping(m_amount)),
            static_cast<long long>(m_amount)
        );
    }
    return true;
}

bool QuestOperation_DeleteItemAll::Do(User& user, AtomicJobList*, ItemManipList& itemManipulations, const int npcId) const {
    // Current L2Server.exe 0x00a071fc uses enchant only for the inventory
    // query and emits a zero-enchant removal manipulation.
    L2_GUARD_FUNCTION();
    const std::int64_t amount = user.m_inv.GetItemAmount(static_cast<int>(m_itemClassID), static_cast<int>(m_enchant), true);
    itemManipulations.Push(
        ItemManip(0, static_cast<int>(m_itemClassID), runtime::NegateInt64Wrapping(amount), 0, 0, 0, true)
    );
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_DeleteItemAll Called. QuestID(%d), ItemType(%d), Amount(%d), NPC(%d),
        //     User(%s)", static_cast<int>(m_questID), static_cast<int>(m_itemClassID), static_cast<int>(amount), npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_DeleteItemAll Called. QuestID(%d), ItemType(%d), Amount(%d), NPC(%d),
        //     User(%s)", static_cast<int>(m_questID), static_cast<int>(m_itemClassID), static_cast<int>(amount), npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: inventory query found %lld of item class %d with enchant %d.
        //     added change %lld to pending manipulations with enchant 0.", static_cast<int>(m_questID),
        //     WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId, static_cast<long
        //     long>(amount), static_cast<int>(m_itemClassID), static_cast<int>(m_enchant), static_cast<long
        //     long>(runtime::NegateInt64Wrapping(amount)))
        L2_LOG_INFO(
            Log,
            "Quest %d for character '%s' (DBID %u), NPC %d: inventory query found %lld of item class %d with enchant %d; "
            "added change %lld to pending manipulations with enchant 0.",
            static_cast<int>(m_questID), Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
            npcId, static_cast<long long>(amount), static_cast<int>(m_itemClassID), static_cast<int>(m_enchant),
            static_cast<long long>(runtime::NegateInt64Wrapping(amount))
        );
    }
    return true;
}

bool QuestOperation_GiveExp::Do(User& user, AtomicJobList* const jobs, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a0b3b0.
    L2_GUARD_FUNCTION();
    jobs->AddAtomicJob(new AtomicParamChange(0, user.Id(), PARAM_EXP, m_exp));
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_GiveExp Called. Exp(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_exp),
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_GiveExp Called. Exp(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_exp),
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest action for character '%s' (DBID %u), NPC %d: added EXP change %lld to the pending atomic jobs.",
        //     WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId, static_cast<long
        //     long>(m_exp))
        L2_LOG_INFO(
            Log, "Quest action for character '%s' (DBID %u), NPC %d: added EXP change %lld to the pending atomic jobs.",
            Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId,
            static_cast<long long>(m_exp)
        );
    }
    return true;
}

bool QuestOperation_GiveSP::Do(User& user, AtomicJobList* const jobs, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a0b4e8 keeps the Exp label.
    // The operator-log overlay below identifies the actual SP parameter.
    L2_GUARD_FUNCTION();
    jobs->AddAtomicJob(new AtomicParamChange(0, user.Id(), PARAM_SP, m_sp));
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_GiveSP Called. Exp(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_sp),
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_GiveSP Called. Exp(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_sp),
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest action for character '%s' (DBID %u), NPC %d: added SP change %lld to the pending atomic jobs.",
        //     WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId, static_cast<long
        //     long>(m_sp))
        L2_LOG_INFO(
            Log, "Quest action for character '%s' (DBID %u), NPC %d: added SP change %lld to the pending atomic jobs.",
            Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId,
            static_cast<long long>(m_sp)
        );
    }
    return true;
}

bool QuestOperation_ShowQuestMark::Do(User& user, AtomicJobList*, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a06f70 returns false.
    L2_GUARD_FUNCTION();
    user.GetSocket()->Send("chdd", 0xfe, 0x21, m_questID, m_journalID);
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_ShowQuestMark Called. QuestID(%d), NPC(%d), User(%s)",
        //     m_questID,
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_ShowQuestMark Called. QuestID(%d), NPC(%d), User(%s)",
        //     m_questID,
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: submitted journal marker %d to the client transport. This
        //     operation returns false.", m_questID, WideToUtf8(user.GetName()).c_str(), static_cast<unsigned
        //     int>(user.GetDBID()), npcId, m_journalID)
        L2_LOG_INFO(
            Log,
            "Quest %d for character '%s' (DBID %u), NPC %d: submitted journal marker %d to the client transport; this "
            "operation returns false.",
            m_questID, Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId, m_journalID
        );
    }
    return false;
}

bool QuestOperation_PlaySoundEffect::Do(User& user, AtomicJobList*, ItemManipList&, int) const {
    // Current L2Server.exe 0x00a07b5c returns false.
    L2_GUARD_FUNCTION();
    const std::wstring soundName = Utf16ToWide(m_wstrName);
    user.PlaySound(soundName.c_str(), 0, 0, 0, 0, 0);
    return false;
}

bool QuestOperation_IncreaseNPCLog::Do(User& user, AtomicJobList*, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a0736c.
    L2_GUARD_FUNCTION();
    user.IncreaseQuestNPCLog(static_cast<int>(m_questID), USER_NPC_LOG_QUEST_HUNT, m_npcClassID, m_count);
    user.SaveQuestNPCLog(static_cast<int>(m_questID));
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_IncreaseNPCLog Called. QuestID(%d), NpcClassId(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     m_npcClassID,
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_IncreaseNPCLog Called. QuestID(%d), NpcClassId(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     m_npcClassID,
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: requested NPC class %d hunt-count increase with maximum %d and
        //     requested persistence of the quest NPC log.", static_cast<int>(m_questID), WideToUtf8(user.GetName()).c_str(),
        //     static_cast<unsigned int>(user.GetDBID()), npcId, m_npcClassID, m_count)
        L2_LOG_INFO(
            Log,
            "Quest %d for character '%s' (DBID %u), NPC %d: requested NPC class %d hunt-count increase with maximum %d and "
            "requested persistence of the quest NPC log.",
            static_cast<int>(m_questID), Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
            npcId, m_npcClassID, m_count
        );
    }
    return true;
}

bool QuestOperation_ShowQuestHTML::Do(User& user, AtomicJobList*, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a088d8.
    L2_GUARD_FUNCTION();
    const std::u16string fileName = SanitizeUtf16(m_wstrName);
    std::u16string html;
    if (!g_HTMLCache.GetHTMLFile(m_wstrName.c_str(), html, user.m_data->m_CountryCode)) {
        return false;
    }
    const std::u16string hostHtml(html);
    user.ShowQuestHTML(fileName.c_str(), hostHtml.c_str(), m_questID);
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_ShowQuestHTML Called. QuestID(%d), HTML(%s), NPC(%d), User(%s)",
        //     m_questID,
        //     fileName.c_str(),
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_ShowQuestHTML Called. QuestID(%d), HTML(%s), NPC(%d), User(%s)",
        //     m_questID,
        //     fileName.c_str(),
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: loaded quest HTML '%s' and submitted it for display.",
        //     m_questID, WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId,
        //     WideToUtf8(fileName).c_str())
        L2_LOG_INFO(
            Log, "Quest %d for character '%s' (DBID %u), NPC %d: loaded quest HTML '%s' and submitted it for display.",
            m_questID, Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId,
            Utf16ToUtf8(fileName).c_str()
        );
    }
    return true;
}

bool QuestOperation_ChangeClass::Do(User& user, AtomicJobList* const jobs, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a0b620.
    L2_GUARD_FUNCTION();
    jobs->AddAtomicJob(new AtomicChangeClass(user.Id(), m_nClassID, npcId, m_nTargetMaxLevel));
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_ChangeClass Called. QuestID(%d), Class(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     m_nClassID,
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_ChangeClass Called. QuestID(%d), Class(%d), NPC(%d), User(%s)",
        //     static_cast<int>(m_questID),
        //     m_nClassID,
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: added change to class %d with target maximum level %d to the
        //     pending atomic jobs.", static_cast<int>(m_questID), WideToUtf8(user.GetName()).c_str(), static_cast<unsigned
        //     int>(user.GetDBID()), npcId, m_nClassID, m_nTargetMaxLevel)
        L2_LOG_INFO(
            Log,
            "Quest %d for character '%s' (DBID %u), NPC %d: added change to class %d with target maximum level %d to the "
            "pending atomic jobs.",
            static_cast<int>(m_questID), Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
            npcId, m_nClassID, m_nTargetMaxLevel
        );
    }
    return true;
}

bool QuestOperation_AddPCSocial::Do(User& user, AtomicJobList*, ItemManipList&, int) const {
    // Current L2Server.exe 0x00a07478.
    L2_GUARD_FUNCTION();
    user.SocialAction(m_nSocialNumber, 0x800, false);
    return true;
}

bool QuestOperation_ShowOnScreenMsg::Do(User& user, AtomicJobList*, ItemManipList&, int) const {
    // Current L2Server.exe 0x00a07ce8.
    L2_GUARD_FUNCTION();
    user.GetSocket()->Send(
        "chdddddddddddSSSSS", 0xfe, 0x39, 1, m_nMsgNum, m_nWindowType, m_nFontSize, m_nFontType, m_nMsgColor, m_nShadowType,
        m_nBackGroundType, m_nLifeTime, m_nAnimationType, m_nFstrIndex, m_wstr1.c_str(), m_wstr2.c_str(), m_wstr3.c_str(),
        m_wstr4.c_str(), m_wstr5.c_str()
    );
    return true;
}

bool QuestOperation_ShowSystemMsg::Do(User& user, AtomicJobList*, ItemManipList&, int) const {
    // Current L2Server.exe 0x00a07530.
    L2_GUARD_FUNCTION();
    user.GetSocket()->SendSystemMessage(m_nSystemMsgNo);
    return true;
}

bool QuestOperation_AddSkillDesire::Do(User& user, AtomicJobList*, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a075e4.
    L2_GUARD_FUNCTION();
    const int targetId = m_nTargetType == 0 ? npcId : static_cast<int>(user.Id());
    g_npcserver.Send("cddQddd", 0xb9, npcId, targetId, m_skillNameID, m_nSkillLevel, m_nDesire, m_nUnknown);
    return true;
}

bool QuestOperation_RenewSubjob::Do(User& user, AtomicJobList*, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a076e4.
    L2_GUARD_FUNCTION();
    const int memoState1 = user.m_data->GetMemoState(m_nQuestID, 1);
    const int memoState2 = user.m_data->GetMemoState(m_nQuestID, 2);
    user.OnNpcRenewSubJobPacket(npcId, memoState1, memoState2, m_nType);
    return true;
}

bool QuestOperation_SetOnetimeQuestFlag::Do(User& user, AtomicJobList* const jobs, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a0b7a0.
    L2_GUARD_FUNCTION();
    jobs->AddAtomicJob(new MemoControl(user.Id(), MemoControl::SET_ONETIME_QUEST_FLAG, m_nQuestID, m_nState, 0));
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_SetOnetimeQuestFlag Called. QuestID(%d), State(%d), NPC(%d), User(%s)",
        //     m_nQuestID,
        //     m_nState,
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_SetOnetimeQuestFlag Called. QuestID(%d), State(%d), NPC(%d), User(%s)",
        //     m_nQuestID,
        //     m_nState,
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: added one-time quest flag value %d to the pending atomic
        //     jobs.", m_nQuestID, WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId,
        //     m_nState)
        L2_LOG_INFO(
            Log,
            "Quest %d for character '%s' (DBID %u), NPC %d: added one-time quest flag value %d to the pending atomic jobs.",
            m_nQuestID, Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId, m_nState
        );
    }
    return true;
}

bool QuestOperation_SetDailyQuestFlag::Do(User& user, AtomicJobList* const jobs, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a0b924.
    L2_GUARD_FUNCTION();
    jobs->AddAtomicJob(new AtomicChangeDailyQuestFlag(user.Id(), m_nQuestID));
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_SetDailyQuestFlag Called. QuestID(%d), NPC(%d), User(%s)",
        //     m_nQuestID,
        //     npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_SetDailyQuestFlag Called. QuestID(%d), NPC(%d), User(%s)",
        //     m_nQuestID,
        //     npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: added a daily quest flag update to the pending atomic jobs.",
        //     m_nQuestID, WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId)
        L2_LOG_INFO(
            Log,
            "Quest %d for character '%s' (DBID %u), NPC %d: added a daily quest flag update to the pending atomic jobs.",
            m_nQuestID, Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId
        );
    }
    return true;
}

bool QuestOperation_ShowMultiSell::Do(User& user, AtomicJobList*, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a077d0 ignores ShowMultiSell's result.
    L2_GUARD_FUNCTION();
    user.ShowMultiSell(npcId, m_nMultiSellGroupID);
    return true;
}

bool QuestOperation_GiveItemRandom::Do(
    User& user, AtomicJobList* const jobs, ItemManipList& itemManipulations, const int npcId
) const {
    // Current L2Server.exe 0x00a07e84.
    L2_GUARD_FUNCTION();
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_GiveItemRandom Called. QuestID(%d), m_nHigh(%d), m_nProb(%d), NPC(%d),
        //     User(%s)", static_cast<int>(m_questID), static_cast<int>(m_nHigh), static_cast<int>(m_nProb), npcId,
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_GiveItemRandom Called. QuestID(%d), m_nHigh(%d), m_nProb(%d), NPC(%d),
        //     User(%s)", static_cast<int>(m_questID), static_cast<int>(m_nHigh), static_cast<int>(m_nProb), npcId,
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: preparing random item selection with configured upper bound
        //     %lld (effective 32-bit bound %d) and threshold %lld.", static_cast<int>(m_questID),
        //     WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId, static_cast<long
        //     long>(m_nHigh), static_cast<int>(m_nHigh), static_cast<long long>(m_nProb))
        L2_LOG_INFO(
            Log,
            "Quest %d for character '%s' (DBID %u), NPC %d: preparing random item selection with configured upper bound "
            "%lld (effective 32-bit bound %d) and threshold %lld.",
            static_cast<int>(m_questID), Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
            npcId, static_cast<long long>(m_nHigh), static_cast<int>(m_nHigh), static_cast<long long>(m_nProb)
        );
    }

    int roll = static_cast<int>(m_nHigh);
    if (roll < 1) {
        // Log.Add(LOG_ERROR, L"nHigh [%d] in Random", roll)
        // L2_LOG_ERROR(Log, L"nHigh [%d] in Random", roll)
        // L2_LOG_WARN(Log,
        //     "Quest %d for character '%s' (DBID %u), NPC %d: random item selection has effective upper bound %d below 1
        //     (configured %lld), using fixed roll 1 with threshold %lld.", static_cast<int>(m_questID),
        //     WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId, roll, static_cast<long
        //     long>(m_nHigh), static_cast<long long>(m_nProb))
        L2_LOG_WARN(
            Log,
            "Quest %d for character '%s' (DBID %u), NPC %d: random item selection has effective upper bound %d below 1 "
            "(configured %lld); using fixed roll 1 with threshold %lld.",
            static_cast<int>(m_questID), Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
            npcId, roll, static_cast<long long>(m_nHigh), static_cast<long long>(m_nProb)
        );
        roll = 1;
    } else {
        roll = runtime::Win64CvttDoubleToInt32(genrand64_real2() * static_cast<double>(roll) + 1.0);
    }
    const QuestOperation_GiveItem& selected = m_nProb < static_cast<std::int64_t>(roll) ? m_opGiveItem2 : m_opGiveItem1;
    return selected.Do(user, jobs, itemManipulations, npcId);
}

bool QuestOperation_InstantTeleport::Do(User& user, AtomicJobList* const jobs, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a0ba90 truncates the stored yaw to uint16.
    L2_GUARD_FUNCTION();
    const int direction = static_cast<std::uint16_t>(user.m_data->GetDirByInt());
    jobs->AddAtomicJob(new AtomicTeleportTo(static_cast<int>(user.Id()), m_x, m_y, m_z, direction, user.GetInZoneID()));
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_InstantTeleport Called. X(%d), Y(%d), Z(%d), Direction(%d), NPC(%d),
        //     User(%s)", m_x, m_y, m_z, direction, npcId, user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_InstantTeleport Called. X(%d), Y(%d), Z(%d), Direction(%d), NPC(%d),
        //     User(%s)", m_x, m_y, m_z, direction, npcId, user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest action for character '%s' (DBID %u), NPC %d: added teleport to (%d, %d, %d), direction %d, to the
        //     pending atomic jobs.", WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId,
        //     m_x, m_y, m_z, direction)
        L2_LOG_INFO(
            Log,
            "Quest action for character '%s' (DBID %u), NPC %d: added teleport to (%d, %d, %d), direction %d, to the "
            "pending atomic jobs.",
            Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId, m_x, m_y, m_z, direction
        );
    }
    return true;
}

bool QuestOperation_InstantTeleportEx::Do(User& user, AtomicJobList* const jobs, ItemManipList&, const int npcId) const {
    // Current L2Server.exe 0x00a0bc54.
    L2_GUARD_FUNCTION();
    jobs->AddAtomicJob(new AtomicTeleportTo(static_cast<int>(user.Id()), m_x, m_y, m_z, m_dir, user.GetInZoneID()));
    if (m_bLogToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] QuestOperation_InstantTeleportEx Called. X(%d), Y(%d), Z(%d), Direction(%d), NPC(%d),
        //     User(%s)", m_x, m_y, m_z, m_dir, npcId, user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] QuestOperation_InstantTeleportEx Called. X(%d), Y(%d), Z(%d), Direction(%d), NPC(%d),
        //     User(%s)", m_x, m_y, m_z, m_dir, npcId, user.GetName())
        // L2_LOG_INFO(Log,
        //     "Quest action for character '%s' (DBID %u), NPC %d: added teleport to (%d, %d, %d), explicit direction %d, to
        //     the pending atomic jobs.", WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
        //     npcId, m_x, m_y, m_z, m_dir)
        L2_LOG_INFO(
            Log,
            "Quest action for character '%s' (DBID %u), NPC %d: added teleport to (%d, %d, %d), explicit direction %d, to "
            "the pending atomic jobs.",
            Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), npcId, m_x, m_y, m_z, m_dir
        );
    }
    return true;
}

QuestPage::~QuestPage() = default;

QuestPage_FHTML::~QuestPage_FHTML() = default;

void QuestPage_HTML::Send(User& user, CQuestBase&) {
    L2_GUARD_FUNCTION();

    const std::u16string fileName(m_QuestPageName);
    std::u16string html;
    if (g_HTMLCache.GetHTMLFile(m_QuestPageName.c_str(), html, user.m_data->m_CountryCode)) {
        const std::u16string hostHtml(html);
        user.ShowHTML(fileName.c_str(), hostHtml.c_str(), 0, HTML::LAUNCH, true);
    }
}

void QuestPage_QuestHTML::Send(User& user, CQuestBase& quest) {
    L2_GUARD_FUNCTION();

    const std::u16string fileName(m_QuestPageName);
    std::u16string html;
    if (g_HTMLCache.GetHTMLFile(m_QuestPageName.c_str(), html, user.m_data->m_CountryCode)) {
        const std::u16string hostHtml(html);
        user.ShowQuestHTML(fileName.c_str(), hostHtml.c_str(), quest.GetId());
    }
}

void QuestPage_FHTML::Send(User& user, CQuestBase&) {
    L2_GUARD_FUNCTION();

    const std::u16string fileName(m_QuestPageName);
    const std::u16string html = CFHTML::MakeHTMLFromStrMap(m_QuestPageName.c_str(), m_Param, user.m_data->m_CountryCode);
    const std::u16string hostHtml(html);
    user.ShowHTML(fileName.c_str(), hostHtml.c_str(), 0, HTML::LAUNCH, true);
}

void QuestPage_QuestFHTML::Send(User& user, CQuestBase&) {
    L2_GUARD_FUNCTION();

    const std::u16string fileName(m_QuestPageName);
    const std::u16string html = CFHTML::MakeHTMLFromStrMap(m_QuestPageName.c_str(), m_Param, user.m_data->m_CountryCode);
    const std::u16string hostHtml(html);
    user.ShowQuestHTML(fileName.c_str(), hostHtml.c_str(), 0);
}

void QuestFeedback_HTML::Send(User& user, CQuestBase& quest) {
    if (m_pQuestPage != nullptr) {
        m_pQuestPage->Send(user, quest);
    }
}

void QuestFeedback_SystemMsg::Send(User& user, CQuestBase&) {
    L2_GUARD_FUNCTION();
    user.SendSystemMessage(static_cast<unsigned int>(m_SystemMsgId));
}

bool QuestCondition_Level::IsValidCondition(User& user, CQuestBase&) {
    L2_GUARD_FUNCTION();
    return m_MinLevel <= user.Level() && user.Level() <= m_MaxLevel;
}

bool QuestCondition_Class::IsValidCondition(User& user, CQuestBase&) {
    L2_GUARD_FUNCTION();
    return m_ClassList.find(static_cast<int>(user.m_data->m_nClass)) != m_ClassList.end();
}

bool QuestCondition_Party::IsValidCondition(User& user, CQuestBase&) {
    L2_GUARD_FUNCTION();
    return user.m_data->m_nPartyId != 0;
}

bool QuestCondition_Pledge::IsValidCondition(User& user, CQuestBase&) {
    L2_GUARD_FUNCTION();

    int residenceType = 0;
    if (CPledge* const pledge = user.GetPledge(); pledge != nullptr) {
        if (CCastle* const castle = pledge->GetCastle(); castle != nullptr && castle->m_data != nullptr) {
            residenceType = castle->m_data->m_ResidenceType;
        } else {
            residenceType = 3;
        }
    }
    return m_PledgeTypeList.find(residenceType) != m_PledgeTypeList.end();
}

QuestCondition_DailyQuest::QuestCondition_DailyQuest() :
    QuestCondition(CQuestSystem::inst().m_DefaultSetting.m_DefaultFeedback[4]) {
    L2_GUARD_FUNCTION();
}

bool QuestCondition_DailyQuest::IsValidCondition(User& user, CQuestBase& quest) {
    L2_GUARD_FUNCTION();
    return user.m_data->GetDailyQuestFlag(quest.GetId());
}

void QuestOperation_ShowQuestPage::Do(User& user, CQuestBase& quest) {
    L2_GUARD_FUNCTION();
    if (m_pQuestPage != nullptr) {
        m_pQuestPage->Send(user, quest);
    }
}

QuestOperation::~QuestOperation() = default;

QuestOperation_Item::~QuestOperation_Item() = default;

QuestPacket::~QuestPacket() = default;

void QuestOperation_Item::Do(User& user, CQuestBase& quest) {
    L2_GUARD_FUNCTION();

    AtomicJobList* const job = MakeAtomicJobList(user, quest);
    const int requestCount = static_cast<int>(m_ItemList.size());

    if (requestCount < 1) {
        if (job->CanDo()) {
            job->Do();
        }
        job->Destroy();
        return;
    }

    ItemManip requests[100]{};
    for (int index = 0; index < requestCount; ++index) {
        const ItemDataType& source = m_ItemList[static_cast<std::size_t>(index)];
        int enchant = source.itemEnchant;
        if (enchant == -1) {
            CObject* const object = g_ObjectDB.GetObjectW(static_cast<std::int32_t>(source.itemClassId), false);
            CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
            if (item != nullptr && item->m_data != nullptr) {
                enchant = item->m_data->GetEnchanted();
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Item[%d] is not exist.",
                //     L"QuestBase.cpp",
                //     0x735,
                //     source.itemClassId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Item[%d] is not exist.",
                //     L"QuestBase.cpp",
                //     0x735,
                //     source.itemClassId)
                L2_LOG_ERROR(
                    Log,
                    "Quest %d, character DBID %u: no usable prototype for item class %u when resolving default enchant; the "
                    "request for %lld items retains enchant -1.",
                    quest.GetId(), static_cast<unsigned int>(user.GetDBID()), source.itemClassId,
                    static_cast<long long>(source.itemCount)
                );
            }
        }
        requests[index] =
            ItemManip(quest.GetId(), static_cast<int>(source.itemClassId), source.itemCount, enchant, 0, 0, true);
    }

    L2_GUARD(user.m_ManipulateQueueLock);
    CPacketHelperEx<20480> packet;
    User::etcMsg message = user.SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = user.SaveManipulateQueue(
        ManipulateType::MANI_QUEST, quest.GetId(), requestCount, requests, job, 0, packet, false, ItemGetReason::Etc, message
    );

    const User::ManiItemResultType result = user._SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        if (user._HandleItemManipulation(queue)) {
            LogOnSuccess(user, requests);
        } else {
            job->Destroy();
        }
        if (user.GetTrade() != nullptr) {
            user.TradeCancel(false);
        }
        return;
    }
    if (result == User::MRT_JOBMAP) {
        job->Destroy();
        user.SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return;
    }
    if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"QuestBase.cpp",
        //     0x769)
        // Noise: MRT_JOBNOTMAP means the item job was queued behind existing work. This is an expected asynchronous outcome,
        // not completed item delivery or an error. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"QuestBase.cpp",
        //     0x769)
        LogOnSuccess(user, requests);
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"QuestBase.cpp",
    //     0x76f)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"QuestBase.cpp",
    //     0x76f)
    L2_LOG_ERROR(
        Log,
        "Quest %d, character DBID %u: item queue returned unexpected result %d for %d entries; the job is destroyed and an "
        "active trade is canceled.",
        quest.GetId(), static_cast<unsigned int>(user.GetDBID()), static_cast<int>(result), requestCount
    );
    job->Destroy();
    if (user.GetTrade() != nullptr) {
        user.TradeCancel(false);
    }
}

void QuestCondition::SendFeedback(User& user, CQuestBase& quest) {
    L2_GUARD_FUNCTION();
    if (m_pFeedback != nullptr) {
        m_pFeedback->Send(user, quest);
    }
}

bool QuestCondition::IsValid(User& user, CQuestBase& quest) {
    L2_GUARD_FUNCTION();
    const bool valid = IsValidCondition(user, quest);
    if (!valid) {
        SendFeedback(user, quest);
    }
    return valid;
}

bool QuestCondition_FullInven::IsValidCondition(User& user, CQuestBase&) {
    L2_GUARD_FUNCTION();
    return user.m_inv.CheckAddableMulti(m_CheckAddableItem, false);
}

namespace ConditionChecker {

bool IsValid(CQuestBase& quest, const std::vector<QuestCondition*>& conditions, User& user) {
    L2_GUARD_FUNCTION();
    for (QuestCondition* const condition : conditions) {
        if (!condition->IsValid(user, quest)) {
            return false;
        }
    }
    return true;
}

} // namespace ConditionChecker

CQuestBase::CQuestBase(int id) :
    m_Id(id), m_pAcceptOperation_Item(nullptr), m_pFinishOperation_Item1(nullptr), m_pFinishOperation_Item2(nullptr) {
    L2_GUARD_FUNCTION();
}

CQuestBase::~CQuestBase() {
    L2_GUARD_FUNCTION();
    DeletePointers(m_StartOperationList);
    DeletePointers(m_AcceptOperationList);
    DeletePointers(m_FinishOperationList);
    DeletePointers(m_StartConditionList);
    DeletePointers(m_AcceptConditionList);
    DeletePointers(m_FinishConditionList);
    delete m_pAcceptOperation_Item;
    delete m_pFinishOperation_Item1;
    delete m_pFinishOperation_Item2;
}

void CQuestBase::SendPacket(User& user, QuestPacket& packet) { packet.Send(user); }

void CQuestBase::DoOperationList(User& user, OperationList& operations, CQuestBase& quest) {
    L2_GUARD_FUNCTION();
    for (QuestOperation* const operation : operations) {
        operation->Do(user, quest);
    }
}

bool CQuestBase::CanStartQuest(User& user) {
    L2_GUARD_FUNCTION();
    return ConditionChecker::IsValid(*this, m_StartConditionList, user) && CanStart(user);
}

bool CQuestBase::CanAcceptQuest(User& user) {
    L2_GUARD_FUNCTION();
    return ConditionChecker::IsValid(*this, m_AcceptConditionList, user) && CanAccept(user);
}

bool CQuestBase::CanFinishQuest(User& user) {
    L2_GUARD_FUNCTION();
    return ConditionChecker::IsValid(*this, m_FinishConditionList, user) && CanFinish(user);
}

void CQuestBase::DoStartOperation(User& user) {
    L2_GUARD_FUNCTION();
    DoOperationList(user, m_StartOperationList, *this);
}

void CQuestBase::DoAcceptOperation(User& user) {
    L2_GUARD_FUNCTION();
    DoOperationList(user, m_AcceptOperationList, *this);
    OnAcceptOperation(user);
}

void CQuestBase::DoFinishOperation(User& user) {
    L2_GUARD_FUNCTION();
    DoOperationList(user, m_FinishOperationList, *this);
    OnFinishOperation(user);
}

bool CQuestBase::TryStart(User& user) {
    L2_GUARD_FUNCTION();
    const bool valid = CanStartQuest(user);
    if (valid) {
        DoStartOperation(user);
    }
    return valid;
}

void CQuestBase::TryAccept(User& user) {
    L2_GUARD_FUNCTION();
    if (!CanAcceptQuest(user)) {
        return;
    }
    if (m_pAcceptOperation_Item == nullptr) {
        DoAcceptOperation(user);
        return;
    }
    m_pAcceptOperation_Item->Do(user, *this);
}

void CQuestBase::TryFinish(User& user) {
    L2_GUARD_FUNCTION();
    if (!CanFinishQuest(user)) {
        return;
    }
    if (m_pFinishOperation_Item1 == nullptr) {
        DoFinishOperation(user);
        return;
    }
    m_pFinishOperation_Item1->Do(user, *this);
}

int CQuestBase::GetAdjustedRewardRate() { return 0; }
