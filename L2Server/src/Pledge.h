#pragma once

#include "Protocol/src/L2Constants.h"

#include "Object.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "SubPledge.h"

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/PledgeType.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeObjectMap.h"
#include "Shared/src/Utils.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

class CBattleCamp;
class CAlliance;
class CCastle;
class CCompeteAgit;
class CDominion;
class CFortress;
class CPledgeMemberList;
class CPledgeMemberService;
class CSharedPledgeData;
class CWorldAgit;
class CWorld;
class CPledgeTimer;
class PledgeFunction;
class PledgeItem;
class PledgeLevel;
class PledgeMastery;
class PledgeSkill;
class User;
class CPledge;
class CSocket;
class CUserSocket;
class CDBSocket;
class RaidClientSocket;
enum eSMsgType : std::int32_t;
enum SayPacketType : std::int32_t;

bool ReplyChangeMergedName(CDBSocket* socket, const unsigned char* packet);
bool ReplyCreatePledgeByNpc(CDBSocket* socket, const unsigned char* packet);
bool ReplyModPledgeNamePacket(CDBSocket* socket, const unsigned char* packet);
bool RequestChangeCharName(CSocket* socket, const unsigned char* packet);

namespace PledgeMission {
enum GoalType : std::int32_t;
}

namespace BuilderCmd {
bool PledgeBonus(CUserSocket*, User*, char16_t*);
}

namespace UserPacketEx {
bool Request_C_EX_PLEDGE_SIGNIN(CSocket* socket, const std::uint8_t* packet);
bool RequesPledgeBonusUIOpen(CSocket* socket, const std::uint8_t* packet);
bool RequesPledgeBonusReceiveReward(CSocket* socket, const std::uint8_t* packet);
} // namespace UserPacketEx

namespace DBPacketExHandler {
bool ReplyPledgeOnPledgeRecruitBoard(CDBSocket* socket, const unsigned char* packet);
}

namespace PledgeItemStuff {
void RequestLoadPledgeItem(CPledge* pledge);
}

// Exact three-dword capture embedded in asynchronous pledge jobs. The
// smart id is the fast path. World/database ids keep the callback valid when
// the pledge object was re-created between request and completion.
struct PledgeCapture {
    std::uint32_t m_sid = 0;
    std::int32_t m_worldid = 0;
    std::int32_t m_dbid = 0;

    CPledge* GetPledge() const;
};

enum PledgePowerEnum : std::int32_t {
    PP_NO = 0,
    PP_JOIN = 1,
    PP_GIVE_NICKNAME = 2,
    PP_VIEW_WAREHOUSE = 3,
    PP_MANAGE_GRADE = 4,
    PP_DECLARE_WAR = 5,
    PP_OUST_MEMBER = 6,
    PP_SET_CREST = 7,
    PP_MANAGE_MASTER = 8,
    PP_USE_SYSTEM_FUNC = 8,
    PP_MANAGE_GROWTH = 9,
    PP_CONTROL_SYSTEM_FUNC = 9,
    PP_SUMMON_AIRSHIP = 10,
    PP_OPEN_AGIT_DOOR = 11,
    PP_USE_AGIT_FUNC = 12,
    PP_AGIT_AUCTION = 13,
    PP_OUST_FROM_AGIT = 14,
    PP_CONTROL_AGIT_FUNC = 15,
    PP_OPEN_CASTLE_DOOR = 16,
    PP_MANAGE_MANOR = 17,
    PP_REGISTER_CASTLE_WAR = 18,
    PP_USE_CASTLE_FUNC = 19,
    PP_OUST_FROM_CASTLE = 20,
    PP_MANAGE_TAX = 21,
    PP_MANAGE_MERCENARY = 22,
    PP_CONTROL_CASTLE_FUNC = 23,
    PP_REGISTER_AGIT_RAID = 24,
    PP_MAX = 25,
};

// enum CastleUnderSiege : std::int32_t {
//     CUS_NOT_IN_SIEGE = 0,
//     CUS_OFFENCE = 1,
//     CUS_DEFENCE = 2,
//     CUS_MAX = 3,
// }

enum PledgeRequestLoadType : std::int32_t {
    PRLT_DISMISS = 0,
    PRLT_AGIT = 1,
    PRLT_PLEDGEWAR = 2,
    PRLT_ALLIANCE = 3,
    PRLT_TEAMBATTLEAGIT = 4,
    PRLT_SIEGEAGIT = 5,
    PRLT_FORTRESS = 6,
    PRLT_CASTLE = 7,
    PRLT_MAX = 8,
};

namespace PledgeBonus {

enum LastUpdateTime : std::int32_t {
    TODAY = 0,
    YESTERDAY = 1,
    BEFORE_YESTERDAY = 2,
};

enum Activity : std::int32_t {
    NOTHING = 0,
    MISSION_COMPLETE = 1,
    NEW_MEMBER = 2,
};

enum TimeStamp : std::int32_t {
    ACCESS_MISSION_COMPLETE = 0,
    ACCESS_REWARD_RECEIVE = 1,
    HUNT_REWARD_RECEIVE = 2,
    MAX_TIME_STAMP = 3,
};

} // namespace PledgeBonus


