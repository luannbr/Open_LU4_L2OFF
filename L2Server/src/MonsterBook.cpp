#include "MonsterBook.h"
#include "DB.h"
#include "Inventory.h"
#include "NPC.h"
#include "Object.h"
#include "ObjectDB.h"
#include "ParserMonsterBook.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <optional>

namespace MonsterBook {

int Script::TrophyTable::GetKillCount(int level) {
    L2_GUARD_FUNCTION();

    if (static_cast<unsigned int>(level) < m_nKillData.size()) {
        return m_nKillData[static_cast<std::size_t>(level)];
    }

    // Log.Add(
    //     LOG_ERROR,
    //     "[MonsterBook] invalid kill count table level[%d]",
    //     level)
    // L2_LOG_ERROR(Log,
    //     "[MonsterBook] invalid kill count table level[%d]",
    //     level)
    L2_LOG_ERROR(Log, "Monster Book kill-count lookup failed: level index %d is outside 0..3; returning -1.", level);
    return -1;
}

Script::Card::Card() : m_nCardId(-1), m_pFactionInfo(nullptr), m_nNpcIdSet{}, m_pTrophyTable(nullptr), m_reward{} {
    for (std::vector<ItemAmountData>& items : m_reward.m_vItem) {
        items.clear();
    }
}

void Script::Card::AddFactionPoints(const std::vector<std::int64_t>& points) {
    if (points.size() != m_reward.m_nRewardFP.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook] AddFactionPoints invalid level script")
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook] AddFactionPoints invalid level script")
        L2_LOG_ERROR(
            Log, "Monster Book card %d faction reward data ignored: got %zu level entries, expected %zu.", m_nCardId,
            points.size(), m_reward.m_nRewardFP.size()
        );
        return;
    }
    std::copy(points.begin(), points.end(), m_reward.m_nRewardFP.begin());
}

void Script::Card::AddExpPoints(const std::vector<std::int64_t>& points) {
    if (points.size() != m_reward.m_nRewardEXP.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook] AddExpPoints invalid level script")
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook] AddExpPoints invalid level script")
        L2_LOG_ERROR(
            Log, "Monster Book card %d experience reward data ignored: got %zu level entries, expected %zu.", m_nCardId,
            points.size(), m_reward.m_nRewardEXP.size()
        );
        return;
    }
    std::copy(points.begin(), points.end(), m_reward.m_nRewardEXP.begin());
}

void Script::Card::AddSPPoints(const std::vector<std::int64_t>& points) {
    if (points.size() != m_reward.m_nRewardSP.size()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook] AddSPPoints invalid level script")
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook] AddSPPoints invalid level script")
        L2_LOG_ERROR(
            Log, "Monster Book card %d SP reward data ignored: got %zu level entries, expected %zu.", m_nCardId,
            points.size(), m_reward.m_nRewardSP.size()
        );
        return;
    }
    std::copy(points.begin(), points.end(), m_reward.m_nRewardSP.begin());
}

void Script::Card::AddNPCIdSet(const std::vector<std::int64_t>& npcIds) {
    int* destination = m_nNpcIdSet.data();
    for (const std::int64_t npcId : npcIds) {
        *destination++ = static_cast<int>(npcId);
    }
}

void Script::Card::AddItems(int level, const std::vector<ItemAmountData>& items) {
    for (const ItemAmountData& item : items) {
        m_reward.m_vItem[static_cast<std::size_t>(level)].push_back(item);
    }
}

Script::Script() :
    m_nMaxCardCount(0),
    m_vCardList() {}

Script::~Script() = default;

Script::Card* Script::GetCard(int cardId) {
    L2_GUARD_FUNCTION();

    const CardMap::iterator found = m_mapCard.find(cardId);
    return found == m_mapCard.end() ? nullptr : found->second;
}

Script::Card* Script::GetCardByNpcId(int npcId) {
    L2_GUARD_FUNCTION();

    const CardMap::iterator found = m_mapCardByNpcId.find(npcId);
    return found == m_mapCardByNpcId.end() ? nullptr : found->second;
}

std::vector<int>& Script::GetCardIdListByFactionId(int factionId) {
    L2_GUARD_FUNCTION();

    if (static_cast<unsigned int>(factionId) > 15U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[GetCardIdListByFactionId] invalid faction id![%d]",
        //     factionId)
        // L2_LOG_ERROR(Log,
        //     L"[GetCardIdListByFactionId] invalid faction id![%d]",
        //     factionId)
        L2_LOG_ERROR(
            Log, "Monster Book faction index %d is outside 0..15; the following card-list access is invalid.", factionId
        );
    }
    return m_vCardList[static_cast<std::size_t>(factionId)];
}

Script::FactionInfo* Script::GetFactionInfo(int factionId) {
    L2_GUARD_FUNCTION();

    const FactionMap::iterator found = m_mapFaction.find(factionId);
    return found == m_mapFaction.end() ? nullptr : found->second;
}

Script::TrophyTable* Script::GetTrophyTable(const char16_t* tableName) {
    const std::u16string key(tableName == nullptr ? u"" : tableName);
    const TrophyTableMap::iterator found = m_mapKillTable.find(key);
    if (found == m_mapKillTable.end()) {
        const std::wstring hostName = target_utf16::ToHostCodeUnits(tableName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook]invalid kill group name! [%s]",
        //     hostName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook]invalid kill group name! [%s]",
        //     hostName.c_str())
        L2_LOG_ERROR(
            Log, "Monster Book kill-count table '%s' is not registered; lookup returns no table.",
            Utf16ToUtf8(target_utf16::FromHostCodeUnits(hostName)).c_str()
        );
        return nullptr;
    }
    return found->second;
}

