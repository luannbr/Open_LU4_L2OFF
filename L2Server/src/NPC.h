#pragma once
#include "Creature.h"
#include "Protocol/src/L2Constants.h"
#include "Protocol/src/NPCProtocol.h"

#include "Protocol/src/L2NPCProtocol.h"

#include "Shared/src/IOC.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

class NpcSocket;
bool NpcCreateOnePrivate(NpcSocket* socket, const unsigned char* packet);

// Current L2Server keeps the typed packet boundary. TotalChecker2015
// supplies the same-source declaration vocabulary, while current L2NPC
// producers and validation corroborate the complete 0/1 domain.
enum NpcAttackMoveType : std::int32_t {
    AMT_STAND = 0,
    AMT_MOVE_TO_TARGET = 1,
};

// Current L2Server accepts exactly these three semantic cases and rejects
// every other integer in CNPC::IsItemChanceAffectedNPCType.
enum ItemChanceNpcType : std::int32_t {
    ICN_NORMAL = 0,
    ICN_BOSS = 1,
    ICN_BOTH = 2,
};

enum DropItemType : std::int32_t {
    DROPITEM_MULTI = 0,
    DROPITEM_EX = 1,
    DROPITEM_VITALITY = 2,
};

namespace NpcTypeForDeco {

enum Type : std::int32_t {
    NotDecoNpc = 0,
    DecoNpcInRealWorld = 1,
    DecoNpcInInstantAgit = 2,
};

} // namespace NpcTypeForDeco


struct OrgSpeeds {
    std::int32_t m_nOrgSpeed0;
    std::int32_t m_nOrgSpeed1;
    std::int32_t m_nOrgSpeed2;
    std::int32_t m_nOrgSpeed3;
    std::int32_t m_nOrgSpeed4;
    std::int32_t m_nOrgSpeed5;
    std::int32_t m_nOrgSpeed6;
    std::int32_t m_nOrgSpeed7;
};


struct ItemDropStruct {
    ItemDropStruct(std::int32_t itemClassId, std::int64_t dropMin, std::int64_t dropMax, std::int32_t dropProb) :
        ItemClassId(itemClassId), DropMin(dropMin), DropMax(dropMax), DropProb(static_cast<double>(dropProb)) {}

    ItemDropStruct() : ItemClassId(0), DropMin(0), DropMax(0), DropProb(0.0) {}

    std::int32_t ItemClassId;
    std::int64_t DropMin;
    std::int64_t DropMax;
    double DropProb;
};

struct ItemDropMultiStruct {
    ItemDropMultiStruct() : pItemDropList(nullptr), DropProb(0.0), m_bChanceAffected(false) {}

    std::vector<ItemDropStruct*>* pItemDropList;
    double DropProb;
    bool m_bChanceAffected;
};


// Exact current NPC.obj load-time carrier. The current map value consists
// solely of the ordered ignore-clan set. Both containers use the target
// 0x20-byte ABI envelope.
struct CNPCLoadingData {
    std::set<std::int32_t> m_IgnoreDB;
};


extern std::map<std::int32_t, CNPCLoadingData> g_NPCLoadingDataMap;

// NPC.cpp, current L2Server 0x00888984. This startup pass derives the
// 15000-by-15000 clan-help relation bit table from the registered NPC
// prototypes after CObjectDB::LoadNPC has completed.
void ComputeClanHelpTable();

// Exact current prototype-data owner. Current construction and all consumer
// offsets fix the expanded VS2017 container layout at 0x1b8. The older
// same-source donor contributes member vocabulary and element types only.
class CNPCClassConst {
public:
    CNPCClassConst() {
        m_IsDeathPenalty = false;
        m_nItemDropValue = 0;
        std::memset(m_SpeedByME, 0, sizeof(m_SpeedByME));
        std::memset(m_RegenByME, 0, sizeof(m_RegenByME));
        std::memset(m_NoiseRangeByME, 0, sizeof(m_NoiseRangeByME));
        std::memset(m_dCollisionHeight, 0, sizeof(m_dCollisionHeight));
        std::memset(m_dCollisionRadius, 0, sizeof(m_dCollisionRadius));
        m_nDefaultChestItemClassID = 0;
        m_nDefaultLHandItemClassID = 0;
        m_nDefaultRHandItemClassID = 0;
        m_nHpRate = 0.0;
        m_BaseAttribute.Init();
    }

