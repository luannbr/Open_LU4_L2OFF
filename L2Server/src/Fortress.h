#pragma once

#include "Residence.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

class CCastle;
class CDBSocket;
class CDxResetFortressFacility;
class CDxSetFortressFacility;
class CFortress;
class CFortressFlagpole;
class CFortressNpcAIEventHandler;
class CFortressNpcMakerEventHandler;
class CFortressSiegeRegistry;
class CItem;
class CPledge;
class CSocket;
class CUserSocket;
class User;
enum eSMsgType : std::int32_t;

namespace BuilderCmd {
bool _set_castle_owner(User*, CUserSocket*, CResidence*, char16_t*, int);
}

enum FortressLocationType : std::int32_t {
    FLT_TERRITORY_DOMAIN = 0,
    FLT_TERRITORY_BOUNDARY = 1,
    FLT_TERRITORY_MAX = 2,
};

enum FortressPledgeNameValueChange : std::int32_t {
    FPNVC_NONE = 0,
    FPNVC_BASE_GAIN = 10000,
    FPNVC_PARENT_CASTLE_DEDUCT = -35,
    FPNVC_MAX = -34,
};

enum FortressSiegeResumeType : std::int32_t {
    FSRT_SERVER_DOWN = 1,
    FSRT_NPC_SERVER_DOWN = 2,
    FSRT_MAX = 3,
};

enum FortressSiegeStatus : std::int32_t {
    FSS_NONE = 0,
    FSS_BARRACK_CAPTURE = 1,
    FSS_FLAG_CAPTURE = 2,
    FSS_MAX = 3,
};

// The spellings are donor-selected source vocabulary. Current parsing,
// storage and siege construction close both values and the exclusive bound.
enum FortressScaleType : std::int32_t {
    FST_SMALL = 0,
    FST_LARGE = 1,
    FST_MAX = 2,
};

enum FortressContractStatus : std::int32_t {
    FCS_NONE = 0,
    FCS_INDEPENDENT = -1,
    FCS_CASTLE_CONTRACT = 1,
    FCS_MAX = 2,
};

class CFortressBuilderCmdHandler {
public:
    static bool Handle(CSocket* socket, User* user, char16_t* command);

protected:
    using FortressBuilderCmdFunc = bool (*)(CSocket*, User*, char16_t*);

    struct stFortressBuilderCmdMap {
        const char16_t* pszCmd;
        FortressBuilderCmdFunc pFunc;
    };


    static stFortressBuilderCmdMap m_CmdMap[30];

    static bool BuilderCMd_SetOwnMinutes(CSocket*, User*, char16_t*);
    static bool BuilderCMd_GetOwnMinutes(CSocket*, User*, char16_t*);
    static bool BuilderCmd_AllBarracksCaptured(CSocket*, User*, char16_t*);
    static bool BuilderCmd_FlagCaptured(CSocket*, User*, char16_t*);
    static bool BuilderCmd_SetTreasureLevel(CSocket*, User*, char16_t*);
    static bool BuilderCmd_SetRewardCycleCount(CSocket*, User*, char16_t*);
    static bool BuilderCmd_ShowInfo(CSocket*, User*, char16_t*);
    static bool BuilderCmd_ContractCastle(CSocket*, User*, char16_t*);
    static bool BuilderCmd_SiegeUnregister(CSocket*, User*, char16_t*);
    static bool BuilderCmd_ResetOwner(CSocket*, User*, char16_t*);
    static bool BuilderCmd_SetOwner(CSocket*, User*, char16_t*);
    static bool BuilderCmd_SiegeRegister(CSocket*, User*, char16_t*);
    static bool BuilderCmd_ShowSiegeRegistery(CSocket*, User*, char16_t*);
    static bool BuilderCmd_SiegeStandby(CSocket*, User*, char16_t*);
    static bool BuilderCmd_SiegeStart(CSocket*, User*, char16_t*);
};

class CFortressDBEventHandler {
public:
    static bool ReplyLoadFortress(CDBSocket* socket, const unsigned char* packet);
    static bool ReplyLoadFortressSiege(CDBSocket* socket, const unsigned char* packet);
    static bool ReplyLoadFortressSiegeRegistry(CDBSocket* socket, const unsigned char* packet);
    static bool ReplyPayFortressRent(CDBSocket* socket, const unsigned char* packet);
};


struct FortressSiegeResumeInfo {
    FortressSiegeResumeInfo() = default;

    std::int32_t tSiegeStartTime = 0; // +0x00
    std::int32_t tSiegeElapsedTime = 0; // +0x04
    FortressSiegeStatus eSiegeStatus = FSS_NONE; // +0x08
    bool bIsValidInfo = false; // +0x0c
    std::array<std::byte, 0x03> m_alignment0dTo10{};
};