void Script::AddCard(Card* card) {
    if (card == nullptr) {
        // Log.Add(LOG_ERROR, L"[MonsterBook][AddCard] invalid card!")
        // L2_LOG_ERROR(Log, L"[MonsterBook][AddCard] invalid card!")
        L2_LOG_ERROR(Log, "Cannot register a Monster Book card: card data is missing.");
        return;
    }

    m_mapCard.emplace(card->m_nCardId, card);
    for (const int npcId : card->m_nNpcIdSet) {
        if (npcId == 0) {
            break;
        }
        if (!m_mapCardByNpcId.emplace(npcId, card).second) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[MonsterBook][AddCard] Duplicated Monster! npcid [%d]",
            //     npcId)
            // L2_LOG_ERROR(Log,
            //     L"[MonsterBook][AddCard] Duplicated Monster! npcid [%d]",
            //     npcId)
            L2_LOG_ERROR(
                Log,
                "Monster Book NPC %d already has a card mapping; keeping that mapping while registration of card %d "
                "continues.",
                npcId, card->m_nCardId
            );
        }
    }

    const std::uint32_t factionId =
        card->m_pFactionInfo == nullptr ? UINT32_MAX : static_cast<std::uint32_t>(card->m_pFactionInfo->m_nFactionId);
    if (factionId < 16U) {
        m_vCardList[factionId].push_back(card->m_nCardId);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][AddCard] invalid faction index![%d]",
        //     static_cast<int>(factionId))
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook][AddCard] invalid faction index![%d]",
        //     static_cast<int>(factionId))
        L2_LOG_ERROR(
            Log,
            "Monster Book card %d has missing or invalid faction index %u (allowed 0..15); faction-list registration "
            "skipped.",
            card->m_nCardId, static_cast<unsigned>(factionId)
        );
    }
}

void Script::AddKillTable(const std::u16string& tableName, TrophyTable* table) {
    L2_GUARD_FUNCTION();

    if (table == nullptr) {
        const std::wstring hostName = target_utf16::ToHostCodeUnits(tableName.c_str());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][AddKillTable] invalid tablename[%s]",
        //     hostName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook][AddKillTable] invalid tablename[%s]",
        //     hostName.c_str())
        L2_LOG_ERROR(
            Log,
            "Cannot register Monster Book kill-count table '%s': table data is missing; initiating a fatal script-integrity "
            "report.",
            Utf16ToUtf8(target_utf16::FromHostCodeUnits(hostName)).c_str()
        );
        ForceCrash(CrashInfo::INVALID_SCRIPT_INTEGRITY);
        return;
    }

    if (!m_mapKillTable.emplace(tableName, table).second) {
        const std::wstring hostName = target_utf16::ToHostCodeUnits(tableName.c_str());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][KillTable] duplicated!![%s]",
        //     hostName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook][KillTable] duplicated!![%s]",
        //     hostName.c_str())
        L2_LOG_ERROR(
            Log, "Duplicate Monster Book kill-count table '%s' ignored; the registered table is retained.",
            Utf16ToUtf8(target_utf16::FromHostCodeUnits(hostName)).c_str()
        );
    }
}

void Script::AddFactionInfo(int factionId, FactionInfo* factionInfo) {
    L2_GUARD_FUNCTION();
    if (!factionInfo) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][AddFactionInfo] invalid tablename[%d]",
        //     factionId)
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook][AddFactionInfo] invalid tablename[%d]",
        //     factionId)
        L2_LOG_ERROR(
            Log,
            "Cannot register Monster Book faction %d: faction data is missing; initiating a fatal script-integrity report.",
            factionId
        );
        ForceCrash(CrashInfo::INVALID_SCRIPT_INTEGRITY);
        return;
    }

    if (!m_mapFaction.emplace(factionId, factionInfo).second) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][KillTable] duplicated!![%d]",
        //     factionId)
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook][KillTable] duplicated!![%d]",
        //     factionId)
        L2_LOG_ERROR(Log, "Duplicate Monster Book faction %d ignored; the registered faction is retained.", factionId);
    }
}

bool Script::RegisterNPCsOnMonsterBook() {
    L2_GUARD_FUNCTION();

    for (const auto& entry : m_mapCardByNpcId) {
        CObject* const object = g_ObjectDB.GetObjectW(entry.first + 1000000, false);
        if (object == nullptr || !object->IsNPC()) {
            if (entry.second == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[MonsterBook]Where is NPC[%d]?? and Where is Card?",
                //     entry.first)
                // L2_LOG_ERROR(Log,
                //     L"[MonsterBook]Where is NPC[%d]?? and Where is Card?",
                //     entry.first)
                L2_LOG_ERROR(
                    Log,
                    "Monster Book NPC registration stopped at NPC ID %d: NPC template is missing or not an NPC, and card "
                    "data is missing; earlier NPC marks remain.",
                    entry.first
                );
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[MonsterBook]Where is NPC?? card id[%d] npc id[%d]",
                //     entry.second->m_nCardId,
                //     entry.first)
                // L2_LOG_ERROR(Log,
                //     L"[MonsterBook]Where is NPC?? card id[%d] npc id[%d]",
                //     entry.second->m_nCardId,
                //     entry.first)
                L2_LOG_ERROR(
                    Log,
                    "Monster Book NPC registration stopped at card %d, NPC ID %d: template is missing or not an NPC; "
                    "earlier NPC marks remain.",
                    entry.second->m_nCardId, entry.first
                );
            }
            return false;
        }

        CNPC* const npc = object->CastNPC();
        if (npc != nullptr) {
            npc->SetMonsterBookNPC(true);
        }
    }
    return true;
}