    std::int32_t m_SpeedByME[2][4];
    std::int32_t m_RegenByME[2][4];
    std::int32_t m_NoiseRangeByME[2][4];
    std::vector<std::int64_t> m_SkillList;
    std::vector<ItemDropStruct> m_CorpseMakeList;
    std::vector<ItemDropMultiStruct> m_ItemAdditionalMakeListMulti;
    std::vector<ItemDropMultiStruct> m_ExItemDropListMulti;
    std::vector<ItemDropMultiStruct> m_ItemVitalityDropListMulti;
    std::vector<ItemDropMultiStruct> m_ItemLuckyStatListMulti;
    std::map<std::u16string, std::u16string, runtime::CStringLess<std::u16string>> m_mapParameter;
    std::int32_t m_nDefaultChestItemClassID;
    std::int32_t m_nDefaultLHandItemClassID;
    std::int32_t m_nDefaultRHandItemClassID;
    double m_dCollisionHeight[2];
    double m_dCollisionRadius[2];
    double m_nHpRate;
    BaseAttribute m_BaseAttribute;
    MPRewardData m_MPRewardData;
    std::int64_t m_nItemDropValue;
    bool m_IsDeathPenalty;
};


class CObjectDB;
class ExDropItemData;
class QuestNpcEventController;
class User;
enum QuestNpcEventType : std::int32_t;

bool NpcEnterWorldPacket(NpcSocket* socket, const unsigned char* packet);
bool NpcCreatePacket(NpcSocket* socket, const unsigned char* packet);

// Current Droid-player appearance carrier. The complete CodeView field list
// and the current SendCharInfo_internal loads fix every field
// and offset below.
struct alignas(8) DroidNPCInfo {
    char cGender;
    char cRace;
    std::array<std::byte, 0x02> alignment02To04;
    std::int32_t nBaseClass;
    std::int32_t nNowClass;
    char cHero;
    char cNobless;
    std::array<std::byte, 0x02> alignment0eTo10;
    double dCollisionRadius;
    double dCollisionHeight;
    std::int32_t nPledgeType;
    std::int32_t nPledgeDBID;
    std::int32_t nPledgeSID;
    std::int32_t nPledgeCrestDBID;
    std::int32_t nPledgeEmblemDBID;
    std::int32_t nAllianceDBID;
    std::int32_t nAllianceCrestDBID;
    std::int32_t nFace;
    std::int32_t nHairShape;
    std::int32_t nHairColor;
    std::int32_t nFaceEx;
    std::int32_t nHairShapeEx;
    std::int32_t nHairColorEx;
    char cHairAccFlag;
    std::array<std::byte, 0x03> alignment55To58;
    std::int32_t nSlotUnderware;
    std::int32_t nSlotHead;
    std::int32_t nSlotRightHand;
    std::int32_t nSlotLeftHand;
    std::int32_t nSlotBothHands;
    std::int32_t nSlotGloves;
    std::int32_t nSlotChest;
    std::int32_t nSlotLegs;
    std::int32_t nSlotFeet;
    std::int32_t nSlotBack;
    std::int32_t nSlotHair;
    std::int32_t nSlotHair2;
    std::int32_t nShapeShiftRightHand;
    std::int32_t nShapeShiftLeftHand;
    std::int32_t nShapeShiftBothHands;
    std::int32_t nShapeShiftGloves;
    std::int32_t nShapeShiftChest;
    std::int32_t nShapeShiftLegs;
    std::int32_t nShapeShiftFeet;
    std::int32_t nShapeShiftHair;
    std::int32_t nShapeShfitHair2;
    std::int32_t nDeco1;
    std::int32_t nDeco2;
    std::int32_t nDeco3;
    std::int32_t nDeco4;
    std::int32_t nDeco5;
    std::int32_t nDeco6;
    std::int32_t nEnchantWeapon;
    std::int32_t nEnchantChest;
    std::int32_t nEnchantLegs;
    std::int32_t nEnchantHead;
    std::int32_t nEnchantGloves;
    std::int32_t nEnchantFeet;
    std::int32_t nVariationOpt1;
    std::int32_t nVariationOpt2;
    std::int32_t nKarma;
    std::int32_t nNickNameColor;
    std::int32_t nCharNameColor;
    std::int32_t nAP;
    std::int32_t nBonusCount;
};


