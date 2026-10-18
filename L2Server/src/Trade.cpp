#include "Trade.h"
#include "DB.h"
#include "Inventory.h"
#include "Item.h"
#include "ItemAssembler.h"
#include "Pet.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "User.h"
#include "UserSocket.h"

#include "Portable/ClientProtocolRuntime.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/platform/Runtime.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

namespace {

constexpr const char* kTradeSource = "Trade.cpp";



} // namespace

CTradeManager g_TradeManager;

CTrade::CTrade(User* first, User* second) :
    m_nTrader{}, m_TradeList{}, m_bIsTradeDone{}, m_bEnd(false), m_bIsStart(false), m_dwTradeBegin(0) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_lock);
        m_nTrader[0] = static_cast<std::int32_t>(first->Id());
        m_nTrader[1] = static_cast<std::int32_t>(second->Id());
    }

    m_dwTradeBegin = runtime::GetTickCount();
    m_dwShortTimeStamp = runtime::GetTickCount();
    SetShortTimeStamp(&m_dwShortTimeStamp, 0);
}

CTrade::~CTrade() { g_ServerContext.Trade.fetch_sub(1, std::memory_order_seq_cst); }

L2_DEFINE_OBJECT_CLASS_ID(CTrade)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CTrade)

L2_DEFINE_OBJECT_ALLOCATOR(CTrade, __GetClassName())

L2_DEFINE_OBJECT_NEW(CTrade)

L2_DEFINE_OBJECT_RELEASE(CTrade)

void CTrade::OnCreate() {
    L2_GUARD_FUNCTION();
    g_ServerContext.Trade.fetch_add(1, std::memory_order_seq_cst);
}

void CTrade::Delete() {
    L2_GUARD_FUNCTION();
    static_cast<void>(new CTradeReleaseVerifier(this));
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

int CTrade::AddItems(
    const unsigned char* packet,
    const unsigned int traderNumber,
    CPackageItemPacketManager& traderPacketManager,
    CPackageItemPacketManager& otherPacketManager
) {
    // Current L2Server 0x00af2634.
    L2_GUARD_FUNCTION();
    if (traderNumber >= 2U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]trader index is invalid. [%d]",
        //     kTradeSource,
        //     0x36d,
        //     traderNumber)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]trader index is invalid. [%d]",
        //     kTradeSource,
        //     0x36d,
        //     traderNumber)
        L2_LOG_ERROR(Log, "Cannot add trade items: participant index %u is outside 0..1.", traderNumber);
        return 0;
    }

    CSPointer<User> trader = g_UserMap.FindObject(m_nTrader[traderNumber]);
    if (!trader) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x374)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x374)
        L2_LOG_WARN(
            Log, "Cannot continue adding items: participant object ID %d is unavailable.",
            static_cast<int>(m_nTrader[traderNumber])
        );
        return 0;
    }

    traderPacketManager.Init();
    otherPacketManager.Init();
    unsigned int itemAmount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &itemAmount);
    if (itemAmount >= 0xc9U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[CTrade::AddItems] Error!! user[%s]'s itemAmount [%d] > 200 ",
        //     trader->GetName(),
        //     itemAmount)
        // L2_LOG_ERROR(Log,
        //     L"[CTrade::AddItems] Error!! user[%s]'s itemAmount [%d] > 200 ",
        //     trader->GetName(),
        //     itemAmount)
        // L2_LOG_WARN(Log,
        //     "Trade item addition rejected for player %s: %u entries exceed the limit of 200.",
        //     WideToUtf8(trader->GetName()).c_str(),
        //     itemAmount)
        L2_LOG_WARN(
            Log, "Trade item addition rejected for player %s: %u entries exceed the limit of 200.",
            Utf16ToUtf8(trader->GetName()).c_str(), itemAmount
        );
        return 0;
    }

    int addedItems = 0;
    for (unsigned int itemNumber = 0; itemNumber < itemAmount; ++itemNumber) {
        int itemServerId = 0;
        std::int64_t amount = 0;
        cursor = ::Disassemble(cursor, "dQ", &itemServerId, &amount);

        CItem* const item = trader->m_inv.GetByServerID(itemServerId);
        if (item == nullptr) {
            return 0;
        }

        bool validAmount = false;
        if (item->m_data->m_ItemConsumeType == ICT_CHARGE) {
            validAmount = static_cast<std::uint64_t>(amount - 1) < 999999999999ULL;
        } else if (static_cast<std::uint64_t>(amount - 1) < 1000000ULL) {
            char decimal[32]{};
            std::snprintf(decimal, sizeof(decimal), "%lld", static_cast<long long>(amount));
            validAmount = std::strlen(decimal) < 9;
        }
        if (!validAmount || !item->IsTradable(trader.get(), false)) {
            continue;
        }

        enum class ItemTradeStep { Rejected, Cancel, RemoveWhole, Ready };
        CItem* combinedItem;
        int originalItemIndex;
        InventoryUpdateType otherUpdateType;
        CItem* tradedItem;
        const ItemTradeStep step = [&] {
            L2_GUARD(*item->m_lock);
            combinedItem = nullptr;
            if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
                for (const int tradeItemIndex : m_TradeList[traderNumber]) {
                    CSPointer<CItem> tradeItem = CItemFactory::Inst().FindObjectSP(tradeItemIndex);
                    if (tradeItem && tradeItem->m_data->m_nClassID == item->m_data->m_nClassID) {
                        combinedItem = tradeItem.get();
                        break;
                    }
                }
            }

            originalItemIndex = item->m_data->index;
            otherUpdateType = IUT_DELETE;
            tradedItem = nullptr;
            const std::int64_t originalAmount = item->m_data->m_nCount;
            if (originalAmount == amount) {
                if (SameDBItemExistInTradeList(item, traderNumber)) {
                    return ItemTradeStep::Cancel;
                }
                return ItemTradeStep::RemoveWhole;
            }
            if (originalAmount <= amount) {
                return ItemTradeStep::Rejected;
            }

            if (combinedItem == nullptr) {
                tradedItem = static_cast<CItem*>(item->Copy());
                if (!tradedItem->SetAmount(amount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     kTradeSource,
                    //     0x3fd,
                    //     tradedItem->m_data->m_nCount,
                    //     amount)
                    // L2_LOG_ERROR(Log,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     kTradeSource,
                    //     0x3fd,
                    //     tradedItem->m_data->m_nCount,
                    //     amount)
                    L2_LOG_ERROR(
                        Log,
                        "Setting split trade stack amount failed: stack count %lld, requested amount %lld; processing "
                        "continues.",
                        static_cast<long long>(tradedItem->m_data->m_nCount), static_cast<long long>(amount)
                    );
                }
                if (tradedItem->GetDBID() != item->GetDBID()) {
                    tradedItem->SetDBID(item->GetDBID());
                }
                m_TradeList[traderNumber].push_back(tradedItem->m_data->index);
            } else {
                tradedItem = combinedItem;
                if (!combinedItem->AddAmount(amount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     kTradeSource,
                    //     0x3ef,
                    //     combinedItem->m_data->m_nCount,
                    //     amount)
                    // L2_LOG_ERROR(Log,
                    //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                    //     kTradeSource,
                    //     0x3ef,
                    //     combinedItem->m_data->m_nCount,
                    //     amount)
                    L2_LOG_ERROR(
                        Log, "Split-stack trade merge failed: stack count %lld, requested delta %lld; processing continues.",
                        static_cast<long long>(combinedItem->m_data->m_nCount), static_cast<long long>(amount)
                    );
                }
            }

            if (!item->AddAmount(-amount)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     kTradeSource,
                //     0x40a,
                //     item->m_data->m_nCount,
                //     -amount)
                // L2_LOG_ERROR(Log,
                //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                //     kTradeSource,
                //     0x40a,
                //     item->m_data->m_nCount,
                //     -amount)
                L2_LOG_ERROR(
                    Log,
                    "Reducing source inventory stack for trade failed: stack count %lld, requested delta %lld; processing "
                    "continues.",
                    static_cast<long long>(item->m_data->m_nCount), static_cast<long long>(-amount)
                );
            }
            trader->m_inv.SetInventoryChanged(item, IUT_UPDATE);
            otherUpdateType = IUT_DELETE;
            return ItemTradeStep::Ready;
        }();
        if (step == ItemTradeStep::Cancel) {
            Canceled(trader.get(), false);
            return 0;
        }
        if (step == ItemTradeStep::Rejected) {
            return 0;
        }
        if (step == ItemTradeStep::RemoveWhole) {
            const bool removed = trader->m_inv.Pop(originalItemIndex, false);
            const bool moved = [&] {
                L2_GUARD(*item->m_lock);
                if (!removed) {
                    return false;
                }
                if (combinedItem == nullptr) {
                    tradedItem = item;
                    m_TradeList[traderNumber].push_back(item->m_data->index);
                } else {
                    tradedItem = combinedItem;
                    if (!combinedItem->AddAmount(amount)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                        //     kTradeSource,
                        //     0x3d1,
                        //     combinedItem->m_data->m_nCount,
                        //     amount)
                        // L2_LOG_ERROR(Log,
                        //     L"[%S][%d] item amount is invalid[%I64d][%I64d]",
                        //     kTradeSource,
                        //     0x3d1,
                        //     combinedItem->m_data->m_nCount,
                        //     amount)
                        L2_LOG_ERROR(
                            Log,
                            "Whole-stack trade merge failed after inventory removal: stack count %lld, requested delta "
                            "%lld; processing continues.",
                            static_cast<long long>(combinedItem->m_data->m_nCount), static_cast<long long>(amount)
                        );
                    }
                }
                trader->m_inv.SetInventoryChanged(item, IUT_DELETE);
                otherUpdateType = IUT_UPDATE;
                return true;
            }();
            if (!moved) {
                return 0;
            }
        }

        traderPacketManager.CheckLimitPackageItemPacketDataSize();
        CItemAssembler tradedAssembler(*trader.get(), *tradedItem, false);
        int assembled = traderPacketManager.PackageAssembleItem(&tradedAssembler);
        if (assembled < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Assemble Error at file[%s], line[%d], assemble result = [%d], total length = [%d]",
            //     kTradeSourceWide,
            //     0x42b,
            //     assembled,
            //     traderPacketManager.GetTotalPackageItemLength())
            // L2_LOG_ERROR(Log,
            //     L"Assemble Error at file[%s], line[%d], assemble result = [%d], total length = [%d]",
            //     kTradeSourceWide,
            //     0x42b,
            //     assembled,
            //     traderPacketManager.GetTotalPackageItemLength())
            L2_LOG_ERROR(
                Log, "Cannot assemble trade traded-item data: result %d, package length %d; item state has already changed.",
                assembled, traderPacketManager.GetTotalPackageItemLength()
            );
            return 0;
        }
        traderPacketManager.SetAssembleItemLengthPerItem(assembled);
        traderPacketManager.IncreasePackageItemCount();

        otherPacketManager.CheckLimitPackageItemPacketDataSize();
        assembled = otherPacketManager.PackageAssemble("h", static_cast<int>(static_cast<std::int16_t>(otherUpdateType)));
        CItemAssembler originalAssembler(*trader.get(), *item, false);
        assembled += otherPacketManager.PackageAssembleItem(&originalAssembler);
        if (assembled < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Assemble Error at file[%s], line[%d], assemble result = [%d], total length = [%d]",
            //     kTradeSourceWide,
            //     0x442,
            //     assembled,
            //     otherPacketManager.GetTotalPackageItemLength())
            // L2_LOG_ERROR(Log,
            //     L"Assemble Error at file[%s], line[%d], assemble result = [%d], total length = [%d]",
            //     kTradeSourceWide,
            //     0x442,
            //     assembled,
            //     otherPacketManager.GetTotalPackageItemLength())
            L2_LOG_ERROR(
                Log,
                "Cannot assemble trade inventory-update data: result %d, package length %d; item state has already changed.",
                assembled, otherPacketManager.GetTotalPackageItemLength()
            );
            return 0;
        }
        otherPacketManager.SetAssembleItemLengthPerItem(assembled);
        otherPacketManager.IncreasePackageItemCount();
        ++addedItems;
    }
    return addedItems;
}

