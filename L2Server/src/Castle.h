#pragma once

#include "Residence.h"

#include "Shared/src/Runtime/TargetWideChar.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>
#include <cstdint>
#include <utility>

class CFortress;
class CDominion;
class CDBSocket;
class CEventController;
class CObjectDB;
class CParserForResidence;
class CUserSocket;
class User;

bool BuilderCmd_set_pausable(CUserSocket* socket, User* user, char16_t* command);
bool BuilderCmd_show_castle_info(CUserSocket* socket, User* user, char16_t* command);
bool BuilderCmd_reset_castle_owner(CUserSocket* socket, User* user, char16_t* command);

namespace BuilderCmd {
bool _set_castle_owner(User*, CUserSocket*, CResidence*, char16_t*, int);
bool SetCastleType(CUserSocket*, User*, char16_t*);
} // namespace BuilderCmd

namespace NameValueForCastle {

extern int SUCCESS_FROM_NPC;
extern int SUCCESS_FROM_PC;
extern int SUCCESS_DEFENCE;
extern int FAILED_DEFENCE;

} // namespace NameValueForCastle

struct CastleSiegeData {
    std::int32_t nServerId; // +0x00
    std::int32_t nProposeTime; // +0x04
    std::int32_t nStatus; // +0x08
};


struct BufForCaching {
    std::int32_t nBufSize = 0; // +0x0000
    char pBuf[0x2004]{}; // +0x0004
};


// Current CCastle allocation sites pass 0x146f8. The two reservation maps,
// ten fixed cache buffers, four xvectors, tree carriers, and trailing siege
// state are all fixed by current constructor/destructor and
// packet-list accesses.
class CCastle : public CResidence {
public:
    using DomainFortressSet = std::set<CFortress*>;
    using PledgeKey = std::CDualKey<int, int>;
    using ReservedPledgeMap = std::map<PledgeKey, CastleSiegeData*>;
    using EventControllerKey = std::pair<CastleType::Enum, CEventController*>;
    using EventControllerMap = std::map<EventControllerKey, bool>;

    enum BAN_TYPE : std::int32_t {
        BAN_TYPE_0 = 0,
        BAN_TYPE_1 = 1,
        BAN_TYPE_2 = 2,
    };

    CCastle();
    ~CCastle() override;

    L2_DECLARE_OBJECT_TYPE(CCastle)

    bool IsCastle() const override;
    CCastle* CastCastle() const override;
    bool IsExtended() const override;
    int GetDominionSiegeID() const override;
    bool IsUnderDominionSiege() const override;
    bool IsUnderSiege() const override;
    void ChangeTaxRate() override;
    void StartTimer() override;
    void TimerExpired(int timerId) override;
    void StartSiege() override;
    void EndSiege() override;
    void PassSiege() override;
    void ChangeOwner(CPledge* pledge, int reason, bool announce) override;
    void PauseSiege() override;
    void ResumeSiege() override;
    bool IsRegisteredForSiege(CPledge* pledge) const override;
    void ExpelNonPledgeMember() override;
    void OnStartInBattleField(User* user, FVector& position, bool& started) override;

    DomainFortressSet GetDomainFortress() const { return m_sAllDomainFortress; }

    static int GetBattleFieldCastleId(FVector position);
    static void AddReservableSiegeTime(int weekday, int hour) {
        s_lstWday.push_back(weekday);
        s_lstHour.push_back(hour);
    }
    static void SetBaseSiegeTime(std::int32_t siegeTime) { s_tBaseSiegeTime = siegeTime; }
    static void SetMaxConcurrentSiege(int count) { s_nMaxConcurrentSiege = count; }
    static void StartAllTimer();
    static void SetAllCastleSiegeToPledges();
    static bool ReserveSiegeTime(int residenceId, std::int32_t siegeTime);
    static void CancelAttackSiegeAtAllCastle(CSPointer<CPledge> pledge);
    static int GetSelectableSiegeTimes(std::int32_t baseTime, int* siegeTimes, int count, int residenceId);

