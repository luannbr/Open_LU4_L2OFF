#pragma once

#include "CWorldObject.h"
#include "EnsoulData.h"
#include "ItemCondition.h"
#include "Protocol/src/L2Constants.h"
#include "Shared/src/BaseAttribute.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/IOC.h"
#include "Shared/src/ItemOption.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"
#include "Skill.h"

#include <cstddef>
#include <cstdint>
#include <utility>

class CEnchantOption;
class CEtcItem;
class CCreature;
class CObjectDB;
class CParserForSetItem;
class CItemEquipCondition;
class CInventory;
class CSharedItemConstant;
class CSharedItemData;
class CUserSocket;
class IPacketHelper;
class User;

namespace Commission {
enum Enum : std::int32_t;
}

enum ItemDefaultActionEnum : std::int32_t;
enum WareHouseClass : std::int32_t;

enum ItemTypeForEnchantAdd : std::int32_t {
    ITE_NORMAL = 0,
    ITE_ONEHAND = 1,
    ITE_TWOHAND = 2,
    ITE_BOW = 3,
    ITE_CROSSBOW = 4,
    ITE_MAX = 5,
};

enum ObjectFieldType : std::int32_t;


// Exact Item.cpp value records embedded by CItemConstInfo. Their current
// sizes and member order are fixed by the object parser's construction and
// iteration code. The generated parser itself remains a separate owner.
struct CapsuledItem {
    std::int32_t nItemClassId;
    std::int64_t nMin;
    std::int64_t nMax;
    double nProb;
};

// Item.cpp-owned parser staging record consumed by CObjectDB::PostLoad.
// Current parser construction fixes the xwstring followed by two signed
// 64-bit bounds and a double probability, for an exact 0x40-byte extent.
struct TempCapsuledItem {
    std::u16string m_ItemName;
    std::int64_t nMin;
    std::int64_t nMax;
    double nProb;
};

struct CrystalItemStruct {
    std::int32_t ItemID;
    std::int64_t CrystalCount;
    double Prob;
};

struct CreateItemRandomStruct {
    std::int32_t ItemID;
    std::int64_t Count;
    double Prob;
    std::int32_t Enchant;
};

struct CreateItemStructInUseCount {
    std::int32_t classId_;
    std::int64_t count_;
    double prob_;
};


// Current CItemConstInfo is a non-polymorphic 0x188-byte Item.cpp owner. The
// first two methods below are the complete runtime route used by item-use
// validation and riding food. Parser-population stays in parser_object.cpp.
class CItemConstInfo {
public:
    CItemConstInfo() = default;

    void GetUseConditionList(std::vector<int>& conditions, UseConditionType type);
    bool CanUse(CCreature* creature, CItem* item);
    bool CanEquip(CCreature* creature, CItem* item, CheckEquipConditionItemType::Enum itemType);
    void GetNewSetItemIDList(std::vector<int>& result) const;
    void GetShapeShiftedSetItemIDList(std::vector<int>& result) const;
    bool HasEquipConditionType(EquipConditionType type);
    void GetCreateItemRandom(std::vector<CreateItemRandomStruct>& items);
    void GetCreateItem(std::vector<CreateItemStruct>& items);
    int GetMaxUnpackableCapsuledItemCount();
    int GetMaxUnpackableCapsuledArtifactCount();
    void GetUnpackableCapsuledItemList(std::vector<CapsuledItem*>& items);
    std::vector<CreateItemStructInUseCount>& GetCreateItemsInUseCount(std::int16_t useCount);
    void AddFixedCreateItemsInUseCount(std::int16_t fixedNumber, CreateItemStructInUseCount item);
    bool HasItemPropertyList(VALIDATE::Enum type);
    void SetItemProperty(VALIDATE::Enum type);
    void SetCrystalItem(int itemId, int crystalCount, double probability);
    void GetCrystalItemList(std::vector<CrystalItemStruct>& items);
    void SetCreateItemRandom(int itemId, std::int64_t count, double probability, int enchant);
    void SetCreateItem(int itemId, std::int64_t count, int enchant);
    void SetCreateItemRandomGroup(const std::vector<std::vector<CreateItemRandomStruct>>& groups);

