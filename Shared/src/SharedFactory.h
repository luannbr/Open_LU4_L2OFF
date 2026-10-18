#pragma once

#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string>
#include <vector>
#include "Shared/src/Runtime/RawAtomic.h"
#include "Protocol/VariableBitSet.h"
#include "Protocol/src/L2Constants.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/ItemConsumeType.h"
#include "Shared/src/ScriptAccessible.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Utils.h"
#include "Shared/src/YieldLock.h"

namespace runtime {
class SharedFactoryMappingAccess;
}

class User;

#if defined(L2_BUILD_L2SERVER)
class CDB;
class CCreature;
class CSkillAction2;
class CNPC;
class CDuel;
class CPet;
class CPledgeGameField;
class CPledgeGamePlayers;
#endif

// These enums are global in the shipping PDB type names and are consumed by
// more than one server process. Keep one physical Shared definition rather
// than creating CacheD-local copies.
// enum ItemType : std::int32_t {
//     IT_NOTITEM = -1,
//     IT_WEAPON = 0,
//     IT_ARMOR = 1,
//     IT_ACCESSARY = 2,
//     IT_QUESTITEM = 3,
//     IT_ASSET = 4,
//     IT_ETCITEM = 5,
// }

enum ItemDefaultActionEnum : std::int32_t {
    ITEM_ACTION_NONE = 0,
    ITEM_ACTION_SKILL_REDUCE = 1,
    ITEM_ACTION_EQUIP = 2,
    ITEM_ACTION_SOULSHOT = 3,
    ITEM_ACTION_SPIRITSHOT = 4,
    ITEM_ACTION_BLESS_SPIRITSHOT = 5,
    ITEM_ACTION_SKILL_MAINTAIN = 6,
    ITEM_ACTION_MAP_OPEN = 7,
    ITEM_ACTION_ROLL_DICE = 8,
    ITEM_ACTION_RECIPE = 9,
    ITEM_ACTION_CAPSULE = 10,
    ITEM_ACTION_CALC = 11,
    ITEM_ACTION_SEED = 12,
    ITEM_ACTION_HARVEST = 13,
    ITEM_ACTION_XMASSEAL_OPEN = 14,
    ITEM_ACTION_SHOW_HTML = 15,
    ITEM_ACTION_SHOW_TUTORIAL = 16,
    ITEM_ACTION_SHOW_SSQ_STATUS = 17,
    ITEM_ACTION_SUMMON_SOULSHOT = 18,
    ITEM_ACTION_SUMMON_SPIRITSHOT = 19,
    ITEM_ACTION_FISHINGSHOT = 20,
    ITEM_ACTION_SHOW_ADVENTURER_GUIDE_BOOK = 21,
    ITEM_ACTION_KEEP_EXP = 22,
    ITEM_ACTION_CALL_SKILL = 23,
    ITEM_ACTION_CREATE_MPCC = 24,
    ITEM_ACTION_PEEL = 25,
    ITEM_ACTION_NICK_COLOR = 26,
    ITEM_ACTION_HIDE_NAME = 27,
    ITEM_ACTION_START_QUEST = 28,
    ITEM_ACTION_REDUCE_ON_SKILL_SUCCESS = 29,
    ITEM_ACTION_BLESS_SOULSHOT = 30,
    ITEM_ACTION_USE_COUNT = 31,
    ITEM_ACTION_MAX = 32,
};


// The enumerator names and values are kept by the matching
// nasc64_2015.pdb. Current Cached.exe confirms every value
// used by CReadFile::GetCrystalType at 0x00523d34. Value 7 remains part of
// the shared enum even though that particular CacheD parser does not accept
// the "s84" spelling.
// enum CrystalType : std::int32_t {
//     CRT_NONE = 0,
//     CRT_D = 1,
//     CRT_C = 2,
//     CRT_B = 3,
//     CRT_A = 4,
//     CRT_S = 5,
//     CRT_S80 = 6,
//     CRT_S84 = 7,
//     CRT_R = 8,
//     CRT_R95 = 9,
//     CRT_R99 = 10,
//     CRT_EVENT = 11,
//     CRT_MAX = 12,
//     CRT_INVALID = -1,
// }

namespace Shot {

// enum Type : std::int32_t {
//     TypeNone = 0,
//     SoulShot = 1,
//     SpiritShot = 2,
//     FishingShot = 3,
//     TypeMax = 4,
// }

// enum Grade : std::int32_t {
//     Invalid = -1,
//     None = 0,
//     D = 1,
//     C = 2,
//     B = 3,
//     A = 4,
//     S = 5,
//     R = 6,
//     GradeMax = 7,
// }

} // namespace Shot


class CCreature;
class GlobalObject;
class CSharedCreatureConstant;

#if !defined(L2_BUILD_L2SERVER)
// The shipping L2NPC image keeps consecutive SharedDefine blocks 0x40 bytes
// apart. Its newer definition removed the donor PDB's obsolete lock_no field.
class CSharedDefine {
public:
    CSharedDefine(
        const wchar_t* dataMMFName,
        const wchar_t* constantMMFName,
        const wchar_t* indexMMFName,
        const wchar_t* mapMMFName,
        int maxData,
        int maxClass,
        const wchar_t* lockName,
        const wchar_t* mapLockName,
        const wchar_t* eachLockName
    );

    const wchar_t* DataMMFName;
    const wchar_t* ConstantMMFName;
    const wchar_t* IndexMMFName;
    const wchar_t* MapMMFName;
    const int max_data;
    const int max_class;
    const wchar_t* lock_name;
    const wchar_t* maplock_name;
    const wchar_t* eachlock_name;
};

extern const CSharedDefine SharedDefine_Alliance;
extern const CSharedDefine SharedDefine_AirShip;
extern const CSharedDefine SharedDefine_Creature;
extern const CSharedDefine SharedDefine_GeneralObject;
extern const CSharedDefine SharedDefine_Item;
extern const CSharedDefine SharedDefine_Party;
extern const CSharedDefine SharedDefine_Pledge;
extern const CSharedDefine SharedDefine_StaticObject;
#endif

class CSharedObject : public ScriptAccessibleEx {
public:
    FVector GetPos() const;
    void SetPos(const FVector& position);
    FAngle GetDir() const;
    int GetDirByInt() const;
    void SetDirByInt(int direction);
    void SetDir(const FAngle& direction);

    FVector m_Pos;

private:
    FAngle m_Dir;

public:
    std::int32_t m_nId;
    std::int32_t index;
    std::int32_t container_index;
    std::int32_t const_index;
    bool m_bIsActive;
    std::int32_t before;
    std::int32_t next;

    void CollectVar(ScriptTypeInfo* typeInfo) override;
    ScriptAccessibleEx* GetScriptAccessibleEx() const override;
};

// Exact 0x3ed8-byte CSharedFactory record. TotalChecker2015.pdb describes
// this target revision completely and L2NPC.exe uses the same stride for
// L2World_CreatureData. The record is addressed in-place. It must not gain
// process-local ownership or cache members.
class CSharedCreatureData : public CSharedObject {
public:
    static constexpr std::size_t kRecordSize = 0x3ed8;

    struct DailyQuestState {
        std::int32_t nQuestID;
        std::int32_t nCompleteTime;
    };

    struct AbnormalData {
        AbnormalStatusSlotType m_Type;
        std::uint8_t m_cAbnormalLevel;
        std::int32_t m_nMagicLevel;
    };

    struct AcquiredPotentialCount {
        std::int32_t nCount1;
        std::int32_t nCount2;
        std::int32_t nCount3;
    };

    CSharedCreatureData();
    ~CSharedCreatureData() override;
    void CollectVar(ScriptTypeInfo* typeInfo) override;

    CSharedCreatureData* Next();
    CSharedCreatureConstant* GetConstant();
    void CopyFrom(CSharedCreatureData* source);
#if defined(L2_BUILD_L2SERVER)
    void SetCollisionRadius(double value) {
        m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_dCollisionRadius, value, 27);
    }
    void SetCollisionHeight(double value) {
        m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_dCollisionHeight, value, 28);
    }
#else
    void SetCollisionRadius(double value);
    void SetCollisionHeight(double value);
#endif
    void SetAllianceID(std::uint32_t value);
    void SetPKCount(int value);
    void SetPKPardonCount(int value);
    void SetDuelCount(int value);
    void SetAttackType(AttackType value);
    void SetGuility(char value);
    void SetHeroType(int value);
    void SetNoblessType(int value);
#if defined(L2_BUILD_L2SERVER)
    void SetPVPPoint(int value) { m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nPVPPoint, value, 42); }
#else
    void SetPVPPoint(int value);
#endif
    void SetUltimateSkillGauge(int value);
    void UltimateSkillGauge(int value);
    void SetMyAirShipID(std::uint32_t value);
    void SetMyShuttleID(std::uint32_t value);
    void AddBaseAttribute(BaseAttribute& value);
    bool SetStats(CStats& value);
    void SetAttackRange(int value);
#if defined(L2_BUILD_L2SERVER)
    void SetAlive(bool value) { m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_bIsAlive, value ? 1 : 0, 97); }
#else
    void SetAlive(bool value);
#endif
    void SetNoblessIndex(int value);
    bool HasSubJobClass() const { return m_nSubJob1Class != -1 || m_nSubJob2Class != -1 || m_nSubJob3Class != -1; }
    bool IsSubJobClass() { return m_nSubJobId > 0; }
    bool HasDualClass() { return m_DualClassID > 0; }
    int DualClassID() { return m_DualClassID; }
    void DualClassID(int value) { m_DualClassID = value == 0 ? -1 : value; }
    bool IsDualClass() { return m_IsDualClass; }
    void IsDualClass(bool value) { m_IsDualClass = value; }
    void ChangeMainDualClass(bool dualClass) { m_IsDualClass = dualClass; }
    void SetRace(RaceType value);
    void SetClass(ClassType value);
    void SetSex(SexType value);
    void SetPrivateStoreType(int value);
#if defined(L2_BUILD_L2SERVER)
    void SetMoveMode(int value) {
        const MoveType converted = static_cast<MoveType>(value);
        if (m_MoveMode == converted) {
            return;
        }
        m_MoveMode = converted;
        m_NeedUpdatePacketBitSet.SetBit(84);
    }
#else
    void SetMoveMode(int value);
#endif
    bool GetEventFlag(NpcEventFlag flag);
    bool IsOlympiadApplicant() const;
    void SetOlympidApplicant(bool value);
    void SetRideType(RideType value);
    void SetRideClassID(int value);
    bool IsSummon();
    bool IsPet();
    bool IsSummonOrPet();
    bool IsPrivateRespawnNoLog() const { return m_nPrivateRespawnNoLog != 0; }
