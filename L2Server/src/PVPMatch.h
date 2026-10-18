#pragma once

#include "Protocol/src/L2Constants.h"

#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Territory.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>

class CBlockUpsetManager;
class CCleft;
class CCrataeCube;
class CNPC;
class CParty;
class CUndergroundColosseum;
class CUserSocket;
class IPacketHelper;
class User;

// enum PVP_MATCH_TYPE : std::int32_t {
//     PVPMT_MIN = 0,
//     PVPMT_UGC_40 = 0,
//     PVPMT_UGC_50 = 1,
//     PVPMT_UGC_60 = 2,
//     PVPMT_UGC_70 = 3,
//     PVPMT_UGC_UL = 4,
//     PVPMT_COC_70 = 5,
//     PVPMT_COC_76 = 6,
//     PVPMT_COC_80 = 7,
//     PVPMT_UGC_EVENT = 8,
//     PVPMT_MAX = 9,
// }

enum PVPMatchResult : std::int32_t {
    PVPMR_SUCCESS = 0,
    PVPMR_INVALID_MATCH = 1,
    PVPMR_INVALID_PARTY_ID = 2,
    PVPMR_ALREADY_REGISTERED = 3,
    PVPMR_ALREADY_REGISTERED_ANOTHER = 4,
    PVPMR_INVALID_MEMBER_NUM = 5,
    PVPMR_INVALID_MEMBER_LEVEL = 6,
    PVPMR_INVALID_MEMBER_DIST = 7,
    PVPMR_FULL = 8,
    PVPMR_BANNED_PARTY_MEMBER = 9,
    PVPMR_INVALID_USER = 10,
    PVPMR_INVALID_USER_LEVEL = 11,
    PVPMR_INVALID_USER_DIST = 12,
    PVPMR_USER_IN_PARTY = 13,
    PVPMR_USER_PVPMATCH_READY = 14,
    PVPMR_USER_CURSED = 15,
    PVPMR_USER_CHAO = 16,
    PVPMR_UNKNOWN = 17,
};

namespace PVPMatchGameResult {

enum Enum : std::int32_t {
    INVALID = 0,
    WIN = 1,
    LOSE = 2,
    DRAW = 3,
};

} // namespace PVPMatchGameResult

class CPVPMatchMemberInfo : public MemoryObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CPVPMatchMemberInfo)

    struct SetOrder {
        SetOrder();
        void operator()(CPVPMatchMemberInfo* memberInfo);

    private:
        std::int32_t m_nOrder = 0;
        std::int32_t m_nPrevPoint = INT32_MAX;
    };

    CPVPMatchMemberInfo(BlueRedTeam::Enum team, std::uint32_t userId, const char16_t* userName);
    ~CPVPMatchMemberInfo() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    virtual void MakeListInfo(IPacketHelper& packet);
    virtual void MakeStateInfo(IPacketHelper& packet);
    virtual int GetPoint() const;

    static bool SortPlayerByPoint(CPVPMatchMemberInfo* left, CPVPMatchMemberInfo* right);

    BlueRedTeam::Enum m_Team;
    const std::uint32_t m_nUserId;
    const std::u16string m_UserName;
    std::int32_t m_nOrder;
};

class CPVPMatch2;

class CPVPMatch : public CIOObject {
public:
    static constexpr int BanArraySize = 3;

    using UserDB = std::map<std::uint32_t, CPVPMatchMemberInfo*>;
    using UserList = std::list<CPVPMatchMemberInfo*>;

    CPVPMatch();
    ~CPVPMatch() override;

    virtual PVPMatchType::Enum GetPVPMatchType() const = 0;
    virtual void Init();
    virtual void InitSetting();

    bool IsPVPMatch() const;
    virtual bool IsPVPMatch2() const;
    virtual CPVPMatch2* CastPVPMatch2();
    bool CanAddPlayer(User& user) const;