    std::vector<CItemUseCondition*> m_vItemUseCondition; // +0x000
    std::vector<CItemEquipCondition*> m_vItemEquipCondition; // +0x020
    std::vector<CapsuledItem*> m_vCapsuledItemList; // +0x040
    std::vector<CrystalItemStruct> m_CrystalItemList; // +0x060
    std::vector<int> m_vNewSetId; // +0x080
    std::vector<int> m_vShapeShiftedSetId; // +0x0a0
    std::vector<VALIDATE::Enum> m_vItemProperty; // +0x0c0
    std::vector<CreateItemRandomStruct> m_vCreateItemList; // +0x0e0
    std::vector<CreateItemStruct> m_vCreateItemList2; // +0x100
    std::vector<std::vector<CreateItemRandomStruct>> m_vCreateItemGroupList; // +0x120
    std::int16_t m_cMaxUseCount; // +0x140
    std::byte m_padding142[0x06];
    std::vector<CreateItemStructInUseCount> m_vCreateItemsInUseCount; // +0x148
    std::map<std::int16_t,
        std::vector<CreateItemStructInUseCount>> m_mFixedCreateItemsInUseCount; // +0x168
};


class CItemConstInfoDB {
public:
    CItemConstInfo* GetItemConstInfo(int itemClassId);
    void AddItemConstInfo(int itemClassId, CItemConstInfo* itemConstInfo);

private:
    std::map<int, CItemConstInfo*> m_mItemConstInfo;
};


extern CItemConstInfoDB g_ItemConstInfoDB;

// Current Item.cpp owner used by convertdata.txt. The shipping object is a
// single 0x20-byte MSVC map. The portable object owns a standard map
// directly. Its host extent is implementation-dependent.
class CConvertItemDB {
public:
    CConvertItemDB();
    ~CConvertItemDB();

    void Load();
    bool AddItem(int inputItemId, int outputItemId);
    int GetConvertItem(int inputItemId);

private:
    std::map<int, int> m_mapConvertItem;
};


extern CConvertItemDB g_ConvertItemDB;

// Item_sub.cpp owner loaded from quest_pch2.txt during the current server's
// cold-start data pass. The target xstd map and each quest-item vector both
// occupy 0x20 bytes. The portable carriers keep those physical extents.
class CQuestItemDB {
public:
    void Load();
    bool DestroyQuestItem(
        User* user,
        int questId,
        bool force
    ); // current body 0x007e9a20. Two-pass pet-collar/item teardown.

private:
    std::map<int, std::vector<int>> m_mapQuestItem;
};


extern CQuestItemDB g_QuestItemDB;

void LoadQuestItem();


class CCombinationItem {
public:
    CCombinationItem();
    virtual ~CCombinationItem();

    void SetOne(std::int32_t itemClassId, std::int64_t amount);
    void SetTwo(std::int32_t itemClassId, std::int64_t amount);
    void SetProb(std::int32_t probability);
    void SetResult(
        std::int32_t successItemClassId, std::int64_t successAmount, std::int32_t failItemClassId, std::int64_t failAmount
    );
    bool IsValidItemAmount(std::int32_t itemClassId, std::int64_t amount);
    bool IsValidCombinationItem();

private:
    friend class CObjectDB;
    friend class CParserForCombinationItem;

    std::int32_t m_nSlotOne; // +0x08
    std::int64_t m_nSlotOneAmount; // +0x10
    std::int32_t m_nSlotTwo; // +0x18
    std::int64_t m_nSlotTwoAmount; // +0x20
    std::int32_t m_nProb; // +0x28
    std::int32_t m_nSuccessResultItem; // +0x2c
    std::int64_t m_nSuccessResultItemAmount; // +0x30
    std::int32_t m_nFailResultItem; // +0x38
    std::int64_t m_nFailResultItemAmount; // +0x40
};