bool Script::ValidateIndexAndCount() {
    int maximumCardId = 0;
    for (const auto& entry : m_mapCard) {
        if (entry.first > maximumCardId) {
            maximumCardId = entry.first;
        }
    }

    if (maximumCardId < 400 && m_mapCard.size() < 400) {
        m_nMaxCardCount = static_cast<int>(m_mapCard.size());
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[MonsterBook][ValidateIndexAndCount] too many cards...!")
    // L2_LOG_ERROR(Log,
    //     L"[MonsterBook][ValidateIndexAndCount] too many cards...!")
    L2_LOG_ERROR(
        Log, "Monster Book card validation failed: maximum card ID %d, registered count %zu; both must be below 400.",
        maximumCardId, m_mapCard.size()
    );
    return false;
}

void Script::LoadScript() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kTrophyPath = L"..\\Script\\monsterbook_trophy.txt";
    constexpr const wchar_t* kCardPath = L"..\\Script\\monsterbook.txt";
#else
    constexpr const wchar_t* kTrophyPath = L"../script/monsterbook_trophy.txt";
    constexpr const wchar_t* kCardPath = L"../script/monsterbook.txt";
#endif

    CSecureStream trophyInput(kTrophyPath, true);
    if (trophyInput.fail()) {
        L2_LOG_ERROR(Log, "Cannot read monsterbook_trophy.txt; Monster Book loading stops.");
        return;
    }

    CParserForMonsterBook trophyParser;
    if (!trophyParser.Create(trophyInput.GetRawString() + 1, trophyInput.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] objectParser.create failed",
        //     L"MonsterBook.cpp",
        //     0x3e)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] objectParser.create failed",
        //     L"MonsterBook.cpp",
        //     0x3e)
        L2_LOG_ERROR(Log, "Cannot initialize monsterbook_trophy.txt parser; Monster Book loading stops.");
        return;
    }
    trophyParser.m_lexer.yybegin(4);
    trophyParser.m_lexer.yyin = trophyInput.GetStream();
    if (trophyParser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing Monster Book Kill Table file failed after line[%d]",
        //     L"MonsterBook.cpp",
        //     0x49,
        //     trophyParser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing Monster Book Kill Table file failed after line[%d]",
        //     L"MonsterBook.cpp",
        //     0x49,
        //     trophyParser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Cannot parse monsterbook_trophy.txt at script line %lld; registered data is retained and card loading "
            "continues.",
            static_cast<long long>(trophyParser.m_lexer.line) + 1
        );
    }

    CSecureStream cardInput(kCardPath, true);
    if (cardInput.fail()) {
        L2_LOG_ERROR(Log, "Cannot read monsterbook.txt; Monster Book loading stops with registered trophy data retained.");
        return;
    }

    CParserForMonsterBook cardParser;
    if (!cardParser.Create(cardInput.GetRawString() + 1, cardInput.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] objectParser.create failed",
        //     L"MonsterBook.cpp",
        //     0x57)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] objectParser.create failed",
        //     L"MonsterBook.cpp",
        //     0x57)
        L2_LOG_ERROR(
            Log, "Cannot initialize monsterbook.txt parser; Monster Book loading stops with registered trophy data retained."
        );
        return;
    }
    cardParser.m_lexer.yybegin(4);
    cardParser.m_lexer.yyin = cardInput.GetStream();
    if (cardParser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing Monster Book file failed after line[%d]",
        //     L"MonsterBook.cpp",
        //     0x62,
        //     cardParser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing Monster Book file failed after line[%d]",
        //     L"MonsterBook.cpp",
        //     0x62,
        //     cardParser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Cannot parse monsterbook.txt at script line %lld; registered data is retained and NPC/card validation "
            "continues.",
            static_cast<long long>(cardParser.m_lexer.line) + 1
        );
    }

    if (!RegisterNPCsOnMonsterBook() || !ValidateIndexAndCount()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][LoadScript] invalid additional job")
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook][LoadScript] invalid additional job")
        L2_LOG_ERROR(Log, "Monster Book NPC registration or card validation failed; initiating a crash report.");
        CrashImpl();
    }
}

UserRewardPerFaction::UserRewardPerFaction() : m_bNeedSendToClient(false), m_bNeedUpdate{}, m_nRewardCount{} {}

void UserRewardPerFaction::PrepareSendRewardCandidateOnFactionUI(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();

    for (std::size_t index = 0; index < m_bNeedUpdate.size(); ++index) {
        if (m_bNeedUpdate[index]) {
            m_bNeedUpdate[index] = false;
            packet.Assemble("cc", static_cast<int>(index + 1), m_nRewardCount[index] > 0);
        }
    }
}

UserMonsterBook::BookData::BookData() :
    m_card(),
    m_killLock(),
    m_killMap(),
    m_bNeedUpdateToDB(false),
    m_bLoadCompleteFromDB(false),
    m_nNeedCardMerge(0) {}

UserMonsterBook::BookData::~BookData() = default;

UserMonsterBook::BookStateFlags::BookStateFlags() : m_bBookOpend(false), m_bNeedUpdateToClientBookData(false) {}

UserMonsterBook::RewardRelatedFlags::RewardRelatedFlags() : m_bLevelChanged(false) {}

UserMonsterBook::RewardRelatedFlags::~RewardRelatedFlags() = default;

UserMonsterBook::UserMonsterBook() = default;

UserMonsterBook::~UserMonsterBook() = default;

void UserMonsterBook::WriteLogForMonsterBookState(
    User& user,
    const int cardIndex,
    const int previousKillCount,
    const int previousLevel,
    const int currentKillCount,
    const int currentLevel
) {
    // Current L2Server 0x0043be80.
    L2_GUARD_FUNCTION();
    CLogFormatter formatter;
    formatter.WriteLog1(
        0x1fe1, &user, nullptr, nullptr, nullptr, nullptr, cardIndex, previousKillCount, previousLevel, currentKillCount,
        currentLevel, 0, 0
    );
}

void UserMonsterBook::IncrementMonsterCardKillCount(CNPC* npc) {
    // Current L2Server 0x0043e498.
    L2_GUARD_FUNCTION();
    if (npc == nullptr || !npc->IsMonsterBookNPC()) {
        return;
    }

    Script::Card* const card = Singleton<Script>::inst().GetCardByNpcId(npc->GetClassId() - 1000000);
    if (card == nullptr) {
        return;
    }

    const int cardId = card->m_nCardId;
    UserCard& userCard = m_book.m_card[static_cast<std::size_t>(cardId)];
    if (userCard.m_nLevel >= 4 || userCard.m_nKillCount >= card->m_pTrophyTable->GetKillCount(userCard.m_nLevel)) {
        return;
    }

    const int threadId = CThreadManager::GetThreadID();
    CYieldLock& killLock = m_book.m_killLock[static_cast<std::size_t>(threadId)];
    {
        L2_GUARD(killLock);
        auto& killMap = m_book.m_killMap[static_cast<std::size_t>(threadId)];
        const auto [position, inserted] = killMap.emplace(cardId, 1);
        if (!inserted) {
            position->second = runtime::AddInt32Wrapping(position->second, 1);
        }
        runtime::raw_atomic::ExchangeI32(&m_book.m_nNeedCardMerge, 1);
    }
}