// Current .data slice 015dda00..015dda68 fixes these initial values, and the
// current set_bp/show_bp callbacks plus pledge/castle consumers close each
// read/write role. TotalChecker2015 Pledge.obj supplies name hints only after
// those current contracts are closed in the shipping image.
namespace BP {

extern int _PLEDGE_OUST_PENALTY_TIMEOUT;
extern int _PLEDGE_WITHDRAW_PENALTY_TIMEOUT;
extern int _PLEDGE_OUSTED_PENALTY_TIMEOUT;
extern int _PLEDGE_WAR_TIMEOUT;
extern int _PLEDGE_RECHALLENGE_PENALTY_TIMEOUT;
extern int _PLEDGE_RECHALLENGE_DESPITE_REJECTION_PENALTY_TIMEOUT;
extern int _PLEDGE_WAR_CANCEL_TIME;
extern int _PLEDGE_WAR_WAITING_TIME;
extern int _PLEDGE_WAR_PROCESSING_TIME;
extern int _PLEDGE_WAR_SUSPENDDING_TIME;
extern int _PLEDGE_WAR_STANDBY_TIME;
extern int _PLEDGE_HEARTBEAT_FOR_EXPIRATION;
extern int _PLEDGE_DISMISS_TIMEOUT;
extern int _PLEDGE_DISMISS_PENALTY_TIMEOUT;
extern int _PLEDGE_DISMISS_PROPOSE_PENALTY_TIMEOUT;
extern int _ALLIANCE_OUST_PENALTY_TIMEOUT;
extern int _ALLIANCE_WITHDRAW_PENALTY_TIMEOUT;
extern int _ALLIANCE_OUSTED_PENALTY_TIMEOUT;
extern int _ALLIANCE_DISMISS_PENALTY_TIMEOUT;
extern int _ALLIANCE_RECHALLENGE_PENALTY_TIMEOUT;
extern int _ALLIANCE_RECHALLENGE_DESPITE_REJECTION_PENALTY_TIMEOUT;
extern int _CASTLE_ANNOUNCE_TIME;
extern int _CASTLE_STANDBY_TIME;
extern int _CASTLE_IN_WAR_TIME;
extern int _CASTLE_CHANGE_TIME;
extern int _COMPETE_AGIT_PREPARE_TIME;
extern int _DOMINION_STANDBY_TIME;
extern int _DOMINION_SIEGE_TIME;

} // namespace BP

// Win64 long is 32-bit. Keep that exact type on Windows while retaining the
// same arithmetic width on LP64 hosts.
using L2ServerLong = std::conditional_t<sizeof(long) == sizeof(std::int32_t), long, std::int32_t>;

// Exact current 0x10 bonus record. The replacement field spellings are
// donor-selected. Every offset and all accesses below are independently
// established by the current L2Server instructions.
struct PledgeBonusInfo final {
    std::int32_t m_nCurrentBonusPoint;
    std::int32_t m_nMaxBonusPoint;
    std::int32_t m_nLastSaveTime;
    std::int32_t m_nTodayRewardLev;
};

// Exact current 0xa4 pledge-bonus configuration object. The current image
// proves every member boundary and all indexing below. Member spellings and
// the source-level [4][6] spelling of the first 0x60 bytes are independently
// corroborated by the TotalChecker donor. No donor layout is used as current
// evidence.
class CPledgeBonusSetting final {
public:
    CPledgeBonusSetting();
    ~CPledgeBonusSetting();

    static CPledgeBonusSetting& Inst();

    void SetGoalRate(int bonusType, int pledgeLevel, int goalRate) { m_nGoalRate[bonusType][pledgeLevel] = goalRate; }

    void SetAccessRewardSkill(int rewardIndex, std::int64_t skillKey) {
        const std::uint64_t bits = static_cast<std::uint64_t>(skillKey);
        const std::uint32_t skillIdBits = static_cast<std::uint32_t>(bits >> 32);
        std::memcpy(&m_nAccessRewardSkill[rewardIndex][0], &skillIdBits, sizeof(skillIdBits));
        m_nAccessRewardSkill[rewardIndex][1] = static_cast<int>((bits >> 16) & 0xffffu);
        m_nAccessRewardSkill[rewardIndex][2] = static_cast<int>(bits & 0xffffu);
    }

    void SetHuntRewardItem(int rewardIndex, int itemId) { m_nHuntRewardItem[rewardIndex] = itemId; }

    void SetExpToHuntpoint(int rate) { m_nExpToHuntpointRate = rate; }

    int GetGoalRate(int bonusType, int pledgeLevel) {
        if (pledgeLevel > 5) {
            pledgeLevel = 5;
        }
        return m_nGoalRate[bonusType][pledgeLevel];
    }

    int GetAccessRewardSkill(int rewardLevel, int component) {
        if (static_cast<std::uint32_t>(rewardLevel) - 1u < 4u) {
            return m_nAccessRewardSkill[rewardLevel - 1][component];
        }
        return 0;
    }

    int GetHuntRewardItem(int rewardLevel) {
        if (static_cast<std::uint32_t>(rewardLevel) - 1u < 4u) {
            return m_nHuntRewardItem[rewardLevel - 1];
        }
        return 0;
    }

    int GetExpToHuntpointRate() { return m_nExpToHuntpointRate; }


private:
    int m_nGoalRate[4][6];
    int m_nAccessRewardSkill[4][3];
    int m_nHuntRewardItem[4];
    int m_nExpToHuntpointRate;
};

