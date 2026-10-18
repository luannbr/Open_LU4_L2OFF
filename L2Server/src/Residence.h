#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/IOC.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/ResidenceData.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/Territory.h"
#include "Shared/src/Utils.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

class User;
class CResidence;
class CResidenceDecoVisible;
class CControlTower;
class CDoor;
class CHolyThing;
class CSignBoard;
struct OwningNPCData;
template <typename TObject>
class CSPointer;

namespace TerritoryType {

enum Enum : std::int32_t {
    OwnerRestart = 0,
    Residence = 1,
    BattleCamp = 2,
    BattleField = 3,
    Max = 4,
};

} // namespace TerritoryType

namespace ResidenceUtil {
namespace DecoNpcSetReason {

enum Type : std::int32_t {
    Set = 0,
    Reset = 1,
    Expire = 2,
    ChangeOwner = 3,
};

} // namespace DecoNpcSetReason

void SendDecoNpcSet(CSPointer<User> user, int result, int agitId, int slot, int decoCID, std::int32_t expireTime);
void SendDecoNpcSetForReset(CSPointer<User> user, int result, int agitId, int slot);
void WriteDecoNpcSetLog(
    int logId,
    int actorDbId,
    int actorAccountId,
    const FVector& position,
    int agitId,
    int pledgeDbId,
    int slot,
    int decoNpcId,
    std::int32_t expireTime,
    DecoNpcSetReason::Type reason
);
const unsigned char* DisassembleDecoNpc(const unsigned char* packet, int count, CResidence* residence);
bool HasRightToSetDecoNpc(CResidence* residence, CSPointer<User> user);
} // namespace ResidenceUtil

enum ResidenceGuardTypeEnum {
    RGT_TELEPORTER = 0,
    RGT_WIZARD = 1,
    RGT_CLERIC = 2,
    RGT_SWORD = 3,
    RGT_POLE = 4,
    RGT_BOW = 5,
    RGT_MAX = 6,
};

bool operator<(const FVector& left, const FVector& right);

struct ResidenceGuardInfo {
    ResidenceGuardInfo(
        ResidenceGuardTypeEnum guardType, bool moveable, unsigned int npcId, unsigned int maxNum, char ssqType
    ) :
        nGuardType(guardType), bMoveable(moveable), nNpcId(npcId), nMaxNum(maxNum), cSSQType(ssqType) {}

    ResidenceGuardTypeEnum nGuardType; // +0x00
    bool bMoveable; // +0x04
    unsigned int nNpcId; // +0x08
    unsigned int nMaxNum; // +0x0c
    char cSSQType; // +0x10
};

struct ResidenceGuardData {
    ResidenceGuardData(
        unsigned int itemId,
        ResidenceGuardTypeEnum guardType,
        bool moveable,
        FAngle angle,
        unsigned int npcId,
        unsigned int itemServerId
    ) :
        nItemId(itemId),
        nGuardType(guardType),
        bMoveable(moveable),
        aAngle(angle),
        nNpcId(npcId),
        nItemServerId(itemServerId) {}

    unsigned int nItemId; // +0x00
    ResidenceGuardTypeEnum nGuardType; // +0x04
    bool bMoveable; // +0x08
    std::byte m_alignment09To0C[0x03]; // +0x09..+0x0b
    FAngle aAngle; // +0x0c
    unsigned int nNpcId; // +0x10
    unsigned int nItemServerId; // +0x14
};

static_assert(std::is_trivially_copyable_v<ResidenceGuardInfo>);
static_assert(std::is_trivially_destructible_v<ResidenceGuardInfo>);

struct CastleSiegeMusicRecord {
    int StartTime; // +0x00
    int PlayTime; // +0x04
    const wchar_t* MusicName; // +0x08
};

static_assert(std::is_aggregate_v<CastleSiegeMusicRecord>);
static_assert(std::is_trivially_default_constructible_v<CastleSiegeMusicRecord>);
static_assert(std::is_trivially_copyable_v<CastleSiegeMusicRecord>);
static_assert(std::is_trivially_destructible_v<CastleSiegeMusicRecord>);

// Current L2Server constructs two adjacent instances with a 0x38 stride at
// 0x2013e930. The trailing 0x20 member is the current debug-capable vector
// carrier. The older TotalChecker owner used the smaller 0x18 carrier.
class CastleSiegeMusicPlayer : public CIOObject {
public:
    CastleSiegeMusicPlayer();
    ~CastleSiegeMusicPlayer() override;

    void TimerExpired(int id) override;
    const wchar_t* GetMusicName(int elapsedTime) const;

    std::vector<CastleSiegeMusicRecord> m_MusicData; // +0x18
};