class CNPC : public CCreature {
public:
    friend struct NPCInfoPacket;
    friend class CContributeData;
    friend class User;
    friend bool NpcCreateOnePrivate(NpcSocket* socket, const unsigned char* packet);
    friend bool NpcCreatePacket(NpcSocket* socket, const unsigned char* packet);
    friend bool NpcEnterWorldPacket(NpcSocket* socket, const unsigned char* packet);

    L2_DECLARE_OBJECT_REGISTRATION(CNPC)
    static std::int32_t s_nMaxID;
    static double m_dAdenaDropValue1;
    static double m_dAdenaDropValue2;
    static double m_dItemDropValue1;
    static double m_dItemDropValue2;

    static void* operator new(std::size_t requestedSize);
    CNPC();
    CNPC(const CNPC& other);
    ~CNPC() override;

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;
    void TimerExpired(int id) override;
    bool IsNPC() override;
    CNPC* CastNPC() override;
    CObject* Copy() override;
    CNPC* CopyFakeNPC();
    void SetDefaultParameters();
    bool IsPCOrPCRelatedNPC() const override;
    int GetClassId() const override;
    void Delete() override;
    bool IsRealEnemyTo(CCreature* creature) override;
    bool IsEnemyTo(CCreature* creature) override;
    bool IsMakeAttackerGuilty(CCreature* attacker, bool flag) override;
    User* GetUserOrMaster() override;
    void ParamChanged(std::int64_t parameterIndex) override;
    void OnUseSkillFinished(int targetSid, int skillId, int skillLevel, int skillSubLevel, int success) override;
    void OnUseSkillStarted(int targetSid, int skillId, int skillLevel, int skillSubLevel) override;
    bool Die(CCreature* killer, int reason) override;
    void _OnAbnormalStatusChanged(
        const CSkillInfo& skillInfo, CCreature* caster, SkillPumpStartType startType, bool unknown
    ) override;
    void _OnAbnormalStatusDeleted(
        const CSkillInfo& skillInfo, CCreature* caster, SkillPumpEndType endType, int unknown
    ) override;
    void OnCorpseDecayed() override;
    bool FreezeIfNobodyNearMe() override;
    bool IsSummonedNpc() override;
    bool IsSummonedFriendNpc() override;
    void OnCreate() override;
    void OnCreateFakeNPC() override;

    void CacheMasterInfo(User* user);
    bool CopyUserBuffToNPC_Do();
    bool ProcessEnteringWorldServer(unsigned int inZoneId, const FVector& position);