bool UserMonsterBook::_MergeMonsterCardsAndUpdateStates(
    User& user, Script::Card* const card, const int cardIndex, const int count
) {
    // Current L2Server 0x0043d460.
    L2_GUARD_FUNCTION();
    UserCard& userCard = m_book.m_card[static_cast<std::size_t>(cardIndex)];
    const int maximumKillCount = card->m_pTrophyTable->GetKillCount(userCard.m_nLevel);
    const int previousLevel = userCard.m_nLevel;
    const int previousKillCount = userCard.m_nKillCount;
    if (previousLevel >= 4 || previousKillCount == maximumKillCount) {
        return false;
    }

    userCard.m_bNeedSendToClient = true;
    userCard.m_bNeedUpdateToDB = true;
    const int mergedKillCount = runtime::AddInt32Wrapping(previousKillCount, count);
    userCard.m_nKillCount = mergedKillCount;
    if (maximumKillCount <= mergedKillCount) {
        userCard.m_nKillCount = maximumKillCount;
        m_rewardFlags.m_bLevelChanged = true;

        const std::uint32_t factionIndex =
            card->m_pFactionInfo == nullptr ? UINT32_MAX : static_cast<std::uint32_t>(card->m_pFactionInfo->m_nFactionId);
        if (factionIndex < 16U) {
            std::int16_t& rewardCount = m_rewardFlags.m_faction.m_nRewardCount[factionIndex];
            if (rewardCount == 0) {
                m_rewardFlags.m_faction.m_bNeedSendToClient = true;
                m_rewardFlags.m_faction.m_bNeedUpdate[factionIndex] = true;
            }
            rewardCount = runtime::AddInt16Wrapping(rewardCount, 1);
        }

        {
            L2_GUARD(m_rewardFlags.m_lock);
            m_rewardFlags.m_mapSysMsg.emplace(card->m_nCardId, userCard.m_nLevel);
        }
    }

    WriteLogForMonsterBookState(user, cardIndex, previousKillCount, previousLevel, userCard.m_nKillCount, userCard.m_nLevel);
    return true;
}

void UserMonsterBook::MergeMonsterBook(User& user) {
    // Current L2Server 0x0043e640.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::LoadI32Relaxed(&m_book.m_nNeedCardMerge) != 1 || m_book.m_lock.IsEntered()) {
        return;
    }

    runtime::raw_atomic::ExchangeI32(&m_book.m_nNeedCardMerge, 0);
    {
        L2_GUARD(m_book.m_lock);
        bool changed = false;
        for (std::size_t index = 0; index < m_book.m_killMap.size(); ++index) {
            CYieldLock& killLock = m_book.m_killLock[index];
            if (killLock.IsEntered()) {
                continue;
            }

            L2_GUARD(killLock);
            BookData::KillMap& killMap = m_book.m_killMap[index];
            for (const auto& entry : killMap) {
                Script::Card* const card = Singleton<Script>::inst().GetCard(entry.first);
                if (card == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[MonsterBook][MergeMonsterBook][%d] invalid card "
                    //     L"index[%d] user[%d][%s]",
                    //     0x1e4,
                    //     entry.first,
                    //     static_cast<int>(user.GetDBID()),
                    //     user.GetName())
                    // L2_LOG_ERROR(Log,
                    //     L"[MonsterBook][MergeMonsterBook][%d] invalid card "
                    //     L"index[%d] user[%d][%s]",
                    //     0x1e4,
                    //     entry.first,
                    //     static_cast<int>(user.GetDBID()),
                    //     user.GetName())
                    // L2_LOG_ERROR(Log,
                    //     "Monster Book merge for character '%s' (DBID %u): card %d is not registered. %d queued kills are
                    //     discarded when this worker queue is cleared.",
                    //     WideToUtf8(std::wstring_view(user.GetName())).c_str(), static_cast<unsigned>(user.GetDBID()),
                    //     entry.first, entry.second)
                    L2_LOG_ERROR(
                        Log,
                        "Monster Book merge for character '%s' (DBID %u): card %d is not registered; %d queued kills are "
                        "discarded when this worker queue is cleared.",
                        Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned>(user.GetDBID()), entry.first, entry.second
                    );
                } else if (_MergeMonsterCardsAndUpdateStates(user, card, entry.first, entry.second)) {
                    changed = true;
                }
            }
            killMap.clear();
        }
        if (changed) {
            m_book.m_bNeedUpdateToDB = true;
            m_client.m_bNeedUpdateToClientBookData = true;
        }
    }
}