// Exact current two-record CPledge bonus envelope.
class CPledgeBonus final {
public:
    PledgeBonus::LastUpdateTime CheckSaveTime(L2ServerLong currentTime, L2ServerLong lastSaveTime);
    int SetRewardLev(int enabled, int currentPoint, int maximumPoint);
    int AddBonusPoint(int bonusType, int point, L2ServerLong saveTime);
    void RefreshBonusPoint(int bonusType, L2ServerLong saveTime, int pledgeLevel, int currentPoint, int enabled);

    PledgeBonusInfo m_PledgeBonus[2];
};

// Current MemberInfo RTTI at 010f38b8 derives directly from the 0x5010-byte
// packet helper. TotalChecker2015 PDB names the sole added dword and its
// two inline accessors. Current CPledge::_OpenAllWindowPacket constructs the
// same 0x5018-byte envelope and initializes the count to zero.
class MemberInfo : public CPacketHelperEx<20480> {
public:
    MemberInfo() noexcept : CPacketHelperEx<20480>(), m_memberCount(0) {}

    int GetMemberCount() const noexcept { return m_memberCount; }

    void IncreaseMemberCount() noexcept { ++m_memberCount; }

private:
    int m_memberCount;
};


struct CTransactionLock final {
    volatile std::int32_t m_nTransactionStatus;
    std::int32_t m_nTransaction;
};

// Exact current 0x290 CPledge owner. The object is only
// partially constructible at this recovery frontier: current code closes the
// complete envelope and the typed member-SID/lock tail used below, while the
// unresolved state blocks remain honest gaps until their own methods are
// recovered. No offset-only surrogate is used by production code.
class CPledge : public CObject {
public:
    static void SetPledgePenalty();
    static void InitPledge();
    static void GarbageCollect();
    L2_DECLARE_OBJECT_REGISTRATION(CPledge)

    using PledgeSmartIdManager = CSmartIdManager<CPledge, 10002, 16>;
    using PledgeDbIdMap = CObjectMap<CPledge, 128, 5, RWLock, std::CDualKey<int, int>>;
    using PledgeDbIdMapTemp = CObjectMap<CPledge, 1, 5, RWLock, std::CDualKey<int, int>>;
    using PledgeNameMap = CObjectByIntKeyStringMap<CPledge>;

    struct PledgeWarInfo final {
        std::uint32_t m_nEnemyPledgeSId;
        bool m_bBothDeclared;
        std::array<std::byte, 0x03> m_alignment05To08;
        std::int32_t m_nWarStartTime;
        std::int32_t m_nDeclaredTime;
        std::int32_t m_nWarPoint;
        std::int32_t m_nWarPointPrev;
    };

    struct WarDetailForView final {
        WarDetailForView() noexcept :
            sPledgeName(nullptr),
            nWarType(0),
            nWarStatus(0),
            nElapsedTime(0),
            nWarPointDiff(0),
            nWarPoint(0),
            nLeftKillCount(0) {}

        ~WarDetailForView() noexcept { sPledgeName = nullptr; }

        const char16_t* sPledgeName;
        std::int32_t nWarType;
        std::int32_t nWarStatus;
        std::int32_t nElapsedTime;
        std::int32_t nWarPointDiff;
        std::int32_t nWarPoint;
        std::int32_t nLeftKillCount;
    };

    using WarListType = std::vector<std::pair<int, WarDetailForView>>;

    enum : std::int32_t {
        PLEDGE_DECLARE = 0,
        PLEDGE_TARGETED = 1,
        PLEDGE_BOTH_DECLARE = 2,
        PLEDGE_WIN = 3,
        PLEDGE_LOSE = 4,
        PLEDGE_DRAW = 5,
    };

    enum : std::int32_t {
        WARLIST_DECLARE = 0,
        WARLIST_TARGETED = 1,
    };

    CPledge(int pledgeDatabaseId, int masterDatabaseId, wchar_t* pledgeName, wchar_t* masterName, int worldId);
    ~CPledge() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void TimerExpired(int id) override;
    int GetIndex() const override;
    CObject* Copy() override;

    static CPledge* GetObjectW(std::uint32_t smartId);
    static CSPointer<CPledge> FindPledge(int worldId, int pledgeDatabaseId);
    static CSPointer<CPledge> FindPledge(int worldId, SharedUtilsWideChar* pledgeName);
    static bool IsMemberInTeamBattleEntry(User* user);
    static int GetDefaultPowerGrade(PledgeType pledgeType);
    static void ReserveLoaded(int worldId, int pledgeDatabaseId, PledgeRequestLoadType type);
    static int GetPledgeCountToBeLoaded();
    static void RequestReservedLoaded();
    static void SetFlagReservePledge(PledgeRequestLoadType type);
    static void PledgeMasterTransferByNpc(CNPC* npc, User* user, const char16_t* masterName);
    static void PledgeMasterTransferCancelByNpc(CNPC* npc, User* user);
    static void CreatePledgeByNpc(CNPC* npc, User* user, const char16_t* pledgeName);
    static void DismissPledgeByNpc(CNPC* npc, User* user);
    static void RevivePledgeByNpc(CNPC* npc, User* user);
    static void LoadSubPledge(const unsigned char* packet);
    static void LoadPledgeSkill(const unsigned char* packet);
    static void LoadPledge(
        wchar_t* pledgeName,
        int pledgeDatabaseId,
        int masterDatabaseId,
        wchar_t* masterName,
        int memberCount,
        const unsigned char* packet
    );
    static void ReserveWarDeclare(
        std::uint32_t pledgeSid,
        std::uint32_t enemyPledgeSid,
        std::uint32_t declaredTime,
        std::uint32_t warPoint,
        std::uint32_t previousWarPoint,
        std::uint32_t deathCount
    );
    static void SetAllDeclaredWarToPledges();
    static void UnregisterPledge(CSPointer<CPledge> pledge);

