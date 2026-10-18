#pragma once

#include "NPC.h"
#include "PetDataSheet.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>

enum PetCommandType : std::int32_t {
    PCMD_NONE = -1,
    PCMD_CHANGE_MOVEMODE = 0,
    PCMD_ATTACK = 1,
    PCMD_STOP = 2,
    PCMD_GET_ITEM = 3,
    PCMD_SPECIAL_SKILL = 4,
    PCMD_TELEPORT = 5,
    PCMD_MOVE_TO_TARGET = 6,
    PCMD_OFFENSIVE_AI = 7,
    PCMD_DEFENSIVE_AI = 8,
    PCMD_PASSIVE_AI = 9,
    PCMD_FAST_REACTION = 10,
    PCMD_NORMAL_REACTION = 11,
    PCMD_SLOW_REACTION = 12,
};

class CSummon;
class CEtcItem;
class CPet;
class User;
class AtomicJob;
class CDB;
class CDBSocket;
class NpcSocket;
class CSkillEffect_i_consume_summon;

// Current pet.cpp keeps only these four class-ID/name publications. The
// matched TotalChecker type/source records close their pet.h declarations and
// layouts. No current ctor, OnDespawn, vtable or RTTI body survives.
class CPetDespawnInfo : public MemoryObject {
public:
    CPetDespawnInfo() = default;
    ~CPetDespawnInfo() override = default;

    L2_DECLARE_OBJECT_TYPE(CPetDespawnInfo)

    virtual void OnDespawn(User& user) = 0;
};

class CPetDespawnOnOlympiadPlayer : public CPetDespawnInfo {
public:
    explicit CPetDespawnOnOlympiadPlayer(FVector& position);
    ~CPetDespawnOnOlympiadPlayer() override = default;

    L2_DECLARE_OBJECT_TYPE(CPetDespawnOnOlympiadPlayer)

    void OnDespawn(User& user) override;

private:
    FVector m_Pos;
};

class CPetDespawnOnOlympiadObserver : public CPetDespawnInfo {
public:
    explicit CPetDespawnOnOlympiadObserver(FVector& position);
    ~CPetDespawnOnOlympiadObserver() override = default;

    L2_DECLARE_OBJECT_TYPE(CPetDespawnOnOlympiadObserver)

    void OnDespawn(User& user) override;

private:
    FVector m_Pos;
};

class CPetDespawnOnEventMatchObserver : public CPetDespawnInfo {
public:
    explicit CPetDespawnOnEventMatchObserver(int pvpMatchType);
    ~CPetDespawnOnEventMatchObserver() override = default;

    L2_DECLARE_OBJECT_TYPE(CPetDespawnOnEventMatchObserver)

    void OnDespawn(User& user) override;

private:
    int m_PvpMatchType;
};


bool NpcKillMe(NpcSocket* socket, const unsigned char* packet);

namespace DBPacketHandler {
bool ReplyLoadPetItems(CDBSocket* socket, const unsigned char* packet);
bool _ReplyWithdrawPet(
    std::uint32_t userServerId,
    std::uint32_t petCollarServerId,
    int result,
    CPet* pet,
    std::int64_t sp,
    int meal,
    int slot0,
    int slot1,
    int slot2,
    std::int64_t experience,
    double hp,
    double mp,
    wchar_t* name,
    int loadItems,
    std::int64_t downExp,
    int alive,
    int deathCount
);
} // namespace DBPacketHandler

namespace PetSummonActionHelper {
bool Help(User* user, CSummon* summon, PetCommandType command, bool controlPressed, bool shiftPressed, int actionId);
}

enum PetCheckCond : std::int32_t {
    COND_COLLAR = 0,
    COND_CLASS = 1,
};


class CPetManager {
public:
    struct PetStruct {
        std::uint32_t PetDBID;
        std::uint32_t PetSID;
    };

    CPetManager();

