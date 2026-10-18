#include "Area.h"
#include "Artifact.h"
#include "BuySell.h"
#include "BuySellListManager.h"
#include "CConfig.h"
#include "CContributeData.h"
#include "CWorld.h"
#include "Castle.h"
#include "CreatureController.h"
#include "CursedWeapon.h"
#include "DB.h"
#include "Default.h"
#include "Economy.h"
#include "EnchantOption.h"
#include "EventMaker.h"
#include "Fishing.h"
#include "Fortress.h"
#include "Item.h"
#include "ItemAnnounceInfo.h"
#include "ItemAssembler.h"
#include "MonRaceData.h"
#include "MultiSell.h"
#include "NPC.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "PVPMatch.h"
#include "Party.h"
#include "Pet.h"
#include "Pledge.h"
#include "Portable/ClientProtocolRuntime.h"
#include "PremiumService.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Recipe.h"
#include "Residence.h"
#include "SSQ.h"
#include "Shared/src/CategoryDataDB.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/TimeAnalysis.h"
#include "Shared/src/UserConst.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"
#include "Skill.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserPacketEx.h"
#include "UserSocket.h"
#include "VariationDB.h"
#include "VipAttendance.h"
#include "WareHouse.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <optional>
#include <string>
#include <string_view>
#include <typeinfo>

namespace {

constexpr std::int32_t BaseCrystal[] = {0, 1, 2, 3, 4, 5, 5, 5, 8, 8, 8, 11, 12};


int DropZoneCoordinate(const double coordinate, const std::uint32_t bias) noexcept {
    const std::uint32_t bits = static_cast<std::uint32_t>(runtime::Win64CvttDoubleToInt32(coordinate)) + bias;
    std::uint32_t shifted = bits >> 15U;
    if ((bits & 0x80000000U) != 0U) {
        shifted |= ~std::uint32_t{0} << 17U;
    }
    return runtime::AddInt32Wrapping(runtime::Int32FromBits(shifted), 10);
}


int AssembleForDeleteItemInInventoryBeforeCommit(
    CItem& item, const int changeType, const std::int64_t amount, CPacketHelperEx<20480>& packet
) {
    // Current L2Server 0x00b893dc, original anonymous user_item.cpp owner.
    L2_GUARD_FUNCTION();
    CItemAssemblerChangeItemData assembler(item, changeType, amount, item.m_itemBaseAttribute, 0x2b);
    return assembler.AssembleItemInfo(packet);
}

int AssembleForBuyItemWithProperties(
    CItem& item, const std::int64_t amount, const std::int64_t totalPrice, CPacketHelperEx<20480>& packet
) {
    // Current L2Server 0x00b8fb9c, original anonymous user_item.cpp owner.
    L2_GUARD_FUNCTION();
    CItemAssemblerBuyItem assembler(item, amount, totalPrice, item.m_itemBaseAttribute, 0x7f);
    return assembler.AssembleItemInfo(packet);
}

bool RejectInvalidPendingTeleportCount(CCreature*, const ItemManip*) { return false; }

const wchar_t* PendingTeleportAccountName(
    const std::array<char16_t, 15>& accountName, std::array<wchar_t, 16>& storage
) noexcept {
#if defined(_WIN32)
    static_cast<void>(storage);
    return reinterpret_cast<const wchar_t*>(accountName.data());
#else
    storage.fill(wchar_t{});
    std::size_t index = 0;
    while (index < accountName.size() && index + 1 < storage.size() && accountName[index] != u'\0') {
        storage[index] = static_cast<wchar_t>(accountName[index]);
        ++index;
    }
    return storage.data();
#endif
}

template <std::size_t Capacity>
const char16_t* UserItemHostWideToTargetUtf16(const wchar_t* source, std::array<char16_t, Capacity>& destination) noexcept {
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
const char16_t* UserItemHostWideToTargetUtf16(const char16_t* source, std::array<char16_t, Capacity>& destination) noexcept {
    destination.fill(u'\0');
    if (source == nullptr) {
        return destination.data();
    }
    for (std::size_t index = 0; source[index] != u'\0' && index + 1 < Capacity; ++index) {
        destination[index] = source[index];
    }
    return destination.data();
}

const wchar_t* RecipeStoreMessageForLog(
    const char16_t* message, const std::size_t capacity, std::array<wchar_t, 30>& storage
) {
#if defined(_WIN32)
    static_cast<void>(capacity);
    static_cast<void>(storage);
    return reinterpret_cast<const wchar_t*>(message);
#else
    std::size_t length = 0;
    while (length < capacity && message[length] != u'\0') {
        ++length;
    }
    const std::wstring converted = Utf16ToWide(std::u16string_view(message, length));
    const std::size_t copyLength = std::min(converted.size(), storage.size() - 1U);
    std::copy_n(converted.data(), copyLength, storage.data());
    storage[copyLength] = L'\0';
    return storage.data();
#endif
}

} // namespace

namespace UserHelper {

bool CheckSameStackableItem(const ItemAmountDataStruct& items) {
    // Current L2Server 0x00b86c70. Repeated non-stackable class IDs are
    // valid. A repeated stackable class ID makes the whole request invalid.
    L2_GUARD_FUNCTION();
    std::set<int> seenClassIds;
    for (const ItemAmountData* const entry : items.dataList) {
        CObject* const object = g_ObjectDB.GetObjectW(entry->m_nItemClassId, false);
        if (object == nullptr || !object->IsItem()) {
            return false;
        }

        const auto inserted = seenClassIds.insert(entry->m_nItemClassId);
        if (!inserted.second && CSharedItemData::IsStackable(static_cast<CItem*>(object)->m_data->m_ItemConsumeType)) {
            return false;
        }
    }
    return true;
}

} // namespace UserHelper

int User::GetPendingUseETCItemID(PendingUseItemType type) {
    L2_GUARD_FUNCTION();
    const int itemId = [&] {
        L2_READ_GUARD(m_PendingItemLock);
        CItem* const item = m_PendingItem[type].GetItem();
        const int itemId = item ? static_cast<int>(item->m_nId) : 0;
        return itemId;
    }();
    return itemId;
}
void User::SetPendingUseETCItem(CItem* item, PendingUseItemType type) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_PendingItemLock);
        m_PendingItem[type] = item;
    }
}
CItem* User::GetPendingUseETCItem(PendingUseItemType type) {
    L2_GUARD_FUNCTION();
    CItem* const item = [&] {
        L2_READ_GUARD(m_PendingItemLock);
        CItem* const item = m_PendingItem[type].GetItem();
        return item;
    }();
    return item;
}
bool User::CheckAndSetPendingUseETCItem(CItem* item, PendingUseItemType type) {
    L2_GUARD_FUNCTION();
    const bool available = [&] {
        L2_WRITE_GUARD(m_PendingItemLock);
        const bool available = m_PendingItem[type].GetItem() == nullptr;
        if (available) {
            m_PendingItem[type] = item;
        }
        return available;
    }();
    return available;
}
bool User::CheckAndResetPendingUseETCItem(PendingUseItemType type) {
    L2_GUARD_FUNCTION();
    const bool occupied = [&] {
        L2_WRITE_GUARD(m_PendingItemLock);
        const bool occupied = m_PendingItem[type].GetItem() != nullptr;
        if (occupied) {
            m_PendingItem[type] = nullptr;
        }
        return occupied;
    }();
    return occupied;
}
L2ServerLong User::EnterItemManipulation(int manipulation) {
    L2_GUARD_FUNCTION();
    L2ServerLong result = manipulation;
    if (manipulation > 0) {
        {
            L2_GUARD(m_ItemManipulationLock);
            result = m_nItemManipulation;
            if (result == 0) {
                m_nItemManipulation = manipulation;
                m_nLastItemManipulateTick = runtime::GetTickCount();
            }
        }
    }
    return result;
}
void User::LeaveItemManipulation() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_ItemManipulationLock);
        m_nItemManipulation = 0;
        m_nLastItemManipulateTick = 0;
        m_nItemManipulationID = 0;
        m_nItemManipulationCount = 0;
    }
}
int User::GetEquipDurationItemCount() {
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);
    return static_cast<int>(m_mmapEquipDurationItem.size());
}
void User::SetPendingTeleportItem(int itemClassId, std::int64_t amount) {
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(m_PendingTeleportItemLock);
        m_nPendingTeleportItemClassID = itemClassId;
        m_nPendingTeleportItemAmount = amount;
    }
}
int User::GetPendingTeleportItemClassID() {
    L2_GUARD_FUNCTION();
    const int itemClassId = [&] {
        L2_READ_GUARD(m_PendingTeleportItemLock);
        const int itemClassId = m_nPendingTeleportItemClassID;
        return itemClassId;
    }();
    return itemClassId;
}
std::int64_t User::GetPendingTeleportItemAmount() {
    L2_GUARD_FUNCTION();
    const std::int64_t amount = [&] {
        L2_READ_GUARD(m_PendingTeleportItemLock);
        const std::int64_t amount = m_nPendingTeleportItemAmount;
        return amount;
    }();
    return amount;
}
bool User::SetItemPeriod(CItem& item, int periodMinutes) {
    L2_GUARD_FUNCTION();
    const int itemDbId = runtime::Int32FromBits(item.GetDBID());
    L2_GUARD(m_PeriodicItemLock);
    const int endTime =
        runtime::AddInt32Wrapping(runtime::MultiplyInt32Wrapping(periodMinutes, 60), runtime::crt::Time32Now());
    const auto periodicItem = m_periodicItemMap.find(itemDbId);
    if (periodicItem == m_periodicItemMap.end()) {
        return false;
    }

    const int previousEndTime = periodicItem->second.expireTime;
    periodicItem->second.expireTime = endTime;
    db.AddItemPeriod(
        *this, static_cast<unsigned int>(itemDbId), runtime::SubtractInt32Wrapping(endTime, previousEndTime)
    );
    return true;
}
int User::GetPeriodLeftForShapeShiftedItem(int itemDbId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_PeriodicShapeShiftedItemLock);
    const auto item = m_periodicShapeShiftedItemMap.find(itemDbId);
    if (item == m_periodicShapeShiftedItemMap.end()) {
        return -9999;
    }
    return runtime::SubtractInt32Wrapping(item->second.expireTime, runtime::crt::Time32Now());
}
int User::GetPeriodLeft(int itemDbId, int shapeShifted) {
    L2_GUARD_FUNCTION();
    if (shapeShifted < 1) {
        L2_GUARD(m_PeriodicItemLock);
        const auto periodicItem = m_periodicItemMap.find(itemDbId);
        if (periodicItem == m_periodicItemMap.end()) {
            return -9999;
        }
        return runtime::SubtractInt32Wrapping(periodicItem->second.expireTime, runtime::crt::Time32Now());
    }
    return GetPeriodLeftForShapeShiftedItem(itemDbId);
}
void User::ShowPremiumItemList() {
    L2_GUARD_FUNCTION();
    if (g_PremiumSetting.pause) {
        SendSystemMessage(SystemMessageId::DIMENSIONAL_ITEM_RETRIEVAL_TEMPORARILY_UNAVAILABLE);
        return;
    }

    CPacketHelperEx<20480> packet;
    int itemCount = 0;
    {
        L2_GUARD(m_PremiumItemLock);
        for (const auto& premiumItem : m_mapPremiumItem) {
            const CPremiumItem* const item = premiumItem.second;
            packet.Assemble(
                "QdQdS", item->m_nWarehouseNo, item->m_nItemClassId, item->m_nItemAmount, item->m_bIsGift ? 1 : 0,
                item->m_sBuyerCharName.data()
            );
            ++itemCount;
            if (itemCount >= 0x12c) {
                break;
            }
        }
    }

    if (itemCount > 0) {
        GetSocket()->Send("chdb", 0xfe, 0x87, itemCount, packet.GetLength(), packet.GetPacket());
    } else {
        SendSystemMessage(SystemMessageId::NO_MORE_DIMENSIONAL_ITEMS_TO_RETRIEVE);
    }
}
void User::ReleaseAllPremiumItemList() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_PremiumItemLock);
    for (const auto& premiumItem : m_mapPremiumItem) {
        L2_RELEASE(premiumItem.second, ObjectRefType::CreateDelete);
    }
    m_mapPremiumItem.clear();
}
void SharedReuseDelaysOfItems::AddReuseDelay(int userDatabaseId, int sharedReuseGroupId, int reuseEndTime) {
    L2_GUARD_FUNCTION();
    static_cast<void>(userDatabaseId);
    L2_GUARD(m_lock);

    const auto reuseDelay = m_SharedReuseDelays.find(sharedReuseGroupId);
    if (reuseDelay == m_SharedReuseDelays.end()) {
        if (m_SharedReuseDelays.size() < static_cast<std::size_t>(MAX_COUNT)) {
            m_SharedReuseDelays.emplace(sharedReuseGroupId, reuseEndTime);
        }
        return;
    }
    reuseDelay->second = reuseEndTime;
}
int SharedReuseDelaysOfItems::GetReuseRemainTime(int sharedReuseGroupId) {
    L2_GUARD_FUNCTION();
    int remainTime = -1;
    {
        L2_GUARD(m_lock);
        const auto reuseDelay = m_SharedReuseDelays.find(sharedReuseGroupId);
        if (reuseDelay != m_SharedReuseDelays.end()) {
            remainTime = runtime::SubtractInt32Wrapping(reuseDelay->second, runtime::crt::Time32Now());
        }
    }
    return remainTime < 0 ? 0 : remainTime;
}
bool SharedReuseDelaysOfItems::RemoveReuseDelay(User& user, int sharedReuseGroupId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto reuseDelay = m_SharedReuseDelays.find(sharedReuseGroupId);
    if (reuseDelay == m_SharedReuseDelays.end()) {
        return false;
    }

    const int now = runtime::crt::Time32Now();
    reuseDelay->second = now;
    db.RequestSaveSharedReuseDelayOfItem(user, sharedReuseGroupId, now);
    return true;
}
void SharedReuseDelaysOfItems::RemoveReuseDelayAll(User& user) {
    L2_GUARD_FUNCTION();
    const int now = runtime::crt::Time32Now();
    L2_GUARD(m_lock);
    for (auto& reuseDelay : m_SharedReuseDelays) {
        reuseDelay.second = now;
        db.RequestSaveSharedReuseDelayOfItem(user, reuseDelay.first, now);
    }
}
bool SharedReuseDelaysOfItems::UseDelayShareItem(User& user, int sharedReuseGroupId, int reuseDelay, int* reuseEndTime) {
    L2_GUARD_FUNCTION();
    *reuseEndTime = runtime::AddInt32Wrapping(runtime::crt::Time32Now(), reuseDelay / 1000);

    bool used = false;
    {
        L2_GUARD(m_lock);
        const auto previousDelay = m_SharedReuseDelays.find(sharedReuseGroupId);
        if (previousDelay == m_SharedReuseDelays.end()) {
            if (m_SharedReuseDelays.size() < static_cast<std::size_t>(MAX_COUNT)) {
                m_SharedReuseDelays.emplace(sharedReuseGroupId, *reuseEndTime);
                used = true;
            } else {
                *reuseEndTime = 0;
            }
        } else if (runtime::crt::Time32Now() < previousDelay->second) {
            *reuseEndTime = previousDelay->second;
        } else {
            previousDelay->second = *reuseEndTime;
            used = true;
        }
    }

    if (used) {
        db.RequestSaveSharedReuseDelayOfItem(user, sharedReuseGroupId, *reuseEndTime);
    }
    return used;
}
bool User::_ItemRefundPush(CItem* item, std::int64_t itemAmount, std::int64_t totalPrice) {
    L2_GUARD_FUNCTION();
    if (itemAmount < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] nItemAmount[%d]",
        //     L"user_item.cpp",
        //     0x2f88,
        //     runtime::Int32FromBits(
        //         static_cast<std::uint32_t>(itemAmount)))
        L2_LOG_ERROR(
            Log, u"[refunds] Refund item amount %d is invalid; entry rejected.",
            runtime::Int32FromBits(static_cast<std::uint32_t>(itemAmount))
        );
        return false;
    }

    const double halfDefaultPrice = static_cast<double>(item->m_const->m_nDefaultPrice) * 0.5;
    double itemPrice = static_cast<double>(totalPrice / itemAmount);
    if (itemPrice <= halfDefaultPrice) {
        itemPrice = halfDefaultPrice;
    }

    if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
        for (ItemRefund* refund : m_ItemRefunds) {
            if (item->m_data->m_nClassID == refund->m_pItem->m_data->m_nClassID &&
                runtime::Win64CvttDoubleToInt64(itemPrice) == refund->m_Price) {
                refund->m_Amount = runtime::AddInt64Wrapping(refund->m_Amount, itemAmount);
                return false;
            }
        }
    }

    CItem* copiedItem = static_cast<CItem*>(item->Copy());
    auto* refund = new ItemRefund{copiedItem, itemAmount, runtime::Win64CvttDoubleToInt64(itemPrice)};
    m_ItemRefunds.push_back(refund);
    while (m_ItemRefunds.size() > 12) {
        _ItemRefundFrontClear();
    }
    return true;
}
void User::ItemRefundClear() {
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);
    while (!m_ItemRefunds.empty()) {
        _ItemRefundFrontClear();
    }
}

int User::OnDiePenalty(bool, double) {
    // Current L2Server.exe 0x00b86904, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (m_AreaMod.m_bIsInPvPZone || IsInMySiegeBattleField() != 0 || m_SkillMod.m_OnProtectDP ||
        (m_data->m_nPKCount == m_data->m_nPKPardonCount && m_data->m_nLevel <= 9) ||
        CPVPMatchSystem::inst().IsPVPPlayer(this)) {
        return 0;
    }

    const int skillId = m_data->m_bWeakenedDeathPenalty ? 0x5b30 : 0x38eb;
    int penaltyLevel = m_data->GetAbnormalLevel(ABNS_DEATH_PENALTY_GD);
    if (penaltyLevel < 1) {
        penaltyLevel = 1;
    } else {
        const CSkillInfo* const currentSkill = g_SkillDB.GetSkillInfo(CSkillKey(skillId, penaltyLevel), this);
        if (currentSkill->m_pNextLevel == nullptr) {
            return 0;
        }
        ++penaltyLevel;
    }

    const CSkillInfo* const penaltySkill = g_SkillDB.GetSkillInfo(CSkillKey(skillId, penaltyLevel), this);
    if (!InfectedSkillBy(nullptr, penaltySkill, nullptr, runtime::crt::Time32Now())) {
        return 0;
    }

    SetMaxMagicLevel();
    ValidateChanged(VB_MAX);
    SendAbnormalStatusInfo();
    GetSocket()->SendSystemMessage_d(SystemMessageId::AFFLICTED_BY_SHILEN_BREATH_LEVEL, penaltyLevel);

    const FVector position = GetPos();
    int acquiredLevel = GetAcquiredSkillLevel(skillId);
    if (acquiredLevel == -1) {
        acquiredLevel = 0;
    }
    const char16_t* const userName = GetName();
    const std::uint32_t accountId = GetAccountID();
    const std::uint32_t userDatabaseId = GetDBID();
    std::array<wchar_t, 16> accountNameStorage{};
    const wchar_t* const accountName = PendingTeleportAccountName(m_sAccountName, accountNameStorage);

    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%64d,%I64d,%I64d,,,,"
    //     L"%s,%s,,,,",
    //     std::int64_t{0x370},
    //     static_cast<std::int64_t>(userDatabaseId),
    //     static_cast<std::int64_t>(accountId),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.x)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.y)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.z)),
    //     static_cast<std::int64_t>(m_data->m_Race),
    //     static_cast<std::int64_t>(m_data->m_Sex),
    //     static_cast<std::int64_t>(m_data->m_nClass),
    //     static_cast<std::int64_t>(m_data->m_nLevel),
    //     skillId,
    //     static_cast<std::int64_t>(acquiredLevel),
    //     static_cast<std::int64_t>(penaltyLevel),
    //     userName,
    //     accountName)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%64d,%I64d,%I64d,,,,"
        u"%s,%s,,,,",
        std::int64_t{0x370}, static_cast<std::int64_t>(userDatabaseId), static_cast<std::int64_t>(accountId),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)), static_cast<std::int64_t>(m_data->m_Race),
        static_cast<std::int64_t>(m_data->m_Sex), static_cast<std::int64_t>(m_data->m_nClass),
        static_cast<std::int64_t>(m_data->m_nLevel), skillId, static_cast<std::int64_t>(acquiredLevel),
        static_cast<std::int64_t>(penaltyLevel), userName, accountName
    );
    return penaltyLevel;
}

bool User::DeleteItemInInventoryBeforeCommit(const int itemClassId, std::int64_t count) {
    // Current L2Server 0x00b9200c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (count < 1) {
        return false;
    }

    CItem* const item = m_inv.GetFirstItemByClassID(itemClassId, false, false);
    if (item == nullptr) {
        return false;
    }

    CPacketHelperEx<20480> packet;
    int changedItemCount = 0;
    const bool requestReady = [&] {
        L2_WRITE_GUARD(m_inv);

        if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
            if (m_inv.Have(item->m_data->index)) {
                std::int64_t realDeleteCount;
                std::int64_t remainingItemCount;
                {
                    L2_GUARD(*item->m_lock);
                    realDeleteCount = item->m_data->m_nCount <= count ? item->m_data->m_nCount : count;
                    count -= realDeleteCount;
                    if (!item->AddAmount(-realDeleteCount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                        //     "user_item.cpp",
                        //     0x8de,
                        //     static_cast<long long>(item->m_data->m_nCount),
                        //     static_cast<long long>(-realDeleteCount))
                        L2_LOG_ERROR(
                            Log, u"[inventory] Item DB ID %u rejected amount change %I64d; current amount is %I64d.",
                            item->GetDBID(), static_cast<long long>(-realDeleteCount),
                            static_cast<long long>(item->m_data->m_nCount)
                        );
                    }
                    remainingItemCount = item->m_data->m_nCount;
                }

                if (count < 0) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d]DeleteItemInInventoryBeforeCommit error, "
                    //     L"req del count[%I64d], remain item count[%I64d], "
                    //     L"real del count[%I64d]",
                    //     "user_item.cpp",
                    //     0x8e8,
                    //     static_cast<long long>(count),
                    //     static_cast<long long>(item->m_data->m_nCount),
                    //     static_cast<long long>(realDeleteCount))
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Pre-commit deletion produced negative remaining request count %I64d for item DB ID "
                        u"%u; item now has %I64d after deleting %I64d.",
                        static_cast<long long>(count), item->GetDBID(), static_cast<long long>(item->m_data->m_nCount),
                        static_cast<long long>(realDeleteCount)
                    );
                }

                const int assembled = remainingItemCount < 1
                    ? AssembleForDeleteItemInInventoryBeforeCommit(*item, 3, 0, packet)
                    : AssembleForDeleteItemInInventoryBeforeCommit(*item, 2, realDeleteCount, packet);
                if (assembled == 0) {
                    return false;
                }

                if (remainingItemCount < 1) {
                    m_inv.SetInventoryChanged(item, IUT_DELETE);
                    m_inv.Pop(item->m_data->index, false);
                    item->Delete();
                } else {
                    m_inv.SetInventoryChanged(item, IUT_UPDATE);
                }
                changedItemCount = 1;
            }

            return true;
        }

        if (count == 1) {
            const int assembled = AssembleForDeleteItemInInventoryBeforeCommit(*item, 3, 0, packet);
            if (assembled != 0) {
                changedItemCount = 1;
                m_inv.SetInventoryChanged(item, IUT_DELETE);
                m_inv.Pop(item->m_data->index, false);
                item->Delete();
                count = 0;

                return true;
            }
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Cannot delete non-stackable item in inventory. "
            //     L"count[%d] item[%d]",
            //     L"user_item.cpp",
            //     0x90e,
            //     static_cast<int>(count),
            //     item->m_nClassId)
            L2_LOG_ERROR(
                Log, u"[inventory] Cannot delete %d units of non-stackable item class %d; request rejected.",
                static_cast<int>(count), item->m_nClassId
            );
        }

        return false;
    }();
    if (!requestReady) {
        return false;
    }
    db.RequestChangeItemData(this, changedItemCount, packet.GetLength(), packet.GetPacket());
    m_data->m_NeedUpdatePacketBitSet.SetBit(0x49);
    SendItemList(true, true, nullptr);
    return count == 0;
}

bool User::ReleaseEconomy() {
    // Current L2Server 0x00b7884c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (m_pEconomy == nullptr) {
        return false;
    }

    L2_RELEASE(m_pEconomy, ObjectRefType::CreateDelete);
    m_pEconomy = nullptr;
    return true;
}

bool User::DropItem(CItem* const item, std::int64_t count, FVector& position, const bool unknown) {
    // Current L2Server 0x00b78f74, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!CanDropItem(item, count, position, false)) {
        return false;
    }

    if (!CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
        count = -1;
    }

    SlotType slot = ST_UNDERWEAR;
    while (slot < ST_MAX && (ST2SBT[static_cast<int>(slot)] & item->m_data->m_Slot) == 0) {
        slot = static_cast<SlotType>(static_cast<int>(slot) + 1);
    }
    slot = IsEquiped(item->m_data->index, slot);
    db.RequestDropItem(item, this, position, slot, unknown, count);
    return true;
}

bool User::CancelEquipmentAddedEffectAndDropItem(
    CItem* const item, std::int64_t count, const int effectMask, FVector* const position, const bool setContributeData
) {
    // Current L2Server.exe 0x00b7b354, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!CanDropItem(item, count, *position, false) ||
        ((item->m_itemOption.itemOptionKey.variationOption1 < 1 || item->m_itemOption.itemOptionKey.variationOption2 < 1) &&
            !item->IsPeriodicShapeShiftedItem(this))) {
        return false;
    }

    if (!CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
        count = -1;
    }

    SlotType slot = ST_UNDERWEAR;
    while (slot < ST_MAX && (ST2SBT[static_cast<int>(slot)] & item->m_data->m_Slot) == 0) {
        slot = static_cast<SlotType>(static_cast<int>(slot) + 1);
    }
    slot = IsEquiped(item->m_data->index, slot);
    db.RequestCancelEquipmentAddedEffectAndDropItem(this, item, position, slot, count, effectMask, setContributeData);
    return true;
}

bool User::CheckDropItemCondition() {
    // Current L2Server.exe 0x00b790e0, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (m_data->m_nWorldId != g_nWorldId || g_nNotDropItemOnUserDie != 0 || m_data->m_nKarma >= 0 ||
        runtime::SubtractInt32Wrapping(m_data->m_nPKCount, m_data->m_nPKPardonCount) < g_nPKPenaltyCount ||
        GetSocket()->m_bEconomicRestriction) {
        return false;
    }

    CGeoData* const geo = CWorld::Inst().GetGeo();
    CGeoZone* const zone =
        geo->GetZone(DropZoneCoordinate(m_data->m_Pos.x, 0x50000U), DropZoneCoordinate(m_data->m_Pos.y, 0x40000U));
    return zone == nullptr || !zone->IsInNoDropItemArea(m_data->m_Pos);
}

void User::CalcChaoDropRate(double* itemDropRate, double* adenaDropRate, bool applyQuarterPenalty) {
    // Current L2Server.exe 0x00b79244, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    double rate = (static_cast<double>(m_data->m_nKarma) * -1.0 * 3.0) / 2000.0 - 3.0;
    double cappedRate = rate;
    if (50.0 <= rate) {
        cappedRate = 50.0;
    }
    if (cappedRate <= 0.0) {
        rate = 0.0;
    } else if (50.0 <= rate) {
        rate = 50.0;
    }

    *itemDropRate = rate;
    *adenaDropRate = rate / 3.0;
    if (applyQuarterPenalty) {
        *adenaDropRate = (rate / 3.0) * 0.25;
        *itemDropRate *= 0.25;
    }
    *itemDropRate = g_dDropPenaltyRate * *itemDropRate;
    *adenaDropRate = g_dDropPenaltyRate * *adenaDropRate;
}

int User::DecideDropItem(int* const itemIndices, const double dropRateBonus, const bool applyQuarterPenalty) {
    // Current L2Server.exe 0x00b7ebc4, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    CSharedItemData* itemData = m_inv.GetFirst();
    int candidateCount = 0;
    int testedDepth = 0;
    double itemDropRate = 0.0;
    double equippedDropRate = 0.0;
    CalcChaoDropRate(&itemDropRate, &equippedDropRate, applyQuarterPenalty);

    while (itemData != nullptr && candidateCount <= 0x191) {
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemData->index);
        if (!item) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Cannot Get item at file[%S], line[%d]",
            //     "user_item.cpp",
            //     0x12be)
            L2_LOG_ERROR(Log, u"[items] Cannot resolve the requested item; operation rejected.");
            itemData = itemData->Next();
            continue;
        }

        CSharedItemConstant* const constant = item->m_const;
        const bool excluded = item->IsQuestItem() || (!item->IsTrade() && constant->m_nAttribution >= 0) ||
            (!item->IsDrop() && constant->m_nAttribution >= 0) || item->m_data->m_Type == IT_ASSET ||
            constant->m_EtcitemType == EIT_CASTLE_GUARD || item->IsDuration() || constant->m_bIsPremium;
        const bool optionExcluded = g_bPKPenaltyItemDrop && item->m_itemOption.itemOptionKey.variationOption1 > 0 &&
            item->m_itemOption.itemOptionKey.variationOption2 > 0;
        if (excluded || optionExcluded || CItemRestrict::Inst().CheckItemRestrict(itemData->m_nClassID)) {
            itemData = itemData->Next();
            continue;
        }

        double rate = itemDropRate;
        if (item->IsAsset()) {
            rate = 0.0;
        } else if (item->IsWeapon() && IsEquipedItem(item->m_data->index)) {
            rate = equippedDropRate;
        }
        if (genrand64_real1() * 100.0 + 0.0 < rate * (dropRateBonus / 100.0 + 1.0)) {
            itemIndices[candidateCount] = itemData->index;
            ++candidateCount;
        }

        itemData = itemData->Next();
        ++testedDepth;
        if (testedDepth > 0x192) {
            // Log.Add(
            //     LOG_ERROR,
            //     "depth> %d at file[%s], line[%d]",
            //     0x192,
            //     "user_item.cpp",
            //     0x130b)
            L2_LOG_ERROR(Log, "[items] Traversal depth exceeds %d; scan stopped.", 0x192);
            return candidateCount;
        }
    }
    return candidateCount;
}

void User::DropAndDestroyItem(CItem& item, const bool chao, bool, int*, int*, bool* const destroyed) {
    // Current L2Server.exe 0x00b833d0, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    FVector position = m_data->m_Pos;
    CGeoData* const geo = CWorld::Inst().GetGeo();
    position.z = static_cast<double>(geo->FindGround(
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z)
    ));

    const int angleDegrees = runtime::Win64CvttDoubleToInt32(genrand64_real2() * 360.0 + 0.0);
    const double angle = (static_cast<double>(angleDegrees) * 3.14) / 360.0;
    const int distance = runtime::Win64CvttDoubleToInt32(genrand64_real2() * 16.0 + 30.0);
    const double sine = std::sin(angle);
    const double cosine = std::cos(angle);
    position.y += static_cast<double>(runtime::Win64CvttDoubleToInt32(sine * static_cast<double>(distance)));
    position.x += static_cast<double>(runtime::Win64CvttDoubleToInt32(cosine * static_cast<double>(distance)));
    position.z = static_cast<double>(geo->FindNearGround(
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z)
    ));

    if (!chao) {
        return;
    }

    if (item.m_const->m_nAttribution != -1 && item.m_data->GetLocked() == 0) {
        if ((item.m_itemOption.itemOptionKey.variationOption1 < 1 || item.m_itemOption.itemOptionKey.variationOption2 < 1) &&
            !item.IsPeriodicShapeShiftedItem(this)) {
            DropItem(&item, item.m_data->m_nCount, position, false);
            if (item.m_nClassId == 0x115a || item.m_data->GetEnchanted() < 1) {
                GetSocket()->Send("chccd", 0x62, SystemMessageId::DROPPED_ITEM, 1, 3, item.m_nClassId);
            } else {
                GetSocket()->Send(
                    "chccccd", 0x62, SystemMessageId::DROPPED_ENCHANTED_ITEM, 2, 0x13,
                    static_cast<char>(item.m_data->GetEnchanted()), 3, item.m_nClassId
                );
            }
            return;
        }

        int effectMask = 0;
        if (item.m_itemOption.itemOptionKey.variationOption1 > 0 && item.m_itemOption.itemOptionKey.variationOption2 > 0) {
            effectMask = 1;
        }
        if (item.IsPeriodicShapeShiftedItem(this)) {
            effectMask |= 2;
        }
        CancelEquipmentAddedEffectAndDropItem(&item, item.m_data->m_nCount, effectMask, &position, true);
        return;
    }

    CUserSocket* const socket = GetSocket();
    if (item.m_data->m_nCount < 2) {
        if (item.m_data->GetEnchanted() > 1) {
            const char16_t* const selfName = GetName();
            socket->Send(
                "chccScccd", 0x62, SystemMessageId::ENCHANTED_ITEM_DESTROYED_AFTER_CHARACTER_DEATH, 3, 0, selfName, 0x13,
                static_cast<char>(item.m_data->GetEnchanted()), 3, item.m_nClassId
            );
            const char16_t* const broadcastName = GetName();
            const FVector broadcastPosition = GetPos();
            g_ptlUserMap->BroadcastToNeighborExceptSelf(
                this, 0x5000, broadcastPosition, "chccScccd", 0x62, 0x17f6, 3, 0x0c, broadcastName, 0x13,
                static_cast<char>(item.m_data->GetEnchanted()), 3, item.m_nClassId
            );
        } else {
            const char16_t* const selfName = GetName();
            socket->Send(
                "chccScd", 0x62, SystemMessageId::ITEM_DESTROYED_AFTER_CHARACTER_DEATH, 2, 0, selfName, 3, item.m_nClassId
            );
            const char16_t* const broadcastName = GetName();
            const FVector broadcastPosition = GetPos();
            g_ptlUserMap->BroadcastToNeighborExceptSelf(
                this, 0x5000, broadcastPosition, "chccScd", 0x62, 0xe5e, 2, 0x0c, broadcastName, 3, item.m_nClassId
            );
        }
    } else {
        const char16_t* const selfName = GetName();
        socket->Send(
            "chccScdcQ", 0x62, SystemMessageId::MULTIPLE_ITEMS_DESTROYED_AFTER_CHARACTER_DEATH, 3, 0, selfName, 3,
            item.m_nClassId, 6, item.m_data->m_nCount
        );
        const char16_t* const broadcastName = GetName();
        const FVector broadcastPosition = GetPos();
        g_ptlUserMap->BroadcastToNeighborExceptSelf(
            this, 0x5000, broadcastPosition, "chccScdcQ", 0x62, 0xf2a, 3, 0x0c, broadcastName, 3, item.m_nClassId, 6,
            item.m_data->m_nCount
        );
    }

    db.RequestDestroyItem(&item, 1, this, 0);
    *destroyed = true;
}

void User::OnDieDropItem(const bool applyQuarterPenalty, const double dropRateBonus) {
    // Current L2Server.exe 0x00b83a44, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!CheckDropItemCondition()) {
        return;
    }

    const bool chao = m_data->m_nKarma < 0 &&
        runtime::SubtractInt32Wrapping(m_data->m_nPKCount, m_data->m_nPKPardonCount) >= g_nPKPenaltyCount;

    {
        L2_WRITE_GUARD(m_inv);
        int maximumDropCount = g_nMaxDropItemLimit;
        const int inventoryCount = m_inv.GetTotalCount();
        if (inventoryCount < maximumDropCount) {
            maximumDropCount = inventoryCount;
        }

        std::array<int, 0x192> itemIndices{};
        double itemDropRate = 0.0;
        double equippedDropRate = 0.0;
        CalcChaoDropRate(&itemDropRate, &equippedDropRate, applyQuarterPenalty);
        int candidateCount = DecideDropItem(itemIndices.data(), dropRateBonus, applyQuarterPenalty);

        const int race = static_cast<int>(m_data->m_Race);
        const int sex = static_cast<int>(m_data->m_Sex);
        const int characterClass = static_cast<int>(m_data->m_nClass);
        const int level = m_data->m_nLevel;
        int equippedCount = 0;
        int unequippedCount = 0;
        CLogFormatter formatter;

        if (maximumDropCount < candidateCount) {
            int processedCount = 0;
            int depth = 0;
            while (processedCount < maximumDropCount) {
                if (depth > 0x192) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "depth>%d  at file[%s], line[%d]",
                    //     0x192,
                    //     "user_item.cpp",
                    //     0x1479)
                    L2_LOG_ERROR(Log, "[items] Selection depth exceeds %d; selection stopped.", 0x192);
                    break;
                }

                int upperBound = candidateCount;
                int lowerBound = 1;
                if (candidateCount < 1) {
                    upperBound = 1;
                    lowerBound = candidateCount;
                }
                const int selectedOneBased = runtime::Win64CvttDoubleToInt32(
                    genrand64_real2() * static_cast<double>((upperBound - lowerBound) + 1) + static_cast<double>(lowerBound)
                );
                const int selectedIndex = selectedOneBased - 1;
                const int itemIndex = itemIndices[static_cast<std::size_t>(selectedIndex)];
                if (itemIndex < 1) {
                    ++depth;
                    continue;
                }

                CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
                if (!item) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Cannot Get item at file[%S], line[%d]",
                    //     "user_item.cpp",
                    //     0x1484)
                    L2_LOG_ERROR(Log, u"[items] Cannot resolve the selected item; entry skipped.");
                    ++depth;
                    continue;
                }

                const bool equipped = IsEquipedItem(item->m_data->index);
                if (equipped) {
                    ++equippedCount;
                } else {
                    ++unequippedCount;
                }
                const std::int64_t previousAmount =
                    static_cast<std::int64_t>(runtime::Int32FromBits(static_cast<std::uint32_t>(item->m_data->m_nCount)));
                bool destroyed = false;
                DropAndDestroyItem(
                    *item.get(), chao, equipped, reinterpret_cast<int*>(&itemDropRate),
                    reinterpret_cast<int*>(&equippedDropRate), &destroyed
                );

                --candidateCount;
                for (int index = selectedIndex; index < candidateCount; ++index) {
                    itemIndices[static_cast<std::size_t>(index)] = itemIndices[static_cast<std::size_t>(index + 1)];
                }
                ++processedCount;

                std::array<char16_t, 256> shapeAndVariation{};
                std::array<char16_t, 256> attribute{};
                std::array<char16_t, 256> ensoul{};
                item->MakeShapeShiftAndVariationLogInfo(shapeAndVariation.data());
                item->m_itemBaseAttribute.GetLogString(attribute.data());
                item->MakeEnsoulLogInfo(ensoul.data());
                formatter.WriteLog4(
                    0x393, this, nullptr, shapeAndVariation.data(), attribute.data(), ensoul.data(), race, sex,
                    characterClass, level, equipped, destroyed, item->m_data->GetEnchanted(), item->m_data->m_nClassID,
                    previousAmount, runtime::SubtractInt64Wrapping(item->m_data->m_nCount, previousAmount),
                    item->GetDBID()
                );
                ++depth;
            }

            formatter.WriteLog4(
                0x45f, this, nullptr, nullptr, nullptr, nullptr, race, sex, characterClass, level, processedCount,
                equippedCount, unequippedCount, 1, 0, 0, 0
            );
        } else {
            for (int index = 0; index < candidateCount; ++index) {
                CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndices[static_cast<std::size_t>(index)]);
                if (!item) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Cannot Get item at file[%S], line[%d]",
                    //     "user_item.cpp",
                    //     0x143f)
                    L2_LOG_ERROR(Log, u"[items] Cannot resolve the selected item; entry skipped.");
                    continue;
                }

                const bool equipped = IsEquipedItem(item->m_data->index);
                if (equipped) {
                    ++equippedCount;
                } else {
                    ++unequippedCount;
                }
                const std::int64_t previousAmount =
                    static_cast<std::int64_t>(runtime::Int32FromBits(static_cast<std::uint32_t>(item->m_data->m_nCount)));
                bool destroyed = false;
                DropAndDestroyItem(
                    *item.get(), chao, equipped, reinterpret_cast<int*>(&itemDropRate),
                    reinterpret_cast<int*>(&equippedDropRate), &destroyed
                );

                std::array<char16_t, 256> shapeAndVariation{};
                std::array<char16_t, 256> attribute{};
                std::array<char16_t, 256> ensoul{};
                item->MakeShapeShiftAndVariationLogInfo(shapeAndVariation.data());
                item->m_itemBaseAttribute.GetLogString(attribute.data());
                item->MakeEnsoulLogInfo(ensoul.data());
                formatter.WriteLog4(
                    0x393, this, nullptr, shapeAndVariation.data(), attribute.data(), ensoul.data(), race, sex,
                    characterClass, level, equipped, destroyed, item->m_data->GetEnchanted(), item->m_data->m_nClassID,
                    previousAmount, runtime::SubtractInt64Wrapping(item->m_data->m_nCount, previousAmount),
                    item->GetDBID()
                );
            }

            if (candidateCount > 0) {
                formatter.WriteLog4(
                    0x45f, this, nullptr, nullptr, nullptr, nullptr, race, sex, characterClass, level, candidateCount,
                    equippedCount, unequippedCount, 0, 0, 0, 0
                );
            }
        }
    }
}

bool User::GetItem(CItem* const item, const bool unknown) {
    // Current L2Server 0x00b817cc, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (m_data->m_moveType == CMT_FLY || IsInvisibleBySkill()) {
        return false;
    }
    if (IsAirShipDriver()) {
        SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_STEERING);
        return false;
    }

    if (item != nullptr) {
        if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType) &&
            item->m_data->m_nClassID == g_nBotCheckItemClassID) {
            {
                L2_GUARD(m_BotCheckItemLock);
                m_bHadBotCheckItem = true;
            }

            const FVector itemPosition = GetPos();
            const std::u16string ipAddress = GetSocket()->GetIPAddressString();
            std::array<char16_t, 64> ipAddress16{};
            std::u16string actorName16;
            UserItemHostWideToTargetUtf16(ipAddress.c_str(), ipAddress16);
            actorName16 = GetName();
            CLogFormatter audit;
            audit.WriteAuditLog(
                0x585, GetDBID(), GetAccountID(), 0, 0, runtime::Win64CvttDoubleToInt32(itemPosition.x),
                runtime::Win64CvttDoubleToInt32(itemPosition.y), runtime::Win64CvttDoubleToInt32(itemPosition.z),
                ipAddress16.data(), u"", u"", 8, 0, 0, 0, 0, 0, 0, 0, 0, Level(), actorName16.data(), m_sAccountName.data(),
                u"", u"", 0
            );

            m_data->m_NeedUpdatePacketBitSet.SetBoolValue_IfModifed(m_IsSheathingWeapon, false, 0x5a);

            FOR_ALL_OBJECT_IN_OBJECT_MAP(g_GMCharMap, observer, {
                if (observer == nullptr || !observer->IsBuilder()) {
                    continue;
                }
                const FVector observerPosition = observer->GetPos();
                const double xDistance = itemPosition.x - observerPosition.x;
                const double yDistance = itemPosition.y - observerPosition.y;
                if (xDistance * xDistance + yDistance * yDistance >= 16777216.0) {
                    continue;
                }

                const int objectId = static_cast<int>(Id());
                const int x = runtime::Win64CvttDoubleToInt32(itemPosition.x);
                const int y = runtime::Win64CvttDoubleToInt32(itemPosition.y);
                const int z = runtime::Win64CvttDoubleToInt32(itemPosition.z);
                observer->GetSocket()->Send(
                    "cdddddddddddhhddddd", 0x48, 0, objectId, objectId, 0x7e8, 1, 500, 0, 0x5dc, x, y, z, 0, 2, x, y, z, 0, 0
                );
                observer->GetSocket()->Send("cdddddd", 0x54, 0, objectId, 0x7e8, 1, 1, objectId);
            });

            item->Delete();
            return false;
        }

        if (item->IsEtcItem() && item->m_const->m_EtcitemType == EIT_CASTLE_GUARD) {
            if (GetPledge() == nullptr || GetPledge()->GetCastle() == nullptr || !HavePledgePower(PP_MANAGE_MERCENARY)) {
                SendSystemMessage(SystemMessageId::NO_AUTHORITY_TO_CANCEL_MERCENARY_POSITION);
                return false;
            }

            CResidence* const residence = GetPledge()->GetCastle()->CastResidence();
            const std::uint32_t itemClassId = static_cast<std::uint32_t>(item->m_nClassId);
            if (residence->m_mapResidenceGuardItem.find(itemClassId) == residence->m_mapResidenceGuardItem.end()) {
                SendSystemMessage(SystemMessageId::CANNOT_CANCEL_MERCENARY_FROM_UNOWNED_CASTLE);
                return false;
            }
        }
    }

    if (!IsNowTrade()) {
        return CCreature::GetItem(item, unknown);
    }
    if (GetTrade() != nullptr) {
        TradeCancel(false);
    }
    SendSystemMessage(SystemMessageId::CANNOT_PICK_UP_OR_USE_ITEMS_WHILE_TRADING);
    return false;
}

CSPointer<CBuySell> User::GetBuySell() {
    // Current L2Server 0x00b7c9f4, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (m_pEconomy != nullptr && typeid(*m_pEconomy) == typeid(CBuySell)) {
        return CSPointer<CBuySell>(static_cast<CBuySell*>(m_pEconomy), ObjectRefType::FunctionChain);
    }
    return {};
}

bool User::CanUseNpcMerchant(CSPointer<CBuySell> buySell) {
    // Current L2Server 0x00b82658, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!buySell) {
        return false;
    }
    if (GetTrade() != nullptr) {
        SendSystemMessage(SystemMessageId::ALREADY_TRADING);
        return false;
    }
    if (GetPrivateStoreType() != PST_NONE) {
        SendSystemMessage(SystemMessageId::CANNOT_DROP_DESTROY_OR_TRADE_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP);
        return false;
    }
    if (!IsItemUsable(0)) {
        return false;
    }

    CObject* const merchantObject = CObject::GetObjectW(static_cast<std::uint32_t>(buySell->nNPCServerId));
    CNPC* const merchant = merchantObject != nullptr && merchantObject->IsNPC() ? merchantObject->CastNPC() : nullptr;
    if (merchant == nullptr) {
        // Log.Add(LOG_ERROR, L"cannot find merchant npc.")
        L2_LOG_ERROR(Log, u"[merchant] Merchant NPC is unavailable; the request is rejected.");
        return false;
    }
    return IsTalkableDistance(merchant);
}

short User::GetItemBuyFromNpcList(const std::int64_t listId, CPacketHelperEx<20480>& packet) {
    // Current L2Server 0x00b88360, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    CSPointer<CBuySell> buySell = GetBuySell();
    if (!buySell) {
        return 0;
    }

    const BuySellListCache::Key key{buySell->m_nResidenceID, static_cast<int>(GetInZoneID()), listId};
    BuySellListCache::List* const list = BuySellListCache::Inst().GetList(key);
    if (list == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Not a cached buy-sell list[%I64u]",
        //     L"user_item.cpp",
        //     0x2eb0,
        //     static_cast<std::uint64_t>(listId))
        L2_LOG_ERROR(
            Log, u"[merchant] Buy/sell list %I64u is not cached; item list cannot be generated.",
            static_cast<std::uint64_t>(listId)
        );
        return 0;
    }

    short count = 0;
    for (BuySell& entry : *list) {
        std::int64_t stock = entry.m_nCurrentStock;
        if (entry.m_dStockRate > 0.0 && stock <= 0) {
            const std::int32_t now = runtime::crt::Time32Now();
            if (runtime::SubtractInt32Wrapping(now, entry.m_nStockTimestamp) < 0xe11) {
                continue;
            }
            if (entry.m_dStockRate > 0.0 && entry.m_dStockRate <= 1.0 && ::genrand64_real1() <= entry.m_dStockRate) {
                stock = entry.m_nStock;
            } else {
                stock = 0;
            }
            entry.m_nCurrentStock = stock;
            entry.m_nStockTimestamp = now;
            if (stock == 0) {
                continue;
            }
        }

        CObject* const object = g_ObjectDB.GetObjectW(entry.m_nItemClassId, false);
        if (object == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Undefined item(%d)",
            //     L"user_item.cpp",
            //     0x2eec,
            //     entry.m_nItemClassId)
            L2_LOG_ERROR(
                Log, u"[merchant] Buy/sell list %I64u references undefined item class %d; entry skipped.",
                static_cast<std::uint64_t>(listId), entry.m_nItemClassId
            );
            continue;
        }
        if (!object->IsItem()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Not a item(%d)",
            //     L"user_item.cpp",
            //     0x2ef1,
            //     entry.m_nItemClassId)
            L2_LOG_ERROR(
                Log, u"[merchant] Buy/sell list %I64u references object class %d, which is not an item; entry skipped.",
                static_cast<std::uint64_t>(listId), entry.m_nItemClassId
            );
            continue;
        }

        CItem* const item = static_cast<CItem*>(object);
        const int crystal = BaseCrystal[static_cast<int>(item->m_const->GetCrystalType())];
        const double bonus = crystal == CRT_MAX ? 1.0 : m_SkillMod.m_dTradeBonusPer[crystal];
        const std::int64_t adjustedRate = runtime::AddInt64Wrapping(
            runtime::AddInt64Wrapping(entry.m_nPrice, static_cast<std::int64_t>(buySell->GetTaxRate())), 100
        );
        const std::int64_t price = runtime::Win64CvttDoubleToInt64(
            static_cast<double>(adjustedRate) * static_cast<double>(item->m_const->m_nDefaultPrice) * 0.01 * (2.0 - bonus)
        );

        CItemAssemblerAmount assembler(*item, stock);
        const int assembled = assembler.AssembleItemInfo(packet);
        packet.Assemble("Q", price);
        if (assembled > 0) {
            ++count;
        }
    }
    return count;
}

short User::GetItemSellToNpcList(const std::int64_t listId, const double ratio, CPacketHelperEx<20480>& packet) {
    // Current L2Server 0x00b88814, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    CSPointer<CBuySell> buySell = GetBuySell();
    if (!buySell) {
        return 0;
    }

    const BuySellListCache::Key key{buySell->m_nResidenceID, static_cast<int>(GetInZoneID()), listId};
    const BuySellListCache::List* const banList = BuySellListCache::Inst().GetList(key);
    if (banList == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Not a cached buy-sell list[%I64d]",
        //     L"user_item.cpp",
        //     0x2f2e,
        //     listId)
        L2_LOG_ERROR(Log, u"[merchant] Buy/sell ban list %I64d is not cached; sell list cannot be generated.", listId);
        return 0;
    }

    short count = 0;
    int depth = 0;
    {
        L2_READ_GUARD(m_inv);
        for (CSharedItemData* data = m_inv.GetFirst(); data != nullptr; data = data->Next()) {
            CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(data->index);
            if (!item || !item->IsSellableToNpcStore(this, false) ||
                BuySellListCache::IsInBuySellBanList(item->m_data->m_nClassID, *banList)) {
                continue;
            }

            const int crystal = BaseCrystal[static_cast<int>(item->m_const->GetCrystalType())];
            const double bonus = crystal == CRT_MAX ? 1.0 : m_SkillMod.m_dTradeBonusPer[crystal];
            const std::int64_t price = runtime::Win64CvttDoubleToInt64(
                ((ratio + 100.0) - static_cast<double>(buySell->GetBuyTaxRate())) *
                static_cast<double>(item->m_const->m_nDefaultPrice) * 0.01 * bonus
            );

            CItemAssembler assembler(*this, *item.get(), false);
            const int assembled = assembler.AssembleItemInfo(packet);
            packet.Assemble("Q", price);
            if (assembled > 0) {
                ++count;
            }

            ++depth;
            if (depth > 0xca) {
                // Log.Add(
                //     LOG_ERROR,
                //     "depth>%d at file[%s], line[%d]",
                //     0xca,
                //     "user_item.cpp",
                //     0x2f62)
                L2_LOG_ERROR(Log, "[merchant] Item-list depth exceeds %d; list truncated.", 0xca);
                break;
            }
        }
    }
    return count;
}

short User::ItemRefundShow(CPacketHelperEx<20480>& packet) {
    // Current L2Server 0x00b88ce4, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    short count;
    {
        L2_GUARD(*m_lock);
        count = static_cast<short>(m_ItemRefunds.size());
        if (count < 1) {
            return 0;
        }

        int index = 0;
        for (ItemRefund* const refund : m_ItemRefunds) {
            CItem* const item = refund->m_pItem;
            if (!item->IsSellableToNpcStore(this, false)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"not sellable item. [%s][%d][%d]",
                //     GetName(),
                //     static_cast<int>(GetDBID()),
                //     item->m_data->m_nClassID)
                // L2_LOG_ERROR(Log,
                //     L"[merchant] Item is not sellable: user '%s' (%d), class %d. Refund list rejected.",
                //     GetName(),
                //     static_cast<int>(GetDBID()),
                //     item->m_data->m_nClassID)
                L2_LOG_ERROR(
                    Log, u"[merchant] Item is not sellable: user '%s' (%d), class %d; refund list rejected.", GetName(),
                    static_cast<int>(GetDBID()), item->m_data->m_nClassID
                );
                return 0;
            }

            CItemAssemblerAmount assembler(*item, refund->m_Amount);
            assembler.AssembleItemInfo(packet);
            packet.Assemble("dQ", index, runtime::MultiplyInt64Wrapping(refund->m_Price, refund->m_Amount));
            ++index;
        }
    }
    return count;
}

bool User::ItemRefundGet(const std::vector<int>& indices, CSPointer<CBuySell> buySell) {
    // Current L2Server 0x00b8fcb4, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (indices.empty() || indices.size() > 12U || !buySell) {
        return false;
    }

    CObject* const merchantObject = CObject::GetObjectW(static_cast<std::uint32_t>(buySell->nNPCServerId));
    CNPC* const merchant = merchantObject != nullptr && merchantObject->IsNPC() ? merchantObject->CastNPC() : nullptr;
    if (merchant == nullptr) {
        return false;
    }

    std::vector<int> sortedIndices(indices);
    std::sort(sortedIndices.begin(), sortedIndices.end());
    for (std::size_t index = 1; index < sortedIndices.size(); ++index) {
        if (runtime::CheckedVectorAt(sortedIndices, index - 1U) == runtime::CheckedVectorAt(sortedIndices, index)) {
            // Log.Add(
            //     LOG_ERROR,
            //     "item refund error : refund index overlapped.")
            L2_LOG_ERROR(Log, "[merchant] Refund indices overlap; the refund request is rejected.");
            return false;
        }
    }

    ItemAmountDataStruct items;
    std::vector<ItemRefund*> selectedRefunds;
    std::int64_t totalPrice = 0;
    {
        L2_GUARD(*m_lock);
        if (sortedIndices.front() < 0 || static_cast<std::size_t>(sortedIndices.back()) >= m_ItemRefunds.size()) {
            // Log.Add(
            //     LOG_ERROR,
            //     "item refund get index failed [%d][%d]",
            //     static_cast<int>(m_ItemRefunds.size()),
            //     static_cast<int>(sortedIndices.size()))
            L2_LOG_ERROR(
                Log, "[merchant] Refund index is outside %d stored entries (%d requested); request rejected.",
                static_cast<int>(m_ItemRefunds.size()), static_cast<int>(sortedIndices.size())
            );
            return false;
        }

        for (std::size_t order = sortedIndices.size(); order != 0; --order) {
            const int requested = runtime::CheckedVectorAt(sortedIndices, order - 1U);
            auto refundPosition = m_ItemRefunds.begin();
            std::advance(refundPosition, requested);
            ItemRefund* const refund = *refundPosition;
            items.Push(refund->m_pItem->m_nClassId, refund->m_pItem->m_data->m_nCount);
            totalPrice = runtime::AddInt64Wrapping(
                totalPrice, runtime::MultiplyInt64Wrapping(refund->m_Price, refund->m_Amount)
            );
            selectedRefunds.push_back(refund);
        }
    }

    if (!UserHelper::CheckSameStackableItem(items)) {
        return false;
    }

    CPacketHelperEx<20480> request;
    {
        L2_WRITE_GUARD(m_inv);
        if (!m_inv.CheckAddableMulti(items, false)) {
            SendSystemMessage(SystemMessageId::CANNOT_EXCEED_INVENTORY_WEIGHT_OR_QUANTITY_LIMIT);
            return false;
        }
        if (m_inv.GetAdenaAmount() < totalPrice) {
            SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
            return false;
        }

        for (ItemRefund* const refund : selectedRefunds) {
            AssembleForBuyItemWithProperties(
                *refund->m_pItem, refund->m_Amount, runtime::MultiplyInt64Wrapping(refund->m_Price, refund->m_Amount),
                request
            );
        }
    }

    db.RequestBuyItems(
        buySell->nNPCClassId, this, totalPrice, 0, buySell->m_nResidenceID, false, static_cast<int>(selectedRefunds.size()),
        request.GetLength(), request.GetPacket()
    );

    {
        L2_GUARD(*m_lock);
        for (ItemRefund* const refund : selectedRefunds) {
            const auto position = std::find(m_ItemRefunds.begin(), m_ItemRefunds.end(), refund);
            if (position != m_ItemRefunds.end()) {
                delete *position;
                m_ItemRefunds.erase(position);
            }
        }
    }
    return true;
}

int User::ShowBuySellList(const char refresh) {
    // Current L2Server 0x00b904b0, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    CSPointer<CBuySell> buySell = GetBuySell();
    if (!buySell) {
        SendBuySellFail(BuySellConst::BSFT_INVALID_DEAL);
        return 0;
    }
    // Portable: a sale or purchase leaves its changes queued, and the LU4
    // client shows neither them nor the new adena until something sends them.
    if (refresh != 0 || runtime::client_protocol::UsesLu4Protocol(GetSocket())) {
        SendItemList(true, true, nullptr);
    }

    const std::int64_t adena = m_inv.GetAdenaAmount();
    const int pointerQuotient = static_cast<int>(reinterpret_cast<std::intptr_t>(buySell.get()) / 72);
    const int inventoryCount = m_inv.GetInventoryCount();

    CPacketHelperEx<20480> buyPacket;
    const short buyCount = GetItemBuyFromNpcList(buySell->m_nItemNpcSellListID, buyPacket);
    CPacketHelperEx<20480> sellPacket;
    const short sellCount = GetItemSellToNpcList(buySell->m_nItemNpcBuyBanListID, buySell->m_dRatio, sellPacket);
    CPacketHelperEx<20480> refundPacket;
    const short refundCount = ItemRefundShow(refundPacket);

    GetSocket()->Send(
        "chdQddhb", 0xfe, 0xb8, 0, adena, pointerQuotient, inventoryCount, static_cast<int>(buyCount), buyPacket.GetLength(),
        buyPacket.GetPacket()
    );
    GetSocket()->Send(
        "chddhbhbc", 0xfe, 0xb8, 1, inventoryCount, static_cast<int>(sellCount), sellPacket.GetLength(),
        sellPacket.GetPacket(), static_cast<int>(refundCount), refundPacket.GetLength(), refundPacket.GetPacket(),
        static_cast<int>(refresh)
    );
    return static_cast<int>(buyCount) + static_cast<int>(sellCount) + static_cast<int>(refundCount);
}

bool User::CanEquip(CItem* item) {
    // Current L2Server 0x00b97524, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return false;
    }

    if (!item->CheckItemEquipCondition(this, item->m_data->GetShapeShiftedClassId())) {
        if (!m_ItemMasterBuilder) {
            if (item->m_data->GetShapeShiftedClassId() != 0) {
                GetSocket()->SendSystemMessage(
                    SystemMessageId::CANNOT_EQUIP_MODIFIED_APPEARANCE_CHECK_CHARACTER_RESTRICTIONS
                );
            }
            return false;
        }
        GetSocket()->SendSystemMessage(u"SYS", u"equip item because you are Item Master");
    }

    const int itemClassId = item->m_data->m_nClassID;
    if (m_data->m_transformID > 0 &&
        ((itemClassId >= 0x1e88 && itemClassId <= 0x1e97) || (itemClassId >= 0x2180 && itemClassId <= 0x218f))) {
        SendSystemMessage(SystemMessageId::CANNOT_EQUIP_ITEM_WITH_POSITIVE_PK_COUNT);
        return false;
    }

    if ((item->m_data->m_Slot & 0x4180) != 0) {
        // Current evaluates the first singleton expression even when the
        // character has no cursed weapon, then evaluates it again for Find.
        static_cast<void>(CCursedWeaponMgr::inst());
        if (m_nCursedWeaponClassId > 0 && CCursedWeaponMgr::inst().FindConst(itemClassId) == nullptr) {
            return false;
        }
    }

    SlotType slot = ST_UNDERWEAR;
    while (slot < ST_MAX && (ST2SBT[static_cast<int>(slot)] & item->m_data->m_Slot) == 0) {
        slot = static_cast<SlotType>(static_cast<int>(slot) + 1);
    }
    return !IsEquippedWithForcedItem(slot);
}

void User::UnequipItemWhenBeChao(int previousKarma) {
    // Current L2Server 0x00b7e724, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (previousKarma >= 0 && m_data->m_nKarma < 0) {
        for (int slot = 0; slot < 63; ++slot) {
            CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[slot]);
            if (item && !CanEquip(item.get())) {
                UnequipItem(item.get(), false);
            }
        }
    }
}

void User::UpdateSlotItemInInventory(const SlotType slotType, const InventoryUpdateType updateType) {
    // Current L2Server 0x00b7e5b0, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[static_cast<int>(slotType)]);
    if (item && m_inv.Have(item->m_data->index)) {
        m_inv.SetInventoryChanged(item.get(), updateType);
        m_inv.AddSlotInfo(-1, item.get());
    }
}

bool User::BuyMonRaceTicket(const int itemClassId, const int raceId, const int laneAndFee, const int amount) {
    // Current L2Server 0x00b79aa0, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
    if (object == nullptr || !object->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item[%d] is not exist.",
        //     L"user_item.cpp",
        //     0x252d,
        //     itemClassId)
        L2_LOG_ERROR(
            Log, u"[monster race] Ticket item class %d is undefined or is not an item; purchase rejected.", itemClassId
        );
        return false;
    }
    CItem* const ticket = static_cast<CItem*>(object);

    if (!m_inv.CheckAddable(itemClassId, 1, false)) {
        SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
        return false;
    }
    if (CSharedItemData::IsStackable(ticket->m_data->m_ItemConsumeType)) {
        return false;
    }

    int tax = 0;
    const int taxRate = g_MonRaceDB.GetTaxRate();
    if (taxRate != 0) {
        tax = runtime::MultiplyInt32Wrapping(taxRate, amount) / 100;
    }
    const int remoteFee = g_MonRaceDB.GetRaceFee(GetPos(), amount);
    const std::int64_t total =
        static_cast<std::int64_t>(runtime::AddInt32Wrapping(runtime::AddInt32Wrapping(remoteFee, tax), amount));

    CItem* const adena = m_inv.GetFirstItemByClassID(0x39, false, false);
    if (adena == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item classid[%d] is not exist.",
        //     L"user_item.cpp",
        //     0x2550,
        //     0x39)
        L2_LOG_ERROR(Log, u"[monster race] Character %u has no adena item in inventory; ticket purchase rejected.", Id());
        return false;
    }
    if (adena->m_data->m_nCount < total) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] adena is not enough [%I64d] < [%I64d].",
        //     L"user_item.cpp",
        //     0x2556,
        //     adena->m_data->m_nCount,
        //     total)
        L2_LOG_ERROR(
            Log, u"[monster race] Character %u has %I64d adena but ticket purchase requires %I64d; request rejected.", Id(),
            adena->m_data->m_nCount, total
        );
        SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
        return false;
    }

    std::array<char, 20480> packet;
    int packetSize = 0;
    int itemCount = 0;
    ItemOptionKey optionKey{};
    if (MsgHelper_ManipulateItem(
            packet.data(), static_cast<int>(packet.size()), packetSize, adena, runtime::NegateInt64Wrapping(total),
            ItemGetReason::Etc, raceId, laneAndFee, amount, optionKey, 0, 0, -1
        ) != 0) {
        itemCount = 1;
    }
    optionKey = ItemOptionKey{};
    if (MsgHelper_ManipulateItem(
            packet.data(), static_cast<int>(packet.size()), packetSize, ticket, 1, ItemGetReason::Etc, raceId, laneAndFee,
            amount, optionKey, 0, 0, -1
        ) != 0) {
        ++itemCount;
    }
    db.CreateUpdateItems(this, itemCount, packetSize, packet.data(), nullptr, 0, nullptr, false);
    return true;
}

void User::SaveDurationItem(CItem* item) {
    // Current L2Server 0x00b79eec, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (item->IsDuration() && item->GetDuration() < 0) {
        db.RequestDestroyItem(item, 1, this, 0);
        return;
    }
    db.UpdateItemDuration(*this, item->GetDBID(), item->GetDuration());
}

int User::DecreaseEquippedItemDurationOnTimerExpired() {
    // Current L2Server.exe 0x00b8e9e8.
    L2_GUARD_FUNCTION();
    std::set<int> processedItemIds;

    for (int slot = 0; slot < 63; ++slot) {
        const int itemIndex = m_data->m_nSlotIndex[slot];
        if (itemIndex == 0) {
            continue;
        }

        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
        if (!item) {
            continue;
        }

        const int itemId = static_cast<int>(item->Id());
        if (processedItemIds.find(itemId) != processedItemIds.end()) {
            continue;
        }
        if (!item->IsDuration() || !item->IsTickToExpire(m_nTickCount)) {
            continue;
        }

        const int duration = item->GetDuration();
        unsigned int messageId = 0;
        if (duration == 10) {
            messageId = SystemMessageId::ITEM_REMAINING_MANA_TEN;
        } else if (duration == 5) {
            messageId = SystemMessageId::ITEM_REMAINING_MANA_FIVE;
        } else if (duration == 1) {
            messageId = SystemMessageId::ITEM_REMAINING_MANA_ONE_WILL_DISAPPEAR;
        }
        if (messageId != 0) {
            GetSocket()->Send("chccd", 0x62, messageId, 1, 3, item->m_data->m_nClassID);
        }

        item->DecreaseDurationOneUnit();
        if (item->GetDuration() > 0) {
            m_inv.SetInventoryChanged(item.get(), IUT_UPDATE);
        }
        SaveDurationItem(item.get());
        processedItemIds.insert(itemId);
    }

    const int changedCount = static_cast<int>(processedItemIds.size());
    if (changedCount > 0) {
        m_data->m_NeedUpdatePacketBitSet.SetBit(0x49);
        SendItemList(true, true, nullptr);
    }
    return changedCount;
}

bool User::_AddEquipDurationItem(CItem* item) {
    // Current L2Server 0x00b8593c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return false;
    }

    const int itemClassId = item->m_data->m_nClassID;
    const int itemIndex = item->m_data->index;
    const auto range = m_mmapEquipDurationItem.equal_range(itemClassId);
    for (auto current = range.first; current != range.second; ++current) {
        if (current->second == itemIndex) {
            return false;
        }
    }
    m_mmapEquipDurationItem.emplace(itemClassId, itemIndex);
    return true;
}

int User::EquipItem(CItem* item, SlotType slotType) {
    // Current L2Server 0x00b97704, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 1) == 1) {
        return -1;
    }

    bool equipped = false;
    do {
        const int itemClassId = item->m_data->m_nClassID;
        const std::uint64_t itemSlotMask = static_cast<std::uint64_t>(item->m_data->m_Slot);

        if (IsRiding() && (itemSlotMask & 0x4180U) != 0) {
            if (CCursedWeaponMgr::inst().FindConst(itemClassId) == nullptr) {
                break;
            }
            TryDismounting(true, true);
        }

        if (item->m_const->m_bForNpc) {
            SendSystemMessage(SystemMessageId::CANNOT_EQUIP_PET_ITEM);
            break;
        }
        if (!CanEquip(item)) {
            SendSystemMessage(SystemMessageId::CANNOT_EQUIP_ITEM_REQUIREMENTS_NOT_MET);
            break;
        }
        if (GetPrivateStoreType() != PST_NONE) {
            SendSystemMessage(SystemMessageId::CANNOT_EQUIP_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP);
            break;
        }

        if (slotType == ST_MAX) {
            slotType = ST_UNDERWEAR;
            while (slotType < ST_MAX && (ST2SBT[static_cast<int>(slotType)] & item->m_data->m_Slot) == 0) {
                slotType = static_cast<SlotType>(static_cast<int>(slotType) + 1);
            }
        }
        if (static_cast<std::uint32_t>(slotType) >= ST_MAX) {
            break;
        }

        int previousArrowClassId = 0;
        int previousBoltClassId = 0;
        CSPointer<CItem> previousLeftHand = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[ST_LHAND]);
        if (previousLeftHand && m_inv.Have(m_data->m_nSlotIndex[ST_LHAND])) {
            if (previousLeftHand->m_const->m_EtcitemType == EIT_ARROW) {
                previousArrowClassId = previousLeftHand->m_data->m_nClassID;
            } else if (previousLeftHand->m_const->m_EtcitemType == EIT_BOLT) {
                previousBoltClassId = previousLeftHand->m_data->m_nClassID;
            }
        }

        if (item->m_const->m_EtcitemType == EIT_ARROW) {
            CSPointer<CItem> weapon = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[ST_RLHAND]);
            if (!m_inv.Have(m_data->m_nSlotIndex[ST_RLHAND]) || !weapon || weapon->m_const->m_AttackType != AT_BOW) {
                break;
            }
        }
        if (item->m_const->m_EtcitemType == EIT_BOLT) {
            CSPointer<CItem> weapon = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[ST_RLHAND]);
            if (!m_inv.Have(m_data->m_nSlotIndex[ST_RLHAND]) || !weapon ||
                (weapon->m_const->m_AttackType != AT_CROSSBOW && weapon->m_const->m_AttackType != AT_TWOHANDCROSSBOW)) {
                break;
            }
        }

        if ((itemSlotMask & (ST2SBT[ST_REAR] | ST2SBT[ST_LEAR])) != 0) {
            if (m_data->m_nSlotIndex[ST_REAR] == -1) {
                slotType = ST_REAR;
            } else if (m_data->m_nSlotIndex[ST_LEAR] == -1) {
                slotType = ST_LEAR;
            } else {
                CSPointer<CItem> rightEar = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[ST_REAR]);
                slotType = rightEar && rightEar->m_nClassId == item->m_nClassId ? ST_LEAR : ST_REAR;
            }
        } else if ((itemSlotMask & (ST2SBT[ST_RFINGER] | ST2SBT[ST_LFINGER])) != 0) {
            if (m_data->m_nSlotIndex[ST_RFINGER] == -1) {
                slotType = ST_RFINGER;
            } else if (m_data->m_nSlotIndex[ST_LFINGER] == -1) {
                slotType = ST_LFINGER;
            } else {
                CSPointer<CItem> rightFinger = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[ST_RFINGER]);
                slotType = rightFinger && rightFinger->m_nClassId == item->m_nClassId ? ST_LFINGER : ST_RFINGER;
            }
        }

        UpdateSlotItemInInventory(slotType, IUT_UPDATE);
        if ((slotType >= ST_HEAD && slotType <= ST_RLHAND) || slotType == ST_ONEPIECE) {
            UpdateSlotItemInInventory(ST_ALLDRESS, IUT_UPDATE);
        }
        if (slotType == ST_ALLDRESS) {
            for (int slot = ST_HEAD; slot <= ST_ONEPIECE; ++slot) {
                UpdateSlotItemInInventory(static_cast<SlotType>(slot), IUT_UPDATE);
            }
        }
        if (slotType == ST_RLHAND) {
            UpdateSlotItemInInventory(ST_RHAND, IUT_UPDATE);
            UpdateSlotItemInInventory(ST_LHAND, IUT_UPDATE);
        }
        if (slotType == ST_RHAND || slotType == ST_LHAND) {
            UpdateSlotItemInInventory(ST_RLHAND, IUT_UPDATE);
        }
        if (slotType == ST_ONEPIECE) {
            UpdateSlotItemInInventory(ST_CHEST, IUT_UPDATE);
            UpdateSlotItemInInventory(ST_LEGS, IUT_UPDATE);
        }
        if (slotType == ST_CHEST || slotType == ST_LEGS) {
            UpdateSlotItemInInventory(ST_ONEPIECE, IUT_UPDATE);
        }
        if (slotType == ST_HAIRALL) {
            UpdateSlotItemInInventory(ST_HAIR, IUT_UPDATE);
            UpdateSlotItemInInventory(ST_HAIR2, IUT_UPDATE);
        }
        if (slotType == ST_HAIR || slotType == ST_HAIR2) {
            UpdateSlotItemInInventory(ST_HAIRALL, IUT_UPDATE);
        }

        if (CCreature::EquipItem(item, slotType) == 0) {
            break;
        }

        if (item->IsWeapon() || m_data->m_nSlotIndex[ST_RHAND] == -1) {
            if (m_data->m_nSoulShot != 0 || m_bWaitSoulShotUse) {
                SendSystemMessage(SystemMessageId::SOULSHOT_POWER_DISABLED);
                m_bWaitSoulShotUse = false;
            }
            TurnOffSoulshot(false);
            if (m_data->m_bSpiritShot || m_bWaitSpiritShotUse) {
                SendSystemMessage(SystemMessageId::SPIRITSHOT_POWER_DISABLED);
                m_bWaitSpiritShotUse = false;
            }
            TurnOffSpiritshot(false);
            TurnOffFishingShot(false);
        }

        SendSystemMessageEquipItem(item);
        {
            L2_WRITE_GUARD(m_inv);
            m_inv.SetInventoryChanged(item, IUT_UPDATE);

            if (slotType == ST_RHAND || slotType == ST_LHAND || slotType == ST_RLHAND) {
                if (item->m_const->m_AttackType == AT_BOW) {
                    const int lastArrow = CheckLastArrow(item->m_const->GetCrystalType());
                    if (lastArrow == -1) {
                        CItem* const arrow = m_inv.GetArrowByCrystal(item->m_const->GetCrystalType());
                        if (arrow != nullptr) {
                            m_data->m_nSlotIndex[ST_LHAND] = arrow->m_data->index;
                            m_data->m_iLastArrowIndex = arrow->m_data->index;
                            m_inv.SetInventoryChanged(arrow, IUT_UPDATE);
                        }
                    } else {
                        m_data->m_nSlotIndex[ST_LHAND] = lastArrow;
                        CSPointer<CItem> arrow = CItemFactory::Inst().FindObjectSP(lastArrow);
                        m_inv.SetInventoryChanged(arrow.get(), IUT_UPDATE);
                    }
                } else if (item->m_const->m_EtcitemType != EIT_ARROW && previousArrowClassId > 0) {
                    CSPointer<CItem> arrow = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[ST_LHAND]);
                    if (arrow && arrow->m_const->m_EtcitemType == EIT_ARROW) {
                        CCreature::UnequipItem(arrow.get(), false);
                        m_inv.SetInventoryChanged(arrow.get(), IUT_UPDATE);
                    }
                }

                if (item->m_const->m_AttackType == AT_CROSSBOW || item->m_const->m_AttackType == AT_TWOHANDCROSSBOW) {
                    const int lastBolt = CheckLastBolt(item->m_const->GetCrystalType());
                    if (lastBolt == -1) {
                        CItem* const bolt = m_inv.GetBoltByCrystal(item->m_const->GetCrystalType());
                        if (bolt != nullptr) {
                            m_data->m_nSlotIndex[ST_LHAND] = bolt->m_data->index;
                            m_data->m_iLastArrowIndex = bolt->m_data->index;
                            m_inv.SetInventoryChanged(bolt, IUT_UPDATE);
                        }
                    } else {
                        m_data->m_nSlotIndex[ST_LHAND] = lastBolt;
                        CSPointer<CItem> bolt = CItemFactory::Inst().FindObjectSP(lastBolt);
                        m_inv.SetInventoryChanged(bolt.get(), IUT_UPDATE);
                    }
                } else if (item->m_const->m_EtcitemType != EIT_BOLT && previousBoltClassId > 0) {
                    CSPointer<CItem> bolt = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[ST_LHAND]);
                    if (bolt && bolt->m_const->m_EtcitemType == EIT_BOLT) {
                        CCreature::UnequipItem(bolt.get(), false);
                        m_inv.SetInventoryChanged(bolt.get(), IUT_UPDATE);
                    }
                }

                if (item->m_const->m_EtcitemType != EIT_LURE) {
                    CSPointer<CItem> lure = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[ST_LHAND]);
                    if (lure && m_inv.Have(m_data->m_nSlotIndex[ST_LHAND]) && lure->m_const->m_EtcitemType == EIT_LURE) {
                        CCreature::UnequipItem(lure.get(), false);
                        m_inv.SetInventoryChanged(lure.get(), IUT_UPDATE);
                    }
                }
            }

            if (item->IsDuration()) {
                item->SetDurationCommitTick(static_cast<int>(m_nLastTick));
                item->DecreaseDurationOneUnit();
                SaveDurationItem(item);
                _AddEquipDurationItem(item);
            }
        }

        if (CCursedWeaponMgr::inst().FindConst(itemClassId) == nullptr) {
            SetAllOverPointToDefault();
        } else {
            m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dHP, m_data->m_dBaseHP, 0x0b);
            m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dMP, m_data->m_dBaseMP, 0x0c);
            m_data->m_NeedUpdatePacketBitSet.SetDoubleValue_IfModifed(m_data->m_dCP, m_data->m_dBaseCP, 0x0d);
            SetKarma(-100000000);
        }

        if (slotType == ST_HAIR || slotType == ST_HAIR2 || slotType == ST_HAIRALL) {
            m_data->m_NeedUpdatePacketBitSet.SetBit(0x12);
            m_data->m_NeedUpdatePacketBitSet.SetBit(0x15);
            m_data->m_NeedUpdatePacketBitSet.SetBit(0x13);
            m_data->m_NeedUpdatePacketBitSet.SetBit(0x16);
        }
        m_data->m_NeedUpdatePacketBitSet.SetBit(0x49);

        std::array<char16_t, 256> attributeInfo{};
        std::array<char16_t, 256> optionInfo{};
        std::array<char16_t, 256> ensoulInfo{};
        item->m_itemBaseAttribute.GetLogString(attributeInfo.data());
        FormatUtf16To(
            optionInfo.data(), optionInfo.size(), u"%d:%d:%d", item->m_itemOption.intensiveItemClassID,
            item->m_itemOption.itemOptionKey.variationOption1, item->m_itemOption.itemOptionKey.variationOption2
        );
        item->MakeEnsoulLogInfo(ensoulInfo.data());

        const std::int64_t firstItemDbId = item->GetDBID();
        const std::int64_t secondItemDbId = item->GetDBID();
        CLogFormatter formatter;
        formatter.WriteLog1_1(
            0x338, this, nullptr, ensoulInfo.data(), attributeInfo.data(), optionInfo.data(), 0, itemClassId,
            static_cast<int>(slotType), firstItemDbId, item->m_data->GetEnchanted(), 0, secondItemDbId
        );
        if (item->IsDuration()) {
            const std::int64_t durationItemDbId = item->GetDBID();
            formatter.WriteLog4(
                0x3d4, this, nullptr, nullptr, nullptr, nullptr, item->m_nClassId, item->GetDuration(), 0, 0, 0, 0, 0, 0, 0,
                0, durationItemDbId
            );
        }

        equipped = true;
    } while (false);

    runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
    if (!equipped) {
        return 0;
    }

    if (IsBuilder() && m_ShowEquipItemInfo) {
        item->ShowEquipItemInfo(this);
    }
    db.SaveCharacterSlotInfo(this);
    ValidateParameters(VB_WEAPONARMOR | VB_ITEMSKILL | VB_SKILLLIST | VB_SKILLMOD | VB_HPMPCP | VB_SIM | VB_SPEED, false);
    SendUserInfo(nullptr, nullptr);
    ParamChanged(std::int64_t{PCB_CHAR_EQUIPSLOT});
    SendETCBuffStatusInfo();
    SendItemList(true, true, nullptr);
    return 1;
}

SlotType User::UnequipItem(SlotType slotType, bool force) {
    // Current L2Server 0x00b86f5c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (slotType >= ST_MAX) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"User::UnequipItem. slot[%d] is not valid",
        //     static_cast<int>(slotType))
        L2_LOG_ERROR(Log, u"[equipment] Cannot unequip item: slot %d is invalid.", static_cast<int>(slotType));
        return ST_MAX;
    }

    if (runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 1) == 1) {
        {
            L2_GUARD(m_UnEquipQueueLock);
            m_UnEquipItemQueue.push(SaveUnEquipItemQueue(0, slotType, force));
        }
        return ST_NONE;
    }

    if (slotType == ST_LHAND) {
        CSPointer<CItem> leftHand = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[ST_LHAND]);
        if (leftHand && (leftHand->m_const->m_EtcitemType == EIT_ARROW || leftHand->m_const->m_EtcitemType == EIT_BOLT)) {
            runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
            return ST_MAX;
        }
    }

    if (slotType == ST_WAIST || slotType == ST_BROOCH) {
        if (GetInventoryLimit() - GetBeltInventoryLimit() < m_inv.GetTotalCount()) {
            runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
            SendSystemMessage(SystemMessageId::CANNOT_CHANGE_BELT_OR_BROOCH_WITH_EXPANDED_INVENTORY_OCCUPIED);
            return ST_MAX;
        }
    }

    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[static_cast<int>(slotType)]);
    runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
    return item ? UnequipItem(item.get(), false) : ST_NONE;
}

SlotType User::UnequipItem(CItem* item, bool force) {
    // Current L2Server 0x00b90c4c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        // Log.Add(LOG_ERROR, L"user::unequipitem pItem is NULL")
        L2_LOG_ERROR(Log, u"[equipment] Cannot unequip item: item pointer is null.");
        return ST_MAX;
    }

    bool equipped = false;
    for (int slot = 0; slot < ST_MAX; ++slot) {
        if (m_data->m_nSlotIndex[slot] == item->m_data->index) {
            equipped = true;
            break;
        }
    }
    if (!equipped) {
        return ST_MAX;
    }

    if (runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 1) == 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][SetUseWarehouseWait() "
        //     L"m_UnEquipItemQueue.push] DBID[%d]",
        //     L"user_item.cpp",
        //     0x529,
        //     static_cast<int>(GetDBID()))
        // Noise: queuing an unequip while an item job owns the character is
        // expected serialization, not an operator-visible failure.
        // L2_LOG_DEBUG(Log,
        //     L"[equipment] Character %u queued item %u for unequip while another item job is active.",
        //     GetDBID(),
        //     item->GetDBID())
        {
            L2_GUARD(m_UnEquipQueueLock);
            m_UnEquipItemQueue.push(SaveUnEquipItemQueue(item->Id(), ST_MAX, force));
        }
        return ST_NONE;
    }

    const bool agathionBusy = runtime::raw_atomic::CompareExchangeI32(&m_lAgathionFlag, 1, 1) == 1;
    if (agathionBusy && m_data->m_nSlotIndex[ST_AGATHION_MAIN] == item->m_data->index) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][m_lAgathionFlag "
        //     L"m_UnEquipItemQueue.push] DBID[%d]",
        //     L"user_item.cpp",
        //     0x533,
        //     static_cast<int>(GetDBID()))
        // Noise: the agathion update defers this unequip, so the
        // queue transition is expected and can occur frequently.
        // L2_LOG_DEBUG(Log,
        //     L"[equipment] Character %u queued agathion item %u for unequip until its update completes.",
        //     GetDBID(),
        //     item->GetDBID())
        {
            L2_GUARD(m_UnEquipQueueLock);
            m_UnEquipItemQueue.push(SaveUnEquipItemQueue(item->Id(), ST_MAX, force));
        }
        runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
        return ST_NONE;
    }

    const SlotType unequippedSlot = CCreature::UnequipItem(item, force);

    if (item->IsWeapon()) {
        if (m_data->m_nSoulShot == 0) {
            if (m_bWaitSoulShotUse) {
                SendSystemMessage(SystemMessageId::SOULSHOT_POWER_DISABLED);
                m_bWaitSoulShotUse = false;
            }
        } else {
            SendSystemMessage(SystemMessageId::SOULSHOT_POWER_DISABLED);
            m_bWaitSoulShotUse = false;
            TurnOffSoulshot(false);
        }

        if (!m_data->m_bSpiritShot) {
            if (m_bWaitSpiritShotUse) {
                SendSystemMessage(SystemMessageId::SPIRITSHOT_POWER_DISABLED);
                m_bWaitSpiritShotUse = false;
            }
        } else {
            SendSystemMessage(SystemMessageId::SPIRITSHOT_POWER_DISABLED);
            m_bWaitSpiritShotUse = false;
            TurnOffSpiritshot(false);
        }

        if (m_data->m_nFishingShot != 0 || m_bWaitFishingShotUse) {
            m_data->m_nFishingShot = 0;
            SendSystemMessage(SystemMessageId::SPIRITSHOT_POWER_DISABLED);
            m_bWaitFishingShotUse = false;
        }
    }

    if (unequippedSlot != ST_MAX && unequippedSlot != ST_NONE) {
        {
            L2_WRITE_GUARD(m_inv);
            m_inv.SetInventoryChanged(item, IUT_UPDATE);
        }

        const AttackType attackType = item->m_const->m_AttackType;
        CItem* ammunition = nullptr;
        [[maybe_unused]] int changedLine = 0;
        if (attackType == AT_BOW) {
            {
                L2_READ_GUARD(m_inv);
                ammunition = m_inv.GetArrowByCrystal(item->m_const->GetCrystalType());
            }
            changedLine = 0x581;
        } else if (attackType == AT_CROSSBOW || attackType == AT_TWOHANDCROSSBOW) {
            {
                L2_READ_GUARD(m_inv);
                ammunition = m_inv.GetBoltByCrystal(item->m_const->GetCrystalType());
            }
            changedLine = 0x592;
        }
        if (ammunition != nullptr) {
            CCreature::UnequipItem(ammunition, false);
            {
                L2_WRITE_GUARD(m_inv);
                m_inv.SetInventoryChanged(ammunition, IUT_UPDATE);
            }
        }

        if (attackType == AT_FISHINGROD) {
            CSPointer<CItem> lure = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[ST_LHAND]);
            if (lure && m_inv.Have(lure->m_data->index) && lure->m_const->m_EtcitemType == EIT_LURE) {
                CCreature::UnequipItem(lure.get(), false);
                {
                    L2_WRITE_GUARD(m_inv);
                    m_inv.SetInventoryChanged(lure.get(), IUT_UPDATE);
                }
            }
        }

        if (unequippedSlot == ST_ARTIFACTBOOK) {
            for (int slot = ST_ARTIFACT_A1; slot < ST_MAX; ++slot) {
                const int itemIndex = m_data->m_nSlotIndex[slot];
                if (itemIndex == -1) {
                    continue;
                }
                CItem* const artifact = m_inv.GetByIndex(itemIndex);
                if (artifact == nullptr) {
                    continue;
                }
                CCreature::UnequipItem(artifact, false);
                {
                    L2_WRITE_GUARD(m_inv);
                    m_inv.SetInventoryChanged(artifact, IUT_UPDATE);
                }
                SendSystemMessageUnEquipItem(artifact, static_cast<SlotType>(slot));
            }
            m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_SkillMod.m_nActiveArtifactSlotGroup, 0, 0x66);
        }

        SendItemList(true, true, nullptr);
        SendSystemMessageUnEquipItem(item, unequippedSlot);

        if (unequippedSlot == ST_HAIR || unequippedSlot == ST_HAIR2 || unequippedSlot == ST_HAIRALL) {
            m_data->m_NeedUpdatePacketBitSet.SetBit(0x12);
            m_data->m_NeedUpdatePacketBitSet.SetBit(0x15);
            m_data->m_NeedUpdatePacketBitSet.SetBit(0x13);
            m_data->m_NeedUpdatePacketBitSet.SetBit(0x16);
        }

        if (unequippedSlot == ST_RBRACELET) {
            for (int slot = ST_DECO1; slot <= ST_DECO6; ++slot) {
                UnequipItem(static_cast<SlotType>(slot), false);
            }
            m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_SkillMod.m_dMaxDecoSlotCount, 0, 0x36);
        }
        if (unequippedSlot == ST_BROOCH) {
            for (int slot = ST_JEWEL1; slot <= ST_JEWEL6; ++slot) {
                UnequipItem(static_cast<SlotType>(slot), false);
            }
            m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_SkillMod.m_dMaxJewelSlotCount, 0, 0x37);
        }
        if (unequippedSlot == ST_LBRACELET) {
            for (int slot = ST_AGATHION_MAIN; slot <= ST_AGATHION_SUB4; ++slot) {
                UnequipItem(static_cast<SlotType>(slot), false);
            }
            m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_SkillMod.m_dMaxAgathionMainSlotCount, 0, 0x38);
            m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_SkillMod.m_dMaxAgathionSubSlotCount, 0, 0x39);
        }

        m_data->m_NeedUpdatePacketBitSet.SetBit(0x49);
        SendUserInfo(nullptr, nullptr);
        ParamChanged(std::int64_t{PCB_CHAR_EQUIPSLOT});
        SendETCBuffStatusInfo();
    }

    runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
    db.SaveCharacterSlotInfo(this);
    ValidateChanged(VB_MAX);
    return unequippedSlot;
}

bool User::UseItem(CItem& item, const int count, const bool force, const int unknown) {
    // Current L2Server 0x00b9172c, original user_item.cpp owner.
    L2_GUARD_FUNCTION();
    L2_DECLARE_AUTO_TIMER(timer, JOB_USER_USEITEM);

    if (item.m_const->m_enumDefaultAction == ITEM_ACTION_EQUIP) {
        const ActionType action = GetController()->GetCurrentActionType();
        if (static_cast<unsigned int>(action - ACT_ATTACK) < 2U) {
            if (GetController()->GetCurrentActionState() != ActionState::End) {
                static_cast<void>(runtime::raw_atomic::CompareExchangeI32(
                    &m_PendingEquipItemID, static_cast<std::int32_t>(item.Id()), 0
                ));
                return false;
            }

            const std::int32_t pending = runtime::raw_atomic::LoadI32Relaxed(&m_PendingEquipItemID);
            if (runtime::raw_atomic::CompareExchangeI32(&m_PendingEquipItemID, 0, pending) == 0) {
                return false;
            }
        }

        if (m_pFishingController != nullptr &&
            (m_pFishingController->m_FishingState != FISHINGS_NONE || m_pFishingController->m_bIsAutoFishing == true)) {
            SendSystemMessage(SystemMessageId::CANNOT_EQUIP_UNEQUIP_DESTROY_OR_DROP_ITEMS_WHILE_FISHING);
            return false;
        }
    }

    CTransform* const transform = IsTransformed(TransformType::Max);
    if (transform != nullptr && !transform->CanUseItem(item.m_data->m_nClassID)) {
        SendSystemMessage(SystemMessageId::ITEM_CANNOT_BE_USED_IN_CURRENT_TRANSFORMATION);
        return false;
    }

    const int itemClassId = item.m_data->m_nClassID;
    if (itemClassId == 0x18f7) {
        CNPC* const npc = m_spTalkTarget.GetNPC();
        if (npc == nullptr ||
            (npc->m_nClassId != 0xf7426 && npc->m_nClassId != 0xf7429 && npc->m_nClassId != 0xf7617 &&
                npc->m_nClassId != 0xf761b)) {
            SendSystemMessage(SystemMessageId::INVALID_TARGET);
            return false;
        }
    }
    if (itemClassId == 0x92a7) {
        CNPC* const npc = m_spTalkTarget.GetNPC();
        if (npc == nullptr || (npc->m_nClassId != 0xf7697 && npc->m_nClassId != 0xf769b)) {
            SendSystemMessage(SystemMessageId::INVALID_TARGET);
            return false;
        }
    }

    const bool restrictedVehicleItem = itemClassId == 0x18f5 || itemClassId == 0x18f6 || itemClassId == 0x2990 ||
        itemClassId == 0x2991 || itemClassId == 0x3c06 || itemClassId == 0x3c07 || itemClassId == 0x3c08 ||
        itemClassId == 0x3c09;
    if (restrictedVehicleItem && (IsAirShip() || IsVehicle())) {
        SendSystemMessage(SystemMessageId::NAMED_ACTION_CONDITIONS_NOT_MET);
        return false;
    }

    if (m_EventMatchUser.m_byEventMatchTeamId != 0) {
        CSPointer<CEventMatch> event = m_EventMatchUser.GetEventMatch();
        if (event) {
            const int allowed = [&] {
                L2_GUARD(event->m_csLock);
                return event->m_ItemRule.IsAllow(itemClassId);
            }();
            if (allowed == 0) {
                return false;
            }
        }
    }

    if (IsOlympiadPlayer() && !item.m_const->m_bCanOlympiadUse) {
        SendSystemMessage(SystemMessageId::ITEM_CANNOT_BE_USED_IN_OLYMPIAD);
        return false;
    }
    if (m_PledgeGameUser.IsPlayer() && item.m_const->m_bCanNotPledgeGameUse) {
        SendSystemMessage(SystemMessageId::ITEM_CANNOT_BE_USED_IN_MATCH);
        return false;
    }
    if (!item.CanUseWithCrystalType(static_cast<CrystalType>(BaseCrystal[GetOriginalCrystal()]))) {
        SendSystemMessage(SystemMessageId::CANNOT_USE_ITEM_INCOMPATIBLE_GRADE);
        return false;
    }

    if (IsRiding() && item.m_const->m_bForNpc) {
        if (!CheckRidingFoodItem(GetStriderClassID(), itemClassId)) {
            return false;
        }
    } else if (!item.CheckItemUseCondition(this)) {
        GetSocket()->Send("chccd", 0x62, SystemMessageId::NAMED_ACTION_CONDITIONS_NOT_MET, 1, 3, itemClassId);
        if (!m_ItemMasterBuilder) {
            return false;
        }
        GetSocket()->SendSystemMessage(u"SYS", u"use item because you are Item Master");
    }

    if (force) {
        return true;
    }
    return CCreature::UseItem(item, count, false, unknown);
}

bool User::IsItemUsable(const int itemClassId) {
    // Current L2Server 0x00b801a0, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!IsAlive()) {
        return false;
    }
    if (m_data->m_bGMInvisible) {
        SendSystemMessage(u"SYS", u"You cannot do this action in hide mode.");
        return false;
    }
    if (IsNowTrade()) {
        return false;
    }

    if (!m_SkillMod.m_bCanAct) {
        const bool allowed = [&] {
            L2_GUARD(m_UsedItemsSect);
            const auto found = m_UsedItemsLastTimeMap.find(itemClassId);
            return found != m_UsedItemsLastTimeMap.end() && found->second > 0;
        }();
        if (!allowed) {
            // ActionFailed(0)
            ActionFailed(AF_DEFAULT);
        }
        return allowed;
    }

    if (m_bFlyMoving) {
        SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_USING_SAYUNE);
        return false;
    }
    return true;
}

bool User::CheckCrystalItem(CItem* const item, int) {
    // Current L2Server 0x00b33460. The item-type argument is unused.
    L2_GUARD_FUNCTION();
    if (!item->IsDestruct()) {
        SendSystemMessage(SystemMessageId::CANNOT_DISCARD_QUEST_ITEM);
        return false;
    }
    if (IsNowTrade() || m_data->m_nPrivateStoreType != 0) {
        SendSystemMessage(SystemMessageId::CANNOT_DROP_OR_DESTROY_ITEMS_DURING_PRIVATE_STORE_OR_WORKSHOP);
        return false;
    }
    if (!IsItemUsable(0)) {
        return false;
    }
    if (!item->CanCrystalized() || item->m_data->m_hLocked != 0) {
        SendSystemMessage(SystemMessageId::ITEM_CANNOT_BE_CRYSTALLIZED);
        return false;
    }

    if (BaseCrystal[static_cast<int>(m_CrystallizeLevel)] < BaseCrystal[static_cast<int>(item->m_const->GetCrystalType())]) {
        SendSystemMessage(SystemMessageId::CRYSTALLIZATION_SKILL_LEVEL_TOO_LOW);
        return false;
    }

    if (static_cast<float>(m_inv.GetTotalCount()) <= static_cast<float>(GetInventoryLimit()) * 0.9F &&
        // m_nCurrentWeight < 3) {
        m_nCurrentWeight < WEIGHT_OVERWEIGHT) {
        return true;
    }
    // BUG in shipping EXE at 00b33612: crystallization checks ordinary inventory, reports quest inventory.
    // SendSystemMessage(SystemMessageId::QUEST_INVENTORY_QUANTITY_MUST_BE_BELOW_90_PERCENT)
    SendSystemMessage(SystemMessageId::INVENTORY_WEIGHT_MUST_BE_BELOW_80_AND_QUANTITY_BELOW_90_PERCENT);
    return false;
}

bool User::CheckCombinationItemJob() {
    // Current L2Server 0x00b7b9cc, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!IsActive()) {
        SendSystemMessage(SystemMessageId::CANNOT_PROCEED_SYSTEM_ERROR);
        return false;
    }
    if (!IsAlive()) {
        SendSystemMessage(SystemMessageId::PROCESSING_FAILED_CHARACTER_DEAD);
        return false;
    }
    if (m_data->m_nPrivateStoreType == PST_MANAGE || GetPrivateStoreType() != PST_NONE) {
        SendSystemMessage(SystemMessageId::CANNOT_ACT_DURING_PRIVATE_STORE_OR_WORKSHOP);
        return false;
    }
    if (GetPendingUseETCItem(PUIT_COMMISSION) != nullptr) {
        SendSystemMessage(SystemMessageId::CANNOT_ACT_DURING_COMMISSION_SALE);
        return false;
    }
    if (GetPendingUseETCItem(PUIT_CRYSTAL) != nullptr) {
        SendSystemMessage(SystemMessageId::CANNOT_ACT_DURING_CRYSTALLIZATION);
        return false;
    }
    if (GetPendingUseETCItem(PUIT_HLOCKED) != nullptr) {
        SendSystemMessage(SystemMessageId::SEAL_OR_UNSEAL_IN_PROGRESS);
        return false;
    }
    if (GetPendingUseETCItem(PUIT_ENCHANT) != nullptr || GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT) != nullptr) {
        SendSystemMessage(SystemMessageId::ENCHANT_OR_ATTRIBUTE_ENCHANT_IN_PROGRESS);
        return false;
    }
    if (GetPendingUseETCItem(PUIT_ATTRIBUTE_CHANGE) != nullptr) {
        SendSystemMessage(SystemMessageId::ATTRIBUTE_CHANGE_IN_PROGRESS);
        return false;
    }
    if (GetPendingUseETCItem(PUIT_SHAPE_SHIFTING) != nullptr) {
        SendSystemMessage(SystemMessageId::ITEM_APPEARANCE_MODIFICATION_OR_RESTORATION_IN_PROGRESS);
        return false;
    }
    if (IsNowTrade()) {
        SendSystemMessage(SystemMessageId::CANNOT_ACT_DURING_TRADE);
        return false;
    }
    if (m_lWaitVariation == 1) {
        SendSystemMessage(SystemMessageId::ANOTHER_OPERATION_IN_PROGRESS);
        return false;
    }
    return true;
}

bool User::CheckArtifactEnchantJob() {
    // Current L2Server 0x00b7c128, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!IsActive()) {
        SendSystemMessage(SystemMessageId::CANNOT_PROCEED_SYSTEM_ERROR);
        return false;
    }
    if (!IsAlive()) {
        SendSystemMessage(SystemMessageId::PROCESSING_FAILED_CHARACTER_DEAD);
        return false;
    }
    if (m_data->m_nPrivateStoreType == PST_MANAGE || GetPrivateStoreType() != PST_NONE) {
        SendSystemMessage(SystemMessageId::CANNOT_ACT_DURING_PRIVATE_STORE_OR_WORKSHOP);
        return false;
    }
    if (GetPendingUseETCItem(PUIT_ENCHANT) != nullptr || GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT) != nullptr) {
        SendSystemMessage(SystemMessageId::ENCHANT_OR_ATTRIBUTE_ENCHANT_IN_PROGRESS);
        return false;
    }
    if (GetPendingUseETCItem(PUIT_CRYSTAL) != nullptr) {
        SendSystemMessage(SystemMessageId::CANNOT_ACT_DURING_CRYSTALLIZATION);
        return false;
    }
    if (GetPendingUseETCItem(PUIT_COMMISSION) != nullptr) {
        SendSystemMessage(SystemMessageId::CANNOT_ACT_DURING_COMMISSION_SALE);
        return false;
    }
    if (GetPendingUseETCItem(PUIT_ATTRIBUTE_CHANGE) != nullptr) {
        SendSystemMessage(SystemMessageId::ATTRIBUTE_CHANGE_IN_PROGRESS);
        return false;
    }
    if (GetPendingUseETCItem(PUIT_SHAPE_SHIFTING) != nullptr) {
        SendSystemMessage(SystemMessageId::ITEM_APPEARANCE_MODIFICATION_OR_RESTORATION_IN_PROGRESS);
        return false;
    }
    if (GetPendingUseETCItem(PUIT_ONE) != nullptr || GetPendingUseETCItem(PUIT_TWO) != nullptr) {
        SendSystemMessage(SystemMessageId::ANOTHER_OPERATION_IN_PROGRESS);
        return false;
    }
    if (GetPendingUseETCItem(PUIT_HLOCKED) != nullptr) {
        SendSystemMessage(SystemMessageId::SEAL_OR_UNSEAL_IN_PROGRESS);
        return false;
    }
    if (IsNowTrade()) {
        SendSystemMessage(SystemMessageId::CANNOT_ACT_DURING_TRADE);
        return false;
    }
    return true;
}

bool User::OnEnchantOne(int itemServerId) {
    // Current L2Server 0x00b7d230, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!CheckCombinationItemJob()) {
        return false;
    }

    CItem* const item = m_inv.GetByServerID(itemServerId);
    if (item == nullptr) {
        SendSystemMessage(SystemMessageId::ITEM_DOES_NOT_EXIST);
        return false;
    }
    if (!item->IsCombinableItem(this)) {
        return false;
    }
    if (GetPendingUseETCItem(PUIT_TWO) != nullptr) {
        SendSystemMessage(SystemMessageId::REGISTER_ITEMS_IN_ORDER);
        return false;
    }

    CombinationItemStruct* const combination = g_ObjectDB.FindCombinationItemStructBySlotOneItem(item->GetClassId());
    if (combination == nullptr) {
        SendSystemMessage(SystemMessageId::COMBINATION_DOES_NOT_EXIST);
        return false;
    }

    const bool amountValid = item->m_data->IsStackable() ? combination->SlotOneAmount <= item->m_data->m_nCount
                                                         : item->m_data->m_nCount == combination->SlotOneAmount;
    if (!amountValid) {
        SendSystemMessage(SystemMessageId::NOT_ENOUGH_ITEM_COMBINATION_MATERIALS);
        return false;
    }
    if (item->m_data->m_hLocked != 0) {
        SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
        return false;
    }

    SetPendingUseETCItem(item, PUIT_ONE);
    return true;
}

bool User::OnEnchantOneRemove(int itemServerId) {
    // Current L2Server 0x00b7d3e8, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!CheckCombinationItemJob()) {
        return false;
    }

    CItem* const item = m_inv.GetByServerID(itemServerId);
    if (item != nullptr && static_cast<int>(item->m_nId) == GetPendingUseETCItemID(PUIT_ONE)) {
        CheckAndResetPendingUseETCItem(PUIT_ONE);
        return true;
    }
    SendSystemMessage(SystemMessageId::ITEM_DOES_NOT_EXIST);
    return false;
}

bool User::OnEnchantTwo(int itemServerId) {
    // Current L2Server 0x00b7d4f8, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!CheckCombinationItemJob()) {
        return false;
    }

    CItem* const first = GetPendingUseETCItem(PUIT_ONE);
    if (first == nullptr) {
        SendSystemMessage(SystemMessageId::REGISTER_ITEMS_IN_ORDER);
        return false;
    }

    CItem* const second = m_inv.GetByServerID(itemServerId);
    if (second == nullptr) {
        SendSystemMessage(SystemMessageId::ITEM_DOES_NOT_EXIST);
        return false;
    }
    if (!second->IsCombinableItem(this)) {
        return false;
    }

    CombinationItemStruct* const combination =
        g_ObjectDB.FindCombinationItemStructByMapKey(first->GetClassId(), second->GetClassId());
    if (combination == nullptr) {
        SendSystemMessage(SystemMessageId::COMBINATION_DOES_NOT_EXIST);
        return false;
    }

    std::int64_t requiredAmount = combination->SlotTwoAmount;
    if (second->m_data->IsStackable() && second == first) {
        requiredAmount = runtime::AddInt64Wrapping(requiredAmount, combination->SlotOneAmount);
    }
    const bool amountValid = second->m_data->IsStackable() ? requiredAmount <= second->m_data->m_nCount
                                                           : second->m_data->m_nCount == combination->SlotTwoAmount;
    if (!amountValid) {
        SendSystemMessage(SystemMessageId::NOT_ENOUGH_ITEM_COMBINATION_MATERIALS);
        return false;
    }
    if (second->m_data->m_hLocked != 0) {
        SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
        return false;
    }

    SetPendingUseETCItem(second, PUIT_TWO);
    return true;
}

bool User::OnEnchantTwoRemove(int itemServerId) {
    // Current L2Server 0x00b7d6e0, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!CheckCombinationItemJob()) {
        return false;
    }

    CItem* const item = m_inv.GetByServerID(itemServerId);
    if (item != nullptr && static_cast<int>(item->m_nId) == GetPendingUseETCItemID(PUIT_TWO)) {
        CheckAndResetPendingUseETCItem(PUIT_TWO);
        return true;
    }
    SendSystemMessage(SystemMessageId::ITEM_DOES_NOT_EXIST);
    return false;
}

bool User::OnEnchantRetryToPutItems(protocol::C_EX_ENCHANT_RETRY_TO_PUT_ITEMS_INFO& packet) {
    // Current L2Server 0x00b7d7f0, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!CheckCombinationItemJob()) {
        return false;
    }
    if (GetPendingUseETCItem(PUIT_ONE) != nullptr || GetPendingUseETCItem(PUIT_TWO) != nullptr) {
        SendSystemMessage(SystemMessageId::ANOTHER_OPERATION_IN_PROGRESS);
        return false;
    }

    CItem* const first = m_inv.GetByServerID(packet.nSlotOneItemServerID);
    CItem* const second = m_inv.GetByServerID(packet.nSlotTwoItemServerID);
    if (first == nullptr || second == nullptr) {
        SendSystemMessage(SystemMessageId::ITEM_DOES_NOT_EXIST);
        return false;
    }
    if (CItemRestrict::Inst().CheckItemRestrict(first->m_data->m_nClassID) ||
        CItemRestrict::Inst().CheckItemRestrict(second->m_data->m_nClassID) || !first->IsCombinableItem(this) ||
        !second->IsCombinableItem(this)) {
        return false;
    }

    CombinationItemStruct* const combination =
        g_ObjectDB.FindCombinationItemStructByMapKey(first->m_data->m_nClassID, second->m_data->m_nClassID);
    if (combination == nullptr) {
        SendSystemMessage(SystemMessageId::COMBINATION_DOES_NOT_EXIST);
        return false;
    }

    const bool firstAmountValid = first->m_data->IsStackable()
        ? combination->SlotOneAmount + (first == second ? combination->SlotTwoAmount : 0) <= first->m_data->m_nCount
        : first->m_data->m_nCount == combination->SlotOneAmount;
    const bool secondAmountValid = second->m_data->IsStackable()
        ? combination->SlotTwoAmount + (first == second ? combination->SlotOneAmount : 0) <= second->m_data->m_nCount
        : second->m_data->m_nCount == combination->SlotTwoAmount;
    if (!firstAmountValid || !secondAmountValid) {
        SendSystemMessage(SystemMessageId::NOT_ENOUGH_ITEM_COMBINATION_MATERIALS);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[OnEnchantRetryToPutItems] invalid item amount")
        L2_LOG_ERROR(Log, u"[OnEnchantRetryToPutItems] invalid item amount");
        return false;
    }

    SetPendingUseETCItem(first, PUIT_ONE);
    SetPendingUseETCItem(second, PUIT_TWO);
    return true;
}

bool User::OnEnchantTry() {
    // Current L2Server 0x00b92428, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!CheckCombinationItemJob()) {
        SetPendingUseETCItem(nullptr, PUIT_ONE);
        SetPendingUseETCItem(nullptr, PUIT_TWO);
        GetSocket()->Send("chdd", 0xfe, 0x171, 0, 0);
        return false;
    }

    if (runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 1) == 1) {
        SendSystemMessage(SystemMessageId::ANOTHER_OPERATION_IN_PROGRESS);
        SetPendingUseETCItem(nullptr, PUIT_ONE);
        SetPendingUseETCItem(nullptr, PUIT_TWO);
        GetSocket()->Send("chdd", 0xfe, 0x171, 0, 0);
        return false;
    }

    if (GetPendingUseETCItem(PUIT_ONE) == nullptr || GetPendingUseETCItem(PUIT_TWO) == nullptr) {
        SendSystemMessage(SystemMessageId::ITEM_DOES_NOT_EXIST);
        SetPendingUseETCItem(nullptr, PUIT_ONE);
        SetPendingUseETCItem(nullptr, PUIT_TWO);
        GetSocket()->Send("chdd", 0xfe, 0x171, 0, 0);
        runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);
        return false;
    }

    CItem* const first = m_inv.GetByServerID(GetPendingUseETCItemID(PUIT_ONE));
    CItem* const second = m_inv.GetByServerID(GetPendingUseETCItemID(PUIT_TWO));
    if (first == nullptr || second == nullptr) {
        SendSystemMessage(SystemMessageId::ITEM_DOES_NOT_EXIST);
        SetPendingUseETCItem(nullptr, PUIT_ONE);
        SetPendingUseETCItem(nullptr, PUIT_TWO);
        GetSocket()->Send("chdd", 0xfe, 0x171, 0, 0);
        runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);
        return false;
    }
    if (first->m_data->m_hLocked != 0 || second->m_data->m_hLocked != 0) {
        SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
        SetPendingUseETCItem(nullptr, PUIT_ONE);
        SetPendingUseETCItem(nullptr, PUIT_TWO);
        GetSocket()->Send("chdd", 0xfe, 0x171, 0, 0);
        runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);
        return false;
    }

    if (CItemRestrict::Inst().CheckItemRestrict(first->GetClassId()) ||
        CItemRestrict::Inst().CheckItemRestrict(second->GetClassId())) {
        // The current image leaves both pending slots and
        // m_lWaitVariation untouched on this early rejection path.
        return false;
    }

    CombinationItemStruct* const combination =
        g_ObjectDB.FindCombinationItemStructByMapKey(first->GetClassId(), second->GetClassId());
    if (combination == nullptr) {
        SendSystemMessage(SystemMessageId::COMBINATION_DOES_NOT_EXIST);
        SetPendingUseETCItem(nullptr, PUIT_ONE);
        SetPendingUseETCItem(nullptr, PUIT_TWO);
        GetSocket()->Send("chdd", 0xfe, 0x171, 0, 0);
        runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);
        return false;
    }

    const int slotOne = combination->SlotOne;
    const std::int64_t slotOneAmount = combination->SlotOneAmount;
    const int slotTwo = combination->SlotTwo;
    const std::int64_t slotTwoAmount = combination->SlotTwoAmount;
    const int probability = combination->Prob;
    const int successResultItem = combination->SuccessResultItem;
    const std::int64_t successResultItemAmount = combination->SuccessResultItemAmount;
    const int failResultItem = combination->FailResultItem;
    const std::int64_t failResultItemAmount = combination->FailResultItemAmount;

    bool firstAmountValid = false;
    if (CSharedItemData::IsStackable(first->m_data->m_ItemConsumeType)) {
        std::int64_t requiredAmount = slotOneAmount;
        if (first == second) {
            requiredAmount = runtime::AddInt64Wrapping(slotOneAmount, slotTwoAmount);
        }
        firstAmountValid = requiredAmount <= first->m_data->m_nCount;
    } else {
        firstAmountValid = first->m_data->m_nCount == slotOneAmount;
    }

    bool secondAmountValid = false;
    if (CSharedItemData::IsStackable(second->m_data->m_ItemConsumeType)) {
        std::int64_t requiredAmount = slotTwoAmount;
        if (second == first) {
            requiredAmount = runtime::AddInt64Wrapping(slotTwoAmount, slotOneAmount);
        }
        secondAmountValid = requiredAmount <= second->m_data->m_nCount;
    } else {
        secondAmountValid = second->m_data->m_nCount == slotTwoAmount;
    }

    if (!firstAmountValid || !secondAmountValid) {
        SendSystemMessage(SystemMessageId::NOT_ENOUGH_ITEM_COMBINATION_MATERIALS);
        SetPendingUseETCItem(nullptr, PUIT_ONE);
        SetPendingUseETCItem(nullptr, PUIT_TWO);
        GetSocket()->Send("chdd", 0xfe, 0x171, 0, 0);
        runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);
        // Log.Add(LOG_ERROR, L"[OnEnchantTry] invalid item amount")
        L2_LOG_ERROR(Log, u"[OnEnchantTry] invalid item amount");
        return false;
    }

    L2_GUARD(m_ManipulateQueueLock);
    EtcManiUpdateUserItemCombination manipulation;
    const double randomValue = genrand64_real2();
    std::array<ItemManip, 3> requests{};

    requests[0].mClassId = slotOne;
    requests[0].mDBId = static_cast<int>(first->GetDBID());
    requests[0].mCount = runtime::NegateInt64Wrapping(slotOneAmount);
    requests[1].mClassId = slotTwo;
    requests[1].mDBId = static_cast<int>(second->GetDBID());
    requests[1].mCount = runtime::NegateInt64Wrapping(slotTwoAmount);

    const int roll = runtime::Win64CvttDoubleToInt32(randomValue * 101.0);
    const bool failed = probability < roll;
    int resultItem = 0;
    if (failed) {
        resultItem = failResultItem;
        requests[2].mClassId = failResultItem;
        requests[2].mCount = failResultItemAmount;
        manipulation.m_nUserDBID = static_cast<int>(GetDBID());
    } else {
        resultItem = successResultItem;
        requests[2].mClassId = successResultItem;
        requests[2].mCount = successResultItemAmount;
        manipulation.m_nUserDBID = static_cast<int>(GetDBID());
    }

    manipulation.m_isSuccess = !failed;
    manipulation.m_nDel1ItemID = slotOne;
    manipulation.m_nItemID = resultItem;
    manipulation.m_nUserSid = Id();
    CPacketHelperEx<20480> etcPacket;
    manipulation.m_nDel2ItemID = slotTwo;
    manipulation.Assemble(etcPacket);

    etcMsg message = SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    ManipulateQueue queue = SaveManipulateQueue(
        ManipulateType::MANI_COMBINATION_ITEM, 0, 3, requests.data(), nullptr, 1, etcPacket, false,
        ItemGetReason::Combination, message
    );

    const ManiItemResultType result = _SetManiItemJob(queue);
    if (result == MRT_NOJOB) {
        if (!_HandleItemManipulation(queue)) {
            SetPendingUseETCItem(nullptr, PUIT_ONE);
            SetPendingUseETCItem(nullptr, PUIT_TWO);
            GetSocket()->Send("chdd", 0xfe, 0x171, 0, 0);
            runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);
            return false;
        }
    } else if (result == MRT_JOBMAP) {
        SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        SetPendingUseETCItem(nullptr, PUIT_ONE);
        SetPendingUseETCItem(nullptr, PUIT_TWO);
        GetSocket()->Send("chdd", 0xfe, 0x171, 0, 0);
        runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Duple",
        //     L"user_item.cpp",
        //     0x1d8e)
        // Noise: duplicate pending request already receives a client response.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Duple",
        //     L"user_item.cpp",
        //     0x1d8e)
        return false;
    } else if (result == MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x1d93)
        // Noise: normal deferral behind a pending item operation. Not a failure.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x1d93)
    } else {
        SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        SetPendingUseETCItem(nullptr, PUIT_ONE);
        SetPendingUseETCItem(nullptr, PUIT_TWO);
        GetSocket()->Send("chdd", 0xfe, 0x171, 0, 0);
        runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x1d9d)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x1d9d)
        L2_LOG_ERROR(
            Log, "[items] Operation %d, request %d, character %u: unexpected queue result %d; item operation not started.",
            static_cast<int>(queue.eType), queue.nId, static_cast<unsigned int>(GetDBID()), static_cast<int>(result)
        );
        return false;
    }
    return true;
}

int User::CheckUpgradeSystemJob() {
    // Current L2Server 0x00b7bc38, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!IsAlive()) {
        return 0x1146;
    }
    if (m_data->m_nPrivateStoreType == PST_MANAGE || GetPrivateStoreType() != PST_NONE) {
        return 0x1079;
    }
    if (GetPendingUseETCItem(PUIT_COMMISSION) != nullptr) {
        return 0x1080;
    }
    if (GetPendingUseETCItem(PUIT_CRYSTAL) != nullptr) {
        return 0x1081;
    }
    if (GetPendingUseETCItem(PUIT_HLOCKED) != nullptr) {
        return 0;
    }
    if (GetPendingUseETCItem(PUIT_ENCHANT) != nullptr || GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT) != nullptr) {
        return 0x0e4c;
    }
    if (GetPendingUseETCItem(PUIT_ATTRIBUTE_CHANGE) != nullptr) {
        return 0x0e4a;
    }
    if (GetPendingUseETCItem(PUIT_SHAPE_SHIFTING) != nullptr) {
        return 0x17c4;
    }
    if (IsNowTrade()) {
        return 0x107f;
    }
    if (m_lWaitVariation == 1) {
        return 0x0e4f;
    }
    return 0;
}

int User::DropItemAll(const bool force) {
    // Current L2Server 0x00b9a128, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!IsBuilder()) {
        return -1;
    }

    int droppedCount = 0;
    const FVector position = GetPos();
    {
        L2_WRITE_GUARD(m_inv);
        for (CSharedItemData* itemData = m_inv.GetFirst(); itemData != nullptr; itemData = itemData->Next()) {
            // The current body releases the factory CSPointer temporary before
            // using this raw pointer while the inventory write lock is held.
            CItem* const item = CItemFactory::Inst().FindObjectSP(itemData->index).get();
            if (item == nullptr) {
                continue;
            }

            bool drop = force;
            if (!drop &&
                (item->m_itemOption.itemOptionKey.variationOption1 < 1 ||
                    item->m_itemOption.itemOptionKey.variationOption2 < 1) &&
                !item->IsQuestItem() && CCursedWeaponMgr::inst().FindConst(item->m_data->m_nClassID) == nullptr &&
                item->IsDrop() && !IsEquipedItem(item->m_data->index)) {
                drop = true;
            }
            if (!drop) {
                continue;
            }

            if (!item->IsPetCollarItem()) {
                const int angleDegrees = runtime::Win64CvttDoubleToInt32(::genrand64_real2() * 360.0 + 0.0);
                const double angle = (static_cast<double>(angleDegrees) * 3.14) / 360.0;
                const int distance = runtime::Win64CvttDoubleToInt32(::genrand64_real2() * 16.0 + 30.0);
                const double sine = std::sin(angle);
                const double cosine = std::cos(angle);
                FVector dropPosition{position.x + cosine * static_cast<double>(distance),
                    position.y + sine * static_cast<double>(distance), position.z + static_cast<double>(distance) * 0.0};
                static_cast<void>(DropItem(item, itemData->m_nCount, dropPosition, false));
            } else {
                SendSystemMessage(SystemMessageId::CANNOT_DESTROY_SUMMONING_ITEM_WHILE_PET_SUMMONED);
            }
            ++droppedCount;
        }
    }
    return droppedCount;
}

void User::RequestUpgradeSystem(protocol::C_EX_UPGRADE_SYSTEM_REQUEST& packet) {
    // Current L2Server 0x00b9a44c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    CUserSocket* const socket = GetSocket();
    if (socket == nullptr) {
        return;
    }

    protocol::S_EX_UPGRADE_SYSTEM_RESULT response{};
    const auto sendResult = [&](const int messageId) {
        response.nResult = 0;
        response.nID = messageId;
        socket->Send(S_EX, S_EX_UPGRADE_SYSTEM_RESULT, response);
    };
    const auto rejectAfterWait = [&](const int messageId) {
        sendResult(messageId);
        runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);
    };

    if (!TalkChannelCache::inst().mChannels[22].IsValidChannel(Id(), m_spTalkTarget.GetId(), 0xfa, 0)) {
        sendResult(0x1c1e);
        return;
    }
    const int jobResult = CheckUpgradeSystemJob();
    if (jobResult != 0) {
        sendResult(jobResult);
        return;
    }
    if (runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 1) == 1) {
        sendResult(0x0e4f);
        return;
    }

    CPacketHelperEx<20480> request;
    request.Assemble("d", static_cast<int>(GetDBID()));
    CUpgradeSystem* const upgrade = g_ObjectDB.CheckUpgradeSystem(packet.nUpgradeSystemID);
    if (upgrade == nullptr) {
        rejectAfterWait(0x1179);
        return;
    }
    CItem* const item = m_inv.GetByServerID(packet.nItemSid);
    if (item == nullptr) {
        rejectAfterWait(0x117a);
        return;
    }
    if (item->m_data->GetLocked() != 0) {
        rejectAfterWait(0x1403);
        return;
    }
    if (item->GetClassId() != upgrade->m_nUpgradeID ||
        static_cast<std::int64_t>(item->m_data->GetEnchanted()) != upgrade->m_nUpgradeEnchant) {
        rejectAfterWait(0x1179);
        return;
    }

    request.Assemble("dd", static_cast<int>(item->GetDBID()), item->m_data->m_nId);
    request.Assemble("d", static_cast<int>(upgrade->m_vMaterial.size()));
    bool hasAllMaterials = true;
    for (const UpgradeMaterial& material : upgrade->m_vMaterial) {
        hasAllMaterials &= m_inv.HaveItemByClassId(material.m_PrimaryKey, material.m_SecondaryKey, false, false);
        request.Assemble("dQ", material.m_PrimaryKey, material.m_SecondaryKey);
    }
    if (!hasAllMaterials) {
        rejectAfterWait(0x117b);
        return;
    }
    if (!m_inv.HaveItemByClassId(0x39, upgrade->m_nCommission, false, false)) {
        rejectAfterWait(0x117c);
        return;
    }
    request.Assemble("dQ", 0x39, upgrade->m_nCommission);
    request.Assemble("dd", upgrade->m_nResultID, static_cast<int>(upgrade->m_nResultEnchant));

    const int shapeClassId = item->m_data->GetShapeShiftedClassId();
    int shapePeriod = -9999;
    if (shapeClassId != 0) {
        shapePeriod = GetPeriodLeftForShapeShiftedItem(static_cast<int>(item->GetDBID()));
        if (shapePeriod == -9999) {
            shapePeriod = -1;
        }
    }
    request.Assemble("dd", shapeClassId, shapePeriod);
    db.RequestUpdateSystem(this, request.GetLength(), request.GetPacket());
}

bool User::TryEnchantArtifact(const unsigned char* packet) {
    // Current L2Server 0x00b9a9c0, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!CheckArtifactEnchantJob()) {
        return false;
    }

    if (runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 1) == 1) {
        SendSystemMessage(SystemMessageId::ANOTHER_OPERATION_IN_PROGRESS);
        return false;
    }
    if (runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 1) == 1) {
        SendSystemMessage(SystemMessageId::ANOTHER_OPERATION_IN_PROGRESS);
        runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);
        return false;
    }

    const auto resetJobs = [this]() {
        runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);
        runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
    };

    int targetItemServerId = 0;
    std::uint32_t materialCount = 0;
    std::array<int, 3> materialItemServerIds{};
    const unsigned char* cursor = ::Disassemble(packet, "dd", &targetItemServerId, &materialCount);
    if (materialCount - 1U > 2U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ArtifactSystem][Enchant] Invalid Artifact Enchant "
        //     L"Material Item Count. char[%s] Value[%d]",
        //     GetName(),
        //     static_cast<int>(materialCount))
        // L2_LOG_ERROR(Log,
        //     L"[ArtifactSystem][Enchant] Invalid Artifact Enchant "
        //     L"Material Item Count. char[%s] Value[%d]",
        //     GetName(),
        //     static_cast<int>(materialCount))
        L2_LOG_ERROR(
            Log,
            u"[ArtifactSystem][Enchant] Invalid Artifact Enchant "
            u"Material Item Count. char[%s] Value[%d]",
            GetName(), static_cast<int>(materialCount)
        );
        resetJobs();
        return false;
    }
    for (std::uint32_t index = 0; index < materialCount; ++index) {
        cursor = ::Disassemble(cursor, "d", &materialItemServerIds[index]);
    }

    for (std::uint32_t firstIndex = 0; firstIndex < materialCount; ++firstIndex) {
        for (std::uint32_t secondIndex = firstIndex + 1; secondIndex < materialCount; ++secondIndex) {
            if (materialItemServerIds[firstIndex] == materialItemServerIds[secondIndex]) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[ArtifactSystem][Enchant] Artifanct Enchant "
                //     L"Material Duplicated. char[%s], item[%d]",
                //     GetName(),
                //     materialItemServerIds[firstIndex])
                // L2_LOG_ERROR(Log,
                //     L"[ArtifactSystem][Enchant] Artifanct Enchant "
                //     L"Material Duplicated. char[%s], item[%d]",
                //     GetName(),
                //     materialItemServerIds[firstIndex])
                L2_LOG_ERROR(
                    Log,
                    u"[ArtifactSystem][Enchant] Artifanct Enchant "
                    u"Material Duplicated. char[%s], item[%d]",
                    GetName(), materialItemServerIds[firstIndex]
                );
                resetJobs();
                return false;
            }
        }
    }

    CItem* const target = m_inv.GetByServerID(targetItemServerId);
    if (target == nullptr) {
        SendSystemMessage(SystemMessageId::ITEM_DOES_NOT_EXIST);
        resetJobs();
        return false;
    }
    if (IsEquipedItem(target->GetIndex())) {
        SendSystemMessage(SystemMessageId::CANNOT_REGISTER_EQUIPPED_ITEM);
        resetJobs();
        return false;
    }
    if (target->m_data->GetLocked() != 0) {
        SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
        resetJobs();
        return false;
    }
    if (!target->IsEnchantableItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ArtifactSystem][Enchant] Not Enchantable Item[%d] "
        //     L"Enchant[%d]",
        //     target->m_data->m_nClassID,
        //     target->m_data->GetEnchanted())
        L2_LOG_ERROR(
            Log,
            u"[ArtifactSystem][Enchant] Not Enchantable Item[%d] "
            u"Enchant[%d]",
            target->m_data->m_nClassID, target->m_data->GetEnchanted()
        );
        resetJobs();
        return false;
    }
    if (!target->IsArtifact()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ArtifactSystem][Enchant] Target Item is not "
        //     L"Artifact Item[%d]",
        //     target->m_data->m_nClassID)
        L2_LOG_ERROR(
            Log,
            u"[ArtifactSystem][Enchant] Target Item is not "
            u"Artifact Item[%d]",
            target->m_data->m_nClassID
        );
        resetJobs();
        return false;
    }

    const int targetItemClassId = target->GetClassId();
    const int targetEnchant = target->m_data->GetEnchanted();
    const int minimumMaterialEnchant = Singleton<CArtifactDB>::inst().GetMaterialMinEnchantLevel(targetEnchant);
    const std::vector<int>* const validMaterialItems =
        Singleton<CArtifactDB>::inst().GetMaterialItemInfo(targetItemClassId, targetEnchant);
    if (minimumMaterialEnchant == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ArtifactSystem][Enchant] Invalid Material Min "
        //     L"Enchant Level. TargetEnchantLevel[%d]",
        //     targetEnchant)
        L2_LOG_ERROR(
            Log,
            u"[ArtifactSystem][Enchant] Invalid Material Min "
            u"Enchant Level. TargetEnchantLevel[%d]",
            targetEnchant
        );
        resetJobs();
        return false;
    }
    if (validMaterialItems == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ArtifactSystem][Enchant] Invalid Enchant Material "
        //     L"Info. Target: ClassID[%d], EnchantLevel[%d]",
        //     targetItemClassId,
        //     targetEnchant)
        L2_LOG_ERROR(
            Log,
            u"[ArtifactSystem][Enchant] Invalid Enchant Material "
            u"Info. Target: ClassID[%d], EnchantLevel[%d]",
            targetItemClassId, targetEnchant
        );
        resetJobs();
        return false;
    }

    std::array<CItem*, 3> materialItems{};
    std::array<int, 3> materialItemDbIds{};
    for (std::uint32_t index = 0; index < materialCount; ++index) {
        CItem* const material = m_inv.GetByServerID(materialItemServerIds[index]);
        materialItems[index] = material;
        if (material == nullptr) {
            SendSystemMessage(SystemMessageId::ITEM_DOES_NOT_EXIST);
            resetJobs();
            return false;
        }
        if (IsEquipedItem(material->GetIndex())) {
            SendSystemMessage(SystemMessageId::CANNOT_REGISTER_EQUIPPED_ITEM);
            resetJobs();
            return false;
        }
        if (material->m_data->GetLocked() != 0) {
            SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
            resetJobs();
            return false;
        }
        if (!material->IsArtifact()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[ArtifactSystem][Enchant] Material Item is not "
            //     L"Artifact Item[%d]",
            //     material->m_data->m_nClassID)
            L2_LOG_ERROR(
                Log,
                u"[ArtifactSystem][Enchant] Material Item is not "
                u"Artifact Item[%d]",
                material->m_data->m_nClassID
            );
            resetJobs();
            return false;
        }
        if (material->m_data->GetEnchanted() < minimumMaterialEnchant) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[ArtifactSystem][Enchant] Invalid Material "
            //     L"Enchant Level[%d]. Required Min Enchant Level[%d]",
            //     material->m_data->GetEnchanted(),
            //     minimumMaterialEnchant)
            L2_LOG_ERROR(
                Log,
                u"[ArtifactSystem][Enchant] Invalid Material "
                u"Enchant Level[%d]. Required Min Enchant Level[%d]",
                material->m_data->GetEnchanted(), minimumMaterialEnchant
            );
            resetJobs();
            return false;
        }

        auto validPosition = validMaterialItems->begin();
        while (validPosition != validMaterialItems->end()) {
            if (material->GetClassId() == *validPosition) {
                break;
            }
            ++validPosition;
        }
        if (validPosition == validMaterialItems->end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[ArtifactSystem][Enchant] Invalid Material. "
            //     L"TargetItem[%d], Material[%d]",
            //     targetItemClassId,
            //     material->GetClassId())
            L2_LOG_ERROR(
                Log,
                u"[ArtifactSystem][Enchant] Invalid Material. "
                u"TargetItem[%d], Material[%d]",
                targetItemClassId, material->GetClassId()
            );
            resetJobs();
            return false;
        }
        materialItemDbIds[index] = static_cast<int>(material->GetDBID());
    }

    const ArtifactEnchantInfo* const enchantInfo =
        Singleton<CArtifactDB>::inst().GetEnchantInfo(targetItemClassId, targetEnchant);
    if (enchantInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ArtifactSystem][Enchant] No Enchant Info. "
        //     L"TargetItem[%d], TargetItemEnchantLevel[%d]",
        //     targetItemClassId,
        //     targetEnchant)
        L2_LOG_ERROR(
            Log,
            u"[ArtifactSystem][Enchant] No Enchant Info. "
            u"TargetItem[%d], TargetItemEnchantLevel[%d]",
            targetItemClassId, targetEnchant
        );
        resetJobs();
        return false;
    }
    if (static_cast<std::uint32_t>(enchantInfo->count) != materialCount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[ArtifactSystem][Enchant] Invalid Material Count[%d]. "
        //     L"TargetItem[%d], Required Material Count[%d]",
        //     static_cast<int>(materialCount),
        //     targetItemClassId,
        //     enchantInfo->count)
        L2_LOG_ERROR(
            Log,
            u"[ArtifactSystem][Enchant] Invalid Material Count[%d]. "
            u"TargetItem[%d], Required Material Count[%d]",
            static_cast<int>(materialCount), targetItemClassId, enchantInfo->count
        );
        resetJobs();
        return false;
    }

    L2_GUARD(m_ManipulateQueueLock);
    EtcManiEnchantArtifact manipulation;
    const int roll = runtime::Win64CvttDoubleToInt32(genrand64_real2() * 100.0);
    const bool failed = enchantInfo->probability <= roll;

    int targetNewEnchant = targetEnchant;
    int targetItemDbId = 0;
    int userDbId = 0;
    int userSid = 0;
    bool enchantSuccess = false;
    if (failed) {
        targetItemDbId = static_cast<int>(target->GetDBID());
        userDbId = static_cast<int>(GetDBID());
        userSid = static_cast<int>(GetServerId());
        enchantSuccess = false;
    } else {
        targetNewEnchant = runtime::AddInt32Wrapping(targetEnchant, 1);
        targetItemDbId = static_cast<int>(target->GetDBID());
        userDbId = static_cast<int>(GetDBID());
        userSid = static_cast<int>(GetServerId());
        enchantSuccess = true;
    }
    manipulation.Set(
        userSid, userDbId, enchantSuccess, targetItemDbId, targetNewEnchant, materialItemDbIds[0], materialItemDbIds[1],
        materialItemDbIds[2]
    );

    std::array<ItemManip, 3> requests{};
    for (std::uint32_t index = 0; index < materialCount; ++index) {
        requests[index].mClassId = materialItems[index]->m_data->m_nClassID;
        requests[index].mDBId = materialItemDbIds[index];
        requests[index].mCount = -1;
    }

    CPacketHelperEx<20480> etcPacket;
    manipulation.Assemble(etcPacket);
    etcMsg message = SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    ManipulateQueue queue = SaveManipulateQueue(
        ManipulateType::MANI_ENCHANT_ARTIFACT, 0, static_cast<int>(materialCount), requests.data(), nullptr, 1, etcPacket,
        false, ItemGetReason::EnchantArtifact, message
    );

    const ManiItemResultType result = _SetManiItemJob(queue);
    if (result == MRT_NOJOB) {
        if (!_HandleItemManipulation(queue)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[ArtifactSystem][Enchant] "
            //     L"_HandleItemManipulation Failed")
            L2_LOG_ERROR(
                Log,
                u"[ArtifactSystem][Enchant] "
                u"_HandleItemManipulation Failed"
            );
            resetJobs();
            return false;
        }
    } else if (result == MRT_JOBMAP) {
        SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        resetJobs();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Duple",
        //     L"user_item.cpp",
        //     0x34d0)
        // Noise: duplicate pending request already receives a client response.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Duple",
        //     L"user_item.cpp",
        //     0x34d0)
        return false;
    } else if (result == MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x34d5)
        // Noise: normal deferral behind a pending item operation. Not a failure.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x34d5)
    } else {
        SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        resetJobs();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x34dd)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x34dd)
        L2_LOG_ERROR(
            Log, "[items] Operation %d, request %d, character %u: unexpected queue result %d; item operation not started.",
            static_cast<int>(queue.eType), queue.nId, static_cast<unsigned int>(GetDBID()), static_cast<int>(result)
        );
        return false;
    }
    return true;
}

bool User::BaseAttributeCancel(CItem* const item, const AttributeTypeEnum attribute) {
    // Current L2Server 0x00b93100, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    if (m_lWaitVariation == 1) {
        SendSystemMessage(SystemMessageId::TRY_AGAIN_LATER);
        return false;
    }

    if (item == nullptr || !item->m_itemBaseAttribute.IsEnchanted(attribute)) {
        GetSocket()->Send("chddd", 0xfe, 0x76, 0, 0, 0);
        SendSystemMessage(SystemMessageId::ATTRIBUTE_REMOVAL_FAILED);

        std::array<char16_t, 256> attributeInfo{};
        std::array<char16_t, 256> optionInfo{};
        if (item != nullptr) {
            item->m_itemBaseAttribute.GetLogString(attributeInfo.data());
            FormatUtf16To(
                optionInfo.data(), optionInfo.size(), u"%d:%d:%d", item->m_itemOption.intensiveItemClassID,
                item->m_itemOption.itemOptionKey.variationOption1, item->m_itemOption.itemOptionKey.variationOption2
            );
        }
        return false;
    }

    const std::int64_t commission = item->GetBaseAttributeCancelCommision();
    if (m_inv.GetAdenaAmount() < commission) {
        GetSocket()->Send("chddd", 0xfe, 0x76, 0, 0, 0);
        SendSystemMessage(SystemMessageId::ATTRIBUTE_REMOVAL_FAILED);

        std::array<char16_t, 256> attributeInfo{};
        item->m_itemBaseAttribute.GetLogString(attributeInfo.data());
        std::array<char16_t, 256> optionInfo{};
        FormatUtf16To(
            optionInfo.data(), optionInfo.size(), u"%d:%d:%d", item->m_itemOption.intensiveItemClassID,
            item->m_itemOption.itemOptionKey.variationOption1, item->m_itemOption.itemOptionKey.variationOption2
        );
        return false;
    }

    std::array<ItemManip, 100> requests{};
    requests[0].mClassId = 0x39;
    requests[0].mCount = runtime::NegateInt64Wrapping(commission);

    const unsigned int userSid = Id();
    const int itemDbId = static_cast<int>(item->GetDBID());
    const int userDbId = static_cast<int>(GetDBID());
    EtcManiRollbackUserItemBaseAttribute manipulation(userSid, userDbId, itemDbId, static_cast<int>(attribute));
    CPacketHelperEx<20480> packet;
    manipulation.Assemble(packet);

    L2_GUARD(m_ManipulateQueueLock);
    etcMsg message = SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    ManipulateQueue queue = SaveManipulateQueue(
        ManipulateType::MANI_ATTRIBUTE_ITEM_CANCEL, 0, 1, requests.data(), nullptr, 1, packet, false, ItemGetReason::Etc,
        message
    );

    const ManiItemResultType result = _SetManiItemJob(queue);
    if (result == MRT_NOJOB) {
        return _HandleItemManipulation(queue);
    }
    if (result == MRT_JOBMAP) {
        SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return false;
    }
    if (result == MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x213b)
        // Noise: normal deferral behind a pending item operation. Not a failure.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x213b)
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"user_item.cpp",
    //     0x2140)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"user_item.cpp",
    //     0x2140)
    L2_LOG_ERROR(
        Log, "[items] Operation %d, request %d, character %u: unexpected queue result %d; item operation not started.",
        static_cast<int>(queue.eType), queue.nId, static_cast<unsigned int>(GetDBID()), static_cast<int>(result)
    );
    return false;
}

void User::GiveEventItem(
    const int deleteItemClassId,
    const std::int64_t deleteItemAmount,
    const int addItemClassId,
    const std::int64_t addItemAmount,
    const int addItemEnchant,
    const int timeGap,
    const int eventType,
    const int eventVariable
) {
    // Current L2Server 0x00b937c0, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    std::array<ItemManip, 100> requests{};
    int requestCount = 0;

    const bool hasRequiredItems = [&] {
        L2_WRITE_GUARD(m_inv);
        if (deleteItemClassId > 0) {
            if (m_inv.GetItemAmount(deleteItemClassId, 0, true) < deleteItemAmount) {
                return false;
            }
            requests[requestCount++] =
                ItemManip(0, deleteItemClassId, runtime::NegateInt64Wrapping(deleteItemAmount), 0, 0, 0, true);
        }
        if (addItemClassId > 0) {
            requests[requestCount++] = ItemManip(0, addItemClassId, addItemAmount, addItemEnchant, 0, 0, true);
        }
        return true;
    }();
    if (!hasRequiredItems) {
        CUserSocket* const socket = GetSocket();
        if (socket != nullptr) {
            socket->Send(
                "chccdcQ", 0x62, SystemMessageId::NAMED_ITEM_QUANTITY_REQUIRED, 2, 3, deleteItemClassId, 6,
                static_cast<long long>(deleteItemAmount)
            );
        }
        return;
    }

    EtcManiUpdateUserEventTime update;
    update.m_userDBID = static_cast<int>(GetDBID());
    update.m_timeGap = timeGap;
    update.m_eventTime = runtime::crt::Time32Now();
    update.m_eventType = eventType;
    update.m_eventVariable = eventVariable;
    update.m_nDelItemClassID = deleteItemClassId;
    update.m_nDelItemAmount = deleteItemAmount;
    update.m_nAddItemClassID = addItemClassId;
    update.m_nAddItemAmount = addItemAmount;
    update.m_nAddItemEnchant = addItemEnchant;
    update.m_nRet = 0;
    CPacketHelperEx<20480> packet;
    update.Assemble(packet);

    L2_GUARD(m_ManipulateQueueLock);
    etcMsg message = SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    ManipulateQueue queue = SaveManipulateQueue(
        ManipulateType::MANI_GIVE_EVENT_ITEM, 0, requestCount, requests.data(), nullptr, 1, packet, false,
        ItemGetReason::Etc, message
    );

    const ManiItemResultType result = _SetManiItemJob(queue);
    if (result == MRT_NOJOB) {
        static_cast<void>(_HandleItemManipulation(queue));
    } else if (result == MRT_JOBMAP) {
        SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
    } else if (result == MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x261d)
        // Noise: normal deferral behind a pending item operation. Not a failure.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x261d)
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x2622)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x2622)
        L2_LOG_ERROR(
            Log, "[items] Operation %d, request %d, character %u: unexpected queue result %d; item operation not started.",
            static_cast<int>(queue.eType), queue.nId, static_cast<unsigned int>(GetDBID()), static_cast<int>(result)
        );
    }
}

// User::ResetInzoneBinding@0x00b93cf8.
void User::ResetInzoneBinding(int enabled, int startTime, int endTime, int timeGap, int eventType, int eventVariable) {
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(enabled) >= 2U || eventType < 0 || eventVariable < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"ResetInzoneBinding Invalid Params(%d, %d, %d)",
        //     enabled,
        //     eventType,
        //     eventVariable)
        L2_LOG_ERROR(
            Log,
            u"[instance binding] Reset request has invalid parameters "
            u"(%d, %d, %d); no binding is reset.",
            enabled, eventType, eventVariable
        );
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"ResetInzoneBinding Params(%d, %d, %d)",
    //     enabled,
    //     eventType,
    //     eventVariable)
    L2_LOG_ERROR(
        Log, u"[instance binding] Reset requested with parameters (%d, %d, %d).", enabled, eventType, eventVariable
    );

    std::array<ItemManip, 100> requests{};
    CPacketHelperEx<20480> packet;

    EtcManiUpdateUserEventTime update;
    update.m_userDBID = static_cast<int>(GetDBID());
    update.m_timeGap = timeGap;
    update.m_eventTime = runtime::crt::Time32Now();
    update.m_eventType = eventType;
    update.m_eventVariable = eventVariable;
    update.m_nDelItemClassID = 0;
    update.m_nDelItemAmount = 0;
    update.m_nAddItemClassID = 0;
    update.m_nAddItemAmount = 0;
    update.m_nAddItemEnchant = 0;
    update.m_nRet = 0;
    update.Assemble(packet);

    int etcManipulationCount = 1;
    if (enabled == 1) {
        const UserInzoneBindingData binding = GetInZoneRestrictionByTimeSlice(startTime, endTime);
        if (binding.inzoneTypeId > 0) {
            EtcManiRemoveInzoneBindingByEvent remove;
            remove.m_nUserSid = static_cast<int>(Id());
            remove.m_nResult = 0;
            remove.m_nInzoneTypeID = binding.inzoneTypeId;
            remove.m_isGroupRestriction = binding.groupRestriction ? 1 : 0;
            remove.Assemble(packet);
            etcManipulationCount = 2;
        }
    }

    L2_GUARD(m_ManipulateQueueLock);
    etcMsg message = SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    ManipulateQueue queue = SaveManipulateQueue(
        ManipulateType::MANI_GIVE_EVENT_ITEM, 0, 0, requests.data(), nullptr, etcManipulationCount, packet, false,
        ItemGetReason::Etc, message
    );

    const ManiItemResultType result = _SetManiItemJob(queue);
    if (result == MRT_NOJOB) {
        static_cast<void>(_HandleItemManipulation(queue));
    } else if (result == MRT_JOBMAP) {
        SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
    } else if (result == MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x2678)
        // Noise: normal deferral behind a pending item operation. Not a failure.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x2678)
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x267d)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x267d)
        L2_LOG_ERROR(
            Log, "[items] Operation %d, request %d, character %u: unexpected queue result %d; item operation not started.",
            static_cast<int>(queue.eType), queue.nId, static_cast<unsigned int>(GetDBID()), static_cast<int>(result)
        );
    }
}

bool User::ChangeAttributeItem(CItem* scrollItem, CItem* targetItem) {
    // Current L2Server 0x00b7ccd8, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 1) == 1) {
        SetPendingUseETCItem(nullptr, PUIT_ATTRIBUTE_CHANGE);
        SendSystemMessage(SystemMessageId::ANOTHER_OPERATION_IN_PROGRESS);
        GetSocket()->Send("ch", 0xfe, 0x116);
        return false;
    }
    for (const CSkillInfo* const skill : scrollItem->m_ItemSkillList) {
        skill->ActivateSkill(this, targetItem, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
    }
    return true;
}

bool User::EnchantAttrItem(CItem* scrollItem, CItem* targetItem) {
    // Current L2Server 0x00b7db1c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 1) == 1) {
        L2_LOG_WARN(
            Log, "[attribute enchant] Character %u: another enchant/variation operation is pending; cancelling request.",
            static_cast<unsigned>(GetDBID())
        );
        SetPendingUseETCItem(nullptr, PUIT_ATTRIBUTE_ENCHANT);
        GetSocket()->Send("chd", 0xfe, 0x62, 2);
        nAttributeCount = 0;
        nAttributeFailCount = 0;
        return false;
    }
    for (const CSkillInfo* const skill : scrollItem->m_ItemSkillList) {
        skill->ActivateSkill(this, targetItem, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
    }
    return true;
}

bool User::EnchantItem(CItem* scrollItem, CItem* targetItem) {
    // Current L2Server 0x00b7ce2c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 1) == 1) {
        SetPendingUseETCItem(nullptr, PUIT_ENCHANT);
        GetSocket()->Send(
            "cddQdddd", 0x87,
            // 2,
            IER_CANCELED, 0, static_cast<std::int64_t>(0), 0, 0, 0, 0
        );
        return false;
    }
    for (const CSkillInfo* const skill : scrollItem->m_ItemSkillList) {
        skill->ActivateSkill(this, targetItem, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
    }
    return true;
}

bool User::ShapeShiftItem(CItem* scrollItem, CItem* targetItem) {
    // Current L2Server 0x00b7dee8, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 1) == 1) {
        SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING);
        SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING_EXTRACTION);
        // GetSocket()->Send("chdddd", 0xfe, 300, 2, 0, 0, -1)
        GetSocket()->Send("chdddd", 0xfe, 300, ShapeShiftingResult::SSR_CANCELED, 0, 0, -1);
        return false;
    }
    for (const CSkillInfo* const skill : scrollItem->m_ItemSkillList) {
        skill->ActivateSkill(this, targetItem, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
    }
    return true;
}

bool User::hLockedItem(CItem* scrollItem, CItem* targetItem) {
    // Current L2Server 0x00b7cf8c, original owner user_item.cpp.
    // 0x00b7d020 atomically acquires User+0x3ce8 (m_lWaitVariation).
    // OnResponseLockedItem releases the same lane at 0x00b23ff9 and
    // 0x00b24193. This transaction does not use m_lWaitItemJob.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 1) == 1) {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        protocol::S_EX_LOCKED_RESULT response{};
        response.lock = 0;
        response.result = 0;
        GetSocket()->Send(S_EX, S_EX_LOCKED_RESULT, response);
        return false;
    }
    for (const CSkillInfo* const skill : scrollItem->m_ItemSkillList) {
        if (skill != nullptr) {
            skill->ActivateSkill(this, targetItem, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
        }
    }
    return true;
}

bool User::hUnLockedItem(CItem* scrollItem, CItem* targetItem) {
    // Current L2Server 0x00b7d0dc, original owner user_item.cpp.
    // 0x00b7d170 acquires the same User+0x3ce8 transaction lane used by
    // hLockedItem and released by OnResponseLockedItem.
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 1) == 1) {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        protocol::S_EX_LOCKED_RESULT response{};
        response.lock = 1;
        response.result = 0;
        GetSocket()->Send(S_EX, S_EX_LOCKED_RESULT, response);
        return false;
    }
    for (const CSkillInfo* const skill : scrollItem->m_ItemSkillList) {
        if (skill != nullptr) {
            skill->ActivateSkill(this, targetItem, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
        }
    }
    return true;
}

bool User::RequestLockedItem(protocol::C_EX_REQUEST_LOCKED_ITEM& packet) {
    // Current L2Server 0x00b31e74, original owner User.cpp.
    L2_GUARD_FUNCTION();
    CUserSocket* const socket = GetSocket();
    if (socket == nullptr) {
        return true;
    }

    protocol::S_EX_LOCKED_RESULT response{};
    response.lock = 0;
    response.result = 0;
    const auto failWithMessage = [&]() {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
        socket->SendSystemMessage(SystemMessageId::SEAL_OR_UNSEAL_SCROLL_USE_CANCELLED);
        return false;
    };

    if (IsNowTrade()) {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
        return false;
    }
    if (m_data->m_nPrivateStoreType > 0) {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        SendSystemMessage(SystemMessageId::CANNOT_SEAL_OR_UNSEAL_DURING_PRIVATE_STORE_OR_WORKSHOP);
        socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
        return false;
    }
    if (packet.nTargetItemId == -1 && CheckAndResetPendingUseETCItem(PUIT_HLOCKED)) {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
        socket->SendSystemMessage(SystemMessageId::SEAL_OR_UNSEAL_SCROLL_USE_CANCELLED);
        return false;
    }

    CItem* const targetItem = m_inv.GetByServerID(packet.nTargetItemId);
    if (targetItem == nullptr || !targetItem->IsItem()) {
        return failWithMessage();
    }
    if (targetItem->m_data->GetLocked() != 0) {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
        socket->SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
        return false;
    }
    if (!targetItem->m_const->m_bhLockedAble) {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        GetSocket()->Send("chccd", 0x62, SystemMessageId::ITEM_CANNOT_BE_SEALED, 1, 3, targetItem->GetClassId());
        socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
        return false;
    }
    if (targetItem->m_const->m_period > 0) {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        GetSocket()->Send("chccd", 0x62, SystemMessageId::ITEM_CANNOT_BE_SEALED, 1, 3, targetItem->GetClassId());
        socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
        return false;
    }
    if (targetItem->IsDuration()) {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        GetSocket()->Send("chccd", 0x62, SystemMessageId::ITEM_CANNOT_BE_SEALED, 1, 3, targetItem->GetClassId());
        socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
        return false;
    }
    if (targetItem->IsPeriodicShapeShiftedItem(this)) {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        GetSocket()->Send("chccd", 0x62, SystemMessageId::ITEM_CANNOT_BE_SEALED, 1, 3, targetItem->GetClassId());
        socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
        return false;
    }
    CItem* const scrollItem = GetPendingUseETCItem(PUIT_HLOCKED);
    if (scrollItem == nullptr || !m_inv.Have(scrollItem->m_data->index)) {
        return failWithMessage();
    }
    for (const CSkillInfo* const skill : scrollItem->m_ItemSkillList) {
        if (skill == nullptr || skill->CheckOperateCond(this, targetItem, CSkillInfo::OCT_BOTH) != -2) {
            return failWithMessage();
        }
    }

    CObject* const oldTarget = m_spTarget.GetObjectW();
    SetSkillTarget(targetItem);
    hLockedItem(scrollItem, targetItem);
    SetSkillTarget(oldTarget);
    return false;
}

bool User::RequestUnLockedItem(protocol::C_EX_REQUEST_UNLOCKED_ITEM& packet) {
    // Current L2Server 0x00b32458, original owner User.cpp.
    L2_GUARD_FUNCTION();
    CUserSocket* const socket = GetSocket();
    if (socket == nullptr) {
        return true;
    }

    protocol::S_EX_LOCKED_RESULT response{};
    response.lock = 1;
    response.result = 0;
    const auto failWithMessage = [&]() {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
        socket->SendSystemMessage(SystemMessageId::SEAL_OR_UNSEAL_SCROLL_USE_CANCELLED);
        return false;
    };

    if (IsNowTrade()) {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
        return false;
    }
    if (m_data->m_nPrivateStoreType > 0) {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        SendSystemMessage(SystemMessageId::CANNOT_SEAL_OR_UNSEAL_DURING_PRIVATE_STORE_OR_WORKSHOP);
        socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
        return false;
    }
    if (packet.nTargetItemId == -1 && CheckAndResetPendingUseETCItem(PUIT_HLOCKED)) {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
        socket->SendSystemMessage(SystemMessageId::SEAL_OR_UNSEAL_SCROLL_USE_CANCELLED);
        return false;
    }

    CItem* const targetItem = m_inv.GetByServerID(packet.nTargetItemId);
    if (targetItem == nullptr || !targetItem->IsItem()) {
        return failWithMessage();
    }
    if (targetItem->m_data->GetLocked() == 0) {
        SetPendingUseETCItem(nullptr, PUIT_HLOCKED);
        socket->Send(S_EX, S_EX_LOCKED_RESULT, response);
        socket->SendSystemMessage_S(SystemMessageId::ITEM_NOT_SEALED, targetItem->GetName());
        return false;
    }
    CItem* const scrollItem = GetPendingUseETCItem(PUIT_HLOCKED);
    if (scrollItem == nullptr || !m_inv.Have(scrollItem->m_data->index)) {
        return failWithMessage();
    }
    for (const CSkillInfo* const skill : scrollItem->m_ItemSkillList) {
        if (skill == nullptr || skill->CheckOperateCond(this, targetItem, CSkillInfo::OCT_BOTH) != -2) {
            return failWithMessage();
        }
    }

    CObject* const oldTarget = m_spTarget.GetObjectW();
    SetSkillTarget(targetItem);
    hUnLockedItem(scrollItem, targetItem);
    SetSkillTarget(oldTarget);
    return false;
}

bool User::CanOpenPrivateStore(const PrivateStoreType privateStoreType) {
    // Current L2Server 0x00b80374, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    if (GetSocket()->m_bEconomicRestriction) {
        SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_STORE);
        return false;
    }
    if (m_pFishingController->m_FishingState != FISHINGS_NONE || m_pFishingController->m_bIsAutoFishing) {
        SendSystemMessage(SystemMessageId::CANNOT_PERFORM_OTHER_ACTIONS_WHILE_FISHING);
        return false;
    }
    if (m_DuelUserInfo.IsInPartyDuel() || !m_SkillMod.m_bCanAct) {
        return false;
    }

    const int currentStoreType = GetPrivateStoreType();
    if ((currentStoreType != PST_NONE && currentStoreType != privateStoreType) || IsNowTrade()) {
        return false;
    }
    if (GetTrade() != nullptr) {
        TradeCancel(false);
    }
    if (!IsItemUsable(0)) {
        QuitPrivateStore(false);
        return false;
    }
    if (IsRiding()) {
        return false;
    }
    if (m_bDominionRenamed) {
        SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_STORE_OR_WORKSHOP_DURING_DISGUISE_SEAL_OR_UNSEAL);
        return false;
    }
    if (IsAirShipDriver()) {
        SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_STEERING);
        return false;
    }
    if (!AreaBasedRule::PrivateStore(this)) {
        SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_STORE_HERE);
        return false;
    }

    const int zoneId = GetInZoneID();
    const FVector position = GetPos();
    if (CAreaDB::inst().IsFlyMoveZone(position, zoneId)) {
        SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_STORE_HERE);
        return false;
    }
    if (GetPendingUseETCItem(PUIT_HLOCKED) != nullptr) {
        SendSystemMessage(SystemMessageId::SEAL_OR_UNSEAL_IN_PROGRESS);
        return false;
    }
    if (CheckAndResetPendingUseETCItem(PUIT_ENCHANT)) {
        SendSystemMessage(SystemMessageId::ENCHANT_SCROLL_USE_CANCELLED);
        GetSocket()->Send(
            "cddQdddd", 0x87,
            // 2,
            IER_CANCELED, 0, std::int64_t{0}, 0, 0, 0, 0
        );
    }
    if (CheckAndResetPendingUseETCItem(PUIT_SHAPE_SHIFTING)) {
        GetSocket()->Send(
            "chdddd", 0xfe, 300,
            // 2,
            ShapeShiftingResult::SSR_CANCELED, 0, 0, -1
        );
    }
    if (privateStoreType == PST_BUY_OPEN && !F2PConfig::CanOpenPrivateStoreBuy(this)) {
        SendSystemMessage(SystemMessageId::FREE_USER_CANNOT_USE_PRIVATE_BUY_STORE);
        return false;
    }
    if (m_data->m_TrainingState == 1) {
        SendSystemMessage(SystemMessageId::CANNOT_TAKE_OTHER_ACTIONS_WHILE_IN_TRAINING_CAMP);
        return false;
    }
    return true;
}

bool User::RequestPrivateStore() {
    // Current L2Server 0x00b80700.
    L2_GUARD_FUNCTION();
    const bool allowed = CanOpenPrivateStore(PST_OPEN);
    if (allowed) {
        SendPrivateStoreManageList(false, nullptr);
    }
    return allowed;
}

bool User::RequestPrivateStoreWhole() {
    // Current L2Server 0x00b807c0.
    L2_GUARD_FUNCTION();
    const bool allowed = CanOpenPrivateStore(PST_WHOLESALE_OPEN);
    if (allowed) {
        SendPrivateStoreManageList(true, nullptr);
    }
    return allowed;
}

bool User::RequestPrivateStoreBuy() {
    // Current L2Server 0x00b80880.
    L2_GUARD_FUNCTION();
    const bool allowed = CanOpenPrivateStore(PST_BUY_OPEN);
    if (allowed) {
        SendPrivateStoreBuyManageList(nullptr);
    }
    return allowed;
}

bool User::RequestRecipeShopManageList(const int shopType) {
    // Current L2Server 0x00b86558, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    m_data->m_nPrivateStoreRecipeType = shopType;
    CUserSocket* const socket = GetSocket();
    if (socket->m_bEconomicRestriction) {
        SendSystemMessage(SystemMessageId::ACCOUNT_CANNOT_OPEN_PRIVATE_WORKSHOP);
        return false;
    }
    if (!IsAlive()) {
        return false;
    }
    if (IsNowTrade()) {
        SendSystemMessage(SystemMessageId::CANNOT_CRAFT_DURING_TRADE);
        return false;
    }
    if (GetTrade() != nullptr) {
        TradeCancel(false);
    }
    if (!m_data->m_bIsActive || IsRiding()) {
        return false;
    }
    const int storeType = GetPrivateStoreType();
    if ((storeType != PST_NONE && storeType != PST_RECIPE_SHOP_OPEN) || !IsItemUsable(0) || IsInDuel()) {
        return false;
    }

    const int createLevel = shopType == 0 ? m_nDwarfCreateItemLevel : m_nCommonCreateItemLevel;
    if (createLevel == 0 || m_bDominionRenamed) {
        return false;
    }
    if (IsAirShipDriver()) {
        SendSystemMessage(SystemMessageId::CANNOT_ACT_WHILE_STEERING);
        return false;
    }
    if (CPVPMatchSystem::inst().IsPVPPlayer(this)) {
        return false;
    }
    if (!AreaBasedRule::PrivateRecipeShop(this)) {
        SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_WORKSHOP_HERE);
        return false;
    }
    const int zoneId = GetInZoneID();
    const FVector position = GetPos();
    if (CAreaDB::inst().IsFlyMoveZone(position, zoneId)) {
        SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_WORKSHOP_HERE);
        return false;
    }
    if (GetPendingUseETCItem(PUIT_HLOCKED) != nullptr) {
        SendSystemMessage(SystemMessageId::CANNOT_OPEN_PRIVATE_STORE_OR_WORKSHOP_DURING_DISGUISE_SEAL_OR_UNSEAL);
        return false;
    }
    if (CheckAndResetPendingUseETCItem(PUIT_ENCHANT)) {
        SendSystemMessage(SystemMessageId::ENCHANT_SCROLL_USE_CANCELLED);
        socket->Send(
            "cddQdddd", 0x87,
            // 2,
            IER_CANCELED, 0, std::int64_t{0}, 0, 0, 0, 0
        );
    }
    if (CheckAndResetPendingUseETCItem(PUIT_SHAPE_SHIFTING)) {
        socket->Send(
            "chdddd", 0xfe, 300,
            // 2,
            ShapeShiftingResult::SSR_CANCELED, 0, 0, -1
        );
    }
    if (m_data->m_TrainingState == 1) {
        SendSystemMessage(SystemMessageId::CANNOT_TAKE_OTHER_ACTIONS_WHILE_IN_TRAINING_CAMP);
        return false;
    }
    SendRecipeStoreManageList();
    return false;
}

void User::SendRecipeStoreManageList() {
    // Current L2Server 0x00b80f8c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    std::array<char, 0x5000> learnedPacket{};
    int learnedPacketLength = 0;
    std::array<char, 0x5000> shopPacket{};
    int shopPacketLength = 0;
    int learnedCount = 0;
    int shopCount = 0;
    int depth = 0;
    bool valid = true;
    const int recipeType = m_data->m_nPrivateStoreRecipeType;
    const int createLevel = recipeType == 0 ? m_nDwarfCreateItemLevel : m_nCommonCreateItemLevel;

    {
        L2_READ_GUARD(m_recipeBookMapLock);
        for (const auto& entry : m_recipeBookMap) {
            CRecipeInfo* const recipe = entry.second;
            if (recipe == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"NULL recipe info, [%d]",
                //     entry.first)
                L2_LOG_ERROR(
                    Log, u"[recipes] Recipe-book entry %d is null for character %u; management list generation stopped.",
                    entry.first, GetDBID()
                );
                valid = false;
                break;
            }
            if (recipe->m_nCommonRecipe != recipeType) {
                continue;
            }
            const int result = ::Assemble(
                learnedPacket.data() + learnedPacketLength, static_cast<int>(learnedPacket.size()) - learnedPacketLength,
                "dd", recipe->m_nId, recipe->m_nLevel <= createLevel ? 1 : 0
            );
            if (result < 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Assembe Error at file[%s], line[%d], "
                //     L"assemble result = [%d], total length = [%d]",
                //     L"user_item.cpp",
                //     0xd6e,
                //     result,
                //     learnedPacketLength)
                L2_LOG_ERROR(
                    Log, u"[recipes] Failed to encode learned recipe %d for character %u: result %d, packet length %d.",
                    recipe->m_nId, GetDBID(), result, learnedPacketLength
                );
            } else {
                learnedPacketLength += result;
                ++learnedCount;
            }
            if (++depth > 0xca) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"depth > %d at file[%s], line[%d]",
                //     0xca,
                //     L"user_item.cpp",
                //     0xd75)
                L2_LOG_ERROR(
                    Log,
                    u"[recipes] Management-list traversal exceeded %d learned recipes for character %u; list truncated.",
                    0xca, GetDBID()
                );
                break;
            }
        }
    }
    if (!valid) {
        return;
    }

    {
        L2_READ_GUARD(m_recipeShopMapLock);
        for (const auto& entry : m_recipeShopMap) {
            CRecipeInfoData* const recipeData = entry.second;
            CRecipeInfo* const recipe = recipeData->m_pRecipeInfo;
            if (recipe->m_nCommonRecipe != recipeType) {
                continue;
            }

            const bool learned = [&] {
                L2_READ_GUARD(m_recipeBookMapLock);
                return m_recipeBookMap.find(recipe->m_nId) != m_recipeBookMap.end();
            }();
            if (!learned) {
                continue;
            }

            const int result = ::Assemble(
                shopPacket.data() + shopPacketLength, static_cast<int>(shopPacket.size()) - shopPacketLength, "ddQ",
                recipe->m_nId, 1, recipeData->m_nMakingFee
            );
            if (result < 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Assembe Error at file[%s], line[%d], "
                //     L"assemble result = [%d], total length = [%d]",
                //     L"user_item.cpp",
                //     0xda8,
                //     result,
                //     shopPacketLength)
                L2_LOG_ERROR(
                    Log, u"[recipes] Failed to encode offered recipe %d for character %u: result %d, packet length %d.",
                    recipe->m_nId, GetDBID(), result, shopPacketLength
                );
            } else {
                shopPacketLength += result;
                ++shopCount;
            }
            if (++depth > 0xca) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"depth > %d at file[%s], line[%d]",
                //     0xca,
                //     L"user_item.cpp",
                //     0xdaf)
                L2_LOG_ERROR(
                    Log, u"[recipes] Management-list traversal exceeded %d total recipes for character %u; list truncated.",
                    0xca, GetDBID()
                );
                break;
            }
        }
    }

    GetSocket()->Send(
        "cddddbdb", 0xde, recipeType, static_cast<int>(m_data->GetMP()), static_cast<int>(m_data->m_dBaseMP), learnedCount,
        learnedPacketLength, learnedPacket.data(), shopCount, shopPacketLength, shopPacket.data()
    );
    GetController()->PrivateStore(PST_RECIPE_SHOP_MANAGE);
}

User::ManiItemResultType User::_SetManiItemJob(ManipulateQueue queue) {
    const std::int32_t previous = runtime::raw_atomic::ExchangeI32(&m_nWaitGetItemOnce, 1);
    const ManiItemJobKey key(queue.eType, queue.nId);

    if (previous == 0) {
        m_mapManiItemJob.emplace(key, queue);
        return MRT_NOJOB;
    }

    if (m_mapManiItemJob.find(key) == m_mapManiItemJob.end()) {
        m_mapManiItemJob.emplace(key, queue);
        m_ManipulateQueue.push(queue);
        return MRT_JOBNOTMAP;
    }
    return MRT_JOBMAP;
}

void User::_DoneManiItemJob(ManipulateType::Enum type, int id) {
    const ManiItemJobKey key(type, id);
    const auto found = m_mapManiItemJob.find(key);
    if (found != m_mapManiItemJob.end()) {
        m_mapManiItemJob.erase(found);
    }
    runtime::raw_atomic::ExchangeI32(&m_nWaitGetItemOnce, 0);
}

bool User::ManipuateAlchemyItems(ItemManip* const requestList, const int count, AtomicJob* const atomicJob) {
    // Current L2Server 0x00b96bf0, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ManipulateQueueLock);

    CPacketHelperEx<20480> packet;
    const etcMsg message = SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    const ManipulateQueue queue = SaveManipulateQueue(
        ManipulateType::MANI_ALCHEMY_ITEM, 0, count, requestList, atomicJob, 0, packet, false, ItemGetReason::Alchemy,
        message
    );

    const ManiItemResultType result = _SetManiItemJob(queue);
    if (result == MRT_NOJOB) {
        return _HandleItemManipulation(queue);
    }
    if (result == MRT_JOBMAP) {
        SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return false;
    }
    if (result == MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x3323)
        // Noise: normal deferral behind a pending item operation. Not a failure.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x3323)
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"user_item.cpp",
    //     0x3329)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"user_item.cpp",
    //     0x3329)
    L2_LOG_ERROR(
        Log, "[items] Operation %d, request %d, character %u: unexpected queue result %d; item operation not started.",
        static_cast<int>(queue.eType), queue.nId, static_cast<unsigned int>(GetDBID()), static_cast<int>(result)
    );
    return false;
}

bool User::ManipuateEnsoulItems(
    ItemManip* const requestList, const int count, CPacketHelperEx<20480> packet, AtomicJob* const atomicJob
) {
    // Current L2Server 0x00b970a4, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ManipulateQueueLock);

    const etcMsg message = SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    const ManipulateQueue queue = SaveManipulateQueue(
        ManipulateType::MANI_ENSOUL, 0, count, requestList, atomicJob, 1, packet, false, ItemGetReason::Ensoul, message
    );

    const ManiItemResultType result = _SetManiItemJob(queue);
    if (result == MRT_NOJOB) {
        return _HandleItemManipulation(queue);
    }
    if (result == MRT_JOBMAP) {
        SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return false;
    }
    if (result == MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x335e)
        // Noise: normal deferral behind a pending item operation. Not a failure.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x335e)
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"user_item.cpp",
    //     0x3364)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"user_item.cpp",
    //     0x3364)
    L2_LOG_ERROR(
        Log, "[items] Operation %d, request %d, character %u: unexpected queue result %d; item operation not started.",
        static_cast<int>(queue.eType), queue.nId, static_cast<unsigned int>(GetDBID()), static_cast<int>(result)
    );
    return false;
}

bool User::CheckAndSetPendingTeleportItem(int itemClassId, std::int64_t amount, int x, int y, int z) {
    // Current L2Server.exe 0x00b95e64, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ManipulateQueueLock);

    ItemManip request(0, itemClassId, amount, 0, 0, 0, true);
    request.SetPolicyOnInvalidCount(&RejectInvalidPendingTeleportCount);
    CPacketHelperEx<20480> packet;
    etcMsg message = SaveEtcMsg(false, false, x, y, z, std::u16string(), std::u16string(), 0);
    ManipulateQueue queue = SaveManipulateQueue(
        ManipulateType::MANI_TELEPORT_KEEPER, 0, 1, &request, nullptr, 0, packet, false, ItemGetReason::Etc, message
    );

    const ManiItemResultType result = _SetManiItemJob(queue);
    if (result == MRT_JOBMAP) {
        return false;
    }
    if (result != MRT_NOJOB && result != MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x29e7)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x29e7)
        L2_LOG_ERROR(
            Log, "[items] Operation %d, request %d, character %u: unexpected queue result %d; item operation not started.",
            static_cast<int>(queue.eType), queue.nId, static_cast<unsigned int>(GetDBID()), static_cast<int>(result)
        );
        return false;
    }

    {
        L2_WRITE_GUARD(m_PendingTeleportItemLock);
        if (m_nPendingTeleportItemClassID == 0) {
            m_nPendingTeleportItemClassID = itemClassId;
            m_nPendingTeleportItemAmount = amount;
            if (result == MRT_JOBNOTMAP) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
                //     L"user_item.cpp",
                //     0x29e2)
                // Noise: normal deferral behind a pending item operation. Not a failure.
                // L2_LOG_ERROR(Log,
                //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
                //     L"user_item.cpp",
                //     0x29e2)
            }
            return true;
        }

        _DoneManiItemJob(ManipulateType::MANI_TELEPORT_KEEPER, 0);
    }
    return false;
}

void User::PayPendingTeleportItem() {
    // Current L2Server.exe 0x00b87c54, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    const int itemClassId = GetPendingTeleportItemClassID();
    const std::int64_t amount = GetPendingTeleportItemAmount();
    SetPendingTeleportItem(0, 0);

    if (itemClassId == 0 || amount >= 0) {
        _DoneManiItemJob(ManipulateType::MANI_TELEPORT_KEEPER, 0);
        return;
    }

    bool succeeded = false;
    int currentPoint = 0;
    if (itemClassId == -100) {
        _DoneManiItemJob(ManipulateType::MANI_TELEPORT_KEEPER, 0);
        succeeded = HandlePointItemManipulation(static_cast<int>(amount), 1, 3, 0, nullptr, nullptr, 0) == 1;
        if (!succeeded) {
            currentPoint = GetPoint(1);
        }
    } else {
        ItemManip request(0, itemClassId, amount, 0, 0, 0, true);
        request.SetPolicyOnInvalidCount(&RejectInvalidPendingTeleportCount);
        CPacketHelperEx<20480> packet;
        etcMsg message = SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
        ManipulateQueue queue = SaveManipulateQueue(
            ManipulateType::MANI_TELEPORT_KEEPER, 0, 1, &request, nullptr, 0, packet, false, ItemGetReason::Etc, message
        );
        if (_HandleItemManipulation(queue)) {
            CLogFormatter formatter;
            formatter.WriteLog2_1(0x3ab, this, nullptr, amount, 0, 0, 0, 0, 0, 0);
            return;
        }
        _DoneManiItemJob(ManipulateType::MANI_TELEPORT_KEEPER, 0);
    }

    const FVector position = GetPos();
    std::array<wchar_t, 16> accountNameStorage{};
    const wchar_t* const accountName = PendingTeleportAccountName(m_sAccountName, accountNameStorage);
    if (succeeded) {
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,,,,%I64d,,,%s,%s,,,%s",
        //     std::int64_t{0x367},
        //     static_cast<std::int64_t>(GetDBID()),
        //     static_cast<std::int64_t>(GetAccountID()),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.x)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.y)),
        //     static_cast<std::int64_t>(
        //         runtime::Win64CvttDoubleToInt32(position.z)),
        //     static_cast<std::int64_t>(m_data->m_Race),
        //     static_cast<std::int64_t>(m_data->m_Sex),
        //     static_cast<std::int64_t>(m_data->m_nClass),
        //     static_cast<std::int64_t>(m_data->m_nLevel),
        //     amount,
        //     GetName(),
        //     accountName,
        //     L"")
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,,,,%I64d,,,%s,%s,,,%s",
            std::int64_t{0x367}, static_cast<std::int64_t>(GetDBID()), static_cast<std::int64_t>(GetAccountID()),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
            static_cast<std::int64_t>(m_data->m_Race), static_cast<std::int64_t>(m_data->m_Sex),
            static_cast<std::int64_t>(m_data->m_nClass), static_cast<std::int64_t>(m_data->m_nLevel), amount, GetName(),
            accountName, u""
        );
        return;
    }

    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,,,,,,,,%s,%s,,,%s",
    //     std::int64_t{0x3df},
    //     static_cast<std::int64_t>(GetDBID()),
    //     static_cast<std::int64_t>(GetAccountID()),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.x)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.y)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.z)),
    //     amount,
    //     static_cast<std::int64_t>(currentPoint),
    //     static_cast<std::int64_t>(itemClassId),
    //     GetName(),
    //     accountName,
    //     L"")
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,,,,,,,,%s,%s,,,%s",
        std::int64_t{0x3df}, static_cast<std::int64_t>(GetDBID()), static_cast<std::int64_t>(GetAccountID()),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)), amount,
        static_cast<std::int64_t>(currentPoint), static_cast<std::int64_t>(itemClassId), GetName(), accountName, u""
    );
}

void User::PayTeleportAddItem(int itemClassId, int amount) {
    // Current L2Server.exe 0x00b958cc, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (itemClassId == 0 || amount >= 0) {
        return;
    }

    L2_GUARD(m_ManipulateQueueLock);
    ItemManip request(0, itemClassId, amount, 0, 0, 0, true);
    CPacketHelperEx<20480> packet;
    etcMsg message = SaveEtcMsg(false, false, itemClassId, 0, amount, std::u16string(), std::u16string(), 0);
    ManipulateQueue queue = SaveManipulateQueue(
        ManipulateType::MANI_TELEPORT_ITEM, 0, 1, &request, nullptr, 0, packet, false, ItemGetReason::Etc, message
    );

    const ManiItemResultType result = _SetManiItemJob(queue);
    if (result == MRT_NOJOB) {
        const bool handled = _HandleItemManipulation(queue);
        const FVector position = GetPos();
        std::array<wchar_t, 16> accountNameStorage{};
        const wchar_t* const accountName = PendingTeleportAccountName(m_sAccountName, accountNameStorage);
        if (!handled) {
            const std::int64_t currentAmount = m_inv.GetItemAmount(itemClassId, 0, true);
            // Log.AddT(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            //     L"%s,%s,,,,",
            //     std::int64_t{0x2a96},
            //     static_cast<std::int64_t>(GetDBID()),
            //     static_cast<std::int64_t>(GetAccountID()),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.x)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.y)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.z)),
            //     static_cast<std::int64_t>(m_data->m_Race),
            //     static_cast<std::int64_t>(m_data->m_Sex),
            //     static_cast<std::int64_t>(m_data->m_nClass),
            //     static_cast<std::int64_t>(m_data->m_nLevel),
            //     static_cast<std::int64_t>(amount),
            //     currentAmount,
            //     static_cast<std::int64_t>(itemClassId),
            //     GetName(),
            //     accountName)
            Log.AddT(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
                u"%s,%s,,,,",
                std::int64_t{0x2a96}, static_cast<std::int64_t>(GetDBID()), static_cast<std::int64_t>(GetAccountID()),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
                static_cast<std::int64_t>(m_data->m_Race), static_cast<std::int64_t>(m_data->m_Sex),
                static_cast<std::int64_t>(m_data->m_nClass), static_cast<std::int64_t>(m_data->m_nLevel),
                static_cast<std::int64_t>(amount), currentAmount, static_cast<std::int64_t>(itemClassId), GetName(),
                accountName
            );
        } else {
            // Log.AddT(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,,",
            //     std::int64_t{0x2a95},
            //     static_cast<std::int64_t>(GetDBID()),
            //     static_cast<std::int64_t>(GetAccountID()),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.x)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.y)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(position.z)),
            //     static_cast<std::int64_t>(m_data->m_Race),
            //     static_cast<std::int64_t>(m_data->m_Sex),
            //     static_cast<std::int64_t>(m_data->m_nClass),
            //     static_cast<std::int64_t>(m_data->m_nLevel),
            //     static_cast<std::int64_t>(amount),
            //     GetName(),
            //     accountName)
            Log.AddT(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,%I64d,%I64d,%I64d,,,,,,%s,%s,,,,",
                std::int64_t{0x2a95}, static_cast<std::int64_t>(GetDBID()), static_cast<std::int64_t>(GetAccountID()),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
                static_cast<std::int64_t>(m_data->m_Race), static_cast<std::int64_t>(m_data->m_Sex),
                static_cast<std::int64_t>(m_data->m_nClass), static_cast<std::int64_t>(m_data->m_nLevel),
                static_cast<std::int64_t>(amount), GetName(), accountName
            );
        }
    } else if (result == MRT_JOBMAP) {
        return;
    } else if (result == MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x29a5)
        // Noise: normal deferral behind a pending item operation. Not a failure.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x29a5)
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x29aa)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x29aa)
        L2_LOG_ERROR(
            Log, "[items] Operation %d, request %d, character %u: unexpected queue result %d; item operation not started.",
            static_cast<int>(queue.eType), queue.nId, static_cast<unsigned int>(GetDBID()), static_cast<int>(result)
        );
    }
}

bool User::_HandleItemManipulation(ManipulateQueue queue) {
    L2_GUARD_FUNCTION();

    if (queue.nCount == 0 && queue.nEtcManiCount == 0) {
        _DoneManiItemJob(queue.eType, queue.nId);
        return false;
    }

    ItemManipList manipulations;
    manipulations.Push(queue.nCount, queue.aRequestList.data());
    manipulations.Serialize();

    std::array<char, 20480> packet;
    int packetPosition = 0;
    int packetItemCount = 0;

    {
        L2_READ_GUARD(m_inv);
        if (!m_inv.IsValidManipulation(&manipulations)) {
            _DoneManiItemJob(queue.eType, queue.nId);
            return false;
        }

        for (const ItemManip& manipulation : manipulations.m_vOutputList) {
            if (!manipulation.MakeDBPacket(
                    m_inv, packet.data(), static_cast<int>(packet.size()), packetPosition, packetItemCount, queue.reason
                )) {
                // Log.Add(
                //     LOG_WARN,
                //     L"[items] Queued item update not sent for character DB ID %u (SID %u): cannot encode item selector %d,
                //     amount delta %lld.", GetDBID(), static_cast<unsigned int>(m_nId), manipulation.mClassId,
                //     static_cast<long long>(manipulation.mCount))
                L2_LOG_WARN(
                    Log,
                    u"[items] Queued item update not sent for character DB ID %u (SID %u): cannot encode item selector %d, "
                    u"amount delta %lld.",
                    GetDBID(), static_cast<unsigned int>(m_nId), manipulation.mClassId,
                    static_cast<long long>(manipulation.mCount)
                );
                _DoneManiItemJob(queue.eType, queue.nId);
                return false;
            }
        }

        db._CreateUpdateItems(
            this, queue.eType, queue.nId, packetItemCount, packetPosition, packet.data(), queue.pAtomicJob,
            queue.nEtcManiCount, &queue.pEtcManiPacket, queue.bIsPremium
        );
    }
    return true;
}

bool User::HandleItemManipulation(
    int count,
    ItemManip requestList[],
    AtomicJob* atomicJob,
    int etcManipulationCount,
    CPacketHelperEx<20480>* etcManipulationPacket,
    bool premium,
    ItemGetReason::Enum reason
) {
    L2_GUARD_FUNCTION();

    if (count == 0 && etcManipulationCount == 0) {
        runtime::raw_atomic::ExchangeI32(&m_nWaitGetItemOnce, 0);
        return false;
    }

    ItemManipList manipulations;
    manipulations.Push(count, requestList);
    manipulations.Serialize();

    std::array<char, 20480> packet;
    int packetPosition = 0;
    int packetItemCount = 0;

    {
        L2_READ_GUARD(m_inv);
        if (!m_inv.IsValidManipulation(&manipulations)) {
            runtime::raw_atomic::ExchangeI32(&m_nWaitGetItemOnce, 0);
            return false;
        }

        for (const ItemManip& manipulation : manipulations.m_vOutputList) {
            if (!manipulation.MakeDBPacket(
                    m_inv, packet.data(), static_cast<int>(packet.size()), packetPosition, packetItemCount, reason
                )) {
                // Log.Add(
                //     LOG_WARN,
                //     L"[items] Item update not sent for character DB ID %u (SID %u): cannot encode item selector %d, amount
                //     delta %lld.", GetDBID(), static_cast<unsigned int>(m_nId), manipulation.mClassId, static_cast<long
                //     long>(manipulation.mCount))
                L2_LOG_WARN(
                    Log,
                    u"[items] Item update not sent for character DB ID %u (SID %u): cannot encode item selector %d, amount "
                    u"delta %lld.",
                    GetDBID(), static_cast<unsigned int>(m_nId), manipulation.mClassId,
                    static_cast<long long>(manipulation.mCount)
                );
                runtime::raw_atomic::ExchangeI32(&m_nWaitGetItemOnce, 0);
                return false;
            }
        }

        db.CreateUpdateItems(
            this, packetItemCount, packetPosition, packet.data(), atomicJob, etcManipulationCount, etcManipulationPacket,
            premium
        );
    }
    return true;
}

void User::ImpoundHeroEntryItem(const bool olympiadEntry) {
    L2_GUARD_FUNCTION();
    if (CItem* const item = m_inv.GetHeroEntryItem(olympiadEntry)) {
        db.RequestDestroyItem(item, 1, this, 0);
    }
}

bool User::CheckRidingFoodItem(int striderClassId, int itemClassId) {
    L2_GUARD_FUNCTION();

    CItemConstInfo* const itemConstInfo = g_ItemConstInfoDB.GetItemConstInfo(itemClassId);
    if (itemConstInfo == nullptr) {
        return true;
    }

    std::vector<int> categories;
    itemConstInfo->GetUseConditionList(categories, UCT_CATEGORY);
    if (categories.empty()) {
        return true;
    }

    for (const int category : categories) {
        if (CCategoryDataDB::Inst().IsInCategory(striderClassId, category)) {
            return true;
        }
    }
    return false;
}

void User::SendRecipeBookList(int recipeType) {
    // Current L2Server 0x00b7e8a0, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    if (GetController()->GetCurrentActionType() == ACT_SKILL && GetController()->GetCurrentActionSkillId() != 0x529 &&
        GetController()->GetCurrentActionSkillId() != 0x52a) {
        SendSystemMessage(SystemMessageId::CANNOT_USE_RECIPE_BOOK_WHILE_USING_SKILL);
        return;
    }

    const int createLevel = recipeType == 0 ? m_nDwarfCreateItemLevel : m_nCommonCreateItemLevel;
    std::array<char, 0x5000> packet{};
    int packetLength = 0;
    int recipeCount = 0;
    int depth = 0;

    {
        L2_READ_GUARD(m_recipeBookMapLock);
        for (const auto& entry : m_recipeBookMap) {
            CRecipeInfo* const recipe = entry.second;
            if (recipe == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Error. Cannot find recipe id. user [%d]",
                //     GetDBID())
                L2_LOG_ERROR(
                    Log, u"[recipes] Character %u has null recipe-book entry %d; entry skipped.", GetDBID(), entry.first
                );
                continue;
            }
            if (recipe->m_nCommonRecipe != recipeType) {
                continue;
            }

            const int result = ::Assemble(
                packet.data() + packetLength, static_cast<int>(packet.size()) - packetLength, "dd", recipe->m_nId,
                recipe->m_nLevel <= createLevel ? 1 : 0
            );
            if (result < 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Assembe Error at file[%s], line[%d], "
                //     L"assemble result = [%d], total length = [%d]",
                //     L"user_item.cpp",
                //     0xbf4,
                //     result,
                //     packetLength)
                L2_LOG_ERROR(
                    Log, u"[recipes] Failed to encode recipe %d for character %u: result %d, packet length %d.",
                    recipe->m_nId, GetDBID(), result, packetLength
                );
            } else {
                packetLength += result;
                ++recipeCount;
            }

            if (++depth > 0xca) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"depth > %d at file[%s], line[%d]",
                //     0xca,
                //     L"user_item.cpp",
                //     0xbfb)
                L2_LOG_ERROR(
                    Log, u"[recipes] Recipe-book traversal exceeded %d entries for character %u; list truncated.", 0xca,
                    GetDBID()
                );
                break;
            }
        }
    }

    GetSocket()->Send(
        "cdddb", 0xdc, recipeType, static_cast<int>(m_data->GetMP()), recipeCount, packetLength, packet.data()
    );
}

void User::DeleteRecipeBook(const int recipeId) {
    // Current L2Server 0x00b82980, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    int recipeType = 0;
    {
        L2_WRITE_GUARD(m_recipeBookMapLock);
        const auto position = m_recipeBookMap.find(recipeId);
        if (position == m_recipeBookMap.end()) {
            CRecipeInfo* const recipe = g_RecipeDB.find(recipeId);
            if (recipe != nullptr) {
                GetSocket()->SendSystemMessage_d(SystemMessageId::RECIPE_NOT_FOUND_IN_BOOK, recipe->m_nItemId);
                db.DeleteRecipeBookChapter(*this, recipeId);
            }
        } else {
            CRecipeInfo* const recipe = position->second;
            if (recipe != nullptr) {
                recipeType = recipe->m_nCommonRecipe;
                const int recipeItemId = recipe->m_nItemId;
                m_recipeBookMap.erase(recipeId);
                if (recipeType == 0) {
                    --m_nDwarvenRecipeCount;
                } else {
                    --m_nCommonRecipeCount;
                }

                GetSocket()->Send("chccd", 0x62, SystemMessageId::RECIPE_REMOVED_FROM_BOOK, 1, 3, recipeItemId);
                db.DeleteRecipeBookChapter(*this, recipeId);

                const FVector positionValue = GetPos();
                std::array<wchar_t, 16> accountNameStorage{};
                // Log.AddT(
                //     LOG_BOTH,
                //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
                //     std::int64_t{0x3be},
                //     static_cast<std::int64_t>(GetDBID()),
                //     static_cast<std::int64_t>(GetAccountID()),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(
                //             positionValue.x)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(
                //             positionValue.y)),
                //     static_cast<std::int64_t>(
                //         runtime::Win64CvttDoubleToInt32(
                //             positionValue.z)),
                //     static_cast<std::int64_t>(m_data->m_Race),
                //     static_cast<std::int64_t>(m_data->m_Sex),
                //     static_cast<std::int64_t>(m_data->m_nClass),
                //     static_cast<std::int64_t>(m_data->m_nLevel),
                //     static_cast<std::int64_t>(recipeId),
                //     static_cast<std::int64_t>(
                //         m_recipeBookMap.size()),
                //     GetName(),
                //     PendingTeleportAccountName(
                //         m_sAccountName,
                //         accountNameStorage))
                Log.AddT(
                    LOG_BOTH,
                    u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                    u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
                    std::int64_t{0x3be}, static_cast<std::int64_t>(GetDBID()), static_cast<std::int64_t>(GetAccountID()),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionValue.x)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionValue.y)),
                    static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionValue.z)),
                    static_cast<std::int64_t>(m_data->m_Race), static_cast<std::int64_t>(m_data->m_Sex),
                    static_cast<std::int64_t>(m_data->m_nClass), static_cast<std::int64_t>(m_data->m_nLevel),
                    static_cast<std::int64_t>(recipeId), static_cast<std::int64_t>(m_recipeBookMap.size()), GetName(),
                    PendingTeleportAccountName(m_sAccountName, accountNameStorage)
                );
            }
        }
    }
    SendRecipeBookList(recipeType);
}

void User::InitRecipeBook() {
    // Current L2Server 0x00b8093c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_recipeBookMapLock);
        m_recipeBookMap.clear();
    }
}

void User::AddRecipeBook(const int recipeId, const bool loading) {
    // Current L2Server 0x00b84f78, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    CRecipeInfo* recipe;
    {
        L2_WRITE_GUARD(m_recipeBookMapLock);
        const auto position = m_recipeBookMap.find(recipeId);
        if (position != m_recipeBookMap.end()) {
            CRecipeInfo* const recipe = position->second;
            if (recipe != nullptr) {
                GetSocket()->Send("chccd", 0x62, SystemMessageId::RECIPE_ALREADY_IN_BOOK, 1, 3, recipe->m_nItemId);
                db.DeleteRecipeBookChapter(*this, recipeId);
            }
            return;
        }

        recipe = g_RecipeDB.find(recipeId);
        if (recipe == nullptr) {
            char16_t message[260]{};
            FormatUtf16To(message, 260, u"[%d], [%d]can not find recipe..", GetDBID(), recipeId);
            // Log.Add(LOG_ERROR, message)
            // L2_LOG_ERROR(Log, message)
            L2_LOG_ERROR(
                Log, u"[crafting] Cannot add recipe %d to character DB ID %d: recipe is not registered.", recipeId, GetDBID()
            );
            return;
        }

        m_recipeBookMap.emplace(recipeId, recipe);
        if (recipe->m_nCommonRecipe == 0) {
            ++m_nDwarvenRecipeCount;
        } else {
            ++m_nCommonRecipeCount;
        }

        if (!loading) {
            GetSocket()->Send("chccd", 0x62, SystemMessageId::RECIPE_ADDED_TO_BOOK, 1, 3, recipe->m_nItemId);

            const FVector positionValue = GetPos();
            std::array<wchar_t, 16> accountNameStorage{};
            // Log.AddT(
            //     LOG_BOTH,
            //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
            //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
            //     std::int64_t{0x3bd},
            //     static_cast<std::int64_t>(GetDBID()),
            //     static_cast<std::int64_t>(GetAccountID()),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(
            //             positionValue.x)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(
            //             positionValue.y)),
            //     static_cast<std::int64_t>(
            //         runtime::Win64CvttDoubleToInt32(
            //             positionValue.z)),
            //     static_cast<std::int64_t>(m_data->m_Race),
            //     static_cast<std::int64_t>(m_data->m_Sex),
            //     static_cast<std::int64_t>(m_data->m_nClass),
            //     static_cast<std::int64_t>(m_data->m_nLevel),
            //     static_cast<std::int64_t>(recipeId),
            //     static_cast<std::int64_t>(
            //         m_recipeBookMap.size()),
            //     GetName(),
            //     PendingTeleportAccountName(
            //         m_sAccountName,
            //         accountNameStorage))
            Log.AddT(
                LOG_BOTH,
                u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
                u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,%s,%s,,,",
                std::int64_t{0x3bd}, static_cast<std::int64_t>(GetDBID()), static_cast<std::int64_t>(GetAccountID()),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionValue.x)),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionValue.y)),
                static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionValue.z)),
                static_cast<std::int64_t>(m_data->m_Race), static_cast<std::int64_t>(m_data->m_Sex),
                static_cast<std::int64_t>(m_data->m_nClass), static_cast<std::int64_t>(m_data->m_nLevel),
                static_cast<std::int64_t>(recipeId), static_cast<std::int64_t>(m_recipeBookMap.size()), GetName(),
                PendingTeleportAccountName(m_sAccountName, accountNameStorage)
            );
        }
    }

    if (!loading) {
        db.SaveRecipeBookChapter(*this, recipeId);
        SendRecipeBookList(recipe->m_nCommonRecipe);
    }
}

void User::SendRecipeInfo(const int recipeId, const int result) {
    // Current L2Server 0x00b809fc, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    if (GetController()->GetCurrentActionType() == ACT_SKILL) {
        SendSystemMessage(SystemMessageId::CANNOT_USE_RECIPE_BOOK_WHILE_USING_SKILL);
        return;
    }

    {
        L2_READ_GUARD(m_recipeBookMapLock);
        const auto position = m_recipeBookMap.find(recipeId);
        if (position == m_recipeBookMap.end()) {
            if (result == -1 && GetSocket() != nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] User[%s:%d] try to view "
                //     L"unowned recipe info[%d]. ForceClose!!!",
                //     L"user_item.cpp",
                //     0xcc3,
                //     GetName(),
                //     GetDBID(),
                //     recipeId)
                // L2_LOG_ERROR(Log,
                //     L"[recipes] Character '%s' (DB ID %u) requested unowned recipe %d. Client connection is being
                //     closed.", GetName(), GetDBID(), recipeId)
                L2_LOG_ERROR(
                    Log,
                    u"[recipes] Character '%s' (DB ID %u) requested unowned recipe %d; client connection is being closed.",
                    GetName(), GetDBID(), recipeId
                );
                GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
                return;
            }
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] User[%s:%d] try to view unowned "
            //     L"recipe info[%d]. result[%d]",
            //     L"user_item.cpp",
            //     0xcca,
            //     GetName(),
            //     GetDBID(),
            //     recipeId,
            //     result)
            // L2_LOG_ERROR(Log,
            //     L"[recipes] Character '%s' (DB ID %u) requested unowned recipe %d with result code %d. Request rejected.",
            //     GetName(),
            //     GetDBID(),
            //     recipeId,
            //     result)
            L2_LOG_ERROR(
                Log,
                u"[recipes] Character '%s' (DB ID %u) requested unowned recipe %d with result code %d; request rejected.",
                GetName(), GetDBID(), recipeId, result
            );
            return;
        }

        CRecipeInfo* const recipe = position->second;
        if (recipe != nullptr) {
            if (result == 1 && recipe->m_bAnnounce) {
                recipe->AnnounceOfferingRecipeMakeItem(this);
            }
            GetSocket()->Send(
                "cdddddcQ", 0xdd, recipeId, recipe->m_nCommonRecipe, static_cast<int>(m_data->GetMP()),
                static_cast<int>(m_data->GetHP()), result, static_cast<int>(recipe->m_bUseOffering), recipe->m_nOfferingDP
            );
        }
    }
}

CRecipeInfo* User::GetRecipeInfo(const int recipeId) {
    // Current L2Server 0x00b80cd8, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    CRecipeInfo* recipe;
    {
        L2_READ_GUARD(m_recipeBookMapLock);
        const auto position = m_recipeBookMap.find(recipeId);
        if (position == m_recipeBookMap.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] User[%s][dbid:%d] try to view "
            //     L"unowned recipe info [%d]. Force close",
            //     L"user_item.cpp",
            //     0xcde,
            //     GetName(),
            //     GetDBID(),
            //     recipeId)
            // L2_LOG_ERROR(Log,
            //     L"[recipes] Character '%s' (DB ID %u) requested unowned recipe %d. Access rejected and abnormal action
            //     recorded.", GetName(), GetDBID(), recipeId)
            L2_LOG_ERROR(
                Log,
                u"[recipes] Character '%s' (DB ID %u) requested unowned recipe %d; access rejected and abnormal action "
                u"recorded.",
                GetName(), GetDBID(), recipeId
            );
            LogAbnormalAction(AUA_UNOWNED_RECIPE, true, 0, 0, 0, 0);
            return nullptr;
        }

        recipe = position->second;
    }
    return recipe;
}

bool User::HaveRecipe(const int recipeId) {
    // Current L2Server 0x00b80e7c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    const bool found = [&] {
        L2_READ_GUARD(m_recipeBookMapLock);
        return m_recipeBookMap.find(recipeId) != m_recipeBookMap.end();
    }();
    return found;
}

void User::SetRecipeStoreMsg(const unsigned char* packet) {
    // Current L2Server 0x00b7b0d0, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    std::array<char16_t, 30> decoded{};
    std::fill_n(m_wszRecipeStoreMsg, 30, u'\0');
    ::Disassemble(packet, "S", 0x3c, decoded.data());
    std::size_t length = 0;
    while (length < decoded.size() && decoded[length] != u'\0') {
        ++length;
    }
    std::copy_n(decoded.data(), length, m_wszRecipeStoreMsg);
    if (m_wszRecipeStoreMsg[0] == u'\0') {
        return;
    }

    SendPrivateStoreMessage(GetSocket(), PSMT_CHANGE_MESSAGE);
    std::array<char16_t, 30> logMessage{};
    std::copy_n(m_wszRecipeStoreMsg, logMessage.size(), logMessage.data());
    for (char16_t& value : logMessage) {
        if (value == u',') {
            value = u' ';
        }
    }

    const FVector position = GetPos();
    std::array<wchar_t, 30> messageStorage{};
    std::array<wchar_t, 16> accountNameStorage{};
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,,,,,,,%s,%s,,,",
    //     std::int64_t{0x3bf},
    //     static_cast<std::int64_t>(GetDBID()),
    //     static_cast<std::int64_t>(GetAccountID()),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.x)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.y)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(position.z)),
    //     RecipeStoreMessageForLog(
    //         logMessage.data(),
    //         logMessage.size(),
    //         messageStorage),
    //     static_cast<std::int64_t>(m_data->m_Race),
    //     static_cast<std::int64_t>(m_data->m_Sex),
    //     static_cast<std::int64_t>(m_data->m_nClass),
    //     static_cast<std::int64_t>(m_data->m_nLevel),
    //     GetName(),
    //     PendingTeleportAccountName(
    //         m_sAccountName,
    //         accountNameStorage))
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,%s,,,"
        u"%I64d,%I64d,%I64d,%I64d,,,,,,,%s,%s,,,",
        std::int64_t{0x3bf}, static_cast<std::int64_t>(GetDBID()), static_cast<std::int64_t>(GetAccountID()),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(position.z)),
        RecipeStoreMessageForLog(logMessage.data(), logMessage.size(), messageStorage),
        static_cast<std::int64_t>(m_data->m_Race), static_cast<std::int64_t>(m_data->m_Sex),
        static_cast<std::int64_t>(m_data->m_nClass), static_cast<std::int64_t>(m_data->m_nLevel), GetName(),
        PendingTeleportAccountName(m_sAccountName, accountNameStorage)
    );
}

int User::SetRecipeStoreList(const unsigned char* packet) {
    // Current L2Server 0x00b85340, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    std::uint32_t recipeCount = 0;
    const unsigned char* const entriesPacket = ::Disassemble(packet, "d", &recipeCount);
    if (recipeCount > 40U) {
        GetSocket()->SendSystemMessage_d(SystemMessageId::RECIPE_REGISTRATION_LIMIT, 40);
        SendRecipeStoreManageList();
        return 0;
    }
    if (recipeCount == 0U) {
        SendSystemMessage(SystemMessageId::NO_REGISTERED_RECIPES);
        SendRecipeStoreManageList();
        return 0;
    }

    bool valid = true;
    const unsigned char* cursor = entriesPacket;
    for (std::uint32_t index = 0; index < recipeCount; ++index) {
        int recipeId = 0;
        std::int64_t makingFee = 0;
        cursor = ::Disassemble(cursor, "dQ", &recipeId, &makingFee);
        if (recipeId < 1) {
            SendSystemMessage(SystemMessageId::INVALID_RECIPE_INFORMATION);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]SetPrivateStoreList request error. "
            //     L"invalid nRecipeId[%d], ",
            //     "user_item.cpp",
            //     0xdec,
            //     recipeId)
            L2_LOG_ERROR(
                Log, u"[recipe shop] Character %u submitted invalid recipe ID %d; store list rejected.", GetDBID(), recipeId
            );
            valid = false;
            break;
        }
        if (makingFee < 1) {
            SendSystemMessage(SystemMessageId::INVALID_RECIPE_INFORMATION);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]SetPrivateStoreList request error. "
            //     L"invalid nMakingFee[%I64d], ",
            //     "user_item.cpp",
            //     0xdf5,
            //     static_cast<long long>(makingFee))
            L2_LOG_ERROR(
                Log, u"[recipe shop] Character %u submitted invalid crafting fee %I64d; store list rejected.", GetDBID(),
                static_cast<long long>(makingFee)
            );
            valid = false;
            break;
        }
        if ((recipeId >= 314 && recipeId <= 316) || recipeId == 322) {
            SendSystemMessage(SystemMessageId::CANNOT_REGISTER_QUEST_RECIPE);
            SendSystemMessage(SystemMessageId::INVALID_STORE_OR_WORKSHOP_ITEM_QUANTITY);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]SetPrivateStoreList request error. "
            //     L"invalid nRecipeId[%d], ",
            //     "user_item.cpp",
            //     0xe04,
            //     recipeId)
            L2_LOG_ERROR(
                Log, u"[recipe shop] Character %u attempted to register restricted quest recipe %d; store list rejected.",
                GetDBID(), recipeId
            );
            valid = false;
        }
    }
    if (!valid) {
        SendRecipeStoreManageList();
        return 0;
    }

    {
        L2_WRITE_GUARD(m_recipeShopMapLock);
        for (auto& entry : m_recipeShopMap) {
            delete entry.second;
        }
        m_recipeShopMap.clear();
    }

    cursor = entriesPacket;
    for (std::uint32_t index = 0; index < recipeCount; ++index) {
        int recipeId = 0;
        std::int64_t makingFee = 0;
        cursor = ::Disassemble(cursor, "dQ", &recipeId, &makingFee);
        if (recipeId < 1) {
            SendSystemMessage(SystemMessageId::INVALID_RECIPE_INFORMATION);
            SendSystemMessage(SystemMessageId::INVALID_STORE_OR_WORKSHOP_ITEM_QUANTITY);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]SetPrivateStoreList request error. "
            //     L"invalid nRecipeId[%d], ",
            //     "user_item.cpp",
            //     0xe31,
            //     recipeId)
            L2_LOG_ERROR(
                Log,
                u"[recipe shop] Character %u submitted invalid recipe ID %d while rebuilding the store list; entry skipped.",
                GetDBID(), recipeId
            );
            continue;
        }
        if (makingFee < 1) {
            SendSystemMessage(SystemMessageId::INVALID_CRAFTING_FEE);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]SetPrivateStoreList request error. "
            //     L"invalid nMakingFee[%I64d], ",
            //     "user_item.cpp",
            //     0xe39,
            //     static_cast<long long>(makingFee))
            L2_LOG_ERROR(
                Log,
                u"[recipe shop] Character %u submitted invalid crafting fee %I64d while rebuilding the store list; entry "
                u"skipped.",
                GetDBID(), static_cast<long long>(makingFee)
            );
            continue;
        }
        if ((recipeId >= 314 && recipeId <= 316) || recipeId == 322) {
            SendSystemMessage(SystemMessageId::INVALID_RECIPE_INFORMATION);
            SendSystemMessage(SystemMessageId::INVALID_STORE_OR_WORKSHOP_ITEM_QUANTITY);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]SetPrivateStoreList request error. "
            //     L"invalid nRecipeId[%d], ",
            //     "user_item.cpp",
            //     0xe46,
            //     recipeId)
            L2_LOG_ERROR(
                Log,
                u"[recipe shop] Character %u attempted to register restricted quest recipe %d while rebuilding the store "
                u"list; entry skipped.",
                GetDBID(), recipeId
            );
            continue;
        }

        CRecipeInfo* const recipe = g_RecipeDB.find(recipeId);
        if (recipe == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%d], [%d]can not find recipe..",
            //     GetDBID(),
            //     recipeId)
            L2_LOG_ERROR(
                Log, u"[recipe shop] Character %u references missing recipe %d; store-list rebuild stopped.", GetDBID(),
                recipeId
            );
            break;
        }
        if (recipe->m_nCommonRecipe != m_data->m_nPrivateStoreRecipeType) {
            continue;
        }

        const int createLevel = recipe->m_nCommonRecipe == 0 ? m_nDwarfCreateItemLevel : m_nCommonCreateItemLevel;
        if (createLevel < recipe->m_nLevel) {
            SendSystemMessage(SystemMessageId::CHARACTER_AND_RECIPE_LEVELS_DO_NOT_MATCH);
            if (recipeCount == 1U) {
                SendRecipeStoreManageList();
                return 0;
            }
            continue;
        }

        auto* const recipeData = new CRecipeInfoData(recipe, makingFee);
        {
            L2_WRITE_GUARD(m_recipeShopMapLock);
            m_recipeShopMap.emplace(recipeId, recipeData);
        }
    }

    GetController()->PrivateStore(PST_RECIPE_SHOP_OPEN);
    return 0;
}

void User::QuitRecipeStore() {
    // Current L2Server 0x00b78e54, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    if (GetPrivateStoreType() != PST_NONE) {
        runtime::raw_atomic::DecrementI32(&g_nStoreUserCount);
        if (m_dwPrivateStoreTick != 0) {
            ResetPrivateStoreTick();
        }
    }
    if (m_data->m_StopMode == STP_STORE) {
        m_data->m_StopMode = STP_SIT;
    }
    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_nPrivateStoreType, PST_NONE, 0x1a);
    BroadcastWaitType(0);
    ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_PRIVATESTORE});
}

void User::CancelRecipeStore() {
    // Current L2Server 0x00b7caec, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    if (!IsAlive()) {
        return;
    }
    if (GetTrade() != nullptr) {
        TradeCancel(false);
    }
    if (IsRiding()) {
        return;
    }
    if (GetPrivateStoreType() != PST_RECIPE_SHOP_MANAGE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Not recipe shop manage try "
        //     L"RequestRecipeShopCancel")
        L2_LOG_ERROR(
            Log,
            u"[recipe shop] Cancel request ignored: the user is not managing "
            u"a recipe shop."
        );
        return;
    }

    const bool empty = [&] {
        L2_READ_GUARD(m_recipeShopMapLock);
        return m_recipeShopMap.empty();
    }();
    if (!empty) {
        GetController()->PrivateStore(PST_RECIPE_SHOP_OPEN);
        return;
    }

    if (GetPrivateStoreType() != PST_NONE) {
        runtime::raw_atomic::DecrementI32(&g_nStoreUserCount);
        if (m_dwPrivateStoreTick != 0) {
            ResetPrivateStoreTick();
        }
    }
    m_data->m_NeedUpdatePacketBitSet.SetIntValue_IfModifed(m_data->m_nPrivateStoreType, PST_NONE, 0x1a);
    BroadcastWaitType(0);
    ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_PRIVATESTORE});
}

void User::RecipeStoreBuy(const unsigned char* packet, User* requester) {
    // Current L2Server 0x00b8a3cc, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    int recipeId = 0;
    std::int64_t requestedFee = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dQ", &recipeId, &requestedFee);
    if (recipeId < 1) {
        return;
    }
    if (GetPrivateStoreType() == PST_RECIPE_SHOP_MANAGE) {
        requester->SendSystemMessage(SystemMessageId::PREPARING_PRIVATE_WORKSHOP);
        return;
    }
    if (GetPrivateStoreType() != PST_RECIPE_SHOP_OPEN) {
        requester->SendSystemMessage(SystemMessageId::PRIVATE_STORE_ALREADY_CLOSED);
        return;
    }

    std::size_t recipeCount;
    CRecipeInfo* recipe;
    int createLevel;
    std::int64_t makingFee;
    {
        L2_READ_GUARD(m_recipeShopMapLock);
        recipeCount = m_recipeShopMap.size();
        const auto position = m_recipeShopMap.find(recipeId);
        if (position == m_recipeShopMap.end()) {
            return;
        }

        CRecipeInfoData* const recipeData = position->second;
        if (recipeData == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"RecipeInfoData is null. [%d]",
            //     GetDBID())
            L2_LOG_ERROR(Log, u"[recipe shop] Character %d has null recipe data; request rejected.", GetDBID());
            return;
        }
        if (requestedFee != recipeData->m_nMakingFee) {
            requester->SendSystemMessage(SystemMessageId::PRIVATE_WORKSHOP_CRAFTING_PRICE_CHANGED);
            return;
        }

        recipe = recipeData->m_pRecipeInfo;
        if (recipe == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"RecipeInfo is null. [%d]",
            //     GetDBID())
            L2_LOG_ERROR(Log, u"[recipe shop] Character %d has a null recipe definition; request rejected.", GetDBID());
            return;
        }
        createLevel = recipe->m_nCommonRecipe == 0 ? m_nDwarfCreateItemLevel : m_nCommonCreateItemLevel;
        makingFee = recipeData->m_nMakingFee;
    }

    std::vector<RecipeOfferingItemStruct*> offeringItems;
    if (recipe->m_bUseOffering) {
        int offeringCount = 0;
        cursor = ::Disassemble(cursor, "d", &offeringCount);
        for (int index = 0; index < offeringCount; ++index) {
            int itemServerId = 0;
            std::int64_t count = 0;
            cursor = ::Disassemble(cursor, "dQ", &itemServerId, &count);
            offeringItems.push_back(new RecipeOfferingItemStruct(itemServerId, count));
        }
    }

    if (createLevel < recipe->m_nLevel) {
        SendSystemMessage(SystemMessageId::CREATE_ITEM_SKILL_MISSING_OR_LEVEL_TOO_LOW);
        requester->SendSystemMessage(SystemMessageId::CREATE_ITEM_SKILL_MISSING_OR_LEVEL_TOO_LOW);
    } else {
        recipe->RecipeShopMake(this, requester, makingFee, offeringItems);
    }

    for (RecipeOfferingItemStruct* const item : offeringItems) {
        delete item;
    }
    if (recipeCount == 0U) {
        requester->SendSystemMessage(SystemMessageId::ITEM_OUT_OF_STOCK);
    }
}

void User::RecipeStoreShowInfo(User* requester, const int recipeId, const int result) {
    // Current L2Server 0x00b814fc, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    if (recipeId < 1) {
        return;
    }
    if (GetPrivateStoreType() == PST_RECIPE_SHOP_MANAGE) {
        requester->SendSystemMessage(SystemMessageId::PREPARING_PRIVATE_WORKSHOP);
        return;
    }
    if (GetPrivateStoreType() != PST_RECIPE_SHOP_OPEN) {
        requester->SendSystemMessage(SystemMessageId::PRIVATE_STORE_ALREADY_CLOSED);
        return;
    }

    std::size_t recipeCount;
    CRecipeInfo* recipe;
    std::int64_t makingFee;
    {
        L2_READ_GUARD(m_recipeShopMapLock);
        recipeCount = m_recipeShopMap.size();
        const auto position = m_recipeShopMap.find(recipeId);
        if (position == m_recipeShopMap.end()) {
            return;
        }

        CRecipeInfoData* const recipeData = position->second;
        if (recipeData == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"RecipeInfoData is null. [%d]",
            //     GetDBID())
            L2_LOG_ERROR(Log, u"[recipe shop] Character %d has null recipe data; request rejected.", GetDBID());
            return;
        }
        recipe = recipeData->m_pRecipeInfo;
        makingFee = recipeData->m_nMakingFee;
    }
    if (recipe == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"RecipeInfo is null. [%d]",
        //     GetDBID())
        L2_LOG_ERROR(Log, u"[recipe shop] Character %d has a null recipe definition; request rejected.", GetDBID());
        return;
    }

    requester->GetSocket()->Send(
        "cdddddQcQ", 0xe0, static_cast<int>(GetServerId()), recipeId, static_cast<int>(m_data->GetMP()),
        static_cast<int>(m_data->GetHP()), result, makingFee, static_cast<int>(recipe->m_bUseOffering), recipe->m_nOfferingDP
    );
    if (recipeCount == 0U) {
        requester->SendSystemMessage(SystemMessageId::ITEM_OUT_OF_STOCK);
    }
}

void User::RecipeStoreListReq(int requesterServerId) {
    // Current L2Server 0x00b82cc0, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (m_nDwarfCreateItemLevel == 0 && m_nCommonCreateItemLevel == 0) {
        return;
    }

    CSPointer<User> requester = g_UserMap.FindObject(requesterServerId);
    if (!requester) {
        return;
    }
    CUserSocket* const socket = requester->GetSocket();
    if (socket == nullptr) {
        return;
    }

    const int storeType = GetPrivateStoreType();
    if (storeType == PST_RECIPE_SHOP_MANAGE) {
        socket->SendSystemMessage(SystemMessageId::PREPARING_PRIVATE_WORKSHOP);
        return;
    }
    if (storeType != PST_RECIPE_SHOP_OPEN) {
        socket->SendSystemMessage(SystemMessageId::PRIVATE_STORE_ALREADY_CLOSED);
        return;
    }
    if (socket->m_bEconomicRestriction) {
        socket->SendSystemMessage(SystemMessageId::ACCOUNT_CANNOT_USE_PRIVATE_WORKSHOPS);
        return;
    }

    std::array<char, 0x5000> packet{};
    int packetLength = 0;
    int recipeCount = 0;
    int depth = 0;
    {
        L2_READ_GUARD(m_recipeShopMapLock);
        if (m_recipeShopMap.empty()) {
            socket->SendSystemMessage(SystemMessageId::ITEM_OUT_OF_STOCK);
        }

        for (const auto& entry : m_recipeShopMap) {
            CRecipeInfoData* const recipeData = entry.second;
            if (recipeData == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"recipe info data error. [%d]",
                //     GetDBID())
                L2_LOG_ERROR(Log, u"[recipe shop] Character %d has invalid recipe data; list request rejected.", GetDBID());
                return;
            }
            CRecipeInfo* const recipe = recipeData->m_pRecipeInfo;
            if (recipe == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"recipe info error. [%d]",
                //     GetDBID())
                L2_LOG_ERROR(
                    Log, u"[recipe shop] Character %d has an invalid recipe definition; list request rejected.", GetDBID()
                );
                return;
            }

            const bool learned = [&] {
                L2_READ_GUARD(m_recipeBookMapLock);
                return m_recipeBookMap.find(recipe->m_nId) != m_recipeBookMap.end();
            }();
            if (!learned) {
                continue;
            }

            const int createLevel = recipe->m_nCommonRecipe == 0 ? m_nDwarfCreateItemLevel : m_nCommonCreateItemLevel;
            const int result = ::Assemble(
                packet.data() + packetLength, static_cast<int>(packet.size()) - packetLength, "ddQ", recipe->m_nId,
                recipe->m_nLevel <= createLevel ? 1 : 0, recipeData->m_nMakingFee
            );
            if (result < 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Assembe Error at file[%s], line[%d], "
                //     L"assemble result = [%d], total length = [%d]",
                //     L"user_item.cpp",
                //     0xf69,
                //     result,
                //     packetLength)
                L2_LOG_ERROR(
                    Log, u"[recipe shop] Failed to encode recipe %d for viewer %u: result %d, packet length %d.",
                    recipe->m_nId, requester->GetDBID(), result, packetLength
                );
            } else {
                packetLength += result;
                ++recipeCount;
            }
            if (++depth > 0xca) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"depth > %d at file[%s], line[%d]",
                //     0xca,
                //     L"user_item.cpp",
                //     0xf70)
                L2_LOG_ERROR(
                    Log, u"[recipe shop] Store-list traversal exceeded %d recipes for character %u; list truncated.", 0xca,
                    GetDBID()
                );
                break;
            }
        }

        const int currentMp = static_cast<int>(m_data->GetMP());
        const int baseMp = static_cast<int>(m_data->m_dBaseMP);
        socket->Send(
            "cdddQdb", 0xdf, static_cast<int>(Id()), currentMp, baseMp, requester->m_inv.GetAdenaAmount(), recipeCount,
            packetLength, packet.data()
        );
    }
}

bool User::DeleteItemInInventory2(int itemClassId, std::int64_t count, AtomicJob* job) {
    L2_GUARD_FUNCTION();
    (void)job;

    if (count == 0) {
        return false;
    }

    CObject* const prototype = g_ObjectDB.GetObjectW(itemClassId, false);
    if (prototype == nullptr || !prototype->IsItem() || !m_inv.HaveItemByClassId(itemClassId, count, false, false)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item[%d] is not exist.",
        //     L"user_item.cpp",
        //     0x279a,
        //     itemClassId)
        L2_LOG_ERROR(
            Log, u"[items] Conversion source class %d is undefined or is not an item; request rejected.", itemClassId
        );
        return false;
    }

    CItem* const ownedItem = m_inv.GetFirstItemByClassID(itemClassId, false, false);
    if (ownedItem == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item[%d] is not exist.",
        //     L"user_item.cpp",
        //     0x279a,
        //     itemClassId)
        L2_LOG_ERROR(
            Log, u"[items] Character %u does not own conversion source item class %d; request rejected.", GetDBID(),
            itemClassId
        );
        return false;
    }

    CItemConstInfo* const itemConstInfo = g_ItemConstInfoDB.GetItemConstInfo(ownedItem->GetClassId());

    std::array<ItemManip, 100> requests{};
    requests[0].mClassId = itemClassId;
    requests[0].mCount = -1;
    int requestCount = 1;
    bool hasCreatedItem = false;

    if (itemConstInfo != nullptr) {
        std::vector<CreateItemRandomStruct> randomItems;
        itemConstInfo->GetCreateItemRandom(randomItems);

        const double random = ::genrand64_real1() * 100.0 + 0.0;
        double cumulativeProbability = 0.0;
        for (const CreateItemRandomStruct& item : randomItems) {
            cumulativeProbability += item.Prob;
            if (random < cumulativeProbability) {
                requests[requestCount].mClassId = item.ItemID;
                requests[requestCount].mCount = item.Count;
                requests[requestCount].mEnchanted = item.Enchant;
                ++requestCount;
                hasCreatedItem = true;
                break;
            }
        }

        std::vector<CreateItemStruct> fixedItems;
        itemConstInfo->GetCreateItem(fixedItems);
        for (const CreateItemStruct& item : fixedItems) {
            requests[requestCount].mClassId = item.ItemID;
            requests[requestCount].mCount = item.Count;
            requests[requestCount].mEnchanted = item.Enchant;
            ++requestCount;
            hasCreatedItem = true;
        }

        for (const auto& group : itemConstInfo->m_vCreateItemGroupList) {
            for (const CreateItemRandomStruct& item : group) {
                if (::genrand64_real1() * 100.0 + 0.0 <= item.Prob) {
                    requests[requestCount].mClassId = item.ItemID;
                    requests[requestCount].mCount = item.Count;
                    requests[requestCount].mEnchanted = item.Enchant;
                    ++requestCount;
                    hasCreatedItem = true;
                    break;
                }
            }
        }

        if (hasCreatedItem) {
            int inventorySlots = 0;
            int artifactSlots = 0;
            for (int index = 1; index < requestCount; ++index) {
                CItem* const rewardItem = static_cast<CItem*>(g_ObjectDB.GetObjectW(requests[index].mClassId, false));
                if (CSharedItemData::IsStackable(rewardItem->m_data->m_ItemConsumeType)) {
                    if (rewardItem->IsArtifact()) {
                        ++artifactSlots;
                    } else {
                        ++inventorySlots;
                    }
                } else {
                    const int slots = static_cast<int>(requests[index].mCount);
                    if (rewardItem->IsArtifact()) {
                        artifactSlots += slots;
                    } else {
                        inventorySlots += slots;
                    }
                }
            }

            const bool artifactCapacity = m_inv.GetArtifactCount() + 5 + artifactSlots <= GetArtifactInventoryLimit();
            if (GetInventoryLimit() < m_inv.GetTotalCount() + 5 + inventorySlots) {
                SendSystemMessage(
                    artifactCapacity ? SystemMessageId::NOT_ENOUGH_INVENTORY_SPACE
                                     : SystemMessageId::NOT_ENOUGH_NORMAL_AND_ARTIFACT_INVENTORY_SPACE
                );
                return false;
            }
            if (!artifactCapacity) {
                SendSystemMessage(SystemMessageId::NOT_ENOUGH_ARTIFACT_INVENTORY_SPACE);
                return false;
            }
        }
    }

    CPacketHelperEx<20480> etcPacket;
    etcMsg message = SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    ManipulateQueue queue = SaveManipulateQueue(
        ManipulateType::MANI_UNDEFINED_ITEM, 0, requestCount, requests.data(), nullptr, 0, etcPacket, false,
        ItemGetReason::Etc, message
    );

    const ManiItemResultType result = _SetManiItemJob(queue);
    if (result == MRT_NOJOB) {
        if (!_HandleItemManipulation(queue)) {
            SendSystemMessage(SystemMessageId::ITEM_CONVERSION_FAILED);
        } else if (hasCreatedItem) {
            CLogFormatter formatter;
            for (int index = 0; index < requestCount; ++index) {
                formatter.WriteLog1_1(
                    0x234e, this, nullptr, nullptr, nullptr, nullptr, 0, requests[index].mClassId, requests[index].mCount,
                    itemClassId, requests[0].mCount, 0, 0
                );
            }
        }
        return true;
    }
    if (result == MRT_JOBMAP) {
        SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return false;
    }
    if (result == MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x286c)
        // Noise: normal deferral behind a pending item operation. Not a failure.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x286c)
        return true;
    }

    SendSystemMessage(SystemMessageId::ITEM_CONVERSION_FAILED);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"user_item.cpp",
    //     0x2872)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"user_item.cpp",
    //     0x2872)
    L2_LOG_ERROR(
        Log, "[items] Operation %d, request %d, character %u: unexpected queue result %d; item operation not started.",
        static_cast<int>(queue.eType), queue.nId, static_cast<unsigned int>(GetDBID()), static_cast<int>(result)
    );
    return false;
}

bool User::DeleteItemInInventory(const int itemClassId) {
    // Current L2Server 0x00b95728, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    ItemPointerList items;
    {
        L2_READ_GUARD(m_inv);
        m_inv.GetItemListByClassID(items, itemClassId, -1, -1, false);
    }

    std::int64_t total = 0;
    for (const CSPointer<CItem>& item : items) {
        total += item->m_data->m_nCount;
    }
    if (total > 0) {
        return DeleteItemInInventory(itemClassId, total, nullptr, false, false);
    }
    return false;
}

bool User::DeleteItemInInventory(
    const int itemClassId, std::int64_t count, AtomicJob* const job, const bool includeVariation, const bool includeEnsoul
) {
    // Current L2Server 0x00b7a164, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (count == 0) {
        return false;
    }

    CObject* const prototype = g_ObjectDB.GetObjectW(itemClassId, false);
    if (prototype == nullptr || !prototype->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item[%d] is not exist.",
        //     L"user_item.cpp",
        //     0x28a7,
        //     itemClassId)
        L2_LOG_ERROR(
            Log, u"[inventory] Item class %d is undefined or is not an item; deletion request rejected.", itemClassId
        );
        return false;
    }

    if (!m_inv.HaveItemByClassId(itemClassId, count, includeVariation, includeEnsoul)) {
        return false;
    }

    CItem* item = m_inv.GetFirstItemByClassID(itemClassId, includeVariation, includeEnsoul);
    if (item == nullptr) {
        return false;
    }

    std::array<char, 20480> packet;
    int packetSize = 0;
    std::uint32_t itemCount = 0;
    ItemOptionKey optionKey{};

    if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
        if (MsgHelper_ManipulateItem(
                packet.data(), static_cast<int>(packet.size()), packetSize, item, runtime::NegateInt64Wrapping(count),
                ItemGetReason::Etc, 0, 0, 0, optionKey, 0, 0, -1
            ) != 0) {
            itemCount = 1;
        }
    } else if (count < 2) {
        if (count != 1) {
            return false;
        }
        if (MsgHelper_ManipulateItem(
                packet.data(), static_cast<int>(packet.size()), packetSize, item, -1, ItemGetReason::Etc, 0, 0, 0, optionKey,
                0, 0, -1
            ) != 0) {
            itemCount = 1;
        }
    } else {
        const bool firstAssembled = MsgHelper_ManipulateItem(
                                        packet.data(), static_cast<int>(packet.size()), packetSize, item, -1,
                                        ItemGetReason::Etc, 0, 0, 0, optionKey, 0, 0, -1
                                    ) != 0;
        if (firstAssembled) {
            count = runtime::AddInt64Wrapping(count, -1);
        }
        itemCount = firstAssembled ? 1U : 0U;

        int index = 0;
        if (count > 0) {
            do {
                item = m_inv.GetNextItemByClassID(item->m_data->index, false, false);
                if (item == nullptr) {
                    return false;
                }
                optionKey = ItemOptionKey{};
                if (MsgHelper_ManipulateItem(
                        packet.data(), static_cast<int>(packet.size()), packetSize, item, -1, ItemGetReason::Etc, 0, 0, 0,
                        optionKey, 0, 0, -1
                    ) != 0) {
                    ++itemCount;
                }
                ++index;
            } while (static_cast<std::int64_t>(index) < count);
        }
    }

    db.CreateUpdateItems(this, static_cast<int>(itemCount), packetSize, packet.data(), job, 0, nullptr, false);
    return true;
}

void User::GiveItemOnAccount(
    const int condition,
    const int deleteItemClassId,
    const std::int64_t deleteItemAmount,
    const int addItemClassId,
    const std::int64_t addItemAmount,
    const int addItemEnchant,
    const int type,
    wchar_t* const eventName,
    const int npcId,
    const int eventStartTime
) {
    // Current L2Server 0x00b941d8, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (eventName == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"sEventName is NULL in GiveItemOnAccount")
        L2_LOG_ERROR(Log, u"[account items] Event name is null; account item delivery is rejected.");
        return;
    }

    std::array<ItemManip, 100> requests{};
    int requestCount = 0;

    const bool requestReady = [&] {
        L2_WRITE_GUARD(m_inv);
        if (deleteItemClassId > 0) {
            if (m_inv.GetItemAmount(deleteItemClassId, 0, true) < deleteItemAmount) {
                return false;
            }
            requests[requestCount++] = ItemManip(0, deleteItemClassId, -deleteItemAmount, 0, 0, 0, true);
        }
        if (addItemClassId > 0) {
            requests[requestCount++] = ItemManip(0, addItemClassId, addItemAmount, addItemEnchant, 0, 0, true);
        }
        return true;
    }();
    if (!requestReady) {
        CUserSocket* const socket = GetSocket();
        if (socket != nullptr) {
            socket->Send(
                "chccdcQ", 0x62, SystemMessageId::NAMED_ITEM_QUANTITY_REQUIRED, 2, 3, deleteItemClassId, 6,
                static_cast<long long>(deleteItemAmount)
            );
        }
        return;
    }

    EtcManiGiveItemOnAccount manipulation(
        static_cast<int>(GetDBID()), condition, deleteItemClassId, deleteItemAmount, addItemClassId, addItemAmount,
        addItemEnchant, type, GetAccountID(), npcId, eventName, eventStartTime
    );
    CPacketHelperEx<20480> packet;
    manipulation.Assemble(packet);

    L2_GUARD(m_ManipulateQueueLock);
    etcMsg message = SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    ManipulateQueue queue = SaveManipulateQueue(
        ManipulateType::MANI_GIVE_ITEM_ON_ACCOUNT, addItemClassId, requestCount, requests.data(), nullptr, 1, packet, false,
        ItemGetReason::Etc, message
    );

    const ManiItemResultType result = _SetManiItemJob(queue);
    if (result == MRT_NOJOB) {
        static_cast<void>(_HandleItemManipulation(queue));
    } else if (result == MRT_JOBMAP) {
        SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
    } else if (result == MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x26de)
        // Noise: normal deferral behind a pending item operation. Not a failure.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x26de)
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x26e3)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x26e3)
        L2_LOG_ERROR(
            Log, "[items] Operation %d, request %d, character %u: unexpected queue result %d; item operation not started.",
            static_cast<int>(queue.eType), queue.nId, static_cast<unsigned int>(GetDBID()), static_cast<int>(result)
        );
    }
}

void User::GiveItemsOnAccount(
    const int condition,
    const int deleteItemClassId,
    const std::int64_t deleteItemAmount,
    const int addItemCount,
    int* const addItemClassIds,
    std::int64_t* const addItemAmounts,
    int* const addItemEnchants,
    const int type
) {
    // Current L2Server 0x00b9475c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    std::array<ItemManip, 100> requests{};
    int requestCount = 0;

    const bool requestReady = [&] {
        L2_WRITE_GUARD(m_inv);
        if (deleteItemClassId > 0) {
            if (m_inv.GetItemAmount(deleteItemClassId, 0, true) < deleteItemAmount) {
                return false;
            }
            requests[requestCount++] = ItemManip(0, deleteItemClassId, -deleteItemAmount, 0, 0, 0, true);
        }
        if (addItemCount > 0) {
            for (int index = 0; index < addItemCount; ++index) {
                requests[requestCount++] =
                    ItemManip(0, addItemClassIds[index], addItemAmounts[index], addItemEnchants[index], 0, 0, true);
            }
        }
        return true;
    }();
    if (!requestReady) {
        CUserSocket* const socket = GetSocket();
        if (socket != nullptr) {
            socket->Send(
                "chccdcQ", 0x62, SystemMessageId::NAMED_ITEM_QUANTITY_REQUIRED, 2, 3, deleteItemClassId, 6,
                static_cast<long long>(deleteItemAmount)
            );
        }
        return;
    }

    EtcManiGiveItemOnAccount manipulation(
        static_cast<int>(GetDBID()), condition, deleteItemClassId, deleteItemAmount, addItemCount, addItemClassIds,
        addItemAmounts, addItemEnchants, type, GetAccountID()
    );
    CPacketHelperEx<20480> packet;
    manipulation.Assemble(packet);

    L2_GUARD(m_ManipulateQueueLock);
    etcMsg message = SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    ManipulateQueue queue = SaveManipulateQueue(
        ManipulateType::MANI_GIVE_ITEM_ON_ACCOUNT, 0, requestCount, requests.data(), nullptr, 1, packet, false,
        ItemGetReason::Etc, message
    );

    const ManiItemResultType result = _SetManiItemJob(queue);
    if (result == MRT_NOJOB) {
        static_cast<void>(_HandleItemManipulation(queue));
    } else if (result == MRT_JOBMAP) {
        SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
    } else if (result == MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x273b)
        // Noise: normal deferral behind a pending item operation. Not a failure.
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"user_item.cpp",
        //     0x273b)
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x2740)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"user_item.cpp",
        //     0x2740)
        L2_LOG_ERROR(
            Log, "[items] Operation %d, request %d, character %u: unexpected queue result %d; item operation not started.",
            static_cast<int>(queue.eType), queue.nId, static_cast<unsigned int>(GetDBID()), static_cast<int>(result)
        );
    }
}

void User::TurnOnAutoSoulshot(int itemClassId, int activeState, int uiSlot) {
    // Current L2Server 0x00b793a8.
    L2_GUARD_FUNCTION();

    CItem* const item = m_inv.GetFirstItemByClassID(itemClassId, false, false);
    if (item == nullptr) {
        return;
    }

    if (g_bUseAutoSoulShotUI && static_cast<unsigned int>(uiSlot) < 4U) {
        AutoSoulShotUi& ui = m_AutoSoulShotUi[uiSlot];
        ui.m_ItemClassID = itemClassId;
        ui.m_IsActiveState = activeState;
        GetSocket()->Send("chddd", 0xfe, 0x0c, ui.m_ItemClassID, ui.m_IsActiveState, uiSlot);
    }

    int row = -1;
    const ItemDefaultActionEnum action = item->m_const->m_enumDefaultAction;
    if (action == ITEM_ACTION_SUMMON_SOULSHOT || action == ITEM_ACTION_SUMMON_SPIRITSHOT) {
        row = static_cast<int>(action) - 4;
    } else if (action == ITEM_ACTION_SOULSHOT || action == ITEM_ACTION_SPIRITSHOT ||
        action == ITEM_ACTION_BLESS_SPIRITSHOT) {
        row = BaseCrystal[static_cast<int>(item->m_const->GetCrystalType())];
        if (row == 8) {
            row = 6;
        }
        if (action == ITEM_ACTION_SPIRITSHOT || action == ITEM_ACTION_BLESS_SPIRITSHOT) {
            row += 7;
        }
    } else if (action == ITEM_ACTION_FISHINGSHOT) {
        row = 16;
    } else {
        return;
    }
    if (row < 0) {
        return;
    }

    int column = 0;
    while (column < 32 && g_nSoulshotIDTables[row][column] != itemClassId) {
        ++column;
    }
    if (column < 32) {
        m_nAutoSoulshotFlags[row] |= std::uint32_t{1} << (static_cast<unsigned int>(column) & 31U);
    }

    int legacyBit = -1;
    if (itemClassId == 0x169d || itemClassId == 0x169e) {
        legacyBit = itemClassId - 0x169d;
    } else if (itemClassId == 0x72b || (itemClassId >= 0x5b7 && itemClassId <= 0x5bc)) {
        legacyBit = itemClassId - 0x5b2;
        if (legacyBit > 10) {
            legacyBit = 4;
        }
    } else if (static_cast<unsigned int>(itemClassId) - 0x9cdU < 7U) {
        legacyBit = itemClassId - 0x9c2;
    } else if (static_cast<unsigned int>(itemClassId) - 0xf6bU < 7U) {
        legacyBit = itemClassId - 0xf59;
    } else if (static_cast<unsigned int>(itemClassId) - 0x19f5U < 3U) {
        legacyBit = itemClassId - 0x19dc;
    } else if (static_cast<unsigned int>(itemClassId) - 0x872bU < 14U) {
        legacyBit = itemClassId - 0x870f;
    } else {
        return;
    }
    if (legacyBit >= 0) {
        m_nAutoSoulshotFlag |= std::uint32_t{1} << (static_cast<unsigned int>(legacyBit) & 31U);
    }
}

void User::TurnOffAutoSoulshot(int itemClassId, int activeState, int uiSlot, bool clearItemClassId) {
    L2_GUARD_FUNCTION();

    if (g_bUseAutoSoulShotUI) {
        if (uiSlot < 4) {
            AutoSoulShotUi& ui = m_AutoSoulShotUi[uiSlot];
            ui.m_ItemClassID = clearItemClassId ? 0 : itemClassId;
            ui.m_IsActiveState = activeState;
            GetSocket()->Send("chddd", 0xfe, 0x0c, ui.m_ItemClassID, ui.m_IsActiveState, uiSlot);
        }
    } else {
        GetSocket()->Send("chddd", 0xfe, 0x0c, itemClassId, 0, -1);
    }

    unsigned int foundRow = 17U;
    unsigned int foundColumn = 32U;
    for (unsigned int row = 0; row < 17U; ++row) {
        for (unsigned int column = 0; column < 32U; ++column) {
            if (g_nSoulshotIDTables[row][column] == itemClassId) {
                foundRow = row;
                foundColumn = column;
            }
        }
    }

    if (foundRow < 17U && foundColumn < 32U) {
        m_nAutoSoulshotFlags[foundRow] &= ~(std::uint32_t{1} << foundColumn);
        if (foundRow < 7U) {
            m_bWaitSoulShotUse = false;
        } else if (foundRow - 7U < 7U) {
            m_bWaitSpiritShotUse = false;
        } else if (foundRow == 14U) {
            m_bWaitSummonSoulShotUse = false;
        } else if (foundRow == 15U) {
            m_bWaitSummonSpiritShotUse = false;
        }
    }

    int legacyBit = -1;
    if (itemClassId == 0x169d || itemClassId == 0x169e) {
        legacyBit = itemClassId - 0x169d;
        if (itemClassId == 0x169d) {
            m_bWaitSoulShotUse = false;
        } else {
            m_bWaitSpiritShotUse = false;
        }
    } else if (itemClassId == 0x72b || (itemClassId >= 0x5b7 && itemClassId <= 0x5bc)) {
        legacyBit = itemClassId - 0x5b2;
        if (legacyBit > 10) {
            legacyBit = 4;
        }
        m_bWaitSoulShotUse = false;
    } else if (static_cast<unsigned int>(itemClassId) - 0x9cdU < 7U) {
        legacyBit = itemClassId - 0x9c2;
        m_bWaitSpiritShotUse = false;
    } else if (static_cast<unsigned int>(itemClassId) - 0xf6bU < 7U) {
        legacyBit = itemClassId - 0xf59;
        m_bWaitSpiritShotUse = false;
    } else if (static_cast<unsigned int>(itemClassId) - 0x19f5U < 3U) {
        legacyBit = itemClassId - 0x19dc;
        if (itemClassId == 0x19f5) {
            m_bWaitSummonSoulShotUse = false;
        } else {
            m_bWaitSummonSpiritShotUse = false;
        }
    } else if (static_cast<unsigned int>(itemClassId) - 0x872bU < 14U) {
        legacyBit = itemClassId - 0x870f;
        m_bWaitSpiritShotUse = false;
    } else {
        return;
    }

    if (legacyBit >= 0) {
        m_nAutoSoulshotFlag &= ~(std::uint32_t{1} << (static_cast<unsigned int>(legacyBit) & 31U));
    }
}

void User::TakeOffItems() {
    L2_GUARD_FUNCTION();

    for (int slot = 0; slot < ST_MAX; ++slot) {
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(m_data->m_nSlotIndex[slot]);
        if (item) {
            if (item->m_data->m_Slot == 0 || !CanEquip(item.get()) || item->m_const->m_bForNpc) {
                UnequipItem(static_cast<SlotType>(slot), false);
            }
        }
    }

    if (m_data->m_nNumInventory > 0) {
        CSharedItemData* const weapon = GetEquipedWeapon();
        if (weapon != nullptr &&
            ((weapon->m_nClassID >= 0x1e88 && weapon->m_nClassID <= 0x1e97) ||
                (weapon->m_nClassID >= 0x2180 && weapon->m_nClassID <= 0x218f))) {
            UnequipItem(ST_RHAND, false);
            UnequipItem(ST_LHAND, false);
            UnequipItem(ST_RLHAND, false);
        }
    }

    CFortress::HandleDeleteFlag(this, false);
}

bool User::MultiSellChoose(
    const int listId,
    const int entryId,
    const std::int64_t amount,
    const int enchanted,
    const ItemOptionKey optionKey,
    const BaseAttribute attribute,
    std::map<int, EnsoulOptionData*> ensoulOptions[3]
) {
    // Current L2Server 0x00b8a7ac, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    CMultiSellList* const list = static_cast<std::uint32_t>(listId) < UINT32_C(0x80000000)
        ? g_MultiSellDB.find(listId)
        : CEventMakerManager::inst().GetMultisellListPointer(listId);
    if (list == nullptr) {
        return false;
    }

    CNPC* npc = nullptr;
    if (!list->IsWithoutNPC()) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nMultisellNpcSid));
        if (creature != nullptr && creature->IsNPC()) {
            npc = creature->CastNPC();
        }
        if (npc == nullptr) {
            return false;
        }
    }
    if (!list->IsAllowed(this, npc) || amount < 1) {
        return false;
    }

    CMultiSellInfo* const info = list->find(entryId);
    if (info == nullptr) {
        return false;
    }
    if (list->m_nSSQFlag == 1 && SSQSystem::inst().GetCurrentStatus() != SSQStatus::SEAL_EFFECT) {
        return false;
    }

    {
        GuardHelper checkOutputGuard(L"User::MultiSellChoose::CheckValidOutputItems", CThreadManager::GetThreadID());
        bool hasOrdinaryOutput = false;
        bool hasArtifactOutput = false;
        for (MultiSellItemStruct* const itemData : info->m_vOutputList) {
            if (itemData->ItemClassId < 0) {
                const std::int64_t pointTotal = runtime::MultiplyInt64Wrapping(itemData->Count, amount);
                if (amount > INT64_C(1000000000000) ||
                    // (itemData->ItemClassId == -300 &&
                    (itemData->ItemClassId == MultiSellPointItem::MSIT_PVP_POINT && amount > INT64_C(20000000)) ||
                    runtime::AddInt64Wrapping(pointTotal, static_cast<std::int64_t>(GetPoint(5))) > INT64_C(20000000) ||
                    // (itemData->ItemClassId == -500 &&
                    (itemData->ItemClassId == MultiSellPointItem::MSIT_RAID_POINT && amount > INT64_C(20000000)) ||
                    runtime::AddInt64Wrapping(pointTotal, static_cast<std::int64_t>(GetPoint(8))) > INT64_C(20000000)) {
                    // if (itemData->ItemClassId == -300 &&
                    if (itemData->ItemClassId == MultiSellPointItem::MSIT_PVP_POINT &&
                        runtime::AddInt64Wrapping(pointTotal, static_cast<std::int64_t>(GetPoint(5))) >
                            INT64_C(20000000)) {
                        SendSystemMessage(SystemMessageId::FAME_LIMIT_REACHED);
                    }
                    // if (itemData->ItemClassId == -500 &&
                    if (itemData->ItemClassId == MultiSellPointItem::MSIT_RAID_POINT &&
                        runtime::AddInt64Wrapping(pointTotal, static_cast<std::int64_t>(GetPoint(8))) >
                            INT64_C(20000000)) {
                        SendSystemMessage(SystemMessageId::RAID_POINT_LIMIT_REACHED);
                    }
                    SendSystemMessage(SystemMessageId::PURCHASE_FAILED);
                    return false;
                }
                continue;
            }

            CItem* const item = static_cast<CItem*>(g_ObjectDB.GetObjectW(itemData->ItemClassId, false));
            if (item == nullptr) {
                return false;
            }
            if (item->m_data->GetLocked() != 0) {
                SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
                return false;
            }
            if (!CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType) && amount > 1) {
                return false;
            }
            if (amount > INT64_C(999999999999) ||
                runtime::AddInt64Wrapping(m_inv.GetItemAmount(itemData->ItemClassId, 0, true), amount) >
                    INT64_C(999999999999)) {
                SendSystemMessage(SystemMessageId::PURCHASE_FAILED);
                return false;
            }
            if (item->IsArtifact()) {
                hasArtifactOutput = true;
            } else {
                hasOrdinaryOutput = true;
            }
        }

        const bool inventoryFull =
            static_cast<float>(GetInventoryLimit()) * 0.9F < static_cast<float>(m_inv.GetTotalCount()) ||
            // m_nCurrentWeight > 2.
            m_nCurrentWeight > WEIGHT_HEAVY;
        const bool artifactCapacityAvailable =
            static_cast<float>(m_inv.GetArtifactCount()) <= static_cast<float>(GetArtifactInventoryLimit()) * 0.9F;
        if (hasOrdinaryOutput && hasArtifactOutput) {
            if (inventoryFull || !artifactCapacityAvailable) {
                SendSystemMessage(SystemMessageId::NORMAL_AND_ARTIFACT_INVENTORY_CAPACITY_RESTRICTION);
                return false;
            }
        } else if (hasArtifactOutput) {
            if (!artifactCapacityAvailable) {
                SendSystemMessage(SystemMessageId::ARTIFACT_INVENTORY_QUANTITY_MUST_BE_BELOW_90_PERCENT);
                return false;
            }
        } else if (inventoryFull) {
            SendSystemMessage(SystemMessageId::INVENTORY_WEIGHT_MUST_BE_BELOW_80_AND_QUANTITY_BELOW_90_PERCENT);
            return false;
        }
    }

    std::int64_t tax = 0;
    if (list->m_nIsDutyfree == 0 && !list->IsWithoutNPC()) {
        tax = runtime::MultiplyInt64Wrapping(static_cast<std::int64_t>(info->GetTaxTo(npc)), amount);
    }
    const std::int64_t originalTax = tax;

    int point = 0;
    int pointType = 0;
    std::int64_t adenaInputTotal = 0;
    std::int64_t specialInputTotal = 0;
    const int amount32 = runtime::Int32FromBits(static_cast<std::uint32_t>(amount));

    {
        GuardHelper checkInputGuard(L"User::MultiSellChoose::CheckInputItems", CThreadManager::GetThreadID());
        bool valid = true;
        int previousClassId = -1;
        int previousItemIndex = -1;
        std::int64_t cumulativeInput = 0;

        for (MultiSellItemStruct* const itemData : info->m_vInputList) {
            const std::int64_t itemTotal = runtime::MultiplyInt64Wrapping(itemData->Count, amount);
            cumulativeInput = runtime::AddInt64Wrapping(cumulativeInput, itemTotal);
            if (itemData->ItemClassId == 0x39) {
                cumulativeInput = runtime::AddInt64Wrapping(cumulativeInput, tax);
                adenaInputTotal =
                    runtime::AddInt64Wrapping(adenaInputTotal, runtime::AddInt64Wrapping(tax, itemTotal));
            } else if (itemData->ItemClassId == 0x840c) {
                specialInputTotal = runtime::AddInt64Wrapping(specialInputTotal, itemTotal);
            }

            if (cumulativeInput > INT64_C(999999999999)) {
                SendSystemMessage(SystemMessageId::PURCHASE_FAILED);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] MultiSellChoose request overflow int64 "
                //     L"error. group id[%d], selected item[%d], "
                //     L"amount[%I64d]",
                //     "user_item.cpp",
                //     0x1771,
                //     listId,
                //     entryId,
                //     amount)
                L2_LOG_ERROR(
                    Log,
                    u"[multisell] Input total exceeds the supported range for list %d, entry %d, amount %I64d; purchase "
                    u"rejected.",
                    listId, entryId, amount
                );
                return false;
            }

            if (itemData->ItemClassId < 0) {
                const int count32 = runtime::Int32FromBits(static_cast<std::uint32_t>(itemData->Count));
                // if (itemData->ItemClassId == -200) {
                if (itemData->ItemClassId == MultiSellPointItem::MSIT_PLEDGE_POINT) {
                    point = runtime::NegateInt32Wrapping(runtime::MultiplyInt32Wrapping(amount32, count32));
                    if (!HavePledgePower(PP_CONTROL_SYSTEM_FUNC)) {
                        SendSystemMessage(SystemMessageId::NO_AUTHORITY);
                        return false;
                    }
                    CPledge* const pledge = GetPledge();
                    const int requiredPledge = runtime::NegateInt32Wrapping(point);
                    if (pledge == nullptr || pledge->GetNameValue() < requiredPledge ||
                        -static_cast<std::int64_t>(point) != itemTotal) {
                        SendSystemMessage(SystemMessageId::NOT_ENOUGH_CLAN_REPUTATION);
                        return false;
                    }
                    pointType = 2;
                    continue;
                }
                // if (itemData->ItemClassId == -400) {
                if (itemData->ItemClassId == MultiSellPointItem::MSIT_FIELD_CYCLE_POINT) {
                    if (-static_cast<std::int64_t>(point) != itemTotal) {
                        return false;
                    }
                    point = runtime::MultiplyInt32Wrapping(amount32, count32);
                    pointType = 7;
                    continue;
                }
                // if (itemData->ItemClassId == -100) {
                if (itemData->ItemClassId == MultiSellPointItem::MSIT_PCCAFE_POINT) {
                    pointType = 1;
                    // } else if (itemData->ItemClassId == -300) {
                } else if (itemData->ItemClassId == MultiSellPointItem::MSIT_PVP_POINT) {
                    pointType = 5;
                    // } else if (itemData->ItemClassId == -500) {
                } else if (itemData->ItemClassId == MultiSellPointItem::MSIT_RAID_POINT) {
                    pointType = 8;
                }
                point = runtime::NegateInt32Wrapping(runtime::MultiplyInt32Wrapping(amount32, count32));
                if (!CheckPointAddable(pointType, point) || -static_cast<std::int64_t>(point) != itemTotal) {
                    if (pointType == 5) {
                        SendSystemMessage(0x917);
                    } else if (pointType == 8) {
                        SendSystemMessage(SystemMessageId::NOT_ENOUGH_RAID_POINTS);
                    } else {
                        SendSystemMessage(SystemMessageId::NOT_ENOUGH_PC_POINTS);
                    }
                    return false;
                }
                continue;
            }

            CItem* item =
                m_inv.GetFirstItemByClassID(itemData->ItemClassId, list->m_bShowVariationItem, list->m_bShowEnsoulItem);
            while (item != nullptr && (IsEquipedItem(item->m_data->index) || item->m_data->GetLocked() != 0)) {
                item = m_inv.GetNextItemByClassID(item->m_data->index, list->m_bShowVariationItem, list->m_bShowEnsoulItem);
            }
            if (item == nullptr) {
                if (itemData->ItemClassId == 0x39) {
                    SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
                } else {
                    valid = false;
                    GetSocket()->Send(
                        "chccdcQ", 0x62, SystemMessageId::NAMED_ITEM_QUANTITY_REQUIRED, 2, 3, itemData->ItemClassId, 6,
                        itemTotal
                    );
                    continue;
                }
                return false;
            }

            const bool stackable = CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType);
            if (!stackable && (amount != 1 || itemData->Count != 1)) {
                return false;
            }
            if (!m_inv.HaveItemByClassId(
                    itemData->ItemClassId, itemTotal, list->m_bShowVariationItem, list->m_bShowEnsoulItem
                )) {
                if (itemData->ItemClassId == 0x39) {
                    SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
                } else {
                    valid = false;
                    GetSocket()->Send(
                        "chccdcQ", 0x62, SystemMessageId::NAMED_ITEM_QUANTITY_REQUIRED, 2, 3, itemData->ItemClassId, 6,
                        runtime::SubtractInt64Wrapping(itemTotal, item->m_data->m_nCount)
                    );
                    continue;
                }
                return false;
            }

            if (!stackable && (item->IsWeapon() || item->IsArmor() || item->IsAccessary())) {
                if (list->m_nKeepEnchanted == 0) {
                    if (previousClassId == itemData->ItemClassId) {
                        do {
                            item = m_inv.GetNextMinEnchantedItemByClassID(
                                itemData->ItemClassId, previousItemIndex, false, list->m_bShowEnsoulItem
                            );
                            if (item == nullptr) {
                                break;
                            }
                            previousItemIndex = item->m_data->index;
                        } while (IsEquipedItem(item->m_data->index) || item->m_data->GetLocked() != 0);
                    } else {
                        item = m_inv.GetMinEnchantedItemByClassID(itemData->ItemClassId, false, list->m_bShowEnsoulItem);
                        while (item != nullptr && (IsEquipedItem(item->m_data->index) || item->m_data->GetLocked() != 0)) {
                            item = m_inv.GetNextMinEnchantedItemByClassID(
                                itemData->ItemClassId, item->m_data->index, false, list->m_bShowEnsoulItem
                            );
                        }
                    }
                    if (item != nullptr) {
                        previousClassId = itemData->ItemClassId;
                        previousItemIndex = item->m_data->index;
                    }
                } else {
                    item = m_inv.GetFirstItemByClassIDEnchantedAndOption(itemData->ItemClassId, enchanted, optionKey);
                    while (item != nullptr) {
                        const BaseAttribute itemAttribute = item->GetTotalAttribute();
                        if (attribute == itemAttribute &&
                            (!list->m_bKeepEnsoul || CItem::CompareOptionData(item->m_mEnsoulInfo, ensoulOptions)) &&
                            item->m_data->GetLocked() == 0 && !IsEquipedItem(item->m_data->index)) {
                            break;
                        }
                        item = m_inv.GetNextItemByClassIDEnchantedAndOption(item->m_data->index, enchanted, optionKey);
                    }
                }
                if (item == nullptr) {
                    valid = false;
                    GetSocket()->Send(
                        "chccdcQ", 0x62, SystemMessageId::NAMED_ITEM_QUANTITY_REQUIRED, 2, 3, itemData->ItemClassId, 6,
                        itemTotal
                    );
                    continue;
                }
            }
        }
        if (!valid) {
            return false;
        }
    }

    std::array<char, 20480> itemPacket{};
    int itemPacketPosition = 0;
    int itemManipulationCount = 0;
    int ordinarySlotDelta = 0;
    int artifactSlotDelta = 0;
    std::int64_t weightDelta = 0;
    int savedIntensiveItemClassId = 0;
    BaseAttribute savedAttribute;
    std::map<int, EnsoulOptionData*> savedEnsoulOptions[3];
    int savedShapeClassId = 0;
    int savedShapePeriod = -1;

    {
        GuardHelper assembleInputGuard(
            L"User::MultiSellChoose::AssembleInputItemManipulates", CThreadManager::GetThreadID()
        );
        int previousClassId = -1;
        int previousItemIndex = -1;

        for (MultiSellItemStruct* const itemData : info->m_vInputList) {
            if (itemData->ItemClassId < 0) {
                continue;
            }

            CItem* item =
                m_inv.GetFirstItemByClassID(itemData->ItemClassId, list->m_bShowVariationItem, list->m_bShowEnsoulItem);
            while (item != nullptr && (IsEquipedItem(item->m_data->index) || item->m_data->GetLocked() != 0)) {
                item = m_inv.GetNextItemByClassID(item->m_data->index, list->m_bShowVariationItem, list->m_bShowEnsoulItem);
            }

            if (item != nullptr && !CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType) &&
                (item->IsWeapon() || item->IsArmor() || item->IsAccessary())) {
                if (list->m_nKeepEnchanted == 0) {
                    if (previousClassId == itemData->ItemClassId) {
                        do {
                            item = m_inv.GetNextMinEnchantedItemByClassID(
                                itemData->ItemClassId, previousItemIndex, false, list->m_bShowEnsoulItem
                            );
                            if (item == nullptr) {
                                break;
                            }
                            previousItemIndex = item->m_data->index;
                        } while (IsEquipedItem(item->m_data->index) || item->m_data->GetLocked() != 0);
                    } else {
                        item = m_inv.GetMinEnchantedItemByClassID(itemData->ItemClassId, false, list->m_bShowEnsoulItem);
                        while (item != nullptr && (IsEquipedItem(item->m_data->index) || item->m_data->GetLocked() != 0)) {
                            item = m_inv.GetNextMinEnchantedItemByClassID(
                                itemData->ItemClassId, item->m_data->index, false, list->m_bShowEnsoulItem
                            );
                        }
                    }
                    if (item != nullptr) {
                        previousClassId = itemData->ItemClassId;
                        previousItemIndex = item->m_data->index;
                    }
                } else {
                    item = m_inv.GetFirstItemByClassIDEnchantedAndOption(itemData->ItemClassId, enchanted, optionKey);
                    while (item != nullptr) {
                        const BaseAttribute itemAttribute = item->GetTotalAttribute();
                        if (attribute == itemAttribute &&
                            (!list->m_bKeepEnsoul || CItem::CompareOptionData(ensoulOptions, item->m_mEnsoulInfo)) &&
                            item->m_data->GetLocked() == 0 && !IsEquipedItem(item->m_data->index)) {
                            break;
                        }
                        item = m_inv.GetNextItemByClassIDEnchantedAndOption(item->m_data->index, enchanted, optionKey);
                    }
                    if (item != nullptr) {
                        savedIntensiveItemClassId = item->m_itemOption.intensiveItemClassID;
                        savedAttribute = item->m_itemBaseAttribute;
                    }
                }

                if (item != nullptr && list->m_nKeepShapeShifted != 0) {
                    savedShapeClassId = item->m_data->m_nShapeShiftedClassId;
                    savedShapePeriod = GetPeriodLeftForShapeShiftedItem(static_cast<int>(item->GetDBID()));
                    if (savedShapePeriod == -9999) {
                        savedShapePeriod = -1;
                    }
                }
                if (item != nullptr && list->m_bKeepEnsoul) {
                    for (int slot = 1; slot <= 2; ++slot) {
                        for (const auto& entry : item->m_mEnsoulInfo[slot]) {
                            savedEnsoulOptions[slot].insert(entry);
                        }
                    }
                }
            }

            if (item == nullptr) {
                GetSocket()->Send("chccd", 0x62, SystemMessageId::NAMED_ITEM_REQUIRED, 1, 3, itemData->ItemClassId);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] fee Item[%d] is not exist.",
                //     L"user_item.cpp",
                //     0x18e7,
                //     itemData->ItemClassId)
                L2_LOG_ERROR(
                    Log,
                    u"[multisell] Required input item class %d is missing from character %u inventory; purchase rejected.",
                    itemData->ItemClassId, GetDBID()
                );
                return false;
            }

            std::int64_t itemTotal = 1;
            bool removesInventorySlot = true;
            if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
                itemTotal = runtime::MultiplyInt64Wrapping(itemData->Count, amount);
                if (itemData->ItemClassId == 0x39) {
                    itemTotal = runtime::AddInt64Wrapping(itemTotal, tax);
                    tax = 0;
                    if (item->m_data->m_nCount < itemTotal) {
                        SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
                        return false;
                    }
                }
                removesInventorySlot = item->m_data->m_nCount == itemTotal;
            }
            if (removesInventorySlot) {
                if (item->IsArtifact()) {
                    artifactSlotDelta = runtime::SubtractInt32Wrapping(artifactSlotDelta, 1);
                } else {
                    ordinarySlotDelta = runtime::SubtractInt32Wrapping(ordinarySlotDelta, 1);
                }
            }

            const int itemWeight = item->m_data->m_ItemConsumeType == ICT_ASSET ? 0 : item->m_const->m_nWeight;
            weightDelta = runtime::SubtractInt64Wrapping(
                weightDelta, runtime::MultiplyInt64Wrapping(static_cast<std::int64_t>(itemWeight), itemTotal)
            );

            InventoryParams parameters;
            parameters.nItemClassID = itemData->ItemClassId;
            parameters.nCount = runtime::NegateInt64Wrapping(itemTotal);
            parameters.nEnchanted = item->m_data->GetEnchanted();
            parameters.nShapeShiftedClassId = item->m_data->m_nShapeShiftedClassId;
            if (parameters.nShapeShiftedClassId > 0) {
                parameters.nShapeShiftedPeriod = GetPeriodLeftForShapeShiftedItem(static_cast<int>(item->GetDBID()));
                if (parameters.nShapeShiftedPeriod == -9999) {
                    parameters.nShapeShiftedPeriod = -1;
                }
            }
            for (int slot = 1; slot <= 2; ++slot) {
                for (const auto& entry : savedEnsoulOptions[slot]) {
                    parameters.mEnsoulOptionMap[slot].insert(entry);
                }
            }
            if (MsgHelper_ManipulateItem(
                    itemPacket.data(), static_cast<int>(itemPacket.size()), itemPacketPosition, item, parameters
                ) == 0) {
                return false;
            }
            ++itemManipulationCount;
        }

        if (tax > 0) {
            CItem* const adena = m_inv.GetFirstItemByClassID(0x39, false, false);
            if (adena == nullptr || !m_inv.HaveItemByClassId(0x39, tax, false, false)) {
                SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] fee Item[%d] is not exist.",
                //     L"user_item.cpp",
                //     0x1933,
                //     0x39)
                L2_LOG_ERROR(
                    Log, u"[multisell] Character %u lacks the additional adena tax of %I64d; purchase rejected.", GetDBID(),
                    tax
                );
                return false;
            }
            if (adena->m_data->m_nCount == tax) {
                ordinarySlotDelta = runtime::SubtractInt32Wrapping(ordinarySlotDelta, 1);
            }
            const int itemWeight = adena->m_data->m_ItemConsumeType == ICT_ASSET ? 0 : adena->m_const->m_nWeight;
            weightDelta = runtime::SubtractInt64Wrapping(
                weightDelta, runtime::MultiplyInt64Wrapping(static_cast<std::int64_t>(itemWeight), tax)
            );

            InventoryParams parameters;
            parameters.nItemClassID = 0x39;
            parameters.nCount = runtime::NegateInt64Wrapping(tax);
            if (MsgHelper_ManipulateItem(
                    itemPacket.data(), static_cast<int>(itemPacket.size()), itemPacketPosition, adena, parameters
                ) == 0) {
                return false;
            }
            ++itemManipulationCount;
        }
    }

    const double outputRoll = ::genrand64_real1() * 100.0 + 0.0;
    {
        GuardHelper assembleOutputGuard(
            L"User::MultiSellChoose::AssembleOutputItemManipulates", CThreadManager::GetThreadID()
        );
        double accumulatedProbability = 0.0;
        for (MultiSellItemStruct* const itemData : info->m_vOutputList) {
            if (list->m_nType == 1) {
                const double nextProbability = accumulatedProbability + itemData->Probability;
                if (!(outputRoll <= nextProbability)) {
                    accumulatedProbability = nextProbability;
                    continue;
                }
            }

            if (itemData->ItemClassId < 0) {
                const int count32 = runtime::Int32FromBits(static_cast<std::uint32_t>(itemData->Count));
                point = runtime::MultiplyInt32Wrapping(count32, amount32);
                // if (itemData->ItemClassId == -200) {
                if (itemData->ItemClassId == MultiSellPointItem::MSIT_PLEDGE_POINT) {
                    if (!HavePledgePower(PP_CONTROL_SYSTEM_FUNC)) {
                        SendSystemMessage(SystemMessageId::NO_AUTHORITY);
                        return false;
                    }
                    pointType = 2;
                    // } else if (itemData->ItemClassId == -400) {
                } else if (itemData->ItemClassId == MultiSellPointItem::MSIT_FIELD_CYCLE_POINT) {
                    pointType = 7;
                } else {
                    // if (itemData->ItemClassId == -100) {
                    if (itemData->ItemClassId == MultiSellPointItem::MSIT_PCCAFE_POINT) {
                        pointType = 1;
                        // } else if (itemData->ItemClassId == -300) {
                    } else if (itemData->ItemClassId == MultiSellPointItem::MSIT_PVP_POINT) {
                        pointType = 5;
                        // } else if (itemData->ItemClassId == -500) {
                    } else if (itemData->ItemClassId == MultiSellPointItem::MSIT_RAID_POINT) {
                        pointType = 8;
                    }
                    if (((pointType == 5 && !CanGetPVPPoint()) || pointType == 8) && point > 0) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"%s user cannot get pvp/raid point, but "
                        //     L"attempted to get pvp/raid point.",
                        //     GetName())
                        // L2_LOG_ERROR(Log,
                        //     L"[points] User '%s' attempted to receive "
                        //     L"unavailable PvP or raid points. Request rejected.",
                        //     GetName())
                        L2_LOG_ERROR(
                            Log,
                            u"[points] User '%s' attempted to receive "
                            u"unavailable PvP or raid points; request rejected.",
                            GetName()
                        );
                        return false;
                    }
                }
            } else {
                InventoryParams parameters;
                CItem* const item = static_cast<CItem*>(g_ObjectDB.GetObjectW(itemData->ItemClassId, false));
                if (item == nullptr) {
                    return false;
                }
                if (item->m_data->GetLocked() != 0) {
                    SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
                    return false;
                }

                std::int64_t itemTotal = itemData->Count;
                if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
                    itemTotal = runtime::MultiplyInt64Wrapping(itemTotal, amount);
                    CItem* const existing = m_inv.GetFirstItemByClassID(itemData->ItemClassId, false, false);
                    if (existing == nullptr) {
                        if (item->IsArtifact()) {
                            artifactSlotDelta = runtime::AddInt32Wrapping(artifactSlotDelta, 1);
                        } else {
                            ordinarySlotDelta = runtime::AddInt32Wrapping(ordinarySlotDelta, 1);
                        }
                    }
                } else {
                    const int slotCount = runtime::Int32FromBits(static_cast<std::uint32_t>(itemTotal));
                    if (item->IsArtifact()) {
                        artifactSlotDelta = runtime::AddInt32Wrapping(artifactSlotDelta, slotCount);
                    } else {
                        ordinarySlotDelta = runtime::AddInt32Wrapping(ordinarySlotDelta, slotCount);
                    }
                    if (list->m_nKeepEnchanted != 0) {
                        parameters.nEnchanted = enchanted;
                        parameters.key = optionKey;
                        parameters.nIntensiveItemClassID = savedIntensiveItemClassId;
                        parameters.attribute = savedAttribute;
                    }
                    if (list->m_nKeepShapeShifted != 0) {
                        parameters.nShapeShiftedClassId = savedShapeClassId;
                        parameters.nShapeShiftedPeriod = savedShapePeriod;
                    }
                    if (list->m_bKeepEnsoul) {
                        for (int slot = 1; slot <= 2; ++slot) {
                            for (const auto& entry : ensoulOptions[slot]) {
                                parameters.mEnsoulOptionMap[slot].insert(entry);
                            }
                        }
                    }
                    if (list->m_nType == 1 && itemData->nEnchant >= 0) {
                        parameters.nEnchanted = itemData->nEnchant;
                    }
                }

                const int itemWeight = item->m_data->m_ItemConsumeType == ICT_ASSET ? 0 : item->m_const->m_nWeight;
                weightDelta = runtime::AddInt64Wrapping(
                    weightDelta, runtime::MultiplyInt64Wrapping(static_cast<std::int64_t>(itemWeight), itemTotal)
                );
                parameters.nItemClassID = itemData->ItemClassId;
                parameters.nCount = itemTotal;
                if (MsgHelper_ManipulateItem(
                        itemPacket.data(), static_cast<int>(itemPacket.size()), itemPacketPosition, item, parameters
                    ) == 0) {
                    return false;
                }
                ++itemManipulationCount;
            }

            if (list->m_nType == 1) {
                break;
            }
        }
    }

    bool failed = false;
    const int ordinarySlots = runtime::AddInt32Wrapping(m_data->m_nNumInventory, ordinarySlotDelta);
    if (ordinarySlots < 0) {
        SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
        failed = true;
    }
    const int artifactSlots = runtime::AddInt32Wrapping(m_data->m_nCurrentArtifactInventoryNum, artifactSlotDelta);
    if (artifactSlots < 0) {
        SendSystemMessage(SystemMessageId::INVENTORY_SLOTS_FULL);
        failed = true;
    }

    const bool artifactSlotsFit = artifactSlots <= GetArtifactInventoryLimit();
    if (GetInventoryLimit() < ordinarySlots) {
        SendSystemMessage(
            artifactSlotsFit ? SystemMessageId::NOT_ENOUGH_INVENTORY_SPACE
                             : SystemMessageId::NOT_ENOUGH_NORMAL_AND_ARTIFACT_INVENTORY_SPACE
        );
        failed = true;
    } else if (!artifactSlotsFit) {
        SendSystemMessage(SystemMessageId::NOT_ENOUGH_ARTIFACT_INVENTORY_SPACE);
        failed = true;
    }

    const std::int64_t nextWeight =
        runtime::AddInt64Wrapping(static_cast<std::int64_t>(m_inv.GetCarryingWeight()), weightDelta);
    if (nextWeight < 0) {
        SendSystemMessage(SystemMessageId::WEIGHT_LIMIT_EXCEEDED);
        failed = true;
    }
    if (CarryWeight() < static_cast<double>(nextWeight)) {
        SendSystemMessage(SystemMessageId::WEIGHT_LIMIT_EXCEEDED);
        failed = true;
    }

    if (pointType == 0) {
        double accumulatedProbability = 0.0;
        for (MultiSellItemStruct* const itemData : info->m_vOutputList) {
            if (list->m_nType == 1) {
                const double nextProbability = accumulatedProbability + itemData->Probability;
                if (!(outputRoll <= nextProbability)) {
                    accumulatedProbability = nextProbability;
                    continue;
                }
            }
            if (itemData->ItemClassId < 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"User::MultiSellChooseLog. MultiSellItem is Point")
                L2_LOG_ERROR(
                    Log,
                    u"[multisell] Point-based entry reached the item audit "
                    u"path; item audit skipped."
                );
            }

            CItem* const item = static_cast<CItem*>(g_ObjectDB.GetObjectW(itemData->ItemClassId, false));
            if (item == nullptr) {
                return false;
            }
            if (item->m_data->GetLocked() != 0) {
                SendSystemMessage(SystemMessageId::ITEM_SEALED_UNSEAL_BEFORE_USE);
                return false;
            }
            std::int64_t itemTotal = itemData->Count;
            if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
                itemTotal = runtime::MultiplyInt64Wrapping(itemTotal, amount);
            }

            if (npc != nullptr || list->IsWithoutNPC()) {
                std::array<char16_t, 256> attributeLog{};
                std::array<char16_t, 256> shapeShiftLog{};
                std::array<char16_t, 256> ensoulLog{};
                item->m_itemBaseAttribute.GetLogString(attributeLog.data());
                item->MakeShapeShiftLogInfo(shapeShiftLog.data());
                item->MakeEnsoulLogInfo(ensoulLog.data());
                const int npcClassId = npc == nullptr ? -1 : runtime::SubtractInt32Wrapping(npc->GetClassId(), 1000000);
                CLogFormatter formatter;
                formatter.WriteLog1_1(
                    0x3c8, this, nullptr, attributeLog.data(), shapeShiftLog.data(), ensoulLog.data(), itemData->ItemClassId,
                    itemTotal, npcClassId, adenaInputTotal, specialInputTotal, failed ? 1 : 0, 0
                );
            }
            if (list->m_nType == 1) {
                break;
            }
        }
        if (failed) {
            return false;
        }
    }

    if (itemManipulationCount < 1) {
        // Log.Add(LOG_ERROR, L"MultiSell fail")
        L2_LOG_ERROR(Log, u"[multisell] Exchange failed; no result item was delivered.");
        return false;
    }

    EtcManiMultisellResult result;
    result.m_nUserDBID = static_cast<int>(GetDBID());
    result.m_nUserSID = static_cast<int>(Id());
    result.m_ePointType = pointType;
    result.m_nPoint = point;
    result.m_nGroupId = listId;
    result.m_bSuccess = true;
    CPacketHelperEx<20480> etcPacket;
    result.Assemble(etcPacket);

    if (pointType == 0) {
        db.CreateUpdateItems(
            this, itemManipulationCount, itemPacketPosition, itemPacket.data(), nullptr, 1, &etcPacket, false
        );
    } else {
        db.CreateUpdateItemsWithPoint(
            this, point, pointType, point < 0 ? 3 : 0, itemManipulationCount, 1, &etcPacket, itemPacketPosition,
            itemPacket.data(), nullptr, 0
        );
    }

    if (!list->IsWithoutNPC() && npc != nullptr) {
        CResidence* const residence = g_ResidenceDB.GetResidence(npc->m_data->m_nResidenceID);
        if (residence != nullptr && originalTax > 0) {
            residence->ReportShopTax(originalTax, CastleType::CASTLE_NOTHING, true);
        }
    }
    return true;
}

bool User::ReplyCancelVariationItem(CItem* targetItem, const std::int64_t adenaAmount) {
    // Current L2Server 0x00b8c960, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (targetItem == nullptr) {
        return false;
    }

    CItem* const adenaItem = m_inv.GetFirstItemByClassID(57, false, false);
    if (adenaItem == nullptr && adenaAmount > 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item classid[%d] is not exist.",
        //     L"user_item.cpp",
        //     0x2153,
        //     57)
        L2_LOG_ERROR(
            Log,
            u"[augmentation] Character %u has no adena item while cancelling augmentation with remaining amount %I64d; "
            u"request rejected.",
            GetDBID(), adenaAmount
        );
        return false;
    }

    std::int64_t consumedAdena = 0;
    if (adenaItem != nullptr) {
        consumedAdena = runtime::SubtractInt64Wrapping(adenaItem->m_data->m_nCount, adenaAmount);
        if (adenaAmount < 1) {
            m_inv.Pop(adenaItem->m_data->index, false);
            m_inv.SetInventoryChanged(adenaItem, IUT_DELETE);
            adenaItem->Delete();
        } else {
            {
                L2_GUARD(*adenaItem->m_lock);
                if (!adenaItem->SetAmount(adenaAmount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     "user_item.cpp",
                    //     0x216b,
                    //     adenaItem->m_data->m_nCount,
                    //     adenaAmount)
                    L2_LOG_ERROR(
                        Log, u"[augmentation] Adena item DB ID %u rejected requested amount %I64d; current amount is %I64d.",
                        adenaItem->GetDBID(), adenaAmount, adenaItem->m_data->m_nCount
                    );
                    return false;
                }
                m_inv.SetInventoryChanged(adenaItem, IUT_UPDATE);
            }
        }
    }

    ItemOptionKey oldOptionKey;
    int oldIntensiveItemClassId;
    {
        L2_GUARD(*targetItem->m_lock);
        oldOptionKey = targetItem->m_itemOption.itemOptionKey;
        oldIntensiveItemClassId = targetItem->m_itemOption.intensiveItemClassID;
        targetItem->m_itemOption = g_VariationDB.GetNewBlankItemOption();
        targetItem->m_data->m_bIsVariation = false;
    }
    m_inv.SetInventoryChanged(targetItem, IUT_UPDATE);

    ValidateChanged(VB_MAX);
    GetSocket()->Send("chd", 0xfe, 0x59, 1);
    m_data->m_NeedUpdatePacketBitSet.SetBit(0x49);
    ValidateChanged(VB_MAX);
    SendItemList(true, true, nullptr);
    runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);
    GetSocket()->Send(
        "chccQ", 0x62, SystemMessageId::ADENA_DISAPPEARED, 1, 6, runtime::AbsoluteInt64Wrapping(consumedAdena)
    );

    std::array<char16_t, 256> attributeWide{};
    std::array<char16_t, 256> shapeWide{};
    std::array<char16_t, 256> ensoulWide{};
    targetItem->m_itemBaseAttribute.GetLogString(attributeWide.data());
    targetItem->MakeShapeShiftLogInfo(shapeWide.data());
    targetItem->MakeEnsoulLogInfo(ensoulWide.data());

    std::array<char16_t, 256> attribute{};
    std::array<char16_t, 256> shape{};
    std::array<char16_t, 256> ensoul{};
    UserItemHostWideToTargetUtf16(attributeWide.data(), attribute);
    UserItemHostWideToTargetUtf16(shapeWide.data(), shape);
    UserItemHostWideToTargetUtf16(ensoulWide.data(), ensoul);

    static_cast<void>(GetPos());
    const int enchanted = targetItem->m_data->GetEnchanted();
    const int logZ = runtime::Win64CvttDoubleToInt32(GetPos().z);
    const int logY = runtime::Win64CvttDoubleToInt32(GetPos().y);
    const int logX = runtime::Win64CvttDoubleToInt32(GetPos().x);
    const unsigned int targetItemDbId = targetItem->GetDBID();
    std::u16string userNameStorage;
    const char16_t* const userName = (userNameStorage = GetName()).c_str();
    const std::uint32_t accountId = GetAccountID();
    const unsigned int userDbId = GetDBID();

    CLogFormatter formatter;
    formatter.WriteLog(
        0x3d3, userDbId, accountId, 0, 0, logX, logY, logZ, attribute.data(), shape.data(), ensoul.data(),
        targetItem->m_nClassId, enchanted, oldOptionKey.variationOption1, oldOptionKey.variationOption2, consumedAdena,
        oldIntensiveItemClassId, 0, 0, 0, 0, userName, m_sAccountName.data(), nullptr, nullptr, targetItemDbId
    );
    return true;
}

bool User::ReplyVariationItem(
    CItem* targetItem,
    CItem* mineralItem,
    CItem* supportItem,
    const std::int64_t supportItemAmount,
    const ItemOptionKey optionKey,
    const std::int64_t mineralItemAmount
) {
    // Current L2Server 0x00b8cf18, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (targetItem == nullptr || mineralItem == nullptr || (supportItemAmount >= 0 && supportItem == nullptr)) {
        return false;
    }

    if (mineralItemAmount < 1) {
        m_inv.Pop(mineralItem->m_data->index, false);
        m_inv.SetInventoryChanged(mineralItem, IUT_DELETE);
        mineralItem->Delete();
    } else {
        {
            L2_GUARD(*mineralItem->m_lock);
            if (!mineralItem->SetAmount(mineralItemAmount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%d][%d]",
                //     "user_item.cpp",
                //     0x21c7,
                //     mineralItem->m_data->m_nCount,
                //     mineralItemAmount)
                L2_LOG_ERROR(
                    Log, u"[augmentation] Mineral item DB ID %u rejected requested amount %I64d; current amount is %I64d.",
                    mineralItem->GetDBID(), static_cast<long long>(mineralItemAmount),
                    static_cast<long long>(mineralItem->m_data->m_nCount)
                );
                return false;
            }
        }
        m_inv.SetInventoryChanged(mineralItem, IUT_UPDATE);
    }

    std::int64_t consumedSupportItem = 0;
    if (supportItemAmount >= 0) {
        consumedSupportItem = runtime::SubtractInt64Wrapping(supportItem->m_data->m_nCount, supportItemAmount);
        if (supportItemAmount < 1) {
            m_inv.Pop(supportItem->m_data->index, false);
            m_inv.SetInventoryChanged(supportItem, IUT_DELETE);
            supportItem->Delete();
        } else {
            {
                L2_GUARD(*supportItem->m_lock);
                if (!supportItem->SetAmount(supportItemAmount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     "user_item.cpp",
                    //     0x21e1,
                    //     supportItem->m_data->m_nCount,
                    //     supportItemAmount)
                    L2_LOG_ERROR(
                        Log,
                        u"[augmentation] Support item DB ID %u rejected requested amount %I64d; current amount is %I64d.",
                        supportItem->GetDBID(), supportItemAmount, supportItem->m_data->m_nCount
                    );
                    return false;
                }
            }
            m_inv.SetInventoryChanged(supportItem, IUT_UPDATE);
        }
    }

    {
        L2_GUARD(*targetItem->m_lock);
        targetItem->SetVariation(optionKey, static_cast<std::uint32_t>(mineralItem->m_nClassId));
    }
    m_inv.SetInventoryChanged(targetItem, IUT_UPDATE);

    ValidateChanged(VB_MAX);
    GetSocket()->Send("chddd", 0xfe, 0x57, optionKey.variationOption1, optionKey.variationOption2, 1);
    m_data->m_NeedUpdatePacketBitSet.SetBit(0x49);
    SendItemList(true, true, nullptr);
    ParamChanged(PCB_USER);
    runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);

    const FVector position = GetPos();
    std::array<char16_t, 256> attributeWide{};
    std::array<char16_t, 256> shapeWide{};
    std::array<char16_t, 256> ensoulWide{};
    targetItem->m_itemBaseAttribute.GetLogString(attributeWide.data());
    targetItem->MakeShapeShiftLogInfo(shapeWide.data());
    targetItem->MakeEnsoulLogInfo(ensoulWide.data());

    std::array<char16_t, 256> attribute{};
    std::array<char16_t, 256> shape{};
    std::array<char16_t, 256> ensoul{};
    UserItemHostWideToTargetUtf16(attributeWide.data(), attribute);
    UserItemHostWideToTargetUtf16(shapeWide.data(), shape);
    UserItemHostWideToTargetUtf16(ensoulWide.data(), ensoul);

    const int supportItemClassId = supportItemAmount < 0 ? 0 : supportItem->m_nClassId;
    const int enchanted = targetItem->m_data->GetEnchanted();
    const unsigned int targetItemDbId = targetItem->GetDBID();
    std::u16string userNameStorage;
    const char16_t* const userName = (userNameStorage = GetName()).c_str();
    const std::uint32_t accountId = GetAccountID();
    const unsigned int userDbId = GetDBID();

    CLogFormatter formatter;
    formatter.WriteLog(
        0x3d2, userDbId, accountId, 0, 0, runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z), attribute.data(),
        shape.data(), ensoul.data(), targetItem->m_nClassId, enchanted,
        targetItem->m_itemOption.itemOptionKey.variationOption1, targetItem->m_itemOption.itemOptionKey.variationOption2,
        targetItem->m_itemOption.intensiveItemClassID, supportItemClassId, consumedSupportItem, 0, 0, 0, userName,
        m_sAccountName.data(), nullptr, nullptr, targetItemDbId
    );

    g_npcserver.Send("cdddd", 0xc6, m_data->index, 0, 2, 0);
    PledgeMissionCheckAndAddCount(PledgeMission::PMGT_ITEM_VARI_TRY, mineralItem->GetClassId(), 1);
    PledgeMissionCheckAndAddCount(PledgeMission::PMGT_ITEM_VARI_TRY_ANY, 0, 1);
    return true;
}

bool User::ReplyChangeAttributeItem(CItem& sourceItem, const std::int64_t amount, CItem& targetItem) {
    // Current L2Server 0x00b8e734, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    SetPendingUseETCItem(nullptr, PUIT_ATTRIBUTE_CHANGE);
    runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);

    std::int16_t oldAttackType;
    AttributeTypeEnum changedAttribute;
    {
        L2_GUARD(*targetItem.m_lock);
        oldAttackType = targetItem.m_itemBaseAttribute.m_nAttackType;
        changedAttribute = targetItem.m_eChangeAttribute;
        targetItem.m_itemBaseAttribute.m_nAttackType = static_cast<std::int16_t>(changedAttribute);
        targetItem.m_eChangeAttribute = ATTRT_NONE;
    }
    m_inv.SetInventoryChanged(&targetItem, IUT_UPDATE);

    if (amount == 0) {
        if (sourceItem.m_data->m_ItemConsumeType != 1) {
            m_inv.Pop(sourceItem.m_data->index, false);
            m_inv.SetInventoryChanged(&sourceItem, IUT_DELETE);
            sourceItem.Delete();
        }
    } else {
        {
            L2_GUARD(*sourceItem.m_lock);
            if (!sourceItem.SetAmount(amount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "user_item.cpp",
                //     0x24b3,
                //     sourceItem.m_data->m_nCount,
                //     amount)
                L2_LOG_ERROR(
                    Log, u"[item attribute] Source item DB ID %u rejected requested amount %I64d; current amount is %I64d.",
                    sourceItem.GetDBID(), amount, sourceItem.m_data->m_nCount
                );
            }
        }
        m_inv.SetInventoryChanged(&sourceItem, IUT_UPDATE);
    }

    GetSocket()->Send("ch", 0xfe, 0x116);
    GetSocket()->Send(
        "chccdcccc", 0x62, SystemMessageId::ITEM_ATTRIBUTE_CHANGED_FROM_ONE_TO_ANOTHER, 3, 3, targetItem.m_data->m_nClassID,
        9, static_cast<int>(oldAttackType), 9, static_cast<int>(changedAttribute)
    );
    m_data->m_NeedUpdatePacketBitSet.SetBit(0x49);
    SendItemList(true, true, nullptr);
    ValidateChanged(VB_MAX);
    return true;
}

bool User::CheckAndConsumeDuration(const int itemClassId) {
    // Current L2Server 0x00b8efec.
    L2_GUARD_FUNCTION();
    L2_GUARD(*m_lock);

    bool foundDurationItem = false;
    const auto range = m_mmapEquipDurationItem.equal_range(itemClassId);
    for (auto current = range.first; current != range.second; ++current) {
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(current->second);
        if (!item || !item->IsDuration()) {
            continue;
        }

        foundDurationItem = true;
        const int consumedDuration = item->m_const->m_nUseSkillDistime;
        if (consumedDuration <= 0 || item->m_data->m_nDuration < consumedDuration) {
            continue;
        }

        const int duration = item->m_data->m_nDuration - consumedDuration;
        {
            L2_GUARD(*item->m_lock);
            item->SetDuration(duration);
        }
        db.UpdateItemDuration(*this, item->GetDBID(), duration);
        m_inv.SetInventoryChanged(item.get(), IUT_UPDATE);
        SendItemList(true, true, nullptr);
        return true;
    }
    return !foundDurationItem;
}

void User::_InsertPeriodicShapeShiftedItem(PeriodicItemData& itemData) {
    // Current L2Server 0x00b84a34, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    const int expireTime = runtime::AddInt32Wrapping(itemData.startTime, itemData.itemPeriod);
    const auto periodicItem = m_periodicShapeShiftedItemMap.find(itemData.itemDBId);
    if (periodicItem == m_periodicShapeShiftedItemMap.end()) {
        m_periodicShapeShiftedItemMap.emplace(
            itemData.itemDBId, UserPeriodicItemMapDataType{itemData.itemClassId, expireTime}
        );
        return;
    }

    periodicItem->second.itemClassId = itemData.itemClassId;
    periodicItem->second.expireTime = expireTime;
}

void User::_InsertPeriodicItem(PeriodicItemData& itemData) {
    // Current L2Server.exe 0x00b848fc, original owner user_item.cpp. The
    // regular periodic map keeps its first value and reports a
    // duplicate instead of replacing it.
    L2_GUARD_FUNCTION();
    const int expireTime = runtime::AddInt32Wrapping(itemData.startTime, itemData.itemPeriod);
    const auto inserted =
        m_periodicItemMap.emplace(itemData.itemDBId, UserPeriodicItemMapDataType{itemData.itemClassId, expireTime});
    if (!inserted.second) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] duplicated dbid Item[%d] of char[%s]",
        //     L"user_item.cpp",
        //     0x2cab,
        //     itemData.itemDBId,
        //     GetName())
        // L2_LOG_ERROR(Log,
        //     L"[periodic items] Character '%s' has duplicate periodic item DB ID %d. The first record is retained.",
        //     GetName(),
        //     itemData.itemDBId)
        L2_LOG_ERROR(
            Log, u"[periodic items] Character '%s' has duplicate periodic item DB ID %d; the first record is retained.",
            GetName(), itemData.itemDBId
        );
    }
}

void User::OnGetPeriodicItem(CItem* item) {
    // Current L2Server.exe 0x00b85adc, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    PeriodicItemData itemData{runtime::Int32FromBits(item->GetDBID()), runtime::crt::Time32Now(),
        runtime::MultiplyInt32Wrapping(item->m_const->m_period, 60), static_cast<int>(item->Id())};
    {
        L2_GUARD(m_PeriodicItemLock);
        _InsertPeriodicItem(itemData);
    }

    // The current body performs three independent virtual GetPos calls in
    // z/y/x evaluation order before serializing the x/y/z audit lanes.
    const double positionZ = GetPos().z;
    const double positionY = GetPos().y;
    const double positionX = GetPos().x;
    const char16_t* const userName = GetName();
    const std::uint32_t accountId = GetAccountID();
    const std::uint32_t userDatabaseId = GetDBID();
    std::array<wchar_t, 16> accountNameStorage{};
    const wchar_t* const accountName = PendingTeleportAccountName(m_sAccountName, accountNameStorage);

    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%s,%s,,,%I64d",
    //     std::int64_t{0x3e4},
    //     static_cast<std::int64_t>(userDatabaseId),
    //     static_cast<std::int64_t>(accountId),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(positionX)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(positionY)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(positionZ)),
    //     static_cast<std::int64_t>(m_data->m_Race),
    //     static_cast<std::int64_t>(m_data->m_Sex),
    //     static_cast<std::int64_t>(m_data->m_nClass),
    //     static_cast<std::int64_t>(m_data->m_nLevel),
    //     static_cast<std::int64_t>(itemData.itemClassId),
    //     std::int64_t{1},
    //     static_cast<std::int64_t>(itemData.itemPeriod),
    //     userName,
    //     accountName,
    //     static_cast<std::int64_t>(itemData.itemDBId))
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%s,%s,,,%I64d",
        std::int64_t{0x3e4}, static_cast<std::int64_t>(userDatabaseId), static_cast<std::int64_t>(accountId),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionX)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionY)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionZ)), static_cast<std::int64_t>(m_data->m_Race),
        static_cast<std::int64_t>(m_data->m_Sex), static_cast<std::int64_t>(m_data->m_nClass),
        static_cast<std::int64_t>(m_data->m_nLevel), static_cast<std::int64_t>(itemData.itemClassId), std::int64_t{1},
        static_cast<std::int64_t>(itemData.itemPeriod), userName, accountName, static_cast<std::int64_t>(itemData.itemDBId)
    );
}

void User::OnLoadPeriodicItemInfo(const int itemCount, const unsigned char* packet) {
    // Current L2Server 0x00b85d3c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_PeriodicItemLock);
        m_periodicItemMap.clear();
        for (int index = 0; index < itemCount; ++index) {
            PeriodicItemData itemData{};
            packet = ::Disassemble(
                packet, "dddd", &itemData.itemDBId, &itemData.startTime, &itemData.itemPeriod, &itemData.itemClassId
            );
            _InsertPeriodicItem(itemData);
        }
        m_periodicItemManageStart = runtime::AddInt32Wrapping(runtime::crt::Time32Now(), 10);
    }

    std::int32_t expiredItemCount;
    packet = ::Disassemble(packet, "d", &expiredItemCount);
    for (int index = 0; index < expiredItemCount; ++index) {
        std::int32_t itemDatabaseId = 0;
        std::int32_t itemType = 0;
        packet = ::Disassemble(packet, "dd", &itemDatabaseId, &itemType);
        CItem* const item = m_inv.GetByDBID(itemDatabaseId);
        const std::int32_t itemServerId = item != nullptr ? item->m_data->m_nId : 0;
        db.RemoveExpiredPeriodicItem(
            static_cast<unsigned int>(itemDatabaseId), static_cast<unsigned int>(itemServerId), *this, itemType
        );
    }

    runtime::raw_atomic::CompareExchangeI32(&m_PeriodicItemExpired, expiredItemCount == 0 ? 2 : 1, 0);
    db.RequestLoadSkill(this);
}

bool User::_CheckPeriodicItemOnTick(const UserPeriodicItemMapType& periodicItems, const int currentTime) {
    // Current L2Server.exe 0x00b7e160.
    L2_GUARD_FUNCTION();
    bool expired = false;
    constexpr std::array<std::pair<int, int>, 2> warningRanges{{
        {595, 605},
        {295, 305},
    }};

    for (const auto& periodicItem : periodicItems) {
        const int itemDatabaseId = periodicItem.first;
        const UserPeriodicItemMapDataType& data = periodicItem.second;
        if (currentTime < data.expireTime) {
            const int remaining = runtime::SubtractInt32Wrapping(data.expireTime, currentTime);
            for (const auto& range : warningRanges) {
                if (range.first <= remaining && remaining < range.second) {
                    GetSocket()->Send(
                        "chccdcd", 0x62, SystemMessageId::OWNERSHIP_EXPIRES_IN_MINUTES, 2, 3, data.itemClassId, 0x14,
                        (range.first + 5) / 60
                    );
                }
            }
            continue;
        }

        CItem* const item = m_inv.GetByDBID(itemDatabaseId);
        const unsigned int itemServerId = item != nullptr ? item->m_data->m_nId : 0U;
        db.RemoveExpiredPeriodicItem(static_cast<unsigned int>(itemDatabaseId), itemServerId, *this, data.itemClassId);
        expired = true;
    }
    return expired;
}

bool User::_CheckPeriodicShapeShiftedItemOnTick(const UserPeriodicItemMapType& periodicItems, const int currentTime) {
    // Current L2Server.exe 0x00b7e348.
    L2_GUARD_FUNCTION();
    bool expired = false;
    for (const auto& periodicItem : periodicItems) {
        if (periodicItem.second.expireTime <= currentTime) {
            CItem* const item = m_inv.GetByDBID(periodicItem.first);
            if (item != nullptr) {
                db.RequestExpireShapeShiftItem(this, item);
            }
            expired = true;
        }
    }
    return expired;
}

void User::CheckPeriodicItemOnTick() {
    // Current L2Server.exe 0x00b9648c.
    L2_GUARD_FUNCTION();
    std::optional<UserPeriodicItemMapType> periodicItemsStorage;
    int currentTime;
    {
        L2_GUARD(m_PeriodicItemLock);
        currentTime = runtime::crt::Time32Now();
        if (m_periodicItemManageStart < 1 || m_periodicItemManageStart <= currentTime) {
            m_periodicItemManageStart = 0;
            periodicItemsStorage.emplace(m_periodicItemMap);
        }
    }
    if (periodicItemsStorage) {

        if (runtime::raw_atomic::CompareExchangeI32(&m_PeriodicItemExpired, 3, 1) == 1) {
            SendSystemMessage(SystemMessageId::ITEM_DISAPPEARED_AFTER_EXPIRATION);
        }
        _CheckPeriodicItemOnTick(*periodicItemsStorage, currentTime);
        if (m_nTickCount % 900 == 0) {
            SendPeriodicItemList();
        }
        return;
    }
}

void User::SendPeriodicItemList() {
    // Current L2Server.exe 0x00b8f4f8.
    L2_GUARD_FUNCTION();
    std::array<char, 0x4f9c> packet;
    int packetLength = 0;
    int itemCount = 0;

    std::optional<UserPeriodicItemMapType> periodicItemsStorage;
    {
        L2_GUARD(m_PeriodicItemLock);
        periodicItemsStorage.emplace(m_periodicItemMap);
    }
    const auto& periodicItems = *periodicItemsStorage;

    const int currentTime = runtime::crt::Time32Now();
    for (const auto& periodicItem : periodicItems) {
        CItem* const item = m_inv.GetByDBID(periodicItem.first);
        if (item == nullptr) {
            continue;
        }

        ++itemCount;
        int remaining = runtime::SubtractInt32Wrapping(periodicItem.second.expireTime, currentTime);
        if (remaining < 0) {
            remaining = 0;
        }
        packetLength = runtime::AddInt32Wrapping(
            packetLength,
            ::Assemble(packet.data() + packetLength, 0x4f9c - packetLength, "dd", static_cast<int>(item->Id()), remaining)
        );
    }

    if (itemCount > 0) {
        GetSocket()->Send("chdb", 0xfe, 0x88, itemCount, packetLength, packet.data());
    }
}

bool User::OnDestroyItem(CItem* item, const std::int64_t amount, bool& inUserInventory) {
    // Current L2Server 0x00b7ae2c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    inUserInventory = true;

    CItem* found = m_inv.GetByDBID(static_cast<int>(item->GetDBID()));
    CPet* const pet = GetPet();
    if (found == nullptr && pet != nullptr) {
        found = pet->m_inv.GetByDBID(static_cast<int>(item->GetDBID()));
        inUserInventory = false;
    }

    CCreature* const owner = inUserInventory ? static_cast<CCreature*>(this) : static_cast<CCreature*>(pet);
    CInventory* const inventory = inUserInventory ? &m_inv : &pet->m_inv;
    if (found == nullptr) {
        const char16_t* const userName = GetName();
        const int itemDatabaseId = static_cast<int>(item->GetDBID());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] no item[%d] while periodic item destroyed of char[%s].",
        //     L"user_item.cpp",
        //     0x75c,
        //     itemDatabaseId,
        //     userName)
        // L2_LOG_ERROR(Log,
        //     L"[periodic items] Character '%s' no longer owns item DB ID %d while processing its destruction.",
        //     userName,
        //     itemDatabaseId)
        L2_LOG_ERROR(
            Log, u"[periodic items] Character '%s' no longer owns item DB ID %d while processing its destruction.", userName,
            itemDatabaseId
        );
        return false;
    }

    bool removed = false;
    if (amount == 0) {
        if (!inUserInventory) {
            pet->UnequipItem(found, false);
        } else if (UnequipItem(found, true) == ST_NONE) {
            return false;
        }

        L2_GUARD(*owner->m_lock);
        inventory->Pop(found->m_data->index, false);
        inventory->SetInventoryChanged(found, IUT_DELETE);
        removed = true;
    } else {
        L2_GUARD(*owner->m_lock);
        {
            L2_GUARD(*found->m_lock);
            if (!found->SetAmount(amount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "user_item.cpp",
                //     0x752,
                //     found->m_data->m_nCount,
                //     amount)
                L2_LOG_ERROR(
                    Log,
                    u"[periodic items] Item DB ID %u rejected requested remaining amount %I64d; current amount is %I64d.",
                    found->GetDBID(), amount, found->m_data->m_nCount
                );
            }
        }
        inventory->SetInventoryChanged(found, IUT_UPDATE);
    }
    return removed;
}

bool User::DestroyItemReturn(CItem* item, const std::int64_t amount) {
    // Current L2Server 0x00b91d90, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    if (item == nullptr) {
        return false;
    }

    bool inUserInventory = true;
    if (OnDestroyItem(item, amount, inUserInventory)) {
        if (item->m_const->m_period > 0) {
            GetSocket()->Send("chccd", 0x62, SystemMessageId::ITEM_DISAPPEARED_MANA_DEPLETED, 1, 3, item->m_nClassId);
        }
        if (item->m_const->m_period > 0) {
            OnPeriodicItemDestroyed(static_cast<int>(item->GetDBID()), false);
        }
        item->Delete();
    }

    if (!inUserInventory) {
        CPet* const pet = GetPet();
        if (pet != nullptr) {
            pet->SendPetItemList(true);
        }
    } else {
        m_data->m_NeedUpdatePacketBitSet.SetBit(0x49);
        SendItemList(true, true, nullptr);
        ParamChanged(std::int64_t{PCB_USER});
    }
    return true;
}

void User::OnPeriodicItemDestroyed(const int itemDatabaseId, const bool destroyed) {
    // Current L2Server 0x00b7f3dc, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    int itemClassId = 0;
    int periodSeconds = 0;
    int periodLeft = 0;
    {
        L2_GUARD(m_PeriodicItemLock);
        const int now = runtime::crt::Time32Now();
        const auto periodicItem = m_periodicItemMap.find(itemDatabaseId);
        if (periodicItem != m_periodicItemMap.end()) {
            itemClassId = periodicItem->second.itemClassId;
            periodLeft = runtime::SubtractInt32Wrapping(periodicItem->second.expireTime, now);
            m_periodicItemMap.erase(periodicItem);
        }
    }

    CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
    if (object != nullptr && object->IsItem()) {
        periodSeconds = runtime::MultiplyInt32Wrapping(object->CastItem()->m_const->m_period, 60);
    }

    CUserSocket* const socket = GetSocket();
    if (!destroyed) {
        socket->SendSystemMessage(SystemMessageId::TIME_LIMITED_ITEM_DELETED);
    } else {
        socket->Send("chccd", 0x62, SystemMessageId::NAMED_ITEM_DISAPPEARED_AFTER_EXPIRATION, 1, 3, itemClassId);
    }
    if (periodLeft < 0) {
        periodLeft = 0;
    }
    m_inv.UpdateRuneItem();

    const double positionZ = GetPos().z;
    const double positionY = GetPos().y;
    const double positionX = GetPos().x;
    const char16_t* const userName = GetName();
    const std::uint32_t accountId = GetAccountID();
    const std::uint32_t userDatabaseId = GetDBID();
    std::array<wchar_t, 16> accountNameStorage{};
    const wchar_t* const accountName = PendingTeleportAccountName(m_sAccountName, accountNameStorage);

    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
    //     L"%s,%s,,,%I64d",
    //     std::int64_t{0x3e5},
    //     static_cast<std::int64_t>(userDatabaseId),
    //     static_cast<std::int64_t>(accountId),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(positionX)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(positionY)),
    //     static_cast<std::int64_t>(
    //         runtime::Win64CvttDoubleToInt32(positionZ)),
    //     static_cast<std::int64_t>(m_data->m_Race),
    //     static_cast<std::int64_t>(m_data->m_Sex),
    //     static_cast<std::int64_t>(m_data->m_nClass),
    //     static_cast<std::int64_t>(m_data->m_nLevel),
    //     static_cast<std::int64_t>(itemClassId),
    //     std::int64_t{-1},
    //     static_cast<std::int64_t>(periodSeconds),
    //     static_cast<std::int64_t>(periodLeft),
    //     userName,
    //     accountName,
    //     static_cast<std::int64_t>(itemDatabaseId))
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,,,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
        u"%s,%s,,,%I64d",
        std::int64_t{0x3e5}, static_cast<std::int64_t>(userDatabaseId), static_cast<std::int64_t>(accountId),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionX)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionY)),
        static_cast<std::int64_t>(runtime::Win64CvttDoubleToInt32(positionZ)), static_cast<std::int64_t>(m_data->m_Race),
        static_cast<std::int64_t>(m_data->m_Sex), static_cast<std::int64_t>(m_data->m_nClass),
        static_cast<std::int64_t>(m_data->m_nLevel), static_cast<std::int64_t>(itemClassId), std::int64_t{-1},
        static_cast<std::int64_t>(periodSeconds), static_cast<std::int64_t>(periodLeft), userName, accountName,
        static_cast<std::int64_t>(itemDatabaseId)
    );
}

void User::OnPeriodicItemDestroyed(CItem* item, const int itemDatabaseId) {
    // Current L2Server 0x00b8f3dc, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (item != nullptr) {
        bool inUserInventory = true;
        if (OnDestroyItem(item, 0, inUserInventory)) {
            if (!inUserInventory) {
                if (GetPet() != nullptr) {
                    GetPet()->SendPetItemList(true);
                }
            } else {
                m_data->m_NeedUpdatePacketBitSet.SetBit(0x49);
                SendItemList(true, true, nullptr);
            }
            OnPeriodicItemDestroyed(itemDatabaseId, true);
        }
    }
}

void User::OnGetPeriodicShapeShiftedItem(CItem* item, const int itemPeriod) {
    // Current L2Server 0x00b85f38, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    PeriodicItemData itemData{runtime::Int32FromBits(item->GetDBID()), runtime::crt::Time32Now(), itemPeriod, 0};
    L2_GUARD(m_PeriodicShapeShiftedItemLock);
    _InsertPeriodicShapeShiftedItem(itemData);
}

void User::OnLoadPeriodicShapeShiftedItemInfo(const int itemCount, const unsigned char* packet) {
    // Current L2Server 0x00b86040, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(m_PeriodicShapeShiftedItemLock);
        m_periodicShapeShiftedItemMap.clear();

        for (int index = 0; index < itemCount; ++index) {
            PeriodicItemData itemData{};
            packet = ::Disassemble(packet, "ddd", &itemData.itemDBId, &itemData.startTime, &itemData.itemPeriod);
            _InsertPeriodicShapeShiftedItem(itemData);
        }
        m_periodicShapeShiftedItemManageStart = runtime::AddInt32Wrapping(runtime::crt::Time32Now(), 10);
    }

    std::int32_t expiredItemCount = 0;
    packet = ::Disassemble(packet, "d", &expiredItemCount);
    for (int index = 0; index < expiredItemCount; ++index) {
        std::int32_t itemDbId = 0;
        packet = ::Disassemble(packet, "d", &itemDbId);
        CItem* const item = m_inv.GetByDBID(itemDbId);
        if (item != nullptr) {
            db.RequestExpireShapeShiftItem(this, item);
        }
    }
}

void User::OnRestorePeriodicShapeShiftedItem(CItem* item) {
    // Current L2Server 0x00b96634, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return;
    }

    L2_GUARD(m_PeriodicShapeShiftedItemLock);
    const int itemDbId = runtime::Int32FromBits(item->GetDBID());
    const auto periodicItem = m_periodicShapeShiftedItemMap.find(itemDbId);
    if (periodicItem == m_periodicShapeShiftedItemMap.end()) {
        return;
    }

    periodicItem->second.expireTime = runtime::crt::Time32Now();
    _SendPeriodicShapeShiftedItemList();
    m_periodicShapeShiftedItemMap.erase(periodicItem);
}

void User::OnExpiredPeriodicShapeShiftedItem(CItem* item) {
    // Current L2Server 0x00b7fb6c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return;
    }

    L2_GUARD(m_PeriodicShapeShiftedItemLock);
    const int itemDbId = runtime::Int32FromBits(item->GetDBID());
    const auto periodicItem = m_periodicShapeShiftedItemMap.find(itemDbId);
    if (periodicItem != m_periodicShapeShiftedItemMap.end()) {
        m_periodicShapeShiftedItemMap.erase(periodicItem);
    }
}

int User::DestroyItemAll() {
    // Current L2Server 0x00b7fcb4, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    if (!IsBuilder()) {
        return -1;
    }

    int destroyedCount = 0;
    {
        L2_WRITE_GUARD(m_inv);
        for (CSharedItemData* itemData = m_inv.GetFirst(); itemData != nullptr; itemData = itemData->Next()) {
            // Preserve the current temporary-SP lifetime, as in DropItemAll.
            CItem* const item = CItemFactory::Inst().FindObjectSP(itemData->index).get();
            if (item == nullptr) {
                continue;
            }

            if (!item->IsPetCollarItem()) {
                db.RequestDestroyItem(item, itemData->m_nCount, this, 0);
            } else {
                static_cast<void>(CPet::DestroyPet(this, item->CastEtcItem()));
            }
            ++destroyedCount;
        }
    }
    return destroyedCount;
}

void User::CheckPeriodicShapeShiftedItemOnTick() {
    // Current L2Server.exe 0x00b96790.
    L2_GUARD_FUNCTION();
    std::optional<UserPeriodicItemMapType> periodicItemsStorage;
    int currentTime;
    {
        L2_GUARD(m_PeriodicShapeShiftedItemLock);
        currentTime = runtime::crt::Time32Now();
        if (m_periodicShapeShiftedItemManageStart < 1 || m_periodicShapeShiftedItemManageStart <= currentTime) {
            m_periodicShapeShiftedItemManageStart = 0;
            periodicItemsStorage.emplace(m_periodicShapeShiftedItemMap);
        }
    }
    if (periodicItemsStorage) {
        _CheckPeriodicShapeShiftedItemOnTick(*periodicItemsStorage, currentTime);
        if (m_nTickCount % 900 == 0) {
            SendPeriodicShapeShiftedItemList();
        }
        return;
    }
}

void User::SendPeriodicShapeShiftedItemList() {
    // Current L2Server 0x00b8f740, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    std::array<char, 0x4f9c> packet;
    int packetLength = 0;
    int itemCount = 0;

    std::optional<UserPeriodicItemMapType> periodicItemsStorage;
    {
        L2_GUARD(m_PeriodicShapeShiftedItemLock);
        periodicItemsStorage.emplace(m_periodicShapeShiftedItemMap);
    }
    const auto& periodicItems = *periodicItemsStorage;

    const int now = runtime::crt::Time32Now();
    for (const auto& periodicItem : periodicItems) {
        CItem* const item = m_inv.GetByDBID(periodicItem.first);
        if (item == nullptr) {
            continue;
        }

        ++itemCount;
        int remaining = runtime::SubtractInt32Wrapping(periodicItem.second.expireTime, now);
        if (remaining < 0) {
            remaining = 0;
        }
        packetLength = runtime::AddInt32Wrapping(
            packetLength,
            ::Assemble(packet.data() + packetLength, 0x4f9c - packetLength, "dd", static_cast<int>(item->Id()), remaining)
        );
    }

    if (itemCount > 0) {
        GetSocket()->Send("chdb", 0xfe, 0x88, itemCount, packetLength, packet.data());
    }
}

void User::_SendPeriodicShapeShiftedItemList() {
    // Current L2Server 0x00b8f988, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    std::array<char, 0x4f9c> packet;
    int packetLength = 0;
    int itemCount = 0;
    const UserPeriodicItemMapType periodicItems(m_periodicShapeShiftedItemMap);
    const int now = runtime::crt::Time32Now();

    for (const auto& periodicItem : periodicItems) {
        CItem* const item = m_inv.GetByDBID(periodicItem.first);
        if (item == nullptr) {
            continue;
        }

        ++itemCount;
        int remaining = runtime::SubtractInt32Wrapping(periodicItem.second.expireTime, now);
        if (remaining < 0) {
            remaining = 0;
        }
        packetLength = runtime::AddInt32Wrapping(
            packetLength,
            ::Assemble(packet.data() + packetLength, 0x4f9c - packetLength, "dd", static_cast<int>(item->Id()), remaining)
        );
    }

    if (itemCount > 0) {
        GetSocket()->Send("chdb", 0xfe, 0x88, itemCount, packetLength, packet.data());
    }
}

bool User::ReplyShapeShiftItem(
    CItem* sourceItem,
    const std::int64_t sourceAmount,
    CItem& targetItem,
    CItem* extraItem,
    const int consumeExtraItem,
    const int restoreOnly,
    const int shapeShiftingClassId,
    const std::int64_t adenaAmount,
    const int itemPeriod
) {
    // Current L2Server 0x00b907c4, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING);
    SetPendingUseETCItem(nullptr, PUIT_SHAPE_SHIFTING_EXTRACTION);
    runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);

    {
        L2_GUARD(*targetItem.m_lock);
        targetItem.SetShapeShiftingClassId(shapeShiftingClassId);
    }
    m_inv.SetInventoryChanged(&targetItem, IUT_UPDATE);

    if (sourceItem != nullptr) {
        if (sourceAmount == 0) {
            m_inv.Pop(sourceItem->m_data->index, false);
            m_inv.SetInventoryChanged(sourceItem, IUT_DELETE);
            sourceItem->Delete();
        } else {
            {
                L2_GUARD(*sourceItem->m_lock);
                if (!sourceItem->SetAmount(sourceAmount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[ShapeShifting][%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     "user_item.cpp",
                    //     0x3208,
                    //     sourceItem->m_data->m_nCount,
                    //     sourceAmount)
                    L2_LOG_ERROR(
                        Log,
                        u"[appearance] Source item DB ID %u rejected requested remaining amount %I64d; current amount is "
                        u"%I64d.",
                        sourceItem->GetDBID(), sourceAmount, sourceItem->m_data->m_nCount
                    );
                }
            }
            m_inv.SetInventoryChanged(sourceItem, IUT_UPDATE);
        }
    }

    if (consumeExtraItem == 0 && extraItem != nullptr) {
        for (int slot = 0; slot < ST_MAX; ++slot) {
            if (m_data->m_nSlotIndex[slot] == extraItem->m_data->index) {
                UnequipItem(static_cast<SlotType>(slot), true);
                break;
            }
        }
        m_inv.Pop(extraItem->m_data->index, false);
        m_inv.SetInventoryChanged(extraItem, IUT_DELETE);
        extraItem->Delete();
    }

    if (restoreOnly == 0) {
        CItem* const adena = m_inv.GetFirstItemByClassID(57, false, false);
        if (adena != nullptr) {
            if (adenaAmount == 0) {
                m_inv.Pop(adena->m_data->index, false);
                m_inv.SetInventoryChanged(adena, IUT_DELETE);
                adena->Delete();
            } else {
                {
                    L2_GUARD(*adena->m_lock);
                    if (!adena->SetAmount(adenaAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[ShapeShifting][%S][%d] adena amount is invalid[%I64d][%I64d]",
                        //     "user_item.cpp",
                        //     0x3245,
                        //     adena->m_data->m_nCount,
                        //     adenaAmount)
                        L2_LOG_ERROR(
                            Log,
                            u"[appearance] Adena item DB ID %u rejected requested remaining amount %I64d; current amount is "
                            u"%I64d.",
                            adena->GetDBID(), adenaAmount, adena->m_data->m_nCount
                        );
                    }
                }
                m_inv.SetInventoryChanged(adena, IUT_UPDATE);
            }
        }
    }

    for (int slot = 0; slot < ST_MAX; ++slot) {
        if (m_data->m_nSlotIndex[slot] == targetItem.m_data->index) {
            if (!CanEquip(&targetItem)) {
                UnequipItem(static_cast<SlotType>(slot), true);
            }
            break;
        }
    }

    ValidateSetItem(ST_MAX);
    ValidateNewSetItem();
    ValidateShapeShiftedSetItem();
    ValidateChanged(VB_MAX);
    GetSocket()->Send(
        "chdddd", 0xfe, 300,
        // 1,
        ShapeShiftingResult::SSR_SUCCESS, targetItem.GetClassId(), shapeShiftingClassId, itemPeriod
    );
    m_data->SetNeedUpdatePacket(0x49);
    SendItemList(true, true, nullptr);
    ParamChanged(std::int64_t{PCB_USER | PCB_CHAR_SHAPESHIFT});
    return true;
}

bool User::HandlePremiumItemManipulation(const std::int64_t warehouseNo, CItem* const item, const std::int64_t amount) {
    // Current L2Server 0x00b987d8, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    {
        L2_READ_GUARD(m_inv);
        if (!m_inv.IsValidManipulation(item, amount)) {
            runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
            if (g_GoodsInventorySetting.enable) {
                GetSocket()->Send(
                    "chd", 0xfe, 0x10e,
                    // -1)
                    GIR_INVLAID_REQ
                );
            }
            return false;
        }

        {
            InventoryParams parameters;
            parameters.m_Reason = ItemGetReason::Etc;
            parameters.nEnchanted = item->m_data->GetEnchanted();
            parameters.nBless = item->m_data->m_bBlessed;
            parameters.nEroded = item->m_data->m_nDamaged;
            parameters.nCount = amount;
            parameters.key = item->m_itemOption.itemOptionKey;
            parameters.nIntensiveItemClassID = item->m_itemOption.intensiveItemClassID;
            parameters.attribute = item->m_itemBaseAttribute;
            parameters.hLocked = item->m_data->GetLocked();
            parameters.hUseCount = item->m_data->GetUseCount();
            db.CreateUpdatePremiumItems(this, warehouseNo, item, parameters);
        }
    }
    return true;
}

bool User::ReplyCancelEquipmentAddedEffectAndDropItem(
    FVector* position,
    CItem* beforeItem,
    CItem* afterItem,
    const int beforeOwnerId,
    const int afterOwnerId,
    const std::int64_t beforeAmount,
    const std::int64_t afterAmount,
    const int effectMask,
    const bool setContributeData
) {
    // Current L2Server 0x00b98a04, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    static_cast<void>(afterOwnerId);

    if (beforeItem == nullptr || afterItem == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CItem* pBeforeItem(%x) || CItem* pAfterItem(%x)] is nullptr",
        //     L"user_item.cpp",
        //     0x10a4,
        //     beforeItem,
        //     afterItem)
        L2_LOG_ERROR(
            Log,
            u"[item drop] Database reply omitted the source or destination item; equipment-effect cancellation and drop "
            u"aborted."
        );
        return false;
    }

    if (beforeAmount == 0 && afterAmount == 0) {
        const unsigned int firstItemDbId = beforeItem->GetDBID();
        const unsigned int secondItemDbId = beforeItem->GetDBID();
        const char16_t* const userName = GetName();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]drop item's count is 0. [%s][%d][%d]",
        //     "user_item.cpp",
        //     0x10ac,
        //     userName,
        //     secondItemDbId,
        //     firstItemDbId)
        // L2_LOG_ERROR(Log,
        //     L"[item drop] Character '%s' received zero source and destination amounts for item DB IDs %u and %u. Item is
        //     removed and abnormal action recorded.", userName, secondItemDbId, firstItemDbId)
        L2_LOG_ERROR(
            Log,
            u"[item drop] Character '%s' received zero source and destination amounts for item DB IDs %u and %u; item is "
            u"removed and abnormal action recorded.",
            userName, secondItemDbId, firstItemDbId
        );
        {
            L2_WRITE_GUARD(m_inv);
            m_inv.Pop(beforeItem->m_data->index, false);
            m_inv.SetInventoryChanged(beforeItem, IUT_DELETE);
        }
        m_data->SetNeedUpdatePacket(0x49);
        SendItemList(true, true, nullptr);
        LogAbnormalAction(AUA_TRY_TO_DROP_ZERO_ITEM, true, beforeItem->m_nClassId, 0, 0, 0);
        runtime::raw_atomic::ExchangeI32(&m_nWaitGetItemOnce, 0);
        return false;
    }

    ItemOptionKey oldOptionKey{};
    const bool cancelVariation = (effectMask & 1) != 0;
    if (cancelVariation) {
        {
            L2_GUARD(*beforeItem->m_lock);
            oldOptionKey = beforeItem->m_itemOption.itemOptionKey;
            beforeItem->m_itemOption = g_VariationDB.GetNewBlankItemOption();
            beforeItem->m_data->m_bIsVariation = false;
        }

        {
            L2_GUARD(*afterItem->m_lock);
            afterItem->m_itemOption = g_VariationDB.GetNewBlankItemOption();
            afterItem->m_data->m_bIsVariation = false;
        }
    }

    if ((effectMask & 2) != 0) {
        OnRestorePeriodicShapeShiftedItem(beforeItem);
        {
            L2_GUARD(*beforeItem->m_lock);
            beforeItem->SetShapeShiftingClassId(0);
        }
        {
            L2_GUARD(*afterItem->m_lock);
            afterItem->SetShapeShiftingClassId(0);
        }
    }

    m_inv.SetInventoryChanged(beforeItem, IUT_UPDATE);
    if (beforeOwnerId == 0) {
        UnequipItem(beforeItem, false);
        const bool validAmount = [&] {
            L2_WRITE_GUARD(m_inv);
            if (beforeAmount > beforeItem->m_data->m_nCount) {
                return false;
            }

            m_inv.Pop(beforeItem->m_data->index, false);
            m_inv.SetInventoryChanged(beforeItem, IUT_DELETE);
            return true;
        }();
        if (!validAmount) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Item count is invalid dbid[%d] server count[%I64d] change[%I64d] drop [%I64d]",
            //     "user_item.cpp",
            //     0x10f0,
            //     beforeItem->GetDBID(),
            //     beforeItem->m_data->m_nCount,
            //     beforeAmount,
            //     afterAmount)
            L2_LOG_ERROR(
                Log,
                u"[item drop] Item DB ID %u has %I64d units, below source amount %I64d for requested drop amount %I64d; "
                u"client connection is being closed.",
                beforeItem->GetDBID(), beforeItem->m_data->m_nCount, beforeAmount, afterAmount
            );
            StopMove();
            GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
            return false;
        }

        {
            L2_GUARD(*beforeItem->m_lock);
            if (!beforeItem->SetAmount(afterAmount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "user_item.cpp",
                //     0x1101,
                //     beforeItem->m_data->m_nCount,
                //     afterAmount)
                L2_LOG_ERROR(
                    Log, u"[item drop] Item DB ID %u rejected requested amount %I64d; current amount is %I64d.",
                    beforeItem->GetDBID(), afterAmount, beforeItem->m_data->m_nCount
                );
            }
        }

        if (setContributeData) {
            if (CCursedWeaponMgr::inst().FindConst(beforeItem->m_data->m_nClassID) == nullptr) {
                CContributeDataForUserDie* const contribution = new CContributeDataForUserDie(this);
                contribution->AddTimer(20000, 0);
                L2_GUARD(*beforeItem->m_lock);
                beforeItem->m_ContributeDataID = contribution;
                beforeItem->m_nDroppedTick = runtime::GetTickCount();
            } else {
                L2_GUARD(*beforeItem->m_lock);
                beforeItem->m_ContributeDataID = static_cast<CObject*>(nullptr);
                beforeItem->m_bDroppedByAI = false;
            }
        }

        static_cast<void>(CWorld::Inst().PutItem(beforeItem, *position, this));
        beforeItem->m_nTraceCharId = beforeItem->GetDBID();
    } else {
        CItem* droppedItem;
        const bool validAmount = [&] {
            L2_WRITE_GUARD(m_inv);
            if (beforeAmount > beforeItem->m_data->m_nCount) {
                return false;
            }

            {
                L2_GUARD(*beforeItem->m_lock);
                const std::int64_t remainingDelta = runtime::NegateInt64Wrapping(beforeAmount);
                if (!beforeItem->AddAmount(remainingDelta)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     "user_item.cpp",
                    //     0x113d,
                    //     beforeItem->m_data->m_nCount,
                    //     remainingDelta)
                    L2_LOG_ERROR(
                        Log, u"[item drop] Source item DB ID %u rejected amount change %I64d; current amount is %I64d.",
                        beforeItem->GetDBID(), remainingDelta, beforeItem->m_data->m_nCount
                    );
                }
            }

            droppedItem = static_cast<CItem*>(beforeItem->Copy());
            if (!droppedItem->SetAmount(afterAmount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "user_item.cpp",
                //     0x1145,
                //     droppedItem->m_data->m_nCount,
                //     afterAmount)
                L2_LOG_ERROR(
                    Log, u"[item drop] New dropped item for source DB ID %u rejected amount %I64d; current amount is %I64d.",
                    beforeItem->GetDBID(), afterAmount, droppedItem->m_data->m_nCount
                );
            }
            droppedItem->SetDBID(afterItem->GetDBID());
            droppedItem->m_nTraceCharId = GetDBID();
            droppedItem->m_data->m_nInventorySlotIndex = -1;
            m_inv.SetInventoryChanged(beforeItem, IUT_UPDATE);
            return true;
        }();
        if (!validAmount) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Item count is invalid dbid[%d] server count[%I64d] change[%I64d] drop [%I64d]",
            //     "user_item.cpp",
            //     0x1133,
            //     beforeItem->GetDBID(),
            //     beforeItem->m_data->m_nCount,
            //     beforeAmount,
            //     afterAmount)
            L2_LOG_ERROR(
                Log,
                u"[item drop] Item DB ID %u has %I64d units, below source amount %I64d for requested drop amount %I64d; "
                u"client connection is being closed.",
                beforeItem->GetDBID(), beforeItem->m_data->m_nCount, beforeAmount, afterAmount
            );
            StopMove();
            GetSocket()->ForceClose(SCR_ILLEGAL_BEHAVIOR);
            return false;
        }
        static_cast<void>(CWorld::Inst().PutItem(droppedItem, *position, this));
    }

    if (IsNowTrade()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]item drop & trade try.",
        //     "user_item.cpp",
        //     0x1157)
        L2_LOG_ERROR(
            Log, u"[item drop] Character %u attempted to drop an item during a trade; the trade is being cancelled.",
            GetDBID()
        );
        TradeCancel(false);
    }
    ValidateChanged(VB_MAX);
    ParamChanged(std::int64_t{PCB_USER});
    runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);
    SendItemList(true, true, nullptr);
    runtime::raw_atomic::ExchangeI32(&m_nWaitGetItemOnce, 0);

    const int enchanted = beforeItem->m_data->GetEnchanted();
    if (cancelVariation) {
        if (enchanted < 1) {
            GetSocket()->Send(
                "chccd", 0x62, SystemMessageId::ITEM_AUGMENTATION_EFFECT_REMOVED, 1, 3, beforeItem->m_nClassId
            );
        } else {
            GetSocket()->Send(
                "chccccd", 0x62, SystemMessageId::ENCHANTED_ITEM_AUGMENTATION_EFFECT_REMOVED, 2, 0x13,
                static_cast<char>(enchanted), 3, beforeItem->m_nClassId
            );
        }
    }
    if ((effectMask & 2) != 0) {
        if (enchanted < 1) {
            GetSocket()->Send("chccd", 0x62, SystemMessageId::TEMPORARY_ITEM_APPEARANCE_RESET, 1, 3, beforeItem->m_nClassId);
        } else {
            GetSocket()->Send(
                "chccccd", 0x62, SystemMessageId::TEMPORARY_ENCHANTED_ITEM_APPEARANCE_RESET, 2, 0x13,
                static_cast<char>(enchanted), 3, beforeItem->m_nClassId
            );
        }
    }
    if (enchanted < 1) {
        GetSocket()->Send("chccd", 0x62, SystemMessageId::DROPPED_ITEM_AFTER_DATABASE_REPLY, 1, 3, beforeItem->m_nClassId);
    } else {
        GetSocket()->Send(
            "chccccd", 0x62, SystemMessageId::DROPPED_ENCHANTED_ITEM_AFTER_DATABASE_REPLY, 2, 0x13,
            static_cast<char>(enchanted), 3, beforeItem->m_nClassId
        );
    }

    std::array<char16_t, 256> afterShapeWide{};
    std::array<char16_t, 256> afterAttributeWide{};
    std::array<char16_t, 256> afterEnsoulWide{};
    afterItem->MakeShapeShiftLogInfo(afterShapeWide.data());
    afterItem->m_itemBaseAttribute.GetLogString(afterAttributeWide.data());
    afterItem->MakeEnsoulLogInfo(afterEnsoulWide.data());

    std::array<char16_t, 256> afterShape{};
    std::array<char16_t, 256> afterAttribute{};
    std::array<char16_t, 256> afterEnsoul{};
    UserItemHostWideToTargetUtf16(afterShapeWide.data(), afterShape);
    UserItemHostWideToTargetUtf16(afterAttributeWide.data(), afterAttribute);
    UserItemHostWideToTargetUtf16(afterEnsoulWide.data(), afterEnsoul);

    const int firstLogZ = runtime::Win64CvttDoubleToInt32(GetPos().z);
    const int firstLogY = runtime::Win64CvttDoubleToInt32(GetPos().y);
    const int firstLogX = runtime::Win64CvttDoubleToInt32(GetPos().x);
    const int afterItemDbId = afterItem->m_data->m_nDBID;
    std::u16string firstUserNameStorage;
    const char16_t* const firstUserName = (firstUserNameStorage = GetName()).c_str();
    const int afterItemType = static_cast<int>(afterItem->m_data->m_Type);
    const int race = static_cast<int>(m_data->m_Race);
    const int sex = static_cast<int>(m_data->m_Sex);
    const int characterClass = static_cast<int>(m_data->m_nClass);
    const int level = m_data->m_nLevel;
    const std::uint32_t accountId = GetAccountID();
    const unsigned int userDbId = GetDBID();

    CLogFormatter formatter;
    formatter.WriteLog(
        0x38c, userDbId, accountId, 0, 0, firstLogX, firstLogY, firstLogZ, afterShape.data(), afterAttribute.data(),
        afterEnsoul.data(), race, sex, characterClass, level, 0, 0, afterItem->m_data->GetEnchanted(), afterItemType,
        beforeAmount, afterAmount, firstUserName, m_sAccountName.data(), nullptr, nullptr, afterItemDbId
    );

    if (cancelVariation) {
        std::array<char16_t, 256> secondAttributeWide{};
        std::array<char16_t, 256> secondEnsoulWide{};
        afterItem->m_itemBaseAttribute.GetLogString(secondAttributeWide.data());
        afterItem->MakeEnsoulLogInfo(secondEnsoulWide.data());

        std::array<char16_t, 256> secondAttribute{};
        std::array<char16_t, 256> secondEnsoul{};
        UserItemHostWideToTargetUtf16(secondAttributeWide.data(), secondAttribute);
        UserItemHostWideToTargetUtf16(secondEnsoulWide.data(), secondEnsoul);

        const int secondLogZ = runtime::Win64CvttDoubleToInt32(GetPos().z);
        const int secondLogY = runtime::Win64CvttDoubleToInt32(GetPos().y);
        const int secondLogX = runtime::Win64CvttDoubleToInt32(GetPos().x);
        const unsigned int beforeItemDbId = beforeItem->GetDBID();
        std::u16string secondUserNameStorage;
        const char16_t* const secondUserName = (secondUserNameStorage = GetName()).c_str();
        const int intensiveItemClassId = beforeItem->m_itemOption.intensiveItemClassID;
        const int beforeItemClassId = beforeItem->m_nClassId;
        const std::uint32_t secondAccountId = GetAccountID();
        const unsigned int secondUserDbId = GetDBID();

        formatter.WriteLog(
            0x3d3, secondUserDbId, secondAccountId, 0, 0, secondLogX, secondLogY, secondLogZ, secondAttribute.data(),
            afterShape.data(), secondEnsoul.data(), beforeItemClassId, beforeItem->m_data->GetEnchanted(),
            oldOptionKey.variationOption1, oldOptionKey.variationOption2, -1, intensiveItemClassId, 0, 0, 0, 0,
            secondUserName, m_sAccountName.data(), nullptr, nullptr, beforeItemDbId
        );
    }
    return true;
}

void User::WithdrawPremiumItem(const std::int64_t warehouseNo, const std::int64_t amount) {
    // Current L2Server 0x00b9990c, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    if (g_PremiumSetting.pause) {
        if (!g_GoodsInventorySetting.enable) {
            SendSystemMessage(SystemMessageId::DIMENSIONAL_ITEM_RETRIEVAL_TEMPORARILY_UNAVAILABLE);
        } else {
            GetSocket()->Send(
                "chd", 0xfe, 0x10e,
                // -2)
                GIR_ETC_SYSTEM_ERROR
            );
        }
        return;
    }
    if (IsNowTrade()) {
        if (!g_GoodsInventorySetting.enable) {
            SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_DIMENSIONAL_ITEM_DURING_TRADE);
        } else {
            GetSocket()->Send(
                "chd", 0xfe, 0x10e,
                // -5)
                GIR_ON_TRADING_ACTION
            );
        }
        return;
    }
    if (GetPrivateStoreType() != 0) {
        GetSocket()->Send(
            "chd", 0xfe, 0x10e,
            // -5)
            GIR_ON_TRADING_ACTION
        );
        return;
    }
    if (runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 1) == 1) {
        if (g_GoodsInventorySetting.enable) {
            GetSocket()->Send(
                "chd", 0xfe, 0x10e,
                // -2)
                GIR_ETC_SYSTEM_ERROR
            );
        }
        return;
    }

    int itemClassId = 0;
    std::int64_t storedAmount = 0;
    {
        L2_GUARD(m_PremiumItemLock);
        const auto found = m_mapPremiumItem.find(warehouseNo);
        if (found == m_mapPremiumItem.end()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][Premium] cannot find nWarehouseNo[%d]. User[%s:%d]",
            //     L"user_item.cpp",
            //     0x2ae9,
            //     warehouseNo,
            //     GetName(),
            //     GetDBID())
            // L2_LOG_ERROR(Log,
            //     L"[premium items] Warehouse record %I64d is missing for character '%s' (DB ID %u). Retrieval rejected.",
            //     warehouseNo,
            //     GetName(),
            //     GetDBID())
            L2_LOG_ERROR(
                Log, u"[premium items] Warehouse record %I64d is missing for character '%s' (DB ID %u); retrieval rejected.",
                warehouseNo, GetName(), GetDBID()
            );
            runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
            if (g_GoodsInventorySetting.enable) {
                GetSocket()->Send(
                    "chd", 0xfe, 0x10e,
                    // -2)
                    GIR_ETC_SYSTEM_ERROR
                );
            }
            return;
        }
        itemClassId = found->second->m_nItemClassId;
        storedAmount = found->second->m_nItemAmount;
    }

    CItem* const item = static_cast<CItem*>(g_ObjectDB.GetObjectW(itemClassId, false));
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Premium] cannot find item[%d]. User[%s:%d]",
        //     L"user_item.cpp",
        //     0x2af9,
        //     itemClassId,
        //     GetName(),
        //     GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[premium items] Warehouse record resolves to missing item class %d for character '%s' (DB ID %u). Retrieval
        //     rejected.", itemClassId, GetName(), GetDBID())
        L2_LOG_ERROR(
            Log,
            u"[premium items] Warehouse record resolves to missing item class %d for character '%s' (DB ID %u); retrieval "
            u"rejected.",
            itemClassId, GetName(), GetDBID()
        );
        runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
        if (g_GoodsInventorySetting.enable) {
            GetSocket()->Send(
                "chd", 0xfe, 0x10e,
                // -1)
                GIR_INVLAID_REQ
            );
        }
        return;
    }
    if (!item->m_data->IsStackable() && storedAmount > 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Premium] item is not stackable and amount is not 1pcs. item[%d] user[%s:%d]",
        //     L"user_item.cpp",
        //     0x2b04,
        //     itemClassId,
        //     GetName(),
        //     GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[premium items] Non-stackable item class %d has stored amount %I64d for character '%s' (DB ID %u). Retrieval
        //     rejected.", itemClassId, storedAmount, GetName(), GetDBID())
        L2_LOG_ERROR(
            Log,
            u"[premium items] Non-stackable item class %d has stored amount %I64d for character '%s' (DB ID %u); retrieval "
            u"rejected.",
            itemClassId, storedAmount, GetName(), GetDBID()
        );
        runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
        if (g_GoodsInventorySetting.enable) {
            GetSocket()->Send(
                "chd", 0xfe, 0x10e,
                // -1)
                GIR_INVLAID_REQ
            );
        }
        return;
    }
    if (item->m_const->m_period > 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Premium] item is periodic. item[%d] user[%s:%d]",
        //     L"user_item.cpp",
        //     0x2b0e,
        //     itemClassId,
        //     GetName(),
        //     GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[premium items] Periodic item class %d cannot be retrieved by character '%s' (DB ID %u). Request rejected.",
        //     itemClassId,
        //     GetName(),
        //     GetDBID())
        L2_LOG_ERROR(
            Log,
            u"[premium items] Periodic item class %d cannot be retrieved by character '%s' (DB ID %u); request rejected.",
            itemClassId, GetName(), GetDBID()
        );
        runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
        if (g_GoodsInventorySetting.enable) {
            GetSocket()->Send(
                "chd", 0xfe, 0x10e,
                // -1)
                GIR_INVLAID_REQ
            );
        }
        return;
    }
    if (storedAmount < amount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Premium] item amount is invalid. item[%d] amount[%I64d<%I64d] user[%s:%d]",
        //     L"user_item.cpp",
        //     0x2b1a,
        //     itemClassId,
        //     storedAmount,
        //     amount,
        //     GetName(),
        //     GetDBID())
        // L2_LOG_ERROR(Log,
        //     L"[premium items] Character '%s' (DB ID %u) requested %I64d of item class %d, but warehouse record contains
        //     %I64d, retrieval rejected.", GetName(), GetDBID(), amount, itemClassId, storedAmount)
        L2_LOG_ERROR(
            Log,
            u"[premium items] Character '%s' (DB ID %u) requested %I64d of item class %d, but warehouse record contains "
            u"%I64d; retrieval rejected.",
            GetName(), GetDBID(), amount, itemClassId, storedAmount
        );
        runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
        if (g_GoodsInventorySetting.enable) {
            GetSocket()->Send(
                "chd", 0xfe, 0x10e,
                // -1)
                GIR_INVLAID_REQ
            );
        }
        return;
    }
    if (!item->m_const->m_bIsPremium) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][Premium] item is not premium item. item[%d]",
        //     L"user_item.cpp",
        //     0x2b25,
        //     itemClassId)
        L2_LOG_WARN(
            Log,
            u"[premium items] Item class %d is not marked as premium; retrieval continues with the existing item rules.",
            itemClassId
        );
    }

    std::int64_t itemSlotCount = amount;
    if (item->m_data->IsStackable()) {
        itemSlotCount = m_inv.GetItemAmount(itemClassId, 0, true) < 1;
    }

    if (!item->IsArtifact()) {
        const std::int64_t resultingCount =
            runtime::AddInt64Wrapping(static_cast<std::int64_t>(m_inv.GetTotalCount()), itemSlotCount);
        if (static_cast<float>(resultingCount) > static_cast<float>(GetInventoryLimit()) * 0.8F) {
            runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
            if (!g_GoodsInventorySetting.enable) {
                SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_DIMENSIONAL_ITEM_INVENTORY_WEIGHT_OR_QUANTITY_EXCEEDED);
                SendSystemMessage(SystemMessageId::NOT_ENOUGH_INVENTORY_SPACE);
            } else {
                GetSocket()->Send(
                    "chd", 0xfe, 0x10e,
                    // -3)
                    GIR_INVENTORY_LIMIT_OVER
                );
            }
            return;
        }
    } else {
        const std::int64_t resultingCount =
            runtime::AddInt64Wrapping(static_cast<std::int64_t>(m_data->m_nCurrentArtifactInventoryNum), itemSlotCount);
        if (static_cast<float>(GetArtifactInventoryLimit()) * 0.8F < static_cast<float>(resultingCount)) {
            runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
            if (!g_GoodsInventorySetting.enable) {
                SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_DIMENSIONAL_ITEM_INVENTORY_WEIGHT_OR_QUANTITY_EXCEEDED);
                SendSystemMessage(SystemMessageId::NOT_ENOUGH_ARTIFACT_INVENTORY_SPACE);
            } else {
                GetSocket()->Send(
                    "chd", 0xfe, 0x10e,
                    // -8)
                    GIR_ARTIFACT_INVENTORY_LIMIT_OVER
                );
            }
            return;
        }
    }

    const std::int64_t addedWeight =
        runtime::MultiplyInt64Wrapping(static_cast<std::int64_t>(item->m_const->m_nWeight), amount);
    const std::int64_t resultingWeight =
        runtime::AddInt64Wrapping(static_cast<std::int64_t>(m_inv.GetCarryingWeight()), addedWeight);
    if (static_cast<double>(resultingWeight) <= CarryWeight() * 0.800000011920929) {
        HandlePremiumItemManipulation(warehouseNo, item, amount);
        return;
    }

    runtime::raw_atomic::ExchangeI32(&m_lWaitItemJob, 0);
    if (!g_GoodsInventorySetting.enable) {
        SendSystemMessage(SystemMessageId::CANNOT_RECEIVE_DIMENSIONAL_ITEM_INVENTORY_WEIGHT_OR_QUANTITY_EXCEEDED);
        SendSystemMessage(SystemMessageId::NOT_ENOUGH_INVENTORY_SPACE);
    } else {
        GetSocket()->Send(
            "chd", 0xfe, 0x10e,
            // -3)
            GIR_INVENTORY_LIMIT_OVER
        );
    }
}

void User::SendItemList(bool changedItemsOnly, bool inventoryBlock, CUserSocket* destination) {
    L2_GUARD_FUNCTION();

    CPackageItemPacketManager itemPackets;
    CPackageItemPacketManager questItemPackets;

    {
        GuardHelper makePacketCallStack(L"SendItemList_MakePacket", CThreadManager::GetThreadID());
        m_inv.ReadLock(L"user_item.cpp", 0x93f);

        if (!changedItemsOnly) {
            int inventoryDepth = 0;
            int questItemDepth = 0;
            for (CSharedItemData* itemData = m_inv.GetFirst(); itemData != nullptr;) {
                if (itemData->m_Type == IT_QUESTITEM) {
                    ++questItemDepth;
                } else {
                    ++inventoryDepth;
                }

                if (inventoryDepth > 0x12e) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"depth > %d at file[%s], line[%d], "
                    //     L"UserDBID[%d] / InvenNum[%d] / "
                    //     L"ArtifactInvenNum[%d]",
                    //     0x192,
                    //     L"user_item.cpp",
                    //     0x98d,
                    //     static_cast<int>(GetDBID()),
                    //     m_nMaxInventoryLimit,
                    //     m_nMaxArtifactInventoryLimit)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Inventory packet traversal exceeded %d ordinary items for character %u; list "
                        u"truncated (inventory limit %d, artifact limit %d).",
                        0x12e, GetDBID(), m_nMaxInventoryLimit, m_nMaxArtifactInventoryLimit
                    );
                    break;
                }
                if (questItemDepth > 100) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"quest item depth > %d at file[%s], line[%d]",
                    //     100,
                    //     L"user_item.cpp",
                    //     0x993)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Quest-item packet depth exceeds %d "
                        u"while listing character %u; list truncated.",
                        100, GetDBID()
                    );
                    break;
                }

                CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemData->index);
                if (!item) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] FindObject Failed",
                    //     L"user_item.cpp",
                    //     0x99a)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Runtime item index %d cannot be resolved while listing character %u; entry skipped.",
                        itemData->index, GetDBID()
                    );
                    itemData = itemData->Next();
                    continue;
                }

                std::optional<CItemAssembler> assemblerStorage;
                CPackageItemPacketManager* selectedPackets;
                const int assembled = [&] {
                    L2_GUARD(*item->m_lock);
                    item->m_Useable = CheckUseable(item.get());

                    SlotType candidateSlot = ST_UNDERWEAR;
                    while (candidateSlot < ST_MAX && (ST2SBT[candidateSlot] & item->m_data->m_Slot) == 0) {
                        candidateSlot = static_cast<SlotType>(static_cast<int>(candidateSlot) + 1);
                    }
                    const SlotType equippedSlot = IsEquiped(item->m_data->index, candidateSlot);
                    auto& assembler = assemblerStorage.emplace(*this, *item.get(), equippedSlot != ST_MAX);

                    CPackageItemPacketManager& packets =
                        item->m_data->m_Type == IT_QUESTITEM ? questItemPackets : itemPackets;
                    selectedPackets = &packets;
                    packets.CheckLimitPackageItemPacketDataSize();
                    const int assembled = packets.PackageAssembleItem(&assembler);
                    if (assembled > 0) {
                        packets.SetAssembleItemLengthPerItem(assembled);
                        packets.IncreasePackageItemCount();
                    }
                    return assembled;
                }();
                auto& packets = *selectedPackets;
                if (assembled < 1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Assembe Error at file[%s], line[%d], "
                    //     L"assemble result = [%d], total length = [%d]",
                    //     L"user_item.cpp",
                    //     item->m_data->m_Type == IT_QUESTITEM
                    //         ? 0x9b1
                    //         : 0x9c6,
                    //     assembled,
                    //     packets.GetTotalPackageItemLength())
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Failed to encode item DB ID %u for character %u: result %d, packet length %d; entry "
                        u"skipped.",
                        item->GetDBID(), GetDBID(), assembled, packets.GetTotalPackageItemLength()
                    );
                    itemData = itemData->Next();
                    continue;
                }
                itemData = itemData->Next();
            }
        } else {
            std::optional<std::map<int, int>> changedItemsStorage;
            {
                L2_WRITE_GUARD(m_inv.lockMapChangedItems);
                changedItemsStorage.emplace(m_inv.MapChangedItems);
                if (destination == nullptr) {
                    m_inv._ReleaseMapChangedItems();
                }
            }
            const auto& changedItems = *changedItemsStorage;

            int depth = 0;
            for (const auto& changedItem : changedItems) {
                ++depth;
                if (depth > 0x192) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"depth > %d at file[%s], line[%d], "
                    //     L"UserDBID[%d] / InvenNum[%d]",
                    //     0x192,
                    //     L"user_item.cpp",
                    //     0x953,
                    //     static_cast<int>(GetDBID()),
                    //     m_nMaxInventoryLimit)
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Changed-item packet traversal exceeded %d entries for character %u; list truncated "
                        u"(inventory limit %d).",
                        0x192, GetDBID(), m_nMaxInventoryLimit
                    );
                    break;
                }

                CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(changedItem.first);
                if (!item) {
                    continue;
                }

                std::optional<CItemAssembler> assemblerStorage;
                const int assembled = [&] {
                    L2_GUARD(*item->m_lock);
                    item->m_Useable = CheckUseable(item.get());

                    SlotType candidateSlot = ST_UNDERWEAR;
                    while (candidateSlot < ST_MAX && (ST2SBT[candidateSlot] & item->m_data->m_Slot) == 0) {
                        candidateSlot = static_cast<SlotType>(static_cast<int>(candidateSlot) + 1);
                    }
                    const SlotType equippedSlot = IsEquiped(item->m_data->index, candidateSlot);

                    itemPackets.CheckLimitPackageItemPacketDataSize();
                    int assembled =
                        itemPackets.PackageAssemble("h", static_cast<int>(static_cast<std::int16_t>(changedItem.second)));
                    auto& assembler = assemblerStorage.emplace(*this, *item.get(), equippedSlot != ST_MAX);
                    assembled += itemPackets.PackageAssembleItem(&assembler);
                    if (assembled > 0) {
                        itemPackets.SetAssembleItemLengthPerItem(assembled);
                        itemPackets.IncreasePackageItemCount();
                    }
                    return assembled;
                }();
                if (assembled < 1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Assembe Error at file[%s], line[%d], "
                    //     L"assemble result = [%d], total length = [%d]",
                    //     L"user_item.cpp",
                    //     0x96f,
                    //     assembled,
                    //     itemPackets.GetTotalPackageItemLength())
                    L2_LOG_ERROR(
                        Log,
                        u"[inventory] Failed to encode changed item DB ID %u for character %u: result %d, packet length %d; "
                        u"update aborted.",
                        item->GetDBID(), GetDBID(), assembled, itemPackets.GetTotalPackageItemLength()
                    );
                    return;
                }
            }
        }

        m_inv.ReadUnlock();
    }

    if (destination == nullptr) {
        CUserSocket* const socket = GetSocket();
        if (!changedItemsOnly) {
            CPacketHelperEx<20480> itemPrefix;
            CPacketHelperEx<20480> questItemPrefix;
            CTransform* const transform = IsTransformed(TransformType::Max);
            if (transform == nullptr) {
                itemPrefix.Assemble("h", 0);
                questItemPrefix.Assemble("h", 0);
            } else {
                transform->GetPacketForUseItem(itemPrefix);
                transform->GetPacketForUseItem(questItemPrefix);
            }

            socket->Send(
                "cchbd", S_ITEMLIST, 1, static_cast<int>(inventoryBlock), itemPrefix.GetLength(), itemPrefix.GetPacket(),
                itemPackets.GetTotalPackageItemCount()
            );
            itemPackets.SendPacket_PackageItemData(socket, S_ITEMLIST, PackageItemPacket::PIPT_ITEM_INFO);

            socket->Send(
                "chcbd", S_EX, S_EX_QUEST_ITEMLIST, 1, questItemPrefix.GetLength(), questItemPrefix.GetPacket(),
                questItemPackets.GetTotalPackageItemCount()
            );
            questItemPackets.SendPacket_PackageItemData(
                socket, S_EX, S_EX_QUEST_ITEMLIST, PackageItemPacket::PIPT_ITEM_INFO
            );
        } else if (itemPackets.GetTotalPackageItemLength() == 0) {
            socket->Send("ccdd", S_INVENTORY_UPDATE, 0, 0, 0);
        } else {
            itemPackets.SendPacket_PackageItemData(socket, S_INVENTORY_UPDATE, PackageItemPacket::PIPT_DEFAULT);
        }

        m_inv.ValidateCarringWeight();
        if (!m_data->m_NeedUpdatePacketBitSet.IsSet(0x49)) {
            m_data->m_NeedUpdatePacketBitSet.SetBit(0x49);
        }
        ParamChanged(std::int64_t{PCB_USER});
    } else {
        destination->Send(
            "ccSdhd", S_GM_VIEW_ITEMLIST, 1, GetName(), GetInventoryLimit(), static_cast<int>(inventoryBlock),
            itemPackets.GetTotalPackageItemCount()
        );
        itemPackets.SendPacket_PackageItemData(destination, S_GM_VIEW_ITEMLIST, PackageItemPacket::PIPT_ITEM_INFO);

        destination->Send(
            "chcSdd", S_EX, S_EX_GM_VIEW_QUEST_ITEMLIST, 1, GetName(), GetQuestInventoryLimit(),
            questItemPackets.GetTotalPackageItemCount()
        );
        questItemPackets.SendPacket_PackageItemData(
            destination, S_EX, S_EX_GM_VIEW_QUEST_ITEMLIST, PackageItemPacket::PIPT_ITEM_INFO
        );
        SendUserHennaInfo(destination);
    }

    SendAdenaInvenCount();
}

void User::RequestPackageSend(const unsigned char* const packet) {
    // Current L2Server 0x00b7c510, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    int targetDatabaseId = 0;
    int requestedItemCount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "dd", &targetDatabaseId, &requestedItemCount);
    if (targetDatabaseId == static_cast<int>(GetDBID())) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] try hack? package to same person",
        //     "user_item.cpp",
        //     0x16d)
        L2_LOG_ERROR(
            Log, u"[package delivery] Character %u attempted to send a package to itself; request rejected.", GetDBID()
        );
        return;
    }
    if (requestedItemCount > 200) {
        return;
    }

    std::array<int, 200> itemDatabaseIds{};
    std::array<char, 0x5000> assembledItems{};
    int assembledLength = 0;
    int assembledItemCount = 0;
    std::int64_t requestedAdena = 0;
    bool valid = true;

    {
        L2_READ_GUARD(m_inv);
        for (int itemIndex = 0; itemIndex < requestedItemCount; ++itemIndex) {
            int itemDatabaseId = 0;
            std::int64_t amount = 0;
            cursor = ::Disassemble(cursor, "dQ", &itemDatabaseId, &amount);
            CItem* const item = m_inv.GetByDBID(itemDatabaseId);
            if (item == nullptr ||
                (item->m_itemOption.itemOptionKey.variationOption1 > 0 &&
                    item->m_itemOption.itemOptionKey.variationOption2 > 0) ||
                item->IsDuration() || (item->IsEtcItem() && !item->CastEtcItem()->m_bPetCollarTradable)) {
                valid = false;
                break;
            }

            if (amount < 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] try hack? amount is below 0",
                //     "user_item.cpp",
                //     0x19a)
                L2_LOG_ERROR(
                    Log,
                    u"[package delivery] Character %u requested invalid amount %I64d for item DB ID %d; package rejected.",
                    GetDBID(), amount, itemDatabaseId
                );
                valid = false;
                break;
            }

            for (int duplicateIndex = 0;
                duplicateIndex < 200 && itemDatabaseIds[static_cast<std::size_t>(duplicateIndex)] != 0; ++duplicateIndex) {
                if (itemDatabaseIds[static_cast<std::size_t>(duplicateIndex)] == itemDatabaseId) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] try hack? same item",
                    //     "user_item.cpp",
                    //     0x1a8)
                    L2_LOG_ERROR(
                        Log, u"[package delivery] Character %u submitted item DB ID %d more than once; package rejected.",
                        GetDBID(), itemDatabaseId
                    );
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                break;
            }
            for (int duplicateIndex = 0; duplicateIndex < 200; ++duplicateIndex) {
                int& stored = itemDatabaseIds[static_cast<std::size_t>(duplicateIndex)];
                if (stored == 0) {
                    stored = itemDatabaseId;
                    break;
                }
            }

            if (item->m_data->m_nCount < amount) {
                valid = false;
            } else {
                const int assembled = ::Assemble(
                    assembledItems.data() + assembledLength, static_cast<int>(assembledItems.size()) - assembledLength,
                    "dQd", itemDatabaseId, amount, item->m_data->GetEnchanted()
                );
                if (assembled > 0) {
                    assembledLength = runtime::AddInt32Wrapping(assembledLength, assembled);
                    ++assembledItemCount;
                }
            }

            if (item->m_data->m_nClassID == 0x39) {
                requestedAdena = runtime::AddInt64Wrapping(requestedAdena, amount);
            }
            if (!valid) {
                break;
            }
        }

        if (valid) {
            CItem* const adena = m_inv.GetFirstItemByClassID(0x39, false, false);
            const std::int64_t fee = static_cast<std::int64_t>(assembledItemCount) * 1000;
            if (adena != nullptr && runtime::AddInt64Wrapping(requestedAdena, fee) <= adena->m_data->m_nCount) {
                const int assembled = ::Assemble(
                    assembledItems.data() + assembledLength, static_cast<int>(assembledItems.size()) - assembledLength,
                    "dQd", adena->GetDBID(), fee, adena->m_data->GetEnchanted()
                );
                if (assembled > 0) {
                    assembledLength = runtime::AddInt32Wrapping(assembledLength, assembled);
                    ++assembledItemCount;
                }
            } else {
                SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
                valid = false;
            }
        }
    }

    if (valid && m_pEconomy != nullptr && m_pEconomy->IsWarehouse()) {
        auto* const warehouse = static_cast<CWareHouseDeposit*>(m_pEconomy);
        if (warehouse->m_nWarehouseType == 3) {
            db.RequestPackageSendToTarget(
                this, targetDatabaseId, assembledItemCount, assembledLength, assembledItems.data(),
                warehouse->GetWarehouseKeeperClassId()
            );
            return;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [%s] send package error.",
        //     L"user_item.cpp",
        //     0x1fb,
        //     GetName())
        // L2_LOG_ERROR(Log,
        //     L"[package delivery] Character '%s' opened a warehouse of the wrong type for package delivery. Request
        //     rejected.", GetName())
        L2_LOG_ERROR(
            Log,
            u"[package delivery] Character '%s' opened a warehouse of the wrong type for package delivery; request "
            u"rejected.",
            GetName()
        );
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] [%s] send package error..",
    //     L"user_item.cpp",
    //     0x200,
    //     GetName())
    // L2_LOG_ERROR(Log,
    //     L"[package delivery] Character '%s' cannot send a package because the request or warehouse state is invalid.",
    //     GetName())
    L2_LOG_ERROR(
        Log, u"[package delivery] Character '%s' cannot send a package because the request or warehouse state is invalid.",
        GetName()
    );
}

void User::SendPackageSendableList(const int requestValue) {
    // Current L2Server 0x00b88f64, original owner user_item.cpp.
    L2_GUARD_FUNCTION();

    CPackageItemPacketManager itemPackets;
    CSPointer<CItem> item;

    std::int64_t adenaAmount;
    {
        L2_READ_GUARD(m_inv);
        CSharedItemData* itemData = m_inv.GetFirst();
        while (itemData != nullptr) {
            item = CItemFactory::Inst().FindObjectSP(itemData->index);
            if (!item || !item->IsItem()) {
                return;
            }

            const std::int32_t etcItemType = item->m_const->m_EtcitemType;
            const bool ordinaryCandidate = !item->IsAccessary() && !item->IsWeapon() && !item->IsArmor() &&
                etcItemType != EIT_ARROW && etcItemType != EIT_LURE && etcItemType != EIT_BOLT;
            if (!(ordinaryCandidate || !IsEquipedItem(item->m_data->index)) || !item->CanKeep(WHC_ETC) ||
                item->IsQuestItem() ||
                !(item->m_itemOption.itemOptionKey.variationOption1 < 1 ||
                    item->m_itemOption.itemOptionKey.variationOption2 < 1) ||
                item->IsDuration() || (item->IsEtcItem() && !item->CastEtcItem()->m_bPetCollarTradable)) {
                itemData = itemData->Next();
                continue;
            }

            itemPackets.CheckLimitPackageItemPacketDataSize();
            CItemAssembler assembler(*this, *item.get(), false);
            int assembled = itemPackets.PackageAssembleItem(&assembler);
            assembled += itemPackets.PackageAssemble("d", item->GetDBID());
            if (assembled < 1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Assemble Error at file[%s], line[%d], "
                //     L"assemble result = [%d], total length = [%d]",
                //     L"user_item.cpp",
                //     0x25d,
                //     assembled,
                //     itemPackets.GetTotalPackageItemLength())
                L2_LOG_ERROR(
                    Log,
                    u"[package delivery] Failed to encode item DB ID %u for character %u: result %d, packet length %d; "
                    u"entry skipped.",
                    item->GetDBID(), GetDBID(), assembled, itemPackets.GetTotalPackageItemLength()
                );
                itemData = itemData->Next();
                continue;
            }

            itemPackets.SetAssembleItemLengthPerItem(assembled);
            itemPackets.IncreasePackageItemCount();
            // Current advances the shared inventory node while the assembler
            // and the kept item are still alive.
            itemData = itemData->Next();
        }

        adenaAmount = m_inv.GetAdenaAmount();
    }

    GetSocket()->Send(
        "ccdQd", S_PACKAGE_SENDABLE_LIST, 1, requestValue, adenaAmount, itemPackets.GetTotalPackageItemCount()
    );
    itemPackets.SendPacket_PackageItemData(GetSocket(), S_PACKAGE_SENDABLE_LIST, PackageItemPacket::PIPT_ITEM_INFO);
}

int User::HandlePointItemManipulation(
    const int point,
    const int pointType,
    const int operation,
    const int itemCount,
    ItemManip* manipulations,
    AtomicJob* atomicJob,
    const int line
) {
    L2_GUARD_FUNCTION();

    if (static_cast<unsigned int>(pointType) < 9U && ((0x122U >> (pointType & 0x1f)) & 1U) != 0U &&
        !CheckPointAddable(pointType, point)) {
        return -1;
    }

    if (itemCount == 0) {
        db.CreateUpdateItemsWithPoint(this, point, pointType, operation, 0, 0, nullptr, 0, nullptr, atomicJob, line);
        return 1;
    }

    ItemManipList manipulationList;
    int result = 1;
    int packetPosition = 0;
    int packetItemCount = 0;
    std::array<char, 20480> packetData;

    if (itemCount > 0) {
        manipulationList.Push(itemCount, manipulations);
        manipulationList.Serialize();

        {
            L2_READ_GUARD(m_inv);
            if (!m_inv.IsValidManipulation(&manipulationList)) {
                return -2;
            }

            for (const ItemManip& manipulation : manipulationList.m_vOutputList) {
                if (!manipulation.MakeDBPacket(
                        m_inv, packetData.data(), static_cast<int>(packetData.size()), packetPosition, packetItemCount,
                        ItemGetReason::Etc
                    )) {
                    // Log.Add(
                    //     LOG_WARN,
                    //     L"[items] Item-and-point update not sent for character DB ID %u (SID %u): cannot encode item
                    //     selector %d, amount delta %lld.", GetDBID(), static_cast<unsigned int>(m_nId),
                    //     manipulation.mClassId,
                    //     static_cast<long long>(manipulation.mCount))
                    L2_LOG_WARN(
                        Log,
                        u"[items] Item-and-point update not sent for character DB ID %u (SID %u): cannot encode item "
                        u"selector %d, amount delta %lld.",
                        GetDBID(), static_cast<unsigned int>(m_nId), manipulation.mClassId,
                        static_cast<long long>(manipulation.mCount)
                    );
                    result = -3;
                    break;
                }
            }
        }
    }

    if (result == 1) {
        db.CreateUpdateItemsWithPoint(
            this, point, pointType, operation, packetItemCount, 0, nullptr, packetPosition, packetData.data(), atomicJob,
            line
        );
    }
    return result;
}

void User::AnnouncePickItem(CItem* const item) {
    // Current L2Server.exe 0x00b8469c.
    L2_GUARD_FUNCTION();
    if (item == nullptr || (!item->IsWeapon() && !item->IsArmor() && !item->IsAccessary())) {
        return;
    }

    const bool variation =
        item->m_itemOption.itemOptionKey.variationOption1 > 0 && item->m_itemOption.itemOptionKey.variationOption2 > 0;
    const FVector position = GetPos();
    const int enchanted = item->m_data->GetEnchanted();
    if (enchanted < 1) {
        g_ptlUserMap->BroadcastToNeighborDist(
            this, 0x5000, position, 0x5dc, "chccScdc", 0x62, 0x5fd, 2, 0x0c, GetName(), 8, item->m_data->m_nClassID,
            variation
        );
        return;
    }

    g_ptlUserMap->BroadcastToNeighborDist(
        this, 0x5000, position, 0x5dc, "chccScccdc", 0x62, 0x5fe, 3, 0x0c, GetName(), 0x13,
        static_cast<int>(static_cast<char>(enchanted)), 8, item->m_data->m_nClassID, variation
    );
}

void User::BroadcastItemGet2Party(
    const int messageId, const int itemClassId, const std::int64_t amount, const int enchant, const bool flag
) {
    // Current L2Server 0x00b42de4, 982 bytes. The current body carries the
    // message-id argument in its ABI but selects the packet ids itself.
    L2_GUARD_FUNCTION();
    (void)messageId;

    CParty* const party = GetParty();
    if (party == nullptr || itemClassId == 57) {
        return;
    }

    std::vector<User*> members;
    party->GetAllMember(members);
    for (User* const member : members) {
        if (member == nullptr || member->m_data->m_nId == m_data->m_nId) {
            continue;
        }

        const char16_t* const visibleName = GetName2(member);
        if (amount < 2) {
            if (itemClassId == 0x115a || enchant < 1) {
                member->GetSocket()->Send(
                    "chccScdc", 0x62, SystemMessageId::CHARACTER_OBTAINED_ITEM, 2, 0x0c, visibleName, 8, itemClassId, flag
                );
            } else {
                member->GetSocket()->Send(
                    "chccScccdc", 0x62, SystemMessageId::CHARACTER_OBTAINED_ENCHANTED_ITEM, 3, 0x0c, visibleName, 0x13,
                    static_cast<int>(static_cast<char>(enchant)), 8, itemClassId, flag
                );
            }
        } else if (itemClassId == 0x115a || enchant < 1) {
            member->GetSocket()->Send(
                "chccScdcQ", 0x62, SystemMessageId::CHARACTER_OBTAINED_MULTIPLE_ITEMS, 3, 0x0c, visibleName, 3, itemClassId,
                6, amount
            );
        } else {
            member->GetSocket()->Send(
                "chccScccd", 0x62, SystemMessageId::CHARACTER_OBTAINED_ENCHANTED_ITEM, 3, 0x0c, visibleName, 0x13,
                static_cast<int>(static_cast<char>(enchant)), 3, itemClassId
            );
        }
    }
}

bool User::ReplyEnchantItem(
    CItem& scrollItem,
    const std::int64_t scrollAmount,
    CItem* const supportItem,
    const std::int64_t supportAmount,
    CItem& targetItem,
    const int enchant,
    const bool lucky
) {
    // Current L2Server 0x00b8d57c, 2550 bytes.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);

    const int crystalType = static_cast<int>(targetItem.m_const->GetCrystalType());
    auto getEnchantOptions = [&targetItem](int (&options)[3]) {
        options[0] = 0;
        options[1] = 0;
        options[2] = 0;
        if (targetItem.m_pEnchantOption != nullptr) {
            const EnchantOptionId& optionId = targetItem.m_pEnchantOption->GetOptionId();
            options[0] = optionId.at[0];
            options[1] = optionId.at[1];
            options[2] = optionId.at[2];
        }
    };

    if (enchant == -1) {
        GetSocket()->Send(
            "cddQdddd", 0x87,
            // 1,
            IER_FAIL, CrystalClassByCrystalType[crystalType], targetItem.GetCrystalizeCount(2), 0, 0, 0, 0
        );
        m_inv.SetInventoryChanged(&targetItem, IUT_UPDATE);
        ParamChanged(std::int64_t{PCB_USER});
        return false;
    }

    const int previousEnchant = targetItem.m_data->GetEnchanted();
    DeleteEnchantOptionSkills(targetItem);
    {
        L2_GUARD(*targetItem.m_lock);
        targetItem.SetEnchanted(enchant);
    }
    m_inv.SetInventoryChanged(&targetItem, IUT_UPDATE);

    const int scrollClassId = scrollItem.m_data->m_nClassID;
    const int scrollEnchant = scrollItem.m_data->GetEnchanted();
    if (scrollAmount == 0) {
        if (scrollItem.m_data->m_ItemConsumeType != ICT_CHARGE) {
            static_cast<void>(m_inv.Pop(scrollItem.m_data->index, false));
            m_inv.SetInventoryChanged(&scrollItem, IUT_DELETE);
            scrollItem.Delete();
        }
        SetPendingUseETCItem(nullptr, PUIT_ENCHANT);
    } else {
        {
            L2_GUARD(*scrollItem.m_lock);
            if (!scrollItem.AddAmount(scrollAmount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "user_item.cpp",
                //     0x227d,
                //     scrollItem.m_data->m_nCount,
                //     scrollAmount)
                L2_LOG_ERROR(
                    Log, u"[enchant] Scroll item DB ID %u rejected amount adjustment %I64d; current amount is %I64d.",
                    scrollItem.GetDBID(), scrollAmount, scrollItem.m_data->m_nCount
                );
            }
        }
        m_inv.SetInventoryChanged(&scrollItem, IUT_UPDATE);
    }

    int supportClassId = 0;
    if (supportItem == nullptr) {
        SetPendingUseETCItem(nullptr, PUIT_INC_ENCHANT_PROP);
        InitAllEnchantBonusOptions();
    } else {
        supportClassId = supportItem->m_data->m_nClassID;
        if (supportAmount == 0) {
            static_cast<void>(m_inv.Pop(supportItem->m_data->index, false));
            m_inv.SetInventoryChanged(supportItem, IUT_DELETE);
            supportItem->Delete();
            SetPendingUseETCItem(nullptr, PUIT_INC_ENCHANT_PROP);
            InitAllEnchantBonusOptions();
        } else if (supportAmount != -1) {
            {
                L2_GUARD(*supportItem->m_lock);
                if (!supportItem->AddAmount(supportAmount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     "user_item.cpp",
                    //     0x229d,
                    //     supportItem->m_data->m_nCount,
                    //     supportAmount)
                    L2_LOG_ERROR(
                        Log, u"[enchant] Support item DB ID %u rejected amount adjustment %I64d; current amount is %I64d.",
                        supportItem->GetDBID(), supportAmount, supportItem->m_data->m_nCount
                    );
                }
            }
            m_inv.SetInventoryChanged(supportItem, IUT_UPDATE);
        }
    }

    ValidateSetItem(ST_MAX);
    ValidateNewSetItem();
    ValidateShapeShiftedSetItem();
    ValidateChanged(std::int64_t{VB_MAX});

    auto broadcastEnchantEffect = [this](const int messageId) {
        char& enchantEffect = reinterpret_cast<char&>(m_hMinNewSetItemEnchanted);
        m_data->m_NeedUpdatePacketBitSet.SetCharValue_IfModifed(enchantEffect, 0, 0x5a);

        SkillUseInfo skill;
        skill.m_skillID = messageId;
        skill.m_skillLevel = 1;
        skill.m_skillSubLevel = 0;
        skill.m_castRange = 0;
        skill.m_type = 0;
        skill.m_param1 = 0;
        int parameter1 = 0;
        int parameter2 = 500;
        int parameter3 = 0;
        int parameter4 = 0x5dc;
        int parameter5 = 0;
        int parameter6 = 0;
        DefaultIOBuffer* const packet = MakePacketForMagicSkillUse(
            parameter1, this, skill, parameter2, parameter3, parameter4, parameter5, parameter6, nullptr
        );
        g_ptlUserMap->BroadcastToNeighborDist(this, 0xf000, GetPos(), GetBroadcastDist(0x1000), packet);
    };

    if (lucky) {
        SkillUseInfo skill;
        skill.m_skillID = 0x46b7;
        skill.m_skillLevel = 1;
        skill.m_skillSubLevel = 0;
        skill.m_castRange = 0;
        skill.m_type = 0;
        skill.m_param1 = 0;
        int parameter1 = 0;
        int parameter2 = 500;
        int parameter3 = 0;
        int parameter4 = 0x5dc;
        int parameter5 = 0;
        int parameter6 = 0;
        DefaultIOBuffer* const packet = MakePacketForMagicSkillUse(
            parameter1, this, skill, parameter2, parameter3, parameter4, parameter5, parameter6, nullptr
        );
        g_ptlUserMap->BroadcastToNeighborDist(this, 0xf000, GetPos(), GetBroadcastDist(0x1000), packet);
        SendSystemMessage(SystemMessageId::LADY_LUCK_ACTIVATED);
    }

    g_npcserver.Send("cdddd", 0xc6, m_data->index, 0, 1, targetItem.m_data->GetEnchanted());

    const bool succeeded = enchant > 0 && enchant > previousEnchant;
    int options[3];
    getEnchantOptions(options);
    if (!succeeded) {
        // const int result = enchant == previousEnchant ? 5 : 3
        const int result = enchant == previousEnchant ? IER_ANCIENT_BLESSED_FAIL : IER_BLESSED_FAIL;
        if (enchant != previousEnchant) {
            SetPendingUseETCItem(nullptr, PUIT_INC_ENCHANT_PROP);
            InitAllEnchantBonusOptions();
        }
        GetSocket()->Send(
            "cddQddddddQdQ", 0x87, result, 0, std::int64_t{0}, enchant, options[0], options[1], options[2], scrollClassId,
            scrollEnchant, scrollAmount, supportClassId, supportAmount
        );
    } else {
        if (crystalType == 0x0b) {
            const int currentEnchant = targetItem.m_data->GetEnchanted();
            int effectMessage = 0;
            if (currentEnchant >= 4 && currentEnchant <= 8) {
                effectMessage = 0x174d;
            } else if (currentEnchant >= 9 && currentEnchant <= 18) {
                effectMessage = 0x173b;
            } else if (currentEnchant >= 19 && currentEnchant <= 22) {
                effectMessage = 0x173c;
            } else if (currentEnchant >= 23) {
                effectMessage = 0x174e;
            }
            if (effectMessage != 0) {
                broadcastEnchantEffect(effectMessage);
            }
        }

        GetSocket()->Send(
            "cddQdddd", 0x87,
            // 0,
            IER_SUCCESS, 0, std::int64_t{0}, enchant, options[0], options[1], options[2]
        );
        for (int level = targetItem.m_data->GetEnchanted(); previousEnchant < level; --level) {
            PledgeMissionCheckAndAddCount(PledgeMission::PMGT_ITEM_ENCHANT_NUM_SUC, targetItem.GetClassId(), level, 1);
        }

        ItemEnchantAnnouncer announcer(&CUserBroadcaster::inst());
        if (announcer.NeedToAnnounce(targetItem)) {
            announcer.Announce(GetName(), targetItem);
        }
    }

    SendItemList(true, true, nullptr);
    m_data->m_NeedUpdatePacketBitSet.SetBit(0x49);
    ParamChanged(std::int64_t{PCB_USER});
    if (succeeded && crystalType >= 8 && crystalType <= 10) {
        if (targetItem.IsWeapon()) {
            m_UserStatData.OnEnchantSucceeded(UserStat::IC_WEAPON, enchant);
        } else if (targetItem.IsArmor()) {
            m_UserStatData.OnEnchantSucceeded(UserStat::IC_ARMOR, enchant);
        }
    }
    PledgeMissionCheckAndAddCount(PledgeMission::PMGT_ITEM_ENCHANT_TRY, targetItem.GetClassId(), 1);
    return true;
}

void User::ReplyEnchantItemFail(
    CItem& scrollItem,
    const std::int64_t scrollAmount,
    CItem* const supportItem,
    const std::int64_t supportAmount,
    CItem& targetItem,
    const int crystalItemDatabaseId,
    const std::int64_t crystalAmount
) {
    // Current L2Server 0x00b8df74, 1981 bytes.
    L2_GUARD_FUNCTION();
    bool deleteScrollAfterCleanup = false;
    const bool targetDeletionQueued = UnequipItem(&targetItem, true) == ST_NONE;
    ValidateChanged(std::int64_t{VB_MAX});

    if (scrollAmount == 0) {
        if (scrollItem.m_data->m_ItemConsumeType == ICT_CHARGE) {
            // Log.Add(LOG_ERROR, L"error. target item is not exist.")
            L2_LOG_ERROR(Log, u"[items] Target item does not exist; enchant cleanup started.");
            goto cleanup;
        }
        static_cast<void>(m_inv.Pop(scrollItem.m_data->index, false));
        m_inv.SetInventoryChanged(&scrollItem, IUT_DELETE);
        deleteScrollAfterCleanup = true;
    } else if (scrollAmount == -1) {
        // Log.Add(LOG_ERROR, L"error. target item is not exist.")
        L2_LOG_ERROR(Log, u"[items] Target item does not exist; enchant cleanup started.");
        goto cleanup;
    } else {
        {
            L2_GUARD(*scrollItem.m_lock);
            if (!scrollItem.AddAmount(scrollAmount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     "user_item.cpp",
                //     0x23c1,
                //     scrollItem.m_data->m_nCount,
                //     scrollAmount)
                L2_LOG_ERROR(
                    Log, u"[enchant] Scroll item DB ID %u rejected amount adjustment %I64d; current amount is %I64d.",
                    scrollItem.GetDBID(), scrollAmount, scrollItem.m_data->m_nCount
                );
            }
        }
        m_inv.SetInventoryChanged(&scrollItem, IUT_UPDATE);
    }

    if (supportItem != nullptr) {
        if (supportAmount == 0) {
            static_cast<void>(m_inv.Pop(supportItem->m_data->index, false));
            m_inv.SetInventoryChanged(supportItem, IUT_DELETE);
            supportItem->Delete();
        } else if (supportAmount != -1) {
            {
                L2_GUARD(*supportItem->m_lock);
                if (!supportItem->AddAmount(supportAmount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     "user_item.cpp",
                    //     0x23d7,
                    //     supportItem->m_data->m_nCount,
                    //     supportAmount)
                    L2_LOG_ERROR(
                        Log, u"[enchant] Support item DB ID %u rejected amount adjustment %I64d; current amount is %I64d.",
                        supportItem->GetDBID(), supportAmount, supportItem->m_data->m_nCount
                    );
                }
            }
            m_inv.SetInventoryChanged(supportItem, IUT_UPDATE);
        }
    }

    if (!targetDeletionQueued) {
        static_cast<void>(m_inv.Pop(targetItem.m_data->index, false));
        m_inv.SetInventoryChanged(&targetItem, IUT_DELETE);
    }

    {
        const int crystalType = static_cast<int>(targetItem.m_const->GetCrystalType());
        const int crystalClassId = CrystalClassByCrystalType[crystalType];
        std::int64_t producedCrystalAmount = 0;
        if (targetItem.CanCrystalized()) {
            producedCrystalAmount = targetItem.GetCrystalizeCount(2);
            CItem* crystal = m_inv.GetByDBID(crystalItemDatabaseId);
            if (crystal == nullptr) {
                crystal = g_ObjectDB.CreateItem(crystalClassId);
                if (crystal == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d][Enchant Result] CreateItem Failed. item[%I64d]",
                    //     L"user_item.cpp",
                    //     0x240e,
                    //     std::int64_t{0})
                    L2_LOG_ERROR(
                        Log,
                        u"[enchant] Failed to create crystal item class %d after enchant failure; reward item is "
                        u"unavailable.",
                        crystalClassId
                    );
                } else {
                    crystal->SetDBID(static_cast<unsigned int>(crystalItemDatabaseId));
                    {
                        L2_GUARD(*crystal->m_lock);
                        if (!crystal->SetAmount(crystalAmount)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%s][%d][Enchant Result] item[%I64d] "
                            //     L"amount is invalid[%I64d][%I64d]",
                            //     L"user_item.cpp",
                            //     0x2403,
                            //     std::int64_t{0},
                            //     crystal->m_data->m_nCount,
                            //     crystalAmount)
                            L2_LOG_ERROR(
                                Log, u"[enchant] New crystal item DB ID %u rejected amount %I64d; current amount is %I64d.",
                                crystal->GetDBID(), crystalAmount, crystal->m_data->m_nCount
                            );
                        }
                    }
                    static_cast<void>(m_inv.Push(crystal->m_data->index));
                    m_inv.SetInventoryChanged(crystal, IUT_ADD);
                    SendItemIncreaseSystemMessage(
                        crystal->GetClassId(), producedCrystalAmount, crystal->m_data->GetEnchanted()
                    );
                }
            } else {
                {
                    L2_GUARD(*crystal->m_lock);
                    if (!crystal->AddAmount(producedCrystalAmount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d][Enchant Result] item[%I64d] "
                        //     L"amount is invalid[%I64d][%I64d]",
                        //     L"user_item.cpp",
                        //     0x2416,
                        //     std::int64_t{0},
                        //     crystal->m_data->m_nCount,
                        //     crystalAmount)
                        L2_LOG_ERROR(
                            Log, u"[enchant] Crystal item DB ID %u rejected amount increase %I64d; current amount is %I64d.",
                            crystal->GetDBID(), producedCrystalAmount, crystal->m_data->m_nCount
                        );
                    }
                }
                m_inv.SetInventoryChanged(crystal, IUT_UPDATE);
                SendItemIncreaseSystemMessage(crystal->GetClassId(), producedCrystalAmount, crystal->m_data->GetEnchanted());
            }
        }

        if (!targetDeletionQueued) {
            targetItem.Delete();
        }

        int options[3] = {0, 0, 0};
        if (targetItem.m_pEnchantOption != nullptr) {
            const EnchantOptionId& optionId = targetItem.m_pEnchantOption->GetOptionId();
            options[0] = optionId.at[0];
            options[1] = optionId.at[1];
            options[2] = optionId.at[2];
        }

        if (crystalType == 0x0b) {
            char& enchantEffect = reinterpret_cast<char&>(m_hMinNewSetItemEnchanted);
            m_data->m_NeedUpdatePacketBitSet.SetCharValue_IfModifed(enchantEffect, 0, 0x5a);
            SkillUseInfo skill;
            skill.m_skillID = 0x173d;
            skill.m_skillLevel = 1;
            skill.m_skillSubLevel = 0;
            skill.m_castRange = 0;
            skill.m_type = 0;
            skill.m_param1 = 0;
            int parameter1 = 0;
            int parameter2 = 500;
            int parameter3 = 0;
            int parameter4 = 0x5dc;
            int parameter5 = 0;
            int parameter6 = 0;
            DefaultIOBuffer* const packet = MakePacketForMagicSkillUse(
                parameter1, this, skill, parameter2, parameter3, parameter4, parameter5, parameter6, nullptr
            );
            g_ptlUserMap->BroadcastToNeighborDist(this, 0xf000, GetPos(), GetBroadcastDist(0x1000), packet);
        }

        GetSocket()->Send(
            "cddQdddd", 0x87,
            // crystalType == 0x0b ? 4 : 1,
            crystalType == 0x0b ? IER_EVENT_FAIL : IER_FAIL, crystalClassId, producedCrystalAmount, 0, options[0],
            options[1], options[2]
        );
        m_data->m_NeedUpdatePacketBitSet.SetBit(0x49);
        SendItemList(true, true, nullptr);
        ParamChanged(std::int64_t{PCB_USER});
    }

cleanup:
    SetPendingUseETCItem(nullptr, PUIT_ENCHANT);
    SetPendingUseETCItem(nullptr, PUIT_INC_ENCHANT_PROP);
    InitAllEnchantBonusOptions();
    runtime::raw_atomic::ExchangeI32(&m_lWaitVariation, 0);
    if (deleteScrollAfterCleanup) {
        scrollItem.Delete();
    }
}

bool User::RemoveItemReuseDelay(const int itemClassId) {
    // Current L2Server.exe 0x00b96918, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
    CItem* const item = object != nullptr ? object->CastItem() : nullptr;
    if (item == nullptr) {
        return false;
    }

    const int sharedReuseGroupId = item->m_const->m_nDelayShareGroup;
    if (sharedReuseGroupId > 0) {
        static_cast<void>(m_SharedReuseDelaysOfItems.RemoveReuseDelay(*this, sharedReuseGroupId));
        m_ShortCut.SendUseSharedGroupItem(item);
        return true;
    }

    L2_GUARD(m_UsedItemsSect);
    ItemPointerList items;
    {
        L2_READ_GUARD(m_inv);
        m_inv.GetItemListByClassID(items, itemClassId, -1, -1, false);
    }

    bool removed = false;
    for (const CSPointer<CItem>& ownedItem : items) {
        const int itemDatabaseId = static_cast<int>(ownedItem->GetDBID());
        const auto position = m_UsedItemsLastTimeMap.find(itemDatabaseId);
        if (position != m_UsedItemsLastTimeMap.end()) {
            m_UsedItemsLastTimeMap.erase(position);
            removed = true;
        }
    }
    return removed;
}

void User::RemoveItemReuseDelayAll() {
    // Current L2Server.exe 0x00b82858, original owner user_item.cpp.
    L2_GUARD_FUNCTION();
    m_SharedReuseDelaysOfItems.RemoveReuseDelayAll(*this);
    m_ShortCut.SendShortCutInfo();
    L2_GUARD(m_UsedItemsSect);
    m_UsedItemsLastTimeMap.clear();
}

// Current L2Server.exe 0x00b78c2c.
bool User::AddItemToInventory2(CItem* item) {
    L2_GUARD_FUNCTION();
    if (item == nullptr) {
        return false;
    }
    return PickItem(item);
}