bool CTrade::CheckItemCount(const unsigned char* packet, const int traderNumber) {
    // Current L2Server 0x00af19c4.
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(traderNumber) >= 2U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] nTraderNum[%d] is invalid value",
        //     kTradeSource,
        //     0x694,
        //     traderNumber)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] nTraderNum[%d] is invalid value",
        //     kTradeSource,
        //     0x694,
        //     traderNumber)
        L2_LOG_ERROR(Log, "Cannot check trade inventory capacity: participant index %d is outside 0..1.", traderNumber);
        return false;
    }

    CSPointer<User> first = g_UserMap.FindObject(m_nTrader[0]);
    if (!first) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x69b)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x69b)
        L2_LOG_WARN(
            Log, "Cannot continue inventory capacity check: participant object ID %d is unavailable.",
            static_cast<int>(m_nTrader[0])
        );
        return false;
    }
    CSPointer<User> second = g_UserMap.FindObject(m_nTrader[1]);
    if (!second) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x6a2)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x6a2)
        L2_LOG_WARN(
            Log, "Cannot continue inventory capacity check: participant object ID %d is unavailable.",
            static_cast<int>(m_nTrader[1])
        );
        return false;
    }

    int itemAmount = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &itemAmount);
    if (itemAmount >= 0xc9) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Error!! Try trade too many item.")
        // L2_LOG_ERROR(Log,
        //     L"Error!! Try trade too many item.")
        L2_LOG_WARN(Log, "Trade inventory capacity check rejected: %d entries exceed the limit of 200.", itemAmount);
        return false;
    }

    User* recipient = first.get();
    User* sender = second.get();
    if (traderNumber == 0) {
        recipient = second.get();
        sender = first.get();
    }

    int normalItemCount = 0;
    int artifactItemCount = 0;
    for (int index = 0; index < itemAmount; ++index) {
        int itemServerId = 0;
        std::int64_t amount = 0;
        cursor = ::Disassemble(cursor, "dQ", &itemServerId, &amount);

        CItem* const item = ItemSmartIdManager()->GetObjectW(static_cast<std::uint32_t>(itemServerId));
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]cannot find item user0[%s] user1[%s]",
            //     kTradeSource,
            //     0x6c6,
            //     sender->GetName(),
            //     recipient->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d]cannot find item user0[%s] user1[%s]",
            //     kTradeSource,
            //     0x6c6,
            //     sender->GetName(),
            //     recipient->GetName())
            // L2_LOG_WARN(Log,
            //     "Cannot check trade inventory capacity: item object ID %d is unavailable. Sender %s, recipient %s.",
            //     itemServerId,
            //     WideToUtf8(sender->GetName()).c_str(),
            //     WideToUtf8(recipient->GetName()).c_str())
            L2_LOG_WARN(
                Log, "Cannot check trade inventory capacity: item object ID %d is unavailable; sender %s, recipient %s.",
                itemServerId, Utf16ToUtf8(sender->GetName()).c_str(), Utf16ToUtf8(recipient->GetName()).c_str()
            );
            return false;
        }
        if (!item->IsTradable(sender, false)) {
            continue;
        }

        bool countSlot = true;
        if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
            for (const int tradeItemIndex : m_TradeList[static_cast<std::size_t>(traderNumber)]) {
                CSPointer<CItem> tradeItem = CItemFactory::Inst().FindObjectSP(tradeItemIndex);
                if (tradeItem && tradeItem->m_data->m_nClassID == item->m_data->m_nClassID) {
                    countSlot = item->m_data->m_nCount == amount;
                    break;
                }
            }
        }
        if (!countSlot) {
            continue;
        }
        if (item->IsArtifact()) {
            ++artifactItemCount;
        } else {
            ++normalItemCount;
        }
    }

    const bool normalFits = normalItemCount + recipient->m_data->m_nNumInventory <= recipient->GetInventoryLimit();
    const bool artifactFits =
        artifactItemCount + recipient->m_data->m_nCurrentArtifactInventoryNum <= recipient->GetArtifactInventoryLimit();
    if (normalFits && artifactFits) {
        return true;
    }

    if (normalFits) {
        recipient->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ARTIFACT_INVENTORY_SPACE);
    } else if (!artifactFits) {
        recipient->SendSystemMessage(SystemMessageId::NOT_ENOUGH_NORMAL_AND_ARTIFACT_INVENTORY_SPACE);
    } else {
        recipient->SendSystemMessage(SystemMessageId::NOT_ENOUGH_INVENTORY_SPACE);
    }
    sender->SendSystemMessage(SystemMessageId::TARGET_INVENTORY_FULL);
    return false;
}

void CTrade::Done() {
    // Current L2Server 0x00af162c.
    L2_GUARD_FUNCTION();
    CSPointer<User> first = g_UserMap.FindObject(m_nTrader[0]);
    if (!first) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x45f)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x45f)
        L2_LOG_WARN(
            Log, "Cannot continue completion notification: participant object ID %d is unavailable.",
            static_cast<int>(m_nTrader[0])
        );
        return;
    }
    CSPointer<User> second = g_UserMap.FindObject(m_nTrader[1]);
    if (!second) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x466)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x466)
        L2_LOG_WARN(
            Log, "Cannot continue completion notification: participant object ID %d is unavailable.",
            static_cast<int>(m_nTrader[1])
        );
        return;
    }

    {
        L2_GUARD(m_lock);
        first->SendSystemMessage(SystemMessageId::TRADE_SUCCEEDED);
        first->InitTrade();
        second->SendSystemMessage(SystemMessageId::TRADE_SUCCEEDED);
        second->InitTrade();
    }
    first->SendItemList(true, true, nullptr);
    second->SendItemList(true, true, nullptr);
}