    const char16_t* GetPledgeName() const;
    const char16_t* GetPledgeMasterName() const;
    const char16_t* GetMasterName() const;
    User* GetMaster();
    User* GetAnyElite();
    User* GetMasterOrAnyElite();
    bool CanAcceptNewMember();
    bool CanDoPledgeWork(int timeoutSeconds);
    bool CanJoinMoreMember(PledgeType pledgeType);
    bool CanAppointElite(int characterDatabaseId);
    bool CanDismissElite(int characterDatabaseId);
    bool CanJoinAlliance() const;
    bool CanCreateAlliance() const;
    bool IsWaitingToDismiss() const;
    bool IsWaitingTransferMaster() const;
    void SetWaitingTransferMaster(bool waiting);
    void SetPledgeMemberCount(std::uint32_t memberCount);
    std::uint32_t GetAllianceSid();
    std::uint32_t GetAllianceDismissTime();
    std::uint32_t GetAllianceOutTime();
    void SetAllianceDismissTime(std::uint32_t time);
    void SetAllianceOustedTime(std::uint32_t time);
    void SetAllianceWithdrawTime(std::uint32_t time);
    CAlliance* GetAlliance();
    bool IsAllianceMaster() const;
    void SetAlliance(CAlliance* alliance, bool serveForRaid);
    void SetAllianceByDbId(unsigned int allianceDatabaseId);
    CCastle* GetCastle();
    void SetCastle(CCastle* castle);
    void SetCastleByDbId(unsigned int castleDatabaseId);
    CWorldAgit* GetWorldAgit();
    void SetWorldAgit(CWorldAgit* worldAgit);
    void SetAgitByDbId(unsigned int agitDatabaseId);
    CFortress* GetFortress() const noexcept { return m_pFortress; }
    CFortress* GetFortressUnderDefenceSiege() const { return m_pFortressUnderDefenceSiege; }
    CFortress* GetFortressUnderOffenceSiege() const { return m_pFortressUnderOffenceSiege; }
    void SetFortress(CFortress* fortress);
    void SetFortressByDbId(unsigned int fortressDatabaseId);
    CDominion* GetDominion() const noexcept { return m_pDominion; }
    void SetDominion(CDominion* dominion);
    CCompeteAgit* GetCompeteAgit() const;
    void SetCompeteAgit(CCompeteAgit* competeAgit);
    void SendDB(const char* format, ...) const override;
    void SelectMemberIDs(std::vector<int>& memberIds) const;
    CCastle* GetCastleUnderSiege(int castleIndex) const;
    bool IsGuilty();
    int GetNameValue();
    bool IsValidNameValue();
    void SetNameValue(int value);
    bool UseNameValue(int amount);
    int GetFieldValue(unsigned int field);
    int GetStatus();
    void SetStatus(int status);
    bool BeginTransaction(int* transaction);
    bool EndTransaction();
    bool CanDeclareWar(CSPointer<CPledge> enemyPledge, CUserSocket& socket);
    bool CanDeclareWarBoth(CSPointer<CPledge> enemyPledge);
    bool HasEnoughMemberForWar();
    static bool SwitchPledgeWarMemberCountCondition();
    void SetPledgeWorkDone() noexcept;
    std::uint32_t GetCrestId();
    void SetNewCrestId(std::uint32_t crestId);
    void SetNewCrest(std::uint32_t crestId);
    void SetMemberAllianceCrest(std::uint32_t crestId);
    std::uint32_t GetEmblemId();
    void SetNewEmblemId(std::uint32_t emblemId);
    void SetNewEmblem(std::uint32_t emblemId);
    std::uint32_t GetPledgeLevel();
    int GetMemberCount() const;
    int GatherAllSubPledgeName(CPacketHelperEx<20480>& packet);
    void ClearRecruitInfo();
    int GetDbMemberCount();
    bool CheckValidMasterCandidate(const char16_t* masterName);
    bool CheckPowerByGradeId(int gradeId, PledgePowerEnum power);
    std::uint32_t GetPowerGrade(int gradeId);
    std::uint32_t GetMasterDbId();
    void SetMasterDbId(std::uint32_t masterDatabaseId);
    std::uint32_t GetDbId();
    void SetDismissReservedTime(std::uint32_t time);
    std::uint32_t GetDismissReservedTime() const;
    std::uint32_t GetLastDismissProposeTime();
    int GetTotalLevel();
    int GetAllianceDbId();
    int GetCastleSiegeDefenceCount();
    bool RequestCastleSiegeDefenceCount(unsigned int count);
    bool SetCastleSiegeDefenceCount(unsigned int count);
    std::uint32_t GetCastleDbId();
    std::uint32_t GetResidenceId();
    std::uint32_t GetWorldAgitDbId();
    std::uint32_t GetOustTime();
    void SetOustTime(std::uint32_t time);
    bool HaveCastleSiegeStatus(CastleUnderSiege status) const;
    bool HaveCastleUnderSiege() const;
    int GetRank() const;
    bool HasResidence() const noexcept;
    bool CanOwnFortress(unsigned int fortressId);
    bool IsInCombatMode();
    bool IsInWar() const noexcept;
    bool IsCastleSiegeEnemy(CPledge* pledge) const;
    bool DidDeclareWar(int pledgeServerId);
    bool DidDeclareWarBoth(CPledge* pledge);
    int GetCurrentDeclareWarCount() const noexcept;
    int GetCurrentTargettedWarCount() const noexcept;
    void MemberAffectedByWar();
    void ResetCastleSiege();
    bool IsAttacker(CCastle* castle) const;
    bool IsDefender(CCastle* castle) const;
    void SetCastleUnderSiege(CCastle* castle, int status);
    void SetFortressUnderDefenceSiege(CFortress* fortress);
    void SetFortressUnderOffenceSiege(CFortress* fortress);
    void InitSiegeKillDeathCountFromDb(int killCount, int deathCount);
    void InitSiegeKillDeathCount();
    static void UpdateSiegeKillDeathPoint(CPledge* killerPledge, CPledge* victimPledge);
    void SetMemberRelationChanged();
    void PledgeMissionCheckAndAddCountInCastleSiege(
        PledgeMission::GoalType goalType, int firstValue, int secondValue, int count
    );
    void PlayMemberMusic();
    CSPointer<CBattleCamp> GetBattleCamp();
    void SetBattleCamp(CBattleCamp* battleCamp);
    int GetCurrentCastleSiegeStatus(int castleIndex) const;
    int GetCurrentCastleSiegeDbId(int castleIndex) const;
    void CopyCastleSiegeStatusToUser(User* user);