#if defined(L2_BUILD_L2SERVER)
    void SetRaidPoint(int value) { m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_nRaidPointAsset, value, 43); }
#else
    void SetRaidPoint(int value);
#endif
    void SetEnvironment(int value);
    void SetPledgeID(std::uint32_t value);
    void SetPledgeType(std::uint32_t value);
    int GetWorldId() { return m_nWorldId; }
    void SetWorldId(int value) { m_nWorldId = value; }
    int GetAcquiredAbilityCount() const;
    void SetAP(int value, bool updatePacket);
    int GetAP() const;
    int GetRemainAP() const;
    int CalcAPByLevel() const;
    double GetHP() const { return m_dHP; }
    void SetHP(double value);
    void ValidateHP(double value);
    void RestoreHP();
    double GetMP() const { return m_dMP; }
    void SetMP(double value);
    void ValidateMP(double value);
    void RestoreMP();
    void SetSP(std::int64_t value);
    void SetExp(std::int64_t value);
    double GetCP() const;
    void SetCP(double value);
    void ValidateCP(double value);
    void RestoreCP();
    void SetLevel(int value);
    void SetBaseHP(double value);
    void SetBaseMP(double value);
    void SetBaseCP(double value);
    bool SetCarryWeight(double value);
    void SetBuilder(int value);
    void SetFace(int value);
    void SetHairShape(int value);
    void SetHairColor(int value);
    void SetFaceEx(int value);
    void SetHairShapeEx(int value);
    void SetHairColorEx(int value);
    void InitFaction();
    bool UpdateFactionPoint(int factionId, std::int64_t factionPoint);
    int FindUpperBoundIndex(int factionIndex, int factionPoint);
    bool IsLevelLimited(int factionId);
    float GetFactionPercentageForCurrentLevel(int factionId);
    std::int32_t GetAccountLastLogoutTime() { return m_AccountLastLogoutTime; }
    void SetAccountLastLogoutTime(std::int32_t value) { m_AccountLastLogoutTime = value; }
    char16_t* Name() { return m_sName; }
    void SetName(const char16_t* value);
    bool AddAbnormalStatus(AbnormalStatusSlotType type, std::uint8_t level, int magicLevel);
    bool DeleteAbnormalStatus(AbnormalStatusSlotType type);
    void DeleteAllAbnormalStatus();
    bool IsAbnormalType(AbnormalStatusSlotType type);
    bool SetAbnormalLevel(AbnormalStatusSlotType type, std::uint8_t level);
    int GetAbnormalLevel(AbnormalStatusSlotType type);
    int GetAbnormalMagicLevel(AbnormalStatusSlotType type);
    void SetEventFlag(NpcEventFlag flag, bool value);
    bool IsStraightAttacker() { return m_bStraightAttacker; }
    bool IsTurnToFollowee() { return m_bTurnToFollowee; }
    bool IsFlying() const { return static_cast<std::int32_t>(m_moveType) == CMT_FLY; }
    bool CanMove() { return m_bCanMove; }
    std::int32_t SetNPCGiveItemJobWait() { return runtime::raw_atomic::ExchangeI32(&m_lNPCGiveItemJob, 1); }
    void SetNPCGiveItemJobDone() { runtime::raw_atomic::ExchangeI32(&m_lNPCGiveItemJob, 0); }
    std::int64_t GetFactionPoint(int factionId);
    int GetFactionLevel(int factionId);
    void SetNeedUpdatePacket(int packetIndex) { m_NeedUpdatePacketBitSet.SetBit(static_cast<std::uint32_t>(packetIndex)); }
    void ClearNeedUpdatePacket() { m_NeedUpdatePacketBitSet.Clear(); }

    bool SetMemo(QuestType questType, int questId);
    bool SetMemo(int questId);
    bool SetMemoState(QuestType questType, int questId, int slot, int value);
    bool SetMemoState(int questId, int slot, int value);
    bool SetJournal(QuestType questType, int questId, int value);
    bool SetJournal(int questId, int value);
    bool SetFlagJournal(QuestType questType, int questId, int flag);
    bool SetFlagJournal(int questId, int flag);
    bool ResetFlagJournal(QuestType questType, int questId, int flag);
    bool ResetFlagJournal(int questId, int flag);
    bool RemoveMemo(QuestType questType, int questId);
    bool RemoveMemo(int questId);
    int GetMaxMemoCount(QuestType questType);
    MemoStruct* GetMemoStructArray(QuestType questType);
    MemoStruct* GetMemoStruct(QuestType questType, int questId);
    bool HaveMemo(QuestType questType, int questId);
    bool HaveMemo(int questId);
    QuestType GetQuestType(int questId);
    int GetMemoState(QuestType questType, int questId, int slot);
    int GetMemoState(int questId, int slot = 0);
    int GetUserNPCLog(QuestType questType, int questId, int npcId, UserNPCLogType userLogType);
    int GetUserNPCLog(int questId, int npcId, UserNPCLogType userLogType);
    int IncreaseUserNPCLog(QuestType questType, int questId, int npcId, UserNPCLogType userLogType, int maximum);
    int IncreaseUserNPCLog(int questId, int npcId, UserNPCLogType userLogType, int maximum);
    bool SetUserNPCLog(QuestType questType, int questId, int npcId, UserNPCLogType userLogType, int value);
    bool SetUserNPCLog(int questId, int npcId, UserNPCLogType userLogType, int value);
    bool DeleteUserNPCLog(QuestType questType, int questId, int npcId, UserNPCLogType userLogType);
    bool DeleteUserNPCLog(int questId, int npcId, UserNPCLogType userLogType);
    void SetOneTimeFlag(int questId, bool value);
    bool GetOneTimeFlag(int questId) const;
    void GetOneTimeFlagAll(char* destination, int destinationSize) const;
    void SetOneTimeFlagAll(char* source);
    void InitDailyQuest();
    bool GetDailyQuestFlag(int questId);
    bool GetWeeklyQuestFlag(int questId);
    std::int32_t GetDailyBindTime();
    std::int32_t GetWeeklyBindTime();
    std::vector<int> GetCompletedDailyQuestsAfterBindTime(std::int32_t bindTime);

    bool SetNRMemo(int questId);
    bool SetNRMemoState(int questId, int slot, int value);
    bool SetNRJournal(int questId, int value);
    bool SetNRFlagJournal(int questId, int flag);
    bool ResetNRFlagJournal(int questId, int flag);
    bool RemoveNRMemo(int questId);
    bool HaveNRMemo(int questId);
    int GetNRMemoState(int questId, int slot);

    int CheckAndSetOneTimeUserFlag(int questId, CSharedCreatureData* creature);

private:
    int ConvertToInternalOneTimeQuestID(int questId) const;
    std::int32_t m_nCreatureStart;

public:
    bool m_bIsUser;
    std::int32_t m_bIsAlive;
    std::int32_t m_nAge;
    bool m_bExistInHash;
    std::int32_t m_nPrivateID;
    std::int32_t m_nSubJobId;
    std::int32_t m_SubJobLevel[4];
    std::int32_t m_nNoblessType;
    std::int32_t m_nHeroType;
    std::int32_t m_nNoblessIndex;
    std::int32_t m_nSubJob0Class;
    std::int32_t m_nSubJob1Class;
    std::int32_t m_nSubJob2Class;
    std::int32_t m_nSubJob3Class;
    std::int32_t m_DualClassID;
    bool m_IsDualClass;
    RaceType m_Race;
    ClassType m_nClass;
    SexType m_Sex;
    bool m_bUndying;
    bool m_bCanBeAttacked;
    bool m_bGMInvisible;
    double m_dCollisionRadius;
    double m_dCollisionHeight;
    std::int32_t m_nFace;
    std::int32_t m_nHairShape;
    std::int32_t m_nHairColor;
    std::int32_t m_nFaceEx;
    std::int32_t m_nHairShapeEx;
    std::int32_t m_nHairColorEx;
    std::int64_t m_nExp;
    std::int64_t m_nSP;
    std::int32_t m_nInventoryIndex;
    std::int32_t m_nSlotIndex[63];
    CStats m_OriginalStat;
    AbnormalResist m_OriginAbnormalResist;

#if defined(L2_BUILD_L2SERVER)
    friend class ::CDB;
    friend class ::CCreature;
    friend class ::CSkillAction2;
    friend class ::CNPC;
    friend class ::CDuel;
    friend class ::CPet;
    friend class ::CPledgeGameField;
    friend class ::CPledgeGamePlayers;
#else
    friend class CCreature;
#endif

    double m_dHP;
    double m_dMP;
    double m_dCP;
    char16_t m_sName[25];

    MovingStatusEnum m_MovingStatus;
    MoveType m_MoveMode;
    StopType m_StopMode;
    EnvType m_Environment;
    CSafePointer2<CCreature> m_pPet;
    CSafePointer2<CCreature> m_pSummon[10];
    std::uint32_t m_nSummonedNpc[10];
    std::int32_t m_TargetIndex;
    std::uint32_t m_TargetSID;
    char m_cIsGuilty;
    bool m_bIsFreezed;
    std::int32_t m_nDuelCount;
    std::int32_t m_nPKCount;
    std::int32_t m_nPKPardonCount;
    std::int32_t m_nKarma;
    std::int16_t m_nSulffrageCount;
    std::int16_t m_nSulffrageUsedCount;
    std::int32_t m_nSociality;
    std::int32_t m_nPKTimeStamp;
    MemoStruct m_Memo[41];
    MemoStruct m_NRMemo[20];
    DailyQuestState m_DailyQuest[300];

private:
    bool m_bEventFlag[1];
    CBitStorage<2048, std::int32_t, 64> m_OneTimeQuestBitFlag;