void UserMonsterBook::LoadMonsterBook(User& user, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    if (!packet) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][LoadMonsterBook]Where is the packet?")
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook][LoadMonsterBook]Where is the packet?")
        L2_LOG_ERROR(
            Log, "Cannot load Monster Book for character %u: DB reply data is missing.",
            static_cast<unsigned>(user.GetDBID())
        );
        return;
    }
    if (m_book.m_lock.IsEntered()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][LoadMonsterBook]Invalid load order!")
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook][LoadMonsterBook]Invalid load order!")
        L2_LOG_ERROR(
            Log, "Monster Book load skipped for character %u: book lock is already held.",
            static_cast<unsigned>(user.GetDBID())
        );
        return;
    }

    {
        L2_GUARD(m_book.m_lock);

        int cardCount = 0;
        const unsigned char* cursor = ::Disassemble(packet, "d", &cardCount);
        for (int loaded = 0; loaded < cardCount; ++loaded) {
            std::int16_t cardIndex = 0;
            std::int8_t level = 0;
            int killCount = 0;
            cursor = ::Disassemble(cursor, "hcd", &cardIndex, &level, &killCount);

            Script::Card* const scriptCard = ::Singleton<Script>::inst().GetCard(cardIndex);
            if (!scriptCard) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[MonsterBook][LoadMonsterBook]invalid card index[%d]",
                //     static_cast<int>(cardIndex))
                // L2_LOG_ERROR(Log,
                //     L"[MonsterBook][LoadMonsterBook]invalid card index[%d]",
                //     static_cast<int>(cardIndex))
                L2_LOG_ERROR(
                    Log, "Monster Book load for character %u: card index %d is not registered; DB entry skipped.",
                    static_cast<unsigned>(user.GetDBID()), static_cast<int>(cardIndex)
                );
                continue;
            }

            UserCard& userCard = m_book.m_card[static_cast<std::size_t>(cardIndex)];
            userCard.m_bNeedSendToClient = true;
            userCard.m_bNeedUpdateToDB = false;
            userCard.m_nLevel = static_cast<int>(level);
            userCard.m_nKillCount = killCount;

            if (userCard.m_nLevel == 4) {
                if (killCount != 0) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[MonsterBook][LoadMonsterBook]invalid max level "
                    //     L"kill count[%d] card index[%d] user[%s]",
                    //     killCount,
                    //     static_cast<int>(cardIndex),
                    //     user.GetName())
                    // L2_LOG_ERROR(Log,
                    //     L"[MonsterBook][LoadMonsterBook]invalid max level "
                    //     L"kill count[%d] card index[%d] user[%s]",
                    //     killCount,
                    //     static_cast<int>(cardIndex),
                    //     user.GetName())
                    // L2_LOG_ERROR(Log,
                    //     "Monster Book load for character '%s': completed card %d has nonzero kill count %d. The loaded
                    //     value is retained.", WideToUtf8(std::wstring_view(user.GetName())).c_str(),
                    //     static_cast<int>(cardIndex), killCount)
                    L2_LOG_ERROR(
                        Log,
                        "Monster Book load for character '%s': completed card %d has nonzero kill count %d; the loaded "
                        "value is retained.",
                        Utf16ToUtf8(user.GetName()).c_str(), static_cast<int>(cardIndex), killCount
                    );
                }
                continue;
            }

            const int maximumKillCount = scriptCard->m_pTrophyTable->GetKillCount(userCard.m_nLevel);
            if (userCard.m_nLevel < 4 && maximumKillCount <= killCount) {
                if (maximumKillCount < killCount) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[MonsterBook][LoadMonsterBook]too many kill "
                    //     L"count[%d] card index[%d] user[%s]",
                    //     killCount,
                    //     static_cast<int>(cardIndex),
                    //     user.GetName())
                    // L2_LOG_ERROR(Log,
                    //     L"[MonsterBook][LoadMonsterBook]too many kill "
                    //     L"count[%d] card index[%d] user[%s]",
                    //     killCount,
                    //     static_cast<int>(cardIndex),
                    //     user.GetName())
                    // L2_LOG_ERROR(Log,
                    //     "Monster Book load for character '%s': card %d kill count %d exceeds threshold %d at level %d.
                    //     clamping to the threshold.", WideToUtf8(std::wstring_view(user.GetName())).c_str(),
                    //     static_cast<int>(cardIndex), killCount, maximumKillCount, userCard.m_nLevel)
                    L2_LOG_ERROR(
                        Log,
                        "Monster Book load for character '%s': card %d kill count %d exceeds threshold %d at level %d; "
                        "clamping to the threshold.",
                        Utf16ToUtf8(user.GetName()).c_str(), static_cast<int>(cardIndex), killCount, maximumKillCount,
                        userCard.m_nLevel
                    );
                }
                userCard.m_nKillCount = maximumKillCount;
                m_rewardFlags.m_bLevelChanged = true;

                const int factionIndex = scriptCard->m_pFactionInfo ? scriptCard->m_pFactionInfo->m_nFactionId : -1;
                if (static_cast<unsigned int>(factionIndex) < 16U) {
                    std::int16_t& rewardCount =
                        m_rewardFlags.m_faction.m_nRewardCount[static_cast<std::size_t>(factionIndex)];
                    if (rewardCount == 0) {
                        m_rewardFlags.m_faction.m_bNeedSendToClient = true;
                        m_rewardFlags.m_faction.m_bNeedUpdate[static_cast<std::size_t>(factionIndex)] = true;
                    }
                    rewardCount = runtime::AddInt16Wrapping(rewardCount, 1);
                }
            } else if (userCard.m_nLevel < 0 || userCard.m_nLevel > 4) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[MonsterBook][LoadMonsterBook]kill lev[%d] problem, "
                //     L"card index[%d] user[%s]",
                //     userCard.m_nLevel,
                //     static_cast<int>(cardIndex),
                //     user.GetName())
                // L2_LOG_ERROR(Log,
                //     L"[MonsterBook][LoadMonsterBook]kill lev[%d] problem, "
                //     L"card index[%d] user[%s]",
                //     userCard.m_nLevel,
                //     static_cast<int>(cardIndex),
                //     user.GetName())
                // L2_LOG_ERROR(Log,
                //     "Monster Book load for character '%s': card %d has invalid level %d (allowed 0..4). The loaded state
                //     is retained.", WideToUtf8(std::wstring_view(user.GetName())).c_str(), static_cast<int>(cardIndex),
                //     userCard.m_nLevel)
                L2_LOG_ERROR(
                    Log,
                    "Monster Book load for character '%s': card %d has invalid level %d (allowed 0..4); the loaded state is "
                    "retained.",
                    Utf16ToUtf8(user.GetName()).c_str(), static_cast<int>(cardIndex), userCard.m_nLevel
                );
            }
        }

        m_book.m_bNeedUpdateToDB = false;
        m_book.m_bLoadCompleteFromDB = true;
        m_client.m_bNeedUpdateToClientBookData = true;
    }
}