    void SetEditorPos(FVector position) { m_EditorPos = position; }
    void MakeEventController(CastleType::Enum castleType, int editorId);
    void SetDominion(CDominion* dominion);
    void AddRelatedFortress(CFortress* fortress);
    void RegisterSiegeGolemSummoner(User* user);
    void ResetCastleOwner(int userSid);
    void SetCastleType(CastleType::Enum castleType);
    void Set(
        int ownerPledgeId,
        int ownerWorldId,
        std::int32_t nextSiege,
        int siegeTerm,
        int residenceState,
        SharedUtilsWideChar* residenceName,
        int previousSiegeEnd,
        std::int64_t taxIncome,
        int siegeElapsed,
        CastleType::Enum castleType,
        int successCampaign
    );
    static void InitCastle();
    bool SetSiegeDate(std::int32_t siegeDate);
    void ClearSiegeDate();
    void ChangeCastleTypeOnTimerExpired(std::int32_t now);
    void InitKillScore();
    void IncreaseAttakerScore();
    void IncreaseDefenderScore();
    std::int64_t GetAttackerScore() const { return m_nAttackerScore; }
    std::int64_t GetDefenderScore() const { return m_nDefenderScore; }
    int GetReservedAttackerCount();
    int GetReservedDefenderCount();
    int GetNumChildDomainFortress();
    int GetReservedStatus(unsigned int pledgeId, int worldId);
    void ReserveAttacker(CPledge* pledge, int proposeTime);
    void ReserveDefender(CPledge* pledge, int proposeTime, int status);
    bool CancelCastleAttackSiege(CPledge* pledge);
    bool CancelCastleDefenseSiege(CPledge* pledge);
    bool ApproveDefence(unsigned int pledgeId, unsigned int worldId);
    static bool IsCastleSiegeRegistered(CPledge* pledge, int worldId);
    bool IsAttackerPledge(CPledge* pledge);
    bool IsDefenderPledge(CPledge* pledge);
    bool IsRelatedFortress(int fortressId);
    bool IsChildFortress(CFortress* fortress);
    bool IsAllChildFortressRelated();
    void NotifySupportBroken();
    void ValidateLifeControlLevel(bool sendEvent);
    void SetChildFortress(CFortress* fortress);
    void SendCastleSiegeInfo(CUserSocket* socket, User* user);
    void SendCastleSiegeAttackerList(CUserSocket* socket, User* user);
    void SendCastleSiegeDefenderList(CUserSocket* socket, User* user);
    void BroadcastSetSiegeTime();

private:
    friend bool ReplyLoadCastlePacket(CDBSocket*, const std::uint8_t*);
    friend class CObjectDB;
    friend class CFortress;
    friend class CParserForResidence;
    friend class CPledge;
    friend class User;
    friend bool BuilderCmd_set_pausable(CUserSocket* socket, User* user, char16_t* command);
    friend bool BuilderCmd_show_castle_info(CUserSocket* socket, User* user, char16_t* command);
    friend bool BuilderCmd_reset_castle_owner(CUserSocket* socket, User* user, char16_t* command);
    friend bool BuilderCmd::SetCastleType(CUserSocket* socket, User* user, char16_t* command);
    friend bool BuilderCmd::_set_castle_owner(
        User* user, CUserSocket* socket, CResidence* residence, char16_t* pledgeName, int castleType
    );

    void BroadcastCastleState();
    void BroadcastToAttacker(int messageId);
    void BroadcastEndRegister();
    void RemoveChildFortress(CFortress* fortress);
    void UpdateDomainFortressContractStatus();
    void AddCastleFortressStatusLog(int fortressId);
    void SetDefendingCastle(bool enabled, std::vector<User*>& users);
    void SetOffendingCastle(bool enabled, std::vector<User*>& users);
    void OnDefenceSiegeSuccess(CSPointer<CPledge> pledge);
    void ValidateStartSiege();
    void FreeReserveCastleSiegeList();
    void FreeSiegeGolem();
    void RegistrationOnTimerExpired(std::int32_t now);
    void StandByOnTimerExpired(std::int32_t now);
    void AnnouncingOnTimerExpired(std::int32_t now);
    void BanNonDefendingPlayers(BAN_TYPE type);
    bool OnChangeOwner();
    static void AdjustSiegeTime();
    void ValidateChangeOwner();
    void SetCastleSiegeRole(bool enabled);
    void ValidateStatus();
    void PrepareSiege();
    void GetDefenderPledge(std::vector<CPledge*>& pledges, std::vector<int>& proposeTimes, std::vector<int>& statuses);
    void GetAttackerPledge(std::vector<CPledge*>& pledges, std::vector<int>& proposeTimes);