// Item.cpp's exact parser-to-PostLoad staging value. The five fixed keys are
// followed by the ordinary, unequip and equip lists. Current xvector expands
// each list to 0x20 bytes on the target ABI.
struct CItemTempData {
    CSkillKey m_ItemSkill;
    CSkillKey m_CriticalSkill;
    CSkillKey m_AttackSkill;
    CSkillKey m_MagicSkill;
    CSkillKey m_EnchantedFourSkill;
    std::vector<CSkillKey> m_ItemSkillList;
    std::vector<CSkillKey> m_ItemUnequipSkillList;
    std::vector<CSkillKey> m_ItemEquipSkillList;
};


extern std::map<int, CItemTempData> g_ItemTempDataDB;
extern std::map<int, std::vector<TempCapsuledItem*>*> g_CapsuledItemTempDataDB;
extern char16_t g_ItemHtml[100000][260];

// Exact Item.cpp read-only tables at current L2Server.exe
// 0x01014cf0/0x01014dc0/0x01014e60/0x01014fc0/0x01014ff0.
extern const int CrystalClassByCrystalType[];
extern const double ArmorEnchantFailTable[20];
extern const double OnePieceEnchantFailTable[20];
extern const int ArrayCrystalCountByEnchantWeapon[];
extern const int ArrayCrystalCountByEnchantArmor[];

// Exact current L2Server item envelope. The current executable fixes the
// 0x220 extent, every field below, the complete inherited override set and
// the 22-slot virtual tail.
class CItem : public CWorldObject {
public:
    CItem(const CItem& other);
    explicit CItem(bool createFail);
    ~CItem() override;

    L2_DECLARE_OBJECT_REGISTRATION(CItem)

    static void* operator new(std::size_t requestedSize);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void TimerExpired(int id) override;
    CObject* Copy() override;
    bool IsItem() const override;
    CItem* CastItem() override;
    bool IsInVehicle() override;
    bool IsInAirShip() override;
    void SetDBID(unsigned int dbId) override;
    FVector GetPos() const override;
    FAngle GetDir() const override;
    int GetIndex() const override;
    void DecideDistance(CMoveToTargetShareData* data, CSmartID creatureId) override;
    const char16_t* GetName() const override;
    bool PostLoad() override;

    // Current CItem vtable slots 163..184, in exact order.
    virtual void Delete();
    virtual CEtcItem* CastEtcItem();
    virtual bool IsSameItem(CItem& item) const;
    virtual bool IsTradable(User* user, bool flag);
    virtual bool IsNPCTradable(User* user, bool flag);
    virtual bool IsSellableToStore(User* user, bool flag, bool commission);
    virtual bool IsSellableToPrivateStore(User* user, bool flag);
    virtual bool IsSellableToNpcStore(User* user, bool flag);
    virtual bool IsNPCTrade();
    virtual bool IsAttribution();
    virtual bool IsTrade();
    virtual bool CanKeep(WareHouseClass warehouseClass);
    virtual bool IsDrop();
    virtual bool IsDestruct();
    virtual void Set(ObjectFieldType field, std::vector<int>* values);
    virtual void Set(ObjectFieldType field, wchar_t* value, int length);
    virtual void Set(ObjectFieldType field, wchar_t* value);
    virtual void Set(ObjectFieldType field, double value);
    virtual void Set(ObjectFieldType field, std::int64_t value);
    virtual void Set(ObjectFieldType field, int value);
    virtual void Set2(std::vector<std::int64_t>* values);
    virtual bool IsPetCollarItem();

    bool CanGetByPet(User* user);
    void OnCreate();
    void OnCreateFakeItem();
    CItem* CopyFakeItem();