public:
    std::uint32_t m_dwLastRewardTime;
    std::int32_t m_LastAttackerServerID;
    std::int32_t m_FirstAttackerServerID;
    std::uint32_t m_nPledgeID;
    std::uint32_t m_nPledgeIndex;
    bool m_bPledgeMaster;
    std::int32_t m_nPledgeType;
    std::uint32_t m_nRelatedIndex;
    bool m_bIsRelated;
    std::int32_t m_nGradeId;
    bool m_bIsNPCRelatedToPledge;
    std::uint32_t m_nAcademyPledgeDBID;
    std::uint32_t m_nAllianceId;
    std::uint32_t m_nAllianceIndex;
    bool m_bCanJoinParty;
    std::uint32_t m_nPartyId;
    std::int32_t m_nPartyIndex;
    bool m_bPartyMaster;
    std::uint32_t m_nMPCCId;
    bool m_bNotFunctionable;
    std::int32_t m_nResidenceID;
    std::int32_t m_nResidenceOwnerID;
    std::int32_t m_nRulingAgit;
    std::int32_t m_nPrivateStoreType;
    std::int32_t m_nPrivateStoreLimit;
    std::int32_t m_nPrivateStoreBuyLimit;
    std::int32_t m_nPrivateStoreRecipeType;
    std::int32_t m_nSoulShot;
    double m_dSoulShotAttackBonusPer;
    bool m_bSpiritShot;
    double m_dSpiritShotAttackBonusPer;
    double m_dSpiritShotSpeedBonusPer;
    std::int32_t m_nSoulShotCount;
    std::int32_t m_nSpiritShotCount;
    std::int32_t m_nFishingShot;
    bool m_bIsDiss;
    bool m_bIsCorpse;
    bool m_bCanBeResurrected;
    std::int32_t m_bCanResurrection;
    char16_t m_sResurrectSpellerName[25];
    std::int64_t m_nResurrectExp;
    std::int32_t m_nResurrectRatio;
    std::uint32_t m_dwResurrectTime;
    bool m_bIsFakeDeath;
    bool m_bFakeDeathFail;
    std::int32_t m_nDeathPenaltyLevel;
    std::int32_t m_nNewbieStatus;
    std::int32_t m_nDBID;
    AttackType m_BaseAttackType;
    std::int32_t m_nBaseAttackRange;
    DamageRangeType m_BaseDamageRange;
    std::int32_t m_nBaseRandomDamage;
    std::int32_t m_nBaseReuseDelay;
    std::int32_t m_nBaseParam[16];
    CStats m_Stat;
    AbnormalResist m_AbnormalResist;
    std::int32_t m_HennaId_1;
    std::int32_t m_HennaId_2;
    std::int32_t m_HennaId_3;
    char m_cEnableHenna;
    std::int32_t m_nLevel;
    std::int32_t m_MainClassLevel;
    std::int32_t m_DualClassLevel;
    std::int32_t m_nMaxInventoryNum;
    std::int32_t m_nNumInventory;
    std::int32_t m_nMaxQuestInventoryNum;
    std::int32_t m_nCurrentQuestInventoryNum;
    std::int32_t m_nMaxArtifactInventoryNum;
    std::int32_t m_nCurrentArtifactInventoryNum;
    std::int32_t m_nInventoryWeight;
    double m_dCarryWeight;
    double m_dOrgHP;
    double m_dOrgMP;
    double m_dOrgCP;
    double m_dBaseHP;
    double m_dBaseMP;
    double m_dBaseCP;
    double m_dOrgHPRegen;
    double m_dOrgMPRegen;
    double m_dOrgCPRegen;
    double m_dHPRegen;
    double m_dMPRegen;
    double m_dCPRegen;
    double m_dAttackTime;
    std::int32_t m_EquipedWeaponClassID;
    AttackType m_AttackType;
    ArmorType m_ArmorType;
    AdditionalArmorType m_AdditionalArmorType;
    std::int32_t m_nAttackRange;
    std::int32_t m_nRandomDamage;
    std::int32_t m_nOrgParam[16];
    std::int32_t m_nMoveTypeSpeed;
    double m_dParam[16];
    double m_dHitTimeFactor;
    double m_dSkillHitTimeFactor;
    std::int32_t m_nBuilder;
    double m_nCoolTime;
    std::int32_t m_nLastBlowWeaponClassID;
    std::int32_t m_nSSQRoundNumber;
    std::int32_t m_nSSQPart;
    std::int32_t m_nSSQPosition;
    std::int32_t m_nSealSelectionNo;
    std::int64_t m_nSSQPoint;
    std::int64_t m_nTwilightItemCount[3];
    std::int64_t m_nDawnItemCount[3];
    std::int32_t m_nTicketBuyCount;
    std::int32_t m_nSSQDawnRound;
    std::int32_t m_nInPeaceZone;
    std::int32_t m_nInBattleField;
    std::int32_t m_nPCCafePoint;
    bool m_bCanUsePCCafePoint;
    bool m_bPCCafeUser;
    std::int32_t m_nDailyPCCafePoint;
    std::int32_t m_nBonusCheckLevel;
    std::int32_t m_nPremiumLevel;
    std::int32_t m_bPremiumPCCafeUser;
    std::int32_t m_nPVPPoint;
    double m_dOverHitBonus;
    std::int32_t m_nInkEnergy;
    std::int32_t m_nLastAttackTick;
    bool m_bIsInCombatMode;
    BaseAttribute m_BaseAttribute;
    AttributeTypeEnum m_TAttackType;
    AttributeTypeEnum m_OrgDefenceType;
    AttributeTypeEnum m_TransDefenceType;
    char16_t m_sAI[100];
    double m_dAcquireSP;
    std::int32_t m_nRaidPointAsset;
    std::int32_t m_nAcquireRaidBossPoint;
    std::int32_t m_nAgroRange;
    bool m_bDecayComplete;
    CSafePointer2<CCreature> m_pMaster;
    std::uint32_t m_dwSummonTime;
    std::uint32_t m_dwSummonLifeTime;
    std::int32_t m_nDomainID;
    NpcSummonType m_SummonType;
    std::int32_t m_nMasterExpRatio;
    std::uint32_t m_SummonerID;
    std::int32_t m_BossID;
    std::int32_t m_nClassID;
    std::int32_t m_nWeightPoint;
    std::int32_t m_nRespawnTime;
    std::int32_t m_nClassIndex;
    // Current CNPC::Set(OBJECT_FIELD_CLAN, vector<int64>) stores the
    // element count at +0x359c and at most five IDs at +0x35a0. The old
    // donor declaration flattened these six cells into m_ClanId[6].
    std::int32_t m_nClanIdCount;
    std::int32_t m_ClanId[5];
    std::int32_t m_nClanHelpRange;
    double m_dAcquireExpRate;
    std::int32_t m_nPState;
    CSharedCreatureData* m_pTarget;
    CSharedCreatureData* m_spMaster;
    ActionType m_Action;
    bool m_bMoveTrouble;
    std::uint32_t m_dwPathFindFailCount;
    std::int32_t m_nCorpseTime;
    bool m_bIsSpoil;
    bool m_bItemCollectable;
    std::int32_t m_nCanSweeper;
    std::int32_t m_nSpoilerSmartID;
    bool m_bIsPlunded;
    bool m_bIsSow;
    bool m_bIsSeed2;
    std::int32_t m_nCanHarvest;
    std::int32_t m_nSowerSmartID;
    bool m_bUnsowing;
    std::int32_t m_nExCrtEffect;
    std::int32_t flag;
    RideType m_rideType;
    std::int32_t m_rideClassID;
    std::int32_t m_rideLevel;
    std::int32_t m_Meal;
    std::int32_t m_MaxMeal;
    std::int32_t m_nPetDBID;
    // The matching 2015 PDB called this +0x3624 byte m_bStarved. Current
    // L2NPC.exe reads it as the L2Server-owned pet-action block.
    bool m_bPetActionBlocked;
    char m_cNoSleepMode;
    char m_bCanAct;
    std::int32_t m_nMaxMagicLevel;
    bool m_bPassableDoor;
    bool m_bUnique;
    std::int32_t m_transformID;
    std::uint32_t m_nInstantZoneID;
    std::uint32_t m_nInstantZoneTypeID;
    std::int32_t m_InstantZoneTypeID_InUse;
    std::int32_t m_UltimateSkillGauge;
    std::int32_t m_UltimateSkillLv;
    SYSTEMTIME m_tCreateDate;
    SYSTEMTIME m_tGetGiftDate;
    std::int32_t m_nGetBirthdayGiftCount;
    bool m_bDailyQuestFlag;
    std::int32_t m_nGet5YearGiftCount;
    SYSTEMTIME m_tGet5GiftDate;
    // The matching 2015 PDB called this +0x3684 byte m_bAbsoluteReturn.
    // Current L2NPC.exe owns it as the return-to-spawn teleport handshake.
    bool m_bTeleporting;
    bool m_StatusFlag[3];
    std::int32_t m_nDominionSiegeId;
    std::int32_t m_nDominionSiegeStatus;
    std::uint32_t m_AirShipId;
    std::int32_t m_EvolutionId;
    bool m_bDieEventSet;
    bool m_bRestartEventSet;
    std::int32_t m_nDieEventDistance;
    std::int32_t m_nRestartEventDistance;
    PVPMatchType::Enum m_PVPMatchType;
    PVPMatchPlayerState::Enum m_PVPMatchPlayerState;
    char m_cPVPMatchTeamID;
    std::int32_t m_nPrivateRespawnNoLog;
    std::uint32_t m_nShuttleId;
    bool m_bStraightAttacker;
    bool m_bTurnToFollowee;
    std::int32_t m_nSummonAI;
    std::int16_t m_AtomicQuestFlag[5];
    AbnormalData m_AbnormalStatus[108];
    std::int32_t m_nDBValue;
    std::int32_t m_nMakerParam1;
    std::int32_t m_nMakerParam2;
    std::int32_t m_nMakerParam3;
    CreatureMoveType m_moveType;
    std::int32_t m_MesTime[2];
    std::int32_t m_MesCount[2];
    bool m_InfectedMes[2];
    bool m_bCanMove;
    bool m_bAbsoluteDefence;
    bool m_bNoAttackDamage;
    bool m_bWeakenedDeathPenalty;
    bool m_EnableMoveAfterTalk;
    std::int32_t m_nWorldId;
    std::int32_t m_TrainingState;
    std::int32_t m_TrainingLevel;
    std::int64_t m_TrainingExp;
    std::int32_t m_TrainingTime;

private:
    std::int32_t m_AccountLastLogoutTime;

public:
    std::uint8_t m_CountryCode;
    bool m_bNpcDebugOn;
    std::int32_t m_OnGoingDynamicContent;

private:
    std::int32_t m_nAP;
    bool m_bIsOlympiadApplicant;

public:
    AcquiredPotentialCount m_stPotentialCount;

private:
    volatile std::int32_t m_lNPCGiveItemJob;

public:
    volatile std::int64_t m_nFactionPoint[16];
    volatile std::int32_t m_nFactionLevel[16];
    std::uint32_t m_bNeedUpdatePacketBits[4];
    CVariableBitSet m_NeedUpdatePacketBitSet;
    std::int32_t m_nPacketSentCount[103];
    std::int32_t m_nPacketSentCountLegacy;
    std::int32_t m_iLastArrowIndex;
    std::int16_t m_nDormantType;

private:
    std::int32_t m_nCreatureEnd;
};

// TotalChecker2015.pdb: empty one-byte class. SharedDefine_Creature has
// max_class == 0, so the shipping factory does not open a constant mapping.
class CSharedCreatureConstant {};

struct IdIndexPair {
    std::uint32_t id;
    std::uint32_t index;
};

struct EnemyWarPair {
    std::uint32_t nEnemyId;
    std::uint32_t nWarId;
};

class CSharedAllianceConstant {
private:
    [[maybe_unused]] std::int32_t a;
};