    void GetAllMember(std::vector<User*>& members, bool append);
    void TeleportToLocation(
        int x, int y, int z, int randomRange, std::uint32_t parameter, TeleportSourceType::Enum source, CCreature* creature
    );
    void GetAllMemberIdSet(std::set<std::uint32_t>& memberIds);
    void GetAllMemberId(std::vector<std::uint32_t>& memberIds);
    void SendToMembers(CPacketHelperEx<20480>& packet);
    void SendSystemMessageToMembersWithNpcId(int messageId, int npcId);
    void SetAllOnlineMemberRenamed(int dominionId);
    void RemovePledgeSkillsFrom(User* user);
    void AddPledgeSkillByEliteTo(User* member);
    void RemovePledgeSkillByEliteFrom(User* member);
    void DeleteDbMember(std::uint32_t userDatabaseId);
    void DeleteMember(User* user);
    void AddDbMember(User* user);
    void AddDbMember(
        std::uint32_t characterDatabaseId,
        const char16_t* characterName,
        const char16_t* nickname,
        int level,
        int classId,
        int gender,
        int race,
        std::uint32_t serverId,
        PledgeType pledgeType,
        int gradeId,
        int missionCheckTime
    );
    bool ChangeMemberName(std::uint32_t characterDatabaseId, wchar_t* name);
    void SetNickName(std::uint32_t characterDatabaseId, wchar_t* nickname);
    void Online(User* user);
    void Offline(User* user);
    void SetMaster(User* user);
    void AddMember(User* user);
    void SetAcademyMember(std::uint32_t characterDatabaseId, int joinLevel, std::uint32_t masterDatabaseId);
    void SetSubPledge(
        PledgeType pledgeType, wchar_t* subPledgeName, std::uint32_t masterDatabaseId, bool memberCountUpgrade
    );
    void AddSubPledgeTypeBits(PledgeType pledgeType);
    void Join(User* user);
    void AcquireSkillForAcademy(std::uint32_t characterDatabaseId, bool notify);
    void DeleteSkillForAcademy(std::uint32_t characterDatabaseId);
    bool SetAcademyMaster(const char16_t* characterName, const char16_t* masterName);
    std::uint32_t SetPledgePowerGrade(wchar_t* characterName, int gradeId);
    std::uint32_t GetMemberDbId(wchar_t* characterName);
    bool IsPledgeTypeMember(wchar_t* characterName, PledgeType pledgeType);
    bool CanBeAcademyMaster(std::uint32_t characterDatabaseId);
    std::uint32_t GetAcademyMasterDbId(std::uint32_t characterDatabaseId);
    wchar_t* GetMemberName(std::uint32_t characterDatabaseId);
    int GetPledgePowerGrade(wchar_t* characterName);
    void SendPledgPowerListPacket(User* user);
    void ViewPledgPowerPacket(User* user, PledgeType pledgeType, wchar_t* characterName);
    void ViewPledgeMemberInfo(User* user, PledgeType pledgeType, wchar_t* characterName);
    void ViewPledgeMemberInfo(User* user, wchar_t* characterName);
    void ViewPledgeMemberInfo(wchar_t* characterName);
    void GraduateAcademy(User* user);
    void ReplyGraduateAcademy(User* user);
    bool HasPledgeType(PledgeType pledgeType);
    bool HasSubPledgeType(PledgeType pledgeType);
    void OpenAllWindowPacket(User* user, bool update, const char16_t* targetName);
    void OpenAllWindowPacket();
    void SendPledgeInfo(CUserSocket* socket);
    void SendPledgeSkillsTo(User* user);
    void AcademyOfflineMsg(User* user);
    void SendPledgeOnlineMemberCount();
    bool HasAcademyMaster(std::uint32_t characterDatabaseId);
    bool HasAcademyStudent(std::uint32_t characterDatabaseId);
    void CloseMemberWindowPacket(User* user);
    void CloseMemberWindowPacket(const char16_t* characterName);
    void ValidateTotalLevel();
    bool IsSubMaster(User* user, PledgeType pledgeType);
    bool IsSubMaster(std::uint32_t userDatabaseId);
    PledgeType GetSubMasterPledgeType(std::uint32_t userDatabaseId);
    PledgeType ChangeMemberPledgeType(std::uint32_t characterDatabaseId, PledgeType pledgeType);
    void RenameSubpledge(PledgeType pledgeType, wchar_t* newName);
    CSubPledge* GetSubPledge(PledgeType pledgeType);
    wchar_t* GetSubPledgeName(PledgeType pledgeType);
    wchar_t* GetSubPledgeMasterName(PledgeType pledgeType);
    void SendSubPledgeUpdatedPacket(PledgeType pledgeType);
    void UpdatePledgeInfoPacket();
    void UpdatePledgeMemberListPacket(std::uint32_t userDatabaseId);
    void UpdateInfoWindowPacket();
    void UpdateNameValue(int delta);
    void ApplyPledgeSkillsTo(User* user, bool sendAcquireMessage, bool replaceSkill);
    void AddPledgeSkillsTo(User* user, bool activate);
    bool RemovePledgeSkill(int skillId, bool synchronizeRaid);
    void ApplyPledgeSkillsToMembers(bool force, bool notify, bool refresh);
    void Save(unsigned int field);
    void SetPledgeLevel(unsigned int level, bool save);
    void SetPowerGrade(int gradeId, unsigned long power);
    void SetMasteryResetTime(unsigned int time) noexcept;
    bool CanResetMastery(int penaltySeconds) const;
    bool AddPledgeSkillWithSaveAndNotify(
        int skillId,
        int skillLevel,
        std::int32_t expiryTime,
        bool synchronizeRaid,
        bool sendAcquireMessage,
        bool replaceSkill
    );
    bool AddPledgeSkillWithNotify(unsigned int skillId, int skillLevel, std::int32_t expiryTime);
    void SavePledgeSkillToDB(int skillId, int skillLevel, std::int32_t expiryTime);
    void RemovePledgeSkills(bool synchronizeRaid);
    void UnequipMemberCastleItem();
    void SystemMessage(int messageId);
    void SystemMessage(int messageId, int parameter);
    void SystemMessage(int messageId, eSMsgType argumentType, const char16_t* text, int parameter);
    void SystemMessage(
        int messageId,
        eSMsgType firstArgumentType,
        const char16_t* firstText,
        eSMsgType secondArgumentType,
        const char16_t* secondText,
        int parameter
    );
    void SystemMessage(int messageId, int firstParameter, int secondParameter);
    void SystemMessage(int messageId, eSMsgType argumentType, const char16_t* text);
    void MailArrived();
    void SetPledgeMissionCheck(User* user, int missionCheckTime);
    void SetPledgeRaidInzoneBinding(bool force);
    void SetPledgeMember(User* user);
    std::uint32_t GetRemainSubPledgeTypeBits();
    void ServeForAllMember(CPledgeMemberService& service, bool serveForRaid);
    void Say(User* user, const char16_t* message, SayPacketType type, bool relay, const char16_t* targetName);
    void Say(int tellerServerId, int tellerDatabaseId, const char16_t* tellerName, const char16_t* message, SayPacketType type);
    void SayByMobile(
        int serverId, int characterDatabaseId, const char16_t* characterName, const char16_t* message, SayPacketType type
    );
    void CancelAllRenamed();
    void SetDominionUnderSiege(CDominion* dominion, bool underSiege);
    void SendDominionChannelSet(bool enabled);
    void SendDominionEventToAllOnlineMember(int npcId, int dominionId, int eventType, bool start);
    void SendDominionScriptEventToAllOnlineMember(int arg1, int arg2, int arg3);
    void SendDominionMessageToAllOnlineMember(
        int messageId, int argument, eSMsgType argumentType, const char16_t* text, bool argumentFirst
    );
    void SendDominionMessageToAllOnlineMember2(int messageId, int argument);
    int ProcessBonusPoint(User* user, int bonusType, int point, int enabled, L2ServerLong saveTime);
    void Dismiss();
    void Oust(std::uint32_t characterDatabaseId, const wchar_t* characterName);
    void Withdraw(User* user);
    void SetPledgeName(wchar_t* pledgeName);
    void SetPledgeMasterName(const wchar_t* masterName);
    void UpdateSubPledgeMaster(PledgeType pledgeType, std::uint32_t masterDatabaseId);
    void DeclareWar(CPledge* enemyPledge, int declaredTime);
    void CheckDeclareWarBoth(CPledge* enemyPledge);
    bool CancelWar(CPledge* enemyPledge, int cancelTime);
    void SendViewPledgeWarList(CUserSocket* socket, unsigned int page, int listCondition);
    void GetDetailTargetedList(WarListType& pledges);
    void GetDetailWarList(WarListType& pledges);
    void _MakeDetailList(CPledge* enemyPledge, PledgeWarInfo war, WarDetailForView& detail);
    bool _MakeDetailTargetList(CPledge* targetPledge, WarDetailForView& detail, int leftKillCount);
    void SendWarReport(CUserSocket* socket);
    void GetBothDeclareList(std::vector<CPledge*>& pledges);
    void GetTargetedList(std::vector<CPledge*>& pledges);
    void CancelWarWhoTargetedMe();
    void TargetedBy(CPledge* enemyPledge, int targetedCount, bool bothDeclared);
    void UpdatePledgeWarPoint(int enemyPledgeSid, int enemyPledgeDatabaseId, int warPointDelta, bool save);
    int GetPledgeWarPoint(int enemyPledgeSid);
    bool UpdateNameValueByPledgeWar(unsigned int userServerId, int delta, unsigned int enemyPledgeDatabaseId);
    void SetPrevWarPointByLoading(int enemyPledgeSid, int previousWarPoint);
    void RemovePledgeSkillsFromMembers();
    void ResetAllNickName();
    void ClearAllSkillForAcademy();
    void TakeOustPenalty();

