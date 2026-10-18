#include "PledgeItem.h"
#include "DB.h"
#include "Item.h"
#include "ItemAssembler.h"
#include "ObjectDB.h"
#include "Pledge.h"
#include "PledgeItemInfo.h"
#include "PledgeMastery.h"
#include "PledgeStuff.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "User.h"
#include "UserPacketEx.h"
#include "UserSocket.h"

#include "Shared/src/AtomicJob.h"
#include "Shared/src/Config.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/proto/DBPacket.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <string>

namespace {


void Send_S_EX_PLEDGE_ITEM_BUY(User* const user, const char result) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    protocol::S_EX_PLEDGE_ITEM_BUY packet{};
    packet.nResult = result;
    PledgeStuff::SendPacket(user, S_EX_PLEDGE_ITEM_BUY, packet);
}

void Send_S_EX_PLEDGE_ITEM_ACTIVATE(User* const user, const char result) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    protocol::S_EX_PLEDGE_ITEM_ACTIVATE packet{};
    packet.nResult = result;
    PledgeStuff::SendPacket(user, S_EX_PLEDGE_ITEM_ACTIVATE, packet);
}

bool CheckCostPoint(User* const user, const PledgeItemInfo* const info, const std::int64_t amount) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x27,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x27,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot check clan-shop point cost for quantity %lld: the user is unavailable.",
            static_cast<long long>(amount)
        );
        return false;
    }
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x28,
        //     L"(pInfo != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x28,
        //     L"(pInfo != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot check clan-shop point cost for character %u: the item definition is unavailable.",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    if (!info->SellCost.HasPointCost()) {
        return true;
    }

    const std::int64_t total = runtime::MultiplyInt64Wrapping(info->SellCost.point_amount, amount);
    const std::int32_t negative = runtime::Int32FromBits(0U - static_cast<std::uint32_t>(total));
    return user->CheckPointAddable(static_cast<int>(info->SellCost.point_type), negative) &&
        -static_cast<std::int64_t>(negative) == total;
}

} // namespace

bool GetPledgeItemInfo(
    const PledgeItemInfo* const info, const bool addable, const PledgeItem* const items, protocol::PkPledgeItemInfo& packet
) {
    L2_GUARD_FUNCTION();

    int remainCount = info->Refill.interval == 0 ? -1 : info->Refill.amount;
    const std::int32_t now = runtime::crt::Time32Now();
    std::int32_t expiryTime = runtime::AddInt32Wrapping(now, info->expiry_time);
    std::int32_t resetTime = runtime::AddInt32Wrapping(now, info->Refill.interval);
    const bool found = items->GetItemInfo(info->class_id, remainCount, expiryTime, resetTime);

    packet.cStatus = static_cast<char>(found ? 2 : (addable ? 1 : 0));
    packet.nSellPriceAdena = info->SellCost.adena;
    packet.nSellPriceNameValue = static_cast<std::int32_t>(info->SellCost.point_amount);
    packet.nLevel = static_cast<char>(info->Condition.level);
    packet.nMasteryID = static_cast<char>(info->Condition.mastery_id);
    packet.nActivePriceAdena = info->ActiveCost.adena;
    packet.nActivePriceNameValue = info->ActiveCost.name_value;
    if (expiryTime == 0) {
        packet.nRemainExpirySecond = 0;
    } else if (now <= expiryTime) {
        packet.nRemainExpirySecond = runtime::SubtractInt32Wrapping(expiryTime, now);
    } else {
        packet.nRemainExpirySecond = 1;
    }
    packet.nRemainResetSecond = resetTime == 0 ? 0 : runtime::SubtractInt32Wrapping(resetTime, now);
    packet.nRemainAmount = static_cast<std::int16_t>(remainCount);
    packet.nRefillAmount = static_cast<std::int16_t>(info->Refill.amount);
    return true;
}

namespace {

std::u16string FormatPledgeItemBuilderMessage(const int itemClassId, const protocol::PkPledgeItemInfo& item) {
    std::array<char16_t, 256> message{};
    FormatUtf16To(
        message.data(), message.size(), u"%d: (%d/%d), %d, (%d/%d)", itemClassId, item.cStatus, item.nSellPriceNameValue,
        item.nActivePriceNameValue, item.nRemainAmount, item.nRefillAmount
    );
    return std::u16string(message.data());
}

} // namespace

CItem* FindItem(const int itemClassId) {
    CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
    if (object == nullptr || reinterpret_cast<std::uintptr_t>(object) == UINT64_C(0xcdcdcdcdcdcdcdcd)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Undefined item: %d",
        //     L"PledgeItem.cpp",
        //     0x391,
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Undefined item: %d",
        //     L"PledgeItem.cpp",
        //     0x391,
        //     itemClassId)
        L2_LOG_ERROR(
            Log,
            "Cannot encode clan-shop item %d: its object prototype is missing or has the uninitialized-pointer sentinel "
            "value.",
            itemClassId
        );
        return nullptr;
    }
    if (!object->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Not a item: %d",
        //     L"PledgeItem.cpp",
        //     0x397,
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Not a item: %d",
        //     L"PledgeItem.cpp",
        //     0x397,
        //     itemClassId)
        L2_LOG_ERROR(Log, "Cannot encode clan-shop item %d: its object prototype is not an item.", itemClassId);
        return nullptr;
    }
    return static_cast<CItem*>(object);
}

bool Assemble_PledgeItemInfo(
    CPacketHelperEx<20480>& packet, const PledgeItemInfo* const info, const bool addable, const PledgeItem* const items
) {
    L2_GUARD_FUNCTION();
    CItem* const item = FindItem(info->class_id);
    if (item == nullptr) {
        return false;
    }

    protocol::PkPledgeItemInfo pledgeInfo{};
    if (!GetPledgeItemInfo(info, addable, items, pledgeInfo)) {
        return false;
    }
    CItemAssemblerSendToCLNT assembler(*item);
    const int assembled = assembler.AssembleItemInfo(packet);
    packet.Assemble(
        "cQdccQdddhh", pledgeInfo.cStatus, pledgeInfo.nSellPriceAdena, pledgeInfo.nSellPriceNameValue, pledgeInfo.nLevel,
        pledgeInfo.nMasteryID, pledgeInfo.nActivePriceAdena, pledgeInfo.nActivePriceNameValue,
        pledgeInfo.nRemainExpirySecond, pledgeInfo.nRemainResetSecond, pledgeInfo.nRemainAmount, pledgeInfo.nRefillAmount
    );
    return assembled > 0;
}