    void SetValidateParam(VALIDATE::Enum type, double value);
    double GetValidateParam(VALIDATE::Enum type);
    void ShowEquipItemInfo(User* user);
    void SetDefaultAction(ItemDefaultActionEnum action);
    void SetRecipeId(int recipeId);
    ItemTypeForEnchantAdd GetITEFromType();
    double GetEnchantValue(VALIDATE::Enum type, ItemTypeForEnchantAdd itemType, int crystal, int enchanted);
    double GetEnchantValueRGrade(VALIDATE::Enum type, ItemTypeForEnchantAdd itemType, int crystal, int enchanted);
    double GetPropertyValue(VALIDATE::Enum type, int enchanted);
    bool IsItemProperty(VALIDATE::Enum type);
    double EnchantAddValue(VALIDATE::Enum type);
    double EnchantAddExtraValue(VALIDATE::ExtraEnum type);
    double GetEnchantedArmorHPBonus();
    std::int64_t GetBaseAttributeCancelCommision();
    void SetVariation(ItemOptionKey optionKey, std::uint32_t intensiveItemClassId);
    void SetAttribute(const BaseAttribute& attribute);
    BaseAttribute GetTotalAttribute() const;
    void ApplyStatBonus(CStats& stats);
    void SetAgathionEnergy(double energy);
    bool AddAmount(std::int64_t amount);
    bool IsAddableAmount(std::int64_t amount);
    bool AddDropPeriodTimer();
    static const unsigned char* DisassembleVariation(const unsigned char* packet, int& firstOption, int& secondOption);
    static int AssembleAttribute(const BaseAttribute& attribute, IPacketHelper& packet);
    static const unsigned char* DisassembleAttribute(const unsigned char* packet, BaseAttribute& attribute);
    static const unsigned char* DisassembleShapeShift(const unsigned char* packet, int& classId);
    int AssembleEnsoul(IPacketHelper& packet);
    static const unsigned char* DisassembleEnsoul(const unsigned char* packet, std::map<int, EnsoulOptionData*> options[3]);
    void SetEnsoulOptionData(std::map<int, EnsoulOptionData*> options[3], bool clear);
    bool IsCombinableItem(User* user);
    static bool CompareOptionData(
        const std::map<int, EnsoulOptionData*> first[3], const std::map<int, EnsoulOptionData*> second[3]
    );
    bool IsPeriodicShapeShiftedItem(User* user) const;
    bool IsShapeShiftableTargetItem(User* user, CItem* shapeShiftItem, CItem* extraItem) const;
    bool IsShapeShiftableExtractionItem(User* user, CItem* targetItem);
    bool CheckItemEquipCondition(CCreature* creature, int shapeShiftClassId);
    bool CheckItemUseCondition(CCreature* creature);
    bool CanUseWithCrystalType(CrystalType crystalType);
    bool ChangeAttributeItem(CItem* targetItem, User* user);
    bool hLockedItem(CItem* targetItem, User* user);
    bool hUnLockedItem(CItem* targetItem, User* user);
    bool ShapeShiftItem(User* user, CItem* targetItem, CItem* shapeShiftItem, bool extract, std::int64_t amount, int period);
    bool ShapeShiftItem(User* user, CItem* targetItem, int shapeShiftClassId, std::int64_t amount, int period);
    bool RestoreShapeShiftItem(User* user, CItem* targetItem);
    ShapeShiftAutoMatchingType::type GetShapeShiftMatchingType();
    void ShowItemInfo(User& user);
    void MakeShapeShiftAndVariationLogInfo(char16_t* buffer);
    void MakeShapeShiftLogInfo(char16_t* buffer);
    void MakeEnsoulLogInfo(char16_t* buffer);
    void LogRelateItem(User& user, const std::int64_t amount);
    static CrystalType GetCrysalType(const wchar_t* name);
    static VALIDATE::Enum GetValidateType(const wchar_t* name);
    static Commission::Enum GetCommissionItemType(const wchar_t* name);
    static int GetOppositeAttribute(int attribute);
    void SetShapeShiftingClassId(int classId);
    bool CheckEnsoulOption(std::map<int, int> ensoulInfo[3]);
    bool CheckEnsoulSlotIndex(EnsoulItem::EnsoulItemSlotType slotType, int slotIndex);
    bool IsEnchantableItem() const;
    bool IsEnsoulItem();
    int GetEnsoulCount();
    int GetDuration();
    void SetDuration(int duration);
    void DecreaseDurationOneUnit();
    void SetDurationCommitTick(int tick);
    bool IsTickToExpire(int tick) const;
    void SetEnchanted(int enchanted);
    std::int64_t GetPreviewPrice() const;
    std::int64_t GetCrystalizeCount(int divisor) const;
    bool CanCrystalized() const;
    bool Crystallize(User* user, CPet* pet);
    bool EnchantItem(CItem* targetItem, User* user, SkillFxChangeType type, double value, int enchantLevel);
    bool EnchantAttributeItemArmor(
        CItem* targetItem,
        User* user,
        AttributeTypeEnum attributeType,
        int grade,
        int additionalProbability,
        int additionalValue
    );
    bool EnchantAttributeItemWeapon(
        CItem* targetItem,
        User* user,
        AttributeTypeEnum attributeType,
        int grade,
        int additionalProbability,
        int additionalValue
    );
    bool SimulateWithInventory(CInventory& inventory, std::pair<int, std::int64_t>& result, std::int64_t count);
    void GetEnsoulSkills(std::map<int, const CSkillInfo*>& skills);
    void ActivateEquipSkillList(CCreature* creature, CWorldObject* target);
    void ActivateMultiSkillList(CCreature* creature, CWorldObject* target);
    void ActivateUnequipSkillList(CCreature* creature, CWorldObject* target);
    std::int16_t GetItemDamage() const;
    double GetAgathionEnergy();
    bool SetAmount(std::int64_t amount);
    void SetInitialDuration();
    bool IsDuration();
    void SendItemSpawn(CUserSocket* socket);
    void SendItemDropped(CUserSocket* socket, CCreature* dropper);


