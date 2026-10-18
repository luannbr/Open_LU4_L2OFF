#include "Quest.h"
#include "Shared/src/TextRuntime.h"

#include "AtomicJobServerSide.h"
#include "CContributeData.h"
#include "Creature.h"
#include "Item.h"
#include "MultiPartyCommandChannel.h"
#include "NPC.h"
#include "ObjectDB.h"
#include "ParserQuestSystem.h"
#include "Party.h"
#include "QuestBase.h"
#include "QuestData.h"
#include "QuestManager.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "User.h"
#include "UserSocket.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/SharedFactory.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

std::int64_t AbsInt64Machine(const std::int64_t value) noexcept {
    const std::uint64_t bits = static_cast<std::uint64_t>(value);
    const std::uint64_t absolute = value < 0 ? 0U - bits : bits;
    std::int64_t result = 0;
    std::memcpy(&result, &absolute, sizeof(result));
    return result;
}

std::int64_t MultiplyInt64Machine(const std::int64_t left, const std::int64_t right) noexcept {
    const std::uint64_t bits = static_cast<std::uint64_t>(left) * static_cast<std::uint64_t>(right);
    std::int64_t result = 0;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}


void AddMemberFromParty(User& user, const bool logToError, std::vector<User*>& users) {
    // Current L2Server.exe 0x009fc7b0.
    L2_GUARD_FUNCTION();
    CParty* const party = user.GetParty();
    if (party == nullptr) {
        if (logToError) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[UserQuestData] Cannot Find Party. User(%s)",
            //     user.GetName())
            // L2_LOG_ERROR(Log,
            //     L"[UserQuestData] Cannot Find Party. User(%s)",
            //     user.GetName())
            // L2_LOG_INFO(Log,
            //     "Quest target collection for character '%s' (DBID %u): no party is available. This lookup adds no
            //     members.", WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()))
            L2_LOG_INFO(
                Log,
                "Quest target collection for character '%s' (DBID %u): no party is available; this lookup adds no members.",
                Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID())
            );
        }
        return;
    }
    if (logToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] Collect Party Member. User(%s)",
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] Collect Party Member. User(%s)",
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Collecting party members as quest target candidates for character '%s' (DBID %u).",
        //     WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()))
        L2_LOG_INFO(
            Log, "Collecting party members as quest target candidates for character '%s' (DBID %u).",
            Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID())
        );
    }
    party->GetAllMember(users);
}

void AddMemberFromMPCC(User& user, const bool logToError, std::vector<User*>& users) {
    // Current L2Server.exe 0x009fc8dc.
    L2_GUARD_FUNCTION();
    CMultiPartyCommandChannel* const channel = user.GetMPCC();
    if (channel == nullptr) {
        if (logToError) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[UserQuestData] Cannot Find MPCC. User(%s)",
            //     user.GetName())
            // L2_LOG_ERROR(Log,
            //     L"[UserQuestData] Cannot Find MPCC. User(%s)",
            //     user.GetName())
            // L2_LOG_INFO(Log,
            //     "Quest target collection for character '%s' (DBID %u): no command channel is available. This lookup adds
            //     no members.", WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()))
            L2_LOG_INFO(
                Log,
                "Quest target collection for character '%s' (DBID %u): no command channel is available; this lookup adds no "
                "members.",
                Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID())
            );
        }
        return;
    }
    if (logToError) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[UserQuestData] Collect All MPCC Member. User(%s)",
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[UserQuestData] Collect All MPCC Member. User(%s)",
        //     user.GetName())
        // L2_LOG_INFO(Log,
        //     "Collecting command-channel members as quest target candidates for character '%s' (DBID %u).",
        //     WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()))
        L2_LOG_INFO(
            Log, "Collecting command-channel members as quest target candidates for character '%s' (DBID %u).",
            Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID())
        );
    }
    channel->GetAllMember(users);
}

template <typename TList>
void DeleteQuestRuntimePointers(TList& values) {
    for (auto* const value : values) {
        delete value;
    }
    values.clear();
}

std::uint64_t QuestTargetRandRange(const std::uint64_t range) {
    // VS2015 _Rng_from_urng<__int64, _Rand_urng_from_func> used by the
    // current random_shuffle instantiation (current xutility 0x005a500c).
    constexpr std::uint64_t kRandMask = 0x7fff;
    std::uint64_t value = 0;
    std::uint64_t mask = 0;
    do {
        value = 0;
        mask = 0;
        if (range != 1) {
            do {
                const std::uint64_t lane = static_cast<unsigned int>(std::rand()) & kRandMask;
                value = (value << 15) | lane;
                mask = (mask << 15) | kRandMask;
            } while (mask < range - 1);
        }
    } while (mask / range <= value / range && mask % range != range - 1);
    return value % range;
}

void QuestTargetRandomShuffle(std::vector<User*>& users) {
    // VS2015 random_shuffle walks forward and swaps slot i with [0, i].
    for (std::size_t index = 1; index < users.size(); ++index) {
        const std::size_t selected = static_cast<std::size_t>(QuestTargetRandRange(index + 1));
        std::swap(runtime::CheckedVectorAt(users, index), runtime::CheckedVectorAt(users, selected));
    }
}

void FilterMemberByRandom(const int limit, std::vector<User*>& users) {
    // Current L2Server.exe 0x00a030ac.
    L2_GUARD_FUNCTION();
    if (static_cast<std::size_t>(limit) <= users.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] Contributors are too many: %d, %d",
        //     limit,
        //     static_cast<int>(users.size()))
        // Noise: Reaching the contributor limit, including exact equality, is an expected bounded-selection path. Preserve
        // the original shuffle and resize without logging each selection as an error. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] Contributors are too many: %d, %d",
        //     limit,
        //     static_cast<int>(users.size()))
        QuestTargetRandomShuffle(users);
        users.resize(static_cast<std::size_t>(limit));
    }
}

void AddMemberFromContributeType(const std::map<std::uint32_t, double>& contributions, std::vector<User*>& users) {
    // Current L2Server.exe 0x00a03198.
    L2_GUARD_FUNCTION();
    for (const auto& contribution : contributions) {
        CCreature* const object = g_CreatureSmartIdManager.GetObjectW(contribution.first);
        User* const contributor = object != nullptr && object->IsPC() ? object->CastUser() : nullptr;
        if (contributor != nullptr) {
            users.push_back(contributor);
        }
    }
}

void FilterMemberByCondition(
    const QuestNpcEvent::ConditionList& conditions,
    const std::vector<User*>& candidates,
    const bool logToError,
    const char16_t* const sourceUserName,
    std::vector<User*>& users
) {
    // Current L2Server.exe 0x00a032d0.
    L2_GUARD_FUNCTION();
    // Own the source name while candidate conditions and diagnostics run.
    const std::string loggedSourceName =
        logToError ? (sourceUserName == nullptr ? "<null>" : Utf16ToUtf8(sourceUserName)) : std::string();
    for (User* const candidate : candidates) {
        bool satisfied = true;
        for (ICondition* const condition : conditions) {
            if (!condition->Check(*candidate)) {
                satisfied = false;
                break;
            }
        }
        if (logToError) {
            // Log.Add(
            //     LOG_ERROR,
            //     satisfied
            //         ? L"[UserQuestData] TargetUser(%s) satisfied! SourceUser(%s)"
            //         : L"[UserQuestData] TargetUser(%s) can't satisfied! SourceUser(%s)",
            //     candidate->GetName(),
            //     sourceUserName)
            // L2_LOG_ERROR(Log,
            //     satisfied
            //         ? L"[UserQuestData] TargetUser(%s) satisfied! SourceUser(%s)"
            //         : L"[UserQuestData] TargetUser(%s) can't satisfied! SourceUser(%s)",
            //     candidate->GetName(),
            //     sourceUserName)
            // L2_LOG_INFO(Log,
            //     "Quest target character '%s' (DBID %u) for source '%s': conditions %s. Candidate %s.",
            //     WideToUtf8(candidate->GetName()).c_str(), static_cast<unsigned int>(candidate->GetDBID()),
            //     loggedSourceName.c_str(), satisfied ? "passed" : "failed", satisfied ? "included" : "excluded")
            L2_LOG_INFO(
                Log, "Quest target character '%s' (DBID %u) for source '%s': conditions %s; candidate %s.",
                Utf16ToUtf8(candidate->GetName()).c_str(), static_cast<unsigned int>(candidate->GetDBID()),
                loggedSourceName.c_str(), satisfied ? "passed" : "failed", satisfied ? "included" : "excluded"
            );
        }
        if (satisfied) {
            users.push_back(candidate);
        }
    }
}

std::size_t QuestTargetRandomIndex(const std::size_t count) {
    return (static_cast<unsigned int>(std::rand()) & 0x7fffU) % count;
}

} // namespace

std::int32_t QuestNpcEvent::_CONTRIBUTOR_LIMIT = 0x20;