void UserMonsterBook::_PrepareUpdateMonsterBookDB(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();

    for (int index = 1; index < 400; ++index) {
        UserCard& card = m_book.m_card[static_cast<std::size_t>(index)];
        if (card.m_bNeedUpdateToDB) {
            packet.Assemble(
                "hcd", static_cast<int>(static_cast<std::int16_t>(index)),
                static_cast<int>(static_cast<std::int8_t>(card.m_nLevel)), card.m_nKillCount
            );
            card.m_bNeedUpdateToDB = false;
            if (packet.GetLength() > 0x4f9b) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[PrepareUpdateMonsterBookDB] buffer size overflow! "
                //     L"count[%d]",
                //     packet.GetAssembledCount())
                // L2_LOG_ERROR(Log,
                //     L"[PrepareUpdateMonsterBookDB] buffer size overflow! "
                //     L"count[%d]",
                //     packet.GetAssembledCount())
                L2_LOG_ERROR(
                    Log,
                    "Monster Book DB update preparation stopped at card %d: packet length %d exceeds 20379 bytes (%d "
                    "entries assembled); processed update flags have been cleared.",
                    index, packet.GetLength(), packet.GetAssembledCount()
                );
                break;
            }
        }
    }
}

void UserMonsterBook::UpdateMonsterBookToDB(User& user) {
    // Current L2Server 0x0043c81c.
    L2_GUARD_FUNCTION();
    if (!m_book.m_bNeedUpdateToDB) {
        return;
    }

    std::optional<CPacketHelperEx<20480>> packetStorage;
    {
        L2_GUARD(m_book.m_lock);
        auto& packet = packetStorage.emplace();
        _PrepareUpdateMonsterBookDB(packet);
        const int count = packet.GetAssembledCount();
        if (count > 0 && count < 401) {
            user.SendDB("chddb", 0xea, 0x1b2, user.GetDBID(), count, packet.GetLength(), packet.GetPacket());
            m_book.m_bNeedUpdateToDB = false;
        } else if (count > 400) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[UpdateMonsterBookToDB] buffer size overflow! count[%d] "
            //     L"user[%d]",
            //     count,
            //     user.GetDBID())
            // L2_LOG_ERROR(Log,
            //     L"[UpdateMonsterBookToDB] buffer size overflow! count[%d] "
            //     L"user[%d]",
            //     count,
            //     user.GetDBID())
            L2_LOG_ERROR(
                Log,
                "Monster Book DB update not sent for character %u: assembled entry count %d exceeds 400; the book remains "
                "marked for DB update.",
                static_cast<unsigned>(user.GetDBID()), count
            );
        }
    }
}

void UserMonsterBook::_PrepareSendMonsterBookData(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();

    for (int index = 1; index < 400; ++index) {
        UserCard& card = m_book.m_card[static_cast<std::size_t>(index)];
        if (card.m_bNeedSendToClient) {
            packet.Assemble(
                "hcd", static_cast<int>(static_cast<std::int16_t>(index)),
                static_cast<int>(static_cast<std::int8_t>(card.m_nLevel)), card.m_nKillCount
            );
            card.m_bNeedSendToClient = false;
        }
    }
}

void UserMonsterBook::_PrepareSendMonsterBookDataForDebug(CUserSocket* const socket, CPacketHelperEx<20480>& packet) {
    // Current L2Server 0x0043c268.
    L2_GUARD_FUNCTION();
    std::array<char16_t, 256> text{};
    for (int index = 1; index < 400; ++index) {
        UserCard& card = m_book.m_card[static_cast<std::size_t>(index)];
        if (!card.m_bNeedSendToClient) {
            continue;
        }
        packet.Assemble(
            "hcd", static_cast<int>(static_cast<std::int16_t>(index)),
            static_cast<int>(static_cast<std::int8_t>(card.m_nLevel)), card.m_nKillCount
        );
        card.m_bNeedSendToClient = false;
        FormatUtf16To(text.data(), text.size(), u"cardid[%d] lev[%d] killCnt[%d]", index, card.m_nLevel, card.m_nKillCount);
        if (socket != nullptr) {
            socket->SendSystemMessage(u"SYS", text.data());
        }
    }
}

void UserMonsterBook::SendMonsterBook(CUserSocket* const socket, const bool debug) {
    // Current L2Server 0x0043c548.
    L2_GUARD_FUNCTION();
    if (socket == nullptr || socket == NullUserSocket::GetNullUserSocket() || !m_client.m_bBookOpend ||
        !m_client.m_bNeedUpdateToClientBookData) {
        return;
    }

    std::optional<CPacketHelperEx<20480>> packetStorage;
    {
        L2_GUARD(m_book.m_lock);
        auto& packet = packetStorage.emplace();
        if (debug) {
            _PrepareSendMonsterBookDataForDebug(socket, packet);
        } else {
            _PrepareSendMonsterBookData(packet);
        }
        socket->Send("chhb", 0xfe, 0x1bd, packet.GetAssembledCount(), packet.GetLength(), packet.GetPacket());
        m_client.m_bNeedUpdateToClientBookData = false;

        if (!debug) {
            User* const rawUser = socket->GetUser();
            if (rawUser == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[MonsterBook]Send Success but Where is user?")
                // L2_LOG_ERROR(Log,
                //     L"[MonsterBook]Send Success but Where is user?")
                L2_LOG_ERROR(
                    Log,
                    "Monster Book send was requested, but the socket has no player afterward; the pending client-update "
                    "flag has been cleared."
                );
            } else {
                CSPointer<User> retained(rawUser, ObjectRefType::FunctionChain);
            }
        }
    }
}