    void PreCalc();
    double MasterExpRatio(int baseRatio, int masterLevel, int summonLevel);
    double SummonExpRatio(int baseRatio, int masterLevel, int summonLevel);
    static double GetExpPenalty(int petIndex, int level);
    bool IsSummonedPetCollar(std::int32_t itemClassId);
    bool LinkPet(User* user, CPet* pet);
    bool CreatePet(User* user, CPet* pet);
    void DeletePet(std::uint32_t userDatabaseId, std::uint32_t userServerId);
    void DeleteAll();

private:
    void _DeletePet(std::uint32_t userDatabaseId, std::uint32_t userServerId);

    std::map<std::uint32_t, PetStruct> m_UserPetDataDB; // +0x000
    std::set<std::int32_t> m_PetCollarDB; // +0x020
    CYieldLock m_Sect; // +0x040
    double m_petExpPenalty[2][122]; // +0x050
};


extern CPetManager g_PetManager;

class TransactionLock {
public:
    TransactionLock() : m_nTransactionStatus(0) {}
    bool Begin() { return runtime::raw_atomic::CompareExchangeI32(&m_nTransactionStatus, 1, 0) == 0; }
    void End() { runtime::raw_atomic::ExchangeI32(&m_nTransactionStatus, 0); }

private:
    volatile std::int32_t m_nTransactionStatus;
};


// Current L2Server's direct CNPC-derived summon owner. The scalar ordering
// and source access come from the same-source type donor. Every offset and the
// expanded real lock/vector tail are fixed by the current
// constructor, copy constructor, accessors, and Copy allocation extent.
class CSummon : public CNPC {
public:

    // Current CNPC::Set(NPC_ID, int64) directly updates the low byte of the
    // first CSummon tail cell for the two pet prototype IDs.
    friend class CNPC;
    // Current CAttackAction::GenerateAttackTargetData reads the protected
    // master DBID cell directly after its second exact CastSummon route.
    friend class CAttackAction;
    // Current CCreature::IsDuelEnemy reads the betrayal flag directly after
    // the exact CastSummon virtual route.
    friend class CCreature;
    friend class CSkillEffect_p_betray;

    CSummon();
    CSummon(const CSummon& other);
    ~CSummon() override;

    static void InitSummon();

    void TimerExpired(int id) override;
    bool CanBeSeenBy(CObject& object) override;
    CObject* Copy() override;
    void CheckControlableCategory();
    void CheckMultiSummonCategory();
    void CheckMultiSummonPoint();
    static int CheckMultiSummonPoint(std::int32_t npcClassId);

    bool IsAlive() override;
    bool IsSummon() const override;
    bool IsSummonOrPet() const override;
    bool IsNewSummon() const;
    bool IsPCOrSummon() const override;
    bool IsPCOrPCRelatedNPC() const override;
    CSummon* CastSummon() override;
    User* GetUserOrMaster() override;
    bool IsRealEnemyTo(CCreature* creature) override;
    bool IsEnemyTo(CCreature* creature) override;
    bool IsMakeAttackerGuilty(CCreature* attacker, bool flag) override;
    bool IsMakeAttackerChao(CCreature* attacker) override;
    void OnCreated() override;
    void OnEnterWorld(FVector& position) override;
    CCreature* GetDelegate() override;
    void SetSpelledInfoChanged() override;
    std::int64_t ExpInc(std::int64_t experience, bool applyBonus) override;
    User* GetMaster() const override;
    void SetMaster(User* master) override;
    bool ValidatePHit() override;
    bool ValidatePAttack() override;
    bool ValidateMAttack() override;
    bool ValidatePDefend() override;
    bool ValidateMDefend() override;
    bool ValidateAttackSpeed() override;
    bool ValidateMUseSpeed() override;
    bool ValidatePUseSpeed() override;
    bool ValidatePCriticalProb() override;
    bool ValidateBaseHP() override;
    bool Morph(int classId) override;
    void UnMorph() override;
    void ChangeMorph() override;
    bool ValidateSpeed() override;
    void CheckSummonBattleMode() override;
    void CheckSummonBattleTimeStamp() override;
    void SendSystemMessage(unsigned int messageId) override;
    void SendSystemMessage_d(unsigned int messageId, int value) override;
    void SendSystemMessage_Q(unsigned int messageId, std::int64_t value) override;
    void SendSystemMessageWithSelf(unsigned int messageId) override;
    void LeaveWorld(bool save) override;
    bool Die(CCreature* killer, int reason) override;
    void SendResurrectionConfirmDlg() override;
    void OnCorpseDecayed() override;
    bool CanForceAttack() const override;
    CParty* GetParty() override;
    double GetCriticalDamagePer() override;
    double GetCriticalDamageDiff() override;
    void EnterWorld(bool unknown, int first, int second, unsigned int third) override;

