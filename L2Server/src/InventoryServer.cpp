#include "Creature.h"
#include "Inventory.h"
#include "Item.h"
#include "Object.h"
#include "ObjectDB.h"
#include "Pet.h"
#include "Protocol/src/L2Constants.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/UserConst.h"
#include "Trade.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>

namespace {

// Current .rdata at 0x0100ddf0. The historical header emits BaseCrystal as
// translation-unit-local data, which is why the adjacent donor PDB records a
// separate S_LDATA32 copy in every consuming object file.
constexpr std::int32_t BaseCrystal[] = {0, 1, 2, 3, 4, 5, 5, 5, 8, 8, 8, 11, 12};

bool IsStackable(std::int32_t consumeType) noexcept {
    // Current 0x00a513f8 is exactly the signed comparison pair
    // 1 < consumeType && consumeType < 4.
    return consumeType > 1 && consumeType < 4;
}

std::uint32_t UnsignedSmartId(std::int32_t signedSmartId) noexcept {
    std::uint32_t smartId = 0;
    std::memcpy(&smartId, &signedSmartId, sizeof(smartId));
    return smartId;
}

CSPointer<CObject> FindCreatureObjectSP(int creatureIndex) {
    CSPointer<CObject> result;
    CCreatureFactory& creatures = CCreatureFactory::Inst();
    if (creatureIndex < 0 || creatureIndex >= creatures.GetSize()) {
        return result;
    }

    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(UnsignedSmartId(creatures.GetIdByIndex(creatureIndex)));
    result.SetObject(reinterpret_cast<CObject*>(creature), ObjectRefType::FindObject);
    return result;
}

CSPointer<CObject> FindItemObjectSP(int itemIndex) {
    CSPointer<CObject> result;
    CItemFactory& items = CItemFactory::Inst();
    if (itemIndex < 0 || itemIndex >= items.GetSize()) {
        return result;
    }

    CItem* const item = ItemSmartIdManager()->GetObjectW(UnsignedSmartId(items.GetIdByIndex(itemIndex)));
    result.SetObject(static_cast<CObject*>(item), ObjectRefType::FindObject);
    return result;
}

template <std::size_t Capacity>
const char16_t* InventoryHostWideToTargetUtf16(const wchar_t* source, std::array<char16_t, Capacity>& destination) noexcept {
    destination.fill(u'\0');
    if (source == nullptr) {
        return destination.data();
    }
    std::size_t output = 0;
    for (std::size_t input = 0; source[input] != L'\0' && output + 1 < destination.size(); ++input) {
        const std::uint32_t value = static_cast<std::uint32_t>(source[input]);
        if (value <= 0xffffU) {
            destination[output++] = static_cast<char16_t>(value);
            continue;
        }
        if (value <= 0x10ffffU && output + 2 < destination.size()) {
            const std::uint32_t adjusted = value - 0x10000U;
            destination[output++] = static_cast<char16_t>(0xd800U + (adjusted >> 10U));
            destination[output++] = static_cast<char16_t>(0xdc00U + (adjusted & 0x3ffU));
        }
    }
    return destination.data();
}

// The source is UTF-16 already. Copy it within the destination.
template <std::size_t Capacity>
const char16_t* InventoryHostWideToTargetUtf16(
    const char16_t* source, std::array<char16_t, Capacity>& destination
) noexcept {
    destination.fill(u'\0');
    if (source == nullptr) {
        return destination.data();
    }
    for (std::size_t index = 0; source[index] != u'\0' && index + 1 < Capacity; ++index) {
        destination[index] = source[index];
    }
    return destination.data();
}

} // namespace

class AndFunctor final : public ItemFunctor {
public:
    AndFunctor(ItemFunctor& lhs, ItemFunctor& rhs) noexcept : m_lhs(&lhs), m_rhs(&rhs) {}

    bool operator()(CItem* item) override;

private:
    ItemFunctor* m_lhs;
    ItemFunctor* m_rhs;
};

class BaseAttributeFunctor final : public ItemFunctor {
public:
    bool operator()(CItem* item) override;
};

class SameBaseAttributeFunctor final : public ItemFunctor {
public:
    explicit SameBaseAttributeFunctor(const BaseAttribute& attribute) noexcept : m_attribute(&attribute) {}

    bool operator()(CItem* item) override;

private:
    const BaseAttribute* m_attribute;
};

class NotFunctor final : public ItemFunctor {
public:
    explicit NotFunctor(ItemFunctor& functor) noexcept : m_functor(&functor) {}

    bool operator()(CItem* item) override;

private:
    ItemFunctor* m_functor;
};

class FilterFunctor final : public ItemFunctor {
public:
    FilterFunctor(ItemFunctor& functor, bool isOn) noexcept : m_functor(&functor), m_bIsOn(isOn) {}

    bool operator()(CItem* item) override;

private:
    ItemFunctor* m_functor;
    bool m_bIsOn;
};

class VariationFunctor final : public ItemFunctor {
public:
    bool operator()(CItem* item) override;
};

class EnsoulFunctor final : public ItemFunctor {
public:
    bool operator()(CItem* item) override;
};

class ClassIDFunctor final : public ItemFunctor {
public:
    explicit ClassIDFunctor(int classId) noexcept : m_nClassId(classId) {}

    bool operator()(CItem* item) override;

private:
    std::int32_t m_nClassId;
};

class EnchantFunctor final : public ItemFunctor {
public:
    explicit EnchantFunctor(int enchanted) noexcept : m_nEnchant(enchanted) {}

    bool operator()(CItem* item) override;

private:
    std::int32_t m_nEnchant;
};

class ItemOptionKeyFunctor final : public ItemFunctor {
public:
    explicit ItemOptionKeyFunctor(ItemOptionKey key) noexcept : m_key(key) {}

    bool operator()(CItem* item) override;

private:
    ItemOptionKey m_key;
};


bool AndFunctor::operator()(CItem* item) { return (*m_lhs)(item) && (*m_rhs)(item); }

bool BaseAttributeFunctor::operator()(CItem* item) { return item->m_itemBaseAttribute.IsEnchanted(ATTRT_NONE); }

bool SameBaseAttributeFunctor::operator()(CItem* item) { return item->GetTotalAttribute() == *m_attribute; }

bool NotFunctor::operator()(CItem* item) { return !(*m_functor)(item); }

bool FilterFunctor::operator()(CItem* item) { return !m_bIsOn || (*m_functor)(item); }

bool VariationFunctor::operator()(CItem* item) {
    return item->m_itemOption.itemOptionKey.variationOption1 > 0 && item->m_itemOption.itemOptionKey.variationOption2 > 0;
}

bool EnsoulFunctor::operator()(CItem* item) { return item->IsEnsoulItem(); }

bool ClassIDFunctor::operator()(CItem* item) { return item->GetClassId() == m_nClassId; }

bool EnchantFunctor::operator()(CItem* item) { return item->m_data->GetEnchanted() == m_nEnchant; }

bool ItemOptionKeyFunctor::operator()(CItem* item) {
    return item->m_itemOption.itemOptionKey.variationOption1 == m_key.variationOption1 &&
        item->m_itemOption.itemOptionKey.variationOption2 == m_key.variationOption2;
}


CInventory::CInventory() :
    m_nCreatureIndex(-1),
    m_bIsUser(false),
    m_lock(nullptr),
    m_nCarryingWeight(0),
    m_nEXPRuneCount(0),
    m_nSPRuneCount(0),
    m_nRPRuneCount(0) {}

CInventory::~CInventory() { m_lock = nullptr; }

::CInventoryLockManager& CInventory::GetLockManager() {
    static ::CInventoryLockManager lockManager(::kInventoryLockLogicalName, ::kInventoryLockSpinCount);
    return lockManager;
}

int CInventory::GetFirstIndex() {
    CSharedCreatureData* const owner = CCreatureFactory::Inst().GetData(m_nCreatureIndex);
    // Current 0x007c0e8c indexes the 0x3ed8-byte creature record and returns
    // its direct +0xd8 load. Like the image, an invalid owner index leaves
    // no recoverable inventory head.
    return owner->m_nInventoryIndex;
}

CSharedItemData* CInventory::GetFirst() {
    const int index = GetFirstIndex();
    return index == -1 ? nullptr : CItemFactory::Inst().GetData(index);
}

int CInventory::ValidateCarringWeight() {
    L2_GUARD_FUNCTION();

    std::int32_t inventoryWeight = 0;
    CSPointer<CCreature> owner;
    std::int32_t tradeWeight = 0;
    const bool haveOwner = [&] {
        L2_READ_GUARD(*this);

        CItemFactory& items = CItemFactory::Inst();
        int currentIndex = GetFirstIndex();
        int depth = 0;
        while (currentIndex != -1) {
            CSPointer<CItem> item = items.FindObjectSP(currentIndex);
            if (item) {
                CItem* const actualItem = item->CastItem();
                if (actualItem->m_data->m_Type != IT_QUESTITEM) {
                    std::int32_t itemWeight = actualItem->m_const->m_nWeight;
                    if (actualItem->m_data->m_Type != IT_ARMOR) {
                        itemWeight = runtime::MultiplyInt32Wrapping(
                            itemWeight, runtime::Int32FromBits(static_cast<std::uint32_t>(actualItem->m_data->m_nCount))
                        );
                    }
                    inventoryWeight = runtime::AddInt32Wrapping(inventoryWeight, itemWeight);
                }
            }

            currentIndex = items.GetNextIndex(currentIndex);
            ++depth;
            if (depth > 0x192) {
                // Log.Add(
                //     LOG_ERROR,
                //     "depth>%d at file[%s], line[%d]",
                //     0x192,
                //     "Inventory_server.cpp",
                //     0x73f)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[inventory] Weight scan reached the %d-link limit for creature index %d. Weight validation will
                //     continue with the accumulated inventory weight %d. Next index: %d.", kMaximumTraversalDepth,
                //     m_nCreatureIndex,
                //     inventoryWeight,
                //     currentIndex)
                L2_LOG_ERROR(
                    Log,
                    u"[inventory] Weight scan reached the %d-link limit for creature index %d; weight validation will "
                    u"continue with the accumulated inventory weight %d. Next index: %d.",
                    kMaximumTraversalDepth, m_nCreatureIndex, inventoryWeight, currentIndex
                );
                break;
            }
        }

        owner = CCreatureFactory::Inst().FindObjectSP(m_nCreatureIndex);
        if (!owner) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] Push failed. because "
            //     "CCreatureFactory::Inst().GetObjectSP failed.",
            //     "Inventory_server.cpp",
            //     0x749)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Cannot validate weight: creature index %d was not found. Returning the previous carrying
            //     weight %d.", m_nCreatureIndex, m_nCarryingWeight)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Cannot validate weight: creature index %d was not found; returning the previous carrying "
                u"weight %d.",
                m_nCreatureIndex, m_nCarryingWeight
            );
            return false;
        }

        if (owner->IsPC()) {
            User* const user = owner->CastUser();
            CTrade* const trade = user->GetTrade();
            if (trade != nullptr) {
                tradeWeight = trade->GetTradeItemWeight(static_cast<int>(owner->Id()));
            }
        }

        return true;
    }();
    if (!haveOwner) {
        return m_nCarryingWeight;
    }

    const bool weightChanged = owner->m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(
        m_nCarryingWeight, runtime::AddInt32Wrapping(inventoryWeight, tradeWeight), 0x10
    );
    owner->ValidateCarryWeight();
    if (weightChanged && owner->IsPC()) {
        owner->CastUser()->ParamChanged(PCB_USER);
    }
    return m_nCarryingWeight;
}

bool CInventory::Have(int itemIndex) {
    L2_READ_GUARD(*this);

    CItemFactory& items = CItemFactory::Inst();
    int currentIndex = GetFirstIndex();
    int depth = 0;
    while (currentIndex != -1) {
        if (currentIndex == itemIndex) {
            return true;
        }
        currentIndex = items.GetNextIndex(currentIndex);
        ++depth;
        if (depth >= kMaximumTraversalDepth) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth > %d at file[%s], line[%d]",
            //     0x192,
            //     "..\\Shared\\Inventory_common.cpp",
            //     0x3c)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Membership scan for item index %d reached the %d-link limit for creature index %d. Returning
            //     not found. Next index: %d.", itemIndex, kMaximumTraversalDepth, m_nCreatureIndex, currentIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Membership scan for item index %d reached the %d-link limit for creature index %d; returning "
                u"not found. Next index: %d.",
                itemIndex, kMaximumTraversalDepth, m_nCreatureIndex, currentIndex
            );
            break;
        }
    }
    return false;
}

bool CInventory::HaveItemByClassId(int itemClassId, std::int64_t count, bool includeVariation, bool includeEnsoul) {
    L2_GUARD_FUNCTION();
    if (itemClassId < 1 || count < 1) {
        return false;
    }

    L2_READ_GUARD(*this);

    CItemFactory& items = CItemFactory::Inst();
    int currentIndex = GetFirstIndex();
    int depth = 0;
    while (currentIndex != -1) {
        bool depthExceeded = false;
        {
            CSPointer<CObject> object = FindItemObjectSP(currentIndex);
            if (object) {
                CItem* const item = static_cast<CItem*>(object.get());
                if (item != nullptr && item->m_data->m_nClassID == itemClassId &&
                    (includeVariation || item->m_itemOption.itemOptionKey.variationOption1 < 1 ||
                        item->m_itemOption.itemOptionKey.variationOption2 < 1) &&
                    (includeEnsoul || !item->IsEnsoulItem())) {
                    count -= item->m_data->m_nCount;
                }
            }

            currentIndex = items.GetNextIndex(currentIndex);
            ++depth;
            if (depth >= kMaximumTraversalDepth) {
                // Log.Add(
                //     LOG_ERROR,
                //     "depth > %d at file[%s], line[%d]",
                //     0x192,
                //     "Inventory_server.cpp",
                //     0x691)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[inventory] Quantity check for template %d reached the %d-link limit for creature index %d.
                //     remaining requested amount %lld, returning %ls.", itemClassId, kMaximumTraversalDepth,
                //     m_nCreatureIndex,
                //     static_cast<long long>(count),
                //     count < 1 ? L"satisfied" : L"not satisfied")
                L2_LOG_ERROR(
                    Log,
                    u"[inventory] Quantity check for template %d reached the %d-link limit for creature index %d; remaining "
                    u"requested amount %lld, returning %ls.",
                    itemClassId, kMaximumTraversalDepth, m_nCreatureIndex, static_cast<long long>(count),
                    count < 1 ? L"satisfied" : L"not satisfied"
                );
                depthExceeded = true;
            }
        }

        if (depthExceeded || count < 1) {
            break;
        }
    }
    return count < 1;
}