int CTrade::SendTradableList(User* const user) {
    // Current L2Server 0x00af31ac.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return -1;
    }

    CCreature* const firstCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nTrader[0]));
    User* const first = firstCreature != nullptr && firstCreature->IsPC() ? firstCreature->CastUser() : nullptr;
    if (first == nullptr) {
        // Log.Add(LOG_ERROR, L"Cannot find trade user1")
        // L2_LOG_ERROR(Log, L"Cannot find trade user1")
        L2_LOG_WARN(
            Log, "Cannot send tradable item list: participant object ID %d is unavailable or is not a player.",
            static_cast<int>(m_nTrader[0])
        );
        return -1;
    }
    CCreature* const secondCreature = g_CreatureSmartIdManager.GetObjectW(static_cast<std::uint32_t>(m_nTrader[1]));
    User* const second = secondCreature != nullptr && secondCreature->IsPC() ? secondCreature->CastUser() : nullptr;
    if (second == nullptr) {
        // Log.Add(LOG_ERROR, L"Cannot find trade user2")
        // L2_LOG_ERROR(Log, L"Cannot find trade user2")
        L2_LOG_WARN(
            Log, "Cannot send tradable item list: participant object ID %d is unavailable or is not a player.",
            static_cast<int>(m_nTrader[1])
        );
        return -1;
    }

    CPackageItemPacketManager packetManager;
    int tradableItemCount = 0;
    int depth = 0;
    CSPointer<CItem> failedItem;
    const bool listed = [&] {
        L2_READ_GUARD(user->m_inv);
        CSharedItemData* itemData = user->m_inv.GetFirst();
        while (itemData != nullptr) {
            CSPointer<CItem> itemObject = CItemFactory::Inst().FindObjectSP(itemData->index);
            if (!itemObject || !itemObject->IsItem()) {
                failedItem = std::move(itemObject);
                return false;
            }
            CItem* const item = itemObject->CastItem();

            if (item->IsTradable(user, false)) {
                ++tradableItemCount;
                std::optional<CItemAssembler> assemblerStorage;
                const int assembled = [&] {
                    L2_GUARD(*item->m_lock);
                    packetManager.CheckLimitPackageItemPacketDataSize();
                    CItemAssembler& assembler = assemblerStorage.emplace(*user, *item, false);
                    const int result = packetManager.PackageAssembleItem(&assembler);
                    if (result >= 1) {
                        packetManager.SetAssembleItemLengthPerItem(result);
                        packetManager.IncreasePackageItemCount();
                    }
                    return result;
                }();
                if (assembled < 1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"Assemble Error at file[%s], line[%d], assemble result = [%d], total length = [%d]",
                    //     kTradeSourceWide,
                    //     0xab,
                    //     assembled,
                    //     packetManager.GetTotalPackageItemLength())
                    // L2_LOG_ERROR(Log,
                    //     L"Assemble Error at file[%s], line[%d], assemble result = [%d], total length = [%d]",
                    //     kTradeSourceWide,
                    //     0xab,
                    //     assembled,
                    //     packetManager.GetTotalPackageItemLength())
                    L2_LOG_ERROR(
                        Log,
                        "Cannot assemble tradable item list entry: result %d, package length %d; continuing with the next "
                        "inventory entry.",
                        assembled, packetManager.GetTotalPackageItemLength()
                    );
                } else {
                    itemData = itemData->Next();
                    ++depth;
                    if (depth > 0x192) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"depth > %d at file[%s], line[%d]",
                        //     0x192,
                        //     kTradeSourceWide,
                        //     0xbb)
                        // L2_LOG_ERROR(Log,
                        //     L"depth > %d at file[%s], line[%d]",
                        //     0x192,
                        //     kTradeSourceWide,
                        //     0xbb)
                        L2_LOG_ERROR(
                            Log, "Tradable item list traversal stopped after %d packaged entries; sending the partial list.",
                            depth
                        );
                        break;
                    }
                    continue;
                }
            }

            itemData = itemData->Next();
        }
        return true;
    }();
    if (!listed) {
        return -1;
    }

    if (static_cast<int>(user->Id()) == m_nTrader[0]) {
        const int relation = second->ComputeChattingRelation(second, first);
        if ((relation & 0x10) == 0) {
            first->GetSocket()->Send(
                "ccdccd", S_TRADE_START, 1, static_cast<int>(second->Id()), relation, second->Level(),
                packetManager.GetTotalPackageItemCount()
            );
        } else {
            first->GetSocket()->Send(
                "ccdcd", S_TRADE_START, 1, static_cast<int>(second->Id()), relation, packetManager.GetTotalPackageItemCount()
            );
        }
        packetManager.SendPacket_PackageItemData(first->GetSocket(), S_TRADE_START, PackageItemPacket::PIPT_DEFAULT);
    } else {
        const int relation = first->ComputeChattingRelation(first, second);
        if ((relation & 0x10) == 0) {
            second->GetSocket()->Send(
                "ccdccd", S_TRADE_START, 1, static_cast<int>(first->Id()), relation, first->Level(),
                packetManager.GetTotalPackageItemCount()
            );
        } else {
            second->GetSocket()->Send(
                "ccdcd", S_TRADE_START, 1, static_cast<int>(first->Id()), relation, packetManager.GetTotalPackageItemCount()
            );
        }
        packetManager.SendPacket_PackageItemData(second->GetSocket(), S_TRADE_START, PackageItemPacket::PIPT_DEFAULT);
    }
    SetShortTimeStamp(&m_dwShortTimeStamp, 0);
    return tradableItemCount;
}

bool CTrade::Request() {
    // Current L2Server 0x00af3854.
    L2_GUARD_FUNCTION();
    CSPointer<User> first = g_UserMap.FindObject(m_nTrader[0]);
    if (!first) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x117)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x117)
        L2_LOG_WARN(
            Log, "Cannot continue trade request: participant object ID %d is unavailable.", static_cast<int>(m_nTrader[0])
        );
        return false;
    }
    CSPointer<User> second = g_UserMap.FindObject(m_nTrader[1]);
    if (!second) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x11e)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x11e)
        L2_LOG_WARN(
            Log, "Cannot continue trade request: participant object ID %d is unavailable.", static_cast<int>(m_nTrader[1])
        );
        return false;
    }

    const auto hasPendingTradeBlocker = [](User* const user) {
        return user->GetPendingUseETCItem(PUIT_ENCHANT) != nullptr ||
            user->GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT) != nullptr ||
            user->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING) != nullptr ||
            user->GetPendingUseETCItem(PUIT_CRYSTAL) != nullptr || user->GetPendingUseETCItem(PUIT_ONE) != nullptr ||
            user->GetPendingUseETCItem(PUIT_TWO) != nullptr || user->GetPendingUseETCItem(PUIT_HLOCKED) != nullptr;
    };

    if (hasPendingTradeBlocker(first.get())) {
        first->SendSystemMessage(SystemMessageId::CANNOT_TRADE_IN_CURRENT_STATE);
        second->SendSystemMessage(SystemMessageId::CANNOT_TRADE_ITEMS_WITH_TARGET_USER);
        first->InitTrade();
        second->InitTrade();
        return false;
    }

    // The current target checks the second user's first six pending slots,
    // then (oddly) checks the first user's HLOCKED slot again.
    const bool secondHasPendingTradeBlocker = second->GetPendingUseETCItem(PUIT_ENCHANT) != nullptr ||
        second->GetPendingUseETCItem(PUIT_ATTRIBUTE_ENCHANT) != nullptr ||
        second->GetPendingUseETCItem(PUIT_SHAPE_SHIFTING) != nullptr ||
        second->GetPendingUseETCItem(PUIT_CRYSTAL) != nullptr || second->GetPendingUseETCItem(PUIT_ONE) != nullptr ||
        second->GetPendingUseETCItem(PUIT_TWO) != nullptr || first->GetPendingUseETCItem(PUIT_HLOCKED) != nullptr;
    if (secondHasPendingTradeBlocker) {
        first->SendSystemMessage(SystemMessageId::CANNOT_TRADE_ITEMS_WITH_TARGET_USER);
        second->SendSystemMessage(SystemMessageId::CANNOT_TRADE_IN_CURRENT_STATE);
        first->InitTrade();
        second->InitTrade();
        return false;
    }

    {
        L2_GUARD(m_lock);
        m_bIsStart = true;
        static_cast<void>(SendTradableList(first.get()));
        static_cast<void>(SendTradableList(second.get()));
        first->GetSocket()->Send("chccS", 0x62, SystemMessageId::TRADE_STARTED, 1, 0x0c, second->GetName2(first.get()));
        second->GetSocket()->Send("chccS", 0x62, SystemMessageId::TRADE_STARTED, 1, 0x0c, first->GetName2(second.get()));
    }

    const FVector position = first->GetPos();
#if defined(_WIN32)
    const wchar_t* const firstAccount = reinterpret_cast<const wchar_t*>(first->m_sAccountName.data());
    const wchar_t* const secondAccount = reinterpret_cast<const wchar_t*>(second->m_sAccountName.data());
#else
    std::array<wchar_t, 16> firstAccountBuffer{};
    std::array<wchar_t, 16> secondAccountBuffer{};
    const wchar_t* const firstAccount =
        CopyUtf16ToWide(first->m_sAccountName.data(), first->m_sAccountName.size(), firstAccountBuffer);
    const wchar_t* const secondAccount =
        CopyUtf16ToWide(second->m_sAccountName.data(), second->m_sAccountName.size(), secondAccountBuffer);