    int GetWorldId() const noexcept { return m_nWorldId; }
    bool IsUnderRaid() const noexcept { return m_bUnderRaid; }
    void SetUnderRaid(bool value) noexcept { m_bUnderRaid = value; }
    CSharedPledgeData* SharedData() const noexcept { return m_data; }
    PledgeFunction* GetFunction() const noexcept { return m_pFunction; }
    PledgeMastery* GetMastery() const noexcept { return m_pMastery; }
    PledgeLevel* GetLevel() const noexcept { return m_pLevel; }
    PledgeSkill* GetSkill() const noexcept { return m_pSkill; }
    PledgeItem* GetItem() const noexcept { return m_pItem; }

private:
    int _GetPledgeWarPoint(int enemyPledgeSid);
    void SetAcademyMasterStudentRelationship(User* user);
    friend bool ReplyChangeMergedName(CDBSocket* socket, const unsigned char* packet);
    friend bool ReplyCreatePledgeByNpc(CDBSocket* socket, const unsigned char* packet);
    friend bool RaidRequestPMSDismissPledge(RaidClientSocket* socket, const unsigned char* packet);
    friend bool RequestChangeCharName(CSocket* socket, const unsigned char* packet);
    friend bool ReplyModPledgeNamePacket(CDBSocket* socket, const unsigned char* packet);
    friend bool UserPacketEx::Request_C_EX_PLEDGE_SIGNIN(CSocket* socket, const std::uint8_t* packet);
    friend bool UserPacketEx::RequesPledgeBonusUIOpen(CSocket* socket, const std::uint8_t* packet);
    friend bool UserPacketEx::RequesPledgeBonusReceiveReward(CSocket* socket, const std::uint8_t* packet);
    friend bool DBPacketExHandler::ReplyPledgeOnPledgeRecruitBoard(CDBSocket* socket, const unsigned char* packet);
    friend bool BuilderCmd::PledgeBonus(CUserSocket*, User*, char16_t*);
    // Current CCreature::EquipItem@0x00624e14 directly consumes the
    // offence-fortress pointer while processing fortress flags.
    friend class CCreature;
    friend class CCastle;
    friend class CFortress;
    friend class CWorld;
    friend class PledgeMastery;
    friend class User;
    friend void PledgeItemStuff::RequestLoadPledgeItem(CPledge* pledge);