    bool CheckConsumeItemCondition();
    void ConsumeItem();
    void UpdateSummonByPassive();
    void UpdateSummonItemCountByRemainTime();
    void ValidateSummonAttackSpeed();
    void SendSummonDelete();
    void SendSummonInfo(bool first, bool second);
    void SendSummonStatusToMaster(int reason);
    void SendSpelledInfoChanged();
    void DeleteInfectedSkills(AtomicJob* atomicJob);
    void _DeleteInfectedSkills();
    int SaveInfectedSkills(AtomicJob* atomicJob);
    int _SaveInfectedSkills();
    void AddSummonStatBonus(CCreatureSkillMod::SummonStatBonusMod mod);
    void AddSummonPassiveBonus(CCreatureSkillMod::SummonStatBonusMod mod);
    int GetSummonPoint() { return m_nSummonPoint; }
    int GetIndexForMaster() const { return m_nIndexForMaster; }
    bool Controlable() { return m_Controlable; }

    // Current appended slots 443..447, in exact order.
    virtual int GetSoulShotCount();
    virtual int GetSpiritShotCount();
    virtual bool ValidateBaseMP();
    virtual int GetCurrentLife();
    virtual int GetMaxLife();

protected:
    friend class CDB;
    friend class CCreature;
    friend class CPetManager;
    friend class User;
    friend class CSkillEffect_i_consume_summon;
    friend bool NpcKillMe(NpcSocket* socket, const unsigned char* packet);
    friend bool PetSummonActionHelper::Help(
        User* user, CSummon* summon, PetCommandType command, bool controlPressed, bool shiftPressed, int actionId
    );

    std::int32_t m_MasterWorldID; // +0x3d78
    std::uint32_t m_MasterUserDBID; // +0x3d7c
    std::uint32_t m_MasterUserSID; // +0x3d80
    std::uint32_t m_InvalidMasterTick; // +0x3d84
    std::uint32_t m_LifeTimeTick; // +0x3d88
    std::int32_t m_nConsumeItemClassID; // +0x3d8c
    std::int64_t m_nConsumeItemCount; // +0x3d90
    volatile std::int32_t m_nConsumedCount; // +0x3d98
    std::int32_t m_petIndex; // +0x3d9c
    std::int32_t m_nOrgDuration; // +0x3da0
    bool m_betrayOwnMaster; // +0x3da4
    bool m_bMultiSummon; // +0x3da5
    bool m_Controlable; // +0x3da6
    std::int32_t m_nSummonPoint; // +0x3da8

private:
    static std::int32_t s_MultiSummonCategoryID;
    static std::int32_t s_ControlCategoryID;
    static std::int32_t s_MultiSummonTwoPointCategoryID;

    std::int32_t m_nSpelledInfoChanged; // +0x3dac
    std::int32_t m_nIndexForMaster; // +0x3db0

public:
    std::int32_t m_SummonDBSaveTick; // +0x3db4
    CYieldLock m_TransferItemSkillSect; // +0x3db8
    std::vector<const CSkillInfo*> m_TransferItemSkills; // +0x3dc8
    std::array<std::byte, 0x08> m_partialGap3de0To3de8;
};