struct FortressSiegeResult {
    FortressSiegeResult() { nWinnerPledgeDBId = -1; }

    int nWinnerPledgeDBId; // +0x00
};

static_assert(std::is_trivially_copyable_v<FortressSiegeResult>);
static_assert(std::is_trivially_destructible_v<FortressSiegeResult>);
static_assert(std::is_default_constructible_v<FortressSiegeResult>);
static_assert(!std::is_trivially_default_constructible_v<FortressSiegeResult>);

// Exact current 0x30 registry envelope. The current image places the
// 0x20 attacker-id tree first and its CYieldLock at +0x20.
class CFortressSiegeRegistry {
public:
    bool RegisterPledge(std::uint32_t pledgeId);
    bool IsRegisteredPledge(std::uint32_t pledgeId);
    bool UnregisterPledge(std::uint32_t pledgeId);
    void BroadcastSystemMessageW(int messageId, int value);

protected:
    void GetRegisteredPledgeList(std::vector<CPledge*>& pledges);

private:
    friend class CFortress;
    friend class CFortressBuilderCmdHandler;
    friend class CFortressSiege;
    friend class CDxResetFortressFacility;
    friend class CDxSetFortressFacility;

    std::set<std::uint32_t> m_sAttackerPledgeSet; // +0x00
    CYieldLock m_AttackerPledgeSetLock; // +0x20
};


// Exact current physical envelope for the complete Fortress.cpp siege owner.
class CFortressSiege : public CIOObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CFortressSiege)

    CFortressSiege(
        int fortressId,
        FortressScaleType scale,
        CFortressSiegeRegistry* registry,
        CFortressNpcMakerEventHandler* makerEventHandler,
        CFortressNpcAIEventHandler* aiEventHandler,
        bool dominionSiege
    );
    ~CFortressSiege() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void TimerExpired(int id) override;

    void StartTimer();
    void Start();
    bool Resume(FortressSiegeResumeType resumeType, FortressSiegeResumeInfo resumeInfo);
    void End();
    void Pause();
    void SetProtectedNpcDie();
    void BarrackCaptured(unsigned int barrackIndex);
    bool FlagCaptured(unsigned int pledgeDatabaseId);
    void FlagCaptureWarn(unsigned int pledgeDatabaseId, User* user);
    void PushFlagItem(CItem* item, const FVector& position);

protected:
    void SetAllBarracksCaptured();
    bool GetBarrackStatusList(std::vector<bool>& statusList);
    int GetNumBarracksCaptured();
    void SetDoorDestroyed(const wchar_t* doorName);

private:
    friend class CFortress;
    friend class CFortressBuilderCmdHandler;

    CFortress* GetFortress();
    void AddSiegeLog(wchar_t* format, int logId, FortressSiegeStatus status);

    void InitSiege(bool resume);
    void FinSiege();
    void SetStatus(FortressSiegeStatus status);
    void SetDefender(bool underSiege);
    void SetAttacker(bool underSiege);
    void SetMembersInSiege();
    void SetMembersNotInSiege();
    void StartBarrackCapture(bool restart);
    void StartFlagCapture(bool createFlags);
    void ResetBarracksCaptured();
    void CreateAllFlags();
    void DeleteAllFlags();
    void BroadcastSystemMessageW(int messageId);
    void BroadcastSystemMessageW(int messageId, int value);

    bool IsMyFlag(CItem* item);

    int GetNumFlagInWorld();
    void BroadcastSystemMessageW(int messageId, eSMsgType type, const char16_t* text);
    void FlagPicked(User* user, CItem* item);

    void DeleteFlag(User* user, CItem* item, bool notify);

    FortressSiegeStatus m_eSiegeStatus; // +0x018
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignment1cTo20{};
    std::vector<bool> m_vBarrackCaptured; // +0x020
    CYieldLock m_BarrackCapturedLock; // +0x048
    std::int32_t m_nNumBarracks; // +0x058
    CYieldLock m_FortressSiegeLock; // +0x05c
    bool m_bFlagCaptured; // +0x06c
    bool m_bAllBarracksCaptured; // +0x06d
    bool m_bFirstFlagCreation; // +0x06e
    [[maybe_unused]] std::byte m_alignment6fTo70{};
    std::uint32_t m_nWinnerPledgeDbId; // +0x070
    bool m_bPaused; // +0x074
    [[maybe_unused]] std::array<std::byte, 0x03> m_alignment75To78{};
    std::int32_t m_nFortressId; // +0x078
    bool m_bIsDominionSiege; // +0x07c
    [[maybe_unused]] std::array<std::byte, 0x03> m_alignment7dTo80{};
    CFortressSiegeRegistry* m_pRegistry; // +0x080
    std::int32_t m_tSiegeStartTime; // +0x088
    std::int32_t m_tFirstBarrackCaptureTime; // +0x08c
    std::int32_t m_tSiegeElapsedTime; // +0x090
    std::int32_t m_nSiegeElapsedTimeSaveTick; // +0x094
    std::map<std::uint32_t, std::pair<FVector, std::uint32_t>> m_mFlagMap; // +0x098
    std::int32_t m_nNumFlags; // +0x0b8
    [[maybe_unused]] std::array<std::byte, 0x04> m_alignmentBcToC0{};
    wchar_t* m_sCurrentMusicName; // +0x0c0
    CFortressNpcMakerEventHandler* m_pNpcMakerEventHandler; // +0x0c8
    CFortressNpcAIEventHandler* m_pNpcAIEventHandler; // +0x0d0

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CFortressSiege)
};