    void _ServeForAllMember(CPledgeMemberService& service);
    void _OpenAllWindowPacket(
        User* user,
        bool update,
        const char16_t* targetName,
        PledgeType pledgeType,
        CPacketHelperEx<20480>& packet,
        bool hasNext
    );
    void OnChangedPledgeSkillLevel();
    wchar_t* _GetSubPledgeName(PledgeType pledgeType);
    wchar_t* _GetSubPledgeMasterName(PledgeType pledgeType);
    void _SetSubPledgeMasterName(PledgeType pledgeType, std::uint32_t masterDatabaseId);
    void _SetPledgeWarStartTime(int enemyPledgeSid, int startTime);
    void InitPledgeData(int pledgeDatabaseId, int masterDatabaseId, wchar_t* pledgeName, wchar_t* masterName);
    void CheckDeclareTime();
    void SetPrevWarPoint();
    const unsigned char* SetProperty(const unsigned char* packet);
    void SetRemainedSubPledgeTypeToLoad(std::uint32_t remainedBits);
    const unsigned char* AddAcademyMemberByDisassemble(int memberCount, const unsigned char* packet);
    const unsigned char* AddMemberByDisassemble(PledgeType pledgeType, int memberCount, const unsigned char* packet);
    bool HasMoreSubPledgeToLoad();
    bool HasLoadedAllData();
    void RequestLoadSubPledge();
    void SubPledgeLoadingCompleted(PledgeType pledgeType);
    void LoadingCompleted();
    void SendPledgeSkillsToMembers();
    static void RegisterPledge(CSPointer<CPledge> pledge);
    static void DeleteReservedLoaded(int worldId, std::uint32_t pledgeDatabaseId);