    virtual void SendCurrentRecordForUIToOneUser(std::uint32_t userId, int type);
    virtual void UserDie(User* user, User* enemy);
    virtual void AddKillPoint(User* user, int point);
    virtual std::uint32_t GetManagerNpcId() const;
    virtual void RegisterManagerNpcId(int npcId);

    static BlueRedTeam::Enum GetTeamType(const char16_t* teamName, CUserSocket* socket);
    static char GetMatchTeamID(BlueRedTeam::Enum team);
    static BlueRedTeam::Enum GetOpposite(BlueRedTeam::Enum team);

    void TimerExpired(int id) override;
    int GetCombatElapsedSec() const;
    static bool GetRandomPos(const std::vector<FVector>& positions, FVector& position);
    static int GetMovingTeamCount(int blueCount, int redCount, int movingTeam);

    virtual int GetRelation(User& user, User& target);
    virtual bool IsEnemyTo(User& user, User& target);
    static int _GetRelation(UserDB& users, User& user, User& target);
    static bool _IsEnemyTo(UserDB& users, User& user, User& target);
    static bool _GetMemberInfo2(
        UserDB& users, int firstUserId, int secondUserId, CPVPMatchMemberInfo*& first, CPVPMatchMemberInfo*& second
    );
    void _BroadcastToPlayer(UserDB& users, const char* format, ...);

    virtual void OnNpcServerClosed();

    PVPMatchState::Enum GetState() const;
    virtual void ChangeState(PVPMatchState::Enum state);
    virtual wchar_t* GetName() const;
    // Preserve lock diagnostic coordinates across the enum owner move.
    void OnRegister() {
        L2_GUARD(m_Lock);
        _OnRegister();
    }
    void OnCombat() {
        L2_GUARD(m_Lock);
        _OnCombat();
    }
    void OnReward() {
        L2_GUARD(m_Lock);
        _OnReward();
    }
    virtual void _OnRegister();
    virtual void _OnCombat();
    virtual void _OnReward();

    void AddStartPos(BlueRedTeam::Enum team, const std::vector<FVector>& positions);
    void AddBanishPos(BlueRedTeam::Enum team, const std::vector<FVector>& positions);
    bool CheckDistanceWithNpc(User& user);
    virtual bool CheckDistance(FVector& first, FVector& second, int distance);
    void InsertBannedUser(int userId);
    void _InsertBannedUser(int userId);
    bool IsBannedUser(int userId);
    bool _IsBannedUser(int userId) const;

protected:
    CYieldLock m_Lock;
    CYieldLock m_NpcLock;
    PVPMatchState::Enum m_State;
    std::int32_t m_CurrentStateStartTime;
    std::int32_t m_ManagerNpcId;
    std::uint32_t m_targetPadding44 = 0;
    std::vector<std::int32_t> m_ManagerNpcs;

public:
    CTerritory m_Territory;
    std::int32_t m_TimerTick;
    std::uint32_t m_targetPaddingE4 = 0;
    std::vector<FVector> m_StartingPos[2];
    std::vector<FVector> m_BanishingPos[2];

protected:
    std::int32_t m_BannedUserIndex;
    std::uint32_t m_targetPadding16c = 0;
    std::set<std::int32_t> m_BannedUsers[BanArraySize];
};

class CPVPMatch2 : public CPVPMatch {
public:

    explicit CPVPMatch2(PVP_MATCH_TYPE type);
    ~CPVPMatch2() override;

    PVPMatchType::Enum GetPVPMatchType() const override = 0;
    bool IsPVPMatch2() const override;
    CPVPMatch2* CastPVPMatch2() override;

    virtual bool IsCC() const;
    virtual CCrataeCube* CastCC();
    virtual bool IsUC() const;
    virtual CUndergroundColosseum* CastUC();