void UserMonsterBook::SendMonsterBookNoti(CUserSocket* const socket) {
    // Current L2Server 0x0043cec4.
    L2_GUARD_FUNCTION();
    if (socket == nullptr || socket == NullUserSocket::GetNullUserSocket()) {
        return;
    }

    User* const rawUser = socket->GetUser();
    if (rawUser == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][SendMonsterBookNoti]Where is User?")
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook][SendMonsterBookNoti]Where is User?")
        L2_LOG_ERROR(Log, "Monster Book notifications skipped: socket has no player.");
        return;
    }
    CSPointer<User> retained(rawUser, ObjectRefType::FunctionChain);

    if (m_rewardFlags.m_bLevelChanged) {
        m_rewardFlags.m_bLevelChanged = false;
        socket->Send("ch", 0xfe, 0x1be);
    }
    if (m_rewardFlags.m_faction.m_bNeedSendToClient) {
        CPacketHelperEx<20480> packet;
        m_rewardFlags.m_faction.PrepareSendRewardCandidateOnFactionUI(packet);
        if (packet.GetAssembledCount() != 0) {
            socket->Send("chcb", 0xfe, 0x1bf, packet.GetAssembledCount(), packet.GetLength(), packet.GetPacket());
        }
        m_rewardFlags.m_faction.m_bNeedSendToClient = false;
    }

    {
        L2_GUARD(m_rewardFlags.m_lock);
        for (const auto& message : m_rewardFlags.m_mapSysMsg) {
            socket->Send(
                "chcchch", 0x62, SystemMessageId::MONSTER_COLLECTION_STAGE_OBJECTIVES_COMPLETED, 2, 0x17,
                static_cast<int>(static_cast<std::int16_t>(message.first)), 0x15,
                static_cast<int>(runtime::AddInt16Wrapping(static_cast<std::int16_t>(message.second), 1))
            );
        }
        m_rewardFlags.m_mapSysMsg.clear();
    }
}

void UserMonsterBook::UpdateMonsterCardForBuilder(User& user, const int cardId, int level, int killCount) {
    // Current L2Server 0x0043ec3c.
    L2_GUARD_FUNCTION();
    if (level > 4) {
        level = 4;
        killCount = 0;
    }

    Script::Card* const card = Singleton<Script>::inst().GetCard(cardId);
    if (card != nullptr) {
        {
            L2_GUARD(m_book.m_lock);
            UserCard& userCard = m_book.m_card[static_cast<std::size_t>(cardId)];
            userCard.m_bNeedSendToClient = true;
            userCard.m_bNeedUpdateToDB = true;

            const auto factionIndex = [card]() -> std::uint32_t {
                Script::FactionInfo* const faction = card->m_pFactionInfo;
                return faction == nullptr ? 0xffffffffU : static_cast<std::uint32_t>(faction->m_nFactionId);
            };
            const auto subtractFactionReward = [this, &factionIndex]() {
                const std::uint32_t index = factionIndex();
                if (index >= 16U) {
                    return;
                }
                std::int16_t& count = m_rewardFlags.m_faction.m_nRewardCount[index];
                count = runtime::SubtractInt16Wrapping(count, 1);
                if (count < 1) {
                    count = 0;
                    m_rewardFlags.m_faction.m_bNeedSendToClient = true;
                    m_rewardFlags.m_faction.m_bNeedUpdate[index] = true;
                }
            };
            const auto addFactionReward = [this, &factionIndex]() {
                const std::uint32_t index = factionIndex();
                if (index >= 16U) {
                    return;
                }
                std::int16_t& count = m_rewardFlags.m_faction.m_nRewardCount[index];
                if (count == 0) {
                    m_rewardFlags.m_faction.m_bNeedSendToClient = true;
                    m_rewardFlags.m_faction.m_bNeedUpdate[index] = true;
                }
                count = runtime::AddInt16Wrapping(count, 1);
            };

            if (userCard.m_nLevel < 4 && userCard.m_nKillCount == card->m_pTrophyTable->GetKillCount(userCard.m_nLevel)) {
                subtractFactionReward();
            }
            for (int index = 0; index < userCard.m_nLevel; ++index) {
                subtractFactionReward();
            }

            userCard.m_nLevel = level;
            userCard.m_nKillCount = killCount;
            if (level < 4 && userCard.m_nKillCount == card->m_pTrophyTable->GetKillCount(level)) {
                addFactionReward();
            }
            for (int index = 0; index < userCard.m_nLevel; ++index) {
                addFactionReward();
            }

            m_client.m_bNeedUpdateToClientBookData = true;
            m_book.m_bNeedUpdateToDB = true;
        }
    }

    UpdateMonsterBookToDB(user);
    m_client.m_bBookOpend = true;
    SendMonsterBook(user.GetSocket(), true);
    m_client.m_bBookOpend = false;
}