    double EquipedArmorsValidate(VALIDATE::Enum type) const override;
    double RegenBonusByMotionEx() override;
    double HPRegenBonusByMotionEx() override;
    double MPRegenBonusByMotionEx() override;
    double CPRegenBonusByMotionEx() override;
    bool ValidateOrgHP() override;
    bool ValidateOrgMP() override;
    bool ValidateOrgCP() override;
    bool ValidateOrgHPRegen() override;
    bool ValidateOrgMPRegen() override;
    bool ValidateOrgCPRegen() override;
    bool ValidateOrgPAttack() override;
    bool ValidateOrgPDefend() override;
    bool ValidateOrgMAttack() override;
    bool ValidateOrgMDefend() override;
    bool Morph(int classId) override;
    void UnMorph() override;
    void ChangeMorph() override;
    bool ValidateOrgSpeed() override;
    bool ValidateSpeed() override;
    bool ValidateMoveToTargetSpeedAndSendInfo() override;
    void MoveToLocation(int x, int y, int z, PathFindType pathFindType) override;
    bool CanEquip(CItem* item) override;
    void TakeOffItems() override;
    bool EquipItemByClassID(int itemClassId);
    int EquipItem(CItem* item, ::SlotType slotType) override;
    ::SlotType UnequipItem(CItem* item, bool force) override;
    ::SlotType UnequipItem(::SlotType slotType, bool force) override;
    void UnequipWeapon(bool force) override;
    void RestoreUnequipedWeapon() override;
    int GetInventoryLimit() override;
    int GetQuestInventoryLimit() override;
    int GetArtifactInventoryLimit() override;
    bool AddItemToInventory2(CItem* item) override;
    void LeaveWorld(bool save) override;
    bool DropItem(CItem* item, std::int64_t count, FVector& position, bool unknown) override;
    bool TeleportToLocation(
        int x,
        int y,
        int z,
        unsigned int inZoneId,
        int heading,
        TeleportSourceType::Enum source,
        CCreature* requestor,
        int unknown
    ) override;
    bool TeleportWithAirShip(CAirShip* airShip, FVector position, unsigned int inZoneId) override;
    int GetCorpseTime() const override;
    bool IsInDuel() const override;
    void SetNickName(const char16_t* nickname) override;
    void SendMyInfo() override;
    void MakeEnterWorldPacket() override;
    void ClearEnterWorldPacket() override;
    void Suicide(DamageTypeEnum damageType, int reason) override;
    bool GetOnAirShip(CAirShip* airShip) override;
    bool GetOffAirShip(CAirShip* airShip, FVector position) override;

private:
    void SendNPCInfo(User* user, int type, FVector position, int distance, bool first, bool second);
    void SendNPCInfoImpl(User& user, int type);
    void SendNPCInfoImplForSpeed(User& user);
    void SendCharInfo_internal(User* user, bool createObject);

public:
    void BroadcastCharInfoUpdate();
    void SendCreateObject(User* user);
    void GetOrgSpeeds(OrgSpeeds& speeds);
    int GetNPCId();
    QuestNpcEventController* GetEventController(QuestNpcEventType eventType, bool create);
    FVector ChoiceValidPosition(const User* user, CSkillInfo* skillInfo);
    int GetPacketType();
    bool IsFakeNPC() const {
        const std::uint32_t objectId = static_cast<std::uint32_t>(m_data->m_nId);
        // return (objectId & UINT32_C(0xf8000000)) ==
        // UINT32_C(0x28000000)
        return (objectId & CLASS_MASK) == (SCLS_FAKE_NPC << CLASS_SHIFT);
    }
    bool IsFriendNPC() { return m_data->m_Race == RT_FRIEND; }
    bool IsSiegeWeapon() { return m_data->m_Race == RT_SIEGE_WEAPON; }
    bool IsCastleRelatedNPC() {
        return m_data->m_Race == RT_MERCENARY || m_data->m_Race == RT_CASTLE_GUARD || m_data->m_nClass == CT_SIEGE_UNIT ||
            m_data->m_nClass == CT_SIEGE_ATTACKER;
    }
    bool CanChangeDir() const { return m_bCanChanageDir; }
    bool CanGetNpcEvent();
    void SetDecoNpcInfo(NpcTypeForDeco::Type type, int agitCidOrPledgeDbId, int agitSlot);
    void SetMonsterBookNPC(bool value) { m_bMonsterBookNPC = value; }
    bool IsMonsterBookNPC() { return m_bMonsterBookNPC; }
    void SetAliveHour(int aliveHour) { m_nAliveHour = aliveHour; }
    int GetAliveHour() { return m_nAliveHour; }
    bool SendEnterWorldPacket(CCreature* creature, CPacketHelperEx<20480>& packet);
    void SendNPCInfoForSpecialCamera3(User* user, FVector position, int distance);
    void SocialAction(int actionId, int distance, bool broadcast) override;