std::int64_t CInventory::GetItemAmount(int itemClassId, int enchanted, bool checkVariation) {
    L2_READ_GUARD(*this);

    CItemFactory& items = CItemFactory::Inst();
    std::uint64_t amount = 0;
    int currentIndex = GetFirstIndex();
    int depth = 0;
    while (currentIndex != -1) {
        CSharedItemData* const item = items.GetData(currentIndex);
        if (!item) {
            // Log.Add(
            //     LOG_ERROR,
            //     "spItem is Invalid at file[%s], line[%d]",
            //     "..\\Shared\\Inventory_common.cpp",
            //     0x5d)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Amount scan for template %d stopped at invalid item index %d for creature index %d.
            //     returning the amount accumulated so far.", itemClassId, currentIndex, m_nCreatureIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Amount scan for template %d stopped at invalid item index %d for creature index %d; returning "
                u"the amount accumulated so far.",
                itemClassId, currentIndex, m_nCreatureIndex
            );
            break;
        }

        if (item->m_nClassID == itemClassId && (enchanted < 0 || item->GetEnchanted() == enchanted) &&
            (!checkVariation || !item->m_bIsVariation)) {
            if (IsStackable(static_cast<std::int32_t>(item->m_ItemConsumeType))) {
                amount += static_cast<std::uint64_t>(item->m_nCount);
            } else {
                ++amount;
            }
        }

        currentIndex = items.GetNextIndex(currentIndex);
        ++depth;
        if (depth >= kMaximumTraversalDepth) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth > %d at file[%s], line[%d]",
            //     0x192,
            //     "..\\Shared\\Inventory_common.cpp",
            //     0x75)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Amount scan for template %d reached the %d-link limit for creature index %d. Returning the
            //     amount accumulated so far. Next index: %d.", itemClassId, kMaximumTraversalDepth, m_nCreatureIndex,
            //     currentIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Amount scan for template %d reached the %d-link limit for creature index %d; returning the "
                u"amount accumulated so far. Next index: %d.",
                itemClassId, kMaximumTraversalDepth, m_nCreatureIndex, currentIndex
            );
            break;
        }
    }

    std::int64_t signedAmount = 0;
    std::memcpy(&signedAmount, &amount, sizeof(signedAmount));
    return signedAmount;
}

bool CInventory::UseLure(const int itemClassId, const std::int64_t amount) {
    // Current L2Server 0x007c708c.
    L2_GUARD_FUNCTION();
    CSPointer<CCreature> owner;
    CSharedItemData* const foundItem = [&]() -> CSharedItemData* {
        L2_READ_GUARD(*this);
        owner = CCreatureFactory::Inst().FindObjectSP(m_nCreatureIndex);
        if (!owner) {
            return nullptr;
        }

        CSharedItemData* item = GetFirst();
        int depth = 0;
        while (item != nullptr) {
            CSharedItemConstant* const constant = item->GetConstant();
            if (constant->m_EtcitemType == EIT_LURE && item->m_nClassID == itemClassId) {
                return item;
            }
            item = item->Next();
            if (++depth > 0x192) {
                // Log.Add(
                //     LOG_ERROR,
                //     "depth > %d  at file[%s], line[%d]",
                //     0x192,
                //     "Inventory_server.cpp",
                //     0x997)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[inventory] Lure search for template %d reached the %d-link limit for creature index %d. Returning
                //     failure without requesting removal of %lld items.", itemClassId, kMaximumTraversalDepth,
                //     m_nCreatureIndex,
                //     static_cast<long long>(amount))
                L2_LOG_ERROR(
                    Log,
                    u"[inventory] Lure search for template %d reached the %d-link limit for creature index %d; returning "
                    u"failure without requesting removal of %lld items.",
                    itemClassId, kMaximumTraversalDepth, m_nCreatureIndex, static_cast<long long>(amount)
                );
                break;
            }
        }
        return nullptr;
    }();
    if (!owner) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]error, Cannot find creature.",
        //     "Inventory_server.cpp",
        //     0x982)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Cannot use lure template %d, amount %lld: creature index %d was not found.",
        //     itemClassId,
        //     static_cast<long long>(amount),
        //     m_nCreatureIndex)
        L2_LOG_ERROR(
            Log, u"[inventory] Cannot use lure template %d, amount %lld: creature index %d was not found.", itemClassId,
            static_cast<long long>(amount), m_nCreatureIndex
        );
        return false;
    }

    if (foundItem != nullptr) {
        return owner->DeleteItemInInventoryBeforeCommit(foundItem->m_nClassID, amount);
    }
    return false;
}

std::int64_t CInventory::GetEtcItemCountByCrystal(EtcItemType itemType, CrystalType crystalType) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    CItemFactory& items = CItemFactory::Inst();
    std::uint64_t amount = 0;
    CSharedItemData* item = GetFirst();
    int depth = 0;
    while (item) {
        const std::int32_t constantIndex = item->const_index;
        CSharedItemConstant* const constant = items.GetConstant(constantIndex);
        const std::int32_t constantCrystal = static_cast<std::int32_t>(constant->GetCrystalType());
        if (constant->m_EtcitemType == static_cast<std::int32_t>(itemType) &&
            BaseCrystal[constantCrystal] == BaseCrystal[static_cast<std::int32_t>(crystalType)]) {
            amount += static_cast<std::uint64_t>(item->m_nCount);
        }

        // Current CSharedItemData::Next first re-resolves this record's
        // +0x28 index through the factory before following main +0x3c.
        item = items.GetNext(item->index);
        ++depth;
        if (depth >= kMaximumTraversalDepth) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth > %d at file[%s], line[%d]",
            //     0x192,
            //     "Inventory_server.cpp",
            //     0x92f)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Item amount scan by type %d and crystal type %d reached the %d-link limit for creature index
            //     %d, returning the amount accumulated so far.", static_cast<int>(itemType), static_cast<int>(crystalType),
            //     kMaximumTraversalDepth,
            //     m_nCreatureIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Item amount scan by type %d and crystal type %d reached the %d-link limit for creature index "
                u"%d; returning the amount accumulated so far.",
                static_cast<int>(itemType), static_cast<int>(crystalType), kMaximumTraversalDepth, m_nCreatureIndex
            );
            break;
        }
    }

    std::int64_t signedAmount = 0;
    std::memcpy(&signedAmount, &amount, sizeof(signedAmount));
    return signedAmount;
}

std::int64_t CInventory::GetArrowCountByCrystal(CrystalType crystalType) {
    L2_GUARD_FUNCTION();
    return GetEtcItemCountByCrystal(static_cast<EtcItemType>(2), crystalType);
}

std::int64_t CInventory::GetBoltCountByCrystal(CrystalType crystalType) {
    L2_GUARD_FUNCTION();
    return GetEtcItemCountByCrystal(static_cast<EtcItemType>(0x1b), crystalType);
}

void CInventory::AddSlotInfo(int slotIndex, CItem* item) {
    L2_GUARD_FUNCTION();

    if (slotIndex < -1 || !m_bIsUser) {
        return;
    }

    L2_WRITE_GUARD(*this);
    CSlotIndexManager& manager = item && item->IsArtifact() ? m_ArtifactSlotIndexManager : m_SlotIndexManager;
    const int assignedSlot = manager.Add(slotIndex, item);
    if (assignedSlot != -1) {
        ::CYieldLock* const itemLock = item->m_lock;
        {
            L2_GUARD(*itemLock);
            item->m_data->m_nInventorySlotIndex = assignedSlot;
        }
    }
}

void CInventory::RemoveSlotInfo(CItem* item) {
    L2_GUARD_FUNCTION();

    if (!m_bIsUser) {
        return;
    }

    L2_WRITE_GUARD(*this);
    CSlotIndexManager& manager = item && item->IsArtifact() ? m_ArtifactSlotIndexManager : m_SlotIndexManager;
    manager.Remove(item);

    ::CYieldLock* const itemLock = item->m_lock;
    {
        L2_GUARD(*itemLock);
        item->m_data->m_nInventorySlotIndex = -1;
    }
}

CItem* CInventory::GetByDBID(int databaseId) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    CItemFactory& items = CItemFactory::Inst();
    int currentIndex = GetFirstIndex();
    int depth = 0;
    while (currentIndex != -1) {
        CSharedItemData* const data = items.GetData(currentIndex);
        if (data && data->m_nDBID == databaseId) {
            const std::uint32_t sid = UnsignedSmartId(data->m_nId);
            return ItemSmartIdManager()->GetObjectW(sid);
        }

        currentIndex = items.GetNextIndex(currentIndex);
        ++depth;
        if (depth >= kMaximumTraversalDepth) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth > %d at file[%s], line[%d]",
            //     0x192,
            //     "Inventory_server.cpp",
            //     0x207)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Item search by DB ID %d reached the %d-link limit for creature index %d. Returning not
            //     found. Next index: %d.", databaseId, kMaximumTraversalDepth, m_nCreatureIndex, currentIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Item search by DB ID %d reached the %d-link limit for creature index %d; returning not found. "
                u"Next index: %d.",
                databaseId, kMaximumTraversalDepth, m_nCreatureIndex, currentIndex
            );
            break;
        }
    }
    return nullptr;
}

CItem* CInventory::GetByServerID(int serverId) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    CItemFactory& items = CItemFactory::Inst();
    int currentIndex = GetFirstIndex();
    int depth = 0;
    while (currentIndex != -1) {
        CSharedItemData* const data = items.GetData(currentIndex);
        if (data && data->m_nId == serverId) {
            return ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(serverId));
        }

        currentIndex = items.GetNextIndex(currentIndex);
        ++depth;
        if (depth >= kMaximumTraversalDepth) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth>%d at file[%s], line[%d]",
            //     0x192,
            //     "Inventory_server.cpp",
            //     0x230)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Item search by SID %u reached the %d-link limit for creature index %d. Returning not found.
            //     Next index: %d.", static_cast<unsigned int>(serverId), kMaximumTraversalDepth, m_nCreatureIndex,
            //     currentIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Item search by SID %u reached the %d-link limit for creature index %d; returning not found. "
                u"Next index: %d.",
                static_cast<unsigned int>(serverId), kMaximumTraversalDepth, m_nCreatureIndex, currentIndex
            );
            break;
        }
    }
    return nullptr;
}

CItem* CInventory::GetByIndex(int itemIndex) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    CItemFactory& items = CItemFactory::Inst();
    int currentIndex = GetFirstIndex();
    int depth = 0;
    while (currentIndex != -1) {
        if (currentIndex == itemIndex) {
            CSharedItemData* const data = items.GetData(currentIndex);
            const std::uint32_t sid = UnsignedSmartId(data->m_nId);
            CItem* const item = ItemSmartIdManager()->GetObjectW(sid);
            if (item && item->m_data->index == itemIndex) {
                return item;
            }
        }

        currentIndex = items.GetNextIndex(currentIndex);
        ++depth;
        if (depth >= kMaximumTraversalDepth) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth > %d at file[%s], line[%d]",
            //     0x192,
            //     "Inventory_server.cpp",
            //     0x25e)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Item search by index %d reached the %d-link limit for creature index %d. Returning not
            //     found. Next index: %d.", itemIndex, kMaximumTraversalDepth, m_nCreatureIndex, currentIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Item search by index %d reached the %d-link limit for creature index %d; returning not found. "
                u"Next index: %d.",
                itemIndex, kMaximumTraversalDepth, m_nCreatureIndex, currentIndex
            );
            break;
        }
    }
    return nullptr;
}

CItem* CInventory::GetFirstItemBy(ItemFunctor& functor) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    CItemFactory& items = CItemFactory::Inst();
    int currentIndex = GetFirstIndex();
    int depth = 0;
    while (currentIndex != -1) {
        bool depthExceeded = false;
        {
            CSPointer<CObject> item = FindItemObjectSP(currentIndex);
            if (item) {
                CItem* const typedItem = item->CastItem();
                if (functor(typedItem)) {
                    return typedItem;
                }
            }

            currentIndex = items.GetNextIndex(currentIndex);
            ++depth;
            if (depth >= kMaximumTraversalDepth) {
                // Log.Add(
                //     LOG_ERROR,
                //     "depth>%d at file[%s], line[%d]",
                //     0x192,
                //     "Inventory_server.cpp",
                //     0x28a)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[inventory] Item predicate search reached the %d-link limit for creature index %d. Returning not
                //     found. Next index: %d.", kMaximumTraversalDepth, m_nCreatureIndex, currentIndex)
                L2_LOG_ERROR(
                    Log,
                    u"[inventory] Item predicate search reached the %d-link limit for creature index %d; returning not "
                    u"found. Next index: %d.",
                    kMaximumTraversalDepth, m_nCreatureIndex, currentIndex
                );
                depthExceeded = true;
            }
        }

        if (depthExceeded) {
            break;
        }
    }
    return nullptr;
}