class CSharedAllianceData : public CSharedObject {
private:
    std::int32_t m_nAllianceStart;

public:
    std::uint32_t m_nDbId;
    // Windows wchar_t is 16-bit. Keep the shared-memory ABI on LP64 hosts.
    char16_t m_sAllianceName[24];
    std::uint32_t m_nMasterPledgeDbId;
    std::uint32_t m_nOustTime;
    std::uint32_t m_nCrestId;
    std::uint32_t m_nOnlineMemberCount;
    IdIndexPair m_OnlineMember[12];
    EnemyWarPair m_EnemyWarPair[5];

    CSharedAllianceData* Next();
    void CopyFrom(const CSharedAllianceData* source);
    void CollectVar(ScriptTypeInfo* typeInfo) override;

private:
    std::int32_t m_nAllianceEnd;
};

class CAlliance;
class CAirShip;
class CCommissionTradeManager;
class CGeneralObject;
class CItem;
class CParty;
class CPledge;
class CStaticObject;

namespace Commission {
enum Enum : std::int32_t;
}

namespace CommissionFirst {
enum Enum : std::int32_t;
}

class CSharedItemConstant {
public:
    ItemDefaultActionEnum m_enumDefaultAction;
    std::int32_t m_nRecipeId;
    std::int32_t m_EtcitemType;
    std::int32_t m_nWeight;
    std::int64_t m_nPrice;
    std::int64_t m_nDefaultPrice;
    void* m_pSkill;
    void* m_pCriticalSkill;
    void* m_pAttackSkill;
    void* m_pMagicSkill;
    void* m_pEnchantedFourSkill;
    std::int32_t m_nMagicSkillProb;
    ArmorType m_ArmorType;
    AdditionalArmorType m_AdditionalArmorType;
    std::int32_t m_Material;
    std::int32_t m_nSoulShotCount;
    std::int32_t m_nSpiritShotCount;
    std::int32_t m_Reduced_soulshot_prob;
    std::int32_t m_Reduced_soulshot_count;
    std::int32_t m_Reduced_spiritshot_prob;
    std::int32_t m_Reduced_spiritshot_count;
    std::int32_t m_nCrystalCount;
    bool m_bImmediateEffect;
    bool m_bExImmediateEffect;
    std::int32_t m_nEffectiveRange;
    std::int32_t m_nInitialCount;
    std::int32_t m_nGroupId;
    bool m_bhLockedAble;
    std::int32_t m_nAttribution;
    bool m_bIsTrade;
    bool m_bIsNPCTrade;
    bool m_bIsPrivateStore;
    bool m_bIsDrop;
    bool m_bIsDestruct;
    std::int32_t m_nItemEnchantType;
    bool m_bIsMagicWeapon;
    std::int32_t m_nRandomDamage;
    AttackType m_AttackType;
    std::int32_t m_nAttackRange;
    DamageRangeType m_DamageRange;
    std::int32_t m_nReuseDelay;
    std::int32_t m_nMpConsume;
    std::int32_t m_Reduced_mp_consume_prob;
    std::int32_t m_Reduced_mp_consume;
    std::uint8_t m_cDualRate;
    std::int16_t m_sMpBonus;
    bool m_bForNpc;
    std::int32_t m_nHennaId;
    std::int32_t m_nManorId;
    std::int32_t m_nManorLev;
    std::int32_t m_nManorCropId;
    std::int32_t m_nManorCropIdReplace;
    std::int32_t m_nDropPeriod;
    std::int32_t m_nDelayShareGroup;
    std::int32_t m_nKeepType;
    std::int32_t m_nDuration;
    bool m_bCanOlympiadUse;
    bool m_bCanNotPledgeGameUse;
    std::int32_t m_nIsHeroEntryType;
    BaseAttribute m_BaseAttribute;
    bool m_bCanAttrEnchant;
    bool m_bNoAttackWeapon;
    bool m_bForceEquip;
    bool m_bOnlyUseEquipedWeaponActiveSkill;
    std::int32_t m_nUseSkillDistime;
    std::int32_t m_nEquipReuseDelay;
    bool m_bIsPremium;
    bool m_bIsItemChanceAffected;

    CrystalType GetCrystalType() const { return m_Crystal; }

    Commission::Enum GetCommissionItemType() const {
        // Current RequestCommissionRegister@00c00aac is the direct
        // +0x14c consumer. The matching-source PDB type record fixes this
        // original inline member name.
        return static_cast<Commission::Enum>(m_CommissionType);
    }

    int GetShotGrade(Shot::Type type);

    void SetCommissionItemType(Commission::Enum type);
    void SetCommissionItemFirstType(Commission::Enum type);
    bool SetGeneralItemType(int type);
    void SetCrystalType(CrystalType type);
    void SetShapeShiftableTargetItem(bool value);
    void SetShapeShiftableExtractionItem(bool value);

    std::int32_t m_period;
    double m_dAgathionEnergy;
    double m_dAgathionMaxEnergy;
    bool m_bHpLimitBreak;
    std::int32_t m_ensoulSlotCount[3];

private:
    friend class CItem;
    friend class CCommissionTradeManager;
    CrystalType m_Crystal;
    std::int32_t m_CommissionType;
    std::int32_t m_CommissionFirstType;

public:
    std::int32_t m_GeneralItemType;
    double m_dValidate[16];

private:
    bool m_bShapeShiftableTarget;
    bool m_bShapeShiftableExtarction;
};

class CSharedItemData : public CSharedObject {
private:
    // Current User::LogEquippedItemOnLeaveWorld@0x00bc1034 reads the
    // trailing shared-item sentinel together with the shape/use fields.
    friend class ::User;
    std::int32_t m_nItemStart;

public:
    std::int64_t m_nCount;
    std::int32_t m_nInventorySlotIndex;
    std::int32_t m_nDBID;
    std::int32_t m_nClassID;
    ItemType m_Type;
    std::int64_t m_Slot;
    std::int32_t m_bBlessed;
    ItemConsumeType m_ItemConsumeType;
    std::int32_t m_nDamaged;

private:
    std::int32_t m_nEnchanted;
    std::int16_t m_hLocked;

public:
    double m_dAgathionEnergy;
    std::int32_t m_nDuration;
    bool m_bIsVariation;

#if defined(L2_BUILD_L2SERVER)
    int GetEnchanted() const { return m_nEnchanted; }
    void SetEnchanted(int value) { m_nEnchanted = value; }
    std::int16_t GetLocked() const { return m_hLocked; }
    void SetLocked(std::int16_t value) { m_hLocked = value; }
    bool IsVariationItem() const { return m_bIsVariation; }
#else
    int GetEnchanted() const;
    void SetEnchanted(int value);
    std::int16_t GetLocked() const;
    void SetLocked(std::int16_t value);
    bool IsVariationItem() const;
#endif
    static bool IsStackable(ItemConsumeType consumeType);
#if defined(L2_BUILD_L2SERVER)
    bool IsStackable() const { return IsStackable(m_ItemConsumeType); }
#else
    bool IsStackable() const;
#endif
    CSharedItemData* Next();
    void CopyFrom(const CSharedItemData* source);
    CSharedItemConstant* GetConstant();
    SlotType GetSlotType() const;
    void CollectVar(ScriptTypeInfo* typeInfo) override;

private:
    std::int32_t m_nShapeShiftedClassId;
    std::int16_t m_hUseCount;
    std::int32_t m_nItemEnd;

public:
#if defined(L2_BUILD_L2SERVER)
    int GetShapeShiftedClassId() const { return m_nShapeShiftedClassId; }
    void SetShapeShiftedClassId(int value) { m_nShapeShiftedClassId = value; }
    std::int16_t GetUseCount() const { return m_hUseCount; }
    void SetUseCount(std::int16_t value) { m_hUseCount = value; }
#else
    int GetShapeShiftedClassId() const;
    void SetShapeShiftedClassId(int value);
    std::int16_t GetUseCount() const;
    void SetUseCount(std::int16_t value);
#endif
};

class CSharedAirShipConstant {};

class CSharedAirShipData : public CSharedObject {
private:
    [[maybe_unused]] std::int32_t m_nAirShipStart;

public:
    bool m_bIsInWorld;
    std::uint32_t m_OwnerDBID;
    std::uint32_t m_DBID;
    std::uint32_t m_DriverId;

private:
    [[maybe_unused]] std::int32_t m_nAirShipEnd;
};

class CSharedPartyConstant {
private:
    [[maybe_unused]] std::int32_t a;
};

class CSharedPartyData : public CSharedObject {
private:
    friend class CParty;
    std::int32_t m_nPartyStart;
    bool m_bPVPMatchStart;
    std::int32_t m_nPVPMatchTeamIndex;

public:
    std::uint32_t m_nOnlineMasterDBID;
    std::uint32_t m_nOnlineMemberCount;
    IdIndexPair m_OnlineMaster;
    IdIndexPair m_OnlineMember[7];
    std::int32_t m_TaticalSign[4];
    std::uint32_t m_nMPCCId;

    CSharedPartyData* Next();
    void CopyFrom(const CSharedPartyData* source);
    void SetPVPMatchStart(int teamIndex);
    void SetPVPMatchEnd();
    bool IsPVPMatching() const;
    int GetPVPMatchTeamIndex() const { return m_nPVPMatchTeamIndex; }
    void CollectVar(ScriptTypeInfo* typeInfo) override;

private:
    std::int32_t m_nPartyEnd;
};

class CSharedPledgeConstant {};

// TotalChecker2015.pdb type 0x764e2 and the shipping L2NPC.exe 0x800-byte
// factory stride describe this exact revision. MSVC stores every
// std::bitset<N>, N <= 32, as one 32-bit word, spelling those two members as
// uint32_t keeps the shared-memory ABI invariant on hosts whose std::bitset
// uses an eight-byte word.
class CSharedPledgeData : public CSharedObject {
private:
    std::int32_t m_nPledgeStart;

public:
    std::uint32_t m_nDbId;
    char16_t m_sPledgeName[24];
    std::uint32_t m_nMasterDbId;
    char16_t m_sMasterName[25];
    std::uint32_t m_nChallengeTime;
    std::uint32_t m_nOustTime;
    std::int32_t m_nRootNameValue;
    std::int32_t m_nTotalLevel;
    std::int32_t m_nAllianceDbId;
    std::uint32_t m_nSkillLevel;
    std::uint32_t m_nCrestId;
    std::uint32_t m_nEmblemId;
    std::uint32_t m_CastleSiegeDefenceCount;
    std::uint32_t m_nPrivateFlag;
    std::int32_t m_nRank;
    std::int32_t m_nStatus;
    bool m_bIsGuilty;
    std::uint32_t m_nDismissReservedTime;
    std::uint32_t m_nCastleDbId;
    std::uint32_t m_nWorldAgitDbId;
    std::uint32_t m_nCAgitDbid;
    std::uint32_t m_nFortressDbId;
    std::uint32_t m_nBattleCampDbId;
    std::int32_t m_nNowCastleSiegeDbId[9];
    std::int32_t m_nNowCastleSiegeStatus[9];
    std::uint32_t m_nFortressUnderOffenceSiegeId;
    std::uint32_t m_nFortressUnderDefenceSiegeId;
    std::uint32_t m_nDominionSiegeId;
    std::uint32_t m_nOnlineMemberCount;
    IdIndexPair m_OnlineMaster;
    IdIndexPair m_OnlineMember[140];
    std::uint32_t m_nTotalMemberCount;
    std::uint32_t m_gradePowerList[25];
    std::uint32_t m_subPledgeTypeBits;
    char16_t m_subpledgeMasterName[8][25];
    std::uint32_t m_nPrevServer;
    char16_t m_sOriginPledgeName[24];
    std::int32_t m_AirShipCount;