    void SetName(const wchar_t* name);
    void Despawn(bool byScript, bool setDeadIfAlive);
    bool NeedToHideInfoAboutMaster();
    bool IsFriendlyNPC();
    bool IsItemChanceAffectedNPCType(ItemChanceNpcType type);
    double GetPrivateDropCorpseItemChanceProb(User* user);
    double GetPrivateDropMultiItemChanceProb(User* user);
    double GetPrivateDropExItemChanceProb(User* user);
    double GetPrivateDropVitalityProb();
    double GetDropItemChanceProb(DropItemType type);
    double GetItemDropPenaltyByLevelDiff();
    void DropItemToInventory(double probability);
    void DropExItemToInventory(double probability);
    void DropVitalityItemToInventory(double probability);
    void BeforeBossDropItemOnDying(int levelDifference);
    void BeforeDropItemOnDying(int levelDifference);
    void DropItemOnDying(CCreature* killer);
    void LogOnDying(CCreature* killer);
    void LuckyItemToInventory(User* user);
    bool KillFakeNpc(CCreature& creature);
    void State(int state);
    int State();
    void SetEnchantOfWeapon(int enchant) {
        m_nEnchantOfWeapon = enchant;
        ParamChanged(NPCT_ENCHANT_WEAPON);
    }
    int GetEnchantOfWeapon() const { return m_nEnchantOfWeapon; }
    void SetNickNameFStr(int stringId, const char16_t* nickname);
    void SetMasterName(const char16_t* masterName);
    void SetMasterNameFStr(int stringId, const wchar_t* masterName);
    void SetHPByMasterRate_Reserve(double rate) { m_dHPRate_Reserve = rate; }
    void SetMPByMasterRate_Reserve(double rate) { m_dMPRate_Reserve = rate; }
    void NullSkillAction(std::int64_t skillNameId, bool stand, bool force, int followType, int followData1);
    bool OnNpcAttackPacket(int targetId, NpcAttackMoveType moveType, int forceAttack);
    bool OnNpcMovePacket(int x, int y, int z);
    bool OnNpcAbsoluteReturn(int x, int y, int z, int mode);
    bool OnNpcFollow(int targetId, char followType, int first, int second);
    bool OnNpcBroadcastScriptEvent(int argument1, int argument2, int argument3, unsigned int radius);
    bool OnNpcBroadcastScriptEventCond(int argument1, int argument2, int argument3, unsigned int radius);
    bool OnNpcSetParam(CHAR_PARAM_TYPE parameter, double value);
    bool OnNpcDispel(int targetId, ::AbnormalStatusSlotType abnormalType);
    bool OnNpcUseSkillPacket(
        int targetId, std::int64_t skillNameId, NpcAttackMoveType moveType, int forceAttack, int followType, int followData1
    );
    void Say(const char16_t* text) override;
    void SayFStr(
        int stringId,
        wchar_t* text1,
        wchar_t* text2,
        wchar_t* text3,
        wchar_t* text4,
        wchar_t* text5,
        int value1,
        int value2,
        int value3,
        int value4,
        int value5
    );
    void Shout(const char16_t* text, int radius);
    void ShoutFStr(
        int stringId,
        wchar_t* text1,
        wchar_t* text2,
        wchar_t* text3,
        wchar_t* text4,
        wchar_t* text5,
        int radius,
        int value1,
        int value2,
        int value3,
        int value4,
        int value5
    );
    void _SetMultiDropList(std::vector<ItemDropMultiStruct*>* values);
    void setExDropItem(std::vector<int>* values);
    void SetBCCondList(std::vector<int>* conditions);
    void SetDoppelGangerType(char type) { m_eDoppelGangerType = type; }
    char GetDoppelGangerType() { return m_eDoppelGangerType; }
    bool IsSetResult() const { return m_bSetResult; }
    void SetResult(bool result) { m_bSetResult = result; }
    bool IsNPCRelatedToPledge() { return m_data->m_bIsNPCRelatedToPledge; }
    void SetAsSummonedNpc() { m_bIsSummonedNpc = true; }
    void SetAsSummonedFriendNpc() { m_bIsSummonedFriendNpc = true; }
    void setEventDropPorbType(int type) { m_nEventDropProbType = type; }
    int getEventDropPorbType() { return m_nEventDropProbType; }
    void SetNpcCustomizedSizeRate(double rate) { m_dNpcCustomizedSizeRate = rate; }
    double GetNpcCustomizedSizeRate() const { return m_dNpcCustomizedSizeRate; }
    void SetLogToError() { m_bLogToError = true; }