extern CastleSiegeMusicPlayer g_CastleSiegeMusicPlayer[2];

class CAuctionAgit;
class CBaseCampAgit;
class CCastle;
class CCompeteAgit;
class CDominion;
class CDominionSiegeEntry;
class CFortress;
class CManor;
class CMiniGameAgit;
class CNPC;
class CNpcBattleAgit;
class CPledge;
class CSiegeAgit;
class CSkillInfo;
class CTeamBattleAgit;
class CTerritory;
class CWorldAgit;

// Current castledata records. The fixed UTF-16 buffers and pointer lanes are
// consumed directly by CParserForResidence and CResidence.
struct OwningNPCData {
    char16_t pszName[25]; // +0x00..+0x31
    bool bFunctional; // +0x32
    std::byte m_alignment33To38[0x05];
    CNPC* pNPC; // +0x38
};


// Current L2Server CResidence vtable ends at +0x138. The concrete data
// members have not all been named yet, but the current CWorldAgit constructor
// proves that the complete base extent ends at +0x490.
class CResidence : public CIOObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CResidence)

    CResidence();
    ~CResidence() override;

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    virtual bool IsCastle() const;
    virtual CCastle* CastCastle() const;
    virtual bool IsExtended() const;
    virtual int GetDominionSiegeID() const;
    virtual bool IsUnderDominionSiege() const;
    virtual bool IsWorldAgit() const;
    virtual CWorldAgit* CastWorldAgit() const;
    virtual bool IsFortress() const;
    virtual CFortress* CastFortress() const;
    virtual bool IsDominion() const;
    virtual CDominion* CastDominion() const;
    virtual CResidence* CastResidence() const;
    virtual bool IsUnderSiege() const;
    virtual void ChangeTaxRate();
    virtual void StartTimer() = 0;
    virtual void StartSiege() = 0;
    virtual void EndSiege() = 0;
    virtual void PassSiege() = 0;
    virtual void ChangeOwner(CPledge* pledge, int reason, bool announce) = 0;
    virtual void PauseSiege() = 0;
    virtual void ResumeSiege() = 0;
    virtual bool IsRegisteredForSiege(CPledge* pledge) const;
    virtual void ExpelNonPledgeMember() = 0;
    virtual void MakeDoorsBreakable(bool breakable);
    virtual void MakeHolyThingBreakable(bool breakable);
    virtual void MakeNPCDisable(bool disabled);
    virtual void MakeDoorsOpenClose(int state);
    virtual void OnStartInBattleField(User* user, FVector& position, bool& started);

    bool IsInBattleField(const FVector& position, bool includeZ);
    bool IsInResidence(const FVector& position, bool includeZ);
    bool IsInBattleCampZone(const FVector& position);
    void ShowTerritories(CCreature& creature, TerritoryType::Enum type);
    void GoTerritory(CCreature& creature, TerritoryType::Enum type);
    void SetBattleField(bool enabled);
    void SetData(CResidenceData* data);
    int GetResidenceID() const { return m_data != nullptr ? m_data->m_nResidenceID : 0; }
    int GetResidenceOwnerID() const { return m_data != nullptr ? static_cast<int>(m_data->m_nResidenceOwnerID) : 0; }
    bool IsOwnerPledge(int pledgeId);
    int GetResidenceOwnerWorldID() const { return m_data != nullptr ? m_data->m_nResidenceOwnerWorldId : 0; }
    ResidenceState GetResidenceState() const { return m_data != nullptr ? m_data->m_ResidenceState : RS_MAX; }
    void SetName(const char16_t* name);
    void SetResidenceID(int residenceId);
    void Set(
        int hpStove, int hpStoveExpire, int mpFlame, int mpFlameExpire, int teleportLevel, int teleportExpire, int hatcherId
    );
    void InstallHpStove(int level, int expireTime);
    void InstallMpFlame(int level, int expireTime);
    void InstallHatcher(int hatcherId);
    void InstallTeleport(int level, int expireTime);
    void SetResidenceGrade(int grade);
    void AddResidenceDomain(int domainId);
    void AddDecoNpcSlot(const FVector& position, int yaw);
    static CResidence* GetBattleFieldResidence(const FVector& position);
    void IsInAllAuctions(int pledgeId, int& residenceId, int& auctionId, int& flags);
    void CheckAttendedAuction(int pledgeId);
    void ReserveExpelOfUsers(std::vector<User*>& users);
    void BanNonDominionUsers(CDominionSiegeEntry* entry);
    void SetFreezeField(bool enabled);
    void SummonMercenary(FVector position, ResidenceGuardData* guardData);
    void LoadGuard();
    static bool IsSiegeRegistered(CPledge* pledge);
    bool IsDefendingPledge(int pledgeId, int worldId);
    bool IsOffendingPledge(int pledgeId, int worldId);
    bool SetDecoNpc(int agitSlot, int npcSid);
    void ResetDecoNpc(int slot);
    CPledge* GetResidenceOwnerPledge();
    void AddResidenceSkill(CPledge& pledge, CastleType::Enum castleType);
    void RemoveResidenceSkill(CPledge& pledge);
    void SetPledge(CPledge* pledge);
    void SetAllNPCPledge(int pledgeId);
    void AddManor(int manorId);
    void DelManor(int manorId);
    bool HaveManor(int manorId);
    CManor* GetManor(int manorId);
    void SetNextCropInfo(
        int npcIndex,
        int userIndex,
        int residenceId,
        int manorId,
        int cropId,
        std::int64_t count,
        std::int64_t price,
        int type
    );
    void SetNextSeedInfo(
        int npcIndex, int userIndex, int residenceId, int manorId, int seedId, std::int64_t count, std::int64_t price
    );
    void ShowSellSeedList(CNPC& npc, User& user, int residenceId, int manorId);
    void SetTaxRates(int taxRate, CastleType::Enum castleType);
    void SetChildTaxRates(int taxRate, CastleType::Enum castleType);
    void SetMaxChangeRateToAllManor(double rate);
    void SendDB(const char* format, ...) const;
    void ReadLock(const wchar_t* file, int line);
    void ReadUnlock();
    void WriteLock(const wchar_t* file, int line);
    void WriteUnlock();
    void SetCampaignResult(bool success);
    void BroadcastSystemMessageW(int messageId);
    void BroadcastSystemMessageW(const char16_t* text, int messageId);
    void SendDecoInfo(CUserSocket* socket);
    void ValidateDeco();
    void DoDecoCheck(std::uint32_t currentTime);
    void DoDecoNpcCheck(std::int32_t currentTime);
    void OnDecoSet();
    void BroadcastDominionMessage(int messageType);
    void ChangeResidenceState(ResidenceState state);
    void SetNPCPledge(CNPC* npc, int pledgeId);
    void RollbackChangeTaxIncome(std::int64_t income);
    void RollbackDepositToSafe(std::int64_t amount);
    bool ChangeTaxIncome(std::int64_t income);
    bool DepositToSafe(std::int64_t amount);
    void ResetVault(int resetIncome);
    void OpenObserver(User* user);
    void SaveIncome(bool isCastle, int reason);
    void ReportShopTax(std::int64_t amount, CastleType::Enum castleType, bool isSell);
    void TributeToParentResidence(std::uint32_t amount, CastleType::Enum castleType, bool isSell);
    void SendResidenceTaxIncome(int mode);

    CResidenceData* m_data; // +0x018
    bool m_bPaused; // +0x020
    bool m_bSiegeContinued; // +0x021
    bool m_bMidVictory; // +0x022
    std::byte m_alignment23To24; // +0x023
    std::uint32_t m_nSiegePeriod; // +0x024
    std::int32_t m_nLostTime; // +0x028
    std::int32_t m_nSiegeElapsed; // +0x02c
    std::int32_t m_nResidenceOwnerPledgeServerId; // +0x030
    bool m_bIsDominionSiege; // +0x034
    std::byte m_alignment35To38[0x03]; // +0x035..+0x037
    std::int32_t m_nWorldId; // +0x038
    std::byte m_alignment3CTo40[0x04]; // +0x03c..+0x03f
    CTerritory m_OwnerRestartTerritory; // +0x040..+0x0b7
    std::vector<CTerritory*> m_lstResidenceTerritory; // +0x0b8..+0x0d7
    std::vector<CTerritory*> m_lstBattleCampTerritory; // +0x0d8..+0x0f7
    std::vector<CTerritory*> m_lstBattleFieldTerritory; // +0x0f8..+0x117
    char16_t m_pszHolyThingName[25]; // +0x118..+0x149
    std::byte m_alignment14ATo150[0x06]; // +0x14a..+0x14f
    std::vector<FVector> m_OwnerRestartPointList; // +0x150..+0x16f
    std::vector<FVector> m_OtherRestartVillageList; // +0x170..+0x18f
    std::vector<FVector> m_BanishPointList; // +0x190..+0x1af
    std::vector<FVector> m_ChaoRestartPointList; // +0x1b0..+0x1cf
    std::vector<FVector> m_ChallengerRestartPointList; // +0x1d0..+0x1ef
    std::vector<FVector> m_AgitEntrancePointList; // +0x1f0..+0x20f
    std::vector<int> m_AgitDeleteItemList; // +0x210..+0x22f
    std::vector<char16_t*> m_BulletinNameList; // +0x230..+0x24f
    std::vector<CDoor*> m_DoorList; // +0x250..+0x26f
    std::vector<CControlTower*> m_ControlTowerList; // +0x270..+0x28f
    std::vector<char16_t*> m_GateNameList; // +0x290..+0x2af
    std::vector<char16_t*> m_ControlTowerNameList; // +0x2b0..+0x2cf
    std::vector<char16_t*> m_BossNpcList; // +0x2d0..+0x2ef
    std::vector<OwningNPCData*> m_OwningNPCList; // +0x2f0..+0x30f
    std::vector<std::uint32_t> m_OwningNPCServerIdList; // +0x310..+0x32f
    std::vector<std::uint32_t> m_AllOwningNPCServerIdList; // +0x330..+0x34f
    std::vector<CHolyThing*> m_HolyThingList; // +0x350..+0x36f
    std::map<std::uint32_t, ResidenceGuardInfo*> m_mapResidenceGuardItem; // +0x370..+0x38f
    std::map<FVector, ResidenceGuardData*> m_mapResidenceGuardData; // +0x390..+0x3af
    std::map<FVector, ResidenceGuardTypeEnum> m_mapResidenceGuardDataCheck; // +0x3b0..+0x3cf
    std::map<int, int> m_GuardNumMap; // +0x3d0..+0x3ef
    CSignBoard* m_pObserver; // +0x3f0
    AgitDecoEffect m_AgitDecoEffect; // +0x3f8..+0x40f
    std::uint32_t m_nDecoFunc; // +0x410
    bool m_bDecoCheck; // +0x414
    std::byte m_alignment415To418[0x03]; // +0x415..+0x417
    CResidenceDecoVisible* m_pDecoVisible; // +0x418
    std::vector<const CSkillInfo*> m_ResidenceSkills[3]; // +0x420..+0x47f
    JobTransactionLock m_decoNpcJobLock; // +0x480..+0x48b
    std::int32_t m_tLastDecoNpcCheck; // +0x48c
};