int Assemble_PledgeItemList(User* const user, CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x40e,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x40e,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot encode the clan-shop item list: the user is unavailable; returning zero entries.");
        return 0;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return 0;
    }

    int count = 0;
    for (const PledgeItemInfo* const info : PledgeItemDB::inst().GetInfos()) {
        const bool addable =
            PledgeItemStuff::IsAddableItem(static_cast<int>(pledge->GetPledgeLevel()), pledge->GetMastery(), info);
        if (Assemble_PledgeItemInfo(packet, info, addable, pledge->GetItem())) {
            ++count;
        }
    }
    return count;
}

class PledgeItemBuying {
public:
    PledgeItemBuying(User* const user, const int itemClassId, const std::int64_t amount) :
        m_pUser(user), m_idItem(itemClassId), m_buyAmount(amount), m_pPledgeItemInfo(nullptr) {}

    bool Prepare();
    void Do(AtomicLambdaJob* job);

private:
    User* m_pUser;
    int m_idItem;
    std::int64_t m_buyAmount;
    const PledgeItemInfo* m_pPledgeItemInfo;
};

bool PledgeItemBuying::Prepare() {
    L2_GUARD_FUNCTION();
    m_pPledgeItemInfo = PledgeItemDB::inst().Find(m_idItem);
    if (m_pPledgeItemInfo == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeItem.cpp",
        //     0x4f5)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeItem.cpp",
        //     0x4f5)
        L2_LOG_ERROR(
            Log, "Cannot prepare clan-shop purchase of item %d (quantity %lld): its definition is unavailable.", m_idItem,
            static_cast<long long>(m_buyAmount)
        );
    }
    return m_pPledgeItemInfo != nullptr;
}

void PledgeItemBuying::Do(AtomicLambdaJob* const job) {
    L2_GUARD_FUNCTION();
    if (m_pUser == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x4cb,
        //     L"(m_pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x4cb,
        //     L"(m_pUser != NULL)")
        L2_LOG_ERROR(
            Log,
            "Cannot execute clan-shop purchase of item %d (quantity %lld): the user is unavailable; the callback is not "
            "invoked.",
            m_idItem, static_cast<long long>(m_buyAmount)
        );
        return;
    }
    if (m_idItem < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x4cc,
        //     L"(0 < m_idItem)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x4cc,
        //     L"(0 < m_idItem)")
        L2_LOG_ERROR(
            Log, "Cannot execute clan-shop purchase: item class %d is not positive; the callback is not invoked.", m_idItem
        );
        return;
    }
    if (m_buyAmount < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x4cd,
        //     L"(0 < m_buyAmount)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x4cd,
        //     L"(0 < m_buyAmount)")
        L2_LOG_ERROR(
            Log, "Cannot execute clan-shop purchase of item %d: quantity %lld is not positive; the callback is not invoked.",
            m_idItem, static_cast<long long>(m_buyAmount)
        );
        return;
    }
    if (!Prepare()) {
        PledgeStuff::DoJob(job, -1, true);
        PledgeStuff::DestroyJob(job);
        return;
    }

    PledgeStuff::ItemManipHelper helper(m_pUser, ItemGetReason::PledgeItemBuy);
    helper.PushManiplutor(m_idItem, m_buyAmount);
    if (m_pPledgeItemInfo->SellCost.HasItemCost()) {
        helper.PushManiplutor(
            m_pPledgeItemInfo->SellCost.item_class,
            runtime::NegateInt64Wrapping(
                runtime::MultiplyInt64Wrapping(m_pPledgeItemInfo->SellCost.item_amount, m_buyAmount)
            )
        );
    }
    if (m_pPledgeItemInfo->SellCost.adena > 0) {
        helper.PushManiplutor(
            57,
            runtime::NegateInt64Wrapping(
                runtime::MultiplyInt64Wrapping(m_pPledgeItemInfo->SellCost.adena, m_buyAmount)
            )
        );
    }
    if (m_pPledgeItemInfo->SellCost.HasPointCost()) {
        helper.PushPointEtcMani(
            m_pPledgeItemInfo->SellCost.point_type,
            runtime::NegateInt64Wrapping(
                runtime::MultiplyInt64Wrapping(m_pPledgeItemInfo->SellCost.point_amount, m_buyAmount)
            )
        );
    }
    helper.Do(job);
}

L2_DEFINE_OBJECT_CLASS_ID(PledgeItem)

L2_DEFINE_OBJECT_CLASS_NAME(PledgeItem, m_pClassName_PledgeItem)

L2_DEFINE_OBJECT_NEW(PledgeItem)

L2_DEFINE_OBJECT_ALLOCATOR(PledgeItem, __GetClassName())

L2_DEFINE_OBJECT_ADD_REF(PledgeItem)

L2_DEFINE_OBJECT_RELEASE(PledgeItem)

PledgeItem::PledgeItem() : m_bLoadedFromDB(false), m_TransactionLock(0x1e) {}

PledgeItem::~PledgeItem() = default;

void PledgeItem::ItemNode::SetItemNode(const PledgeItemInfo* const info, const std::int32_t expiryTime) {
    id = info != nullptr ? info->class_id : 0;
    tExpiryTime = expiryTime;
    nRemainCount = info != nullptr ? info->Refill.amount : 0;
    tLastCheckTime = runtime::crt::Time32Now();
    pInfo = info;
}