    // Current slots 422..442, in exact order.
    virtual bool IsRootNPC();
    virtual bool IsEnemyToAsNpc(CCreature* creature, bool unknown);
    virtual void EnterWorld(bool unknown, int first, int second, unsigned int third);
    virtual void OnTeleport();
    virtual bool DropItemAdena(CCreature* creature, CItem* item, std::int64_t count, FVector& position, bool unknown);
    virtual void SendNPCInfo(User* user, bool first, bool second);
    virtual void Set(ObjectFieldType field, std::vector<ItemDropMultiStruct*>* values);
    virtual void Set(ObjectFieldType field, std::vector<GoodStruct*>* values);
    virtual void Set(ObjectFieldType field, std::vector<ItemDropStruct*>* values);
    virtual void Set(ObjectFieldType field, std::vector<double>* values);
    virtual void Set(ObjectFieldType field, std::vector<std::int64_t>* values);
    virtual void Set(ObjectFieldType field, double value);
    virtual void Set(ObjectFieldType field, std::int64_t value);
    virtual void Set(CObjectDB& objectDb, ObjectFieldType field, wchar_t* value);
    virtual void ValidateMeal();
    virtual User* GetMaster() const;
    virtual void SetMaster(User* master);
    virtual User* GetSummoner() const;
    virtual int GetDoppelgangerSummonerSID();
    virtual void OnNpcSocketClose();
    virtual bool HasSummoner();


    std::uint64_t m_nParamChanged; // +0x38d8
    bool m_bSpeedChanged; // +0x38e0
    std::array<std::byte, 0x07> m_alignment38e1To38e8; // +0x38e1..+0x38e7
    double m_dHPRate_Reserve; // +0x38e8
    double m_dMPRate_Reserve; // +0x38f0
    volatile std::int32_t m_nNpcDeleteCount; // +0x38f8
    bool m_bIsPureAncestor; // +0x38fc
    bool m_bIsPureAncestorOrigin; // +0x38fd
    std::array<std::byte, 0x02> m_alignment38feTo3900; // +0x38fe..+0x38ff
    CNPCClassConst* m_pClassConst; // +0x3900

    std::int32_t m_MasterNameIndex; // +0x3908
    char16_t m_sMasterName[25]; // +0x390c..+0x393d
    bool m_bDespawnByScript; // +0x393e
    std::byte m_partialTail393f; // +0x393f
    std::int32_t m_nPledgeCrestId; // +0x3940
    std::int32_t m_nAllianceCrestId; // +0x3944
    std::uint32_t m_nAirShipServerID; // +0x3948
    bool m_bChargeFullHPBySkillEnterWorld; // +0x394c
    bool m_bChargeFullMPBySkillEnterWorld; // +0x394d
    bool m_bAbsoluteReturn; // +0x394e
    std::byte m_alignment394fTo3950; // +0x394f
    std::int32_t m_nReturnedTime; // +0x3950
    bool m_bHadFakeItem; // +0x3954
    std::array<std::byte, 0x03> m_alignment3955To3958; // +0x3955..+0x3957
    CYieldLock m_FakeItemLock; // +0x3958..+0x3967

    bool m_bCanDropItem; // +0x3968