#endif
#if defined(_WIN32)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
        u"%s,%s,%s,%s,",
        0x396, first->m_nClassId, first->GetDBID(), second->m_nClassId, second->GetDBID(),
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), first->m_data->m_Race, first->m_data->m_Sex, first->m_data->m_nClass,
        first->m_data->m_nLevel, second->m_data->m_Race, second->m_data->m_Sex, second->m_data->m_nClass,
        second->m_data->m_nLevel, first->GetName(), firstAccount, second->GetName(), secondAccount
    );
#else
    // Native varargs must match the 64-bit numeric fields in the kept CSV format.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
    //     L"%s,%s,%s,%s,",
    //     0x396,
    //     first->m_nClassId,
    //     first->GetDBID(),
    //     second->m_nClassId,
    //     second->GetDBID(),
    //     runtime::Win64CvttDoubleToInt32(position.x),
    //     runtime::Win64CvttDoubleToInt32(position.y),
    //     runtime::Win64CvttDoubleToInt32(position.z),
    //     first->m_data->m_Race,
    //     first->m_data->m_Sex,
    //     first->m_data->m_nClass,
    //     first->m_data->m_nLevel,
    //     second->m_data->m_Race,
    //     second->m_data->m_Sex,
    //     second->m_data->m_nClass,
    //     second->m_data->m_nLevel,
    //     first->GetName(),
    //     firstAccount,
    //     second->GetName(),
    //     secondAccount)
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
    //     L"%s,%s,%s,%s,",
    //     static_cast<long long>(0x396),
    //     static_cast<long long>(first->m_nClassId),
    //     static_cast<long long>(first->GetDBID()),
    //     static_cast<long long>(second->m_nClassId),
    //     static_cast<long long>(second->GetDBID()),
    //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
    //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
    //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)),
    //     static_cast<long long>(first->m_data->m_Race),
    //     static_cast<long long>(first->m_data->m_Sex),
    //     static_cast<long long>(first->m_data->m_nClass),
    //     static_cast<long long>(first->m_data->m_nLevel),
    //     static_cast<long long>(second->m_data->m_Race),
    //     static_cast<long long>(second->m_data->m_Sex),
    //     static_cast<long long>(second->m_data->m_nClass),
    //     static_cast<long long>(second->m_data->m_nLevel),
    //     first->GetName(),
    //     firstAccount,
    //     second->GetName(),
    //     secondAccount)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,"
        u"%s,%s,%s,%s,",
        static_cast<long long>(0x396), static_cast<long long>(first->m_nClassId), static_cast<long long>(first->GetDBID()),
        static_cast<long long>(second->m_nClassId), static_cast<long long>(second->GetDBID()),
        static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)), static_cast<long long>(first->m_data->m_Race),
        static_cast<long long>(first->m_data->m_Sex), static_cast<long long>(first->m_data->m_nClass),
        static_cast<long long>(first->m_data->m_nLevel), static_cast<long long>(second->m_data->m_Race),
        static_cast<long long>(second->m_data->m_Sex), static_cast<long long>(second->m_data->m_nClass),
        static_cast<long long>(second->m_data->m_nLevel), first->GetName(), firstAccount, second->GetName(), secondAccount
    );
#endif
    return true;
}

bool CTrade::RequestAddItems(const unsigned char* packet, User* const user) {
    // Current L2Server 0x00af47f0.
    L2_GUARD_FUNCTION();
    CSPointer<User> first = g_UserMap.FindObject(m_nTrader[0]);
    if (!first) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x2a5)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x2a5)
        L2_LOG_WARN(
            Log, "Cannot continue add-items request: participant object ID %d is unavailable.",
            static_cast<int>(m_nTrader[0])
        );
        return false;
    }
    CSPointer<User> second = g_UserMap.FindObject(m_nTrader[1]);
    if (!second) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x2ac)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x2ac)
        L2_LOG_WARN(
            Log, "Cannot continue add-items request: participant object ID %d is unavailable.",
            static_cast<int>(m_nTrader[1])
        );
        return false;
    }

    CPackageItemPacketManager tradedItems;
    CPackageItemPacketManager inventoryUpdates;
    unsigned int traderNumber;
    int addedItems;
    const bool accepted = [&] {
        L2_GUARD(m_lock);
        if (m_bIsTradeDone[0]) {
            user->SendSystemMessage(
                user == first.get() ? SystemMessageId::CANNOT_CHANGE_ITEMS_AFTER_TRADE_CONFIRMATION
                                    : SystemMessageId::CANNOT_MOVE_TRADE_ITEMS_AFTER_OTHER_CHARACTER_CONFIRMED
            );
            return false;
        }
        if (m_bIsTradeDone[1]) {
            user->SendSystemMessage(
                user == first.get() ? SystemMessageId::CANNOT_MOVE_TRADE_ITEMS_AFTER_OTHER_CHARACTER_CONFIRMED
                                    : SystemMessageId::CANNOT_CHANGE_ITEMS_AFTER_TRADE_CONFIRMATION
            );
            return false;
        }

        traderNumber = 0;
        if (user == first.get()) {
            traderNumber = 0;
        } else if (user == second.get()) {
            traderNumber = 1;
        } else {
            return false;
        }

        if (!CheckItemCount(packet, static_cast<int>(traderNumber))) {
            return false;
        }
        addedItems = AddItems(packet, traderNumber, tradedItems, inventoryUpdates);
        return true;
    }();
    if (!accepted) {
        return false;
    }
    if (addedItems < 1) {
        return false;
    }

    SetShortTimeStamp(&m_dwShortTimeStamp, 0);
    User* const trader = traderNumber == 0 ? first.get() : second.get();
    User* const other = traderNumber == 0 ? second.get() : first.get();
    CUserSocket* const traderSocket = trader->GetSocket();
    CUserSocket* const otherSocket = other->GetSocket();

    traderSocket->Send("ccd", S_TRADE_OWN_ADD, 1, tradedItems.GetTotalPackageItemLength());
    tradedItems.SendPacket_PackageItemData(traderSocket, S_TRADE_OWN_ADD, PackageItemPacket::PIPT_ITEM_INFO);

    traderSocket->Send("ccd", S_TRADE_UPDATE, 1, inventoryUpdates.GetTotalPackageItemLength());
    inventoryUpdates.SendPacket_PackageItemData(traderSocket, S_TRADE_UPDATE, PackageItemPacket::PIPT_ITEM_INFO);

    if (traderNumber == 1) {
        trader->SendItemList(true, true, nullptr);
    }
    otherSocket->Send("ccd", S_TRADE_OTHER_ADD, 1, tradedItems.GetTotalPackageItemLength());
    tradedItems.SendPacket_PackageItemData(otherSocket, S_TRADE_OTHER_ADD, PackageItemPacket::PIPT_ITEM_INFO);
    return true;
}

bool CTrade::CheckItemRestrict() {
    // Current L2Server 0x00aef4d4.
    L2_GUARD_FUNCTION();
    for (const std::vector<int>& tradeList : m_TradeList) {
        for (const int itemIndex : tradeList) {
            CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
            if (item && CItemRestrict::Inst().CheckItemRestrict(item->m_data->m_nClassID)) {
                // Log.Add(LOG_ERROR, L"Item Restrict on")
                // L2_LOG_ERROR(Log, L"Item Restrict on")
                L2_LOG_WARN(Log, "Trade contains an item restricted from trading.");
                return true;
            }
        }
    }
    return false;
}

CItem* CTrade::GetItem(User*, const int databaseId) {
    // Current L2Server 0x00aef728 searches trader 1 before trader 0.
    L2_GUARD_FUNCTION();
    for (const int itemIndex : m_TradeList[1]) {
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
        if (item && item->IsItem() && static_cast<int>(item->GetDBID()) == databaseId) {
            return item.get();
        }
    }
    for (const int itemIndex : m_TradeList[0]) {
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
        if (item && item->IsItem() && static_cast<int>(item->GetDBID()) == databaseId) {
            return item.get();
        }
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"Trade is done with not registered Trader.")
    // L2_LOG_ERROR(Log,
    //     L"Trade is done with not registered Trader.")
    L2_LOG_WARN(Log, "Trade item lookup failed: item DB ID %d is absent from both trade lists.", databaseId);
    return nullptr;
}

bool CTrade::SameDBItemExistInTradeList(CItem* const item, const unsigned int traderNumber) {
    // Current L2Server 0x00aefa54.
    L2_GUARD_FUNCTION();
    if (item == nullptr || traderNumber >= m_TradeList.size() ||
        CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
        return false;
    }

    const unsigned int databaseId = item->GetDBID();
    for (const int itemIndex : m_TradeList[traderNumber]) {
        CSPointer<CItem> registered = CItemFactory::Inst().FindObjectSP(itemIndex);
        if (registered && registered->GetDBID() == databaseId) {
            return true;
        }
    }
    return false;
}

bool CTrade::CheckIventoryWeightAndCount(const std::vector<int>& tradeList, CInventory& inventory) {
    // Current L2Server 0x00af52ec. The target spelling is intentional.
    L2_GUARD_FUNCTION();
    ItemAmountDataStruct amounts;
    for (const int itemIndex : tradeList) {
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
        if (item) {
            amounts.Push(item->m_data->m_nClassID, item->m_data->m_nCount);
        }
    }
    return inventory.CheckAddableMulti(amounts, false);
}

