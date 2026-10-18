#include "WareHouse.h"

#include "CReadFile.h"
#include "User.h"
#include "UserDb.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/DelimitedText.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {

// constexpr const char* kBrWareHouseSource = "br_WareHouse.cpp"

std::u16string BrAuditDecimal(std::int64_t value) { return Utf8ToUtf16(std::to_string(value)); }

void FillBrAuditActor(std::array<std::u16string, 26>& fields, CUser* user) {
    if (!user)
        return;
    fields[1] = BrAuditDecimal(user->GetId());
    fields[2] = BrAuditDecimal(user->GetAccountID());
    fields[5] = BrAuditDecimal(user->GetX());
    fields[6] = BrAuditDecimal(user->GetY());
    fields[7] = BrAuditDecimal(user->GetZ());
    fields[11] = BrAuditDecimal(user->GetRace());
    fields[12] = BrAuditDecimal(user->GetGender());
    fields[13] = BrAuditDecimal(user->GetClass());
    fields[14] = BrAuditDecimal(user->GetLevel());
    fields[21] = std::u16string(user->GetCharName());
    fields[22] = std::u16string(user->GetAccountName());
}

void WriteBrGiveItemFailureAudit(CUser* user, int itemType, int itemAmount, std::int64_t warehouseNo, int productId) {
    // Current CWareHouse::GiveItems 0x004539ce emits route-6 record 0x2714
    // after rolling back the transaction map.
    std::array<std::u16string, 26> fields{};
    fields[0] = BrAuditDecimal(0x2714);
    FillBrAuditActor(fields, user);
    fields[15] = BrAuditDecimal(productId);
    fields[16] = BrAuditDecimal(warehouseNo);
    fields[17] = BrAuditDecimal(itemType);
    fields[18] = BrAuditDecimal(itemAmount);
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

void WriteBrGiveItemSuccessAudit(CUser* user, CItem& item, int productId, std::int64_t changedAmount) {
    // Current CWareHouse::GiveItems emits one route-6 record 0x2712 for each
    // transaction entry immediately before committing that entry.
    std::array<std::u16string, 26> fields{};
    fields[0] = BrAuditDecimal(0x2712);
    FillBrAuditActor(fields, user);
    fields[15] = BrAuditDecimal(productId);
    fields[16] = BrAuditDecimal(item.ItemID());
    fields[17] = BrAuditDecimal(item.Enchant());
    fields[18] = BrAuditDecimal(item.ItemType());
    fields[19] = BrAuditDecimal(changedAmount);
    fields[20] = BrAuditDecimal(item.Amount());
    runtime::process::WriteLogFormatterLine(6, runtime::JoinFields(fields, u','));
}

struct BrItemChange {
    CItemSP item;
    std::int64_t amountBefore = 0;
};

} // namespace