class CResidenceDB {
public:
    CResidenceDB();
    ~CResidenceDB();

    CResidenceData* InitializeResidenceData(int residenceId);
    void AddResidence(const char16_t* name, CResidence* residence);
    void Load();
    void ResidenceGuardLoad();

    CResidence* GetResidence(int residenceId);
    CResidence* GetResidence(const char16_t* residenceName);
    int GetCastlePledgeList(std::vector<int>& pledgeIds);
    CResidence* GetWhoseBattleCampZone(const FVector& position);
    CResidence* GetWhoseBattleFieldWithBattleCampZone(const FVector& position);
    bool CanUseSwoopCannonInDominionSiege(const FVector& position, bool includeZ);
    CDominion* GetDominion(int residenceId);
    void SendResidenceTaxIncome();
    bool IsSiegeAgit(int residenceId);
    void SetNowTalkingMessenger(User* user, CNPC* npc);
    bool CheckTalkableDistanceToMessenger(User* user, int residenceId);
    int VerifyMaxRateOfCastle();

    std::map<std::u16string, CResidence*, runtime::CStringLess<std::u16string>> m_ResidenceMap; // +0x00
    std::map<int, CResidence*> m_ResidenceIDMap; // +0x20
    CYieldLock m_ResidenceMapLock; // +0x40

private:
    std::vector<CResidence*> m_lstResidenceWithBattleCampZone; // +0x50
    std::set<int> m_setSiegeAgit; // +0x70
    CYieldLock m_MessengerMapLock; // +0x90
    std::map<int, int> m_mapUserToMessengerLastTalked; // +0xa0
};


class CExpelTimer final : public CIOObject {
public:
    CExpelTimer();
    ~CExpelTimer() override;

    L2_DECLARE_OBJECT_TYPE(CExpelTimer)

    void WriteLock(const wchar_t* callerFile, int callerLine);
    void WriteUnlock();
    void ReserveExpelWithLock(User* user, FVector position);
    void ReserveExpelWithNoLock(User* user, FVector position);
    void ReserveCommandWithLock(int command, FVector position);
    void ReserveCommandWithNoLock(int command, FVector position);

protected:
    void TimerExpired(int id) override;


    RWLock m_Lock; // +0x18
    std::vector<User*, std::allocator<User*>> m_lstUser; // +0x50
    std::vector<FVector, std::allocator<FVector>> m_lstPos; // +0x68
    std::vector<int, std::allocator<int>> m_lstCommand; // +0x80
};


extern CResidenceDB g_ResidenceDB;
extern CExpelTimer g_ExpelTimer;