    CSharedPledgeData();
    CSharedPledgeData* Next();
    void CopyFrom(const CSharedPledgeData* source);
    void CollectVar(ScriptTypeInfo* typeInfo) override;

    std::uint32_t m_nReservedOffenceCastleSiegeDbId;
    std::uint32_t m_nReservedDefenceCastleSiegeDbId;
    std::uint32_t m_nMasteryResetTime;

private:
    std::int32_t m_nPledgeEnd;
};

class CSharedGeneralObjectConstant {
private:
    [[maybe_unused]] std::int32_t a;
};

class CSharedGeneralObjectData : public CSharedObject {
private:
    std::int32_t m_nGeneralObjectStart;

public:
    std::uint32_t m_masterPartySid;
    std::uint32_t m_partyCount;
    std::uint32_t m_partySidList[50];

    CSharedGeneralObjectData* Next();
    void CopyFrom(const CSharedGeneralObjectData* source);
    void CollectVar(ScriptTypeInfo* typeInfo) override;

private:
    std::int32_t m_nGeneralObjectEnd;
};

class CSharedStaticObjectConstant {
private:
    [[maybe_unused]] std::int32_t a;
};

class CSharedStaticObjectData : public CSharedObject {
private:
    friend class CParserForResidence;
    std::int32_t m_nStaticObjectStart;

public:
    std::uint8_t m_cLevel;
    bool m_bBreakable;
    double m_dHP;
    double m_dMaxHP;
    double m_dPDefend;
    double m_dMDefend;
    double m_dCollisionRadius;
    double m_dCollisionHeight;
    bool m_bIsOpen;
    std::int32_t m_nEditorID;
    bool m_OrgTargetable;
    bool m_bTargetable;
    std::int32_t m_nStealth;

    CSharedStaticObjectData* Next();
    void CopyFrom(const CSharedStaticObjectData* source);

private:
    std::int32_t m_nStaticObjectEnd;
};

#if !defined(L2_BUILD_L2SERVER)
template <typename TObject, typename TData, typename TConstant, const CSharedDefine* Define>
class CSharedFactory {
protected:
    void* m_hfm_data;
    TData* m_data;
    CYieldLock Lock;
    void* m_hfm_const;
    TConstant* m_const;
    volatile std::int32_t m_const_num;
    void* m_hfm_index;
    std::int32_t* m_index;
    std::int32_t m_free_index;
    std::int32_t m_free_last;

private:
    friend class runtime::SharedFactoryMappingAccess;

    CSharedFactory();

public:
    static CSharedFactory& Inst();

    int GetSize() { return Define->max_data; }

    std::int32_t m_nAdded;
    std::int32_t m_nRemoved;
    std::int32_t m_nTotal;
    std::int32_t m_nRemain;

    void Initialize();
    ~CSharedFactory();

    TConstant* GetConstant(int indexValue) {
        if (indexValue < 0 || indexValue >= Define->max_class) {
            return nullptr;
        }
        return m_const + indexValue;
    }
    TData* AddPrototype(TObject* object);
    TData* Add(TObject* object);
    TData* AddById(int objectId);
    void SetID(int indexValue, unsigned int objectId);
    int RemovePrototype(TObject* object);
    int Remove(TObject* object);
    int RemoveByIndex(int indexValue);
    CSPointer<TObject> FindObjectSP(int indexValue);
    CSPointer<TObject> FindObjectSP(int objectId, int indexValue);
    TObject* FindObject(int indexValue);
    TObject* FindObject(int objectId, int indexValue);
    TObject* GetObjectW(int indexValue);
    TObject* GetObjectW(int objectId, int indexValue);

    TData* GetData(int indexValue) {
        if (indexValue < 0 || indexValue >= Define->max_data) {
            return nullptr;
        }
        return m_data + indexValue;
    }

    int GetIdByIndex(int indexValue) {
        if (indexValue < 0 || indexValue >= Define->max_data) {
            return -1;
        }
        return m_index[indexValue];
    }

    CYieldLock* GetLock(int indexValue);
    void WriteLock(int indexValue);
    void WriteUnlock(int indexValue);
    void ReadLock(int indexValue);
    void ReadUnlock(int indexValue);

    int GetNextIndex(TData* data) { return data ? GetNextIndex(data->index) : -1; }

    int GetNextIndex(int indexValue) {
        TData* data = GetData(indexValue);
        return data ? data->next : -1;
    }

    TData* GetNext(TData* data) { return data ? GetNext(data->index) : nullptr; }

    TData* GetNext(int indexValue) {
        TData* data = GetData(indexValue);
        return data ? GetData(data->next) : nullptr;
    }

    bool Push(int listIndex, int& value, int maximum);
    bool Pop(int listIndex, int& value);
    bool Change(int listIndex, int& oldValue, int& newValue, int maximum);
};

using CStaticObjectFactory =
    CSharedFactory<CStaticObject, CSharedStaticObjectData, CSharedStaticObjectConstant, &SharedDefine_StaticObject>;

using CAllianceFactory = CSharedFactory<CAlliance, CSharedAllianceData, CSharedAllianceConstant, &SharedDefine_Alliance>;

using CAirShipFactory = CSharedFactory<CAirShip, CSharedAirShipData, CSharedAirShipConstant, &SharedDefine_AirShip>;

using CCreatureFactory = CSharedFactory<CCreature, CSharedCreatureData, CSharedCreatureConstant, &SharedDefine_Creature>;

using CPartyFactory = CSharedFactory<CParty, CSharedPartyData, CSharedPartyConstant, &SharedDefine_Party>;

using CPledgeFactory = CSharedFactory<CPledge, CSharedPledgeData, CSharedPledgeConstant, &SharedDefine_Pledge>;

using CGeneralObjectFactory =
    CSharedFactory<CGeneralObject, CSharedGeneralObjectData, CSharedGeneralObjectConstant, &SharedDefine_GeneralObject>;

using CItemFactory = CSharedFactory<CItem, CSharedItemData, CSharedItemConstant, &SharedDefine_Item>;
#endif


#if defined(L2_BUILD_L2SERVER)

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>

class CAirShip;
class CAlliance;
class CCreature;
class CGeneralObject;
class CItem;
class CParty;
class CPledge;
class CShuttle;
class CStaticObject;
template <typename TObject>
class CSPointer;

class CSharedAirShipData;
class CSharedAirShipConstant;
class CSharedAllianceData;
class CSharedAllianceConstant;
class CSharedCreatureData;
class CSharedCreatureConstant;
class CSharedGeneralObjectData;
class CSharedGeneralObjectConstant;
class CSharedItemData;
class CSharedItemConstant;
class CSharedPartyData;
class CSharedPartyConstant;
class CSharedPledgeData;
class CSharedPledgeConstant;
class CSharedStaticObjectData;
class CSharedStaticObjectConstant;

// L2Server's current producer-side definition. The separately compiled
// L2NPC product has another global CSharedDefine revision for its 0x60
// consumer factory. L2_BUILD_L2SERVER selects this 0x200060 producer revision
// with its embedded bank of 131072 CYieldLocks.
struct CSharedDefine final {
    constexpr CSharedDefine(
        const wchar_t* dataMmfName,
        const wchar_t* constantMmfName,
        const wchar_t* indexMmfName,
        const wchar_t* mapMmfName,
        std::int32_t maxData,
        std::int32_t maxClass,
        const wchar_t* lockName,
        const wchar_t* mapLockName,
        const wchar_t* eachLockName
    ) noexcept :
        DataMMFName(dataMmfName),
        ConstantMMFName(constantMmfName),
        IndexMMFName(indexMmfName),
        MapMMFName(mapMmfName),
        max_data(maxData),
        max_class(maxClass),
        lock_name(lockName),
        maplock_name(mapLockName),
        eachlock_name(eachLockName) {}

    const wchar_t* DataMMFName;
    const wchar_t* ConstantMMFName;
    const wchar_t* IndexMMFName;
    const wchar_t* MapMMFName;
    std::int32_t max_data;
    std::int32_t max_class;
    const wchar_t* lock_name;
    const wchar_t* maplock_name;
    const wchar_t* eachlock_name;
};


// Main Shared already declares the exact global physical records for the
// eight recovered domains, so this product uses those names directly without
// aliases or cloned definitions.

// Current Shuttle consumers prove the main CSharedObject prefix and the
// complete 0x48 record stride. Only the Shuttle-specific eight-byte tail is
// still unnamed.
struct alignas(8) CSharedShuttleData final : CSharedObject {
    std::byte opaque_40[8];
};

struct CSharedShuttleConstant final {
    std::byte opaque_00;
};


inline constexpr CSharedDefine SharedDefine_AirShip{L"L2World_AirShipData", L"L2World_AirShipConstant",
    L"L2World_AirShipIndex", L"L2World_AirShipMap", 1000, 100, L"AirShipLock", L"AirShipMapLock", L"EachAirShipLock"};

inline constexpr CSharedDefine SharedDefine_Alliance{L"L2World_AllianceData", L"L2World_AllianceConstant",
    L"L2World_AllianceIndex", L"L2World_AllianceMap", 5000, 0, L"AllianceLock", L"AllianceMapLock", L"EachAllianceLock"};

inline constexpr CSharedDefine SharedDefine_Creature{L"L2World_CreatureData", L"L2World_CreatureConstant",
    L"L2World_CreatureIndex", L"L2World_CreatureMap", 120000, 0, L"CreatureLock", L"CreatureMapLock", L"EachCreatureLock"};

inline constexpr CSharedDefine SharedDefine_Item{L"L2World_ItemData", L"L2World_ItemConstant", L"L2World_ItemIndex",
    L"L2World_ItemMap", 900000, 100000, L"ItemLock", L"ItemMapLock", L"EachItemLock"};

