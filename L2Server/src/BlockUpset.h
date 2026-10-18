#pragma once

#include "PVPMatch.h"

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CSkillInfo;
class User;

template <std::size_t Capacity>
class CPacketHelperEx;

namespace BlockUpsetRewardType {

enum Enum : std::int32_t {
    DEFAULT = 0,
    PCBANG_DEFAULT = 1,
    WIN_DEFAULT = 2,
    WIN = 3,
    LOSE = 4,
    DRAW = 5,
    MAX = 6,
};

} // namespace BlockUpsetRewardType


class CBlockUpset;
class CBlockUpsetManager;

class CBlockUpsetMemberInfo final : public CPVPMatchMemberInfo {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CBlockUpsetMemberInfo)

    CBlockUpsetMemberInfo(
        CBlockUpset& blockUpset,
        CBlockUpsetManager& manager,
        BlueRedTeam::Enum team,
        std::uint32_t userId,
        const char16_t* userName
    );
    ~CBlockUpsetMemberInfo() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    int GetPoint() const override;

    CBlockUpset& m_BlockUpset;
    CBlockUpsetManager& m_BlockUpsetManager;
    std::int32_t m_Point;
    bool m_bVoted;
    std::array<std::uint8_t, 3> m_targetPadding65;
};

class CBlockUpset final : public CPVPMatch {
public:
    struct BlockUpsetPoint {
        BlockUpsetPoint();
        void Init();

        std::int32_t m_Point;
    };

    struct ReleaseBlockUpsetInfo {
        void operator()(CPVPMatchMemberInfo* memberInfo);
        void Do(CBlockUpsetMemberInfo& memberInfo);
    };

    struct BanishCombatBlockUpsetUser {
        void operator()(CPVPMatchMemberInfo* memberInfo);
        void Do(CBlockUpsetMemberInfo& memberInfo);
    };

    struct OnCombatStart {
        void operator()(CPVPMatchMemberInfo* memberInfo);
        void Do(CBlockUpsetMemberInfo& memberInfo);
    };

    using TeamLine = std::list<CPVPMatchMemberInfo*>;

    CBlockUpset();
    ~CBlockUpset() override;