bool CTrade::PressDone(User* const user) {
    // Current L2Server 0x00af5480.
    L2_GUARD_FUNCTION();
    if (CheckItemRestrict()) {
        return false;
    }

    CSPointer<User> first = g_UserMap.FindObject(m_nTrader[0]);
    if (!first) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x4b5)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x4b5)
        L2_LOG_WARN(
            Log, "Cannot continue trade confirmation: participant object ID %d is unavailable.",
            static_cast<int>(m_nTrader[0])
        );
        return false;
    }
    CSPointer<User> second = g_UserMap.FindObject(m_nTrader[1]);
    if (!second) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x4bc)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x4bc)
        L2_LOG_WARN(
            Log, "Cannot continue trade confirmation: participant object ID %d is unavailable.",
            static_cast<int>(m_nTrader[1])
        );
        return false;
    }
    if (user == nullptr) {
        // Log.Add(LOG_ERROR, L"Trade done packet. but no trade.")
        // L2_LOG_ERROR(Log, L"Trade done packet. but no trade.")
        L2_LOG_WARN(Log, "Trade completion rejected: requesting player is null.");
        return false;
    }

    std::array<int, 2> itemCounts{};
    std::array<int, 2> packetLengths{};
    std::array<std::array<char, 4096>, 2> packets{};

    std::optional<std::array<std::vector<CSPointer<CItem>>, 2>> itemsStorage;
    std::optional<std::set<int>> databaseIdsStorage;
    CSPointer<CItem> failedItem;
    const int completion = [&] {
        L2_GUARD(m_lock);
        if (m_bEnd) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR] press trade done, already sent list1. [%s][%s]",
            //     first->GetName(),
            //     second->GetName())
            // Noise: repeated completion after the trade end flag is set is already handled.
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR] press trade done, already sent list1. [%s][%s]",
            //     first->GetName(),
            //     second->GetName())
            return 1;
        }

        if (!first->IsTalkableDistance(second.get())) {
            return -1;
        }
        SetShortTimeStamp(&m_dwShortTimeStamp, 0);

        User* other = nullptr;
        if (user == first.get()) {
            m_bIsTradeDone[0] = true;
            second->GetSocket()->Send("c", 0x82);
            first->GetSocket()->Send("c", 0x53);
            other = second.get();
        } else if (user == second.get()) {
            m_bIsTradeDone[1] = true;
            first->GetSocket()->Send("c", 0x82);
            second->GetSocket()->Send("c", 0x53);
            other = first.get();
        } else {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Trade is done with not registered Trader. [%s][%s]",
            //     first->GetName(),
            //     second->GetName())
            // L2_LOG_ERROR(Log,
            //     L"Trade is done with not registered Trader. [%s][%s]",
            //     first->GetName(),
            //     second->GetName())
            // L2_LOG_WARN(Log,
            //     "Trade confirmation rejected: requesting player is not a participant in the trade between %s and %s.",
            //     WideToUtf8(first->GetName()).c_str(),
            //     WideToUtf8(second->GetName()).c_str())
            L2_LOG_WARN(
                Log, "Trade confirmation rejected: requesting player is not a participant in the trade between %s and %s.",
                Utf16ToUtf8(first->GetName()).c_str(), Utf16ToUtf8(second->GetName()).c_str()
            );
            return -1;
        }

        other->GetSocket()->Send(
            "chccS", 0x62, SystemMessageId::OTHER_CHARACTER_CONFIRMED_TRADE, 1, 0x0c, user->GetName2(other)
        );

        if (m_bIsTradeDone[0] && m_bIsTradeDone[1]) {
            if (m_TradeList[0].empty() && m_TradeList[1].empty()) {
                second->SendSystemMessage(SystemMessageId::TRADE_ENDED);
                first->SendSystemMessage(SystemMessageId::TRADE_ENDED);
                first->GetSocket()->Send("cd", 0x1c, 1);
                second->GetSocket()->Send("cd", 0x1c, 1);
                first->InitTrade();
                second->InitTrade();
                m_bEnd = true;
                return 1;
            }

            if (!CheckIventoryWeightAndCount(m_TradeList[0], second->m_inv) ||
                !CheckIventoryWeightAndCount(m_TradeList[1], first->m_inv)) {
                return -1;
            }

            auto& items = itemsStorage.emplace();
            for (std::size_t traderNumber = 0; traderNumber < items.size(); ++traderNumber) {
                for (const int itemIndex : m_TradeList[traderNumber]) {
                    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
                    if (!item || !item->IsItem()) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"Trade is done with not registered Trader!")
                        // L2_LOG_ERROR(Log,
                        //     L"Trade is done with not registered Trader!")
                        L2_LOG_ERROR(
                            Log, "Trade completion rejected: item index %d is unavailable or is not an item.", itemIndex
                        );
                        failedItem = std::move(item);
                        return -1;
                    }
                    items[traderNumber].push_back(item);
                }
            }

            auto& databaseIds = databaseIdsStorage.emplace();
            for (const auto& traderItems : items) {
                for (const CSPointer<CItem>& item : traderItems) {
                    const int databaseId = static_cast<int>(item->GetDBID());
                    if (!databaseIds.insert(databaseId).second) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"same item[%d] dbid[%d] exist in trade list. [%s]-[%s]",
                        //     item->m_data->m_nClassID,
                        //     databaseId,
                        //     first->GetName(),
                        //     second->GetName())
                        // L2_LOG_ERROR(Log,
                        //     L"same item[%d] dbid[%d] exist in trade list. [%s]-[%s]",
                        //     item->m_data->m_nClassID,
                        //     databaseId,
                        //     first->GetName(),
                        //     second->GetName())
                        // L2_LOG_ERROR(Log,
                        //     "Trade completion rejected: duplicate item DB ID %d (class %d) in trade between %s and %s.",
                        //     databaseId,
                        //     static_cast<int>(item->m_data->m_nClassID),
                        //     WideToUtf8(first->GetName()).c_str(),
                        //     WideToUtf8(second->GetName()).c_str())
                        L2_LOG_ERROR(
                            Log, "Trade completion rejected: duplicate item DB ID %d (class %d) in trade between %s and %s.",
                            databaseId, static_cast<int>(item->m_data->m_nClassID), Utf16ToUtf8(first->GetName()).c_str(),
                            Utf16ToUtf8(second->GetName()).c_str()
                        );
                        return -1;
                    }
                }
            }

            CPet* const firstPet = first->GetPet();
            if (firstPet != nullptr) {
                for (const int itemIndex : m_TradeList[0]) {
                    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
                    if (!item || !item->IsItem()) {
                        failedItem = std::move(item);
                        return -1;
                    }
                    if (firstPet->GetDBID() == static_cast<unsigned int>(item->m_data->m_nDBID)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"using pet item[%d] dbid[%d] exist in trade list. [%s]-[%s]",
                        //     item->m_data->m_nClassID,
                        //     item->m_data->m_nDBID,
                        //     first->GetName(),
                        //     second->GetName())
                        // L2_LOG_ERROR(Log,
                        //     L"using pet item[%d] dbid[%d] exist in trade list. [%s]-[%s]",
                        //     item->m_data->m_nClassID,
                        //     item->m_data->m_nDBID,
                        //     first->GetName(),
                        //     second->GetName())
                        // L2_LOG_WARN(Log,
                        //     "Trade completion rejected: active pet control item class %d, DB ID %d, in trade between %s
                        //     and %s.", static_cast<int>(item->m_data->m_nClassID), static_cast<int>(item->m_data->m_nDBID),
                        //     WideToUtf8(first->GetName()).c_str(),
                        //     WideToUtf8(second->GetName()).c_str())
                        L2_LOG_WARN(
                            Log,
                            "Trade completion rejected: active pet control item class %d, DB ID %d, in trade between %s and "
                            "%s.",
                            static_cast<int>(item->m_data->m_nClassID), static_cast<int>(item->m_data->m_nDBID),
                            Utf16ToUtf8(first->GetName()).c_str(), Utf16ToUtf8(second->GetName()).c_str()
                        );
                        failedItem = std::move(item);
                        return -1;
                    }
                }
            }
            CPet* const secondPet = second->GetPet();
            if (secondPet != nullptr) {
                for (const int itemIndex : m_TradeList[1]) {
                    CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
                    if (!item || !item->IsItem()) {
                        failedItem = std::move(item);
                        return -1;
                    }
                    if (secondPet->GetDBID() == static_cast<unsigned int>(item->m_data->m_nDBID)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"using pet item[%d] dbid[%d] exist in trade list. [%s]-[%s]",
                        //     item->m_data->m_nClassID,
                        //     item->m_data->m_nDBID,
                        //     first->GetName(),
                        //     second->GetName())
                        // L2_LOG_ERROR(Log,
                        //     L"using pet item[%d] dbid[%d] exist in trade list. [%s]-[%s]",
                        //     item->m_data->m_nClassID,
                        //     item->m_data->m_nDBID,
                        //     first->GetName(),
                        //     second->GetName())
                        // L2_LOG_WARN(Log,
                        //     "Trade completion rejected: active pet control item class %d, DB ID %d, in trade between %s
                        //     and %s.", static_cast<int>(item->m_data->m_nClassID), static_cast<int>(item->m_data->m_nDBID),
                        //     WideToUtf8(first->GetName()).c_str(),
                        //     WideToUtf8(second->GetName()).c_str())
                        L2_LOG_WARN(
                            Log,
                            "Trade completion rejected: active pet control item class %d, DB ID %d, in trade between %s and "
                            "%s.",
                            static_cast<int>(item->m_data->m_nClassID), static_cast<int>(item->m_data->m_nDBID),
                            Utf16ToUtf8(first->GetName()).c_str(), Utf16ToUtf8(second->GetName()).c_str()
                        );
                        failedItem = std::move(item);
                        return -1;
                    }
                }
            }

            for (const CSPointer<CItem>& firstItem : items[0]) {
                if (!CSharedItemData::IsStackable(firstItem->m_data->m_ItemConsumeType)) {
                    continue;
                }
                for (const CSPointer<CItem>& secondItem : items[1]) {
                    if (CSharedItemData::IsStackable(secondItem->m_data->m_ItemConsumeType) &&
                        firstItem->m_data->m_nClassID == secondItem->m_data->m_nClassID) {
                        second->SendSystemMessage(SystemMessageId::CANNOT_TRADE_SAME_TYPE_STACKABLE_ITEMS_SIMULTANEOUSLY);
                        first->SendSystemMessage(SystemMessageId::CANNOT_TRADE_SAME_TYPE_STACKABLE_ITEMS_SIMULTANEOUSLY);
                        return -1;
                    }
                }
            }

            {
                L2_WRITE_GUARD(first->m_inv);
                L2_WRITE_GUARD(second->m_inv);
                for (std::size_t traderNumber = 0; traderNumber < items.size(); ++traderNumber) {
                    User* const source = traderNumber == 0 ? first.get() : second.get();
                    User* const recipient = traderNumber == 0 ? second.get() : first.get();

                    for (const CSPointer<CItem>& item : items[traderNumber]) {
                        int sourceUpdateType = 3;
                        int recipientUpdateType = 4;
                        int recipientDatabaseId = 0;
                        int recipientItemIndex = 0;

                        if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
                            CItem* const recipientItem =
                                recipient->m_inv.GetFirstItemByClassID(item->m_data->m_nClassID, false, false);
                            CItem* const sourceItem = source->m_inv.GetByDBID(static_cast<int>(item->GetDBID()));
                            if (sourceItem != nullptr) {
                                if (sourceItem->m_data->m_nCount < 1) {
                                    source->m_inv.Pop(sourceItem->m_data->index, false);
                                    source->m_inv.SetInventoryChanged(sourceItem, IUT_DELETE);
                                    sourceItem->Delete();
                                } else {
                                    sourceUpdateType = 1;
                                }
                            }

                            if (recipientItem == nullptr) {
                                recipientUpdateType = sourceUpdateType == 1 ? 2 : 4;
                            } else {
                                recipientUpdateType = 1;
                                recipientDatabaseId = static_cast<int>(recipientItem->GetDBID());
                                recipientItemIndex = static_cast<int>(recipientItem->Id());
                            }
                        }

                        const int assembled = ::Assemble(
                            packets[traderNumber].data() + packetLengths[traderNumber],
                            static_cast<int>(packets[traderNumber].size()) - packetLengths[traderNumber], "ddddddQ",
                            sourceUpdateType, static_cast<int>(item->GetDBID()), static_cast<int>(item->Id()),
                            recipientUpdateType, recipientDatabaseId, recipientItemIndex, item->m_data->m_nCount
                        );
                        if (assembled > 0) {
                            packetLengths[traderNumber] += assembled;
                            ++itemCounts[traderNumber];
                        }
                    }
                }
            }

            first->GetSocket()->Send("cd", 0x1c, 1);
            second->GetSocket()->Send("cd", 0x1c, 1);
            m_bEnd = true;
            databaseIdsStorage.reset();
            itemsStorage.reset();
        }

        if (m_bEnd && (itemCounts[0] > 0 || itemCounts[1] > 0)) {
            db.SendTradeCommit(
                first.get(), second.get(), itemCounts[0], packetLengths[0], packets[0].data(), itemCounts[1],
                packetLengths[1], packets[1].data()
            );
            SetShortTimeStamp(&m_dwShortTimeStamp, 0);
        }
        return 0;
    }();
    if (completion != 0) {
        return completion > 0;
    }

    if (m_bEnd) {
        const FVector position = first->GetPos();
#if defined(_WIN32)
        const wchar_t* const firstAccount = reinterpret_cast<const wchar_t*>(first->m_sAccountName.data());
        const wchar_t* const secondAccount = reinterpret_cast<const wchar_t*>(second->m_sAccountName.data());
#else
        std::array<wchar_t, 16> firstAccountBuffer{};
        std::array<wchar_t, 16> secondAccountBuffer{};
        const wchar_t* const firstAccount =
            CopyUtf16ToWide(first->m_sAccountName.data(), first->m_sAccountName.size(), firstAccountBuffer);
        const wchar_t* const secondAccount =
            CopyUtf16ToWide(second->m_sAccountName.data(), second->m_sAccountName.size(), secondAccountBuffer);
#endif
#if defined(_WIN32)
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%s,%s,%s,%s,",
            0x394, first->GetDBID(), first->GetAccountID(), second->GetDBID(), second->GetAccountID(),
            runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
            runtime::Win64CvttDoubleToInt32(position.z), first->m_data->m_Race, first->m_data->m_Sex,
            first->m_data->m_nClass, first->m_data->m_nLevel, second->m_data->m_nClass, second->m_data->m_nLevel,
            (runtime::GetTickCount() - m_dwTradeBegin) / 1000U, first->GetName(), firstAccount, second->GetName(),
            secondAccount
        );