BrBuyProductResultType CWareHouse::GetPaymentItems(
    int paymentType, std::int64_t paymentAmount, int* paymentItemId, int* remainingPaymentAmount
) {
    // Current Cached.exe 0x00452acc, br_WareHouse.cpp:295-347.
    L2_GUARD_FUNCTION();

    CTransaction transaction;
    if (paymentAmount < 1) {
        return BR_BUY_SUCCESS;
    }

    CItemSP paymentItem;
    if (paymentType == 1) {
        paymentItem = GetAdena();
    } else if (paymentType == 2) {
        paymentItem = GetItemByType(0x5cfd, 0);
    } else {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]GiveItems error. unknown nPaymentType [%d][%lld]", kBrWareHouseSource, 0x127,
        //     paymentType, static_cast<long long>(paymentAmount)
        // )
        // Log.Add(LOG_ERROR,
        //     "[item shop] Payment rejected for owner %u: unsupported payment type %d, requested quantity %lld.",
        //     m_ownerId, paymentType, static_cast<long long>(paymentAmount))
        L2_LOG_ERROR(Log,
            "[item shop] Payment rejected for owner %u: unsupported payment type %d, requested quantity %lld.",
            m_ownerId, paymentType, static_cast<long long>(paymentAmount));
        transaction.SetRollback();
        return BR_BUY_INVALID_PRODUCT;
    }

    if (!paymentItem || !transaction.AddItem(paymentItem)) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]GiveItems error. invalid PaymentItem [%u][%d][%lld]", kBrWareHouseSource, 0x12e,
        //     m_ownerId, paymentType, static_cast<long long>(paymentAmount)
        // )
        // Log.Add(LOG_ERROR,
        //     "[item shop] Cannot prepare payment for owner %u: payment item is unavailable or cannot enter the transaction (payment type %d, requested quantity %lld). Rollback requested.",
        //     m_ownerId, paymentType, static_cast<long long>(paymentAmount))
        L2_LOG_ERROR(Log,
            "[item shop] Cannot prepare payment for owner %u: payment item is unavailable or cannot enter the transaction (payment type %d, requested quantity %lld); rollback requested.",
            m_ownerId, paymentType, static_cast<long long>(paymentAmount));
        transaction.SetRollback();
        return BR_BUY_INVALID_ITEM;
    }

    const std::int64_t remaining = paymentItem->Amount() - paymentAmount;
    if (remaining == 0) {
        paymentItem->SetAmount(0);
        paymentItem->TransactDelete();
    } else if (remaining < 1) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d]GiveItems error. invalid PaymentItem [%u][%d][%lld]", kBrWareHouseSource, 0x146,
        //     m_ownerId, paymentType, static_cast<long long>(paymentAmount)
        // )
        // Log.Add(LOG_ERROR,
        //     "[item shop] Payment rejected for owner %u: item %u has quantity %lld, requested %lld (payment type %d). Rollback requested.",
        //     m_ownerId, paymentItem->ItemID(), static_cast<long long>(paymentItem->Amount()), static_cast<long long>(paymentAmount), paymentType)
        L2_LOG_ERROR(Log,
            "[item shop] Payment rejected for owner %u: item %u has quantity %lld, requested %lld (payment type %d); rollback requested.",
            m_ownerId, paymentItem->ItemID(), static_cast<long long>(paymentItem->Amount()), static_cast<long long>(paymentAmount), paymentType);
        transaction.SetRollback();
        return BR_BUY_INVALID_ITEM;
    } else {
        paymentItem->SetAmount(remaining);
    }

    *paymentItemId = static_cast<int>(paymentItem->ItemID());
    *remainingPaymentAmount = static_cast<int>(remaining);
    transaction.SetCommit();
    return BR_BUY_SUCCESS;
}