UserCard& UserMonsterBook::GetCard(int nIndex) {
    L2_GUARD_FUNCTION();

    if (UserCard::CheckIndex(nIndex)) {
        return m_book.m_card[static_cast<std::size_t>(nIndex)];
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[MonsterBook][GetCard] invalid card index[%d]",
    //     nIndex)
    // L2_LOG_ERROR(Log,
    //     L"[MonsterBook][GetCard] invalid card index[%d]",
    //     nIndex)
    L2_LOG_ERROR(Log, "Monster Book card index %d is outside 1..399; returning fallback card slot 0.", nIndex);
    return m_book.m_card[0];
}

void UserMonsterBook::OnRewardFinished(User& user, int cardId, int factionIndex) {
    L2_GUARD_FUNCTION();

    if (!UserCard::CheckIndex(cardId) || static_cast<std::uint32_t>(factionIndex) >= 16U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][OnRewardFinished]invalid index[%d][%d]",
        //     cardId,
        //     factionIndex)
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook][OnRewardFinished]invalid index[%d][%d]",
        //     cardId,
        //     factionIndex)
        L2_LOG_ERROR(
            Log,
            "Monster Book reward completion ignored for character %u: card index %d must be 1..399 and faction index %d "
            "must be 0..15.",
            static_cast<unsigned>(user.GetDBID()), cardId, factionIndex
        );
        return;
    }

    {
        L2_GUARD(m_book.m_lock);
        UserCard& card = m_book.m_card[static_cast<std::size_t>(cardId)];
        card.m_bNeedSendToClient = true;
        card.m_bNeedUpdateToDB = true;
        card.m_nKillCount = 0;
        card.m_nLevel = runtime::AddInt32Wrapping(card.m_nLevel, 1);

        if (card.m_nLevel < 5) {
            std::int16_t& rewardCount = m_rewardFlags.m_faction.m_nRewardCount[static_cast<std::size_t>(factionIndex)];
            rewardCount = runtime::SubtractInt16Wrapping(rewardCount, 1);
            if (rewardCount < 1) {
                rewardCount = 0;
                m_rewardFlags.m_faction.m_bNeedSendToClient = true;
                m_rewardFlags.m_faction.m_bNeedUpdate[static_cast<std::size_t>(factionIndex)] = true;
            }
            m_client.m_bNeedUpdateToClientBookData = true;
            m_book.m_bNeedUpdateToDB = true;

            if (card.m_nLevel == 4) {
                user.GetSocket()->Send(
                    "chcch", 0x62, SystemMessageId::ALL_MONSTER_COLLECTION_STAGES_COMPLETED, 1, 0x17,
                    static_cast<int>(static_cast<std::int16_t>(cardId))
                );
            }

            CLogFormatter formatter;
            formatter.WriteLog1(
                0x1fe3, &user, nullptr, nullptr, nullptr, nullptr, cardId, runtime::AddInt32Wrapping(card.m_nLevel, -1),
                card.m_nLevel, 0, 0, 0, 0
            );
            return;
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][OnRewardFinished]invalid card[%d] "
        //     L"trophy lev[%d] user[%d][%s]",
        //     cardId,
        //     card.m_nLevel,
        //     runtime::Int32FromBits(user.GetDBID()),
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook][OnRewardFinished]invalid card[%d] "
        //     L"trophy lev[%d] user[%d][%s]",
        //     cardId,
        //     card.m_nLevel,
        //     runtime::Int32FromBits(user.GetDBID()),
        //     user.GetName())
        // L2_LOG_ERROR(Log,
        //     "Monster Book reward completion for character '%s' (DBID %u), card %d produced invalid level %d. The
        //     incremented level and cleared kill count remain.", WideToUtf8(std::wstring_view(user.GetName())).c_str(),
        //     static_cast<unsigned>(user.GetDBID()), cardId, card.m_nLevel)
        L2_LOG_ERROR(
            Log,
            "Monster Book reward completion for character '%s' (DBID %u), card %d produced invalid level %d; the "
            "incremented level and cleared kill count remain.",
            Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned>(user.GetDBID()), cardId, card.m_nLevel
        );
    }
}

void UserMonsterBook::OnUserLeaveWorldForRaid(CUserSocket* socket) {
    // Current L2Server 0x0043baac.
    L2_GUARD_FUNCTION();
    if (socket == nullptr || socket == NullUserSocket::GetNullUserSocket()) {
        return;
    }

    socket->Send("ch", 0xfe, 0x1c1);
    // Close the book on the client. Keep pending database updates.
    m_client.m_bBookOpend = false;
}

void UserMonsterBook::OnUserEnterWorld(User& user) {
    // Current L2Server 0x0043bb80.
    L2_GUARD_FUNCTION();
    if (!m_book.m_bLoadCompleteFromDB) {
        db.RequestLoadUserMonsterBook(user);
    }
    m_client.m_bBookOpend = false;
}

void UserMonsterBook::OnRequestOpenBook(User& user, CUserSocket* socket) {
    // Current L2Server 0x0043bc3c.
    L2_GUARD_FUNCTION();
    if (socket == nullptr || socket == NullUserSocket::GetNullUserSocket()) {
        return;
    }

    // int result = 0.
    int result = MORT_FAIL;
    if (!m_client.m_bBookOpend) {
        m_client.m_bBookOpend = true;
        // result = 1.
        result = MORT_SUCCESS;
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][OnRequestOpenBook] book already opend! "
        //     L"userid[%d]",
        //     user.GetServerId())
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook][OnRequestOpenBook] book already opend! "
        //     L"userid[%d]",
        //     user.GetServerId())
        L2_LOG_ERROR(
            Log,
            "Monster Book open request for player object %u arrived while already open; closing book state and replying "
            "with result 0.",
            static_cast<unsigned>(user.GetServerId())
        );
        m_client.m_bBookOpend = false;
    }
    socket->Send("chc", 0xfe, 0x1c0, result);
}

void UserMonsterBook::OnRequestCloseBook(
    // User& user,
    [[maybe_unused]] User& user,
    CUserSocket* socket
) {
    // Current L2Server 0x0043bd8c.
    L2_GUARD_FUNCTION();
    if (socket == nullptr || socket == NullUserSocket::GetNullUserSocket()) {
        return;
    }

    if (!m_client.m_bBookOpend) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][OnRequestCloseBook] book already closed! "
        //     L"userid[%d]",
        //     user.GetServerId())
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook][OnRequestCloseBook] book already closed! "
        //     L"userid[%d]",
        //     user.GetServerId())
        // Noise: Repeated close requests leave the book closed. No additional work or failure follows.
    }
    m_client.m_bBookOpend = false;
}

bool UserMonsterBook::OnCheckMonsterBookFactionTrophyQuest(const int factionId, const int level, const int value) {
    // Current L2Server.exe 0x0043ef10.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(factionId) >= 16U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[MonsterBook][Quest]invalid faction index![%d]",
        //     factionId)
        // L2_LOG_ERROR(Log,
        //     L"[MonsterBook][Quest]invalid faction index![%d]",
        //     factionId)
        L2_LOG_ERROR(
            Log,
            "Monster Book faction quest check rejected: faction index %d is outside 0..15 (required level %d, card count "
            "%d).",
            factionId, level, value
        );
        return false;
    }

    std::vector<int>& cardIds = Script::inst().GetCardIdListByFactionId(factionId);
    int matchedCardCount = 0;
    for (const int cardId : cardIds) {
        if (level <= m_book.m_card[static_cast<std::size_t>(cardId)].m_nLevel) {
            ++matchedCardCount;
            if (value <= matchedCardCount) {
                return true;
            }
        }
    }
    return false;
}

} // namespace MonsterBook