// Current CPet adds exactly 0x50 bytes over CSummon. Current constructor,
// accessors and consumers close the complete tail below. Unrecovered
// behavior remains explicit at method level.
class CPet : public CSummon {
public:

    friend class CPetManager;
    friend class CCreature;
    friend bool DBPacketHandler::ReplyLoadPetItems(CDBSocket* socket, const unsigned char* packet);
    friend bool DBPacketHandler::_ReplyWithdrawPet(
        std::uint32_t userServerId,
        std::uint32_t petCollarServerId,
        int result,
        CPet* pet,
        std::int64_t sp,
        int meal,
        int slot0,
        int slot1,
        int slot2,
        std::int64_t experience,
        double hp,
        double mp,
        wchar_t* name,
        int loadItems,
        std::int64_t downExp,
        int alive,
        int deathCount
    );

    void OnMasterLeaveWorld();

    CPet();
    CPet(const CPet& other);
    ~CPet() override;

    static void InitPet();
    static bool DestroyPet(User* user, CEtcItem* petCollar);
    static bool GetPetData(int npcClassId, int& level, std::int64_t& experience, int& meal);
    static int GetLevel(int petIndex, std::int64_t experience);
    static bool CreatePet(User* user, int itemClassId, int npcClassId, int level);
    static bool CreatePetForAdmin(int userDatabaseId, int itemClassId, int npcClassId, int level);

    void TimerExpired(int id) override;
    CObject* Copy() override;
    void CheckCategory();
    void InitObject();
    void ResetParameter();
    void SetPetIndex(std::int32_t petIndex);

    bool IsPet() override;
    bool IsSummon() const override;
    CPet* CastPet() override;
    unsigned int GetDBID() const override { return m_CollarDBID; }
    void SetDBID(unsigned int dbId) override;
    bool ChangePetName(const char16_t* name);
    void PetNameChanged(const wchar_t* name);
    void SendDB(const char* format, ...) const override;
    int GetPetIndex() const { return m_petIndex; }
    RideType GetRideType();
    int GetMaxMeal() const;
    bool IsSyncLevel() const;
    bool ValidateOrgHP() override;
    bool ValidateOrgMP() override;
    double EquipedArmorsValidate(VALIDATE::Enum type) const override;
    std::int64_t SetExp(std::int64_t experience) override;
    std::int64_t ExpInc(std::int64_t experience, bool applyBonus) override;
    std::int64_t SPInc(std::int64_t amount) override;
    bool ValidatePAttack() override;
    bool ValidateMAttack() override;
    bool ValidatePDefend() override;
    bool ValidateMDefend() override;
    bool ValidateAttackSpeed() override;
    bool ValidateMUseSpeed() override;
    bool ValidatePUseSpeed() override;
    bool ValidatePCriticalProb() override;
    bool ValidateOrgHPRegen() override;
    bool ValidateOrgMPRegen() override;
    bool ValidateBaseHP() override;
    bool ValidateOrgPAttack() override;
    bool ValidateOrgPDefend() override;
    bool ValidateOrgMAttack() override;
    bool ValidateOrgMDefend() override;
    bool Morph(int classId) override;
    void UnMorph() override;
    void ChangeMorph() override;
    bool ValidateSpeed() override;
    bool ValidateLevelUP() override;
    int EquipItem(CItem* item, ::SlotType slotType) override;
    void UnequipWeapon(bool force) override;
    void RestoreUnequipedWeapon() override;
    bool DropItem(CItem* item, std::int64_t count, FVector& position, bool unknown) override;
    bool GetItem(CItem* item, bool unknown) override;
    int GetInventoryLimit() override;
    int GetQuestInventoryLimit() override;
    bool AddItemToInventory2(CItem* item) override;
    bool Die(CCreature* killer, int reason) override;
    void OnCorpseDecayed() override;
    bool CanDoAction_china() override;
    void AnnouncePickItem(CItem* item) override;
    void OnNpcSocketClose() override;