#else
        // Native varargs must match the 64-bit numeric fields in the kept CSV format.
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%s,%s,%s,%s,",
        //     0x394,
        //     first->GetDBID(),
        //     first->GetAccountID(),
        //     second->GetDBID(),
        //     second->GetAccountID(),
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z),
        //     first->m_data->m_Race,
        //     first->m_data->m_Sex,
        //     first->m_data->m_nClass,
        //     first->m_data->m_nLevel,
        //     second->m_data->m_nClass,
        //     second->m_data->m_nLevel,
        //     (runtime::GetTickCount() - m_dwTradeBegin) / 1000U,
        //     first->GetName(),
        //     firstAccount,
        //     second->GetName(),
        //     secondAccount)
        // Log.AddT(
        //     LOG_BOTH,
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        //     L"%s,%s,%s,%s,",
        //     static_cast<long long>(0x394),
        //     static_cast<long long>(first->GetDBID()),
        //     static_cast<long long>(first->GetAccountID()),
        //     static_cast<long long>(second->GetDBID()),
        //     static_cast<long long>(second->GetAccountID()),
        //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
        //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
        //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)),
        //     static_cast<long long>(first->m_data->m_Race),
        //     static_cast<long long>(first->m_data->m_Sex),
        //     static_cast<long long>(first->m_data->m_nClass),
        //     static_cast<long long>(first->m_data->m_nLevel),
        //     static_cast<long long>(second->m_data->m_nClass),
        //     static_cast<long long>(second->m_data->m_nLevel),
        //     static_cast<long long>((runtime::GetTickCount() - m_dwTradeBegin) / 1000U),
        //     first->GetName(),
        //     firstAccount,
        //     second->GetName(),
        //     secondAccount)
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
            u"%s,%s,%s,%s,",
            static_cast<long long>(0x394), static_cast<long long>(first->GetDBID()),
            static_cast<long long>(first->GetAccountID()), static_cast<long long>(second->GetDBID()),
            static_cast<long long>(second->GetAccountID()),
            static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
            static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
            static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)),
            static_cast<long long>(first->m_data->m_Race), static_cast<long long>(first->m_data->m_Sex),
            static_cast<long long>(first->m_data->m_nClass), static_cast<long long>(first->m_data->m_nLevel),
            static_cast<long long>(second->m_data->m_nClass), static_cast<long long>(second->m_data->m_nLevel),
            static_cast<long long>((runtime::GetTickCount() - m_dwTradeBegin) / 1000U), first->GetName(), firstAccount,
            second->GetName(), secondAccount
        );
#endif
    }
    if (m_bEnd && itemCounts[0] == 0 && itemCounts[1] == 0) {
        first->TradeEnd();
    }
    return true;
}