bool PledgeItem::AddItem(const int itemClassId, const std::int32_t expiryTime) {
    L2_GUARD_FUNCTION();
    const PledgeItemInfo* const info = PledgeItemDB::inst().Find(itemClassId);
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Not pledge item: %d",
        //     L"PledgeItem.cpp",
        //     0x83,
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Not pledge item: %d",
        //     L"PledgeItem.cpp",
        //     0x83,
        //     itemClassId)
        L2_LOG_ERROR(Log, "Cannot add clan-shop item %d: no clan-item definition is registered.", itemClassId);
        return false;
    }
    L2_GUARD(m_lock);
    m_Items[itemClassId].SetItemNode(info, expiryTime);
    return true;
}

bool PledgeItem::RemoveItem(const int itemClassId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    return m_Items.erase(itemClassId) != 0;
}

bool PledgeItem::HasItem(const int itemClassId) const {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    return m_Items.find(itemClassId) != m_Items.end();
}

const PledgeItemInfo* PledgeItem::FindInfo(const int itemClassId) const {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto found = m_Items.find(itemClassId);
    return found == m_Items.end() ? nullptr : found->second.pInfo;
}

bool PledgeItem::IncreaseItemCount(const int itemClassId, const int amount) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto found = m_Items.find(itemClassId);
    if (found == m_Items.end()) {
        return false;
    }

    ItemNode& node = found->second;
    if (node.pInfo->Refill.interval != 0) {
        node.nRemainCount = std::min(runtime::AddInt32Wrapping(node.nRemainCount, amount), node.pInfo->Refill.amount);
    }
    return true;
}

bool PledgeItem::DecreaseItemCount(const int itemClassId, const int amount, std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto found = m_Items.find(itemClassId);
    if (found == m_Items.end()) {
        return false;
    }

    ItemNode& node = found->second;
    if (node.pInfo->Refill.interval == 0) {
        return true;
    }
    if (currentTime == 0) {
        currentTime = runtime::crt::Time32Now();
    }
    if (node.pInfo->Refill.interval <= runtime::SubtractInt32Wrapping(currentTime, node.tLastCheckTime)) {
        node.nRemainCount = node.pInfo->Refill.amount;
        node.tLastCheckTime = currentTime;
    }
    if (node.nRemainCount < amount) {
        return false;
    }
    node.nRemainCount = runtime::SubtractInt32Wrapping(node.nRemainCount, amount);
    return true;
}

bool PledgeItem::GetItemInfo(
    const int itemClassId, int& remainCount, std::int32_t& expiryTime, std::int32_t& resetTime
) const {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto found = m_Items.find(itemClassId);
    if (found == m_Items.end()) {
        return false;
    }

    const ItemNode& node = found->second;
    remainCount = node.nRemainCount;
    expiryTime = node.tExpiryTime;
    resetTime = node.pInfo->Refill.interval == 0
        ? 0
        : runtime::AddInt32Wrapping(node.pInfo->Refill.interval, node.tLastCheckTime);
    return true;
}

void PledgeItem::RefillItemCount(std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    if (currentTime == 0) {
        currentTime = runtime::crt::Time32Now();
    }
    for (auto& entry : m_Items) {
        ItemNode& node = entry.second;
        const int interval = node.pInfo->Refill.interval;
        if (interval != 0 && interval <= runtime::SubtractInt32Wrapping(currentTime, node.tLastCheckTime)) {
            node.nRemainCount = node.pInfo->Refill.amount;
            node.tLastCheckTime = currentTime;
        }
    }
}

void PledgeItem::SelectItemIDs(std::vector<int>& itemIds, const bool masteryOnly) const {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    itemIds.reserve(m_Items.size());
    for (const auto& entry : m_Items) {
        const PledgeItemInfo* const info = PledgeItemDB::inst().Find(entry.first);
        if (info == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]No pledge item info: %d, %d",
            //     L"PledgeItem.cpp",
            //     0xec,
            //     entry.first,
            //     static_cast<int>(reinterpret_cast<std::intptr_t>(
            //         &entry.second)))
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]No pledge item info: %d, %d",
            //     L"PledgeItem.cpp",
            //     0xec,
            //     entry.first,
            //     static_cast<int>(reinterpret_cast<std::intptr_t>(
            //         &entry.second)))
            L2_LOG_ERROR(
                Log,
                "Cannot select clan-shop item %d for removal: its definition is unavailable; the item is omitted from the "
                "removal list.",
                entry.first
            );
        } else if (!masteryOnly || info->Condition.HasMasteryCondition()) {
            itemIds.push_back(entry.first);
        }
    }
}

void PledgeItem::SelectExpiredItemIDs(const std::int32_t now, std::vector<int>& itemIds) const {
    // Current L2Server 0x009b272c.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    itemIds.reserve(m_Items.size());
    for (const auto& item : m_Items) {
        if (item.second.tExpiryTime != 0 && item.second.tExpiryTime <= now) {
            itemIds.push_back(item.first);
        }
    }
}

