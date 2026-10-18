#pragma once

#include "SlotIndexManager.h"

#include "Shared/src/BaseAttribute.h"
#include "Shared/src/CenteredSharedRWLock.h"
#include "Shared/src/ItemOptionKey.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstdint>

class CCreature;
class CItem;
class ItemManipList;
class User;

template <typename TObject>
class CSPointer;

using ItemPointerList = std::vector<CSPointer<CItem>>;

class CSharedItemData;

enum MaterialType : std::int32_t {
    MATT_NONE = 0,
    MATT_ORIHARUKON = 1,
    MATT_MITHRIL = 2,
    MATT_GOLD = 3,
    MATT_SILVER = 4,
    MATT_BRASS = 5,
    MATT_BRONZE = 6,
    MATT_DIAMOND = 7,
    MATT_STEEL = 8,
    MATT_DAMASCUS_STEEL = 9,
    MATT_BONE_OF_DRAGON = 10,
    MATT_AIR = 11,
    MATT_WATER = 12,
    MATT_WOOD = 13,
    MATT_BONE = 14,
    MATT_BRANCH_OF_ORBIS_ARBOR = 15,
    MATT_BRANCH_OF_WORLDTREE = 16,
    MATT_CLOTH = 17,
    MATT_PAPER = 18,
    MATT_LEATHER = 19,
    MATT_SKULL = 20,
    MATT_FEATHER = 21,
    MATT_SAND = 22,
    MATT_CRYSTAL = 23,
    MATT_FUR = 24,
    MATT_PARCHMENT = 25,
    MATT_EYEBALL = 26,
    MATT_INSECT = 27,
    MATT_CORPSE = 28,
    MATT_CROW = 29,
    MATT_HATCHING_EGG = 30,
    MATT_SKULL_OF_WYVERN = 31,
    MATT_DRUG = 32,
    MATT_COTTON = 33,
    MATT_SILK = 34,
    MATT_WOOL = 35,
    MATT_FLAX = 36,
    MATT_COBWEB = 37,
    MATT_DYESTUFF = 38,
    MATT_LEATHER_OF_PUMA = 39,
    MATT_LEATHER_OF_LION = 40,
    MATT_LEATHER_OF_MANTICORE = 41,
    MATT_LEATHER_OF_DRAKE = 42,
    MATT_LEATHER_OF_SALAMANDER = 43,
    MATT_LEATHER_OF_UNICORN = 44,
    MATT_LEATHER_OF_DRAGON = 45,
    MATT_SCALE_OF_DRAGON = 46,
    MATT_ADAMANTAITE = 47,
    MATT_BLOOD_STEEL = 48,
    MATT_CHRYSOLITE = 49,
    MATT_DAMASCUS = 50,
    MATT_FINE_STEEL = 51,
    MATT_HORN = 52,
    MATT_LIQUID = 53,
    MATT_FISH = 54,
    MATT_RUNE_XP = 55,
    MATT_RUNE_SP = 56,
    MATT_RUNE_REMOVE_PENALTY = 57,
    MAX_MATT = 58,
};

enum EtcItemType : std::int32_t;
enum CrystalType : std::int32_t;

enum InventoryUpdateType : std::int32_t {
    IUT_ADD = 1,
    IUT_UPDATE = 2,
    IUT_DELETE = 3,
};

// The spellings are donor-selected source vocabulary. Current inventory
// overflow producers and the logging consumer close 1..4.
enum InventoryOverType : std::int32_t {
    OVER_COUNT = 1,
    OVER_WEIGHT = 2,
    OVER_INVEN_LIMIT = 3,
    OVER_ARTIFACT_INVEN_LIMIT = 4,
};


// Original Shared/inventory.h value/owner contract used by every multi-item
// capacity check. The current server's xvector carrier is 0x20 bytes (the
// older TotalChecker image used a 0x18-byte std::vector carrier).
struct ItemAmountData {
    ItemAmountData() noexcept : m_nItemClassId(0), m_nItemAmount(0) {}

    ItemAmountData(std::int32_t itemClassId, std::int64_t itemAmount) noexcept :
        m_nItemClassId(itemClassId), m_nItemAmount(itemAmount) {}

    std::int32_t m_nItemClassId;
    std::int64_t m_nItemAmount;
};

struct ItemAmountDataStruct {
    using ListType = std::vector<ItemAmountData*>;

    ItemAmountDataStruct() = default;

    ItemAmountDataStruct(const ItemAmountDataStruct& other) {
        for (const ItemAmountData* const entry : other.dataList) {
            if (entry != nullptr) {
                Push(entry->m_nItemClassId, entry->m_nItemAmount);
            }
        }
    }

    ItemAmountDataStruct& operator=(const ItemAmountDataStruct& other) {
        if (this != &other) {
            Clear();
            for (const ItemAmountData* const entry : other.dataList) {
                if (entry != nullptr) {
                    Push(entry->m_nItemClassId, entry->m_nItemAmount);
                }
            }
        }
        return *this;
    }

    ~ItemAmountDataStruct() {
        L2_GUARD_FUNCTION();
        Clear();
    }

    void Push(std::int32_t itemClassId, std::int64_t itemAmount) {
        dataList.push_back(new ItemAmountData(itemClassId, itemAmount));
    }

    void Clear() noexcept {
        L2_GUARD_FUNCTION();
        for (ItemAmountData* const entry : dataList) {
            delete entry;
        }
        dataList.clear();
    }

    ListType dataList;
};


// Exact current abstract functor envelope: RTTI fixes one vptr and every
// concrete table contains only operator()(CItem*).
class ItemFunctor {
public:
    virtual bool operator()(CItem* item) = 0;
};


class CInventory {
public:
    static constexpr int kMaximumTraversalDepth = 0x193;