// Current L2Server allocation and constructor fix the complete 0x560 extent.
class CFortress : public CResidence {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CFortress)

    CFortress();
    ~CFortress() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()
    static void InitFortress();
    static void StartAllTimer();

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void TimerExpired(int id) override;

    void StartTimer() override;
    bool IsRegisteredForSiege(CPledge* pledge) const override;
    bool IsUnderSiege() const override;
    bool IsUnderDominionSiege() const override;
    bool IsInSiege() const;
    bool IsFortress() const override;
    CFortress* CastFortress() const override;
    int GetDominionSiegeID() const override;

    CFortressSiege* GetSiege();
    CFortressSiegeRegistry* GetSiegeRegistry();
    int GetNumBarracks();
    int GetNumBarracksCapturedInSiege();
    void SendMapInfo(User* user);
    FortressContractStatus GetContractStatus() const;
    int GetParentCastleId() const;
    bool CanFlagBePicked(User* user, CPledge* pledge, CItem* item, bool force);
    static void HandleDeleteFlag(User* user, bool notify);
    static bool HandleFlagPicked(CFortress* fortress, User* user, CItem* item);
    void SetScale(FortressScaleType scale) { m_eScale = scale; }
    static void SetFlagItemId(int classId) { m_nFlagItemClassId = classId; }
    void AddFlag(FVector& position);
    void AddRelatedCastle(int residenceId);
    void AddToRelatedCastles();
    void SetFlagpole(CFortressFlagpole* flagpole);
    void ResetOwnership();
    void ResetSiegeInfo();
    void RecieveNpcEvent(const unsigned char* packet);
    int RegisterSiege(unsigned int pledgeId);
    int UnregisterSiege(unsigned int pledgeId);
    void HandleDoorDestroyed(const wchar_t* doorName);

    void BroadcastSystemMessageW(int messageId, eSMsgType type, const char16_t* text, bool includeOwner);
    void BroadcastSystemMessageW(int messageId, bool includeOwner);
    void BroadcastSystemMessageWithName(int messageId, bool includeOwner);

protected:
    void SetOwnerRewardCycleCount(int count);
    void SetTreasureLevel(int level);
    int GetFacilityLevel(int facility) const;
    void GetFlags(std::vector<FVector>& flags);