CItem* CInventory::GetHeroItem(const bool weapon) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    CItemFactory& items = CItemFactory::Inst();
    int currentIndex = GetFirstIndex();
    int depth = 0;
    while (currentIndex != -1) {
        CSPointer<CItem> item = items.FindObjectSP(currentIndex);
        if (item && item->IsItem() && item->IsWeapon() == weapon) {
            CItemConstInfo* const itemConstInfo = g_ItemConstInfoDB.GetItemConstInfo(item->m_data->m_nClassID);
            if (itemConstInfo != nullptr && itemConstInfo->HasEquipConditionType(ECT_HERO)) {
                return item.get();
            }
        }

        currentIndex = items.GetNextIndex(currentIndex);
        ++depth;
        if (depth > 0x192) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth > %d at file[%s], line[%d]",
            //     0x192,
            //     "Inventory_server.cpp",
            //     0x2eb)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Hero-item search (%ls) reached the %d-link limit for creature index %d. Returning not found.
            //     Next index: %d.", weapon ? L"weapon" : L"nonweapon", kMaximumTraversalDepth, m_nCreatureIndex,
            //     currentIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Hero-item search (%ls) reached the %d-link limit for creature index %d; returning not found. "
                u"Next index: %d.",
                weapon ? L"weapon" : L"nonweapon", kMaximumTraversalDepth, m_nCreatureIndex, currentIndex
            );
            break;
        }
    }
    return nullptr;
}

CItem* CInventory::GetHeroEntryItem(const bool olympiadEntry) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    const int heroEntryType = olympiadEntry ? 1 : 2;
    CItemFactory& items = CItemFactory::Inst();
    int currentIndex = GetFirstIndex();
    int depth = 0;
    while (currentIndex != -1) {
        CSPointer<CItem> item = items.FindObjectSP(currentIndex);
        if (item && item->IsItem() && item->m_const->m_nIsHeroEntryType == heroEntryType) {
            return item.get();
        }

        currentIndex = items.GetNextIndex(currentIndex);
        ++depth;
        if (depth > 0x192) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth > %d at file[%s], line[%d]",
            //     0x192,
            //     "Inventory_server.cpp",
            //     0x319)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Hero-entry item search for type %d reached the %d-link limit for creature index %d.
            //     returning not found. Next index: %d.", heroEntryType, kMaximumTraversalDepth, m_nCreatureIndex,
            //     currentIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Hero-entry item search for type %d reached the %d-link limit for creature index %d; returning "
                u"not found. Next index: %d.",
                heroEntryType, kMaximumTraversalDepth, m_nCreatureIndex, currentIndex
            );
            break;
        }
    }
    return nullptr;
}

CItem* CInventory::GetNextItemBy(int itemIndex, ItemFunctor& functor) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    if (itemIndex == -1) {
        return nullptr;
    }

    CItemFactory& items = CItemFactory::Inst();
    CSPointer<CObject> item;
    {
        CSPointer<CObject> current = FindItemObjectSP(itemIndex);
        item = current;
    }
    if (!item) {
        return nullptr;
    }

    CItem* const firstItem = static_cast<CItem*>(item.get());
    const std::int32_t itemClassId = firstItem->m_data->m_nClassID;
    int currentIndex = items.GetNextIndex(itemIndex);
    int depth = 0;
    while (true) {
        {
            CSPointer<CObject> current = FindItemObjectSP(currentIndex);
            item = current;
        }

        if (item) {
            CItem* const typedItem = item->CastItem();
            if (typedItem->m_nClassId == itemClassId && functor(typedItem)) {
                return typedItem;
            }
        }

        currentIndex = items.GetNextIndex(currentIndex);
        ++depth;
        if (depth >= kMaximumTraversalDepth) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth>%d at file[%s], line[%d]",
            //     0x192,
            //     "Inventory_server.cpp",
            //     0x2c1)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Next-item predicate search after index %d, template %d reached the %d-link limit for
            //     creature index %d. Returning not found. Next index: %d.", itemIndex, itemClassId, kMaximumTraversalDepth,
            //     m_nCreatureIndex,
            //     currentIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Next-item predicate search after index %d, template %d reached the %d-link limit for creature "
                u"index %d; returning not found. Next index: %d.",
                itemIndex, itemClassId, kMaximumTraversalDepth, m_nCreatureIndex, currentIndex
            );
            break;
        }
        if (currentIndex == -1) {
            break;
        }
    }
    return nullptr;
}

CItem* CInventory::GetFirstItemByClassID(int itemClassId, bool includeVariation, bool includeEnsoul) {
    L2_GUARD_FUNCTION();

    ClassIDFunctor classIdFunctor(itemClassId);
    VariationFunctor variationFunctor;
    NotFunctor notVariationFunctor(variationFunctor);
    FilterFunctor variationFilter(notVariationFunctor, !includeVariation);
    EnsoulFunctor ensoulFunctor;
    NotFunctor notEnsoulFunctor(ensoulFunctor);
    FilterFunctor ensoulFilter(notEnsoulFunctor, !includeEnsoul);
    AndFunctor itemKindFilter(variationFilter, ensoulFilter);
    AndFunctor predicate(classIdFunctor, itemKindFilter);
    return GetFirstItemBy(predicate);
}

CItem* CInventory::GetNextItemByClassID(int itemIndex, bool includeVariation, bool includeEnsoul) {
    // Current L2Server 0x007c3f50. GetNextItemBy keeps the starting
    // item's class ID, so this route applies only the variation/ensoul
    // filters used by the current multisell traversal.
    L2_GUARD_FUNCTION();

    VariationFunctor variationFunctor;
    NotFunctor notVariationFunctor(variationFunctor);
    FilterFunctor variationFilter(notVariationFunctor, !includeVariation);
    EnsoulFunctor ensoulFunctor;
    NotFunctor notEnsoulFunctor(ensoulFunctor);
    FilterFunctor ensoulFilter(notEnsoulFunctor, !includeEnsoul);
    AndFunctor predicate(variationFilter, ensoulFilter);
    return GetNextItemBy(itemIndex, predicate);
}

CItem* CInventory::GetNextMinEnchantedItemByClassID(
    int itemClassId, int currentItemIndex, bool includeVariation, bool includeEnsoul
) {
    // Current L2Server 0x007c480c.
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    CItem* item = GetFirstItemByClassID(itemClassId, includeVariation, includeEnsoul);
    CItem* candidate = nullptr;
    int minimumEnchant = 100;
    while (item != nullptr) {
        const int itemIndex = item->m_data->index;
        if (itemIndex != currentItemIndex) {
            const int enchanted = item->m_data->GetEnchanted();
            if (enchanted == 0) {
                return item;
            }
            if (enchanted < minimumEnchant) {
                minimumEnchant = enchanted;
                candidate = item;
            }
        }
        item = GetNextItemByClassID(itemIndex, includeVariation, includeEnsoul);
    }
    return candidate;
}

CItem* CInventory::GetMinEnchantedItemByClassID(int itemClassId, bool includeVariation, bool includeEnsoul) {
    // Current L2Server 0x007c497c.
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    CItem* minimum = GetFirstItemByClassID(itemClassId, includeVariation, includeEnsoul);
    if (minimum == nullptr) {
        return nullptr;
    }

    CItem* item = minimum;
    do {
        const int enchanted = item->m_data->GetEnchanted();
        if (enchanted == 0) {
            return item;
        }
        if (enchanted < minimum->m_data->GetEnchanted()) {
            minimum = item;
        }
        item = GetNextItemByClassID(item->m_data->index, includeVariation, includeEnsoul);
    } while (item != nullptr);
    return minimum;
}

CItem* CInventory::GetFirstItemByClassIDEnchantedAndOption(int itemClassId, int enchanted, ItemOptionKey optionKey) {
    L2_GUARD_FUNCTION();

    ClassIDFunctor classIdFunctor(itemClassId);
    EnchantFunctor enchantFunctor(enchanted);
    ItemOptionKeyFunctor optionFunctor(optionKey);
    AndFunctor enchantAndOption(enchantFunctor, optionFunctor);
    AndFunctor predicate(enchantAndOption, classIdFunctor);
    return GetFirstItemBy(predicate);
}

CItem* CInventory::GetFirstItemByAttribute(const int itemClassId, const BaseAttribute& attribute) {
    // Current L2Server 0x007c42ec.
    L2_GUARD_FUNCTION();
    ClassIDFunctor classIdFunctor(itemClassId);
    SameBaseAttributeFunctor attributeFunctor(attribute);
    AndFunctor predicate(classIdFunctor, attributeFunctor);
    return GetFirstItemBy(predicate);
}

CItem* CInventory::GetNextItemByClassIDEnchantedAndOption(int itemIndex, int enchanted, ItemOptionKey optionKey) {
    L2_GUARD_FUNCTION();

    EnchantFunctor enchantFunctor(enchanted);
    ItemOptionKeyFunctor optionFunctor(optionKey);
    AndFunctor predicate(enchantFunctor, optionFunctor);
    return GetNextItemBy(itemIndex, predicate);
}

CItem* CInventory::GetFirstItemByEnchantedAttribute() {
    L2_GUARD_FUNCTION();

    BaseAttributeFunctor predicate;
    return GetFirstItemBy(predicate);
}

CItem* CInventory::GetNextItemByEnchantedAttribute(int itemIndex) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    if (itemIndex == -1) {
        return nullptr;
    }

    CItemFactory& items = CItemFactory::Inst();
    CSPointer<CObject> item;
    {
        CSPointer<CObject> current = FindItemObjectSP(itemIndex);
        item = current;
    }
    if (!item) {
        return nullptr;
    }

    int currentIndex = items.GetNextIndex(itemIndex);
    int depth = 0;
    while (true) {
        {
            CSPointer<CObject> current = FindItemObjectSP(currentIndex);
            item = current;
        }

        if (item) {
            CItem* const typedItem = item->CastItem();
            if (typedItem->m_itemBaseAttribute.IsEnchanted(ATTRT_NONE)) {
                return typedItem;
            }
        }

        currentIndex = items.GetNextIndex(currentIndex);
        ++depth;
        if (depth >= kMaximumTraversalDepth) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth>%d at file[%s], line[%d]",
            //     0x192,
            //     "Inventory_server.cpp",
            //     0x3bc)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Attribute-enchanted item search after index %d reached the %d-link limit for creature index
            //     %d. Returning not found. Next index: %d.", itemIndex, kMaximumTraversalDepth, m_nCreatureIndex,
            //     currentIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Attribute-enchanted item search after index %d reached the %d-link limit for creature index "
                u"%d; returning not found. Next index: %d.",
                itemIndex, kMaximumTraversalDepth, m_nCreatureIndex, currentIndex
            );
            break;
        }
        if (currentIndex == -1) {
            break;
        }
    }
    return nullptr;
}

CCreature* CInventory::GetOwner() {
    L2_GUARD_FUNCTION();

    CCreatureFactory& creatures = CCreatureFactory::Inst();
    if (m_nCreatureIndex < 0 || m_nCreatureIndex >= creatures.GetSize()) {
        return nullptr;
    }

    const std::int32_t signedSid = creatures.GetIdByIndex(m_nCreatureIndex);
    std::uint32_t sid = 0;
    std::memcpy(&sid, &signedSid, sizeof(sid));
    return g_CreatureSmartIdManager.GetObjectW(sid);
}

void CInventory::LogByInventoryOverType(InventoryOverType type, CCreature* creature, CItem* item, std::int64_t amount) {
    L2_GUARD_FUNCTION();
    if (creature == nullptr) {
        return;
    }

    int userMessage = SystemMessageId::WEIGHT_LIMIT_EXCEEDED;
    int petMessage = SystemMessageId::CANNOT_GIVE_ITEM_PET_INVENTORY_WEIGHT_LIMIT;
    if (type == OVER_COUNT) {
        userMessage = SystemMessageId::CANNOT_PROCEED_POSSESSION_LIMIT_EXCEEDED;
        petMessage = SystemMessageId::CANNOT_PROCEED_POSSESSION_LIMIT_EXCEEDED;
    } else if (type == OVER_INVEN_LIMIT) {
        userMessage = SystemMessageId::NOT_ENOUGH_INVENTORY_SPACE;
        petMessage = SystemMessageId::CANNOT_GIVE_ITEM_PET_INVENTORY_SLOT_LIMIT;
    } else if (type == OVER_ARTIFACT_INVEN_LIMIT) {
        userMessage = SystemMessageId::NOT_ENOUGH_ARTIFACT_INVENTORY_SPACE;
        petMessage = SystemMessageId::CANNOT_GIVE_ITEM_PET_INVENTORY_SLOT_LIMIT;
    }

    User* user = nullptr;
    if (creature->IsPC()) {
        User* const messageUser = creature->CastUser();
        messageUser->GetSocket()->SendSystemMessage(userMessage);
        user = creature->CastUser();
    } else if (creature->IsPet()) {
        CPet* const messagePet = creature->CastPet();
        User* const messageUser = messagePet->GetMaster();
        if (messageUser == nullptr) {
            return;
        }
        messageUser->GetSocket()->SendSystemMessage(petMessage);
        user = creature->CastPet()->GetMaster();
    }

    if (user == nullptr || item == nullptr) {
        return;
    }

    std::array<char16_t, 256> attributeWide{};
    std::array<char16_t, 256> optionWide{};
    std::array<char16_t, 260> ensoulWide{};
    item->m_itemBaseAttribute.GetLogString(attributeWide.data());
    FormatUtf16To(
        optionWide.data(), optionWide.size(), u"%d:%d:%d", item->m_itemOption.intensiveItemClassID,
        item->m_itemOption.itemOptionKey.variationOption1, item->m_itemOption.itemOptionKey.variationOption2
    );
    item->MakeEnsoulLogInfo(ensoulWide.data());

    std::array<char16_t, 256> attribute{};
    std::array<char16_t, 256> option{};
    std::array<char16_t, 260> ensoul{};
    InventoryHostWideToTargetUtf16(attributeWide.data(), attribute);
    InventoryHostWideToTargetUtf16(optionWide.data(), option);
    InventoryHostWideToTargetUtf16(ensoulWide.data(), ensoul);
    std::u16string userNameStorage;
    const char16_t* const userName = (userNameStorage = user->GetName()).c_str();

    const FVector position = user->GetPos();
    const int inventoryCount =
        type == OVER_ARTIFACT_INVEN_LIMIT ? user->m_data->m_nCurrentArtifactInventoryNum : user->m_data->m_nNumInventory;

    CLogFormatter formatter;
    formatter.WriteLog(
        0x3ae, static_cast<std::int64_t>(user->GetDBID()), static_cast<std::int64_t>(user->GetAccountID()), 0, 0,
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), ensoul.data(), attribute.data(), option.data(),
        static_cast<std::int64_t>(user->m_data->m_Race), static_cast<std::int64_t>(user->m_data->m_Sex),
        static_cast<std::int64_t>(user->m_data->m_nClass), static_cast<std::int64_t>(user->m_data->m_nLevel),
        static_cast<std::int64_t>(type), static_cast<std::int64_t>(m_nCarryingWeight), 0,
        static_cast<std::int64_t>(item->m_data->m_nClassID), amount, static_cast<std::int64_t>(inventoryCount), userName,
        user->m_sAccountName.data(), nullptr, nullptr, static_cast<std::int64_t>(item->m_data->m_nDBID)
    );
}