void PledgeItem::FilterInfos(std::map<int, PledgeItemInfo*>& infos) const {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    for (auto iterator = infos.begin(); iterator != infos.end();) {
        if (m_Items.find(iterator->first) != m_Items.end()) {
            iterator = infos.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void PledgeItem::Builder::ReloadInfo(PledgeItem* const items) {
    if (items == nullptr) {
        return;
    }
    L2_GUARD(items->m_lock);
    for (auto& entry : items->m_Items) {
        const PledgeItemInfo* const info = PledgeItemDB::inst().Find(entry.first);
        if (info == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeItem.cpp",
            //     0x1c3)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeItem.cpp",
            //     0x1c3)
            L2_LOG_ERROR(
                Log,
                "Cannot refresh the definition of clan-shop item %d: no replacement definition exists; the existing "
                "reference is retained.",
                entry.first
            );
        } else {
            entry.second.pInfo = info;
        }
    }
}

namespace PledgeItemStuff {

void RequestLoadPledgeItem(CPledge* const pledge) {
    // Current L2Server 0x009afe2c.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x1e1,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x1e1,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(Log, "Cannot request clan-shop items from the database: the clan is unavailable.");
        return;
    }

    pledge->m_pItem->m_bLoadedFromDB = false;
    const PledgeCapture capture{pledge->Id(), pledge->GetWorldId(), static_cast<int>(pledge->GetDbId())};
    AtomicLambdaJobPacket* const job = MakeLambdaJobPacket([capture](AtomicLambdaJobPacket* const callback) {
        // Current L2Server 0x009b1c20.
        static constexpr wchar_t functionName[] = L"PledgeItem.cpp(498)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        const auto* const packet = static_cast<protocol::RP_EX_PLEDGE_ITEM_LOAD*>(callback->m_pPacket);
        if (packet == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeItem.cpp",
            //     500,
            //     L"(pPacket != NULL)")
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeItem.cpp",
            //     500,
            //     L"(pPacket != NULL)")
            L2_LOG_ERROR(
                Log,
                "Clan-shop item load returned no response for clan %u; this callback does not mark item loading complete.",
                static_cast<unsigned>(capture.m_dbid)
            );
            return;
        }

        CPledge* const owner = capture.GetPledge();
        if (owner == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeItem.cpp",
            //     0x1fc)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
            //     L"PledgeItem.cpp",
            //     0x1fc)
            L2_LOG_WARN(
                Log, "Clan %u is unavailable when its shop-item load response arrives; the response is discarded.",
                static_cast<unsigned>(capture.m_dbid)
            );
            return;
        }

        for (const protocol::PkPledgeItem& item : packet->vItems) {
            owner->m_pItem->AddItem(item.nItemClassID, item.nExpiryTime);
        }
        owner->m_pItem->m_bLoadedFromDB = true;
        if (owner->HasLoadedAllData()) {
            owner->LoadingCompleted();
        }
    });

    protocol::RQ_EX_PLEDGE_ITEM_LOAD request;
    request.nJobKey = CDB::PushAtomicJobHelper(job);
    request.nPledgeDBID = static_cast<int>(pledge->GetDbId());
    db.SendPacket(
        pledge->GetWorldId(), static_cast<RQ_PROTOCOL>(0xea), static_cast<RQ_EX_PROTOCOL>(RQ_EX_PLEDGE_ITEM_LOAD), request
    );
}

bool IsAddableItem(const int pledgeLevel, const PledgeMastery* const mastery, const PledgeItemInfo* const info) {
    L2_GUARD_FUNCTION();
    if (mastery == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x265,
        //     L"(pMastery != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x265,
        //     L"(pMastery != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot check clan-shop eligibility at clan level %d: the mastery state is unavailable.", pledgeLevel
        );
        return false;
    }
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x266,
        //     L"(pInfo != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x266,
        //     L"(pInfo != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot check clan-shop eligibility at clan level %d: the item definition is unavailable.", pledgeLevel
        );
        return false;
    }
    return info->Condition.CheckLevel(pledgeLevel) &&
        info->Condition.CheckMastery(mastery->GetPoint(info->Condition.mastery_id));
}

bool IsAddableItemByAuto(const int pledgeLevel, const PledgeMastery* const mastery, const PledgeItemInfo* const info) {
    L2_GUARD_FUNCTION();
    if (mastery == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x289,
        //     L"(pMastery != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x289,
        //     L"(pMastery != NULL)")
        L2_LOG_ERROR(
            Log,
            "Cannot check clan-shop automatic activation eligibility at clan level %d: the mastery state is unavailable.",
            pledgeLevel
        );
        return false;
    }
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x28a,
        //     L"(pInfo != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x28a,
        //     L"(pInfo != NULL)")
        L2_LOG_ERROR(
            Log,
            "Cannot check clan-shop automatic activation eligibility at clan level %d: the item definition is unavailable.",
            pledgeLevel
        );
        return false;
    }
    return IsAddableItem(pledgeLevel, mastery, info) && info->add_type == PledgeAddType::AUTO;
}

bool IsAddableItemByManual(const int pledgeLevel, const PledgeMastery* const mastery, const PledgeItemInfo* const info) {
    L2_GUARD_FUNCTION();
    if (mastery == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x277,
        //     L"(pMastery != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x277,
        //     L"(pMastery != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot check clan-shop manual activation eligibility at clan level %d: the mastery state is unavailable.",
            pledgeLevel
        );
        return false;
    }
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x278,
        //     L"(pInfo != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x278,
        //     L"(pInfo != NULL)")
        L2_LOG_ERROR(
            Log,
            "Cannot check clan-shop manual activation eligibility at clan level %d: the item definition is unavailable.",
            pledgeLevel
        );
        return false;
    }
    return IsAddableItem(pledgeLevel, mastery, info) && info->add_type == PledgeAddType::MANUAL;
}

void SelectAddableItemsByAuto(
    const int pledgeLevel, const PledgeMastery* const mastery, std::map<int, PledgeItemInfo*>& infos
) {
    L2_GUARD_FUNCTION();
    if (mastery == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x242,
        //     L"(pMastery != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x242,
        //     L"(pMastery != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot select clan-shop items for automatic activation at level %d: mastery state is unavailable.",
            pledgeLevel
        );
        return;
    }
    if (pledgeLevel < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeItem.cpp",
        //     0x246)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]NOT IMPLEMENTED!!!!!",
        //     L"PledgeItem.cpp",
        //     0x246)
        L2_LOG_ERROR(Log, "Cannot select clan-shop items for automatic activation: clan level %d is negative.", pledgeLevel);
        return;
    }

    for (PledgeItemInfo* const info : PledgeItemDB::inst().GetInfos()) {
        if (!IsAddableItemByAuto(pledgeLevel, mastery, info)) {
            continue;
        }
        const auto [iterator, inserted] = infos.emplace(info->class_id, info);
        if (!inserted) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR][PLEDGE][%s][%d]item class duplicated: %d",
            //     L"PledgeItem.cpp",
            //     599,
            //     info->class_id)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR][PLEDGE][%s][%d]item class duplicated: %d",
            //     L"PledgeItem.cpp",
            //     599,
            //     info->class_id)
            L2_LOG_WARN(
                Log,
                "Automatic clan-shop selection encountered duplicate item class %d; the existing candidate is retained.",
                info->class_id
            );
        }
    }
}