int CTrade::GetTradeItemWeight(int userId) {
    L2_GUARD_FUNCTION();

    std::size_t traderIndex = 0;
    if (m_nTrader[0] == userId) {
        traderIndex = 0;
    } else if (m_nTrader[1] == userId) {
        traderIndex = 1;
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x726)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x726)
        L2_LOG_WARN(Log, "Cannot calculate trade item weight: object ID %d is not a participant; returning zero.", userId);
        return 0;
    }

    CSPointer<User> user = g_UserMap.FindObject(userId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x72d)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find user.",
        //     kTradeSource,
        //     0x72d)
        L2_LOG_WARN(
            Log, "Cannot calculate trade item weight: participant object ID %d is unavailable; returning zero.", userId
        );
        return 0;
    }

    std::int32_t weight = 0;
    for (const int itemIndex : m_TradeList[traderIndex]) {
        CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
        if (!item || item->m_data->m_ItemConsumeType == ICT_CHARGE) {
            continue;
        }

        if (item->m_data->m_nCount < 1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d] item count is invalid[%d][%I64d]",
            //     kTradeSource,
            //     0x740,
            //     item->m_data->m_nClassID,
            //     item->m_data->m_nCount)
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d] item count is invalid[%d][%I64d]",
            //     kTradeSource,
            //     0x740,
            //     item->m_data->m_nClassID,
            //     item->m_data->m_nCount)
            L2_LOG_ERROR(
                Log, "Trade weight calculation includes item class %d with nonpositive count %lld; calculation continues.",
                static_cast<int>(item->m_data->m_nClassID), static_cast<long long>(item->m_data->m_nCount)
            );
        }

        weight = runtime::AddInt32Wrapping(
            weight,
            runtime::MultiplyInt32Wrapping(item->m_const->m_nWeight, runtime::Int32FromBits(item->m_data->m_nCount))
        );
    }
    return weight;
}

bool CTrade::CheckTradeTimeout() {
    L2_GUARD_FUNCTION();
    return !IsInShortTime(m_dwShortTimeStamp, 300000);
}

bool CTrade::Canceled(User* user, bool force) {
    L2_GUARD_FUNCTION();

    CSPointer<User> first = g_UserMap.FindObject(m_nTrader[0]);
    if (!first) {
        return false;
    }

    CSPointer<User> second = g_UserMap.FindObject(m_nTrader[1]);
    if (!second) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] Cannot find user. the other user is [%s]",
        //     kTradeSource,
        //     0x172,
        //     first->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] Cannot find user. the other user is [%s]",
        //     kTradeSource,
        //     0x172,
        //     first->GetName())
        // L2_LOG_WARN(Log,
        //     "Cannot cancel trade: participant object ID %d is unavailable. Other participant is %s.",
        //     static_cast<int>(m_nTrader[1]),
        //     WideToUtf8(first->GetName()).c_str())
        L2_LOG_WARN(
            Log, "Cannot cancel trade: participant object ID %d is unavailable; other participant is %s.",
            static_cast<int>(m_nTrader[1]), Utf16ToUtf8(first->GetName()).c_str()
        );
        static std::int32_t missingUserErrors = 0;
        const bool report = missingUserErrors == (missingUserErrors / 20) * 20;
        ++missingUserErrors;
        if (report) {
            runtime::process::ReportCallStack(LOG_ERROR, false);
        }
        return false;
    }

    const bool canceled = [&] {
        L2_GUARD(m_lock);
        if (m_bEnd && !force) {
            return false;
        }

        if (force) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] trade canceled by cache corruption [%s][%s]",
            //     kTradeSourceWide,
            //     0x187,
            //     first->GetName(),
            //     second->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] trade canceled by cache corruption [%s][%s]",
            //     kTradeSourceWide,
            //     0x187,
            //     first->GetName(),
            //     second->GetName())
            // L2_LOG_WARN(Log,
            //     "Forced trade cancellation beginning for %s and %s.",
            //     WideToUtf8(first->GetName()).c_str(),
            //     WideToUtf8(second->GetName()).c_str())
            L2_LOG_WARN(
                Log, "Forced trade cancellation beginning for %s and %s.", Utf16ToUtf8(first->GetName()).c_str(),
                Utf16ToUtf8(second->GetName()).c_str()
            );
        }

        {
            L2_WRITE_GUARD(first->m_inv);
            for (const int itemIndex : m_TradeList[0]) {
                CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
                if (!item) {
                    continue;
                }

                CItem* changedItem = item.get();
                InventoryUpdateType updateType = IUT_ADD;
                if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
                    CItem* const existing = first->m_inv.GetFirstItemByClassID(item->m_data->m_nClassID, false, false);
                    if (existing == nullptr) {
                        if (!first->m_inv.Push(item->m_data->index)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     "inven push failed at file[%s], line[%d]",
                            //     kTradeSource,
                            //     0x1a3)
                            // L2_LOG_ERROR(Log,
                            //     "inven push failed at file[%s], line[%d]",
                            //     kTradeSource,
                            //     0x1a3)
                            L2_LOG_ERROR(
                                Log,
                                "Trade cancellation could not return item index %d to participant object ID %d; processing "
                                "continues.",
                                itemIndex, static_cast<int>(m_nTrader[0])
                            );
                        }
                    } else {
                        if (!existing->AddAmount(item->m_data->m_nCount)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d], dbid[%d] user0[%s]",
                            //     kTradeSource,
                            //     0x19b,
                            //     existing->m_data->m_nCount,
                            //     item->m_data->m_nCount,
                            //     existing->GetDBID(),
                            //     first->GetName())
                            // L2_LOG_ERROR(Log,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d], dbid[%d] user0[%s]",
                            //     kTradeSource,
                            //     0x19b,
                            //     existing->m_data->m_nCount,
                            //     item->m_data->m_nCount,
                            //     existing->GetDBID(),
                            //     first->GetName())
                            // L2_LOG_ERROR(Log,
                            //     "Trade cancellation stack merge failed for player %s, item DB ID %u: stack count %lld,
                            //     requested delta %lld. Processing continues.", WideToUtf8(first->GetName()).c_str(),
                            //     static_cast<unsigned>(existing->GetDBID()),
                            //     static_cast<long long>(existing->m_data->m_nCount),
                            //     static_cast<long long>(item->m_data->m_nCount))
                            L2_LOG_ERROR(
                                Log,
                                "Trade cancellation stack merge failed for player %s, item DB ID %u: stack count %lld, "
                                "requested delta %lld; processing continues.",
                                Utf16ToUtf8(first->GetName()).c_str(), static_cast<unsigned>(existing->GetDBID()),
                                static_cast<long long>(existing->m_data->m_nCount),
                                static_cast<long long>(item->m_data->m_nCount)
                            );
                        }
                        changedItem = existing;
                        updateType = IUT_UPDATE;
                    }
                } else if (!first->m_inv.Push(item->m_data->index)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "inven push failed at file[%s], line[%d]",
                    //     kTradeSource,
                    //     0x1ad)
                    // L2_LOG_ERROR(Log,
                    //     "inven push failed at file[%s], line[%d]",
                    //     kTradeSource,
                    //     0x1ad)
                    L2_LOG_ERROR(
                        Log,
                        "Trade cancellation could not return item index %d to participant object ID %d; processing "
                        "continues.",
                        itemIndex, static_cast<int>(m_nTrader[0])
                    );
                }

                first->m_inv.SetInventoryChanged(changedItem, updateType);
            }
        }

        {
            L2_WRITE_GUARD(second->m_inv);
            for (const int itemIndex : m_TradeList[1]) {
                CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemIndex);
                if (!item) {
                    continue;
                }

                CItem* changedItem = item.get();
                InventoryUpdateType updateType = IUT_ADD;
                if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
                    CItem* const existing = second->m_inv.GetFirstItemByClassID(item->m_data->m_nClassID, false, false);
                    if (existing == nullptr) {
                        if (!second->m_inv.Push(item->m_data->index)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     "inven push failed at file[%s], line[%d]",
                            //     kTradeSource,
                            //     0x1ce)
                            // L2_LOG_ERROR(Log,
                            //     "inven push failed at file[%s], line[%d]",
                            //     kTradeSource,
                            //     0x1ce)
                            L2_LOG_ERROR(
                                Log,
                                "Trade cancellation could not return item index %d to participant object ID %d; processing "
                                "continues.",
                                itemIndex, static_cast<int>(m_nTrader[1])
                            );
                        }
                    } else {
                        if (!existing->AddAmount(item->m_data->m_nCount)) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d], dbid[%d] user1[%s]",
                            //     kTradeSource,
                            //     0x1c6,
                            //     existing->m_data->m_nCount,
                            //     item->m_data->m_nCount,
                            //     existing->GetDBID(),
                            //     second->GetName())
                            // L2_LOG_ERROR(Log,
                            //     L"[%S][%d] item amount is invalid"
                            //     L"[%I64d][%I64d], dbid[%d] user1[%s]",
                            //     kTradeSource,
                            //     0x1c6,
                            //     existing->m_data->m_nCount,
                            //     item->m_data->m_nCount,
                            //     existing->GetDBID(),
                            //     second->GetName())
                            // L2_LOG_ERROR(Log,
                            //     "Trade cancellation stack merge failed for player %s, item DB ID %u: stack count %lld,
                            //     requested delta %lld. Processing continues.", WideToUtf8(second->GetName()).c_str(),
                            //     static_cast<unsigned>(existing->GetDBID()),
                            //     static_cast<long long>(existing->m_data->m_nCount),
                            //     static_cast<long long>(item->m_data->m_nCount))
                            L2_LOG_ERROR(
                                Log,
                                "Trade cancellation stack merge failed for player %s, item DB ID %u: stack count %lld, "
                                "requested delta %lld; processing continues.",
                                Utf16ToUtf8(second->GetName()).c_str(), static_cast<unsigned>(existing->GetDBID()),
                                static_cast<long long>(existing->m_data->m_nCount),
                                static_cast<long long>(item->m_data->m_nCount)
                            );
                        }
                        changedItem = existing;
                        updateType = IUT_UPDATE;
                    }
                } else if (!second->m_inv.Push(item->m_data->index)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "inven push failed at file[%s], line[%d]",
                    //     kTradeSource,
                    //     0x1d8)
                    // L2_LOG_ERROR(Log,
                    //     "inven push failed at file[%s], line[%d]",
                    //     kTradeSource,
                    //     0x1d8)
                    L2_LOG_ERROR(
                        Log,
                        "Trade cancellation could not return item index %d to participant object ID %d; processing "
                        "continues.",
                        itemIndex, static_cast<int>(m_nTrader[1])
                    );
                }

                second->m_inv.SetInventoryChanged(changedItem, updateType);
            }
        }

        first->GetSocket()->Send("cd", 0x1c, 2);
        first->InitTrade();
        second->GetSocket()->Send("cd", 0x1c, 2);
        second->InitTrade();

        m_bEnd = true;
        return true;
    }();
    if (!canceled) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] try to cancel trade, already sent list. [%s][%s]",
        //     kTradeSourceWide,
        //     0x181,
        //     first->GetName(),
        //     second->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] try to cancel trade, already sent list. [%s][%s]",
        //     kTradeSourceWide,
        //     0x181,
        //     first->GetName(),
        //     second->GetName())
        // L2_LOG_WARN(Log,
        //     "Trade cancellation rejected: trade between %s and %s has already ended and cancellation was not forced.",
        //     WideToUtf8(first->GetName()).c_str(),
        //     WideToUtf8(second->GetName()).c_str())
        L2_LOG_WARN(
            Log, "Trade cancellation rejected: trade between %s and %s has already ended and cancellation was not forced.",
            Utf16ToUtf8(first->GetName()).c_str(), Utf16ToUtf8(second->GetName()).c_str()
        );
        return false;
    }

    first->SendItemList(true, true, nullptr);
    second->SendItemList(true, true, nullptr);
    first->GetSocket()->Send(
        "chccS", 0x62, SystemMessageId::OTHER_CHARACTER_CANCELLED_TRADE, 1, 0x0c, user->GetName2(first.get())
    );
    second->GetSocket()->Send(
        "chccS", 0x62, SystemMessageId::OTHER_CHARACTER_CANCELLED_TRADE, 1, 0x0c, user->GetName2(second.get())
    );

    const FVector position = first->GetPos();