bool CInventory::CheckAddable(int itemIndex, bool includeVariation) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    CSPointer<CObject> itemObject = FindItemObjectSP(itemIndex);
    if (!itemObject) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CheckAddable failed. because "
        //     "CItemFactory::Inst().GetObjectSP failed.",
        //     "Inventory_server.cpp",
        //     0x452)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Cannot check addition of item index %d to creature index %d: item object was not found.",
        //     itemIndex,
        //     m_nCreatureIndex)
        L2_LOG_ERROR(
            Log, u"[inventory] Cannot check addition of item index %d to creature index %d: item object was not found.",
            itemIndex, m_nCreatureIndex
        );
        return false;
    }
    CItem* const item = itemObject->CastItem();

    CSPointer<CObject> ownerObject = FindCreatureObjectSP(m_nCreatureIndex);
    if (!ownerObject) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CheckAddable failed. because "
        //     "CCreatureFactory::Inst().GetObjectSP failed.",
        //     "Inventory_server.cpp",
        //     0x459)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Cannot check addition of item index %d: creature index %d was not found.",
        //     itemIndex,
        //     m_nCreatureIndex)
        L2_LOG_ERROR(
            Log, u"[inventory] Cannot check addition of item index %d: creature index %d was not found.", itemIndex,
            m_nCreatureIndex
        );
        return false;
    }
    CCreature* const owner = ownerObject->CastCreature();

    InventoryOverType failure = OVER_COUNT;
    if (!CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
        failure = OVER_INVEN_LIMIT;
        if (item->m_data->m_Type == 3) {
            if (owner->m_data->m_nCurrentQuestInventoryNum >= owner->GetQuestInventoryLimit()) {
                LogByInventoryOverType(failure, owner, item, item->m_data->m_nCount);
                return false;
            }
        } else if (item->IsArtifact()) {
            failure = OVER_ARTIFACT_INVEN_LIMIT;
            if (owner->m_data->m_nCurrentArtifactInventoryNum >= owner->GetArtifactInventoryLimit()) {
                LogByInventoryOverType(failure, owner, item, item->m_data->m_nCount);
                return false;
            }
        } else if (owner->m_data->m_nNumInventory >= owner->GetInventoryLimit()) {
            LogByInventoryOverType(failure, owner, item, item->m_data->m_nCount);
            return false;
        }
    } else {
        CItem* const existing = GetFirstItemByClassID(item->m_data->m_nClassID, includeVariation, false);
        if (existing == nullptr) {
            failure = OVER_INVEN_LIMIT;
            if (item->m_data->m_Type == 3) {
                if (owner->m_data->m_nCurrentQuestInventoryNum >= owner->GetQuestInventoryLimit()) {
                    LogByInventoryOverType(failure, owner, item, item->m_data->m_nCount);
                    return false;
                }
            } else if (item->IsArtifact()) {
                failure = OVER_ARTIFACT_INVEN_LIMIT;
                if (owner->m_data->m_nCurrentArtifactInventoryNum >= owner->GetArtifactInventoryLimit()) {
                    LogByInventoryOverType(failure, owner, item, item->m_data->m_nCount);
                    return false;
                }
            } else if (owner->m_data->m_nNumInventory >= owner->GetInventoryLimit()) {
                LogByInventoryOverType(failure, owner, item, item->m_data->m_nCount);
                return false;
            }
        } else {
            failure = OVER_COUNT;
            if (!existing->IsAddableAmount(item->m_data->m_nCount)) {
                LogByInventoryOverType(failure, owner, item, item->m_data->m_nCount);
                return false;
            }
            if (item->m_data->m_nClassID == 0x39) {
                return true;
            }
        }
    }

    std::int64_t additionalWeight = 0;
    if (item->m_data->m_ItemConsumeType == ICT_CHARGE) {
        additionalWeight = item->m_const->m_nWeight;
    } else if (item->m_data->m_ItemConsumeType == ICT_NORMAL || item->m_data->m_ItemConsumeType == ICT_STACKABLE) {
        additionalWeight = static_cast<std::int64_t>(item->m_const->m_nWeight) * item->m_data->m_nCount;
    }

    if (static_cast<double>(static_cast<std::int64_t>(m_nCarryingWeight) + additionalWeight) <= owner->CarryWeight()) {
        return true;
    }

    LogByInventoryOverType(OVER_WEIGHT, owner, item, item->m_data->m_nCount);
    return false;
}

bool CInventory::CheckAddable(int itemClassId, std::int64_t count, bool includeVariation) {
    // Current L2Server 0x007c927c.
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    CSPointer<CObject> ownerObject = FindCreatureObjectSP(m_nCreatureIndex);
    if (!ownerObject) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] CheckAddable failed. because "
        //     "CCreatureFactory::Inst().GetObjectSP failed.",
        //     "Inventory_server.cpp",
        //     0x4ce)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Cannot check addition of template %d, amount %lld: creature index %d was not found.",
        //     itemClassId,
        //     static_cast<long long>(count),
        //     m_nCreatureIndex)
        L2_LOG_ERROR(
            Log, u"[inventory] Cannot check addition of template %d, amount %lld: creature index %d was not found.",
            itemClassId, static_cast<long long>(count), m_nCreatureIndex
        );
        return false;
    }
    CCreature* const owner = ownerObject->CastCreature();

    CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
    CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item[%d] is not exist.",
        //     L"Inventory_server.cpp",
        //     0x4d5,
        //     itemClassId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Cannot check addition of template %d, amount %lld for creature index %d: template is missing or
        //     is not an item.", itemClassId, static_cast<long long>(count), m_nCreatureIndex)
        L2_LOG_ERROR(
            Log,
            u"[inventory] Cannot check addition of template %d, amount %lld for creature index %d: template is missing or "
            u"is not an item.",
            itemClassId, static_cast<long long>(count), m_nCreatureIndex
        );
        return false;
    }

    const bool stackable = CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType);
    InventoryOverType failure = OVER_COUNT;
    if (stackable) {
        CItem* const existing = GetFirstItemByClassID(itemClassId, includeVariation, false);
        if (existing != nullptr) {
            if (!existing->IsAddableAmount(count)) {
                LogByInventoryOverType(failure, owner, item, count);
                return false;
            }
            if (itemClassId == 0x39) {
                return true;
            }
        } else {
            failure = OVER_INVEN_LIMIT;
            if (item->m_data->m_Type == IT_QUESTITEM) {
                if (owner->m_data->m_nCurrentQuestInventoryNum >= owner->GetQuestInventoryLimit()) {
                    LogByInventoryOverType(failure, owner, item, count);
                    return false;
                }
            } else if (item->IsArtifact()) {
                failure = OVER_ARTIFACT_INVEN_LIMIT;
                if (owner->m_data->m_nCurrentArtifactInventoryNum >= owner->GetArtifactInventoryLimit()) {
                    LogByInventoryOverType(failure, owner, item, count);
                    return false;
                }
            } else if (owner->m_data->m_nNumInventory >= owner->GetInventoryLimit()) {
                LogByInventoryOverType(failure, owner, item, count);
                return false;
            }
        }
    } else {
        failure = OVER_INVEN_LIMIT;
        const std::int64_t currentSlots = item->m_data->m_Type == IT_QUESTITEM ? owner->m_data->m_nCurrentQuestInventoryNum
            : item->IsArtifact() ? owner->m_data->m_nCurrentArtifactInventoryNum
                                 : owner->m_data->m_nNumInventory;
        const std::int64_t slotLimit = item->m_data->m_Type == IT_QUESTITEM ? owner->GetQuestInventoryLimit()
            : item->IsArtifact()                                            ? owner->GetArtifactInventoryLimit()
                                                                            : owner->GetInventoryLimit();
        if (item->IsArtifact()) {
            failure = OVER_ARTIFACT_INVEN_LIMIT;
        }
        const std::int64_t requestedSlots = currentSlots + count;
        if (requestedSlots > 999999999999LL || requestedSlots > slotLimit) {
            LogByInventoryOverType(failure, owner, item, count);
            return false;
        }
    }

    std::int64_t additionalWeight = 0;
    if (item->m_data->m_ItemConsumeType == ICT_CHARGE) {
        additionalWeight = item->m_const->m_nWeight;
    } else if (item->m_data->m_ItemConsumeType == ICT_NORMAL || item->m_data->m_ItemConsumeType == ICT_STACKABLE) {
        additionalWeight = static_cast<std::int64_t>(item->m_const->m_nWeight) * count;
        if (additionalWeight > std::numeric_limits<std::int32_t>::max()) {
            return false;
        }
    }

    if (static_cast<double>(static_cast<std::int64_t>(m_nCarryingWeight) + additionalWeight) <= owner->CarryWeight()) {
        return true;
    }

    LogByInventoryOverType(OVER_WEIGHT, owner, item, count);
    return false;
}