QuestCondition::~QuestCondition() = default;

CQuestSystem::CQuestSystem() :
    m_DefaultSetting{} {
    // Current L2Server 0x0060ca1c.
}

CQuestSystem::~CQuestSystem() {
    // Current L2Server 0x00a02a58.
    L2_GUARD_FUNCTION();
    for (auto& entry : m_QuestIdMap) {
        delete entry.second;
        entry.second = nullptr;
    }
    m_QuestIdMap.clear();
    m_QuestStartItemMap.clear();
    m_mapQuestRestrict.clear();
}

void CQuestSystem::Load() {
    // Current L2Server.exe 0x00a04718.
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\questsystem.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/questsystem.txt";
#endif
    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from questsystem file failed",
        //     L"Quest.cpp",
        //     0x616)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from questsystem file failed",
        //     L"Quest.cpp",
        //     0x616)
        L2_LOG_ERROR(
            Log, "Cannot read quest-system script '%s'; this load stops and existing definitions are retained.",
            WideToUtf8(kFileName).c_str()
        );
        return;
    }

    CParserForQuestSystem parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] QuestSystemParser.create failed",
        //     L"Quest.cpp",
        //     0x61f)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] QuestSystemParser.create failed",
        //     L"Quest.cpp",
        //     0x61f)
        L2_LOG_ERROR(
            Log, "Cannot create the questsystem.txt parser; this load stops and existing definitions are retained."
        );
        return;
    }
    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing questsystem file failed after line[%d]",
        //     L"Quest.cpp",
        //     0x62c,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing questsystem file failed after line[%d]",
        //     L"Quest.cpp",
        //     0x62c,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse questsystem.txt at line %lld; definitions published before the error are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

void CQuestSystem::StartQuest(User& user, CItem& item) {
    // Current L2Server 0x009fe3bc.
    L2_GUARD_FUNCTION();
    const auto found = m_QuestStartItemMap.find(item.GetClassId());
    if (found != m_QuestStartItemMap.end() && found->second != nullptr) {
        found->second->TryStart(user);
    }
}

void CQuestSystem::OnStarted(User& user, const int questId) {
    // Current L2Server 0x009fe4c8.
    L2_GUARD_FUNCTION();
    const auto found = m_QuestIdMap.find(questId);
    if (found != m_QuestIdMap.end() && found->second != nullptr) {
        found->second->TryAccept(user);
    }
}

void CQuestSystem::LogQuestItem(User& user, const int count, ItemManip items[]) {
    // Current L2Server.exe 0x009fc320.
    L2_GUARD_FUNCTION();
    CLogFormatter formatter;
    for (int index = 0; index < count; ++index) {
        const ItemManip& manipulation = items[index];
        std::int64_t price = 0;
        CObject* const object = g_ObjectDB.GetObjectW(manipulation.mClassId, false);
        CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
        if (item != nullptr && item->m_const != nullptr) {
            price = item->m_const->m_nPrice;
        }

        const std::int64_t absoluteCount = AbsInt64Machine(manipulation.mCount);
        const int logId = manipulation.mCount < 0 ? 0x12e : 0x12d;
        formatter.WriteLog2(
            logId, &user, nullptr, manipulation.mClassId, absoluteCount, manipulation.mEnchanted,
            MultiplyInt64Machine(price, absoluteCount), 0, 0, 0
        );

        if (manipulation.mCount > 0 && manipulation.mClassId == 0x39) {
            user.m_UserStatData.AddUserStat2(UserStat::US_ADENA, manipulation.mCount);
        }
        if (manipulation.mCount > 0 && manipulation.mClassId == 0x15c7) {
            const std::int64_t total = user.m_inv.GetItemAmount(0x15c7, 0, false) + manipulation.mCount;
            formatter.WriteLog2(0x13e, &user, nullptr, user.GetSSQPart(), 0, 0, 0, manipulation.mCount, total, 0);
        }
    }
}

QuestCondition_FullQuest::QuestCondition_FullQuest() :
    QuestCondition(CQuestSystem::inst().GetSetting().m_DefaultFeedback[0]) {
    L2_GUARD_FUNCTION();
}

bool QuestCondition_FullQuest::IsValidCondition(User& user, CQuestBase&) {
    L2_GUARD_FUNCTION();
    return !user.IsFullMemo();
}

QuestCondition_DuplicatedQuest::QuestCondition_DuplicatedQuest() :
    QuestCondition(CQuestSystem::inst().GetSetting().m_DefaultFeedback[2]) {
    L2_GUARD_FUNCTION();
}

bool QuestCondition_DuplicatedQuest::IsValidCondition(User& user, CQuestBase& quest) {
    L2_GUARD_FUNCTION();
    return !user.HaveMemo(quest.GetId());
}

QuestCondition_OnetimeQuest::QuestCondition_OnetimeQuest() :
    QuestCondition(CQuestSystem::inst().GetSetting().m_DefaultFeedback[3]) {
    L2_GUARD_FUNCTION();
}

bool QuestCondition_OnetimeQuest::IsValidCondition(User& user, CQuestBase& quest) {
    L2_GUARD_FUNCTION();
    return !user.m_data->GetOneTimeFlag(quest.GetOnetimeQuestId());
}

bool QuestCondition_Item::IsValidCondition(User& user, CQuestBase&) {
    L2_GUARD_FUNCTION();
    for (ItemAmountData* const item : m_NeedItem.dataList) {
        if (item != nullptr && !user.m_inv.HaveItemByClassId(item->m_nItemClassId, item->m_nItemAmount, 0, 0)) {
            return false;
        }
    }
    return true;
}

void QuestOperation_Accept::Do(User& user, CQuestBase& quest) {
    // Current L2Server.exe 0x00a02edc.
    L2_GUARD_FUNCTION();
    UserQuest* const compiledQuest = QuestManager::inst().GetQuest(quest.GetId());
    if (compiledQuest == nullptr) {
        {
            L2_GUARD(*user.m_lock);
            user.m_data->SetMemo(quest.GetId());
            user.m_data->SetFlagJournal(quest.GetId(), 1);
        }
        user.SendQuestList(nullptr);
        user.GetSocket()->Send("chd", 0xfe, 0x21, quest.GetId());
        const std::wstring sound = target_utf16::ToHostCodeUnits(CQuestSystem::inst().GetSetting().m_SoundFile);
        user.PlaySound(sound.c_str(), 0, 0, 0, 0, 0);
    } else {
        user.AcceptQuest(quest.GetId());
        user.SendQuestList(nullptr);
    }

    CLogFormatter formatter;
    formatter.WriteLog2(0x12f, &user, nullptr, quest.GetId(), 0, 0, 0, 0, 0, 0);
}

AtomicJobList* QuestOperation_ItemOnAcceptStaticQuest::MakeAtomicJobList(User& user, CQuestBase& quest) {
    // Current L2Server.exe 0x00a03670.
    L2_GUARD_FUNCTION();
    auto* const jobs = new AtomicJobList(m_ItemList.empty() ? 2 : 3, user.Id());
    if (!m_ItemList.empty()) {
        jobs->AddAtomicJob(new CAtomicItemJobCheck(user.Id()));
    }
    jobs->AddAtomicJob(new CAtomicQuestAccept(user.Id(), &quest));
    jobs->AddAtomicJob(new CAtomicSaveQuestInfo(user.Id()));
    return jobs;
}

void QuestOperation_ItemOnAcceptStaticQuest::LogOnSuccess(User& user, ItemManip (&items)[100]) {
    // Current L2Server.exe 0x009fcb24.
    L2_GUARD_FUNCTION();
    CQuestSystem::LogQuestItem(user, static_cast<int>(m_ItemList.size()), items);
}

CQuest::CQuest(const int id, const int onetimeQuestId) : CQuestBase(id), m_OnetimeQuestId(onetimeQuestId) {
    // Current L2Server.exe 0x00a03838.
    L2_GUARD_FUNCTION();
    m_AcceptConditionList.push_back(new QuestCondition_FullQuest);
    m_AcceptConditionList.push_back(new QuestCondition_DuplicatedQuest);
    if (onetimeQuestId != -1) {
        m_AcceptConditionList.push_back(new QuestCondition_OnetimeQuest);
    }
    m_AcceptConditionList.push_back(new QuestCondition_DailyQuest);
    m_AcceptOperationList.push_back(new QuestOperation_Accept);
    m_pAcceptOperation_Item = new QuestOperation_ItemOnAcceptStaticQuest;
}

int CQuest::GetOnetimeQuestId() const { return m_OnetimeQuestId; }

int CQuest::GetAdjustedRewardRate() { return 100; }

bool CQuest::CanStart(User&) { return true; }

bool CQuest::CanAccept(User&) { return true; }

bool CQuest::CanFinish(User&) { return true; }

void CQuest::OnAcceptOperation(User&) {}

void CQuest::OnFinishOperation(User&) {}