#if defined(_WIN32)
    const wchar_t* const firstAccount = reinterpret_cast<const wchar_t*>(first->m_sAccountName.data());
    const wchar_t* const secondAccount = reinterpret_cast<const wchar_t*>(second->m_sAccountName.data());
#else
    std::array<wchar_t, 16> firstAccountBuffer{};
    std::array<wchar_t, 16> secondAccountBuffer{};
    const wchar_t* const firstAccount =
        CopyUtf16ToWide(first->m_sAccountName.data(), first->m_sAccountName.size(), firstAccountBuffer);
    const wchar_t* const secondAccount =
        CopyUtf16ToWide(second->m_sAccountName.data(), second->m_sAccountName.size(), secondAccountBuffer);
#endif
#if defined(_WIN32)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%s,%s,%s,%s,",
        0x395, first->m_nClassId, first->GetDBID(), second->m_nClassId, second->GetDBID(),
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), first->m_data->m_Race, first->m_data->m_Sex, first->m_data->m_nClass,
        first->m_data->m_nLevel, second->m_data->m_nClass, second->m_data->m_nLevel,
        (runtime::GetTickCount() - m_dwTradeBegin) / 1000U, first->GetName(), firstAccount, second->GetName(), secondAccount
    );
#else
    // Native varargs must match the 64-bit numeric fields in the kept CSV format.
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%s,%s,%s,%s,",
    //     0x395,
    //     first->m_nClassId,
    //     first->GetDBID(),
    //     second->m_nClassId,
    //     second->GetDBID(),
    //     runtime::Win64CvttDoubleToInt32(position.x),
    //     runtime::Win64CvttDoubleToInt32(position.y),
    //     runtime::Win64CvttDoubleToInt32(position.z),
    //     first->m_data->m_Race,
    //     first->m_data->m_Sex,
    //     first->m_data->m_nClass,
    //     first->m_data->m_nLevel,
    //     second->m_data->m_nClass,
    //     second->m_data->m_nLevel,
    //     (runtime::GetTickCount() - m_dwTradeBegin) / 1000U,
    //     first->GetName(),
    //     firstAccount,
    //     second->GetName(),
    //     secondAccount)
    // Log.AddT(
    //     LOG_BOTH,
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
    //     L"%s,%s,%s,%s,",
    //     static_cast<long long>(0x395),
    //     static_cast<long long>(first->m_nClassId),
    //     static_cast<long long>(first->GetDBID()),
    //     static_cast<long long>(second->m_nClassId),
    //     static_cast<long long>(second->GetDBID()),
    //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
    //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
    //     static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)),
    //     static_cast<long long>(first->m_data->m_Race),
    //     static_cast<long long>(first->m_data->m_Sex),
    //     static_cast<long long>(first->m_data->m_nClass),
    //     static_cast<long long>(first->m_data->m_nLevel),
    //     static_cast<long long>(second->m_data->m_nClass),
    //     static_cast<long long>(second->m_data->m_nLevel),
    //     static_cast<long long>((runtime::GetTickCount() - m_dwTradeBegin) / 1000U),
    //     first->GetName(),
    //     firstAccount,
    //     second->GetName(),
    //     secondAccount)
    Log.AddT(
        LOG_BOTH,
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,"
        u"%s,%s,%s,%s,",
        static_cast<long long>(0x395), static_cast<long long>(first->m_nClassId), static_cast<long long>(first->GetDBID()),
        static_cast<long long>(second->m_nClassId), static_cast<long long>(second->GetDBID()),
        static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.x)),
        static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.y)),
        static_cast<long long>(runtime::Win64CvttDoubleToInt32(position.z)), static_cast<long long>(first->m_data->m_Race),
        static_cast<long long>(first->m_data->m_Sex), static_cast<long long>(first->m_data->m_nClass),
        static_cast<long long>(first->m_data->m_nLevel), static_cast<long long>(second->m_data->m_nClass),
        static_cast<long long>(second->m_data->m_nLevel),
        static_cast<long long>((runtime::GetTickCount() - m_dwTradeBegin) / 1000U), first->GetName(), firstAccount,
        second->GetName(), secondAccount
    );
#endif
    return true;
}

CTradeManager::CTradeManager() {}

CSPointer<CTrade> CTradeManager::GetTradeSP(int tradeId) {
    L2_GUARD_FUNCTION();
    CSPointer<CTrade> result;
    {
        L2_GUARD(m_lock);
        const auto found = m_TradeSID_to_Trade.find(tradeId);
        if (found != m_TradeSID_to_Trade.end()) {
            result.SetObject(found->second, ObjectRefType::FunctionLocal);
        }
    }
    return result;
}

bool CTradeManager::DeleteTrade(int tradeId) {
    L2_GUARD_FUNCTION();
    const bool deleted = [&] {
        L2_GUARD(m_lock);
        bool deleted = false;
        const auto found = m_TradeSID_to_Trade.find(tradeId);
        if (found != m_TradeSID_to_Trade.end() && found->second != nullptr) {
            CTrade* const trade = found->second;
            m_TradeSID_to_Trade.erase(found);
            trade->Delete();
            deleted = true;
        }
        return deleted;
    }();
    return deleted;
}

int CTradeManager::CreateTrade(User* first, User* second) {
    L2_GUARD_FUNCTION();
    const int tradeId = [&] {
        L2_GUARD(m_lock);
        CTrade* const trade = new CTrade(first, second);
        trade->OnCreate();
        const int tradeId = trade->MemoryObjectID();
        m_TradeSID_to_Trade[tradeId] = trade;
        return tradeId;
    }();
    return tradeId;
}

CTradeReleaseVerifier::CTradeReleaseVerifier(CTrade* trade) : m_pTrade(trade) {
    L2_ADD_REF(m_pTrade, ObjectRefType::VerifierRetain);
    AddTimer(20000, 1);
    L2_RELEASE(this, ObjectRefType::CreateDelete);
}

void CTradeReleaseVerifier::TimerExpired(int) {
#if defined(_MSC_VER) && defined(_WIN32)
    __try {
#else
    try {
#endif
        const std::int32_t remaining = L2_RELEASE(m_pTrade, ObjectRefType::VerifierRetain);
        if (remaining > 0) {
            m_pTrade->ReportOrt(kTradeSource, 0x51);
            g_ServerContext.NotReleasedTrade.fetch_add(1, std::memory_order_seq_cst);
        }
#if defined(_MSC_VER) && defined(_WIN32)
    } __except (1) {
#else
    } catch (...) {
#endif
        // Log.Add(
        //     LOG_ERROR,
        //     "Safe TradeReleaseVerifier Saved server... T_T")
        // L2_LOG_ERROR(Log,
        //     "Safe TradeReleaseVerifier Saved server... T_T")
        L2_LOG_ERROR(Log, "Exception caught during trade release verification; cleanup may be incomplete.");
    }
}