void AddPledgeItemAndSave(CPledge* const pledge, const int itemClassId, const int expiryTimeSeconds) {
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x35a,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x35a,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot add and save clan-shop item %d (lifetime %d seconds): the clan is unavailable.", itemClassId,
            expiryTimeSeconds
        );
        return;
    }

    const std::int32_t expiryTime = PledgeStuff::CalculateExpiryTime(expiryTimeSeconds);
    if (!pledge->GetItem()->AddItem(itemClassId, expiryTime)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Failed to additem: %d, %d, %d",
        //     L"PledgeItem.cpp",
        //     0x360,
        //     static_cast<int>(pledge->GetDbId()),
        //     itemClassId,
        //     expiryTimeSeconds)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Failed to additem: %d, %d, %d",
        //     L"PledgeItem.cpp",
        //     0x360,
        //     static_cast<int>(pledge->GetDbId()),
        //     itemClassId,
        //     expiryTimeSeconds)
        L2_LOG_ERROR(
            Log, "Cannot add shop item %d to clan %u (lifetime %d seconds); no save request is sent.", itemClassId,
            static_cast<unsigned>(pledge->GetDbId()), expiryTimeSeconds
        );
        return;
    }

    PledgeStuff::Write_LOG_PLEDGE_ITEM_ADD(pledge, itemClassId, expiryTimeSeconds);
    if (pledge->GetWorldId() != g_nWorldId) {
        return;
    }

    const int pledgeDatabaseId = static_cast<int>(pledge->GetDbId());
    protocol::RQ_EX_PLEDGE_ITEM_SAVE request;
    request.nPledgeDBID = pledgeDatabaseId;
    request.nItemClassID = itemClassId;
    request.nExpiryTime = expiryTime;
    AtomicLambdaJobPacket* const job = MakeLambdaJobPacket([request](AtomicLambdaJobPacket* const callback) {
        const auto* const response = static_cast<protocol::RP_EX_PLEDGE_ITEM_SAVE*>(callback->m_pPacket);
        if (response == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeItem.cpp",
            //     0x379,
            //     L"(pPacket != NULL)")
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeItem.cpp",
            //     0x379,
            //     L"(pPacket != NULL)")
            L2_LOG_ERROR(
                Log, "Save response is missing for clan %u shop item %d; the item was already added locally.",
                static_cast<unsigned>(request.nPledgeDBID), request.nItemClassID
            );
        } else if (!response->bSuccess) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Failed to save pledge item: %d, %d",
            //     L"PledgeItem.cpp",
            //     0x37f,
            //     request.nPledgeDBID,
            //     request.nItemClassID)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]Failed to save pledge item: %d, %d",
            //     L"PledgeItem.cpp",
            //     0x37f,
            //     request.nPledgeDBID,
            //     request.nItemClassID)
            L2_LOG_ERROR(
                Log, "Database save failed for clan %u shop item %d; the local item is retained.",
                static_cast<unsigned>(request.nPledgeDBID), request.nItemClassID
            );
        }
    });

    request.nJobKey = CDB::PushAtomicJobHelper(job);
    db.SendPacket(
        pledge->GetWorldId(), static_cast<RQ_PROTOCOL>(0xea), static_cast<RQ_EX_PROTOCOL>(RQ_EX_PLEDGE_ITEM_SAVE), request
    );
}

void AddPledgeItemByLevelAndMastery(CPledge* const pledge) {
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x29b,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x29b,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(Log, "Cannot update clan-shop items from level and mastery: the clan is unavailable.");
        return;
    }

    std::map<int, PledgeItemInfo*> infos;
    SelectAddableItemsByAuto(static_cast<int>(pledge->GetPledgeLevel()), pledge->GetMastery(), infos);
    pledge->GetItem()->FilterInfos(infos);
    for (const auto& entry : infos) {
        PledgeItemInfo* const info = entry.second;
        if (!pledge->GetItem()->HasItem(info->class_id)) {
            AddPledgeItemAndSave(pledge, info->class_id, info->expiry_time);
        }
    }
}

void SendPledgeItemListTo(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x430,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x430,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot send the clan-shop item list: the user is unavailable.");
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return;
    }

    pledge->GetItem()->RefillItemCount();
    CPacketHelperEx<20480> packet;
    const int count = Assemble_PledgeItemList(user, packet);

    user->GetSocket()->Send("chhb", 0xfe, 0x1e8, static_cast<std::int16_t>(count), packet.GetLength(), packet.GetPacket());
}

namespace Builder {

void SendPledgeItemListTo(User* const user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x456,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x456,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot display the clan-shop item list through the builder command: the user is unavailable.");
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return;
    }
    PledgeItem* const items = pledge->GetItem();
    items->RefillItemCount(runtime::crt::Time32Now());
    for (const PledgeItemInfo* const info : PledgeItemDB::inst().GetInfos()) {
        if (info == nullptr) {
            continue;
        }
        const bool addable = IsAddableItem(static_cast<int>(pledge->GetPledgeLevel()), pledge->GetMastery(), info);
        protocol::PkPledgeItemInfo item{};
        if (GetPledgeItemInfo(info, addable, items, item)) {
            PledgeStuff::SendSystemMessage(user, FormatPledgeItemBuilderMessage(info->class_id, item));
        }
    }
}

} // namespace Builder

void SendPledgeItemInfoTo(User* const user, const int itemClassId) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x44e,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x44e,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot send clan-shop item %d information: the user is unavailable.", itemClassId);
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return;
    }
    const PledgeItemInfo* const info = PledgeItemDB::inst().Find(itemClassId);
    if (info == nullptr) {
        return;
    }

    pledge->GetItem()->RefillItemCount();
    const bool addable = IsAddableItem(static_cast<int>(pledge->GetPledgeLevel()), pledge->GetMastery(), info);
    CPacketHelperEx<20480> packet;
    if (Assemble_PledgeItemInfo(packet, info, addable, pledge->GetItem())) {
        user->GetSocket()->Send("chb", 0xfe, 0x1ee, packet.GetLength(), packet.GetPacket());
    }
}