QuestNpcEvent::QuestNpcEvent() noexcept :
    m_conditionTargetType(QCTT_NONE),
    m_executionType(QET_NONE),
    m_choice(0),
    m_eventRange(0),
    m_ask(0),
    m_reply(0),
    m_bLogToError(false) {}

QuestAction::~QuestAction() = default;

QuestJournal::~QuestJournal() = default;

UserQuest::~UserQuest() {
    // Current L2Server.exe 0x00a021e4 deletes only journal pointees. The
    // other pointer vectors release their carriers without deleting their
    // elements.
    DeleteQuestRuntimePointers(m_Journals);
}

ICondition* CreateCondition(QuestConditionData& data) {
    // Current L2Server.exe 0x009fce20.
    L2_GUARD_FUNCTION();
    switch (static_cast<QuestConditionType>(data.m_Type)) {
        case QCT_FALSE:
            return new UserQuestCondition_False;

        case QCT_ITEM: {
            QuestDataVariable* const itemName = data.getParam(1);
            QuestDataVariable* const compareValue = data.getParam(2);
            QuestDataVariable* const countValue = data.getParam(3);
            if (itemName == nullptr || compareValue == nullptr || countValue == nullptr) {
                return nullptr;
            }
            const int itemClassId = GetItemClassIDFromName(itemName->m_strVal.c_str());
            if (itemClassId == -1) {
                // const std::wstring hostName =
                //     target_utf16::ToHostCodeUnits(itemName->m_strVal.c_str())
                const std::string hostName = Utf16ToUtf8(itemName->m_strVal);
                // Log.Add(
                //     LOG_ERROR,
                //     L"Undefined Item[%s] while Creating UserQuestCondition_Item",
                //     hostName.c_str())
                // L2_LOG_ERROR(Log,
                //     L"Undefined Item[%s] while Creating UserQuestCondition_Item",
                //     hostName.c_str())
                L2_LOG_ERROR(
                    Log, "Quest %d: cannot create an item condition for unknown item '%s'; the condition is not created.",
                    data.m_QuestID, hostName.c_str()
                );
                return nullptr;
            }
            return new UserQuestCondition_Item(
                itemClassId, static_cast<int>(countValue->m_intVal),
                GetQuestCompareOperationType(static_cast<int>(compareValue->m_intVal))
            );
        }

        case QCT_LEVEL: {
            int from = 1;
            int up = 120;
            if (QuestDataVariable* const value = data.getParam(1)) {
                from = static_cast<int>(value->m_intVal);
            }
            if (QuestDataVariable* const value = data.getParam(2)) {
                up = static_cast<int>(value->m_intVal);
            }
            return new UserQuestCondition_LevelRange(from, up);
        }

        case QCT_CLASS: {
            QuestDataVariable* const from = data.getParam(1);
            QuestDataVariable* const up = data.getParam(2);
            return from != nullptr && up != nullptr
                ? static_cast<ICondition*>(new UserQuestCondition_Class(from->m_intVal, up->m_intVal))
                : nullptr;
        }

        case QCT_PARTY:
            return new UserQuestCondition_Party;

        case QCT_KILL_NPC: {
            QuestDataVariable* const classId = data.getParam(1);
            QuestDataVariable* const compareValue = data.getParam(2);
            QuestDataVariable* const count = data.getParam(3);
            return classId != nullptr && compareValue != nullptr && count != nullptr
                ? static_cast<ICondition*>(new UserQuestCondition_KillNpc(
                      static_cast<int>(classId->m_intVal), static_cast<int>(count->m_intVal), data.m_QuestID,
                      GetQuestCompareOperationType(static_cast<int>(compareValue->m_intVal))
                  ))
                : nullptr;
        }

        case QCT_ONETIME_QUEST: {
            QuestDataVariable* const questId = data.getParam(1);
            if (questId == nullptr) {
                return nullptr;
            }
            bool state = true;
            if (QuestDataVariable* const value = data.getParam(2)) {
                state = value->m_intVal != 0;
            }
            return new UserQuestCondition_OneTime(questId->m_intVal, state);
        }

        case QCT_DAILY_QUEST: {
            QuestDataVariable* const questId = data.getParam(1);
            if (questId == nullptr) {
                return nullptr;
            }
            bool state = true;
            if (QuestDataVariable* const value = data.getParam(2)) {
                state = value->m_intVal != 0;
            }
            return new UserQuestCondition_Daily(static_cast<int>(questId->m_intVal), state);
        }

        case QCT_MEMO: {
            QuestDataVariable* const questId = data.getParam(1);
            if (questId == nullptr) {
                return nullptr;
            }
            bool state = true;
            if (QuestDataVariable* const value = data.getParam(2)) {
                state = value->m_intVal != 0;
            }
            return new UserQuestCondition_HaveMemo(static_cast<int>(questId->m_intVal), state);
        }

        case QCT_MEMO_STATE: {
            QuestDataVariable* const questId = data.getParam(1);
            QuestDataVariable* const slotId = data.getParam(2);
            QuestDataVariable* const value = data.getParam(3);
            return questId != nullptr && slotId != nullptr && value != nullptr
                ? static_cast<ICondition*>(new UserQuestCondition_MemoState(
                      static_cast<int>(questId->m_intVal), static_cast<int>(slotId->m_intVal),
                      static_cast<int>(value->m_intVal)
                  ))
                : nullptr;
        }

        case QCT_CATEGORY: {
            QuestDataVariable* const compareValue = data.getParam(1);
            QuestDataVariable* const categoryId = data.getParam(2);
            if (compareValue == nullptr || categoryId == nullptr) {
                return nullptr;
            }
            const QuestCompareOperationType compare = GetQuestCompareOperationType(static_cast<int>(compareValue->m_intVal));
            if (compare != QCOT_NOT_EQUAL && compare != QCOT_EQUAL) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[UserQuestData] QuestCondition_Category took unsupported compare type (the compare type must be !=
                //     or ==). QuestId(%d)", data.m_QuestID)
                // L2_LOG_ERROR(Log,
                //     L"[UserQuestData] QuestCondition_Category took unsupported compare type (the compare type must be !=
                //     or ==). QuestId(%d)", data.m_QuestID)
                L2_LOG_ERROR(
                    Log,
                    "Quest %d: category condition has unsupported comparison code %d; only != and == are accepted, so the "
                    "condition is not created.",
                    data.m_QuestID, static_cast<int>(compare)
                );
                return nullptr;
            }
            return new UserQuestCondition_Category(compare, static_cast<int>(categoryId->m_intVal));
        }

        case QCT_RANDOM:
            if (data.m_Params.size() == 2) {
                return new UserQuestCondition_Random(
                    static_cast<int>(data.getParam(1)->m_intVal), static_cast<int>(data.getParam(2)->m_intVal)
                );
            }
            return nullptr;

        case QCT_ACQUIRED_ABILITY_POINT:
            if (data.m_Params.size() == 2) {
                return new UserQuestCondition_AcquiredAbilityPoint(
                    static_cast<int>(data.getParam(1)->m_intVal),
                    GetQuestCompareOperationType(static_cast<int>(data.getParam(2)->m_intVal))
                );
            }
            return nullptr;

        case QCT_MONSTERBOOK_FACTION_ACHIEVEMENT:
            if (data.m_Params.size() == 3) {
                const std::uint32_t factionBits = static_cast<std::uint32_t>(data.getParam(1)->m_intVal) - 1U;
                return new UserQuestCondition_MonsterBookFactionAchievement(
                    static_cast<std::int32_t>(factionBits), static_cast<int>(data.getParam(2)->m_intVal),
                    static_cast<int>(data.getParam(3)->m_intVal)
                );
            }
            return nullptr;

        case QCT_FIELD_CYCLE_STEP: {
            QuestDataVariable* const fieldCycleId = data.getParam(1);
            QuestDataVariable* const compareValue = data.getParam(2);
            QuestDataVariable* const value = data.getParam(3);
            return fieldCycleId != nullptr && compareValue != nullptr && value != nullptr
                ? static_cast<ICondition*>(new UserQuestCondition_FieldCycleStep(
                      static_cast<int>(fieldCycleId->m_intVal),
                      GetQuestCompareOperationType(static_cast<int>(compareValue->m_intVal)),
                      static_cast<int>(value->m_intVal)
                  ))
                : nullptr;
        }

        case QCT_INVALID:
        default:
            L2_LOG_ERROR(
                Log, "Quest %d: unsupported condition type %lld (%llu parameters); no condition is created.", data.m_QuestID,
                static_cast<long long>(data.m_Type), static_cast<unsigned long long>(data.m_Params.size())
            );
            return nullptr;
    }
}