    CSharedItemData* m_data;
    CSharedItemConstant* m_const;
    ::CYieldLock* m_lock;
    std::uint32_t m_nTraceCharId;
    std::uint32_t m_nTraceNPCClassId;
    bool m_bTraceIsBoss;
    std::byte padding_79;
    std::int16_t m_Useable;
    AttributeTypeEnum m_eChangeAttribute;
    std::uint32_t m_nDroppedTick;
    bool m_bDroppedByAI;
    std::byte padding_85[3];
    CSmartID m_ContributeDataID;
    std::byte padding_8c[4];
    ItemOption m_itemOption;
    BaseAttribute m_itemBaseAttribute;
    std::byte padding_142[6];
    CEnchantOption* m_pEnchantOption;
    bool m_bRealItem;
    std::byte padding_151[3];
    std::int32_t m_ItemOwnerId;
    std::vector<const CSkillInfo*> m_ItemSkillList;
    std::vector<const CSkillInfo*> m_ItemEquipSkillList;
    std::vector<const CSkillInfo*> m_ItemUnequipSkillList;

private:
    std::int32_t m_durationCommitTick;

public:
    std::byte padding_1bc[4];
    std::map<int, EnsoulOptionData*> m_mEnsoulInfo[3];
};


// Item_sub.cpp owns the six concrete item classes constructed by
// CParserForObject. Five add no storage. CEtcItem adds the target byte at
// +0x220. Their current memory-class IDs are consecutive after CItem's 169.
class CAccessary : public CItem {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CAccessary)

    static void* operator new(std::size_t requestedSize);
    explicit CAccessary(bool createFail);
    CAccessary(const CAccessary& other);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    CObject* Copy() override;
    bool IsAccessary() const override;
    bool IsArtifact() const override;
};

class CAsset : public CItem {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CAsset)

    static void* operator new(std::size_t requestedSize);
    explicit CAsset(bool createFail);
    CAsset(const CAsset& other);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    CObject* Copy() override;
    bool IsAsset() const override;
};

