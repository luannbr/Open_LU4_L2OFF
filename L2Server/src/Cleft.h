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

class CCleft;

class CCleftMemberInfo final : public CPVPMatchMemberInfo {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CCleftMemberInfo)

    CCleftMemberInfo(CCleft* match, BlueRedTeam::Enum team, std::uint32_t userId, const char16_t* userName);
    ~CCleftMemberInfo() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    int GetPoint() const override;
    void MakeStateInfo(IPacketHelper& packet) override;

    CCleft* m_CleftMatch;
    std::uint32_t m_StartTime;
    std::uint32_t m_DieTime;
    std::int32_t m_CleftTowerCount;
    std::int32_t m_nKillCount;
    std::int32_t m_nDeathCount;
    std::int32_t m_nRank;
};

class CCleft final : public CPVPMatch, public Singleton<CCleft> {
public:
    struct CleftPoint {
        CleftPoint();
        void Init();

        std::int32_t m_Point;
        std::int32_t m_CenterTowerPoint;
        std::int32_t m_DefenceTowerPoint;
        std::int32_t m_CatKillPoint;
    };

    struct BanishCleftUser {
        void operator()(CPVPMatchMemberInfo* memberInfo) const;
        void Do(CCleftMemberInfo& memberInfo) const;
    };

    struct ReleaseCleftInfo {
        void operator()(CPVPMatchMemberInfo* memberInfo) const;
        void Do(CCleftMemberInfo& memberInfo) const;
    };

    struct _OnCombatStart {
        explicit _OnCombatStart(bool sendTotalState);
        void operator()(CPVPMatchMemberInfo* memberInfo) const;
        void Do(CCleftMemberInfo& memberInfo) const;

        bool m_bSendTotalState;
    };

    struct CatData {
        using UserIds = std::deque<int>;

        CatData();
        ~CatData();
        void Init();
        void SetNewCat(int userId);

        std::int32_t m_UserId;
        std::uint32_t m_CatSelectedTick;
        UserIds m_NextCat;
    };

    struct WinLoseSetting {
        WinLoseSetting();
        void Init();

        std::int32_t m_RewardItemClassId;
        std::int64_t m_RewardItemCount;
        std::int32_t m_SpBonus;
        std::int32_t m_SpKillBonus;
    };

    struct RewardHandler {
        RewardHandler(WinLoseSetting& setting, PVPMatchGameResult::Enum result, bool ignore15Min);
        void operator()(CPVPMatchMemberInfo* memberInfo) const;

        WinLoseSetting& m_Setting;
        const PVPMatchGameResult::Enum m_Result;
        bool m_bIgnore15Min;
        std::array<std::uint8_t, 3> m_targetPaddingD{};
    };

    using TeamLine = std::list<CPVPMatchMemberInfo*>;

    CCleft();
    ~CCleft() override;

    PVPMatchType::Enum GetPVPMatchType() const override;
    void UserDie(User* user, User* enemy) override;
    void TowerDestroyed(User& user, int towerType, int point);
    void Init() override;
    void InitSetting() override;
    void OnNpcServerClosed() override;
    void CheckDieUser(std::uint32_t tick);
    void TimerExpired(int id) override;
    void ChangeState(PVPMatchState::Enum state) override;
    int GetCombatRemainSec() const;
    bool IsCleftPlayer(int userId);
    bool _IsCleftPlayer(int userId);
    BlueRedTeam::Enum GetSmallTeam() const;
    bool CanAddCleftPlayer(BlueRedTeam::Enum team, User& user);
    bool EnterCleftPlayer(User& user);
    bool AddCleftPlayer(BlueRedTeam::Enum team, User& user);
    bool _AddCleftPlayer(BlueRedTeam::Enum team, User& user);
    bool RemoveCleftPlayer(int userId);
    bool _RemoveCleftPlayer(int userId);
    void _RemoveCleftPlayerInfo(CCleftMemberInfo* memberInfo);
    bool ChangeCleftPlayerTeam(int userId);
    bool _ChangeCleftPlayerTeam(CCleftMemberInfo* memberInfo);
    void RemoveAllCleftPlayers();
    void _RemoveAllCleftPlayers();
    void _RemoveTeamPlayers(BlueRedTeam::Enum team, bool banish);
    void CheckLogoutPlayers();
    void _OnCleftPlayerAdded(CCleftMemberInfo* memberInfo);
    void ReadyPlayerToStart(BlueRedTeam::Enum team, User& user);
    void GetCleftPlayerCount(int& blueCount, int& redCount) const;
    void _OnRegister() override;
    void _OnCombat() override;
    void _OnReward() override;
    void _SortByPoint();
    void BalanceTeam();
    void _BalanceTeam();
    int AddPoint(BlueRedTeam::Enum team, int point);
    void GetPoint(BlueRedTeam::Enum team, CleftPoint& point) const;
    int GetPoint(BlueRedTeam::Enum team) const;
    bool SetCleftWinner(BlueRedTeam::Enum team);
    void SetTimeProperty(int combatTime, int rewardTime, int catPeriod);
    void SetMinRegisterCount(int count);
    void SetTeamBalance(bool enabled);
    void ShowCleftInfo(User& user);
    void ShowCleftState(User& user);
    std::u16string GetStateString(PVPMatchState::Enum state);
    void SetWinnerReward(int itemClassId, std::int64_t itemCount);
    void SetLoserReward(int itemClassId, std::int64_t itemCount);
    void SetWaitingSkill(CSkillInfo* skillInfo);
    void SetSpBonus(int winnerBonus, int loserBonus, int killBonus);
    static BlueRedTeam::Enum GetCleftWinner(const CleftPoint* points);
    bool IsCat(int userId) const;
    int GetTeamMemberCount(BlueRedTeam::Enum team) const;
    bool SelectRandomCat(BlueRedTeam::Enum team);
    void SendInfosToCats(std::vector<User*>& users);
    bool _SelectRandomCat(BlueRedTeam::Enum team, std::vector<User*>& users);
    bool _SelectCat(BlueRedTeam::Enum team, int userId, std::vector<User*>& users);
    void _ResetNextCat(BlueRedTeam::Enum team);
    int GetRelation(User& user, User& target) override;
    bool LeaveWorld(User& user);
    wchar_t* GetName() const override;

private:
    void _MakeCleftListTotal(CPacketHelperEx<20480>& packet);
    void _MakeCleftStateTotal(CPacketHelperEx<20480>& packet);

    std::int32_t m_CombatTime;
    std::int32_t m_RewardTime;
    std::int32_t m_CatPeriod;
    std::int32_t m_MinRegisterCount;
    bool m_TeamBalance;
    CSkillInfo* m_pWaitingSkill;
    std::int32_t m_OldCombatRemainSec;

public:
    bool m_bIgnore15Min;

private:
    CleftPoint m_CleftPoints[2];
    UserDB m_CleftTeamDB;
    TeamLine m_CleftTeamLine[2];
    TeamLine* m_BlueTeam;
    TeamLine* m_RedTeam;

public:
    CatData m_CatData[2];

private:
    WinLoseSetting m_WinSetting;
    WinLoseSetting m_LoseSetting;
};