IOperation* CreateOperation(QuestOperationData& data) {
    // Current L2Server.exe 0x009fedc8.
    L2_GUARD_FUNCTION();

    auto invalidOperation = [&data]() -> IOperation* {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CreateOperation() - Unknown operation type ( %I64d )",
        //     L"Quest.cpp",
        //     0x5ab,
        //     data.m_Type)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CreateOperation() - Unknown operation type ( %I64d )",
        //     L"Quest.cpp",
        //     0x5ab,
        //     data.m_Type)
        L2_LOG_ERROR(
            Log,
            "Quest %d: cannot create operation type %lld from %llu parameters; required parameters are absent or invalid.",
            data.m_QuestID, static_cast<long long>(data.m_Type), static_cast<unsigned long long>(data.m_Params.size())
        );
        return nullptr;
    };

    switch (static_cast<QuestOperationType>(data.m_Type)) {
        case QOT_COMPLETE_QUEST: {
            QuestDataVariable* const questId = data.getParam(1);
            return questId != nullptr ? static_cast<IOperation*>(new QuestOperation_CompleteQuest(questId->m_intVal))
                                      : invalidOperation();
        }

        case QOT_DELETE_QUEST: {
            QuestDataVariable* const questId = data.getParam(1);
            return questId != nullptr ? static_cast<IOperation*>(new QuestOperation_DeleteQuest(questId->m_intVal))
                                      : invalidOperation();
        }

        case QOT_CREATE_MEMO: {
            QuestDataVariable* const questId = data.getParam(1);
            return questId != nullptr ? static_cast<IOperation*>(new QuestOperation_CreateMemo(questId->m_intVal))
                                      : invalidOperation();
        }

        case QOT_DESTROY_MEMO: {
            QuestDataVariable* const questId = data.getParam(1);
            return questId != nullptr ? static_cast<IOperation*>(new QuestOperation_DestroyMemo(questId->m_intVal))
                                      : invalidOperation();
        }

        case QOT_SET_MEMO_FLAG: {
            QuestDataVariable* const questId = data.getParam(1);
            QuestDataVariable* const value = data.getParam(2);
            return questId != nullptr && value != nullptr
                ? static_cast<IOperation*>(new QuestOperation_SetMemoFlag(questId->m_intVal, value->m_intVal))
                : invalidOperation();
        }

        case QOT_SET_MEMO_STATE: {
            QuestDataVariable* const questId = data.getParam(1);
            QuestDataVariable* const slot = data.getParam(2);
            QuestDataVariable* const value = data.getParam(3);
            return questId != nullptr && slot != nullptr && value != nullptr
                ? static_cast<IOperation*>(
                      new QuestOperation_SetMemoState(questId->m_intVal, slot->m_intVal, value->m_intVal)
                  )
                : invalidOperation();
        }

        case QOT_START_JOURNAL: {
            QuestDataVariable* const questId = data.getParam(1);
            QuestDataVariable* const journal = data.getParam(2);
            return questId != nullptr && journal != nullptr
                ? static_cast<IOperation*>(new QuestOperation_StartJournal(questId->m_intVal, journal->m_intVal))
                : invalidOperation();
        }

        case QOT_COMPLETE_JOURNAL: {
            QuestDataVariable* const questId = data.getParam(1);
            QuestDataVariable* const journal = data.getParam(2);
            return questId != nullptr && journal != nullptr
                ? static_cast<IOperation*>(new QuestOperation_CompleteJournal(questId->m_intVal, journal->m_intVal))
                : invalidOperation();
        }

        case QOT_MONITOR_START_KILL_NPC: {
            QuestDataVariable* const npcClassId = data.getParam(1);
            return npcClassId != nullptr
                ? static_cast<IOperation*>(new QuestOperation_MonitorStart_KillNpc(npcClassId->m_intVal))
                : invalidOperation();
        }

        case QOT_MONITOR_END_KILL_NPC: {
            QuestDataVariable* const npcClassId = data.getParam(1);
            return npcClassId != nullptr
                ? static_cast<IOperation*>(new QuestOperation_MonitorEnd_KillNpc(npcClassId->m_intVal))
                : invalidOperation();
        }

        case QOT_SHOW_HTML: {
            QuestDataVariable* const fileName = data.getParam(1);
            QuestDataVariable* const questId = data.getParam(2);
            return fileName != nullptr && questId != nullptr
                ? static_cast<IOperation*>(
                      new QuestOperation_ShowHTML(fileName->m_strVal, static_cast<int>(questId->m_intVal))
                  )
                : invalidOperation();
        }

        case QOT_SHOW_HTML_IF: {
            QuestDataVariable* const condition = data.getParam(1);
            QuestDataVariable* const fileName = data.getParam(2);
            return condition != nullptr && fileName != nullptr
                ? static_cast<IOperation*>(new QuestOperation_ShowHTML_If(condition->m_intVal, fileName->m_strVal))
                : invalidOperation();
        }

        case QOT_GIVE_ITEM:
        case QOT_DELETE_ITEM: {
            QuestDataVariable* const questId = data.getParam(1);
            QuestDataVariable* const itemClassId = data.getParam(2);
            QuestDataVariable* const enchant = data.getParam(3);
            QuestDataVariable* const amount = data.getParam(4);
            if (questId == nullptr || itemClassId == nullptr || enchant == nullptr || amount == nullptr) {
                return invalidOperation();
            }
            if (static_cast<QuestOperationType>(data.m_Type) == QOT_GIVE_ITEM) {
                return new QuestOperation_GiveItem(
                    questId->m_intVal, itemClassId->m_intVal, enchant->m_intVal, amount->m_intVal
                );
            }
            return new QuestOperation_DeleteItem(
                questId->m_intVal, itemClassId->m_intVal, enchant->m_intVal, amount->m_intVal
            );
        }

        case QOT_DELETE_ITEM_ALL: {
            QuestDataVariable* const questId = data.getParam(1);
            QuestDataVariable* const itemClassId = data.getParam(2);
            QuestDataVariable* const enchant = data.getParam(3);
            return questId != nullptr && itemClassId != nullptr && enchant != nullptr
                ? static_cast<IOperation*>(
                      new QuestOperation_DeleteItemAll(questId->m_intVal, itemClassId->m_intVal, enchant->m_intVal)
                  )
                : invalidOperation();
        }

        case QOT_GIVE_EXP: {
            QuestDataVariable* const experience = data.getParam(1);
            return experience != nullptr ? static_cast<IOperation*>(new QuestOperation_GiveExp(experience->m_intVal))
                                         : invalidOperation();
        }

        case QOT_GIVE_SP: {
            QuestDataVariable* const skillPoint = data.getParam(1);
            return skillPoint != nullptr ? static_cast<IOperation*>(new QuestOperation_GiveSP(skillPoint->m_intVal))
                                         : invalidOperation();
        }

        case QOT_SHOW_QUEST_MARK: {
            QuestDataVariable* const questId = data.getParam(1);
            QuestDataVariable* const journalId = data.getParam(2);
            return questId != nullptr && journalId != nullptr
                ? static_cast<IOperation*>(new QuestOperation_ShowQuestMark(
                      static_cast<int>(questId->m_intVal), static_cast<int>(journalId->m_intVal)
                  ))
                : invalidOperation();
        }

        case QOT_PLAY_SOUND_EFFECT: {
            QuestDataVariable* const soundName = data.getParam(1);
            return soundName != nullptr ? static_cast<IOperation*>(new QuestOperation_PlaySoundEffect(soundName->m_strVal))
                                        : invalidOperation();
        }

        case QOT_INCREASE_NPC_LOG: {
            QuestDataVariable* const questId = data.getParam(1);
            QuestDataVariable* const npcClassId = data.getParam(2);
            QuestDataVariable* const count = data.getParam(3);
            return questId != nullptr && npcClassId != nullptr && count != nullptr
                ? static_cast<IOperation*>(new QuestOperation_IncreaseNPCLog(
                      questId->m_intVal, static_cast<int>(npcClassId->m_intVal), static_cast<int>(count->m_intVal)
                  ))
                : invalidOperation();
        }

        case QOT_SHOW_QUEST_HTML: {
            QuestDataVariable* const fileName = data.getParam(1);
            QuestDataVariable* const questId = data.getParam(2);
            return fileName != nullptr && questId != nullptr
                ? static_cast<IOperation*>(
                      new QuestOperation_ShowQuestHTML(fileName->m_strVal, static_cast<int>(questId->m_intVal))
                  )
                : invalidOperation();
        }

        case QOT_CHANGE_CLASS: {
            QuestDataVariable* const classId = data.getParam(1);
            QuestDataVariable* const targetMaxLevel = data.getParam(2);
            return classId != nullptr && targetMaxLevel != nullptr
                ? static_cast<IOperation*>(new QuestOperation_ChangeClass(
                      data.m_QuestID, static_cast<int>(classId->m_intVal), static_cast<int>(targetMaxLevel->m_intVal)
                  ))
                : invalidOperation();
        }

        case QOT_ADD_PC_SOCIAL: {
            QuestDataVariable* const socialNumber = data.getParam(1);
            return socialNumber != nullptr
                ? static_cast<IOperation*>(new QuestOperation_AddPCSocial(static_cast<int>(socialNumber->m_intVal)))
                : invalidOperation();
        }

        case QOT_SHOW_ON_SCREEN_MSG:
            if (data.m_Params.size() == 14) {
                return new QuestOperation_ShowOnScreenMsg(
                    -1, static_cast<int>(data.getParam(1)->m_intVal), static_cast<int>(data.getParam(2)->m_intVal),
                    static_cast<int>(data.getParam(3)->m_intVal), static_cast<int>(data.getParam(4)->m_intVal),
                    static_cast<int>(data.getParam(5)->m_intVal), static_cast<int>(data.getParam(6)->m_intVal),
                    static_cast<int>(data.getParam(7)->m_intVal), static_cast<int>(data.getParam(8)->m_intVal),
                    static_cast<int>(data.getParam(9)->m_intVal), data.getParam(10)->m_strVal, data.getParam(11)->m_strVal,
                    data.getParam(12)->m_strVal, data.getParam(13)->m_strVal, data.getParam(14)->m_strVal
                );
            }
            return invalidOperation();

        case QOT_SHOW_SYSTEM_MSG: {
            QuestDataVariable* const messageNumber = data.getParam(1);
            return messageNumber != nullptr
                ? static_cast<IOperation*>(new QuestOperation_ShowSystemMsg(static_cast<int>(messageNumber->m_intVal)))
                : invalidOperation();
        }

        case QOT_ADD_SKILL_DESIRE: {
            QuestDataVariable* const targetType = data.getParam(1);
            QuestDataVariable* const skillNameId = data.getParam(2);
            QuestDataVariable* const skillLevel = data.getParam(3);
            QuestDataVariable* const desire = data.getParam(4);
            QuestDataVariable* const unknown = data.getParam(5);
            return targetType != nullptr && skillNameId != nullptr && skillLevel != nullptr && desire != nullptr &&
                    unknown != nullptr
                ? static_cast<IOperation*>(new QuestOperation_AddSkillDesire(
                      static_cast<int>(targetType->m_intVal), skillNameId->m_intVal, static_cast<int>(skillLevel->m_intVal),
                      static_cast<int>(desire->m_intVal), static_cast<int>(unknown->m_intVal)
                  ))
                : invalidOperation();
        }

        case QOT_RENEW_SUBJOB:
            if (data.m_Params.size() == 1) {
                return new QuestOperation_RenewSubjob(data.m_QuestID, static_cast<int>(data.getParam(1)->m_intVal));
            }
            return invalidOperation();

        case QOT_SET_ONETIME_QUEST_FLAG:
            if (data.m_Params.size() == 2) {
                return new QuestOperation_SetOnetimeQuestFlag(
                    static_cast<int>(data.getParam(1)->m_intVal), static_cast<int>(data.getParam(2)->m_intVal)
                );
            }
            return invalidOperation();

        case QOT_SET_DAILY_QUEST_FLAG:
            if (data.m_Params.size() == 1) {
                return new QuestOperation_SetDailyQuestFlag(static_cast<int>(data.getParam(1)->m_intVal));
            }
            return invalidOperation();

        case QOT_SHOW_MULTI_SELL:
            if (data.m_Params.size() == 1) {
                return new QuestOperation_ShowMultiSell(static_cast<int>(data.getParam(1)->m_intVal));
            }
            return invalidOperation();

        case QOT_GIVE_ITEM_RANDOM: {
            QuestDataVariable* params[9]{};
            for (int index = 0; index < 9; ++index) {
                params[index] = data.getParam(index + 1);
                if (params[index] == nullptr) {
                    return invalidOperation();
                }
            }
            if (params[2]->m_intVal < params[1]->m_intVal) {
                return new QuestOperation_GiveItemRandom(
                    params[0]->m_intVal, params[1]->m_intVal, params[2]->m_intVal, params[3]->m_intVal, params[4]->m_intVal,
                    params[5]->m_intVal, params[6]->m_intVal, params[7]->m_intVal, params[8]->m_intVal
                );
            }
            // Log.Add(
            //     LOG_ERROR,
            //     L"[UserQuestData] QuestOperation_GiveItemRandom do not give items randomly. The probability is ( m_nProb /
            //     m_nHigh ). (QuestID:%d, m_nHigh:%d, m_nProb:%d)", static_cast<int>(params[0]->m_intVal),
            //     static_cast<int>(params[1]->m_intVal),
            //     static_cast<int>(params[2]->m_intVal))
            // L2_LOG_ERROR(Log,
            //     L"[UserQuestData] QuestOperation_GiveItemRandom do not give items randomly. The probability is ( m_nProb /
            //     m_nHigh ). (QuestID:%d, m_nHigh:%d, m_nProb:%d)", static_cast<int>(params[0]->m_intVal),
            //     static_cast<int>(params[1]->m_intVal),
            //     static_cast<int>(params[2]->m_intVal))
            L2_LOG_ERROR(
                Log, "Quest %lld: random-item threshold %lld must be below upper bound %lld; the operation is rejected.",
                static_cast<long long>(params[0]->m_intVal), static_cast<long long>(params[2]->m_intVal),
                static_cast<long long>(params[1]->m_intVal)
            );
            // Noise: The detailed threshold error above already explains this rejection.
            // invalidOperation only repeats a generic diagnostic and returns nullptr.
            // return invalidOperation()
            return nullptr;
        }

        case QOT_INSTANT_TELEPORT: {
            QuestDataVariable* const x = data.getParam(1);
            QuestDataVariable* const y = data.getParam(2);
            QuestDataVariable* const z = data.getParam(3);
            return x != nullptr && y != nullptr && z != nullptr
                ? static_cast<IOperation*>(new QuestOperation_InstantTeleport(
                      static_cast<int>(x->m_intVal), static_cast<int>(y->m_intVal), static_cast<int>(z->m_intVal)
                  ))
                : invalidOperation();
        }

        case QOT_INSTANT_TELEPORT_EX: {
            QuestDataVariable* const x = data.getParam(1);
            QuestDataVariable* const y = data.getParam(2);
            QuestDataVariable* const z = data.getParam(3);
            QuestDataVariable* const direction = data.getParam(4);
            return x != nullptr && y != nullptr && z != nullptr && direction != nullptr
                ? static_cast<IOperation*>(new QuestOperation_InstantTeleportEx(
                      static_cast<int>(x->m_intVal), static_cast<int>(y->m_intVal), static_cast<int>(z->m_intVal),
                      static_cast<int>(direction->m_intVal)
                  ))
                : invalidOperation();
        }

        case QOT_INVALID:
        default:
            L2_LOG_ERROR(
                Log, "Quest %d: unsupported operation type %lld (%llu parameters); no operation is created.", data.m_QuestID,
                static_cast<long long>(data.m_Type), static_cast<unsigned long long>(data.m_Params.size())
            );
            return nullptr;
    }
}