bool CInventory::CheckAddableMulti(const ItemAmountDataStruct& items, bool includeVariation) {
    // Current L2Server 0x007c9a54.
    L2_GUARD_FUNCTION();

    CSPointer<CCreature> owner = CCreatureFactory::Inst().FindObjectSP(m_nCreatureIndex);
    if (!owner) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%S][%d]Creature is invalid.",
        //     "Inventory_server.cpp",
        //     0x554)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Cannot check batch addition: creature index %d was not found.",
        //     m_nCreatureIndex)
        L2_LOG_ERROR(Log, u"[inventory] Cannot check batch addition: creature index %d was not found.", m_nCreatureIndex);
        return false;
    }

    std::int32_t normalItemDelta = 0;
    std::int32_t questItemDelta = 0;
    std::int32_t artifactItemDelta = 0;
    std::int64_t weightDelta = 0;

    L2_READ_GUARD(*this);

    // Fix the source bug that skipped every entry after the first 199.
    for (const ItemAmountData* const entry : items.dataList) {
        const std::int32_t itemClassId = entry->m_nItemClassId;
        const std::int64_t amount = entry->m_nItemAmount;
        if (amount == 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"nItemAmount [%I64d] is wrong!!!",
            //     amount)
            // Log.Add(
            //     LOG_WARN,
            //     L"[inventory] Batch addition check rejected for creature index %d, template %d: amount delta is zero.",
            //     m_nCreatureIndex,
            //     itemClassId)
            L2_LOG_WARN(
                Log, u"[inventory] Batch addition check rejected for creature index %d, template %d: amount delta is zero.",
                m_nCreatureIndex, itemClassId
            );
            return false;
        }

        CItem* item = GetFirstItemByClassID(itemClassId, includeVariation, false);
        if (item == nullptr) {
            if (amount < 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Cannot find item. itemclassid %d, %I64d is wrong!!!",
                //     itemClassId,
                //     amount)
                // Log.Add(
                //     LOG_WARN,
                //     L"[inventory] Batch addition check rejected for creature index %d: cannot remove template %d that was
                //     not found in inventory (delta %lld).", m_nCreatureIndex, itemClassId, static_cast<long long>(amount))
                L2_LOG_WARN(
                    Log,
                    u"[inventory] Batch addition check rejected for creature index %d: cannot remove template %d that was "
                    u"not found in inventory (delta %lld).",
                    m_nCreatureIndex, itemClassId, static_cast<long long>(amount)
                );
                return false;
            }

            item = static_cast<CItem*>(g_ObjectDB.GetObjectW(itemClassId, false));
            if (item == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"itemclassid %d is wrong!!!",
                //     itemClassId)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[inventory] Batch addition check rejected for creature index %d: template %d was not found (delta
                //     %lld).", m_nCreatureIndex, itemClassId, static_cast<long long>(amount))
                L2_LOG_ERROR(
                    Log,
                    u"[inventory] Batch addition check rejected for creature index %d: template %d was not found (delta "
                    u"%lld).",
                    m_nCreatureIndex, itemClassId, static_cast<long long>(amount)
                );
                return false;
            }

            const bool stackable = CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType);
            if (!stackable && amount != 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Cannot find item. itemclassid %d, %d is wrong!!!",
                //     itemClassId,
                //     runtime::Int32FromBits(
                //         static_cast<std::uint32_t>(amount)))
                // Log.Add(
                //     LOG_WARN,
                //     L"[inventory] Batch addition check rejected for creature index %d: nonstackable template %d has
                //     addition delta %lld, expected 1.", m_nCreatureIndex, itemClassId, static_cast<long long>(amount))
                L2_LOG_WARN(
                    Log,
                    u"[inventory] Batch addition check rejected for creature index %d: nonstackable template %d has "
                    u"addition delta %lld; expected 1.",
                    m_nCreatureIndex, itemClassId, static_cast<long long>(amount)
                );
                return false;
            }
            if (stackable && amount > 999999999999LL) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]itemclassid[%d]'s amount is invalid.[%I64d]",
                //     L"Inventory_server.cpp",
                //     0x587,
                //     itemClassId,
                //     amount)
                // Log.Add(
                //     LOG_WARN,
                //     L"[inventory] Batch addition check rejected for creature index %d: new stack of template %d has amount
                //     %lld, exceeding 999999999999.", m_nCreatureIndex, itemClassId, static_cast<long long>(amount))
                L2_LOG_WARN(
                    Log,
                    u"[inventory] Batch addition check rejected for creature index %d: new stack of template %d has amount "
                    u"%lld, exceeding 999999999999.",
                    m_nCreatureIndex, itemClassId, static_cast<long long>(amount)
                );
                return false;
            }

            if (item->m_data->m_Type == IT_QUESTITEM) {
                questItemDelta = runtime::AddInt32Wrapping(questItemDelta, 1);
            } else if (item->IsArtifact()) {
                artifactItemDelta = runtime::AddInt32Wrapping(artifactItemDelta, 1);
            } else {
                normalItemDelta = runtime::AddInt32Wrapping(normalItemDelta, 1);
            }

            const ItemConsumeType consumeType = item->m_data->m_ItemConsumeType;
            const std::int64_t itemWeight = static_cast<std::int64_t>(item->m_const->m_nWeight);
            if (consumeType == ICT_CHARGE) {
                weightDelta = runtime::AddInt64Wrapping(weightDelta, itemWeight);
            } else if (consumeType != ICT_ASSET) {
                weightDelta =
                    runtime::AddInt64Wrapping(weightDelta, runtime::MultiplyInt64Wrapping(itemWeight, amount));
            }
            continue;
        }

        const bool stackable = CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType);
        if (amount < 1) {
            if (!stackable) {
                if (amount != -1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Cannot find item. itemclassid %d, %I64d is wrong!!!",
                    //     itemClassId,
                    //     amount)
                    // Log.Add(
                    //     LOG_WARN,
                    //     L"[inventory] Batch addition check rejected for creature index %d: nonstackable template %d has
                    //     removal delta %lld, expected -1.", m_nCreatureIndex, itemClassId, static_cast<long long>(amount))
                    L2_LOG_WARN(
                        Log,
                        u"[inventory] Batch addition check rejected for creature index %d: nonstackable template %d has "
                        u"removal delta %lld; expected -1.",
                        m_nCreatureIndex, itemClassId, static_cast<long long>(amount)
                    );
                    return false;
                }
                if (item->m_data->m_Type == IT_QUESTITEM) {
                    questItemDelta = runtime::SubtractInt32Wrapping(questItemDelta, 1);
                } else if (item->IsArtifact()) {
                    artifactItemDelta = runtime::SubtractInt32Wrapping(artifactItemDelta, 1);
                } else {
                    normalItemDelta = runtime::SubtractInt32Wrapping(normalItemDelta, 1);
                }
            } else {
                const std::int64_t finalAmount = runtime::AddInt64Wrapping(item->m_data->m_nCount, amount);
                if (finalAmount < 0) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] itemclassid[%d]'s amount is invalid.[%I64d]",
                    //     L"Inventory_server.cpp",
                    //     0x5f4,
                    //     itemClassId,
                    //     finalAmount)
                    // Log.Add(
                    //     LOG_WARN,
                    //     L"[inventory] Batch addition check rejected for creature index %d: template %d has calculated
                    //     amount %lld after removal delta %lld. Result is negative.", m_nCreatureIndex, itemClassId,
                    //     static_cast<long long>(finalAmount),
                    //     static_cast<long long>(amount))
                    L2_LOG_WARN(
                        Log,
                        u"[inventory] Batch addition check rejected for creature index %d: template %d has calculated "
                        u"amount %lld after removal delta %lld; result is negative.",
                        m_nCreatureIndex, itemClassId, static_cast<long long>(finalAmount), static_cast<long long>(amount)
                    );
                    return false;
                }
                if (finalAmount > 999999999999LL) {
                    LogByInventoryOverType(OVER_COUNT, owner.get(), item, finalAmount);
                    return false;
                }
            }

            const ItemConsumeType consumeType = item->m_data->m_ItemConsumeType;
            const std::int64_t itemWeight = static_cast<std::int64_t>(item->m_const->m_nWeight);
            if (consumeType == ICT_CHARGE) {
                weightDelta = runtime::SubtractInt64Wrapping(weightDelta, itemWeight);
            } else if (consumeType != ICT_ASSET) {
                weightDelta =
                    runtime::SubtractInt64Wrapping(weightDelta, runtime::MultiplyInt64Wrapping(itemWeight, amount));
            }
            continue;
        }

        if (!stackable) {
            if (amount != 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Cannot find item. itemclassid %d, %I64d is wrong!!!",
                //     itemClassId,
                //     amount)
                // Log.Add(
                //     LOG_WARN,
                //     L"[inventory] Batch addition check rejected for creature index %d: nonstackable template %d has
                //     addition delta %lld, expected 1.", m_nCreatureIndex, itemClassId, static_cast<long long>(amount))
                L2_LOG_WARN(
                    Log,
                    u"[inventory] Batch addition check rejected for creature index %d: nonstackable template %d has "
                    u"addition delta %lld; expected 1.",
                    m_nCreatureIndex, itemClassId, static_cast<long long>(amount)
                );
                return false;
            }
            if (item->m_data->m_Type == IT_QUESTITEM) {
                questItemDelta = runtime::AddInt32Wrapping(questItemDelta, 1);
            } else if (item->IsArtifact()) {
                artifactItemDelta = runtime::AddInt32Wrapping(artifactItemDelta, 1);
            } else {
                normalItemDelta = runtime::AddInt32Wrapping(normalItemDelta, 1);
            }
        } else {
            const std::int64_t finalAmount = runtime::AddInt64Wrapping(item->m_data->m_nCount, amount);
            if (finalAmount < 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]itemclassid[%d]'s amount is invalid.[%I64d]",
                //     L"Inventory_server.cpp",
                //     0x5bf,
                //     itemClassId,
                //     finalAmount)
                // Log.Add(
                //     LOG_WARN,
                //     L"[inventory] Batch addition check rejected for creature index %d: template %d has calculated amount
                //     %lld after addition delta %lld, result is negative.", m_nCreatureIndex, itemClassId, static_cast<long
                //     long>(finalAmount), static_cast<long long>(amount))
                L2_LOG_WARN(
                    Log,
                    u"[inventory] Batch addition check rejected for creature index %d: template %d has calculated amount "
                    u"%lld after addition delta %lld; result is negative.",
                    m_nCreatureIndex, itemClassId, static_cast<long long>(finalAmount), static_cast<long long>(amount)
                );
                return false;
            }
            if (finalAmount > 999999999999LL) {
                LogByInventoryOverType(OVER_COUNT, owner.get(), item, finalAmount);
                return false;
            }
        }

        const ItemConsumeType consumeType = item->m_data->m_ItemConsumeType;
        const std::int64_t itemWeight = static_cast<std::int64_t>(item->m_const->m_nWeight);
        if (consumeType == ICT_CHARGE) {
            weightDelta = runtime::AddInt64Wrapping(weightDelta, itemWeight);
        } else if (consumeType != ICT_ASSET) {
            weightDelta = runtime::AddInt64Wrapping(weightDelta, runtime::MultiplyInt64Wrapping(itemWeight, amount));
        }
    }

    // The current body applies slot/weight limits only to a PC whose shared
    // inventory index has reached the initialized (-1 or greater) state.
    if (owner->m_data->m_nInventoryIndex < -1 || !owner->IsPC()) {
        return true;
    }

    const std::int32_t finalNormalCount = runtime::AddInt32Wrapping(owner->m_data->m_nNumInventory, normalItemDelta);
    if (finalNormalCount < 0) {
        owner->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::WEIGHT_LIMIT_EXCEEDED);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Error. checka addable item count result is minus. [%d]",
        //     "Inventory_server.cpp",
        //     0x630,
        //     owner->CastUser()->GetDBID())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Batch addition check rejected for character DB ID %u: calculated normal item count %d is
        //     negative.", owner->CastUser()->GetDBID(), finalNormalCount)
        L2_LOG_ERROR(
            Log,
            u"[inventory] Batch addition check rejected for character DB ID %u: calculated normal item count %d is "
            u"negative.",
            owner->CastUser()->GetDBID(), finalNormalCount
        );
        return false;
    }

    const std::int32_t finalArtifactCount =
        runtime::AddInt32Wrapping(owner->m_data->m_nCurrentArtifactInventoryNum, artifactItemDelta);
    if (finalArtifactCount < 0) {
        owner->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::WEIGHT_LIMIT_EXCEEDED);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Error. checka addable artifact item count result is minus. [%d]",
        //     "Inventory_server.cpp",
        //     0x636,
        //     owner->CastUser()->GetDBID())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Batch addition check rejected for character DB ID %u: calculated artifact item count %d is
        //     negative.", owner->CastUser()->GetDBID(), finalArtifactCount)
        L2_LOG_ERROR(
            Log,
            u"[inventory] Batch addition check rejected for character DB ID %u: calculated artifact item count %d is "
            u"negative.",
            owner->CastUser()->GetDBID(), finalArtifactCount
        );
        return false;
    }

    const bool normalCapacityOkay = finalNormalCount <= owner->CastUser()->GetInventoryLimit();
    const bool artifactCapacityOkay = finalArtifactCount <= owner->CastUser()->GetArtifactInventoryLimit();
    if (!normalCapacityOkay) {
        if (artifactCapacityOkay) {
            owner->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::NOT_ENOUGH_INVENTORY_SPACE);
        } else {
            owner->CastUser()->GetSocket()->SendSystemMessage(
                SystemMessageId::NOT_ENOUGH_NORMAL_AND_ARTIFACT_INVENTORY_SPACE
            );
        }
        return false;
    }
    if (!artifactCapacityOkay) {
        owner->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ARTIFACT_INVENTORY_SPACE);
        return false;
    }

    const std::int32_t finalQuestCount =
        runtime::AddInt32Wrapping(owner->m_data->m_nCurrentQuestInventoryNum, questItemDelta);
    if (finalQuestCount > owner->CastUser()->GetQuestInventoryLimit()) {
        owner->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
        return false;
    }

    const std::int64_t finalWeight = runtime::AddInt64Wrapping(static_cast<std::int64_t>(m_nCarryingWeight), weightDelta);
    if (finalWeight < 0) {
        owner->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::WEIGHT_LIMIT_EXCEEDED);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Error. checka addable weight result is minus. [%d]",
        //     "Inventory_server.cpp",
        //     0x659,
        //     owner->CastUser()->GetDBID())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Batch addition check rejected for character DB ID %u: calculated weight %lld is negative.",
        //     owner->CastUser()->GetDBID(),
        //     static_cast<long long>(finalWeight))
        L2_LOG_ERROR(
            Log, u"[inventory] Batch addition check rejected for character DB ID %u: calculated weight %lld is negative.",
            owner->CastUser()->GetDBID(), static_cast<long long>(finalWeight)
        );
        return false;
    }
    if (static_cast<double>(finalWeight) > owner->CarryWeight()) {
        owner->CastUser()->GetSocket()->SendSystemMessage(SystemMessageId::WEIGHT_LIMIT_EXCEEDED);
        return false;
    }

    return true;
}

bool CInventory::GetItemListByClassID(
    ItemPointerList& result, const int itemClassId, const int count, const int enchanted, const bool includeVariation
) {
    L2_GUARD_FUNCTION();

    CItemFactory& items = CItemFactory::Inst();
    int currentIndex = GetFirstIndex();
    int depth = 0;
    while (currentIndex != -1) {
        CSPointer<CItem> item = items.FindObjectSP(currentIndex);
        if (item && item->m_data->m_nClassID == itemClassId &&
            (enchanted < 0 || item->m_data->GetEnchanted() == enchanted) &&
            (includeVariation || item->m_itemOption.itemOptionKey.variationOption1 < 1 ||
                item->m_itemOption.itemOptionKey.variationOption2 < 1)) {
            result.push_back(item);
        }

        if (static_cast<int>(result.size()) == count) {
            return true;
        }
        currentIndex = items.GetNextIndex(currentIndex);
        ++depth;
        if (depth >= kMaximumTraversalDepth) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth > %d at file[%s], line[%d]",
            //     0x192,
            //     "Inventory_server.cpp",
            //     0x437)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Item selection for template %d reached the %d-link limit for creature index %d. Returning
            //     failure with %zu items retained in the output list (requested %d).", itemClassId, kMaximumTraversalDepth,
            //     m_nCreatureIndex,
            //     result.size(),
            //     count)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Item selection for template %d reached the %d-link limit for creature index %d; returning "
                u"failure with %zu items retained in the output list (requested %d).",
                itemClassId, kMaximumTraversalDepth, m_nCreatureIndex, result.size(), count
            );
            break;
        }
    }
    return false;
}