inline constexpr CSharedDefine SharedDefine_GeneralObject{L"L2World_GeneralObjectData", L"L2World_GeneralObjectConstant",
    L"L2World_GeneralObjectIndex", L"L2World_GeneralObjectMap", 1020000, 0, L"GeneralObjectLock", L"GeneralObjectMapLock",
    L"EachGeneralObjectLock"};

inline constexpr CSharedDefine SharedDefine_Party{L"L2World_PartyData", L"L2World_PartyConstant", L"L2World_PartyIndex",
    L"L2World_PartyMap", 10003, 0, L"PartyLock", L"PartyMapLock", L"EachPartyLock"};

inline constexpr CSharedDefine SharedDefine_Pledge{L"L2World_PledgeData", L"L2World_PledgeConstant", L"L2World_PledgeIndex",
    L"L2World_PledgeMap", 10002, 0, L"PledgeLock", L"PledgeMapLock", L"EachPledgeLock"};

inline constexpr CSharedDefine SharedDefine_Shuttle{L"L2World_ShuttleData", L"L2World_ShuttleConstant",
    L"L2World_ShuttleIndex", L"L2World_ShuttleMap", 1000, 100, L"ShuttleLock", L"ShuttleMapLock", L"EachShuttleLock"};

inline constexpr CSharedDefine SharedDefine_StaticObject{L"L2World_StaticObjectData", L"L2World_StaticObjectConstant",
    L"L2World_StaticObjectIndex", L"L2World_StaticObjectMap", 10001, 0, L"StaticObjectLock", L"StaticObjectMapLock",
    L"EachStaticObjectLock"};

template <const CSharedDefine* Define>
struct CSharedFactoryDimensions;

template <>
struct CSharedFactoryDimensions<&SharedDefine_AirShip> final {
    static constexpr std::size_t record_stride = 0x58;
    static constexpr std::size_t constant_stride = 0x01;
};

template <>
struct CSharedFactoryDimensions<&SharedDefine_Alliance> final {
    static constexpr std::size_t record_stride = 0x118;
    static constexpr std::size_t constant_stride = 0x04;
};

template <>
struct CSharedFactoryDimensions<&SharedDefine_Creature> final {
    static constexpr std::size_t record_stride = 0x3ed8;
    static constexpr std::size_t constant_stride = 0x01;
};

template <>
struct CSharedFactoryDimensions<&SharedDefine_Item> final {
    static constexpr std::size_t record_stride = 0xa0;
    static constexpr std::size_t constant_stride = 0x1e0;
};

template <>
struct CSharedFactoryDimensions<&SharedDefine_GeneralObject> final {
    static constexpr std::size_t record_stride = 0x118;
    static constexpr std::size_t constant_stride = 0x04;
};

template <>
struct CSharedFactoryDimensions<&SharedDefine_Party> final {
    static constexpr std::size_t record_stride = 0xb0;
    static constexpr std::size_t constant_stride = 0x04;
};

template <>
struct CSharedFactoryDimensions<&SharedDefine_Pledge> final {
    static constexpr std::size_t record_stride = 0x800;
    static constexpr std::size_t constant_stride = 0x04;
};

template <>
struct CSharedFactoryDimensions<&SharedDefine_Shuttle> final {
    static constexpr std::size_t record_stride = 0x48;
    static constexpr std::size_t constant_stride = 0x01;
};

template <>
struct CSharedFactoryDimensions<&SharedDefine_StaticObject> final {
    static constexpr std::size_t record_stride = 0x90;
    static constexpr std::size_t constant_stride = 0x04;
};

template <const CSharedDefine* Define>
struct CSharedFactoryAddByIdContract;

template <>
struct CSharedFactoryAddByIdContract<&SharedDefine_AirShip> final {
    static constexpr wchar_t removeByIndexSignature[] = L"int __cdecl CSharedFactory<class CAirShip,"
                                                        L"class CSharedAirShipData,class CSharedAirShipConstant,"
                                                        L"&class CSharedDefine const SharedDefine_AirShip>"
                                                        L"::RemoveByIndex(int)";
    static constexpr wchar_t pushSignature[] = L"bool __cdecl CSharedFactory<class CAirShip,"
                                               L"class CSharedAirShipData,class CSharedAirShipConstant,"
                                               L"&class CSharedDefine const SharedDefine_AirShip>"
                                               L"::Push(int,int &,int)";
    static constexpr wchar_t popSignature[] = L"bool __cdecl CSharedFactory<class CAirShip,"
                                              L"class CSharedAirShipData,class CSharedAirShipConstant,"
                                              L"&class CSharedDefine const SharedDefine_AirShip>"
                                              L"::Pop(int,int &)";
};

template <>
struct CSharedFactoryAddByIdContract<&SharedDefine_Alliance> final {
    static constexpr wchar_t removeByIndexSignature[] = L"int __cdecl CSharedFactory<class CAlliance,"
                                                        L"class CSharedAllianceData,class CSharedAllianceConstant,"
                                                        L"&class CSharedDefine const SharedDefine_Alliance>"
                                                        L"::RemoveByIndex(int)";
};

template <>
struct CSharedFactoryAddByIdContract<&SharedDefine_Creature> final {
    static constexpr wchar_t removeByIndexSignature[] = L"int __cdecl CSharedFactory<class CCreature,"
                                                        L"class CSharedCreatureData,class CSharedCreatureConstant,"
                                                        L"&class CSharedDefine const SharedDefine_Creature>"
                                                        L"::RemoveByIndex(int)";
    static constexpr wchar_t pushSignature[] = L"bool __cdecl CSharedFactory<class CCreature,"
                                               L"class CSharedCreatureData,class CSharedCreatureConstant,"
                                               L"&class CSharedDefine const SharedDefine_Creature>"
                                               L"::Push(int,int &,int)";
    static constexpr wchar_t popSignature[] = L"bool __cdecl CSharedFactory<class CCreature,"
                                              L"class CSharedCreatureData,class CSharedCreatureConstant,"
                                              L"&class CSharedDefine const SharedDefine_Creature>"
                                              L"::Pop(int,int &)";
    static constexpr wchar_t changeSignature[] = L"bool __cdecl CSharedFactory<class CCreature,"
                                                 L"class CSharedCreatureData,class CSharedCreatureConstant,"
                                                 L"&class CSharedDefine const SharedDefine_Creature>"
                                                 L"::Change(int,int &,int &,int)";
};

template <>
struct CSharedFactoryAddByIdContract<&SharedDefine_Item> final {
    static constexpr wchar_t removeByIndexSignature[] = L"int __cdecl CSharedFactory<class CItem,"
                                                        L"class CSharedItemData,class CSharedItemConstant,"
                                                        L"&class CSharedDefine const SharedDefine_Item>"
                                                        L"::RemoveByIndex(int)";
    static constexpr wchar_t pushSignature[] = L"bool __cdecl CSharedFactory<class CItem,"
                                               L"class CSharedItemData,class CSharedItemConstant,"
                                               L"&class CSharedDefine const SharedDefine_Item>"
                                               L"::Push(int,int &,int)";
    static constexpr wchar_t popSignature[] = L"bool __cdecl CSharedFactory<class CItem,"
                                              L"class CSharedItemData,class CSharedItemConstant,"
                                              L"&class CSharedDefine const SharedDefine_Item>"
                                              L"::Pop(int,int &)";
};

template <>
struct CSharedFactoryAddByIdContract<&SharedDefine_GeneralObject> final {
    static constexpr wchar_t removeByIndexSignature[] =
        L"int __cdecl CSharedFactory<class CGeneralObject,"
        L"class CSharedGeneralObjectData,class CSharedGeneralObjectConstant,"
        L"&class CSharedDefine const SharedDefine_GeneralObject>"
        L"::RemoveByIndex(int)";
};

template <>
struct CSharedFactoryAddByIdContract<&SharedDefine_Party> final {
    static constexpr wchar_t removeByIndexSignature[] = L"int __cdecl CSharedFactory<class CParty,"
                                                        L"class CSharedPartyData,class CSharedPartyConstant,"
                                                        L"&class CSharedDefine const SharedDefine_Party>"
                                                        L"::RemoveByIndex(int)";
};

template <>
struct CSharedFactoryAddByIdContract<&SharedDefine_Pledge> final {
    static constexpr wchar_t removeByIndexSignature[] = L"int __cdecl CSharedFactory<class CPledge,"
                                                        L"class CSharedPledgeData,class CSharedPledgeConstant,"
                                                        L"&class CSharedDefine const SharedDefine_Pledge>"
                                                        L"::RemoveByIndex(int)";
};

template <>
struct CSharedFactoryAddByIdContract<&SharedDefine_Shuttle> final {
    static constexpr wchar_t removeByIndexSignature[] = L"int __cdecl CSharedFactory<class CShuttle,"
                                                        L"class CSharedShuttleData,class CSharedShuttleConstant,"
                                                        L"&class CSharedDefine const SharedDefine_Shuttle>"
                                                        L"::RemoveByIndex(int)";
    static constexpr wchar_t pushSignature[] = L"bool __cdecl CSharedFactory<class CShuttle,"
                                               L"class CSharedShuttleData,class CSharedShuttleConstant,"
                                               L"&class CSharedDefine const SharedDefine_Shuttle>"
                                               L"::Push(int,int &,int)";
    static constexpr wchar_t popSignature[] = L"bool __cdecl CSharedFactory<class CShuttle,"
                                              L"class CSharedShuttleData,class CSharedShuttleConstant,"
                                              L"&class CSharedDefine const SharedDefine_Shuttle>"
                                              L"::Pop(int,int &)";
};

template <>
struct CSharedFactoryAddByIdContract<&SharedDefine_StaticObject> final {
    static constexpr wchar_t removeByIndexSignature[] =
        L"int __cdecl CSharedFactory<class CStaticObject,"
        L"class CSharedStaticObjectData,class CSharedStaticObjectConstant,"
        L"&class CSharedDefine const SharedDefine_StaticObject>"
        L"::RemoveByIndex(int)";
    static constexpr wchar_t pushSignature[] = L"bool __cdecl CSharedFactory<class CStaticObject,"
                                               L"class CSharedStaticObjectData,class CSharedStaticObjectConstant,"
                                               L"&class CSharedDefine const SharedDefine_StaticObject>"
                                               L"::Push(int,int &,int)";
    static constexpr wchar_t popSignature[] = L"bool __cdecl CSharedFactory<class CStaticObject,"
                                              L"class CSharedStaticObjectData,class CSharedStaticObjectConstant,"
                                              L"&class CSharedDefine const SharedDefine_StaticObject>"
                                              L"::Pop(int,int &)";
};