QuestAction* CreateAction(QuestActionData& data, const bool logToError) {
    // Current L2Server.exe 0x00a023b0.
    L2_GUARD_FUNCTION();

    QuestAction::ConditionList conditions;
    for (QuestConditionData& conditionData : data.m_Conditions) {
        ICondition* const condition = CreateCondition(conditionData);
        if (condition == nullptr) {
            DeleteQuestRuntimePointers(conditions);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CreateAction() - Fail to create condition",
            //     L"Quest.cpp",
            //     0x5cf)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] CreateAction() - Fail to create condition",
            //     L"Quest.cpp",
            //     0x5cf)
            L2_LOG_ERROR(
                Log,
                "Quest %d: action construction failed at condition type %lld (%llu parameters); already-created conditions "
                "are deleted and no action is created.",
                conditionData.m_QuestID, static_cast<long long>(conditionData.m_Type),
                static_cast<unsigned long long>(conditionData.m_Params.size())
            );
            return nullptr;
        }
        if (logToError) {
            condition->SetLogToError();
        }
        conditions.push_back(condition);
    }

    QuestAction::OperationList operations;
    for (QuestOperationData& operationData : data.m_Operations) {
        IOperation* const operation = CreateOperation(operationData);
        if (operation == nullptr) {
            DeleteQuestRuntimePointers(conditions);
            DeleteQuestRuntimePointers(operations);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CreateAction() - Fail to create operation",
            //     L"Quest.cpp",
            //     0x5ee)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] CreateAction() - Fail to create operation",
            //     L"Quest.cpp",
            //     0x5ee)
            L2_LOG_ERROR(
                Log,
                "Quest %d: action construction failed at operation type %lld (%llu parameters); already-created conditions "
                "and operations are deleted and no action is created.",
                operationData.m_QuestID, static_cast<long long>(operationData.m_Type),
                static_cast<unsigned long long>(operationData.m_Params.size())
            );
            return nullptr;
        }
        if (logToError) {
            operation->SetLogToError();
        }
        operations.push_back(operation);
    }

    auto* const action = new QuestAction;
    action->m_Conditions = conditions;
    action->m_Operations = operations;
    return action;
}