    std::array<std::byte, 0x03> m_partialTail3969To396c; // +0x3969..+0x396b
    std::int32_t m_NickNameIndex; // +0x396c

private:
    std::int32_t m_nChestItemClassID; // +0x3970
    std::int32_t m_nRHandItemClassID; // +0x3974
    std::int32_t m_nLHandItemClassID; // +0x3978
    std::int32_t m_nOldRHandItemClassID; // +0x397c
    std::int32_t m_nOldLHandItemClassID; // +0x3980
    std::int32_t m_nOldChestItemClassID; // +0x3984
    std::int32_t m_nEnchantOfWeapon; // +0x3988
    std::int32_t m_State; // +0x398c
    std::int8_t m_eDoppelGangerType; // +0x3990
    bool m_bIsSummonedNpc; // +0x3991
    bool m_bIsSummonedFriendNpc; // +0x3992
    std::array<std::byte, 0x05> m_alignment3993To3998; // +0x3993..+0x3997
    double m_dLastCorpseItemChanceProb; // +0x3998
    double m_dLastMultiItemChanceProb; // +0x39a0
    double m_dLastExItemChanceProb; // +0x39a8
protected:
    bool m_bHasSummoner; // +0x39b0
    bool m_bCanChanageDir; // +0x39b1
    std::array<std::byte, 0x02> m_alignment39b2To39b4; // +0x39b2..+0x39b3

public:
    std::int32_t m_nFakeClassId; // +0x39b4
    std::int32_t m_nEnterWorldTime; // +0x39b8
    std::int32_t m_nFakeNpcSuicideTime; // +0x39bc
    std::uint32_t m_sidKillerMaster; // +0x39c0
    std::int32_t m_nKillerMemberCount; // +0x39c4

protected:
    bool m_bSetResult; // +0x39c8

private:
    std::array<std::byte, 0x03> m_alignment39c9To39cc; // +0x39c9..+0x39cb
    std::int32_t m_nEventDropProbType; // +0x39cc

public:
    std::vector<ExDropItemData*> m_vExDropItem; // +0x39d0..+0x39e7
    std::array<std::byte, 0x08> m_alignment39e8To39f0; // +0x39e8..+0x39ef
    std::vector<std::int32_t> m_vBCCondition; // +0x39f0..+0x3a07

private:
    std::array<std::byte, 0x08> m_alignment3a08To3a10; // +0x3a08..+0x3a0f
    double m_dNpcCustomizedSizeRate; // +0x3a10

public:
    bool m_bLogToError; // +0x3a18

protected:
    std::array<std::byte, 0x07> m_alignment3a19To3a20; // +0x3a19..+0x3a1f
    std::vector<QuestNpcEventController*> m_vEventController; // +0x3a20..+0x3a37
    std::array<std::byte, 0x08> m_alignment3a38To3a40; // +0x3a38..+0x3a3f

private:
    NPCInfoPacket m_NPCInfoPacket; // +0x3a40..+0x3be7

public:
    volatile std::int32_t m_isEnterWorldPacket; // +0x3be8
    std::array<std::byte, 0x04> m_alignment3becTo3bf0; // +0x3bec..+0x3bef
    CPacketHelperEx<20480>* m_pac; // +0x3bf0
    CPacketHelperEx<20480>* m_pav; // +0x3bf8
    CPacketHelperEx<20480>* m_pat; // +0x3c00
    CPacketHelperEx<20480>* m_pad; // +0x3c08

    bool StartDroidNPCSetting() {
        return runtime::raw_atomic::CompareExchangeI32(&m_isProcessingDroidNPCSetting, 1, 0) == 0;
    }

    void EndDroidNPCSetting() { runtime::raw_atomic::ExchangeI32(&m_isProcessingDroidNPCSetting, 0); }

    bool IsDroidNPC() const { return m_isDroidNPC; }

    void SetDroidNPCInfo(bool isDroidNPC, unsigned int accountId, unsigned int characterDatabaseId) {
        m_isDroidNPC = isDroidNPC;
        m_nDroidNPCAccountID = accountId;
        m_nDroidNPCCharDBID = characterDatabaseId;
    }

    void ClearDroidNPCInfo() {
        m_isDroidNPC = false;
        m_nDroidNPCAccountID = 0;
        m_nDroidNPCCharDBID = 0;
    }