bool CInventory::IsValidManipulation(CItem* const item, const std::int64_t amount) {
    // Current L2Server 0x007c6820, original owner Inventory_server.cpp.
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return false;
    }

    std::pair<int, std::int64_t> simulated{};
    if (!item->SimulateWithInventory(*this, simulated, amount)) {
        return false;
    }

    CSPointer<CCreature> owner = CCreatureFactory::Inst().FindObjectSP(m_nCreatureIndex);
    if (!owner) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]Creature is invalid.",
        //     L"Inventory_server.cpp",
        //     0x8b5)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Cannot validate single-item change by %lld: creature index %d was not found.",
        //     static_cast<long long>(amount),
        //     m_nCreatureIndex)
        L2_LOG_ERROR(
            Log, u"[inventory] Cannot validate single-item change by %lld: creature index %d was not found.",
            static_cast<long long>(amount), m_nCreatureIndex
        );
        return false;
    }
    if (owner->m_data->m_nInventoryIndex == -1 || !owner->IsPC()) {
        return true;
    }

    const bool adding = amount > 0;
    const int countDelta = simulated.first;
    if (item->m_data->m_Type == IT_QUESTITEM) {
        const int resultingCount = runtime::AddInt32Wrapping(owner->m_data->m_nCurrentQuestInventoryNum, countDelta);
        if (resultingCount < 0) {
            owner->SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Item count result is minus. [%d]",
            //     L"Inventory_server.cpp",
            //     0x8c2,
            //     owner->GetUserOrMaster()->GetDBID())
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Single-item change rejected for character DB ID %u: calculated quest item count %d is
            //     negative (slot delta %d, amount delta %lld).", owner->GetUserOrMaster()->GetDBID(), resultingCount,
            //     countDelta,
            //     static_cast<long long>(amount))
            L2_LOG_ERROR(
                Log,
                u"[inventory] Single-item change rejected for character DB ID %u: calculated quest item count %d is "
                u"negative (slot delta %d, amount delta %lld).",
                owner->GetUserOrMaster()->GetDBID(), resultingCount, countDelta, static_cast<long long>(amount)
            );
            return false;
        }
        if (resultingCount > owner->GetQuestInventoryLimit()) {
            if (adding) {
                owner->SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
                return false;
            }
            return true;
        }
    } else if (!item->IsArtifact()) {
        const int resultingCount = runtime::AddInt32Wrapping(owner->m_data->m_nNumInventory, countDelta);
        if (resultingCount < 0) {
            owner->SendSystemMessage(SystemMessageId::NOT_ENOUGH_INVENTORY_SPACE);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Item count result is minus. [%d]",
            //     L"Inventory_server.cpp",
            //     0x8ed,
            //     owner->GetUserOrMaster()->GetDBID())
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Single-item change rejected for character DB ID %u: calculated normal item count %d is
            //     negative (slot delta %d, amount delta %lld).", owner->GetUserOrMaster()->GetDBID(), resultingCount,
            //     countDelta,
            //     static_cast<long long>(amount))
            L2_LOG_ERROR(
                Log,
                u"[inventory] Single-item change rejected for character DB ID %u: calculated normal item count %d is "
                u"negative (slot delta %d, amount delta %lld).",
                owner->GetUserOrMaster()->GetDBID(), resultingCount, countDelta, static_cast<long long>(amount)
            );
            return false;
        }
        if (resultingCount > owner->GetInventoryLimit()) {
            if (adding) {
                owner->SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
                return false;
            }
            return true;
        }
    } else {
        const int resultingCount = runtime::AddInt32Wrapping(owner->m_data->m_nCurrentArtifactInventoryNum, countDelta);
        if (resultingCount < 0) {
            owner->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ARTIFACT_INVENTORY_SPACE);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Artifact Item count result is minus. [%d]",
            //     L"Inventory_server.cpp",
            //     0x8d8,
            //     owner->GetUserOrMaster()->GetDBID())
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Single-item change rejected for character DB ID %u: calculated artifact item count %d is
            //     negative (slot delta %d, amount delta %lld).", owner->GetUserOrMaster()->GetDBID(), resultingCount,
            //     countDelta,
            //     static_cast<long long>(amount))
            L2_LOG_ERROR(
                Log,
                u"[inventory] Single-item change rejected for character DB ID %u: calculated artifact item count %d is "
                u"negative (slot delta %d, amount delta %lld).",
                owner->GetUserOrMaster()->GetDBID(), resultingCount, countDelta, static_cast<long long>(amount)
            );
            return false;
        }
        if (resultingCount > owner->GetArtifactInventoryLimit()) {
            if (adding) {
                owner->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ARTIFACT_INVENTORY_SPACE);
                return false;
            }
            return true;
        }
    }

    const std::int64_t resultingWeight =
        runtime::AddInt64Wrapping(static_cast<std::int64_t>(m_nCarryingWeight), simulated.second);
    if (resultingWeight < 0) {
        owner->SendSystemMessage(SystemMessageId::WEIGHT_LIMIT_EXCEEDED);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] weight result is minus. [%d]",
        //     L"Inventory_server.cpp",
        //     0x903,
        //     owner->GetUserOrMaster()->GetDBID())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Single-item change rejected for character DB ID %u: calculated weight %lld is negative (amount
        //     delta %lld).", owner->GetUserOrMaster()->GetDBID(), static_cast<long long>(resultingWeight), static_cast<long
        //     long>(amount))
        L2_LOG_ERROR(
            Log,
            u"[inventory] Single-item change rejected for character DB ID %u: calculated weight %lld is negative (amount "
            u"delta %lld).",
            owner->GetUserOrMaster()->GetDBID(), static_cast<long long>(resultingWeight), static_cast<long long>(amount)
        );
        return false;
    }
    if (static_cast<double>(resultingWeight) > owner->CarryWeight() && adding) {
        owner->SendSystemMessage(SystemMessageId::WEIGHT_LIMIT_EXCEEDED);
        return false;
    }
    return true;
}

bool CInventory::IsValidManipulation(ItemManipList* manipulations) {
    // Current L2Server 0x007c57c0, original owner Inventory_server.cpp.
    L2_GUARD_FUNCTION();

    int inventoryDelta = 0;
    int questDelta = 0;
    int artifactDelta = 0;
    std::int64_t weightDelta = 0;
    bool onlyRemoving = true;

    for (const ItemManip& manipulation : manipulations->m_vOutputList) {
        std::pair<int, std::int64_t> simulated{};
        if (!manipulation.SimulateWithInventory(*this, simulated)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] SimulateWithInventory failed",
            //     "Inventory_server.cpp",
            //     0x7f7)
            // Log.Add(
            //     LOG_WARN,
            //     L"[inventory] Batch change rejected for creature index %d: simulation failed for item selector %d, amount
            //     delta %lld.", m_nCreatureIndex, manipulation.mClassId, static_cast<long long>(manipulation.mCount))
            L2_LOG_WARN(
                Log,
                u"[inventory] Batch change rejected for creature index %d: simulation failed for item selector %d, amount "
                u"delta %lld.",
                m_nCreatureIndex, manipulation.mClassId, static_cast<long long>(manipulation.mCount)
            );
            return false;
        }

        if (manipulation.mCount > 0) {
            onlyRemoving = false;
        }

        CObject* const object = g_ObjectDB.GetObjectW(manipulation.mClassId, false);
        CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
        if (item != nullptr && item->m_data->m_Type == IT_QUESTITEM) {
            questDelta = runtime::AddInt32Wrapping(questDelta, simulated.first);
        } else if (item != nullptr && item->IsArtifact()) {
            artifactDelta = runtime::AddInt32Wrapping(artifactDelta, simulated.first);
        } else {
            inventoryDelta = runtime::AddInt32Wrapping(inventoryDelta, simulated.first);
            weightDelta = runtime::AddInt64Wrapping(weightDelta, simulated.second);
        }
    }

    CSPointer<CCreature> owner = CCreatureFactory::Inst().FindObjectSP(m_nCreatureIndex);
    if (!owner) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]Creature is invalid.",
        //     L"Inventory_server.cpp",
        //     0x821)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Cannot validate batch change: creature index %d was not found.",
        //     m_nCreatureIndex)
        L2_LOG_ERROR(Log, u"[inventory] Cannot validate batch change: creature index %d was not found.", m_nCreatureIndex);
        return false;
    }
    if (owner->m_data->m_nInventoryIndex == -1 || !owner->IsPC()) {
        return true;
    }

    const int inventoryCount = runtime::AddInt32Wrapping(owner->m_data->m_nNumInventory, inventoryDelta);
    const int questCount = runtime::AddInt32Wrapping(owner->m_data->m_nCurrentQuestInventoryNum, questDelta);
    const int artifactCount = runtime::AddInt32Wrapping(owner->m_data->m_nCurrentArtifactInventoryNum, artifactDelta);

    if (inventoryCount < 0) {
        owner->SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item count result is minus. [%d]",
        //     L"Inventory_server.cpp",
        //     0x82d,
        //     owner->GetUserOrMaster()->GetDBID())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Batch change rejected for character DB ID %u: calculated normal item count %d is negative (slot
        //     delta %d).", owner->GetUserOrMaster()->GetDBID(), inventoryCount, inventoryDelta)
        L2_LOG_ERROR(
            Log,
            u"[inventory] Batch change rejected for character DB ID %u: calculated normal item count %d is negative (slot "
            u"delta %d).",
            owner->GetUserOrMaster()->GetDBID(), inventoryCount, inventoryDelta
        );
        return false;
    }
    if (questCount < 0) {
        owner->SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item count result is minus. [%d]",
        //     L"Inventory_server.cpp",
        //     0x835,
        //     owner->GetUserOrMaster()->GetDBID())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Batch change rejected for character DB ID %u: calculated quest item count %d is negative (slot
        //     delta %d).", owner->GetUserOrMaster()->GetDBID(), questCount, questDelta)
        L2_LOG_ERROR(
            Log,
            u"[inventory] Batch change rejected for character DB ID %u: calculated quest item count %d is negative (slot "
            u"delta %d).",
            owner->GetUserOrMaster()->GetDBID(), questCount, questDelta
        );
        return false;
    }
    if (artifactCount < 0) {
        owner->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ARTIFACT_INVENTORY_SPACE);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Artifact Item count result is minus. [%d]",
        //     L"Inventory_server.cpp",
        //     0x83e,
        //     owner->GetUserOrMaster()->GetDBID())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Batch change rejected for character DB ID %u: calculated artifact item count %d is negative
        //     (slot delta %d).", owner->GetUserOrMaster()->GetDBID(), artifactCount, artifactDelta)
        L2_LOG_ERROR(
            Log,
            u"[inventory] Batch change rejected for character DB ID %u: calculated artifact item count %d is negative (slot "
            u"delta %d).",
            owner->GetUserOrMaster()->GetDBID(), artifactCount, artifactDelta
        );
        return false;
    }

    const int inventoryLimit = owner->GetInventoryLimit();
    const int artifactLimit = owner->GetArtifactInventoryLimit();
    if (inventoryCount > inventoryLimit) {
        if (artifactCount > artifactLimit) {
            if (onlyRemoving) {
                return true;
            }
            owner->SendSystemMessage(SystemMessageId::NOT_ENOUGH_NORMAL_AND_ARTIFACT_INVENTORY_SPACE);
            const int loggedArtifactLimit = owner->GetArtifactInventoryLimit();
            const int loggedInventoryLimit = owner->GetInventoryLimit();
            const char16_t* const ownerName = owner->GetName();
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Inventory number exceedes max inventory number. User [%s] Invetory num [%d/%d] Artifact
            //     Inventory num [%d/%d]", L"Inventory_server.cpp", 0x84e, ownerName, inventoryCount, loggedInventoryLimit,
            //     artifactCount,
            //     loggedArtifactLimit)
            // Log.Add(
            //     LOG_WARN,
            //     L"[inventory] Batch change rejected for %ls: projected normal slots %d exceed checked limit %d and
            //     artifact slots %d exceed checked limit %d. Latest limits: normal %d, artifact %d.", ownerName,
            //     inventoryCount,
            //     inventoryLimit,
            //     artifactCount,
            //     artifactLimit,
            //     loggedInventoryLimit,
            //     loggedArtifactLimit)
            // L2_LOG_WARN(Log,
            //     L"[inventory] Batch change rejected for %ls: projected normal slots %d exceed checked limit %d and
            //     artifact slots %d exceed checked limit %d. Latest limits: normal %d, artifact %d.", ownerName,
            //     inventoryCount,
            //     inventoryLimit,
            //     artifactCount,
            //     artifactLimit,
            //     loggedInventoryLimit,
            //     loggedArtifactLimit)
            L2_LOG_WARN(
                Log,
                u"[inventory] Batch change rejected for %ls: projected normal slots %d exceed checked limit %d and artifact "
                u"slots %d exceed checked limit %d. Latest limits: normal %d, artifact %d.",
                ownerName, inventoryCount, inventoryLimit, artifactCount, artifactLimit, loggedInventoryLimit,
                loggedArtifactLimit
            );
            return false;
        }
        if (onlyRemoving) {
            return true;
        }
        owner->SendSystemMessage(SystemMessageId::NOT_ENOUGH_INVENTORY_SPACE);
        const int loggedInventoryLimit = owner->GetInventoryLimit();
        const char16_t* const ownerName = owner->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Inventory number exceedes max inventory number. User [%s] Invetory num [%d/%d]",
        //     L"Inventory_server.cpp",
        //     0x85e,
        //     ownerName,
        //     inventoryCount,
        //     loggedInventoryLimit)
        // Log.Add(
        //     LOG_WARN,
        //     L"[inventory] Batch change rejected for %ls: projected normal slots %d exceed checked limit %d (latest limit
        //     %d).", ownerName, inventoryCount, inventoryLimit, loggedInventoryLimit)
        // L2_LOG_WARN(Log,
        //     L"[inventory] Batch change rejected for %ls: projected normal slots %d exceed checked limit %d (latest limit
        //     %d).", ownerName, inventoryCount, inventoryLimit, loggedInventoryLimit)
        L2_LOG_WARN(
            Log,
            u"[inventory] Batch change rejected for %ls: projected normal slots %d exceed checked limit %d (latest limit "
            u"%d).",
            ownerName, inventoryCount, inventoryLimit, loggedInventoryLimit
        );
        return false;
    }
    if (artifactCount > artifactLimit) {
        if (onlyRemoving) {
            return true;
        }
        owner->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ARTIFACT_INVENTORY_SPACE);
        const int loggedArtifactLimit = owner->GetArtifactInventoryLimit();
        const char16_t* const ownerName = owner->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Inventory number exceedes max inventory number. User [%s] Artifact Invetory num [%d/%d]",
        //     L"Inventory_server.cpp",
        //     0x86b,
        //     ownerName,
        //     artifactCount,
        //     loggedArtifactLimit)
        // Log.Add(
        //     LOG_WARN,
        //     L"[inventory] Batch change rejected for %ls: projected artifact slots %d exceed checked limit %d (latest limit
        //     %d).", ownerName, artifactCount, artifactLimit, loggedArtifactLimit)
        // L2_LOG_WARN(Log,
        //     L"[inventory] Batch change rejected for %ls: projected artifact slots %d exceed checked limit %d (latest limit
        //     %d).", ownerName, artifactCount, artifactLimit, loggedArtifactLimit)
        L2_LOG_WARN(
            Log,
            u"[inventory] Batch change rejected for %ls: projected artifact slots %d exceed checked limit %d (latest limit "
            u"%d).",
            ownerName, artifactCount, artifactLimit, loggedArtifactLimit
        );
        return false;
    }

    const int questLimit = owner->GetQuestInventoryLimit();
    if (questCount > questLimit) {
        if (onlyRemoving) {
            return true;
        }
        owner->SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
        const int loggedQuestLimit = owner->GetQuestInventoryLimit();
        const char16_t* const ownerName = owner->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Quest inventory number exceedes max inventory number. User [%s] Inventory num [%d/%d]",
        //     L"Inventory_server.cpp",
        //     0x87e,
        //     ownerName,
        //     questCount,
        //     loggedQuestLimit)
        // Log.Add(
        //     LOG_WARN,
        //     L"[inventory] Batch change rejected for %ls: projected quest slots %d exceed checked limit %d (latest limit
        //     %d).", ownerName, questCount, questLimit, loggedQuestLimit)
        // L2_LOG_WARN(Log,
        //     L"[inventory] Batch change rejected for %ls: projected quest slots %d exceed checked limit %d (latest limit
        //     %d).", ownerName, questCount, questLimit, loggedQuestLimit)
        L2_LOG_WARN(
            Log,
            u"[inventory] Batch change rejected for %ls: projected quest slots %d exceed checked limit %d (latest limit "
            u"%d).",
            ownerName, questCount, questLimit, loggedQuestLimit
        );
        return false;
    }

    const std::int64_t resultingWeight =
        runtime::AddInt64Wrapping(static_cast<std::int64_t>(m_nCarryingWeight), weightDelta);
    if (resultingWeight < 0) {
        owner->SendSystemMessage(SystemMessageId::WEIGHT_LIMIT_EXCEEDED);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] weight result is minus. [%d]",
        //     L"Inventory_server.cpp",
        //     0x888,
        //     owner->GetUserOrMaster()->GetDBID())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Batch change rejected for character DB ID %u: calculated weight %lld is negative (weight delta
        //     %lld).", owner->GetUserOrMaster()->GetDBID(), static_cast<long long>(resultingWeight), static_cast<long
        //     long>(weightDelta))
        L2_LOG_ERROR(
            Log,
            u"[inventory] Batch change rejected for character DB ID %u: calculated weight %lld is negative (weight delta "
            u"%lld).",
            owner->GetUserOrMaster()->GetDBID(), static_cast<long long>(resultingWeight), static_cast<long long>(weightDelta)
        );
        return false;
    }

    const double carryWeight = owner->CarryWeight();
    if (static_cast<double>(resultingWeight) > carryWeight && !onlyRemoving) {
        owner->SendSystemMessage(SystemMessageId::WEIGHT_LIMIT_EXCEEDED);
        const double loggedCarryWeight = owner->CarryWeight();
        // Noise: reinterpreting capacity bits as an integer produced a meaningless weight limit.
        // std::uint64_t loggedCarryWeightBits = 0
        // std::memcpy(
        //     &loggedCarryWeightBits,
        //     &loggedCarryWeight,
        //     sizeof(loggedCarryWeightBits))
        const char16_t* const ownerName = owner->GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Inventory weight exceedes max inventory weight. User [%s] Weight[%d/%d]",
        //     L"Inventory_server.cpp",
        //     0x897,
        //     ownerName,
        //     runtime::Int32FromBits(
        //         static_cast<std::uint32_t>(resultingWeight)),
        //     runtime::Int32FromBits(
        //         static_cast<std::uint32_t>(loggedCarryWeightBits)))
        // Log.Add(
        //     LOG_WARN,
        //     L"[inventory] Batch change rejected for %ls: projected weight %lld exceeds checked limit %.17g (latest limit
        //     %.17g).", ownerName, static_cast<long long>(resultingWeight), carryWeight, loggedCarryWeight)
        // L2_LOG_WARN(Log,
        //     L"[inventory] Batch change rejected for %ls: projected weight %lld exceeds checked limit %.17g (latest limit
        //     %.17g).", ownerName, static_cast<long long>(resultingWeight), carryWeight, loggedCarryWeight)
        L2_LOG_WARN(
            Log,
            u"[inventory] Batch change rejected for %ls: projected weight %lld exceeds checked limit %.17g (latest limit "
            u"%.17g).",
            ownerName, static_cast<long long>(resultingWeight), carryWeight, loggedCarryWeight
        );
        return false;
    }
    return true;
}

