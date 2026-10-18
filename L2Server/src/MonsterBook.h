#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

template <std::size_t Capacity>
class CPacketHelperEx;
struct ItemAmountData;
class User;
class CUserSocket;
class CNPC;
class CParserForMonsterBook;

namespace MonsterBook {


class Script : public ::Singleton<Script> {
public:
    class TrophyTable {
    public:
        TrophyTable() : m_nKillData{} {}

        int GetKillCount(int level);

    private:
        friend class ::CParserForMonsterBook;

        std::array<int, 4> m_nKillData;
    };

    class FactionInfo {
    public:
        int m_nFactionId;
        int m_nRewardLevelBound;
    };

    class Card {
    public:
        struct RewardData {
            std::array<std::int64_t, 4> m_nRewardFP;
            std::array<std::int64_t, 4> m_nRewardEXP;
            std::array<std::int64_t, 4> m_nRewardSP;
            std::array<std::vector<ItemAmountData>, 4> m_vItem;
        };

        Card();

        void AddFactionPoints(const std::vector<std::int64_t>& points);
        void AddExpPoints(const std::vector<std::int64_t>& points);
        void AddSPPoints(const std::vector<std::int64_t>& points);
        void AddNPCIdSet(const std::vector<std::int64_t>& npcIds);
        void AddItems(int level, const std::vector<ItemAmountData>& items);

        int m_nCardId;
        FactionInfo* m_pFactionInfo;
        std::array<int, 10> m_nNpcIdSet;
        TrophyTable* m_pTrophyTable;
        RewardData m_reward;
    };

    Script();
    ~Script() override;

    Card* GetCard(int cardId);
    Card* GetCardByNpcId(int npcId);
    std::vector<int>& GetCardIdListByFactionId(int factionId);
    FactionInfo* GetFactionInfo(int factionId);
    TrophyTable* GetTrophyTable(const char16_t* tableName);
    void AddCard(Card* card);
    void AddKillTable(const std::u16string& tableName, TrophyTable* table);
    void AddFactionInfo(int factionId, FactionInfo* factionInfo);
    void LoadScript();

private:
    bool RegisterNPCsOnMonsterBook();
    bool ValidateIndexAndCount();

    using CardMap = ::std::map<int, Card*>;
    using TrophyTableMap = ::std::map<std::u16string, TrophyTable*, runtime::CStringLess<std::u16string>>;
    using FactionMap = ::std::map<int, FactionInfo*>;

    CardMap m_mapCard;
    CardMap m_mapCardByNpcId;
    TrophyTableMap m_mapKillTable;
    FactionMap m_mapFaction;
    int m_nMaxCardCount;
    std::array<std::vector<int>, 17> m_vCardList;
};


struct UserCard {
    UserCard() : m_bNeedSendToClient(false), m_bNeedUpdateToDB(false), m_nLevel(0), m_nKillCount(0) {}

    static bool CheckIndex(int nIndex) { return nIndex > 0 && nIndex < 400; }

    bool m_bNeedSendToClient;
    bool m_bNeedUpdateToDB;
    int m_nLevel;
    int m_nKillCount;
};

static_assert(std::is_trivially_copyable_v<UserCard>);
static_assert(std::is_trivially_destructible_v<UserCard>);

struct UserRewardPerFaction {
    UserRewardPerFaction();

    void PrepareSendRewardCandidateOnFactionUI(CPacketHelperEx<20480>& packet);

    bool m_bNeedSendToClient;
    std::array<bool, 16> m_bNeedUpdate;
    std::array<std::int16_t, 16> m_nRewardCount;
};


class UserMonsterBook {
public:
    struct BookData {
        using KillMap = ::std::map<int, int>;

        BookData();
        ~BookData();

        ::CYieldLock m_lock;
        std::array<UserCard, 400> m_card;
        std::array<::CYieldLock, 16> m_killLock;
        std::array<KillMap, 16> m_killMap;
        bool m_bNeedUpdateToDB;
        bool m_bLoadCompleteFromDB;
        std::int32_t m_nNeedCardMerge;
    };

    struct BookStateFlags {
        BookStateFlags();

        bool m_bBookOpend;
        bool m_bNeedUpdateToClientBookData;
    };

    struct RewardRelatedFlags {
        using SystemMessageMap = ::std::map<int, int>;

        RewardRelatedFlags();
        ~RewardRelatedFlags();

        bool m_bLevelChanged;
        UserRewardPerFaction m_faction;
        ::CYieldLock m_lock;
        SystemMessageMap m_mapSysMsg;
    };

    UserMonsterBook();
    ~UserMonsterBook();

    void LoadMonsterBook(User& user, const unsigned char* packet);
    void IncrementMonsterCardKillCount(CNPC* npc);
    UserCard& GetCard(int nIndex);
    void OnRewardFinished(User& user, int cardId, int factionIndex);
    void OnRequestOpenBook(User& user, CUserSocket* socket);
    void OnRequestCloseBook(User& user, CUserSocket* socket);
    void MergeMonsterBook(User& user);
    void SendMonsterBook(CUserSocket* socket, bool debug);
    void SendMonsterBookNoti(CUserSocket* socket);
    void UpdateMonsterBookToDB(User& user);
    void OnUserLeaveWorldForRaid(CUserSocket* socket);
    void OnUserEnterWorld(User& user);
    bool OnCheckMonsterBookFactionTrophyQuest(int factionId, int level, int value);

private:
    friend class ::User;

    void UpdateMonsterCardForBuilder(User& user, int cardId, int level, int killCount);

    bool _MergeMonsterCardsAndUpdateStates(User& user, Script::Card* card, int cardIndex, int count);
    void _PrepareUpdateMonsterBookDB(CPacketHelperEx<20480>& packet);
    void _PrepareSendMonsterBookData(CPacketHelperEx<20480>& packet);
    void _PrepareSendMonsterBookDataForDebug(CUserSocket* socket, CPacketHelperEx<20480>& packet);
    void WriteLogForMonsterBookState(
        User& user, int cardIndex, int previousKillCount, int previousLevel, int currentKillCount, int currentLevel
    );

    BookData m_book;
    BookStateFlags m_client;
    RewardRelatedFlags m_rewardFlags;
};


} // namespace MonsterBook