    static PledgeSmartIdManager s_pledgeSmartIdManager;
    static PledgeDbIdMap s_pledgeDBMap;
    static PledgeDbIdMapTemp s_pledgeDBMapTemp;
    static PledgeNameMap s_pledgeNameMap;
    static std::set<std::CDualKey<int, std::uint32_t>> s_setPledgeToBeLoaded;
    // Current Win64 std::bitset<8> occupies one dword. A fixed carrier
    // keeps that target storage width on LP64 hosts.
    static std::uint32_t s_ReserveBitset;
    static RWLock s_lockSet;
    static std::vector<std::uint32_t> s_lstChallengerDbId;
    static std::vector<std::uint32_t> s_lstChallengeeDbId;
    static std::vector<std::uint32_t> s_lstDeclareTime;
    static std::vector<std::uint32_t> s_lstPoint;
    static std::vector<std::uint32_t> s_lstPointPrev;
    static std::vector<std::uint32_t> s_lstDeathCount;
    static RWLock s_lockList;
    static bool s_ignoreMemberCountCondition;

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CPledge)

    CSharedPledgeData* m_data; // +0x040
    CYieldLock* m_lock; // +0x048
    std::uint32_t m_nAllianceSid; // +0x050
    std::int32_t m_nWarCount; // +0x054
    volatile std::int32_t m_Dismissed; // +0x058
    std::uint32_t m_nAllianceDismissTime; // +0x05c
    std::uint32_t m_nAllianceOustedTime; // +0x060
    std::uint32_t m_nAllianceWithdrawTime; // +0x064
    std::int32_t m_nSiegeKillCount; // +0x068
    std::int32_t m_nSiegeDeathCount; // +0x06c
    CCastle* m_pCastle; // +0x070
    CWorldAgit* m_pWorldAgit; // +0x078
    CFortress* m_pFortress; // +0x080
    CCompeteAgit* m_pCompeteAgit; // +0x088
    CCastle* m_pCastleUnderSiege[9]; // +0x090..+0x0d7
    CFortress* m_pFortressUnderDefenceSiege; // +0x0d8
    CFortress* m_pFortressUnderOffenceSiege; // +0x0e0
    CDominion* m_pDominion; // +0x0e8
    std::int32_t m_tLastPledgeWorkTimeOut; // +0x0f0
    std::int32_t m_nDeclareCount; // +0x0f4
    std::int32_t m_LastWarDeclaredTime; // +0x0f8
    std::array<std::byte, 0x04> m_alignment0fcTo100; // +0x0fc..+0x0ff
    ::std::map<std::int32_t, PledgeWarInfo> m_mapDeclare; // +0x100..+0x11f
    ::std::map<std::int32_t, std::int32_t> m_setTargeted; // +0x120..+0x13f
    ::std::set<std::uint32_t> m_memberSidSet; // +0x140..+0x15f
    CTransactionLock m_tlDB; // +0x160..+0x167
    CSPointer<CPledgeMemberList> m_memberList; // +0x168..+0x177
    CObjectStorage<CBattleCamp> m_osBattleCamp; // +0x178..+0x17f
    RWLock m_nameValueLock; // +0x180..+0x1b7
    RWLock m_remainedSubPledgeTypeBitsLock; // +0x1b8..+0x1ef
    CYieldLock m_subPledgeLock; // +0x1f0..+0x1ff
    ::std::map<PledgeType, CSubPledge*> m_subPledgeMap; // +0x200..+0x21f
    std::uint32_t m_remainedSubPledgeTypeBits; // +0x220
    bool m_bSkillLoading; // +0x224
    bool m_bMergedProcessing; // +0x225
    bool m_bWaitingTransferMaster; // +0x226
    std::byte m_alignment227To228{};
    std::int32_t m_nWorldId; // +0x228
    bool m_bUnderRaid; // +0x22c
    std::byte m_alignment22dTo230[3]{};
    std::int32_t m_nStatTick; // +0x230
    bool m_bShowEmblem; // +0x234
    std::byte m_alignment235To238[3]{};
    std::int32_t m_nPrevPointTick; // +0x238
    std::int32_t m_nRecruitType; // +0x23c
    PledgeType m_recruitPledgeType; // +0x240
    CPledgeBonus m_PledgeBonus; // +0x244..+0x263
    std::array<std::byte, 0x04> m_alignment264To268; // +0x264..+0x267
    PledgeMastery* m_pMastery; // +0x268
    PledgeLevel* m_pLevel; // +0x270
    PledgeFunction* m_pFunction; // +0x278
    PledgeSkill* m_pSkill; // +0x280
    PledgeItem* m_pItem; // +0x288
};


// Exact current Pledge.cpp timer owner. It owns NPC-server liveness state,
// the delayed-dismiss schedule, and the 60-second deferred-job dispatcher.
class CPledgeTimer final : public CIOObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CPledgeTimer)

    enum NPCServerAliveType : std::int32_t {
        NSAT_20 = 0,
        NSAT_60 = 1,
        NSAT_MAX = 2,
    };

    CPledgeTimer();
    ~CPledgeTimer() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void TimerExpired(int id) override;

    void ReserveDismiss(std::uint32_t pledgeDatabaseId, std::uint32_t deletionTime);
    void CancelDismiss(std::uint32_t pledgeDatabaseId);
    void SetLastPongTimeAndState(L2ServerLong lastPongTime, int allNpcLoaded);
    bool IsAllNpcRelatedLoaded() const;
    bool IsNpcServerAlive(NPCServerAliveType type) const;
    bool IsAllNpcLoaded() const;
    bool IsAllMakerFinished() const;
    void SetAllMakerFinished(bool finished);

private:
    RWLock m_mapLock; // +0x18
    std::int32_t m_nCheckCount; // +0x50
    bool m_bNpcServerAlive[NSAT_MAX]; // +0x54
    bool m_bAllNpcLoaded; // +0x56
    bool m_bAllMakerFinished; // +0x57
    std::int32_t m_tLastPong; // +0x58
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignment5cTo60;
    std::map<std::uint32_t, std::uint32_t> m_mapDismissReservedPledge; // +0x60

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CPledgeTimer)
};


extern CPledgeTimer g_PledgeTimer;