bool CInventory::Push(int itemIndex) {
    L2_GUARD_FUNCTION();
    L2_WRITE_GUARD(*this);

    CSPointer<CObject> itemObject = FindItemObjectSP(itemIndex);
    if (!itemObject) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Push failed. because "
        //     "CItemFactory::Inst().FindObjectSP failed.",
        //     "Inventory_server.cpp",
        //     0x122)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Cannot insert item index %d into inventory of creature index %d: item object was not found.",
        //     itemIndex,
        //     m_nCreatureIndex)
        L2_LOG_ERROR(
            Log, u"[inventory] Cannot insert item index %d into inventory of creature index %d: item object was not found.",
            itemIndex, m_nCreatureIndex
        );
        return false;
    }

    CSPointer<CObject> ownerObject = FindCreatureObjectSP(m_nCreatureIndex);
    if (!ownerObject) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Push failed. because "
        //     "CCreatureFactory::Inst().FindObjectSP failed.",
        //     "Inventory_server.cpp",
        //     0x129)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Cannot insert item index %d: creature index %d was not found.",
        //     itemIndex,
        //     m_nCreatureIndex)
        L2_LOG_ERROR(
            Log, u"[inventory] Cannot insert item index %d: creature index %d was not found.", itemIndex, m_nCreatureIndex
        );
        return false;
    }

    CItem* const item = itemObject->CastItem();
    CSharedItemData* const itemData = item->m_data;
    if (itemData->next != -1 || itemData->before != -1) {
        return false;
    }

    CCreature* const ownerCreature = static_cast<CCreature*>(ownerObject.get());
    CSharedCreatureData* const ownerData = ownerCreature->m_data;
    const std::int32_t oldHead = ownerData->m_nInventoryIndex;
    if (oldHead != -1) {
        itemData->next = oldHead;
        itemData->before = -1;

        CSPointer<CObject> oldHeadObject = FindItemObjectSP(oldHead);
        if (!oldHeadObject) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] Push failed. because "
            //     "CItemFactory::Inst()e.FindObjectSP failed.",
            //     "Inventory_server.cpp",
            //     0x13b)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Cannot finish inserting item index %d for creature index %d: previous head %d was not found.
            //     New item links are already assigned. Inventory head has not been changed.", itemIndex, m_nCreatureIndex,
            //     oldHead)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Cannot finish inserting item index %d for creature index %d: previous head %d was not found. "
                u"New item links are already assigned; inventory head has not been changed.",
                itemIndex, m_nCreatureIndex, oldHead
            );
            return false;
        }
        CItem* const oldHeadItem = static_cast<CItem*>(oldHeadObject.get());
        oldHeadItem->m_data->before = itemIndex;
    }

    ownerData->m_nInventoryIndex = itemIndex;
    if (!item->IsQuestItem()) {
        AddSlotInfo(itemData->m_nInventorySlotIndex, item);
    }

    if (static_cast<std::int32_t>(itemData->m_Type) == 3) {
        runtime::raw_atomic::IncrementI32(&ownerData->m_nCurrentQuestInventoryNum);
    } else if (!item->IsArtifact()) {
        runtime::raw_atomic::IncrementI32(&ownerData->m_nNumInventory);
    } else {
        runtime::raw_atomic::IncrementI32(&ownerData->m_nCurrentArtifactInventoryNum);
    }

    const std::uint32_t ownerSid = ownerObject->m_nId;
    std::memcpy(&item->m_ItemOwnerId, &ownerSid, sizeof(item->m_ItemOwnerId));
    return true;
}

bool CInventory::Pop(int itemIndex, bool skipShortcutUpdate) {
    L2_GUARD_FUNCTION();
    if (!Have(itemIndex)) {
        return false;
    }

    L2_WRITE_GUARD(*this);
    CSPointer<CCreature> owner = CCreatureFactory::Inst().FindObjectSP(m_nCreatureIndex);
    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
    if (!owner || !item) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid inventory pop",
        //     L"Inventory_server.cpp",
        //     0x16d)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[inventory] Cannot remove item index %d from inventory of creature index %d: creature %ls, item %ls. No
        //     unlink performed.", itemIndex, m_nCreatureIndex, owner ? L"available" : L"unavailable", item ? L"available" :
        //     L"unavailable")
        L2_LOG_ERROR(
            Log,
            u"[inventory] Cannot remove item index %d from inventory of creature index %d: creature %ls, item %ls; no "
            u"unlink performed.",
            itemIndex, m_nCreatureIndex, owner ? L"available" : L"unavailable", item ? L"available" : L"unavailable"
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        return false;
    }

    CSharedItemData* const itemData = item->m_data;
    CItemFactory& itemFactory = CItemFactory::Inst();
    if (itemData->before == -1) {
        owner->m_data->m_nInventoryIndex = itemData->next;
    } else {
        CSharedItemData* const previous = itemFactory.GetData(itemData->before);
        previous->next = itemData->next;
    }
    if (itemData->next >= 0) {
        CSharedItemData* const next = itemFactory.GetData(itemData->next);
        next->before = itemData->before;
    }
    itemData->next = -1;
    itemData->before = -1;

    if (owner->IsPC() && !skipShortcutUpdate) {
        User* const user = owner->CastUser();
        user->m_ShortCut.DeleteItemShortCut(static_cast<int>(item->GetDBID()));
    }

    if (!item->IsQuestItem() && itemData->m_nInventorySlotIndex != -1) {
        RemoveSlotInfo(item.get());
    }

    if (itemData->m_Type == IT_QUESTITEM) {
        runtime::raw_atomic::DecrementI32(&owner->m_data->m_nCurrentQuestInventoryNum);
    } else if (!item->IsArtifact()) {
        runtime::raw_atomic::DecrementI32(&owner->m_data->m_nNumInventory);
    } else {
        runtime::raw_atomic::DecrementI32(&owner->m_data->m_nCurrentArtifactInventoryNum);
    }

    item->m_ItemOwnerId = 0;
    return true;
}

std::int64_t CInventory::GetAdenaAmount() {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    CItemFactory& items = CItemFactory::Inst();
    CSPointer<CObject> item;
    CSharedItemData* data = GetFirst();
    int depth = 0;
    while (data) {
        {
            CSPointer<CObject> current = FindItemObjectSP(data->index);
            item = current;
        }

        if (item && item->m_nClassId == 0x39) {
            CItem* const typedItem = static_cast<CItem*>(item.get());
            const CSharedItemData* const itemData = typedItem->m_data;
            return itemData->m_nCount;
        }

        data = items.GetNext(data->index);
        ++depth;
        if (depth >= kMaximumTraversalDepth) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth> %d at file[%s], line[%d]",
            //     0x192,
            //     "Inventory_server.cpp",
            //     0x1bc)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Adena amount search reached the %d-link limit for creature index %d. Returning zero.",
            //     kMaximumTraversalDepth,
            //     m_nCreatureIndex)
            L2_LOG_ERROR(
                Log, u"[inventory] Adena amount search reached the %d-link limit for creature index %d; returning zero.",
                kMaximumTraversalDepth, m_nCreatureIndex
            );
            break;
        }
    }
    return 0;
}

int CInventory::GetAdenaDBID() {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    CItemFactory& items = CItemFactory::Inst();
    CSPointer<CObject> item;
    CSharedItemData* data = GetFirst();
    int depth = 0;
    while (data) {
        {
            CSPointer<CObject> current = FindItemObjectSP(data->index);
            item = current;
        }

        if (item && item->m_nClassId == 0x39) {
            CItem* const typedItem = static_cast<CItem*>(item.get());
            const CSharedItemData* const itemData = typedItem->m_data;
            return itemData->m_nDBID;
        }

        data = items.GetNext(data->index);
        ++depth;
        if (depth >= kMaximumTraversalDepth) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth> %d at file[%s], line[%d]",
            //     0x192,
            //     "Inventory_server.cpp",
            //     0x1df)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Adena DB ID search reached the %d-link limit for creature index %d. Returning zero.",
            //     kMaximumTraversalDepth,
            //     m_nCreatureIndex)
            L2_LOG_ERROR(
                Log, u"[inventory] Adena DB ID search reached the %d-link limit for creature index %d; returning zero.",
                kMaximumTraversalDepth, m_nCreatureIndex
            );
            break;
        }
    }
    return 0;
}

bool CInventory::HaveFakeItemBySId(std::int64_t count) {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    CItemFactory& items = CItemFactory::Inst();
    int currentIndex = GetFirstIndex();
    int depth = 0;
    while (currentIndex != -1) {
        bool depthExceeded = false;
        {
            CSPointer<CObject> item = FindItemObjectSP(currentIndex);
            if (item) {
                CItem* const typedItem = static_cast<CItem*>(item.get());
                const CSharedItemData* const itemData = typedItem->m_data;
                const std::uint32_t sid = UnsignedSmartId(itemData->m_nId);
                // if ((sid & 0xf8000000U) == 0x30000000U &&
                if ((sid & CLASS_MASK) == (SCLS_FAKE_ITEM << CLASS_SHIFT) &&
                    (typedItem->m_itemOption.itemOptionKey.variationOption1 < 1 ||
                        typedItem->m_itemOption.itemOptionKey.variationOption2 < 1)) {
                    count -= runtime::Int32FromBits(static_cast<std::uint32_t>(itemData->m_nCount));
                }
            }

            currentIndex = items.GetNextIndex(currentIndex);
            ++depth;
            if (depth >= kMaximumTraversalDepth) {
                // Log.Add(
                //     LOG_ERROR,
                //     "depth > %d at file[%s], line[%d]",
                //     0x192,
                //     "Inventory_server.cpp",
                //     0x6be)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[inventory] Fake-item quantity check reached the %d-link limit for creature index %d. Remaining
                //     requested amount %lld, returning %ls.", kMaximumTraversalDepth, m_nCreatureIndex, static_cast<long
                //     long>(count), count < 1 ? L"satisfied" : L"not satisfied")
                L2_LOG_ERROR(
                    Log,
                    u"[inventory] Fake-item quantity check reached the %d-link limit for creature index %d; remaining "
                    u"requested amount %lld, returning %ls.",
                    kMaximumTraversalDepth, m_nCreatureIndex, static_cast<long long>(count),
                    count < 1 ? L"satisfied" : L"not satisfied"
                );
                depthExceeded = true;
            }
        }

        if (depthExceeded || count < 1) {
            break;
        }
    }
    return count < 1;
}