    bool Hungry();
    bool IsPremiumPet() const;
    void EatFood(int delta);
    void ExpDown(std::int64_t experience);
    void OnDieDropItem(bool reducedRate);
    void ValidateMeal() override;
    void SetLevel(int level);
    int GetItemCount();
    void AutoEquip();
    void SendPetItemList(bool changedItemsOnly);
    bool ValidateLevel() override;

    int GetSoulShotCount() override;
    int GetSpiritShotCount() override;
    bool Usable(CItem* item);
    bool ValidateBaseMP() override;
    int GetCurrentLife() override;
    int GetMaxLife() override;

    // Current appended slots 448..449.
    virtual bool ValidateSyncLevel();
    virtual void OnValidateLevel();

    void OnLevelChanged(int oldLevel) override;

    TransactionLock mTrLock; // +0x3de8

protected:
    std::uint32_t m_CollarDBID; // +0x3dec
    bool m_bZolaHungry; // +0x3df0
    std::uint32_t m_ZolaHungryStartTick; // +0x3df4
    bool m_bStarved; // +0x3df8
    std::uint32_t m_MealTick; // +0x3dfc
    std::uint32_t m_DBSaveTick; // +0x3e00
    bool m_bFirstTime; // +0x3e04
    std::uint32_t m_SysMsgTick; // +0x3e08
    std::int32_t m_nSlots[3]; // +0x3e0c
    std::uint32_t m_ZolaHungryTerm; // +0x3e18
    RideType m_rideType; // +0x3e1c
    bool m_bRestartDead; // +0x3e20
    bool m_bItemDroppedByHungry; // +0x3e21
    volatile std::int32_t m_nSaveToDBTick; // +0x3e24
    PetDataSheet* m_pPetSheet; // +0x3e28
    PetDataSheet::Record* m_pPetData; // +0x3e30

private:
    static std::int32_t s_StriderCategoryID;
    static std::int32_t s_WolfCategoryID;
    static std::int32_t s_WyvernCategoryID;
};


class CCommonRidingInfo {
public:
    CCommonRidingInfo() = default;
    ~CCommonRidingInfo() = default;

    void Init();
    const PetDataSheet::Record& Record(int level);
    const PetDataSheet::Record& Record();
    void RideStrider(CPet* pet);
    void RideWyvern(CPet* pet);
    void EventRide(CPet* pet, int level);
    void OnRide(const PetDataSheet::Record& record, double collisionRadius, double collisionHeight);
    RideType GetRideType() { return m_rideType; }

private:
    friend class CUserRidingInfo;

    std::int32_t m_striderDBID; // +0x00
    std::int32_t m_striderClassID; // +0x04
    std::int32_t m_striderPetIndex; // +0x08
    std::int32_t m_striderLevel; // +0x0c
    double m_orgCollisionRadius; // +0x10
    double m_orgCollisionHeight; // +0x18
    double m_attackSpeedOnRide; // +0x20
    double m_maxHPOnRide; // +0x28
    double m_maxMPOnRide; // +0x30
    RideType m_rideType; // +0x38
};


class CAbstractRidingInfo {
public:
    CAbstractRidingInfo() = default;
    virtual ~CAbstractRidingInfo() = default;

    virtual void Ride(CPet* pet, int value) = 0;
    virtual void OnRide(const PetDataSheet::Record& record) = 0;
    virtual int Meal() const = 0;
    virtual int MaxMeal() const = 0;
    virtual bool IsMealEmpty() = 0;
    virtual bool IsHungry(int hungryLimit) = 0;
    virtual int ConsumeAmount(bool inCombat) = 0;
    virtual bool IsMealTime() = 0;
    virtual bool Eat(int amount) = 0;
    virtual bool IsEventRiding() = 0;
    virtual int TimeLeft() = 0;
    virtual void Init() = 0;
};