struct L2ServerSharedFactoryMapping final {
    void* dataHandle = nullptr;
    void* data = nullptr;
    std::size_t dataBytes = 0;
    void* constantHandle = nullptr;
    void* constants = nullptr;
    std::size_t constantBytes = 0;
    void* indexHandle = nullptr;
    std::int32_t* index = nullptr;
    std::size_t indexBytes = 0;
    std::shared_ptr<void> lifetime;
};

namespace l2server_factory_detail {

enum class AddFailure {
    FactoryEmpty,
    FactoryIndexInvalid,
    FreeIndexInvalid,
};

bool AdoptMappingLifetime(const void* factory, std::shared_ptr<void> lifetime, std::string* error);
void ReleaseMappingLifetime(const void* factory) noexcept;
bool HasMappingLifetime(const void* factory) noexcept;

[[noreturn]] void FailAdd(
    const CSharedDefine& define,
    AddFailure failure,
    std::int32_t value,
    std::int32_t previousValue,
    std::source_location location = std::source_location::current()
);
void ReportRemoveMinusOne(std::int32_t indexValue);
void ReportListFailure(const char* message);
void ReportListFailure(const wchar_t* message);
void ReportListCycleFailure(std::source_location location = std::source_location::current());

} // namespace l2server_factory_detail

template <
    typename TObject,
    typename TData,
    typename TConstant,
    const CSharedDefine* Define,
    typename Dimensions = CSharedFactoryDimensions<Define>>
class CSharedFactory {
public:
    static constexpr std::size_t kLockCount = 131072;
    static constexpr std::size_t kLockMask = kLockCount - 1;
    static constexpr std::size_t kCommonIdOffset = 0x24;
    static constexpr std::size_t kCommonIndexOffset = 0x28;
    static constexpr std::size_t kCommonContainerIndexOffset = 0x2c;
    static constexpr std::size_t kCommonConstantIndexOffset = 0x30;
    static constexpr std::size_t kCommonActiveOffset = 0x34;
    static constexpr std::size_t kCommonBeforeOffset = 0x38;
    static constexpr std::size_t kCommonNextOffset = 0x3c;

    CSharedFactory() = default;
    ~CSharedFactory() { DetachMappedViews(); }

    CSharedFactory(const CSharedFactory&) = delete;
    CSharedFactory& operator=(const CSharedFactory&) = delete;

    static CSharedFactory& Inst() {
        static CSharedFactory instance;
        return instance;
    }

    int GetSize() const noexcept { return Define->max_data; }

    static constexpr const CSharedDefine& Definition() noexcept { return *Define; }

    bool InitializeMappedViews(L2ServerSharedFactoryMapping mapping, std::string* error = nullptr) {
        if (IsInitialized()) {
            SetError(error, "shared factory is already initialized");
            return false;
        }
        if (!mapping.lifetime) {
            SetError(error, "shared factory mapping lifetime is unavailable");
            return false;
        }
        if (!mapping.data || mapping.dataBytes < RequiredDataBytes()) {
            SetError(error, "shared factory data mapping is too small");
            return false;
        }
        if (!mapping.index || mapping.indexBytes < RequiredIndexBytes()) {
            SetError(error, "shared factory index mapping is too small");
            return false;
        }
        if (Define->max_class > 0 && (!mapping.constants || mapping.constantBytes < RequiredConstantBytes())) {
            SetError(error, "shared factory constant mapping is too small");
            return false;
        }
        if (!l2server_factory_detail::AdoptMappingLifetime(this, std::move(mapping.lifetime), error)) {
            return false;
        }

        m_hfm_data = mapping.dataHandle;
        m_data = static_cast<TData*>(mapping.data);
        m_hfm_const = mapping.constantHandle;
        m_const = static_cast<TConstant*>(mapping.constants);
        m_const_num = 0;
        m_hfm_index = mapping.indexHandle;
        m_index = mapping.index;

        if (Define->max_class > 0) {
            // Raw mapped bytes, no constructed objects yet.
            std::memset(static_cast<void*>(m_const), 0, RequiredConstantBytes());
        } else {
            m_hfm_const = nullptr;
            m_const = nullptr;
        }

        {
            L2_GUARD(Lock);
            m_nAdded = 0;
            m_nRemoved = 0;
            m_nTotal = Define->max_data;
            m_nRemain = Define->max_data;

            for (std::int32_t index = 0; index < Define->max_data; ++index) {
                WriteRecordI32(index, kCommonBeforeOffset, index - 1);
                WriteRecordI32(index, kCommonNextOffset, index + 1);
            }
            WriteRecordI32(Define->max_data - 1, kCommonNextOffset, -1);
            m_free_index = 0;
            m_free_last = Define->max_data - 1;
            m_const_num = 0;
            for (std::int32_t index = 0; index < Define->max_data; ++index) {
                m_index[index] = -1;
            }
        }
        return true;
    }

    void DetachMappedViews() noexcept {
        if (!l2server_factory_detail::HasMappingLifetime(this)) {
            return;
        }
        m_hfm_index = nullptr;
        m_index = nullptr;
        m_hfm_const = nullptr;
        m_const = nullptr;
        m_const_num = 0;
        m_hfm_data = nullptr;
        m_data = nullptr;
        m_free_index = -1;
        m_free_last = -1;
        l2server_factory_detail::ReleaseMappingLifetime(this);
    }

    bool IsInitialized() const noexcept {
        return m_data && m_index && (Define->max_class == 0 || m_const) && l2server_factory_detail::HasMappingLifetime(this);
    }

    TConstant* GetConstant(int indexValue) noexcept {
        if (!m_const || indexValue < 0 || indexValue >= Define->max_class) {
            return nullptr;
        }
        return reinterpret_cast<TConstant*>(
            reinterpret_cast<std::byte*>(m_const) + static_cast<std::size_t>(indexValue) * Dimensions::constant_stride
        );
    }

    const TConstant* GetConstant(int indexValue) const noexcept {
        return const_cast<CSharedFactory*>(this)->GetConstant(indexValue);
    }

    TData* AddById(int objectId) {
        L2_GUARD_FUNCTION();
        // MSVC mishandles the pointer template parameter named inside a lambda.
        const CSharedDefine& definition = Definition();
        std::byte* const selectedRecord = [&] {
            L2_GUARD(Lock);
            const std::int32_t selected = m_free_index;
            if (selected == -1) {
                l2server_factory_detail::FailAdd(
                    definition, l2server_factory_detail::AddFailure::FactoryEmpty, selected, -2
                );
            }
            if (selected < 0 || selected >= definition.max_data) {
                l2server_factory_detail::FailAdd(
                    definition, l2server_factory_detail::AddFailure::FactoryIndexInvalid, selected, -2
                );
            }

            const std::int32_t next = ReadRecordI32(selected, kCommonNextOffset);
            m_free_index = next;
            if (next < 0 || next >= definition.max_data) {
                l2server_factory_detail::FailAdd(
                    definition, l2server_factory_detail::AddFailure::FreeIndexInvalid, next, selected
                );
            }

            std::byte* const selectedRecord = RecordBytes(selected);
            std::memset(selectedRecord, 0, Dimensions::record_stride);
            WriteRecordI32(next, kCommonBeforeOffset, -1);
            m_index[selected] = objectId;
            WriteRecordI32(selected, kCommonBeforeOffset, -1);
            WriteRecordI32(selected, kCommonNextOffset, -1);
            WriteRecordI32(selected, kCommonIndexOffset, selected);
            WriteRecordI32(selected, kCommonContainerIndexOffset, -1);
            ++m_nAdded;
            --m_nRemain;
            return selectedRecord;
        }();
        return reinterpret_cast<TData*>(selectedRecord);
    }

    // Exact common Add envelope used by all eight current factory
    // specializations. AddById owns the free-list mutation. This wrapper
    // binds the process-local object to its mapped record and striped lock.
    TData* Add(TObject* object) {
        L2_GUARD_FUNCTION();
        TData* const data = AddById(static_cast<int>(object->m_nId));
        object->m_lock = GetLock(data->index);
        return data;
    }

    int RemoveByIndex(int indexValue) {
        L2_GUARD_FUNCTION();
        if (indexValue == -1) {
            l2server_factory_detail::ReportRemoveMinusOne(indexValue);
        }

        {
            L2_GUARD(Lock);
            m_index[indexValue] = -1;
            const std::int32_t oldTailNext = ReadRecordI32(m_free_last, kCommonNextOffset);
            WriteRecordI32(indexValue, kCommonNextOffset, oldTailNext);
            WriteRecordI32(m_free_last, kCommonNextOffset, indexValue);
            WriteRecordI32(indexValue, kCommonBeforeOffset, m_free_last);
            m_free_last = indexValue;
            ++m_nRemoved;
            ++m_nRemain;
        }
        return 1;
    }

    // Exact common Remove envelope. The current body has no
    // object/data null gate: the mapped record remains authoritative until
    // RemoveByIndex returns its index to the free list.
    int Remove(TObject* object) {
        L2_GUARD_FUNCTION();
        return RemoveByIndex(object->m_data->index);
    }

    // Current type-specific publishers write the generated SID directly to
    // the exact index map. This named adapter exposes that proved operation
    // without claiming the unmaterialized SetID template body as evidence.
    void PublishIndexId(int indexValue, std::uint32_t objectId) noexcept {
        m_index[indexValue] = static_cast<std::int32_t>(objectId);
    }

    // Exact Item AddPrototype envelope. The increment has no
    // null/max_class gate and stores its pre-increment value in const_index.
    TData* AddPrototype(TObject* object) {
        TData* const data = Add(object);
        {
            L2_GUARD(Lock);
            const std::int32_t newCount = runtime::raw_atomic::IncrementI32(&m_const_num);
            data->const_index = runtime::SubtractInt32Wrapping(newCount, 1);
        }
        return data;
    }

    TData* GetData(int indexValue) noexcept {
        if (!m_data || indexValue < 0 || indexValue >= Define->max_data) {
            return nullptr;
        }
        return reinterpret_cast<TData*>(RecordBytes(indexValue));
    }

    const TData* GetData(int indexValue) const noexcept { return const_cast<CSharedFactory*>(this)->GetData(indexValue); }

    int GetIdByIndex(int indexValue) const noexcept {
        if (!m_index || indexValue < 0 || indexValue >= Define->max_data) {
            return -1;
        }
        return m_index[indexValue];
    }

    CSPointer<TObject> FindObjectSP(int indexValue);
    TObject* GetObjectW(int indexValue);

    ::CYieldLock* GetLock(int indexValue) noexcept { return &EachLock[static_cast<std::uint32_t>(indexValue) & kLockMask]; }

    void WriteLock(int indexValue) { GetLock(indexValue)->Enter(__WFILE__, __LINE__); }

    void WriteUnlock(int indexValue) { GetLock(indexValue)->Leave(__WFILE__, __LINE__); }