    PVPMatchType::Enum GetPVPMatchType() const override;
    void InitBlockUpset(int index, CBlockUpsetManager& manager);
    void InitSetting() override;
    void OnNpcServerClosed() override;
    void RemoveAllBlockUpsetWaitingPlayers();
    void _RemoveAllBlockUpsetWaitingPlayers();
    void RemoveAllBlockUpsetCombatPlayers();
    void _RemoveAllBlockUpsetCombatPlayers();
    void CheckLogoutPlayers(UserDB& users);
    void _CheckLogoutPlayers(UserDB& users);
    void RegisterManagerNpcId(int npcId) override;
    void TimerExpired(int id) override;
    void ShowBlockUpsetInfo(User& user);
    void ShowBlockUpsetState(User& user);
    void AddBlockUpsetNpc(int npcId);
    void _AddBlockUpsetNpc(int npcId);
    bool _RemoveBlockUpsetNpcFromGround(int npcId);
    bool _InsertBlockUpsetNpcToGround(int npcId);
    void GetNpcBlockCounts(int& waiting, int& combat) const;
    void ShowWaitingNpcBlock(bool show);
    void _ShowWaitingNpcBlock(bool show);
    void ChangeBlockUpsetNpcAmount(int amount);
    bool CanAddBlockUpsetWaitingPlayer(User& user);
    bool EnterBlockUpsetPlayer(User& user);
    bool AddBlockUpsetWaitingPlayer(BlueRedTeam::Enum team, User& user);
    bool _AddBlockUpsetWaitingPlayer(BlueRedTeam::Enum team, User& user);
    bool RemoveBlockUpsetWaitingPlayer(int userId);
    bool _RemoveBlockUpsetWaitingPlayer(int userId);
    void _RemoveBlockUpsetWaitingPlayerInfo(CBlockUpsetMemberInfo* memberInfo);
    bool ChangeBlockUpsetWaitingPlayerTeam(int userId);
    bool _ChangeBlockUpsetWaitingPlayerTeam(CBlockUpsetMemberInfo* memberInfo);
    bool VoteToStart(int userId, bool start);
    void ResetVoteToStart();
    bool _StartVoting();
    bool RemoveBlockUpsetCombatPlayer(int userId);
    bool _RemoveBlockUpsetCombatPlayer(int userId);
    void _RemoveBlockUpsetCombatPlayerInfo(CBlockUpsetMemberInfo* memberInfo);
    void _OnBlockUpsetPlayerAdded(CBlockUpsetMemberInfo* memberInfo);
    bool IsBlockUpsetWaitingPlayer(int userId);
    bool IsBlockUpsetCombatPlayer(int userId);
    bool _IsBlockUpsetCombatPlayer(int userId);
    int GetRelation(User& user, User& target) override;
    bool LeaveWorld(User& user);
    void GetBlockUpsetPlayerCount(int& blueWaiting, int& redWaiting, int& blueCombat, int& redCombat) const;
    bool _CheckStartBlockUpsetCombat(int seconds);
    void ReadyPlayerToStart(BlueRedTeam::Enum team, User& user);
    BlueRedTeam::Enum GetSmallTeam() const;
    int AddBlockUpsetPoint(User& user, int point);
    int GetBlockUpsetPoint(BlueRedTeam::Enum team) const;
    bool ChangeBlockColor(int userId, int color);
    bool OnTeamScoreChanged(int team, int score);
    bool SetBlockUpsetWinner(BlueRedTeam::Enum team);
    void _StartCombat();
    void _OnRegister() override;
    void _OnCombat() override;
    void _OnReward() override;
    void _SortByPoint();
    void _BalanceTeam();
    bool SetCombatStartingRemainSec(int seconds);
    int GetCombatStartingRemainSec() const;
    int GetCombatFinishRemainSec() const;
    static BlueRedTeam::Enum GetBlockUpsetWinner(const BlockUpsetPoint* points);
    void SetMaxRegisterCount(int count);
    void SetMinRegisterCount(int count);
    wchar_t* GetName() const override;
    bool IsVoting() const;

private:
    void _MakeBlockUpsetListTotal(CPacketHelperEx<20480>& packet);

    std::int32_t m_Index;
    std::int32_t m_MinRegisterCount;
    std::int32_t m_MaxRegisterCount;
    [[maybe_unused]] std::uint32_t m_targetPadding1dc;
    CBlockUpsetManager* m_pManager;
    BlockUpsetPoint m_BlockUpsetPoints[2];
    UserDB m_BlockUpsetWaitingDB;
    TeamLine m_BlockUpsetWaitingLine[2];
    TeamLine* m_BlueWaitingTeam;
    TeamLine* m_RedWaitingTeam;
    std::int32_t m_LastAddedUserId;
    [[maybe_unused]] std::uint32_t m_targetPadding254;
    UserDB m_BlockUpsetCombatDB;
    TeamLine m_BlockUpsetCombatLine[2];
    TeamLine* m_BlueCombatTeam;
    TeamLine* m_RedCombatTeam;
    std::int32_t m_CombatStartTick;
    bool m_bOnBalancing;
    [[maybe_unused]] std::array<std::uint8_t, 3> m_targetPadding2bd;
    std::int32_t m_VoteTryTime;
    bool m_Voting;
    [[maybe_unused]] std::array<std::uint8_t, 3> m_targetPadding2c5;
    std::int32_t m_VoteToStart;
    std::int32_t m_VoteToWait;
    std::vector<std::int32_t> m_WaitingNpcBlock;
    std::set<std::int32_t> m_CombatNpcBlock;
};