    PVP_MATCH_TYPE GetMatchTypeDetail();
    virtual void EndMatchByTimeOut();
    virtual bool CanResurrectedByMatch(User* user);
    virtual void SendCurrentRecordForSystemMessage(User* user);
    virtual void SendCurrentRecordForUI(User* user, int type);
    virtual bool IsPVPMatchRelateNpc(CNPC* npc);
    virtual int GetPVPMatchTeamIndex(CNPC* npc);
    void OnNpcServerClosed() override;

protected:
    virtual bool CheckRegisterLevel(int level);
    virtual void PostEndMatch();
    virtual void GivePVPPointToWinnerParty(std::uint32_t partyId);

public:
    std::uint32_t GetGateManNpcId();
    void SetGateManNpcId(std::uint32_t npcId);
    void AddKillPoint(User* user, int point) override;
    virtual int GetRegisteredUsersPacket(char* packet, int packetSize);
    virtual int RegisterUser(std::uint32_t userId);
    virtual bool UnregisterUser(std::uint32_t userId, int reason);
    virtual bool ReadyMatch();
    virtual bool StartMatch();
    virtual void TriggerEndMatch();
    virtual int CheckRegisterUser(std::uint32_t userId);
    virtual bool CheckInParty(std::uint32_t userId);
    void SendCurrentRecordForUIToOneUser(std::uint32_t userId, int type) override;
    virtual bool IsRegisteredUser(std::uint32_t userId);
    virtual bool IsCrataeCubeUser(User& user);
    virtual int GetRankUserPVPMatch(std::uint32_t userId);

protected:
    PVP_MATCH_TYPE m_eType;
    std::uint32_t m_targetPadding1d4 = 0;
    std::vector<std::int32_t> m_listRegisteredParties;
    std::uint32_t m_nBlueTeamPartyId;
    std::uint32_t m_nRedTeamPartyId;
    std::uint32_t m_nLastWinnerPartyId;
    std::int32_t m_nStraightWinCount;
    std::uint32_t m_dwStartTime;
    std::uint32_t m_dwEndTime;
    std::uint32_t m_dwIgnoreStraightWinTime;
    bool m_bGivePVPPoint;
    std::array<std::uint8_t, 3> m_targetPadding215{};
    std::vector<std::int32_t> m_listRegisteredUsers;
    std::uint32_t m_nGateManNpcId;
    std::uint32_t m_targetPadding23c = 0;
};

class CPVPMatchSystem {
public:
    class CPVPPointTimeBlock {
    public:
        CPVPPointTimeBlock();

        void SetStartTime(int hour, int minute);
        void SetEndTime(int hour, int minute);
        void SetWeekDays(int* weekDays);
        bool IsInTime(std::tm* now);
        bool IsValid();
        void ShowInfo(CUserSocket* socket);

    private:
        std::int32_t m_nStartHour;
        std::int32_t m_nStartMinute;
        std::int32_t m_nEndHour;
        std::int32_t m_nEndMinute;
        std::int32_t m_arrWeekDays[7];
    };

    static CPVPMatchSystem& inst();

    CPVPMatchSystem();
    ~CPVPMatchSystem();

    void PrepareAllPVPMatch();
    void Init();
    void OnNpcServerClosed();
    CPVPMatch* CreateMatch(std::uint32_t managerNpcId, PVP_MATCH_TYPE type);
    PVPMatchResult RegisterParty(std::uint32_t matchId, std::uint32_t partyId);
    void UnregisterParty(std::uint32_t matchId, std::uint32_t partyId);
    bool StartMatch(std::uint32_t matchId);
    bool StartMatch(std::uint32_t matchId, std::uint32_t bluePartyId, std::uint32_t redPartyId);
    void EndMatch(std::uint32_t matchId);
    int CheckRegisterParty(std::uint32_t matchId, std::uint32_t partyId);
    bool RegisterResurrectionTower(std::uint32_t matchId, std::uint32_t team, std::uint32_t npcId);
    bool RegisterResurrectionNPC(std::uint32_t matchId, std::uint32_t npcId);
    int GetRegisteredPartiesPacket(std::uint32_t matchId, char* packet, int packetSize);
    bool IsPVPPlayer(User* user);
    int GetTeamIndexForUI(User* user);