private:
    friend class CCastle;
    friend class CDoor;
    friend class CDominion;
    friend class CFortressDBEventHandler;
    friend class CFortressBuilderCmdHandler;
    friend class CFortressNpcAIEventHandler;
    friend class CFortressSiege;
    friend class CDxResetFortressFacility;
    friend class CDxSetFortressFacility;
    friend bool BuilderCmd::_set_castle_owner(
        User* user, CUserSocket* socket, CResidence* residence, char16_t* pledgeName, int castleType
    );

    void AddInfoLog();
    void AddContractLog(int previousStatus, int castleId);
    void AddOwnerRewardTakenLog(User* user, int previousRewardCycle, int rewardValue, std::int64_t rewardAmount);
    void AddCastleTreasureTakenLog(User* user, int previousTreasureLevel);
    void Init();
    void ChangeResidenceStateCurrent(ResidenceState state);
    bool HasMoreThan3DayAndDelta();
    bool HasMoreThan75Hours();
    void GiveOwnerReward();
    void PayFortressRent();
    void HandleReplyPayFortressRent(int result);
    bool DeductParentCastlePledgeNameValue();
    void IncrementTreasureLevel();
    void UpgradeTreasure();
    void UpdatePledgeNameValue(CPledge* pledge, FortressPledgeNameValueChange change);
    void ResetFacilityLevel();
    void SetFacilityLevel(int facility, int level);
    void SendResetFacilityLevelToCacheD();
    void SiegeCancel();
    bool SiegeStandby();
    void SiegeResume();
    void SiegeEnd(FortressSiegeResult result);
    void BanPledgesInFortress();
    void BanExceptOwnerPledge();
    void BroadcastStandbyCountDown(int remainingSeconds);
    void HandleSiegeResult(FortressSiegeResult result);
    void MaintainOwnership();
    void SetOwnerInfo(CPledge* pledge);
    void SetOwnership(CPledge* pledge, bool updateNameValue);
    void ChangeOwnership(CPledge* pledge, bool updateNameValue);
    void ShowOwnerFlagPole();
    void SetOwnerPledge(CPledge* pledge);
    void SetLastRewardGivenTime(std::int32_t time);
    void SetStateInfoFromDB(int state, int stateChangedTime, int stateElapsedTime);
    void SetOwnerInfoFromDB(int pledgeId, int lastOwnerChangedTime, int ownerSiegeDefendCount);
    void SetContractInfoFromDB(int pledgeId, int contractStatus, int parentCastleId);
    void SetRewardInfoFromDB(int lastRewardGivenTime, int ownerRewardCycleCount, int castleTreasureLevel);
    void SetSiegeInfoFromDB(int siegeStatus, int siegeStartTime, int siegeElapsedTime, bool validInfo);
    void HandleReplyLoadFortressSiege();
    void HandleReplyLoadFortressSiegeRegistry(std::set<std::uint32_t>& pledgeIds);
    void ResetOwnerInfo();
    void ResetOwnerRewardInfo();
    void ResetOwnerContractInfo();
    void ResetOwnerRewardCycleCount();
    void _ResetTreasureLevel();
    void SetFlagpole();
    void ResetFlagpole();
    void SiegeRegister();
    bool CanRegisterSiege(CPledge* pledge);
    bool IsRelatedCastle(unsigned int residenceId);
    void HandleRegisterSiege(User* user, CNPC* npc);
    void HandleUnregisterSiege(User* user, CNPC* npc);
    int GoIndependent();
    void HandleGoIndependent(User* user, CNPC* npc);
    int ContractWithCastle(CCastle* castle);
    void HandleContractWithCastle(User* user, CNPC* npc, CCastle* castle);
    void BroadcastSystemMessageWithName(CPledge* pledge, int messageId);
    void BroadcastSystemMessageWithName(int messageId, eSMsgType type, const char16_t* text, bool includeOwner);
    bool OnAIEvent(int eventId, int npcSid, int targetSid, int value, std::int64_t amount);
    bool SiegeStart();
    void NotifyDominionSiegeEnd();

    void StartSiege() override;
    void EndSiege() override;
    void PassSiege() override;
    void ChangeOwner(CPledge* pledge, int reason, bool announce) override;
    void PauseSiege() override;
    void ResumeSiege() override;
    void ExpelNonPledgeMember() override;
    void OnStartInBattleField(User* user, FVector& position, bool& started) override;

    FortressLocationType m_eLocation; // +0x490
    FortressScaleType m_eScale; // +0x494
    FortressContractStatus m_eContractStatus; // +0x498
    CYieldLock m_FortressLock; // +0x49c..+0x4ab
    std::array<std::byte, 0x04> m_alignment4acTo4b0; // +0x4ac..+0x4af
    CCastle* m_pParentCastle; // +0x4b0
    std::set<std::uint32_t> m_sRelatedCastleList; // +0x4b8..+0x4d7
    CFortressSiegeRegistry* m_pSiegeRegistry; // +0x4d8
    CFortressSiege* m_pSiege; // +0x4e0
    FortressSiegeResumeInfo m_stSiegeResumeInfo; // +0x4e8
    CFortressFlagpole* m_pFlagpole; // +0x4f8
    FVector m_vFlagpolePosition; // +0x500

    struct FortressFacilityInfo {
        std::int32_t m_nFacilityType;
        std::int32_t m_nMaxLevel;
    };

    static std::int32_t m_nFlagItemClassId;
    static const std::array<FortressFacilityInfo, 5> m_mFacilityMap;

    std::vector<FVector> m_vFlagPositionList; // +0x518..+0x52f
    std::array<std::byte, 0x08> m_currentOnlyState530To538; // +0x530..+0x537
    std::int32_t m_tStateChangedTime; // +0x538
    std::int32_t m_tStateElapsedTime; // +0x53c
    std::int32_t m_nOwnerSiegeDefendCount; // +0x540
    bool m_bWaitResume; // +0x544
    bool m_bWaitLoading; // +0x545
    bool m_bWaitNpcServerUp; // +0x546
    std::byte m_alignment547To548; // +0x547
    CFortressNpcMakerEventHandler* m_pNpcMakerEventHandler; // +0x548
    CFortressNpcAIEventHandler* m_pNpcAIEventHandler; // +0x550
    std::int32_t m_nDominionSiegeID; // +0x558
    std::int32_t m_tLastCountDownTime; // +0x55c

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CFortress)
};