int CInventory::GetTotalCount() {
    L2_GUARD_FUNCTION();

    CSPointer<CObject> owner = FindCreatureObjectSP(m_nCreatureIndex);
    if (!owner) {
        return -1;
    }

    const CCreature* const ownerCreature = static_cast<const CCreature*>(owner.get());
    return ownerCreature->m_data->m_nNumInventory;
}

int CInventory::GetArtifactCount() {
    L2_GUARD_FUNCTION();

    CSPointer<CObject> owner = FindCreatureObjectSP(m_nCreatureIndex);
    if (!owner) {
        return -1;
    }

    const CCreature* const ownerCreature = static_cast<const CCreature*>(owner.get());
    return ownerCreature->m_data->m_nCurrentArtifactInventoryNum;
}

int CInventory::GetInventoryCount() {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    CItemFactory& items = CItemFactory::Inst();
    int countableDepth = 0;
    int count = 0;
    CSharedItemData* data = GetFirst();
    while (data) {
        const std::int32_t type = static_cast<std::int32_t>(data->m_Type);
        if (type >= 0 && type != 3) {
            ++countableDepth;
        }
        if (countableDepth >= kMaximumTraversalDepth) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth > %d at file[%s], line[%d]",
            //     0x192,
            //     "Inventory_server.cpp",
            //     0xa40)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Inventory normal item count reached the limit of %d nonquest records for creature index %d.
            //     returning accumulated count %d without processing item index %d.", kMaximumTraversalDepth,
            //     m_nCreatureIndex,
            //     count,
            //     data->index)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Inventory normal item count reached the limit of %d nonquest records for creature index %d; "
                u"returning accumulated count %d without processing item index %d.",
                kMaximumTraversalDepth, m_nCreatureIndex, count, data->index
            );
            break;
        }

        CSPointer<CObject> item = FindItemObjectSP(data->index);
        if (!item) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] FindObject Failed",
            //     "Inventory_server.cpp",
            //     0xa47)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Inventory normal item count for creature index %d cannot resolve item index %d. Skipping
            //     this record and continuing the count.", m_nCreatureIndex, data->index)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Inventory normal item count for creature index %d cannot resolve item index %d; skipping this "
                u"record and continuing the count.",
                m_nCreatureIndex, data->index
            );
        } else {
            CItem* const typedItem = static_cast<CItem*>(item.get());
            const CSharedItemData* const currentData = typedItem->m_data;
            const std::int32_t currentType = static_cast<std::int32_t>(currentData->m_Type);
            if (currentType >= 0 && currentType != 3 && !item->IsArtifact()) {
                ++count;
            }
        }

        data = items.GetNext(data->index);
    }
    return count;
}

int CInventory::GetArtifactInventoryCount() {
    L2_GUARD_FUNCTION();
    L2_READ_GUARD(*this);

    CItemFactory& items = CItemFactory::Inst();
    int countableDepth = 0;
    int count = 0;
    CSharedItemData* data = GetFirst();
    while (data) {
        const std::int32_t type = static_cast<std::int32_t>(data->m_Type);
        if (type >= 0 && type != 3) {
            ++countableDepth;
        }
        if (countableDepth >= kMaximumTraversalDepth) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth > %d at file[%s], line[%d]",
            //     0x192,
            //     "Inventory_server.cpp",
            //     0xa6e)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Inventory artifact item count reached the limit of %d nonquest records for creature index
            //     %d. Returning accumulated count %d without processing item index %d.", kMaximumTraversalDepth,
            //     m_nCreatureIndex,
            //     count,
            //     data->index)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Inventory artifact item count reached the limit of %d nonquest records for creature index %d; "
                u"returning accumulated count %d without processing item index %d.",
                kMaximumTraversalDepth, m_nCreatureIndex, count, data->index
            );
            break;
        }

        CSPointer<CObject> item = FindItemObjectSP(data->index);
        if (!item) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d] FindObject Failed",
            //     "Inventory_server.cpp",
            //     0xa75)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Inventory artifact item count for creature index %d cannot resolve item index %d. Skipping
            //     this record and continuing the count.", m_nCreatureIndex, data->index)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Inventory artifact item count for creature index %d cannot resolve item index %d; skipping "
                u"this record and continuing the count.",
                m_nCreatureIndex, data->index
            );
        } else if (item->IsArtifact()) {
            ++count;
        }

        data = items.GetNext(data->index);
    }
    return count;
}

void CInventory::InitSlotInfo() {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(*this);
        {
            CItemFactory& items = CItemFactory::Inst();
            CSPointer<CObject> item;
            int currentIndex = GetFirstIndex();
            int depth = 0;
            while (currentIndex != -1) {
                {
                    CSPointer<CObject> current = FindItemObjectSP(currentIndex);
                    item = current;
                }
                if (item) {
                    CItem* const typedItem = static_cast<CItem*>(item.get());
                    CSharedItemData* const itemData = typedItem->m_data;
                    itemData->m_nInventorySlotIndex = -1;
                }

                currentIndex = items.GetNextIndex(currentIndex);
                ++depth;
                if (depth >= kMaximumTraversalDepth) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "depth > %d at file[%s], line[%d]",
                    //     0x192,
                    //     "Inventory_server.cpp",
                    //     0x9bc)
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[inventory] Slot reset reached the %d-link limit for creature index %d. Earlier item slot resets
                    //     remain applied and both slot managers will still be reset. Next index: %d.",
                    //     kMaximumTraversalDepth,
                    //     m_nCreatureIndex,
                    //     currentIndex)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Slot reset reached the %d-link limit for creature index %d; earlier item slot resets "
                        u"remain applied and both slot managers will still be reset. Next index: %d.",
                        kMaximumTraversalDepth, m_nCreatureIndex, currentIndex
                    );
                    break;
                }
            }

            m_SlotIndexManager.Init();
            m_ArtifactSlotIndexManager.Init();
        }
    }
}

int CInventory::MakeSlotIndexPacket(char* buffer, int& offset) {
    L2_GUARD_FUNCTION();

    int packetCount = 0;
    {
        L2_READ_GUARD(*this);
        {
            CItemFactory& items = CItemFactory::Inst();
            CSPointer<CObject> item;
            int currentIndex = GetFirstIndex();
            int depth = 0;
            while (currentIndex != -1) {
                {
                    CSPointer<CObject> current = FindItemObjectSP(currentIndex);
                    item = current;
                }
                if (item) {
                    CItem* const typedItem = static_cast<CItem*>(item.get());
                    const CSharedItemData* const itemData = typedItem->m_data;
                    const int assembled = Assemble(
                        buffer + offset, 0x5000, "dd", static_cast<std::int32_t>(item->GetDBID()),
                        itemData->m_nInventorySlotIndex
                    );
                    if (assembled > 0) {
                        offset += assembled;
                        ++packetCount;
                    }
                }

                currentIndex = items.GetNextIndex(currentIndex);
                ++depth;
                if (depth >= kMaximumTraversalDepth) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "depth > %d at file[%s], line[%d]",
                    //     0x192,
                    //     "Inventory_server.cpp",
                    //     0xa20)
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[inventory] Slot-index encoding reached the %d-link limit for creature index %d. Returning %d
                    //     appended records with output offset %d. Partial output is retained.", kMaximumTraversalDepth,
                    //     m_nCreatureIndex,
                    //     packetCount,
                    //     offset)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Slot-index encoding reached the %d-link limit for creature index %d; returning %d "
                        u"appended records with output offset %d. Partial output is retained.",
                        kMaximumTraversalDepth, m_nCreatureIndex, packetCount, offset
                    );
                    break;
                }
            }
        }
    }
    return packetCount;
}

void CInventory::_ReleaseMapChangedItems() {
    L2_GUARD_FUNCTION();

    for (const auto& changed : MapChangedItems) {
        if (changed.second == 0) {
            continue;
        }

        CSPointer<CObject> item = FindItemObjectSP(changed.first);
        if (item) {
            L2_RELEASE(item, ObjectRefType::InventoryChangeMap);
        }
    }
    MapChangedItems.clear();
}

void CInventory::ReleaseMapChangedItems() {
    L2_GUARD_FUNCTION();

    {
        L2_READ_GUARD(*this);
    }

    {
        L2_WRITE_GUARD(lockMapChangedItems);
        _ReleaseMapChangedItems();
    }
}

bool CInventory::SetInventoryChanged(CItem* item, InventoryUpdateType updateType) {
    L2_GUARD_FUNCTION();

    CSPointer<CObject> owner = FindCreatureObjectSP(m_nCreatureIndex);
    if (!owner) {
        return false;
    }

    {
        L2_WRITE_GUARD(lockMapChangedItems);

        CObject* const itemObject = static_cast<CObject*>(item);
        CSharedItemData* const itemData = item->m_data;
        const int itemIndex = itemData->index;
        const int update = updateType;
        auto changed = MapChangedItems.find(itemIndex);
        if (changed == MapChangedItems.end()) {
            L2_ADD_REF(itemObject, ObjectRefType::InventoryChangeMap);
            MapChangedItems.emplace(itemIndex, update);
        } else if (changed->second == IUT_ADD) {
            if (update == IUT_DELETE) {
                MapChangedItems.erase(itemIndex);
                L2_RELEASE(itemObject, ObjectRefType::InventoryChangeMap);
            }
        } else if (changed->second == IUT_UPDATE && update == IUT_DELETE) {
            changed->second = IUT_DELETE;
        }
    }
    return true;
}

bool CInventory::CreateOrAddItem(const int databaseId, const int itemClassId, const std::int64_t amount) {
    // Current L2Server 0x007c85e4, original owner Inventory_server.cpp.
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(*this);
    }

    CItem* item = GetByDBID(databaseId);
    InventoryUpdateType updateType = IUT_ADD;
    if (item != nullptr) {
        {
            L2_GUARD(*item->m_lock);
            if (!item->AddAmount(amount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid"
                //     L"[%I64d][%I64d]",
                //     "Inventory_server.cpp",
                //     0xea,
                //     item->m_data->m_nCount,
                //     amount)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[inventory] Cannot add amount %lld to item DB ID %d (requested template %d) for creature index %d:
                //     AddAmount failed, observed amount %lld.", static_cast<long long>(amount), databaseId, itemClassId,
                //     m_nCreatureIndex,
                //     static_cast<long long>(item->m_data->m_nCount))
                L2_LOG_ERROR(
                    Log,
                    u"[inventory] Cannot add amount %lld to item DB ID %d (requested template %d) for creature index %d: "
                    u"AddAmount failed; observed amount %lld.",
                    static_cast<long long>(amount), databaseId, itemClassId, m_nCreatureIndex,
                    static_cast<long long>(item->m_data->m_nCount)
                );
                return false;
            }
        }
        updateType = IUT_UPDATE;
    } else {
        item = g_ObjectDB.CreateItem(itemClassId);
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] CreateItem Failed",
            //     L"Inventory_server.cpp",
            //     0xf7)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Cannot create template %d for item DB ID %d, creature index %d: CreateItem returned no
            //     item.", itemClassId, databaseId, m_nCreatureIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Cannot create template %d for item DB ID %d, creature index %d: CreateItem returned no item.",
                itemClassId, databaseId, m_nCreatureIndex
            );
            return false;
        }

        {
            L2_GUARD(*item->m_lock);
            item->SetDBID(static_cast<std::uint32_t>(databaseId));
            if (!item->SetAmount(amount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid"
                //     L"[%I64d][%I64d]",
                //     "Inventory_server.cpp",
                //     0xff,
                //     item->m_data->m_nCount,
                //     amount)
                // Log.Add(
                //     LOG_ERROR,
                //     L"[inventory] Cannot set amount %lld on new item DB ID %d, template %d for creature index %d:
                //     SetAmount failed, observed amount %lld. Item has not been inserted.", static_cast<long long>(amount),
                //     databaseId,
                //     itemClassId,
                //     m_nCreatureIndex,
                //     static_cast<long long>(item->m_data->m_nCount))
                L2_LOG_ERROR(
                    Log,
                    u"[inventory] Cannot set amount %lld on new item DB ID %d, template %d for creature index %d: SetAmount "
                    u"failed; observed amount %lld. Item has not been inserted.",
                    static_cast<long long>(amount), databaseId, itemClassId, m_nCreatureIndex,
                    static_cast<long long>(item->m_data->m_nCount)
                );
                return false;
            }
        }

        if (!Push(item->m_data->index)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "inven push failed at file[%s], line[%d]",
            //     "Inventory_server.cpp",
            //     0x107)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[inventory] Cannot link new item DB ID %d, template %d for creature index %d. Stopping the creation path
            //     after inventory insertion failed.", databaseId, itemClassId, m_nCreatureIndex)
            L2_LOG_ERROR(
                Log,
                u"[inventory] Cannot link new item DB ID %d, template %d for creature index %d; stopping the creation path "
                u"after inventory insertion failed.",
                databaseId, itemClassId, m_nCreatureIndex
            );
            return false;
        }

        CCreature* const owner = GetOwner();
        if (owner != nullptr && owner->IsPC()) {
            User* const user = owner->CastUser();
            user->AutoEquipArrowOrBolt(item);
        }
    }

    SetInventoryChanged(item, updateType);
    return true;
}