class CNormalRidingInfo : public CAbstractRidingInfo {
public:
    void Ride(CPet* pet, int meal) override;
    void OnRide(const PetDataSheet::Record& record) override;
    int Meal() const override;
    int MaxMeal() const override;
    bool IsMealEmpty() override;
    bool IsHungry(int hungryLimit) override;
    int ConsumeAmount(bool inCombat) override;
    bool IsMealTime() override;
    bool Eat(int amount) override;
    bool IsEventRiding() override;
    int TimeLeft() override;
    void Init() override;

protected:
    std::int32_t m_striderMealTick; // +0x08
    std::int32_t m_striderMeal; // +0x0c
    std::int32_t m_striderMaxMeal; // +0x10
    std::int32_t m_consumeStriderMeal_OnCombat; // +0x14
    std::int32_t m_consumeStriderMeal_OnNormal; // +0x18
};


class CEventRidingInfo : public CAbstractRidingInfo {
public:
    void Ride(CPet* pet, int durationMinutes) override;
    void OnRide(const PetDataSheet::Record& record) override;
    int Meal() const override;
    int MaxMeal() const override;
    bool IsMealEmpty() override;
    bool IsHungry(int hungryLimit) override;
    int ConsumeAmount(bool inCombat) override;
    bool IsMealTime() override;
    bool Eat(int amount) override;
    bool IsEventRiding() override;
    int TimeLeft() override;
    void Init() override;

protected:
    std::int32_t m_duration; // +0x08
    std::int32_t m_startTime; // +0x0c
};


class CUserRidingInfo {
public:
    CUserRidingInfo() = default;
    ~CUserRidingInfo() = default;

    void OnEnterWorld();
    void Init();
    const PetDataSheet::Record& Record(int level) { return m_commonInfo.Record(level); }
    const PetDataSheet::Record& Record() { return m_commonInfo.Record(); }
    bool IsSame(PetCheckCond condition, int value);
    int DBID() { return m_commonInfo.m_striderDBID; }
    int Level() { return m_commonInfo.m_striderLevel; }
    double Radius() { return m_commonInfo.m_orgCollisionRadius; }
    double Height() { return m_commonInfo.m_orgCollisionHeight; }
    double AttackSpeed() { return m_commonInfo.m_attackSpeedOnRide; }
    double MaxHP() { return m_commonInfo.m_maxHPOnRide; }
    double MaxMP() { return m_commonInfo.m_maxMPOnRide; }
    int StriderClassID() { return m_commonInfo.m_striderClassID; }
    bool RideStrider(CPet* pet, double collisionRadius, double collisionHeight);
    bool RideWyvern(CPet* pet, double collisionRadius, double collisionHeight);
    bool RideForEvent(CPet* pet, int level, double collisionRadius, double collisionHeight, int durationMinutes);
    void MakeLog(int type, User* user);
    void MakeEventLog(User* user, int eventId);
    int Meal() const { return m_pRidingInfo->Meal(); }
    int MaxMeal() const { return m_pRidingInfo->MaxMeal(); }
    bool IsMealEmpty() { return m_pRidingInfo->IsMealEmpty(); }
    bool IsHungry() { return m_pRidingInfo->IsHungry(Record().m_nHungryLimit); }
    int ConsumeAmount(bool inCombat) { return m_pRidingInfo->ConsumeAmount(inCombat); }
    bool IsMealTime() { return m_pRidingInfo->IsMealTime(); }
    bool Eat(int amount) { return m_pRidingInfo->Eat(amount); }
    bool IsEventRiding() { return m_pRidingInfo->IsEventRiding(); }
    int TimeLeft() { return m_pRidingInfo->TimeLeft(); }

private:
    CCommonRidingInfo m_commonInfo; // +0x00
    CNormalRidingInfo m_normalInfo; // +0x40
    CEventRidingInfo m_eventInfo; // +0x60
    CAbstractRidingInfo* m_pRidingInfo; // +0x70
};