class CBlockUpsetManager final : public CIOObject, public Singleton<CBlockUpsetManager> {
public:
    static constexpr int MAX_BLOCK_GAME = 4;

    struct WinLoseSetting {
        WinLoseSetting();
        void Init();

        std::int32_t m_RewardItemClassId;
        std::uint32_t m_targetPadding4;
        std::int64_t m_RewardItemCount;
        std::int64_t m_RewardStep;
    };

    struct RewardHandler {
        RewardHandler(CBlockUpsetManager* manager, PVPMatchGameResult::Enum result, int totalPlayer);
        void operator()(CPVPMatchMemberInfo* memberInfo);
        static void GiveItem(User& user, WinLoseSetting& setting);

        CBlockUpsetManager* m_pManager;
        const PVPMatchGameResult::Enum m_Result;
        const std::int32_t m_TotalPlayer;
    };

    CBlockUpsetManager();
    ~CBlockUpsetManager() override;

    void InitBlockUpsetManager();
    void InitSetting();
    void OnNpcServerClosed();
    void RemoveAllBlockUpsetWaitingPlayers();
    void RemoveAllBlockUpsetCombatPlayers();
    static bool IsInGameRange(int stage);
    void TimerExpired(int id) override;
    void ShowBlockUpsetInfo(User& user);
    void ShowBlockUpsetState(int stage, User& user);
    void RegisterManagerNpcId(int stage, int npcId);
    bool AddBlockUpsetNpc(int stage, int npcId);
    void ShowWaitingNpcBlock(bool show);
    void ChangeBlockUpsetNpcAmount(int stage, int amount);
    bool CanAddBlockUpsetWaitingPlayer(int stage, User& user);
    bool EnterBlockUpsetPlayer(int stage, User& user);
    bool AddBlockUpsetWaitingPlayer(int stage, BlueRedTeam::Enum team, User& user);
    bool RemoveBlockUpsetWaitingPlayer(int userId);
    bool RemoveBlockUpsetWaitingPlayer(int stage, int userId);
    bool RemoveBlockUpsetCombatPlayer(int userId);
    bool VoteToStart(int stage, int userId, bool start);
    int IsBlockUpsetWaitingPlayer(int userId);
    int IsBlockUpsetCombatPlayer(int userId);
    int GetRelation(User& user, User& target);
    bool IsEnemyTo(User& user, User& target);
    bool LeaveWorld(User& user);
    int AddBlockUpsetPoint(User& user, int point);
    int GetBlockUpsetPoint(int stage, BlueRedTeam::Enum team) const;
    bool ChangeBlockColor(int stage, int userId, int color);
    bool SetBlockUpsetWinner(int stage, BlueRedTeam::Enum team);
    bool SetMinRegisterCount(int stage, int count);
    void SetVoteWaitingSec(int seconds);
    void SetGamePeriod(int seconds);
    void ReadyPlayerToStart(BlueRedTeam::Enum team, User& user);
    CBlockUpset& GetStage(int stage);
    void SetReward(BlockUpsetRewardType::Enum type, int itemClassId, std::int64_t itemCount, std::int64_t step);
    void DeleteItemsAfterMatch(int itemClassId);
    void SetSkills(CSkillInfo* blueEnterSkill, CSkillInfo* redEnterSkill, CSkillInfo* waitingSkill);

    CBlockUpset m_BlockUpset[MAX_BLOCK_GAME];
    std::int32_t m_GamePeriodSec;
    std::uint32_t m_targetPaddingc64;
    WinLoseSetting m_RewardSettting[BlockUpsetRewardType::MAX];
    std::vector<std::int32_t> m_DeleteItemsAfterMatch;
    std::int32_t m_nVoteWaitSec;
    std::int32_t m_nMinimumPoint;
    CSkillInfo* m_pEnterSkill[2];
    CSkillInfo* m_pWaitingSkill;
};