    CInventory();
    ~CInventory();

    static ::CInventoryLockManager& GetLockManager();

    bool SetIndex(int creatureIndex, bool isUser) {
        m_nCreatureIndex = creatureIndex;
        m_bIsUser = isUser;
        m_lock = GetLockManager().GetLock(creatureIndex);
        return m_lock != nullptr;
    }

    int GetCarryingWeight() { return m_nCarryingWeight; }
    int ValidateCarringWeight();

    int GetFirstIndex();
    CSharedItemData* GetFirst();
    bool Have(int itemIndex);
    bool HaveItemByClassId(int itemClassId, std::int64_t count, bool includeVariation, bool includeEnsoul);
    bool CheckAddable(int itemIndex, bool includeVariation);
    bool CheckAddable(int itemClassId, std::int64_t count, bool includeVariation);
    bool CheckAddableMulti(const ItemAmountDataStruct& items, bool includeVariation);
    bool Push(int itemIndex);
    bool Pop(int itemIndex, bool unknown);
    std::int64_t GetItemAmount(int itemClassId, int enchanted, bool checkVariation);
    bool UseLure(int itemClassId, std::int64_t amount);
    std::int64_t GetEtcItemCountByCrystal(EtcItemType itemType, CrystalType crystalType);
    std::int64_t GetArrowCountByCrystal(CrystalType crystalType);
    std::int64_t GetBoltCountByCrystal(CrystalType crystalType);
    int UseArrow(CrystalType crystalType, int count);
    bool UseBolt(CrystalType crystalType, std::int64_t count);
    CItem* GetArrowByCrystal(CrystalType crystalType);
    CItem* GetBoltByCrystal(CrystalType crystalType);
    CItem* GetHeroItem(bool weapon);
    CItem* GetHeroEntryItem(bool olympiadEntry);
    void AddSlotInfo(int slotIndex, CItem* item);
    void RemoveSlotInfo(CItem* item);
    CItem* GetByDBID(int databaseId);
    bool CreateOrAddItem(int databaseId, int itemClassId, std::int64_t amount);
    CItem* GetByServerID(int serverId);
    CItem* GetByIndex(int itemIndex);
    CItem* GetFirstItemBy(ItemFunctor& functor);
    CItem* GetNextItemBy(int itemIndex, ItemFunctor& functor);
    CItem* GetFirstItemByAttribute(int itemClassId, const BaseAttribute& attribute);
    CItem* GetFirstItemByClassID(int itemClassId, bool includeVariation, bool includeEnsoul);
    CItem* GetNextItemByClassID(int itemIndex, bool includeVariation, bool includeEnsoul);
    CItem* GetNextMinEnchantedItemByClassID(
        int itemClassId, int currentItemIndex, bool includeVariation, bool includeEnsoul
    );
    CItem* GetMinEnchantedItemByClassID(int itemClassId, bool includeVariation, bool includeEnsoul);
    CItem* GetFirstItemByClassIDEnchantedAndOption(int itemClassId, int enchanted, ItemOptionKey optionKey);
    CItem* GetNextItemByClassIDEnchantedAndOption(int itemIndex, int enchanted, ItemOptionKey optionKey);
    CItem* GetFirstItemByEnchantedAttribute();
    CItem* GetNextItemByEnchantedAttribute(int itemIndex);
    bool GetItemListByClassID(ItemPointerList& items, int itemClassId, int count, int enchanted, bool includeVariation);
    bool IsValidManipulation(CItem* item, std::int64_t amount);
    bool IsValidManipulation(ItemManipList* manipulations);
    CCreature* GetOwner();
    std::int64_t GetAdenaAmount();
    int GetAdenaDBID();
    bool HaveFakeItemBySId(std::int64_t count);
    int GetTotalCount();
    int GetArtifactCount();
    int GetInventoryCount();
    int GetArtifactInventoryCount();
    void InitSlotInfo();
    int MakeSlotIndexPacket(char* buffer, int& offset);
    void ReleaseMapChangedItems();
    bool SetInventoryChanged(CItem* item, InventoryUpdateType updateType);
    int GetRuneItemCount(MaterialType materialType);
    void UpdateRuneItem();
    void ClearRuneState();

    void SetMaxInventoryLimit(int maximum) noexcept { m_SlotIndexManager.SetMaxSlotCount(maximum); }

    void SetMaxArtifactInventoryLimit(int maximum) noexcept { m_ArtifactSlotIndexManager.SetMaxSlotCount(maximum); }

    void WriteLock(const wchar_t* file, int line) { m_lock->WriteLock(file, line); }

    void ReadLock(const wchar_t* file, int line) { m_lock->ReadLock(file, line, false); }

    void WriteUnlock() { m_lock->WriteUnlock(nullptr, 0); }

    void ReadUnlock() { m_lock->ReadUnlock(nullptr, 0); }

protected:
    std::int32_t m_nCreatureIndex;
    bool m_bIsUser;
    ::CSharedRWLock* m_lock;
    std::int32_t m_nCarryingWeight;

public:
    std::map<int, int> MapChangedItems;
    RWLock lockMapChangedItems;

private:
    // Current CCreature::EnterWorldImpl directly resets the three rune
    // counters and the rune-item vector owned by its embedded inventory.
    friend class CCreature;
    friend class User;

    void _ReleaseMapChangedItems();
    void LogByInventoryOverType(InventoryOverType type, CCreature* creature, CItem* item, std::int64_t amount);

    CSlotIndexManager m_SlotIndexManager;
    CSlotIndexManager m_ArtifactSlotIndexManager;

protected:
    std::int32_t m_nEXPRuneCount;
    std::int32_t m_nSPRuneCount;
    std::int32_t m_nRPRuneCount;
    std::vector<std::uint32_t> m_RuneItemClassIds;
};