int QuestJournal::Create(QuestJournalData& data) {
    // Current L2Server.exe 0x00a02040.
    L2_GUARD_FUNCTION();

    for (QuestOperationData& operationData : data.m_startOperations) {
        IOperation* const operation = CreateOperation(operationData);
        if (operation == nullptr) {
            return 0;
        }
        if (IsNeedLog(m_QuestID)) {
            operation->SetLogToError();
        }
        m_StartOperations.push_back(operation);
    }
    for (QuestConditionData& conditionData : data.m_completeConditions) {
        ICondition* const condition = CreateCondition(conditionData);
        if (condition == nullptr) {
            return 0;
        }
        if (IsNeedLog(m_QuestID)) {
            condition->SetLogToError();
        }
        m_CompleteConditions.push_back(condition);
    }
    for (QuestOperationData& operationData : data.m_completeOperations) {
        IOperation* const operation = CreateOperation(operationData);
        if (operation == nullptr) {
            return 0;
        }
        if (IsNeedLog(m_QuestID)) {
            operation->SetLogToError();
        }
        m_CompleteOperations.push_back(operation);
    }
    m_Number = data.m_nJournalNum;
    return 1;
}

int UserQuest::Create(QuestData* const data) {
    // Current L2Server.exe 0x00a02770.
    L2_GUARD_FUNCTION();
    m_QuestID = data->m_questID;
    m_Name = data->m_wstrQuestName;
    const bool logToError = IsNeedLog(static_cast<int>(m_QuestID));

    for (QuestConditionData& source : data->m_startConditions) {
        ICondition* const condition = CreateCondition(source);
        if (condition == nullptr) {
            return 0;
        }
        if (logToError) {
            condition->SetLogToError();
        }
        m_StartConditions.push_back(condition);
    }
    for (QuestOperationData& source : data->m_acceptOKOperations) {
        IOperation* const operation = CreateOperation(source);
        if (operation == nullptr) {
            return 0;
        }
        if (logToError) {
            operation->SetLogToError();
        }
        m_AcceptOperations.push_back(operation);
    }
    for (QuestOperationData& source : data->m_acceptFailOperations) {
        IOperation* const operation = CreateOperation(source);
        if (operation == nullptr) {
            return 0;
        }
        if (logToError) {
            operation->SetLogToError();
        }
        m_AcceptFailOperations.push_back(operation);
    }
    for (QuestActionData& source : data->m_completeActions) {
        QuestAction* const action = CreateAction(source, logToError);
        if (action == nullptr) {
            return 0;
        }
        m_CompleteActions.push_back(action);
    }
    for (QuestOperationData& source : data->m_cancelOperations) {
        IOperation* const operation = CreateOperation(source);
        if (operation == nullptr) {
            return 0;
        }
        if (logToError) {
            operation->SetLogToError();
        }
        m_CancelOperations.push_back(operation);
    }
    for (QuestJournalData& source : data->m_journals) {
        auto* const journal = new QuestJournal;
        journal->m_Number = 0;
        journal->m_QuestID = static_cast<int>(m_QuestID);
        if (!journal->Create(source)) {
            delete journal;
            return 0;
        }
        m_Journals.push_back(journal);
    }
    m_NumJournals = static_cast<int>(data->m_journals.size());
    return 1;
}

void QuestNpcEvent::SetCondtionTargetType(const QuestConditionTargetType type) {
    // Current L2Server.exe 0x009fc560.
    L2_GUARD_FUNCTION();
    m_conditionTargetType = type;
}

QuestConditionTargetType QuestNpcEvent::GetCondtionTargetType() {
    // Current L2Server.exe 0x009fc5fc.
    L2_GUARD_FUNCTION();
    return m_conditionTargetType;
}

void QuestNpcEvent::SetExecutionType(const QuestExecutionType type) {
    // Current L2Server.exe 0x009fc688.
    L2_GUARD_FUNCTION();
    m_executionType = type;
}

QuestExecutionType QuestNpcEvent::GetExecutionType() {
    // Current L2Server.exe 0x009fc724.
    L2_GUARD_FUNCTION();
    return m_executionType;
}

QuestNpcEvent::ConditionList QuestNpcEvent::GetConditions() const { return m_conditions; }

void QuestNpcEvent::SetConditions(QuestNpcEventData::ConditionList& conditions) {
    // Current L2Server.exe 0x00a01e20.
    L2_GUARD_FUNCTION();
    for (QuestConditionData& source : conditions) {
        ICondition* const condition = CreateCondition(source);
        if (condition != nullptr) {
            if (m_bLogToError) {
                condition->SetLogToError();
            }
            m_conditions.push_back(condition);
        }
    }
}

void QuestNpcEvent::SetSystemOperations(QuestNpcEventData::OperationList& operations) {
    // Current L2Server.exe 0x00a01ef8.
    L2_GUARD_FUNCTION();
    for (QuestOperationData& source : operations) {
        IOperation* const operation = CreateOperation(source);
        if (operation != nullptr) {
            if (m_bLogToError) {
                operation->SetLogToError();
            }
            m_systemOperations.push_back(operation);
        }
    }
}

void QuestNpcEvent::SetActions(QuestNpcEventData::ActionList& actions) {
    // Current L2Server.exe 0x00a0269c.
    L2_GUARD_FUNCTION();
    for (QuestActionData& source : actions) {
        QuestAction* const action = CreateAction(source, m_bLogToError);
        if (action != nullptr) {
            m_actions.push_back(action);
        }
    }
}

void QuestNpcEvent::SetChoice(const int choice) noexcept { m_choice = choice; }

int QuestNpcEvent::GetChoice() const noexcept { return m_choice; }

void QuestNpcEvent::SetEventRange(const int range) noexcept { m_eventRange = range; }

int QuestNpcEvent::GetEventRange() {
    // Current L2Server.exe 0x009fca00.
    L2_GUARD_FUNCTION();
    if (m_eventRange != 0) {
        return m_eventRange;
    }
    return m_conditionTargetType == QCTT_MPCC_ALL ? 0xaf0 : 0x5dc;
}

void QuestNpcEvent::SetAskReply(const std::int64_t ask, const std::int64_t reply) noexcept {
    m_ask = ask;
    m_reply = reply;
}

bool QuestNpcEvent::IsMatchAskReply(const std::int64_t ask, const std::int64_t reply) const noexcept {
    return m_ask == ask && m_reply == reply;
}

void QuestNpcEvent::SetLogToError() { m_bLogToError = true; }

QuestNpcEventController::QuestNpcEventController() noexcept :
    m_eventType(QUEST_NPC_EVENT_INVALID), m_bLogToError(false) {}

QuestNpcEventController::QuestNpcEventController(const QuestNpcEventType eventType) noexcept :
    m_eventType(eventType), m_bLogToError(false) {}

QuestNpcEventController::QuestNpcEventController(const QuestNpcEventController& other) :
    m_eventType(other.m_eventType), m_eventList(other.m_eventList), m_bLogToError(other.m_bLogToError) {}

bool QuestNpcEventController::IsMatchEventType(const QuestNpcEventType eventType) const noexcept {
    return m_eventType == eventType;
}

void QuestNpcEventController::AddEvent(QuestNpcEvent* const event) {
    // Current L2Server.exe 0x00a01d84.
    L2_GUARD_FUNCTION();
    m_eventList.push_back(event);
}

void QuestNpcEventController::SetLogToError() { m_bLogToError = true; }

QuestNpcDieEventController::QuestNpcDieEventController() noexcept : QuestNpcEventController(QUEST_NPC_EVENT_DIE) {}

QuestTalkSelectedEventController::QuestTalkSelectedEventController() noexcept :
    QuestNpcEventController(QUEST_NPC_EVENT_TALK_SELECTED) {}

QuestMenuSelectedEventController::QuestMenuSelectedEventController() noexcept :
    QuestNpcEventController(QUEST_NPC_EVENT_MENU_SELECTED) {}

QuestNpcEventController* MakeEventController(const QuestNpcEventType eventType) {
    // Current L2Server.exe 0x00a01c2c.
    L2_GUARD_FUNCTION();
    switch (eventType) {
        case QUEST_NPC_EVENT_DIE:
            return new QuestNpcDieEventController;
        case QUEST_NPC_EVENT_TALK_SELECTED:
            return new QuestTalkSelectedEventController;
        case QUEST_NPC_EVENT_MENU_SELECTED:
            return new QuestMenuSelectedEventController;
        default:
            return nullptr;
    }
}