    void SetEnableCheckPartyMemberCount(bool enabled);
    bool IsCheckPartyMemberCount() const;
    void SetEnableCheckPartyMemberLevel(bool enabled);
    bool IsCheckPartyMemberLevel() const;
    void SetEnableCheckPartyMemberDist(bool enabled);
    bool IsCheckPartyMemberDist() const;
    void SetEnableCheckPartyMemberDebuff(bool enabled);
    bool IsCheckPartyMemberDebuff() const;
    void SetEnableCheckGivePVPPointTime(bool enabled);
    bool IsCheckGivePVPPointTime() const;
    void SetEnableCheckAllCondition(bool enabled);

    void UserDie(User* user, User* enemy);
    bool CanResurrectedByMatch(User* user);
    void OnDismissParty(CParty* party);
    void OnHandOverPartyMaster(CParty* party, User* newMaster);
    void SendCurrentRecordForSystemMessage(User* user);
    void SendCurrentRecordForUI(User* user);
    void ShowPVPPointRewardTime(CUserSocket* socket);
    int GetPVPMatchTeamIndex(CNPC* npc);
    int GetRelation(User& user, User& target);
    bool IsEnemy(User* user, User* target);
    bool LeaveWorld(User& user);

    void AddGivePVPPointTime(CPVPPointTimeBlock* block);
    bool IsInGivePVPPointTime();
    CUndergroundColosseum* GetMatchUCByPartyId(std::uint32_t partyId);
    CUndergroundColosseum* GetMatchUCByMatchType(PVP_MATCH_TYPE type);
    static bool IsValidPvpMatch(int matchId);
    static bool IsValidUC(PVP_MATCH_TYPE type);

    bool IsCrataeCubeUser(User& user);
    int GetRankUserPVPMatch(std::uint32_t matchId, std::uint32_t userId);
    int RegisterUser(std::uint32_t matchId, std::uint32_t userId);
    bool UnregisterUser(std::uint32_t matchId, User& user, int reason);
    bool ReadyMatch(std::uint32_t matchId);
    int CheckRegisterUser(std::uint32_t matchId, std::uint32_t userId);
    int GetRegisteredUsersPacket(std::uint32_t matchId, char* packet, int packetSize);
    CPVPMatch* GetMatchByUser(User& user);
    CPVPMatch2* GetMatchByMatchType(PVP_MATCH_TYPE type);
    CPVPMatch2* GetMatchByUserLevel(int level);
    PVP_MATCH_TYPE GetMatchTypeByUserLevel(int level);
    void GetMyStatus(User& user, int& status);
    void GetRelationTo(User& user, User& target, int& relation);
    bool CrataeMatchUsingBuilder(std::uint32_t matchId, int command);

private:
    CUndergroundColosseum* GetMatchUCByManagerNpcId(std::uint32_t npcId);
    CCrataeCube* GetMatchCCByManagerNpcId(std::uint32_t npcId);
    CPVPMatch2* GetMatchByManagerNpcId(std::uint32_t npcId);

    bool m_bCheckPartyMemberCount;
    bool m_bCheckPartyMemberLevel;
    bool m_bCheckPartyMemberDist;
    bool m_bCheckPartyMemberDebuff;
    bool m_bCheckGivePVPPointTime;
    std::array<std::uint8_t, 3> m_targetPadding5{};
    CPVPMatch2* m_PVPMatch2Map[PVPMT_MAX];
    std::vector<CPVPPointTimeBlock*> m_vecGivePVPPointTime;
    CCleft* m_CleftMatch;
    CBlockUpsetManager* m_BlockUpsetManager;
};

class CEventMatchHelper {
public:
    static CUndergroundColosseum* GetUCMatch(int matchId);
    static void RegisterParty(int matchId, User& target, User& builder);
    static void UnregisterParty(int matchId, User& target, User& builder);
    static void StartMatch(int matchId, User& user);
    static void UnregisterAll(int matchId, User& user);
    static void StartObserve(int matchId, User& user);
    static void EndObserve(int matchId, User& user);
};