class CArmor : public CItem {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CArmor)

    static void* operator new(std::size_t requestedSize);
    explicit CArmor(bool createFail);
    CArmor(const CArmor& other);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    CObject* Copy() override;
    bool IsArmor() const override;
};

class CEtcItem : public CItem {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CEtcItem)

    static void* operator new(std::size_t requestedSize);
    explicit CEtcItem(bool createFail);
    CEtcItem(const CEtcItem& other);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    CObject* Copy() override;
    bool IsEtcItem() const override;
    CEtcItem* CastEtcItem() override;
    bool IsNPCTrade() override;
    bool IsAttribution() override;
    bool IsTrade() override;
    bool IsDrop() override;
    bool IsDestruct() override;
    bool IsPetCollarItem() override;

    bool m_bPetCollarTradable; // +0x220
    std::array<std::byte, 0x07> m_targetExtentPadding;
};

class CQuestItem : public CItem {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CQuestItem)

    static void* operator new(std::size_t requestedSize);
    explicit CQuestItem(bool createFail);
    CQuestItem(const CQuestItem& other);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    CObject* Copy() override;
    bool IsQuestItem() const override;
};

class CWeapon : public CItem {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CWeapon)

    static void* operator new(std::size_t requestedSize);
    explicit CWeapon(bool createFail);
    CWeapon(const CWeapon& other);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    CObject* Copy() override;
    bool IsWeapon() const override;
};

// Item_sub.cpp owns the current optional reference-tracing variants selected
// by Setting/ItemLeakDetailCheck. Current RTTI and all six Copy bodies prove
// zero-displacement inheritance and one appended RefHelpers value. The
// memory-class IDs fill the six current slots immediately after the ordinary
// item subtypes and before L2Time (182).
class CAccessary_ORT : public CAccessary {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CAccessary_ORT)

    static void* operator new(std::size_t requestedSize);
    explicit CAccessary_ORT(const CAccessary& other);
    ~CAccessary_ORT() override = default;

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;

    RefHelpers m_RefHelpers;
};

class CAsset_ORT : public CAsset {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CAsset_ORT)

    static void* operator new(std::size_t requestedSize);
    explicit CAsset_ORT(const CAsset& other);
    ~CAsset_ORT() override = default;

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;

    RefHelpers m_RefHelpers;
};

class CArmor_ORT : public CArmor {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CArmor_ORT)

    static void* operator new(std::size_t requestedSize);
    explicit CArmor_ORT(const CArmor& other);
    ~CArmor_ORT() override = default;

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;

    RefHelpers m_RefHelpers;
};

class CEtcItem_ORT : public CEtcItem {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CEtcItem_ORT)

    static void* operator new(std::size_t requestedSize);
    explicit CEtcItem_ORT(const CEtcItem& other);
    ~CEtcItem_ORT() override = default;

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;

    RefHelpers m_RefHelpers;
};

class CQuestItem_ORT : public CQuestItem {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CQuestItem_ORT)

    static void* operator new(std::size_t requestedSize);
    explicit CQuestItem_ORT(const CQuestItem& other);
    ~CQuestItem_ORT() override = default;

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;

    RefHelpers m_RefHelpers;
};

class CWeapon_ORT : public CWeapon {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CWeapon_ORT)

    static void* operator new(std::size_t requestedSize);
    explicit CWeapon_ORT(const CWeapon& other);
    ~CWeapon_ORT() override = default;

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;

    RefHelpers m_RefHelpers;
};

extern volatile bool g_bItemOrtType;


// Current four-name staging value used between object parsing and PostLoad.
class CSetItemTempData {
public:
    std::u16string m_wsSkillName;
    std::u16string m_wsSetSkillName;
    std::u16string m_wsAddtionalSetSkillName;
    std::u16string m_wsAddtional2SetSkillName;
};


extern std::map<int, CSetItemTempData> g_SetItemTempDataDB;