BrBuyProductResultType CWareHouse::GiveItems(
    int itemType,
    int productAmount,
    int itemAmount,
    std::int64_t warehouseNo,
    int productId,
    int* createdItemCount,
    CPacketHelperEx<20480>& packet
) {
    // Current Cached.exe 0x00452ff4, br_WareHouse.cpp:50-240.
    L2_GUARD_FUNCTION();

    BrBuyProductResultType result = BR_BUY_SUCCESS;
    CUserSP user;
    std::vector<BrItemChange> changes;

    {
        CTransaction transaction;
        bool completed = false;

        if (productAmount < 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]invalid nProductAmount.warehouse[%u] "
            //     "itemType[%d] itemAmount[%d]",
            //     kBrWareHouseSource, 0x32, m_ownerId, itemType, productAmount
            // )
            // Log.Add(LOG_ERROR,
            //     "[item shop] Cannot grant product %d to owner %u: product quantity %d is negative (item class %d, item quantity %d, receipt warehouse %lld).",
            //     productId, m_ownerId, productAmount, itemType, itemAmount, static_cast<long long>(warehouseNo))
            L2_LOG_ERROR(Log,
                "[item shop] Cannot grant product %d to owner %u: product quantity %d is negative (item class %d, item quantity %d, receipt warehouse %lld).",
                productId, m_ownerId, productAmount, itemType, itemAmount, static_cast<long long>(warehouseNo));
            result = BR_BUY_INVALID_AMOUNT;
        } else if (itemAmount < 0) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[%s][%d]invalid itemAmount.warehouse[%u] "
            //     "itemType[%d] itemAmount[%d]",
            //     kBrWareHouseSource, 0x39, m_ownerId, itemType, itemAmount
            // )
            // Log.Add(LOG_ERROR,
            //     "[item shop] Cannot grant product %d to owner %u: item quantity %d is negative (item class %d, product quantity %d, receipt warehouse %lld).",
            //     productId, m_ownerId, itemAmount, itemType, productAmount, static_cast<long long>(warehouseNo))
            L2_LOG_ERROR(Log,
                "[item shop] Cannot grant product %d to owner %u: item quantity %d is negative (item class %d, product quantity %d, receipt warehouse %lld).",
                productId, m_ownerId, itemAmount, itemType, productAmount, static_cast<long long>(warehouseNo));
            result = BR_BUY_INVALID_AMOUNT;
        } else {
            const ItemConsumeType consumeType = g_readFile.GetItemConsumeType(itemType);
            if (consumeType == ICT_UNKNOWN) {
                // Log.Add(
                //     LOG_ERROR, "[%s][%d]GiveItems error. unknown AmountType [%u][%d]", kBrWareHouseSource, 0x41,
                //     m_ownerId, itemType
                // )
                // Log.Add(LOG_ERROR,
                //     "[item shop] Cannot grant product %d to owner %u: item class %d has unknown consume type (quantity %d, receipt warehouse %lld).",
                //     productId, m_ownerId, itemType, itemAmount, static_cast<long long>(warehouseNo))
                L2_LOG_ERROR(Log,
                    "[item shop] Cannot grant product %d to owner %u: item class %d has unknown consume type (quantity %d, receipt warehouse %lld).",
                    productId, m_ownerId, itemType, itemAmount, static_cast<long long>(warehouseNo));
                result = BR_BUY_INVALID_ITEM;
            } else if (consumeType == ICT_STACKABLE) {
                if (itemType == 0) {
                    // Log.Add(
                    //     LOG_ERROR, "[%s][%d]GiveItems error.[%u][%d][%d][%d]", kBrWareHouseSource, 0x4a, m_ownerId,
                    //     itemType, itemAmount, consumeType
                    // )
                    // Log.Add(LOG_ERROR,
                    //     "[item shop] Cannot grant product %d to owner %u: stackable item class is zero (quantity %d, receipt warehouse %lld).",
                    //     productId, m_ownerId, itemAmount, static_cast<long long>(warehouseNo))
                    L2_LOG_ERROR(Log,
                        "[item shop] Cannot grant product %d to owner %u: stackable item class is zero (quantity %d, receipt warehouse %lld).",
                        productId, m_ownerId, itemAmount, static_cast<long long>(warehouseNo));
                    result = BR_BUY_INVALID_ITEM;
                } else {
                    CItemSP item = GetItemByType(itemType, 0);
                    const bool isNew = !item;
                    const ItemOptionKey optionKey{};
                    const BaseAttribute baseAttribute{};
                    if (isNew) {
                        item = MakeNewItem(
                            m_ownerId, static_cast<unsigned int>(itemType), 0, 0, 0, 0, 0, 0, 0, optionKey, 0, baseAttribute,
                            0, 0
                        );
                    }
                    if (!item) {
                        // Log.Add(LOG_ERROR,
                        //     "[item shop] Cannot create the stack for product %d, owner %u (class %d, quantity %d, receipt warehouse %lld). Rollback requested.",
                        //     productId, m_ownerId, itemType, itemAmount, static_cast<long long>(warehouseNo))
                        L2_LOG_ERROR(Log,
                            "[item shop] Cannot create the stack for product %d, owner %u (class %d, quantity %d, receipt warehouse %lld); rollback requested.",
                            productId, m_ownerId, itemType, itemAmount, static_cast<long long>(warehouseNo));
                        result = BR_BUY_INVALID_ITEM;
                    } else {
                        const std::int64_t amountBefore = item->Amount();
                        const bool added = isNew ? transaction.AddItemNew(item) : transaction.AddItem(item);
                        if (!added) {
                            // Log.Add(
                            //     LOG_ERROR,
                            //     "Trying To give same stackable item"
                            //     "[owner dbid : %u]",
                            //     m_ownerId
                            // )
                            // Log.Add(LOG_ERROR,
                            //     "[item shop] Cannot enroll stack item %u (class %d) in the grant transaction for product %d, owner %u. Rollback requested.",
                            //     item->ItemID(), itemType, productId, m_ownerId)
                            L2_LOG_ERROR(Log,
                                "[item shop] Cannot enroll stack item %u (class %d) in the grant transaction for product %d, owner %u; rollback requested.",
                                item->ItemID(), itemType, productId, m_ownerId);
                            result = BR_BUY_INVALID_ITEM;
                        } else {
                            item->SetAmount(runtime::AddInt64Wrapping(amountBefore, itemAmount));
                            packet.Assemble(
                                "dddQ", consumeType, static_cast<int>(item->ItemID()), itemType,
                                static_cast<long long>(item->Amount())
                            );
                            ++*createdItemCount;
                            changes.push_back({item, amountBefore});
                            completed = true;
                        }
                    }
                }
            } else if (itemAmount < 1 || itemType == 0) {
                // Log.Add(
                //     LOG_ERROR, "[%s][%d]GiveItems error.[%u][%d][%d][%d]", kBrWareHouseSource, 0x8b, m_ownerId,
                //     itemType, itemAmount, consumeType
                // )
                // Log.Add(LOG_ERROR,
                //     "[item shop] Cannot grant product %d to owner %u: non-stackable item requires a nonzero class and positive quantity (class %d, quantity %d, consume type %d).",
                //     productId, m_ownerId, itemType, itemAmount, static_cast<int>(consumeType))
                L2_LOG_ERROR(Log,
                    "[item shop] Cannot grant product %d to owner %u: non-stackable item requires a nonzero class and positive quantity (class %d, quantity %d, consume type %d).",
                    productId, m_ownerId, itemType, itemAmount, static_cast<int>(consumeType));
                result = BR_BUY_INVALID_ITEM;
            } else {
                const ItemOptionKey optionKey{};
                const BaseAttribute baseAttribute{};
                for (int index = 0; index < itemAmount; ++index) {
                    CItemSP item = MakeNewItem(
                        m_ownerId, static_cast<unsigned int>(itemType), 1, 0, 0, 0, 0, 0, 0, optionKey, 0, baseAttribute, 0,
                        0
                    );
                    if (!item || !transaction.AddItemNew(item)) {
                        // Log.Add(LOG_ERROR, "[%s][%d]GiveItems error.", kBrWareHouseSource, 0xa1)
                        // Log.Add(LOG_ERROR,
                        //     "[item shop] Cannot create or enroll item %d of %d for product %d, owner %u (class %d, receipt warehouse %lld). Rollback requested for the grant transaction.",
                        //     index + 1, itemAmount, productId, m_ownerId, itemType, static_cast<long long>(warehouseNo))
                        L2_LOG_ERROR(Log,
                            "[item shop] Cannot create or enroll item %d of %d for product %d, owner %u (class %d, receipt warehouse %lld); rollback requested for the grant transaction.",
                            index + 1, itemAmount, productId, m_ownerId, itemType, static_cast<long long>(warehouseNo));
                        result = BR_BUY_INVALID_ITEM;
                        break;
                    }
                    packet.Assemble(
                        "dddQ", consumeType, static_cast<int>(item->ItemID()), itemType,
                        static_cast<long long>(item->Amount())
                    );
                    ++*createdItemCount;
                    changes.push_back({item, 1});
                }
                completed = static_cast<int>(changes.size()) == itemAmount;
            }
        }

        if (!completed) {
            transaction.SetRollback();
        } else {
            result = BR_BUY_SUCCESS;
            transaction.SetCommit();
        }

        user = g_userDb.GetUser(static_cast<int>(m_ownerId), true, false, runtime::process::CurrentDatabaseId());

        if (completed) {
            for (const BrItemChange& change : changes) {
                const std::int64_t changedAmount =
                    change.item->Amount() == change.amountBefore ? 1 : change.item->Amount() - change.amountBefore;
                if (warehouseNo > 0) {
                    DBConn connection(runtime::process::CurrentDatabaseId());
                    const int itemId = static_cast<int>(change.item->ItemID());
                    const int characterId = user ? user->GetId() : static_cast<int>(m_ownerId);
                    const long long receiveAmount = static_cast<long long>(changedAmount);
                    const long long sqlWarehouseNo = static_cast<long long>(warehouseNo);
                    constexpr const wchar_t* statement = L"{Call lin_CreatePremiumItemReceive (?, ?, ?, ?, ?, ?)}";
                    connection.Prepare(statement);
                    connection.BindParameter(&itemId);
                    connection.BindParameter(&characterId);
                    connection.BindParameter(&receiveAmount);
                    connection.BindParameter(&sqlWarehouseNo);
                    connection.BindParameter(&productId);
                    connection.BindParameter(&productAmount);
                    if (!connection.ExecutePrepared(statement)) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     "Give Item : qsCreateItemShopWarehouse fail "
                        //     "(%d, %lld)",
                        //     itemId, static_cast<long long>(warehouseNo)
                        // )
                        // Log.Add(LOG_ERROR,
                        //     "[item shop] Receipt registration failed in database %d for product %d, owner %u, item %u, quantity %lld, receipt warehouse %lld. Item transaction will still attempt to commit.",
                        //     runtime::process::CurrentDatabaseId(), productId, m_ownerId, change.item->ItemID(), static_cast<long long>(changedAmount), static_cast<long long>(warehouseNo))
                        L2_LOG_ERROR(Log,
                            "[item shop] Receipt registration failed in database %d for product %d, owner %u, item %u, quantity %lld, receipt warehouse %lld; item transaction will still attempt to commit.",
                            runtime::process::CurrentDatabaseId(), productId, m_ownerId, change.item->ItemID(), static_cast<long long>(changedAmount), static_cast<long long>(warehouseNo));
                    }
                }
                WriteBrGiveItemSuccessAudit(user.get(), *change.item.get(), productId, changedAmount);
            }
        }
    }

    if (result != BR_BUY_SUCCESS) {
        WriteBrGiveItemFailureAudit(user.get(), itemType, itemAmount, warehouseNo, productId);
    }
    return result;
}