    static std::int32_t s_tBaseSiegeTime;
    static std::int32_t s_nMaxConcurrentSiege;
    static RWLock s_lock;
    static std::vector<CCastle*> s_lstCastle;
    static std::set<int> s_setReservableSiegeTime;
    static std::multimap<int, int> s_mmapReservedSiegeTime;
    static std::vector<std::int32_t> s_lstWday;
    static std::vector<std::int32_t> s_lstHour;

    ReservedPledgeMap m_mapAttackerReservedPledge; // +0x490
    ReservedPledgeMap m_mapDefenderReservedPledge; // +0x4b0
    bool m_bAttackerListCached; // +0x4d0
    std::byte m_alignment4D1To4D4[0x03];
    std::int32_t m_tAttackerListLastCached; // +0x4d4
    bool m_bDefenderListCached; // +0x4d8
    std::byte m_alignment4D9To4DC[0x03];
    std::int32_t m_tDefenderListLastCached; // +0x4dc
    std::int32_t m_nAttackerListTotalPage; // +0x4e0
    std::int32_t m_nDefenderListTotalPage; // +0x4e4
    BufForCaching m_bufForAttackerListCaching[5]; // +0x4e8
    BufForCaching m_bufForDefenderListCaching[5]; // +0xa510
    std::vector<std::int32_t> m_lstTempReservedPledgeWorldId; // +0x14538
    std::vector<std::int32_t> m_lstTempReservedPledgeDbId; // +0x14558
    std::vector<std::int32_t> m_lstTempReservedProposeTime; // +0x14578
    std::vector<std::int32_t> m_lstTempReservedProposeStatus; // +0x14598
    std::set<int> m_SiegeGolemSummonerSet; // +0x145b8
    std::vector<CControlTower*> m_lstLifeControlTower; // +0x145d8
    const wchar_t* m_MusicName; // +0x145f8
    FVector m_EditorPos; // +0x14600
    EventControllerMap m_mapEventController; // +0x14618
    DomainFortressSet m_sDomainFortress; // +0x14638
    DomainFortressSet m_sBoundaryFortress; // +0x14658
    DomainFortressSet m_sAllDomainFortress; // +0x14678
    std::int32_t m_nTotalNumDomainFortress; // +0x14698
    std::byte m_alignment1469CTo146A0[0x04];
    CDominion* m_pDominion; // +0x146a0
    std::int32_t m_nPrevDominionStatus; // +0x146a8
    bool m_bSupportBroken; // +0x146ac
    std::byte m_alignment146ADTo146B0[0x03];
    std::int32_t m_tPreviousSiegeEnd; // +0x146b0
    std::int32_t m_nSiegeInform; // +0x146b4
    std::int32_t m_nDbSaveTick; // +0x146b8
    std::int32_t m_tPossessed; // +0x146bc
    std::int32_t m_tPaused; // +0x146c0
    bool m_bPausable; // +0x146c4
    std::byte m_alignment146C5To146C8[0x03];
    std::int32_t m_nValidate; // +0x146c8
    std::int32_t m_nValidateWaiting; // +0x146cc
    CPledge* m_pNewOwnerPledge; // +0x146d0
    CastleType::Enum m_eNewResidenceType; // +0x146d8
    bool m_bHappenDestructTowerEvent; // +0x146dc
    std::byte m_alignment146DDTo146E0[0x03];
    std::int32_t m_PreviousOwnerPledgeDbId; // +0x146e0
    std::int32_t m_nPrevOwnerPledgeWorldId; // +0x146e4
    volatile std::int64_t m_nAttackerScore; // +0x146e8
    volatile std::int64_t m_nDefenderScore; // +0x146f0
};