using NewSetItemSlotMap = std::multimap<SlotType, std::vector<int>>;

// Current Item.cpp staging carrier used by CNewSetItem until ObjectDB's
// separate PostLoad phase resolves the skill names. The generated parser
// creates entries through the global map's operator[].
class CNewSetItemTempData {
public:
    std::u16string m_wsSetSkillName; // +0x00
    std::u16string m_wsAdditionalSkillName; // +0x28
    std::map<int, std::u16string> m_mapEnchantSkillName; // +0x50
    std::u16string m_wsAdditional3SkillName; // +0x70
};

extern std::map<int, CNewSetItemTempData> g_NewSetItemTempDataDB;


// Current Item.cpp set-item record consumed by parser_setitem. Its only
// virtual slot is the deleting destructor. All skill pointers are borrowed
// from g_SkillDB and all item-id expansion is published into CItemConstInfo.
class CNewSetItem {
public:
    CNewSetItem();
    virtual ~CNewSetItem();

    void Set(int setItemId);
    int GetSlotNewSetItemId(int slot) const;
    void PushNewSetItemToUser(User& user, int enchanted);
    void DeleteAllSkills(CCreature* creature);
    CSkillInfo* FindSetItemEnchantSkill(int enchant) const;
    void SetSlotItemToSetID(std::vector<int>* itemClassIds);
    void SetAdditionalSlotItemToSetID(CItem* item);
    void SetAdditionalItemId(std::vector<int>* itemClassIds);
    void SetSlotTypeItemId(NewSetItemSlotMap value);
    void SetSkill(char16_t* skillName);
    void SetAdditionalSkill(char16_t* skillName);
    void AddEnchantSkill(int enchant, char16_t* skillName);
    void SetAdditional3Skill(char16_t* skillName);
    bool PostLoad();

private:
    friend class CCreature;
    friend class CObjectDB;
    friend class CParserForSetItem;

    std::uint32_t m_nNewSetItemId; // +0x08
    std::int32_t m_nEnchantNum; // +0x0c
    std::int32_t m_nMaxPartsNum; // +0x10
    NewSetItemSlotMap m_mapSlotToSetItemId; // +0x18
    std::vector<int> m_vAdditionalList; // +0x38
    CSkillInfo* m_pSkill; // +0x58
    CSkillInfo* m_pAdditionalSkill; // +0x60
    std::map<int, CSkillInfo*> m_pEnchantSetSkillMap; // +0x68
    CSkillInfo* m_pAdditional3Skill; // +0x88
};


// Current Item.cpp staging value used by the shape-shifted set-item parser
// until CObjectDB::PostLoad resolves the borrowed skill pointer.
class CShapeShiftedSetItemTempData {
public:
    std::u16string m_wsSetSkillName;
};

extern std::map<int, CShapeShiftedSetItemTempData> g_ShapeShiftedSetItemTempDataDB;

using ShapeShiftedSetItemSlotMap = std::multimap<SlotType, std::vector<int>>;


// Current SetItem.cpp shape-set record. The map and staging data own their
// values. M_pSkill is borrowed from g_SkillDB. The only virtual slot is the
// deleting destructor compiled for the public virtual destructor below.
class CShapeShiftedSetItem {
public:
    CShapeShiftedSetItem();
    virtual ~CShapeShiftedSetItem();

    bool PostLoad();
    void Set(int shapeShiftedSetItemId);
    void SetSlotTypeItemId(ShapeShiftedSetItemSlotMap value);
    void SetSlotItemToSetID(std::vector<int>* itemClassIds);
    void SetSkill(char16_t* skillName);

    CSkillInfo* GetSetItemSkill() { return m_pSkill; }

    // Matching-build PDB access structure is public. Current target-container
    // extents move the map and pointer to +0x10 and +0x30 respectively.
    std::uint32_t m_nShapeShiftedSetItemId; // +0x08
    ShapeShiftedSetItemSlotMap m_mapSlotToSetItemId; // +0x10
    CSkillInfo* m_pSkill; // +0x30

private:
};