void RequestAddPledgeItem(User* const user, const int itemClassId) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x2c3,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x2c3,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(Log, "Cannot request clan-shop item %d activation: the user is unavailable.", itemClassId);
        return;
    }
    if (!user->HavePledgePower(PP_CONTROL_SYSTEM_FUNC)) {
        user->SendSystemMessage(SystemMessageId::NO_AUTHORITY_FOR_CLAN_DEVELOPMENT);
        Send_S_EX_PLEDGE_ITEM_ACTIVATE(user, 4);
        return;
    }
    if (user->m_data->m_nPledgeType == 5) {
        user->SendSystemMessage(SystemMessageId::CANNOT_USE_FUNCTION_WHILE_CASTING);
        Send_S_EX_PLEDGE_ITEM_ACTIVATE(user, 3);
        return;
    }

    const PledgeItemInfo* const info = PledgeItemDB::inst().Find(itemClassId);
    if (info == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]No pledge item info: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x2d9,
        //     static_cast<int>(user->GetDBID()),
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]No pledge item info: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x2d9,
        //     static_cast<int>(user->GetDBID()),
        //     itemClassId)
        L2_LOG_ERROR(
            Log, "Character %u cannot activate clan-shop item %d: no clan-item definition is registered.",
            static_cast<unsigned>(user->GetDBID()), itemClassId
        );
        user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
        Send_S_EX_PLEDGE_ITEM_ACTIVATE(user, 2);
        return;
    }
    CObject* const prototype = g_ObjectDB.GetObjectW(itemClassId, false);
    if (prototype == nullptr || !prototype->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]invalid item for User: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x2e4,
        //     static_cast<int>(user->GetDBID()),
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]invalid item for User: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x2e4,
        //     static_cast<int>(user->GetDBID()),
        //     itemClassId)
        L2_LOG_ERROR(
            Log, "Character %u cannot activate clan-shop item %d: its object prototype is missing or is not an item.",
            static_cast<unsigned>(user->GetDBID()), itemClassId
        );
        user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
        Send_S_EX_PLEDGE_ITEM_ACTIVATE(user, 2);
        return;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr || pledge->GetNameValue() < info->ActiveCost.name_value) {
        user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_FUNDS_FOR_ACTIVATION);
        Send_S_EX_PLEDGE_ITEM_ACTIVATE(user, static_cast<char>(0xfd));
        return;
    }

    JobTransactionLong jobType = 1;
    JobTransactionLock* const transaction = pledge->GetItem()->GetTransactionLock();
    if (!transaction->BeginJob(&jobType)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x2fd,
        //     pledge->GetDbId(),
        //     static_cast<int>(jobType))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]To set job is failed: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x2fd,
        //     pledge->GetDbId(),
        //     static_cast<int>(jobType))
        L2_LOG_WARN(
            Log, "Clan %u cannot start item %d activation: transaction %d is already active; the request is rejected.",
            static_cast<unsigned>(pledge->GetDbId()), itemClassId, static_cast<int>(jobType)
        );
        user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
        Send_S_EX_PLEDGE_ITEM_ACTIVATE(user, 2);
        return;
    }

    const auto endRejectedJob = [transaction]() {
        if (!transaction->EndJob()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeItem.cpp",
            //     0x2f9)
            // L2_LOG_ERROR(Log,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeItem.cpp",
            //     0x2f9)
            L2_LOG_ERROR(Log, "Rejected clan-shop activation found no active transaction during cleanup.");
        }
    };
    if (pledge->GetItem()->HasItem(itemClassId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Already has item: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x306,
        //     static_cast<int>(user->GetDBID()),
        //     itemClassId)
        // Noise: A repeated request to activate an already-owned clan-shop item is rejected by the existing client reply and
        // transaction cleanup, no operator error is needed. L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Already has item: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x306,
        //     static_cast<int>(user->GetDBID()),
        //     itemClassId)
        user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
        Send_S_EX_PLEDGE_ITEM_ACTIVATE(user, 2);
        endRejectedJob();
        return;
    }
    if (!IsAddableItemByManual(static_cast<int>(pledge->GetPledgeLevel()), pledge->GetMastery(), info)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Is not addable item: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x30f,
        //     static_cast<int>(user->GetDBID()),
        //     itemClassId)
        // Noise: Manual activation eligibility is a normal player restriction. The existing client reply and transaction
        // cleanup remain. L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Is not addable item: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x30f,
        //     static_cast<int>(user->GetDBID()),
        //     itemClassId)
        user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
        Send_S_EX_PLEDGE_ITEM_ACTIVATE(user, 2);
        endRejectedJob();
        return;
    }

    PledgeStuff::PledgeCostHelper helper(user, pledge);
    helper.SetCost(info->ActiveCost);
    PledgeStuff::Write_LOG_PLEDGE_ITEM_ACTIVATE(user, pledge, itemClassId, info->expiry_time, PledgeStuff::ActionStep::Try);
    const UserCapture userCapture{user->Id(), user->m_data->GetWorldId(), static_cast<int>(user->GetDBID())};
    const PledgeCapture pledgeCapture{pledge->Id(), pledge->GetWorldId(), static_cast<int>(pledge->GetDbId())};
    helper.Do(MakeLambdaJob([userCapture, pledgeCapture, info](const int result) {
        // Current L2Server 0x009b20ac.
        static constexpr wchar_t functionName[] = L"PledgeItem.cpp(812)";
        L2_GUARD_PROFILE(functionName);
        GuardHelper stackGuard(functionName, CThreadManager::GetThreadID());
        User* const requester = userCapture.GetUser();
        Send_S_EX_PLEDGE_ITEM_ACTIVATE(requester, result);
        if (result != 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Failed to add item: %d, %d",
            //     L"PledgeItem.cpp",
            //     0x334,
            //     pledgeCapture.m_dbid,
            //     result)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]Failed to add item: %d, %d",
            //     L"PledgeItem.cpp",
            //     0x334,
            //     pledgeCapture.m_dbid,
            //     result)
            L2_LOG_ERROR(
                Log, "Clan %u item activation payment returned result %d; activation stops before transaction cleanup.",
                static_cast<unsigned>(pledgeCapture.m_dbid), result
            );
            return;
        }

        CPledge* const owner = pledgeCapture.GetPledge();
        if (owner == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]No pledge to add item: %d",
            //     L"PledgeItem.cpp",
            //     0x33c,
            //     pledgeCapture.m_dbid)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]No pledge to add item: %d",
            //     L"PledgeItem.cpp",
            //     0x33c,
            //     pledgeCapture.m_dbid)
            L2_LOG_ERROR(
                Log,
                "Clan %u is unavailable after item activation payment completed; item addition and transaction cleanup are "
                "not reached.",
                static_cast<unsigned>(pledgeCapture.m_dbid)
            );
            return;
        }
        CSPointer<CPledge> hold(owner, ObjectRefType::FunctionChain);
        PledgeItem* const pledgeItem = owner->GetItem();
        PledgeStuff::SendSystemMessage_Item(owner, SystemMessageId::ITEM_UNLOCKED_IN_CLAN_SHOP, info->class_id);
        AddPledgeItemAndSave(owner, info->class_id, info->expiry_time);
        PledgeStuff::Write_LOG_PLEDGE_ITEM_ACTIVATE(
            requester, owner, info->class_id, info->expiry_time, PledgeStuff::ActionStep::Success
        );
        if (!pledgeItem->GetTransactionLock()->EndJob()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeItem.cpp",
            //     0x340)
            // L2_LOG_ERROR(Log,
            //     L"Failed to end job at %s(%d)",
            //     L"PledgeItem.cpp",
            //     0x340)
            L2_LOG_ERROR(
                Log,
                "Clan-shop activation callback found no active transaction during cleanup; item-addition and save-request "
                "handling already ran."
            );
        }
    }));
}