    void ReadLock(int indexValue) { GetLock(indexValue)->Enter(__WFILE__, __LINE__); }

    void ReadUnlock(int indexValue) { GetLock(indexValue)->Leave(__WFILE__, __LINE__); }

    int GetNextIndex(TData* data) const noexcept {
        if (!data) {
            return -1;
        }
        return ReadI32(reinterpret_cast<const std::byte*>(data) + kCommonNextOffset);
    }

    int GetNextIndex(int indexValue) const noexcept {
        const TData* const data = GetData(indexValue);
        return data ? GetNextIndex(const_cast<TData*>(data)) : -1;
    }

    TData* GetNext(TData* data) noexcept { return data ? GetData(GetNextIndex(data)) : nullptr; }

    TData* GetNext(int indexValue) noexcept { return GetData(GetNextIndex(indexValue)); }

    bool Push(int indexValue, int& listHead, int containerIndex) {
        L2_GUARD_FUNCTION();
        std::byte* const record = RecordBytes(indexValue);
        if (ReadI32(record + kCommonNextOffset) != -1 || ReadI32(record + kCommonBeforeOffset) != -1) {
            return false;
        }
        if (listHead == indexValue) {
            l2server_factory_detail::ReportListCycleFailure();
            return false;
        }

        WriteI32(record + kCommonContainerIndexOffset, containerIndex);
        WriteI32(record + kCommonNextOffset, listHead);
        if (listHead >= 0) {
            WriteRecordI32(listHead, kCommonBeforeOffset, indexValue);
        }
        listHead = indexValue;
        WriteI32(record + kCommonBeforeOffset, -2);
        return true;
    }

    bool Pop(int indexValue, int& listHead) {
        L2_GUARD_FUNCTION();
        std::byte* const record = RecordBytes(indexValue);
        const std::int32_t next = ReadI32(record + kCommonNextOffset);
        if (next == -1 && ReadI32(record + kCommonBeforeOffset) == -1) {
            l2server_factory_detail::ReportListFailure("Trying Ghost Pop!");
            return false;
        }
        if (listHead == -1) {
            l2server_factory_detail::ReportListFailure("null index entry");
            return false;
        }
        if (indexValue != listHead) {
            const std::int32_t containerIndex = ReadI32(record + kCommonContainerIndexOffset);
            if (containerIndex != ReadRecordI32(listHead, kCommonContainerIndexOffset)) {
                l2server_factory_detail::ReportListFailure(L"that object already gone to another container!");
                return false;
            }
            if (next != -1 && containerIndex != ReadRecordI32(next, kCommonContainerIndexOffset)) {
                l2server_factory_detail::ReportListFailure(L"that object already gone to another container!");
                return false;
            }
            const std::int32_t before = ReadI32(record + kCommonBeforeOffset);
            if (before != -1 && containerIndex != ReadRecordI32(before, kCommonContainerIndexOffset)) {
                l2server_factory_detail::ReportListFailure(L"that object already gone to another container!");
                return false;
            }
        }

        const std::int32_t before = ReadI32(record + kCommonBeforeOffset);
        if (before < 0) {
            if (listHead == indexValue) {
                listHead = next;
            }
        } else {
            WriteRecordI32(before, kCommonNextOffset, next);
        }
        const std::int32_t currentNext = ReadI32(record + kCommonNextOffset);
        if (currentNext >= 0) {
            WriteRecordI32(currentNext, kCommonBeforeOffset, ReadI32(record + kCommonBeforeOffset));
        }
        WriteI32(record + kCommonBeforeOffset, -1);
        WriteI32(record + kCommonNextOffset, -1);
        WriteI32(record + kCommonContainerIndexOffset, -1);
        return true;
    }

    bool Change(int indexValue, int& oldListHead, int& newListHead, int newContainerIndex) {
        L2_GUARD_FUNCTION();
        std::byte* const record = RecordBytes(indexValue);
        const std::int32_t next = ReadI32(record + kCommonNextOffset);
        if (next == -1 && ReadI32(record + kCommonBeforeOffset) == -1) {
            l2server_factory_detail::ReportListFailure("[Container::Change] Trying Ghost Pop! ");
            return false;
        }
        if (oldListHead == -1) {
            l2server_factory_detail::ReportListFailure("[Container::Change] null index entry");
            return false;
        }
        if (newListHead == indexValue) {
            l2server_factory_detail::ReportListCycleFailure();
            return false;
        }
        if (indexValue != oldListHead) {
            const std::int32_t containerIndex = ReadI32(record + kCommonContainerIndexOffset);
            if (containerIndex != ReadRecordI32(oldListHead, kCommonContainerIndexOffset)) {
                l2server_factory_detail::ReportListFailure(
                    L"[Container::Change] that object already gone to another container!"
                );
                return false;
            }
            if (next != -1 && containerIndex != ReadRecordI32(next, kCommonContainerIndexOffset)) {
                l2server_factory_detail::ReportListFailure(
                    L"[Container::Change] that object already gone to another container!"
                );
                return false;
            }
            const std::int32_t before = ReadI32(record + kCommonBeforeOffset);
            if (before != -1 && containerIndex != ReadRecordI32(before, kCommonContainerIndexOffset)) {
                l2server_factory_detail::ReportListFailure(
                    L"[Container::Change] that object already gone to another container!"
                );
                return false;
            }
        }

        const std::int32_t before = ReadI32(record + kCommonBeforeOffset);
        if (before < 0) {
            if (oldListHead == indexValue) {
                oldListHead = next;
            }
        } else {
            WriteRecordI32(before, kCommonNextOffset, next);
        }
        const std::int32_t currentNext = ReadI32(record + kCommonNextOffset);
        if (currentNext >= 0) {
            WriteRecordI32(currentNext, kCommonBeforeOffset, ReadI32(record + kCommonBeforeOffset));
        }
        WriteI32(record + kCommonContainerIndexOffset, newContainerIndex);
        const std::int32_t newHead = newListHead;
        WriteI32(record + kCommonNextOffset, newHead);
        if (newHead >= 0) {
            WriteRecordI32(newHead, kCommonBeforeOffset, indexValue);
        }
        newListHead = indexValue;
        WriteI32(record + kCommonBeforeOffset, -2);
        return true;
    }

    std::int32_t FreeIndex() const noexcept { return m_free_index; }
    std::int32_t FreeLast() const noexcept { return m_free_last; }
    std::int32_t ConstantCount() const noexcept { return runtime::raw_atomic::LoadI32(&m_const_num); }

    static constexpr std::size_t RequiredDataBytes() noexcept {
        return static_cast<std::size_t>(Define->max_data) * Dimensions::record_stride;
    }

    static constexpr std::size_t RequiredConstantBytes() noexcept {
        return static_cast<std::size_t>(Define->max_class) * Dimensions::constant_stride;
    }

    static constexpr std::size_t RequiredIndexBytes() noexcept {
        return static_cast<std::size_t>(Define->max_data) * sizeof(std::int32_t);
    }


private:
    static void SetError(std::string* error, const char* value) {
        if (error) {
            *error = value;
        }
    }

    static std::int32_t ReadI32(const std::byte* address) noexcept {
        std::int32_t value;
        std::memcpy(&value, address, sizeof(value));
        return value;
    }

    static void WriteI32(std::byte* address, std::int32_t value) noexcept { std::memcpy(address, &value, sizeof(value)); }

    std::byte* RecordBytes(std::int32_t indexValue) noexcept {
        return reinterpret_cast<std::byte*>(m_data) +
            static_cast<std::ptrdiff_t>(indexValue) * static_cast<std::ptrdiff_t>(Dimensions::record_stride);
    }

    const std::byte* RecordBytes(std::int32_t indexValue) const noexcept {
        return reinterpret_cast<const std::byte*>(m_data) +
            static_cast<std::ptrdiff_t>(indexValue) * static_cast<std::ptrdiff_t>(Dimensions::record_stride);
    }

    std::int32_t ReadRecordI32(std::int32_t indexValue, std::size_t offset) const noexcept {
        return ReadI32(RecordBytes(indexValue) + offset);
    }

    void WriteRecordI32(std::int32_t indexValue, std::size_t offset, std::int32_t value) noexcept {
        WriteI32(RecordBytes(indexValue) + offset, value);
    }

    static_assert(kLockCount == 0x20000);
    static_assert((kLockCount & (kLockCount - 1)) == 0);
    static_assert(Define->max_data > 0);
    static_assert(Dimensions::record_stride >= 0x40);
    static_assert(Dimensions::constant_stride > 0);

public:
    // The original header exposes counters publicly and the mapping prefix
    // through template helpers. Keeping the complete physical image under
    // one access level also makes the portable type standard-layout, so its
    // compile-time offset audit is well-defined on every host compiler.
    void* m_hfm_data = nullptr;
    TData* m_data = nullptr;
    ::CYieldLock Lock;
    void* m_hfm_const = nullptr;
    TConstant* m_const = nullptr;
    volatile std::int32_t m_const_num = 0;
    void* m_hfm_index = nullptr;
    std::int32_t* m_index = nullptr;
    std::int32_t m_free_index = -1;
    std::int32_t m_free_last = -1;
    ::CYieldLock EachLock[kLockCount];
    std::int32_t m_nAdded = 0;
    std::int32_t m_nRemoved = 0;
    std::int32_t m_nTotal = 0;
    std::int32_t m_nRemain = 0;
};

using CAirShipFactory = CSharedFactory<CAirShip, CSharedAirShipData, CSharedAirShipConstant, &SharedDefine_AirShip>;
using CAllianceFactory = CSharedFactory<CAlliance, CSharedAllianceData, CSharedAllianceConstant, &SharedDefine_Alliance>;
using CCreatureFactory = CSharedFactory<CCreature, CSharedCreatureData, CSharedCreatureConstant, &SharedDefine_Creature>;
using CItemFactory = CSharedFactory<CItem, CSharedItemData, CSharedItemConstant, &SharedDefine_Item>;
using CGeneralObjectFactory =
    CSharedFactory<CGeneralObject, CSharedGeneralObjectData, CSharedGeneralObjectConstant, &SharedDefine_GeneralObject>;
using CPartyFactory = CSharedFactory<CParty, CSharedPartyData, CSharedPartyConstant, &SharedDefine_Party>;
using CPledgeFactory = CSharedFactory<CPledge, CSharedPledgeData, CSharedPledgeConstant, &SharedDefine_Pledge>;
using CShuttleFactory = CSharedFactory<CShuttle, CSharedShuttleData, CSharedShuttleConstant, &SharedDefine_Shuttle>;
using CStaticObjectFactory =
    CSharedFactory<CStaticObject, CSharedStaticObjectData, CSharedStaticObjectConstant, &SharedDefine_StaticObject>;


#endif // L2_BUILD_L2SERVER