    // TotalChecker2015 CodeView field list 0xB468 records the original
    // access of this Droid-NPC boundary as public. Current L2Server's
    // ReplyLoadDroidNPCAppearance directly consumes the same typed fields.
    std::vector<InfectedSkillType> m_InfectedSkillsOfMaster; // +0x3c10..+0x3c27
    std::array<std::byte, 0x08> m_alignment3c28To3c30; // +0x3c28..+0x3c2f
    volatile std::int32_t m_isProcessingDroidNPCSetting; // +0x3c30
    bool m_isDroidNPC; // +0x3c34
    std::array<std::byte, 0x03> m_alignment3c35To3c38; // +0x3c35..+0x3c37
    std::uint32_t m_nDroidNPCAccountID; // +0x3c38
    std::uint32_t m_nDroidNPCCharDBID; // +0x3c3c
    DroidNPCInfo m_DroidNPCInfo; // +0x3c40..+0x3d37
    FVector m_vDroidSpawnPos; // +0x3d38..+0x3d4f
    std::int32_t m_nDroidSpawnDir; // +0x3d50
    std::int32_t m_nDroidSpawnDefaultParam; // +0x3d54
    std::int32_t m_nDroidSpawnDefaultHP; // +0x3d58
    std::int32_t m_nDroidSpawnDefaultMP; // +0x3d5c
    std::int32_t m_nDroidSpawnInZoneID; // +0x3d60

private:
    NpcTypeForDeco::Type m_eNpcTypeForDeco; // +0x3d64
    std::int32_t m_nAgitCIDorPledgeDBID; // +0x3d68
    std::int32_t m_nAgitSlot; // +0x3d6c
    bool m_bMonsterBookNPC; // +0x3d70
    std::array<std::byte, 0x03> m_alignment3d71To3d74; // +0x3d71..+0x3d73
    std::int32_t m_nAliveHour; // +0x3d74
};


// Exact current NPC.obj identity map: 8192 buckets, ref-type 5 and int SID
// keys. OnCreate/OnCreateFakeNPC keep the object here after the shared
// creature registration is complete.
using CNPCObjectMap = CObjectMap<CNPC, 8192, 5, RWLock, int>;
extern CNPCObjectMap g_NPCMap;

// Current NPC.obj global at 0x175c76a0. ListenThread_common creates the
// test NPC only for server type zero. The process image initializes it null.
extern CNPC* g_TestNPC;

// Current NPC.cpp process owner for unique NPCs, keyed first by instant-zone
// ID and then by NPC class ID. Both target maps are 0x20-byte carriers and
// the embedded yield lock closes the exact 0x30-byte physical extent.
class CUniqNPCDB {
public:
    using NpcMap = std::map<int, std::uint32_t>;
    using InZoneMap = std::map<int, NpcMap>;

    CUniqNPCDB();
    ~CUniqNPCDB();

    void DeleteAll();
    bool AddUniqNPC(CNPC* npc);
    bool DeleteUniqNPC(CNPC* npc);
    std::uint32_t GetUniqNPCSID(int inZoneId, int npcClassId);
    CNPC* GetUniqNPC(int inZoneId, int npcClassId);
    CBoss* GetBossNPC(int inZoneId, int npcClassId);

private:
    InZoneMap m_UniqNPCInzoneDB; // +0x00
    CYieldLock m_lock; // +0x20
};


extern CUniqNPCDB g_UniqNPCDB;

// Current NPC.cpp process owner for the teleport-to-NPC position cache. The
// embedded lock and target map fix the physical 0x30-byte extent. Each value
// stores the instant-zone id followed by the exact FVector spawn position.
class CNPCPosDB {
public:
    struct NPCPos {
        std::int32_t nInzoneID;
        std::int32_t m_alignment04To08;
        FVector vPos;
    };

    CNPCPosDB() = default;
    ~CNPCPosDB() = default;

    bool GetNPCPos(int npcClassId, NPCPos& position);
    bool AddNPCPos(CNPC* npc);

private:
    CYieldLock m_lock; // +0x00
    std::map<std::int32_t, NPCPos> m_positions; // +0x10
};


extern CNPCPosDB g_NPCPosDB;

// Current RTTI places this original intermediate verifier base directly over
// CIOObject. Its inlined constructor owns the CCreature deleting-byte write.
// the two concrete verifier pointers belong to the derived classes.
class CCreatureReleaseVerifier : public ::CIOObject {
public:
    ~CCreatureReleaseVerifier() override = default;

protected:
    explicit CCreatureReleaseVerifier(CCreature* creature);
};

// Exact current delayed-release verifier from NPC.cpp. The callback uses the
// current-proven zero-displacement CObject prefix of the partial CNPC owner.
class CNPCReleaseVerifier final : public CCreatureReleaseVerifier {
public:
    CNPCReleaseVerifier(CNPC* npc, const wchar_t* caller);
    ~CNPCReleaseVerifier() override = default;

    void TimerExpired(int id) override;


private:
    CNPC* m_pNPC;
    const wchar_t* m_guardStr;
};