bool RequestBuyPledgeItem(User* const user, const int itemClassId, const std::int64_t amount) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x509,
        //     L"(pUser != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x509,
        //     L"(pUser != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot request clan-shop item %d purchase (quantity %lld): the user is unavailable.", itemClassId,
            static_cast<long long>(amount)
        );
        return false;
    }
    if (!user->HavePledgePower(PP_MANAGE_MASTER)) {
        user->SendSystemMessage(SystemMessageId::NO_AUTHORITY_FOR_CLAN_DEVELOPMENT);
        Send_S_EX_PLEDGE_ITEM_BUY(user, 2);
        return false;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        user->SendSystemMessage(SystemMessageId::NO_AUTHORITY_FOR_CLAN_DEVELOPMENT);
        Send_S_EX_PLEDGE_ITEM_BUY(user, 2);
        return false;
    }

    user->GetSocket();
    if (CItemRestrict::Inst().CheckItemRestrict(itemClassId)) {
        user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
        Send_S_EX_PLEDGE_ITEM_BUY(user, 1);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]CheckItemRestrict error: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x524,
        //     static_cast<int>(user->GetDBID()),
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]CheckItemRestrict error: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x524,
        //     static_cast<int>(user->GetDBID()),
        //     itemClassId)
        L2_LOG_WARN(
            Log, "Character %u cannot buy clan-shop item %d: the configured item restriction rejects it.",
            static_cast<unsigned>(user->GetDBID()), itemClassId
        );
        return false;
    }
    const PledgeItemInfo* const info = pledge->GetItem()->FindInfo(itemClassId);
    if (info == nullptr) {
        user->SendSystemMessage(SystemMessageId::CLAN_DEVELOPMENT_SYSTEM_ERROR);
        Send_S_EX_PLEDGE_ITEM_BUY(user, 1);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]FindInfo error: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x52f,
        //     static_cast<int>(user->GetDBID()),
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]FindInfo error: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x52f,
        //     static_cast<int>(user->GetDBID()),
        //     itemClassId)
        L2_LOG_WARN(
            Log, "Character %u cannot buy clan-shop item %d: no definition is available in the clan shop.",
            static_cast<unsigned>(user->GetDBID()), itemClassId
        );
        return false;
    }
    if (!CheckCostPoint(user, info, amount)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]CheckCostPoint error: %d, %d, %d",
        //     L"PledgeItem.cpp",
        //     0x539,
        //     static_cast<int>(user->GetDBID()),
        //     itemClassId,
        //     messageId)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]CheckCostPoint error: %d, %d, %d",
        //     L"PledgeItem.cpp",
        //     0x539,
        //     static_cast<int>(user->GetDBID()),
        //     itemClassId,
        //     messageId)
        L2_LOG_WARN(
            Log,
            "Clan-shop point-charge check rejected character %u buying item %d (quantity %lld, point type %d, unit cost "
            "%lld); the purchase is rejected.",
            static_cast<unsigned>(user->GetDBID()), itemClassId, static_cast<long long>(amount),
            static_cast<int>(info->SellCost.point_type), static_cast<long long>(info->SellCost.point_amount)
        );
        user->SendSystemMessage(SystemMessageId::CANNOT_BUY_NOT_ENOUGH_FUNDS_OR_INVENTORY_LIMIT_EXCEEDED);
        Send_S_EX_PLEDGE_ITEM_BUY(user, 3);
        return false;
    }
    const int count = static_cast<int>(amount);
    if (!pledge->GetItem()->DecreaseItemCount(itemClassId, count, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]DecreaseItemCount error: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x542,
        //     itemClassId,
        //     static_cast<long long>(amount))
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]DecreaseItemCount error: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x542,
        //     itemClassId,
        //     static_cast<long long>(amount))
        L2_LOG_WARN(
            Log,
            "Clan-shop stock check rejected item %d purchase: requested quantity %lld, stock-counter quantity %d; the entry "
            "is missing or has insufficient stock.",
            itemClassId, static_cast<long long>(amount), count
        );
        user->SendSystemMessage(SystemMessageId::CANNOT_BUY_ITEM_OUT_OF_STOCK);
        Send_S_EX_PLEDGE_ITEM_BUY(user, 4);
        return false;
    }

    PledgeStuff::Write_LOG_PLEDGE_ITEM_BUY(user, pledge, itemClassId, PledgeStuff::ActionStep::Try);
    const UserCapture userCapture{user->Id(), user->m_data->GetWorldId(), static_cast<int>(user->GetDBID())};
    const PledgeCapture pledgeCapture{pledge->Id(), pledge->GetWorldId(), static_cast<int>(pledge->GetDbId())};
    AtomicLambdaJob* const job = MakeLambdaJob([userCapture, pledgeCapture, itemClassId, amount](const int result) {
        // Current L2Server 0x009b1f50.
        User* const requester = userCapture.GetUser();
        Send_S_EX_PLEDGE_ITEM_BUY(requester, result);
        if (result == 0) {
            if (requester != nullptr) {
                const char16_t* const requesterName = requester->GetName();
                CPledge* const notificationOwner = pledgeCapture.GetPledge();
                PledgeStuff::SendSystemMessage_UsernameAndItem(
                    notificationOwner, SystemMessageId::CHARACTER_BOUGHT_ITEM_FROM_CLAN_SHOP, requesterName, itemClassId
                );
            }
            CPledge* const auditOwner = pledgeCapture.GetPledge();
            PledgeStuff::Write_LOG_PLEDGE_ITEM_BUY(requester, auditOwner, itemClassId, PledgeStuff::ActionStep::Success);
            return;
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]Failed to buy item: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x556,
        //     pledgeCapture.m_dbid,
        //     result)
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]Failed to buy item: %d, %d",
        //     L"PledgeItem.cpp",
        //     0x556,
        //     pledgeCapture.m_dbid,
        //     result)
        L2_LOG_ERROR(
            Log,
            "Clan %u item %d purchase (quantity %lld) returned result %d; stock restoration will be attempted if the clan "
            "is still available.",
            static_cast<unsigned>(pledgeCapture.m_dbid), itemClassId, static_cast<long long>(amount), result
        );
        CPledge* const owner = pledgeCapture.GetPledge();
        if (owner != nullptr) {
            owner->GetItem()->IncreaseItemCount(itemClassId, static_cast<int>(amount));
        }
    });
    PledgeItemBuying(user, itemClassId, amount).Do(job);
    return true;
}