bool QuestNpcEvent::Check(User& user) {
    // Current L2Server.exe 0x00a03464.
    L2_GUARD_FUNCTION();
    std::vector<User*> candidates;
    if (m_conditionTargetType >= QCTT_NONE) {
        if (m_conditionTargetType <= QCTT_PC) {
            candidates.push_back(&user);
        } else if (m_conditionTargetType <= QCTT_PARTY_ALL) {
            if (CParty* const party = user.GetParty()) {
                party->GetAllMember(candidates);
            }
            if (candidates.empty()) {
                candidates.push_back(&user);
            }
        } else if (m_conditionTargetType == QCTT_MPCC_ALL) {
            if (CMultiPartyCommandChannel* const channel = user.GetMPCC()) {
                channel->GetAllMember(candidates);
            }
            if (candidates.empty()) {
                if (CParty* const party = user.GetParty()) {
                    party->GetAllMember(candidates);
                }
            }
            if (candidates.empty()) {
                candidates.push_back(&user);
            }
        } else if (m_conditionTargetType == QCTT_CONTRIBUTOR) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"QCTT_CONTRIBUTOR is not implemented for random1")
            // L2_LOG_ERROR(Log,
            //     L"QCTT_CONTRIBUTOR is not implemented for random1")
            // L2_LOG_ERROR(Log,
            //     "User-only quest check for character '%s' (DBID %u) does not support contributor targets. No candidates
            //     are collected and the check returns false.", WideToUtf8(user.GetName()).c_str(), static_cast<unsigned
            //     int>(user.GetDBID()))
            L2_LOG_ERROR(
                Log,
                "User-only quest check for character '%s' (DBID %u) does not support contributor targets; no candidates are "
                "collected and the check returns false.",
                Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID())
            );
        }
    }

    for (User* const candidate : candidates) {
        bool satisfied = true;
        for (ICondition* const condition : m_conditions) {
            if (!condition->Check(*candidate)) {
                satisfied = false;
                break;
            }
        }
        if (satisfied) {
            return true;
        }
    }
    return false;
}

bool QuestNpcEvent::Check(User& user, CNPC& npc, std::vector<User*>& users) {
    // Current L2Server.exe 0x00a048fc.
    L2_GUARD_FUNCTION();
    std::vector<User*> candidates;
    if (m_conditionTargetType == QCTT_CONTRIBUTOR) {
        std::map<std::uint32_t, double> contributions;
        npc.m_pContribute->GetContributeInfos(contributions);
        AddMemberFromContributeType(contributions, candidates);
        FilterMemberByCondition(m_conditions, candidates, m_bLogToError, user.GetName(), users);
        FilterMemberByRandom(_CONTRIBUTOR_LIMIT, users);
    } else {
        if (m_conditionTargetType >= QCTT_NONE) {
            if (m_conditionTargetType <= QCTT_PC) {
                candidates.push_back(&user);
            } else if (m_conditionTargetType <= QCTT_PARTY_ALL) {
                AddMemberFromParty(user, m_bLogToError, candidates);
                if (candidates.empty()) {
                    candidates.push_back(&user);
                }
            } else if (m_conditionTargetType == QCTT_MPCC_ALL) {
                AddMemberFromMPCC(user, m_bLogToError, candidates);
                if (candidates.empty()) {
                    AddMemberFromParty(user, m_bLogToError, candidates);
                }
                if (candidates.empty()) {
                    candidates.push_back(&user);
                }
            }
        }

        FilterMemberByCondition(m_conditions, candidates, m_bLogToError, user.GetName(), users);
    }
    return !users.empty();
}

bool QuestNpcEvent::Do(std::vector<User*> users, const int npcId) {
    // Current L2Server.exe 0x00a04bb4.
    L2_GUARD_FUNCTION();
    int reserveCount = 1;
    for (QuestAction* const action : m_actions) {
        reserveCount += static_cast<int>(action->m_Operations.size());
    }

    for (User* const rawUser : users) {
        // CSPointer<User> kept = g_UserMap.FindObject(
        //     static_cast<int>(rawUser->Id()))
        const int userId = static_cast<int>(rawUser->Id());
        CSPointer<User> retained = g_UserMap.FindObject(userId);
        const bool invalid = !retained;
        const bool deleting = retained && (retained->IsDeleting() || retained->m_Deleted != 0);
        if (invalid || deleting) {
            if (m_bLogToError) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[UserQuestData] User[%s] is Invalid or Deleting. Invalid[%d], Deleting[%d]",
                //     rawUser->GetName(),
                //     invalid ? 1 : 0,
                //     deleting ? 1 : 0)
                // L2_LOG_ERROR(Log,
                //     L"[UserQuestData] User[%s] is Invalid or Deleting. Invalid[%d], Deleting[%d]",
                //     rawUser->GetName(),
                //     invalid ? 1 : 0,
                //     deleting ? 1 : 0)
                L2_LOG_INFO(
                    Log, "Skipping NPC %d quest event for character object ID %u: lookup failed: %s, deleting: %s.", npcId,
                    static_cast<unsigned int>(userId), invalid ? "yes" : "no", deleting ? "yes" : "no"
                );
            }
            continue;
        }

        L2_ADD_REF(retained, ObjectRefType::Anonymous);
        auto* const jobs = new AtomicJobList(reserveCount, static_cast<int>(rawUser->Id()));
        ItemManipList manipulations;
        bool actionExecuted = false;
        for (QuestAction* const action : m_actions) {
            if (action->Check(*rawUser)) {
                action->Do(*rawUser, jobs, manipulations, npcId);
                actionExecuted = true;
            }
        }
        if (actionExecuted) {
            for (IOperation* const operation : m_systemOperations) {
                operation->Do(*rawUser, jobs, manipulations, npcId);
            }
        }
        jobs->AddAtomicJob(new CAtomicSaveQuestInfo(static_cast<int>(rawUser->Id())));

        CCreature* const npcObject = g_CreatureSmartIdManager.GetObjectW(npcId);
        CNPC* const npc = npcObject != nullptr && npcObject->IsNPC() ? npcObject->CastNPC() : nullptr;
        if (m_bLogToError) {
            // Log.Add(
            //     LOG_ERROR,
            //     npc
            //         ? L"[UserQuestData] Call ManipulateItemAndDoAtomicJob. NPC(%d) User(%s)"
            //         : L"[UserQuestData] Invalid NpcID(%d) User(%s)",
            //     npcId,
            //     rawUser->GetName())
            // L2_LOG_ERROR(Log,
            //     npc
            //         ? L"[UserQuestData] Call ManipulateItemAndDoAtomicJob. NPC(%d) User(%s)"
            //         : L"[UserQuestData] Invalid NpcID(%d) User(%s)",
            //     npcId,
            //     rawUser->GetName())
            // L2_LOG_INFO(Log,
            //     "Preparing to submit item manipulations and atomic jobs for character '%s' (DBID %u), NPC object ID %d
            //     (NPC resolved: %s, an action passed its conditions: %s). Quest-state saving is included.",
            //     WideToUtf8(rawUser->GetName()).c_str(), static_cast<unsigned int>(rawUser->GetDBID()), npcId, npc !=
            //     nullptr ? "yes" : "no", actionExecuted ? "yes" : "no")
            L2_LOG_INFO(
                Log,
                "Preparing to submit item manipulations and atomic jobs for character '%s' (DBID %u), NPC object ID %d (NPC "
                "resolved: %s, an action passed its conditions: %s); quest-state saving is included.",
                Utf16ToUtf8(rawUser->GetName()).c_str(), static_cast<unsigned int>(rawUser->GetDBID()), npcId,
                npc != nullptr ? "yes" : "no", actionExecuted ? "yes" : "no"
            );
        }
        rawUser->ManipulateItemAndDoAtomicJob(manipulations, jobs, ItemGetReason::Etc, false);
        L2_RELEASE(retained, ObjectRefType::Anonymous);
    }
    return true;
}

bool QuestTalkSelectedEventController::Do(User& user, CNPC* const npc, const std::int64_t, const std::int64_t) {
    // Current L2Server.exe 0x00a058e4.
    L2_GUARD_FUNCTION();
    if (!m_eventList.empty()) {
        std::vector<User*> users;
        users.push_back(user.CastUser());
        for (QuestNpcEvent* const event : m_eventList) {
            if (event->Check(user)) {
                if (m_bLogToError) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[UserQuestData] Do TalkSelect Event. NPC(%s) User(%s)",
                    //     npc->GetName(),
                    //     user.GetName())
                    // L2_LOG_ERROR(Log,
                    //     L"[UserQuestData] Do TalkSelect Event. NPC(%s) User(%s)",
                    //     npc->GetName(),
                    //     user.GetName())
                    // L2_LOG_INFO(Log,
                    //     "Dispatching quest talk-selection event from NPC '%s' (object ID %u) to character '%s' (DBID
                    //     %u).", WideToUtf8(npc->GetName()).c_str(), static_cast<unsigned int>(npc->Id()),
                    //     WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()))
                    L2_LOG_INFO(
                        Log,
                        "Dispatching quest talk-selection event from NPC '%s' (object ID %u) to character '%s' (DBID %u).",
                        Utf16ToUtf8(npc->GetName()).c_str(), npc->Id(),
                        Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID())
                    );
                }
                event->Do(users, static_cast<int>(npc->Id()));
            }
        }
    }
    return false;
}