class CItemRestrict {
public:
    CItemRestrict();
    ~CItemRestrict();

    void SetState(bool state);
    static CItemRestrict& Inst() {
        if (m_ItemRestrict == nullptr) {
            m_ItemRestrict = new CItemRestrict();
        }
        return *m_ItemRestrict;
    }
    bool IsRestrictItem(int itemClassId);
    bool CheckItemRestrict(int itemClassId);
    bool LoadData();
    bool ReLoadData();

    static CItemRestrict* m_ItemRestrict;

    std::vector<int> m_vItemVector;
    bool m_bIsOn;
    ::CYieldLock m_lock;
};


// Exact current set-item owner. Its destructor retires only the map nodes,
// not the pointed vectors. The four resolved skill pointers are borrowed.
class CSetItem {
public:
    enum SetItemSkillEnum : std::int32_t {
        SIS_Skill = 0,
        SIS_SetSkill = 1,
        SIS_AdditionalSetSkill = 2,
        SIS_Additional2SetSkill = 3,
        SIS_Max = 4,
    };

    CSetItem(bool createFail);
    virtual ~CSetItem();

    void SetItemSlot(ObjectFieldType field, std::vector<int>* itemClassIds);
    void SetItemSlot2(ObjectFieldType field, std::uint32_t value);
    void SetEffectParamDouble(ObjectFieldType field, double baseValue, double additionalValue);
    void SetEffectParam(ObjectFieldType field, int baseValue, int additionalValue);
    void SetSkill(ObjectFieldType field, wchar_t* skillName);
    void Set(ObjectFieldType field, int value);

    int SlotToSetItemClassID(int slot, int itemClassId) const;
    int GetSlotSetItemId(int slot) const;
    bool PostLoad();
    void PushSetItemToUser(User& user, int enchanted);

    std::uint32_t m_nSetItemId;
    std::map<int, std::vector<int>*> m_mapSlotToItemId;
    std::uint32_t m_nAdditionalItemSlot;

    double m_p_def_inc;
    double m_m_def_inc;
    double m_hp_inc;
    double m_mp_inc;
    double m_breath_inc;
    double m_magic_resist_inc;
    double m_casting_speed_inc;
    double m_move_speed_inc;

    double m_p_def_inc_add;
    double m_m_def_inc_add;
    double m_hp_inc_add;
    double m_mp_inc_add;
    double m_breath_inc_add;
    double m_magic_resist_inc_add;
    double m_move_speed_inc_add;
    double m_casting_speed_inc_add;

    double m_p_attack_inc;
    double m_m_attack_inc;
    double m_hp_regen_inc;
    double m_mp_regen_inc;
    double m_avoid_inc;
    double m_shield_def_prob_inc;
    double m_weight_limit_inc;
    double m_resist_poison_inc;
    double m_p_def_vs_dagger_inc;
    ::CStats m_Stat;

    double m_p_attack_inc_add;
    double m_m_attack_inc_add;
    double m_hp_regen_inc_add;
    double m_mp_regen_inc_add;
    double m_avoid_inc_add;
    double m_shield_def_prob_inc_add;
    double m_weight_limit_inc_add;
    double m_resist_poison_inc_add;
    double m_p_def_vs_dagger_inc_add;
    ::CStats m_StatAdd;

    std::uint32_t m_nAdditional2;
    CSkillInfo* m_pSkills[SIS_Max];
};

// Exact current delayed-release verifier from Item.cpp. The complete CItem
// family remains unpublished. This class has its own fully closed envelope.
class CItemReleaseVerifier : public ::CIOObject {
public:
    CItemReleaseVerifier(CItem* item, const wchar_t* caller);
    ~CItemReleaseVerifier() override = default;

    void TimerExpired(int id) override;


private:
    CItem* m_pItem;
    const wchar_t* m_guardStr;
};