void RequestRemovePledgeItem(CPledge* const pledge, const int itemClassId) {
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x498,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x498,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(Log, "Cannot request database removal of clan-shop item %d: the clan is unavailable.", itemClassId);
        return;
    }

    const int pledgeDatabaseId = static_cast<int>(pledge->GetDbId());
    protocol::RQ_EX_PLEDGE_ITEM_DELETE request;
    request.nPledgeDBID = pledgeDatabaseId;
    request.nItemClassID = itemClassId;
    AtomicLambdaJobPacket* const job = MakeLambdaJobPacket([request](AtomicLambdaJobPacket* const callback) {
        const auto* const response = static_cast<protocol::RP_EX_PLEDGE_ITEM_DELETE*>(callback->m_pPacket);
        if (response == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeItem.cpp",
            //     0x4aa,
            //     L"(pPacket != NULL)")
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]assert falied: %s",
            //     L"PledgeItem.cpp",
            //     0x4aa,
            //     L"(pPacket != NULL)")
            L2_LOG_ERROR(
                Log, "Database removal response is missing for clan %u shop item %d.",
                static_cast<unsigned>(request.nPledgeDBID), request.nItemClassID
            );
        } else if (!response->bSuccess) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[PLEDGE][%s][%d]Failed to delete pledge item: %d, %d",
            //     L"PledgeItem.cpp",
            //     0x4b0,
            //     request.nPledgeDBID,
            //     request.nItemClassID)
            // L2_LOG_ERROR(Log,
            //     L"[PLEDGE][%s][%d]Failed to delete pledge item: %d, %d",
            //     L"PledgeItem.cpp",
            //     0x4b0,
            //     request.nPledgeDBID,
            //     request.nItemClassID)
            L2_LOG_ERROR(
                Log, "Database removal failed for clan %u shop item %d.", static_cast<unsigned>(request.nPledgeDBID),
                request.nItemClassID
            );
        }
    });

    request.nJobKey = CDB::PushAtomicJobHelper(job);
    db.SendPacket(
        pledge->GetWorldId(), static_cast<RQ_PROTOCOL>(0xea), static_cast<RQ_EX_PROTOCOL>(RQ_EX_PLEDGE_ITEM_DELETE), request
    );
}

void RequestRemovePledgeItems(CPledge* const pledge, const bool masteryOnly) {
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x475,
        //     L"(pPledge != NULL)")
        // L2_LOG_ERROR(Log,
        //     L"[PLEDGE][%s][%d]assert falied: %s",
        //     L"PledgeItem.cpp",
        //     0x475,
        //     L"(pPledge != NULL)")
        L2_LOG_ERROR(
            Log, "Cannot remove %s clan-shop items: the clan is unavailable.", masteryOnly ? "mastery-dependent" : "all"
        );
        return;
    }
    std::vector<int> itemIds;
    pledge->GetItem()->SelectItemIDs(itemIds, masteryOnly);
    for (const int itemClassId : itemIds) {
        if (pledge->GetItem()->RemoveItem(itemClassId)) {
            PledgeStuff::Write_LOG_PLEDGE_ITEM_REMOVE(pledge, itemClassId);
            if (pledge->GetWorldId() == g_nWorldId) {
                RequestRemovePledgeItem(pledge, itemClassId);
            }
        }
    }
}

} // namespace PledgeItemStuff