bool QuestMenuSelectedEventController::Do(User& user, CNPC* const npc, const std::int64_t ask, const std::int64_t reply) {
    // Current L2Server.exe 0x00a05a80.
    L2_GUARD_FUNCTION();
    if (!m_eventList.empty()) {
        std::vector<User*> users;
        users.push_back(user.CastUser());
        for (QuestNpcEvent* const event : m_eventList) {
            if (event->IsMatchAskReply(ask, reply) && event->Check(user)) {
                if (m_bLogToError) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[UserQuestData] Do MenuSelect Event. NPC(%s) User(%s)",
                    //     npc->GetName(),
                    //     user.GetName())
                    // L2_LOG_ERROR(Log,
                    //     L"[UserQuestData] Do MenuSelect Event. NPC(%s) User(%s)",
                    //     npc->GetName(),
                    //     user.GetName())
                    // L2_LOG_INFO(Log,
                    //     "Dispatching quest menu-selection event from NPC '%s' (object ID %u) to character '%s' (DBID %u),
                    //     ask %lld, reply %lld.", WideToUtf8(npc->GetName()).c_str(), static_cast<unsigned int>(npc->Id()),
                    //     WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), static_cast<long
                    //     long>(ask), static_cast<long long>(reply))
                    L2_LOG_INFO(
                        Log,
                        "Dispatching quest menu-selection event from NPC '%s' (object ID %u) to character '%s' (DBID %u), "
                        "ask %lld, reply %lld.",
                        Utf16ToUtf8(npc->GetName()).c_str(), npc->Id(),
                        Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
                        static_cast<long long>(ask), static_cast<long long>(reply)
                    );
                }
                event->Do(users, static_cast<int>(npc->Id()));
            }
        }
    }
    return false;
}

bool QuestNpcDieEventController::Do(User& user, CNPC* const npc, const std::int64_t, const std::int64_t) {
    // Current L2Server.exe 0x00a0519c.
    L2_GUARD_FUNCTION();
    if (m_eventList.empty() || npc == nullptr) {
        return false;
    }

    auto filterByRange = [this, npc](QuestNpcEvent& event, std::vector<User*>& users) {
        const double range = static_cast<double>(event.GetEventRange());
        for (auto position = users.begin(); position != users.end();) {
            User* const candidate = *position;
            if (npc->IsInRange(candidate, range)) {
                ++position;
                continue;
            }
            if (m_bLogToError) {
                const FVector userPosition = candidate->GetPos();
                const FVector npcPosition = npc->GetPos();
                // Log.Add(
                //     LOG_ERROR,
                //     L"[UserQuestData] User[%s] is so far from NPC[%s]. UserPos(%d,%d,%d) NPCPos(%d,%d,%d) dRange(%lf)",
                //     candidate->GetName(),
                //     npc->GetName(),
                //     static_cast<int>(userPosition.x),
                //     static_cast<int>(userPosition.y),
                //     static_cast<int>(userPosition.z),
                //     static_cast<int>(npcPosition.x),
                //     static_cast<int>(npcPosition.y),
                //     static_cast<int>(npcPosition.z),
                //     range)
                // L2_LOG_ERROR(Log,
                //     L"[UserQuestData] User[%s] is so far from NPC[%s]. UserPos(%d,%d,%d) NPCPos(%d,%d,%d) dRange(%lf)",
                //     candidate->GetName(),
                //     npc->GetName(),
                //     static_cast<int>(userPosition.x),
                //     static_cast<int>(userPosition.y),
                //     static_cast<int>(userPosition.z),
                //     static_cast<int>(npcPosition.x),
                //     static_cast<int>(npcPosition.y),
                //     static_cast<int>(npcPosition.z),
                //     range)
                // L2_LOG_INFO(Log,
                //     "Excluding quest target character '%s' (DBID %u) outside NPC '%s' (object ID %u) range %.17g:
                //     character position (%.17g, %.17g, %.17g), NPC position (%.17g, %.17g, %.17g).",
                //     WideToUtf8(candidate->GetName()).c_str(), static_cast<unsigned int>(candidate->GetDBID()),
                //     WideToUtf8(npc->GetName()).c_str(), static_cast<unsigned int>(npc->Id()), range, userPosition.x,
                //     userPosition.y, userPosition.z, npcPosition.x, npcPosition.y, npcPosition.z)
                L2_LOG_INFO(
                    Log,
                    "Excluding quest target character '%s' (DBID %u) outside NPC '%s' (object ID %u) range %.17g: character "
                    "position (%.17g, %.17g, %.17g), NPC position (%.17g, %.17g, %.17g).",
                    Utf16ToUtf8(candidate->GetName()).c_str(), static_cast<unsigned int>(candidate->GetDBID()),
                    Utf16ToUtf8(npc->GetName()).c_str(), npc->Id(), range, userPosition.x,
                    userPosition.y, userPosition.z, npcPosition.x, npcPosition.y, npcPosition.z
                );
            }
            position = users.erase(position);
        }
    };

    auto keepOnePartyMember = [](QuestNpcEvent& event, std::vector<User*>& users) {
        if (event.GetCondtionTargetType() == QCTT_PARTY_ONE && !users.empty()) {
            User* const selected = runtime::CheckedVectorAt(users, QuestTargetRandomIndex(users.size()));
            users.clear();
            users.push_back(selected);
        }
    };

    std::vector<User*> users;
    std::vector<QuestNpcEvent*> randomEvents;
    for (QuestNpcEvent* const event : m_eventList) {
        if (event->GetExecutionType() == QET_ALWAYS) {
            users.clear();
            if (event->Check(user, *npc, users)) {
                filterByRange(*event, users);
                keepOnePartyMember(*event, users);
                if (m_bLogToError) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[UserQuestData] Do Always NpcDie Event. NPC(%s) User(%s)",
                    //     npc->GetName(),
                    //     user.GetName())
                    // L2_LOG_ERROR(Log,
                    //     L"[UserQuestData] Do Always NpcDie Event. NPC(%s) User(%s)",
                    //     npc->GetName(),
                    //     user.GetName())
                    // L2_LOG_INFO(Log,
                    //     "Dispatching always-run NPC-death quest event from '%s' (object ID %u), source character '%s'
                    //     (DBID %u): %llu targets remain after filtering.", WideToUtf8(npc->GetName()).c_str(),
                    //     static_cast<unsigned int>(npc->Id()), WideToUtf8(user.GetName()).c_str(), static_cast<unsigned
                    //     int>(user.GetDBID()), static_cast<unsigned long long>(users.size()))
                    L2_LOG_INFO(
                        Log,
                        "Dispatching always-run NPC-death quest event from '%s' (object ID %u), source character '%s' (DBID "
                        "%u): %llu targets remain after filtering.",
                        Utf16ToUtf8(npc->GetName()).c_str(), npc->Id(),
                        Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
                        static_cast<unsigned long long>(users.size())
                    );
                }
                event->Do(users, static_cast<int>(npc->Id()));
            }
        } else if (event->GetExecutionType() == QET_RANDOM1 && event->Check(user)) {
            randomEvents.push_back(event);
        }
    }

    if (!randomEvents.empty()) {
        QuestNpcEvent* const event = randomEvents[QuestTargetRandomIndex(randomEvents.size())];
        users.clear();
        if (event->Check(user, *npc, users)) {
            filterByRange(*event, users);
            keepOnePartyMember(*event, users);
            if (m_bLogToError) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[UserQuestData] Do Random NpcDie Event. NPC(%s) User(%s)",
                //     npc->GetName(),
                //     user.GetName())
                // L2_LOG_ERROR(Log,
                //     L"[UserQuestData] Do Random NpcDie Event. NPC(%s) User(%s)",
                //     npc->GetName(),
                //     user.GetName())
                // L2_LOG_INFO(Log,
                //     "Dispatching the selected random NPC-death quest event from '%s' (object ID %u), source character '%s'
                //     (DBID %u): %llu targets remain after filtering.", WideToUtf8(npc->GetName()).c_str(),
                //     static_cast<unsigned int>(npc->Id()), WideToUtf8(user.GetName()).c_str(), static_cast<unsigned
                //     int>(user.GetDBID()), static_cast<unsigned long long>(users.size()))
                L2_LOG_INFO(
                    Log,
                    "Dispatching the selected random NPC-death quest event from '%s' (object ID %u), source character '%s' "
                    "(DBID %u): %llu targets remain after filtering.",
                    Utf16ToUtf8(npc->GetName()).c_str(), npc->Id(),
                    Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
                    static_cast<unsigned long long>(users.size())
                );
            }
            event->Do(users, static_cast<int>(npc->Id()));
        }
    }
    return false;
}
