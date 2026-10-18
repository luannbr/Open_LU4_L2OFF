#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"

#include "Item.h"

#include "CReadFile.h"
#include "Cached.h"
#include "CastleDb.h"
#include "PledgeDb.h"
#include "UserDb.h"
#include "WareHouse.h"

#include "L2Core/src/inc/Guard.h"
#include "Portable/ProcessRuntime.h"
#include "Runtime.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
#include <cwchar>

CItemMap g_itemCache;

namespace {

constexpr std::int64_t kMaximumItemAmount = 999999999999LL;

// Original name is absent from all surviving symbols. Current
// Cached.exe 0x004e9654 proves this exact 16-parameter order and that Prepare
// precedes parameter binding.
bool ExecuteItemWrite(
    DBConn& connection,
    const wchar_t* statement,
    const std::uint32_t& ownerId,
    const std::uint32_t& itemType,
    const std::int64_t& amount,
    const int& enchant,
    const int& eroded,
    const int& bless,
    const int& ident,
    const int& wished,
    const int& warehouse,
    const std::uint32_t& itemId,
    const ItemOptionKey& optionKey,
    const int& intensiveItemType,
    const int& inventorySlotIndex,
    const short& hLocked,
    const short& useCount
) {
    // Select the 64-bit SQL binding on Linux and Windows.
    const long long sqlAmount = amount;
    connection.Prepare(statement);
    connection.BindParameter(&ownerId);
    connection.BindParameter(&itemType);
    connection.BindParameter(&sqlAmount);
    connection.BindParameter(&enchant);
    connection.BindParameter(&eroded);
    connection.BindParameter(&bless);
    connection.BindParameter(&ident);
    connection.BindParameter(&wished);
    connection.BindParameter(&warehouse);
    connection.BindParameter(&itemId);
    connection.BindParameter(&optionKey.variationOption1);
    connection.BindParameter(&optionKey.variationOption2);
    connection.BindParameter(&intensiveItemType);
    connection.BindParameter(&inventorySlotIndex);
    connection.BindParameter(&hLocked);
    connection.BindParameter(&useCount);
    return connection.ExecutePrepared(statement);
}

// Semantic name for current helper 0x004e9764. Its original name is not
// present in the PDB/guard material.
bool ExecuteBaseAttributeWrite(
    DBConn& connection, const wchar_t* statement, const std::uint32_t& itemId, const BaseAttribute& attribute
) {
    connection.Prepare(statement);
    connection.BindParameter(&itemId);
    connection.BindParameter(&attribute.m_nAttackType);
    connection.BindParameter(&attribute.m_nAttackValue);
    for (const short& value : attribute.m_nDefendValue) {
        connection.BindParameter(&value);
    }
    return connection.ExecutePrepared(statement);
}

// Original name is absent from the current symbols. Cached.exe 0x004e98c8
// fixes the parameter order used by CItem::ChangeAmount.
bool ExecuteAmountChange(
    DBConn& connection,
    const wchar_t* statement,
    const std::uint32_t& ownerId,
    const std::uint32_t& itemId,
    const std::int64_t& amount,
    const int& changeType
) {
    connection.Prepare(statement);
    connection.BindParameter(&ownerId);
    connection.BindParameter(&itemId);
    connection.BindParameter(&amount);
    connection.BindParameter(&changeType);
    return connection.ExecutePrepared(statement);
}

// Original name is absent from the current symbols. Cached.exe 0x004e99fc
// fixes the four SQL input types/order used by CItem::InitExtraInfo.
bool RegisterPeriodicItem(
    DBConn& connection,
    const wchar_t* statement,
    const std::uint32_t& itemId,
    const int& registrationTime,
    const int& period,
    const std::uint32_t& itemType
) {
    connection.Prepare(statement);
    connection.BindParameter(&itemId);
    connection.BindParameter(&registrationTime);
    connection.BindParameter(&period);
    connection.BindParameter(&itemType);
    return connection.ExecutePrepared(statement);
}

// Original helper name is absent from the surviving symbols. Current
// Cached.exe 0x004e9944 fixes this exact seven-parameter bind order. Callers
// select either lin_LogForPremiumItemCreate or ...Delete before entering it.
void ExecutePremiumItemLog(
    DBConn& connection,
    const wchar_t* statement,
    const unsigned int& accountId,
    const int& ownerId,
    const int& itemId,
    const std::int64_t& amount,
    const std::int64_t& secondaryAmount,
    const int& reason,
    const unsigned int& itemType
) {
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&ownerId);
    connection.BindParameter(&itemId);
    connection.BindParameter(&amount);
    connection.BindParameter(&secondaryAmount);
    connection.BindParameter(&reason);
    connection.BindParameter(&itemType);
    connection.ExecutePrepared(statement);
}

} // namespace

L2_DEFINE_OBJECT_CLASS_ID(CItem)

L2_DEFINE_OBJECT_CLASS_NAME(CItem, m_pClassName_CItem)

L2_DEFINE_OBJECT_NEW(CItem)

// The CRT initializer at 0x006a3084 makes CItem the eighteenth current
// CacheD MemoryObject class. The allocator body at 0x004e81dc publishes
// the name and target extent before zeroing the allocation.
L2_DEFINE_OBJECT_ALLOCATOR(CItem, m_pClassName_CItem)

L2_DEFINE_OBJECT_RELEASE(CItem)

void CItem::Init() {
    L2_GUARD_FUNCTION();

    // Current 0x004e82ac does not clear the configuration flag after use (the
    // C1 donor does). Retain the shipping current behavior.
    if (!GetCacheDConfiguration().itemClearOnStart)
        return;

    // Log.Add(LOG_NORMAL, "[NO_ERROR] Clearing Unowned items...")
    // Log.Add(LOG_NORMAL,
    //     "[items] Running unowned-item cleanup in database %d.",
    //     runtime::process::CurrentDatabaseId())
    L2_LOG_INFO(Log,
        "[items] Running unowned-item cleanup in database %d.",
        runtime::process::CurrentDatabaseId());

    int itemId = 0;
    int characterId = 0;
    int itemType = 0;
    int warehouse = 0;
    // Current 0x004e8382 constructs DBConn from TLS +0x10.
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&itemId);
    connection.Bind(&characterId);
    connection.Bind(&itemType);
    connection.Bind(&warehouse);
    connection.Prepare(L"{Call lin_DeleteNotOwnedItem}");
    if (connection.ExecutePrepared(L"{Call lin_DeleteNotOwnedItem}")) {
        while (connection.Fetch()) {
            // Log.Add(
            //     LOG_NORMAL,
            //     "[NO_ERROR] Unowned Item Deleted!! item_id[%d] "
            //     "char_id[%d] item_type[%d] warehouse[%d]",
            //     itemId, characterId, itemType, warehouse
            // )
            // Log.Add(LOG_NORMAL,
            //     "[items] Cleanup returned deleted item %d (owner %d, type %d, warehouse %d).",
            //     itemId, characterId, itemType, warehouse)
            L2_LOG_INFO(Log,
                "[items] Cleanup returned deleted item %d (owner %d, type %d, warehouse %d).",
                itemId, characterId, itemType, warehouse);
        }
        // Log.Add(LOG_NORMAL, "[items] Unowned-item cleanup result processing finished in database %d.",
        //     runtime::process::CurrentDatabaseId())
        L2_LOG_INFO(Log, "[items] Unowned-item cleanup result processing finished in database %d.",
            runtime::process::CurrentDatabaseId());
    } else {
        // Log.Add(LOG_ERROR, "[items] Unowned-item cleanup query failed in database %d.",
        //     runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log, "[items] Unowned-item cleanup query failed in database %d.",
            runtime::process::CurrentDatabaseId());
    }

    // The current image emits completion even when ExecutePrepared fails.
    // Noise: The unconditional completion record claimed success even when the cleanup query failed. Outcome is now reported in the query branch.
    // Log.Add(LOG_NORMAL, "[NO_ERROR] Unowned items are cleared.")
}

CItem::CItem(std::uint32_t itemId) {
    L2_GUARD_FUNCTION();
    InitItem(itemId);
}

CItem::CItem(
    std::uint32_t itemId,
    std::uint32_t ownerId,
    std::uint32_t itemType,
    std::int64_t amount,
    int enchant,
    int eroded,
    int bless,
    int ident,
    int wished,
    int warehouse,
    const ItemOptionKey& optionKey,
    int intensiveItemType,
    const BaseAttribute& baseAttribute,
    int inventorySlotIndex,
    int appearanceType,
    short hLocked,
    short useCount
) {
    L2_GUARD_FUNCTION();
    InitItem(itemId);
    Set(ownerId, itemType, amount, enchant, eroded, bless, ident, wished, warehouse, optionKey, intensiveItemType,
        baseAttribute, inventorySlotIndex, appearanceType, hLocked, useCount);
}

void CItem::InitItem(std::uint32_t itemId) {
    L2_GUARD_FUNCTION();

    // Exact current 0x004e84a0 stores. The already-constructed option key and
    // three xmaps are not reset here.
    m_transactMode = TransactType_Invalid;
    m_itemId = itemId;
    m_ownerId = 0;
    m_itemType = 0;
    m_amount = 0;
    m_enchant = 0;
    m_eroded = 0;
    m_bless = 0;
    m_ident = 0;
    m_wished = 0;
    m_warehouse = 0;
    m_inTransactions = 0;
    m_deleted = false;
    m_changeScope = ItemChangeScope_Unknown;
    m_privateStorePrice = 0;
    m_auditAmount = 0;
    m_isUsable = true;
    m_writeCachePending = false;
    m_intensiveItemType = 0;
    m_inventorySlotIndex = -1;
    m_baseAttribute = BaseAttribute();
    m_lateSavingAmount = 0;
    m_baseAttributeDirty = true;
    m_appearanceType = 0;
    m_hLocked = 0;
    m_useCount = 0;
}

CItemSP CItem::Set(
    std::uint32_t ownerId,
    std::uint32_t itemType,
    std::int64_t amount,
    int enchant,
    int eroded,
    int bless,
    int ident,
    int wished,
    int warehouse,
    const ItemOptionKey& optionKey,
    int intensiveItemType,
    const BaseAttribute& baseAttribute,
    int inventorySlotIndex,
    int appearanceType,
    short hLocked,
    short useCount
) {
    L2_GUARD_FUNCTION();

    // Exact stores at current 0x004e8bec. No validation occurs here.
    m_ownerId = ownerId;
    m_itemType = itemType;
    m_amount = amount;
    m_enchant = enchant;
    m_eroded = eroded;
    m_bless = bless;
    m_ident = ident;
    m_wished = wished;
    m_warehouse = warehouse;
    m_optionKey = optionKey;
    m_intensiveItemType = intensiveItemType;
    m_baseAttribute = baseAttribute;
    m_inventorySlotIndex = inventorySlotIndex;
    m_appearanceType = appearanceType;
    m_hLocked = hLocked;
    m_useCount = useCount;
    return CItemSP(this, ObjectRefType::FunctionChain);
}

CItemSP CItem::SetTransation() {
    L2_GUARD_FUNCTION();
    ++m_inTransactions;
    m_deleted = false;
    return CItemSP(this, ObjectRefType::FunctionChain);
}

bool CItem::TransactDelete() {
    L2_GUARD_FUNCTION();
    if (m_inTransactions == 0)
        return false;
    m_deleted = true;
    return true;
}

bool CItem::RollbackTransation(CItem* rollbackItem) {
    L2_GUARD_FUNCTION();

    if (m_inTransactions == 0) {
        // Log.Add(
        //     LOG_ERROR, "Rollback Item Error, id[%u], user[%u]. Not in transation.", m_itemId, m_ownerId
        // )
        // Log.Add(LOG_ERROR,
        //     "[items] Cannot roll back item %u (owner %u): no active item transaction.",
        //     m_itemId, m_ownerId)
        L2_LOG_ERROR(Log,
            "[items] Cannot roll back item %u (owner %u): no active item transaction.",
            m_itemId, m_ownerId);
        return false;
    }

    const TransactType transactMode = rollbackItem->GetTrasactMode();
    if (transactMode == TransactType_Invalid) {
        // Log.Add(LOG_ERROR, "Item transaction ERROR [%s][%d]", "Item.cpp", 0x24b)
        // Log.Add(LOG_ERROR,
        //     "[items] Cannot roll back item %u (owner %u): rollback snapshot %u has an invalid transaction mode.",
        //     m_itemId, m_ownerId, rollbackItem->ItemID())
        L2_LOG_ERROR(Log,
            "[items] Cannot roll back item %u (owner %u): rollback snapshot %u has an invalid transaction mode.",
            m_itemId, m_ownerId, rollbackItem->ItemID());
        return false;
    }

    if (transactMode == TransactType_RollbackChanges) {
        SetDBID(rollbackItem->ItemID());
        Set(rollbackItem->OwnerID(), rollbackItem->ItemType(), rollbackItem->Amount(), rollbackItem->Enchant(),
            rollbackItem->Eroded(), rollbackItem->Bless(), rollbackItem->Ident(), rollbackItem->Wished(),
            rollbackItem->Warehouse(), rollbackItem->OptionKey(), rollbackItem->m_intensiveItemType,
            rollbackItem->Attribute(), rollbackItem->InventorySlotIndex(), rollbackItem->AppearanceType(),
            rollbackItem->HLocked(), rollbackItem->UseCount());
        m_deleted = false;
        --m_inTransactions;
        m_deleted = false;
        return true;
    }

    if (transactMode == TransactType_RollbackCreation) {
        if (rollbackItem->ItemID() != ItemID() && !rollbackItem->Delete(true, false)) {
            // Log.Add(LOG_ERROR, "Rollback item delete Error, id[%u], user[%u]", m_itemId, m_ownerId)
            // Log.Add(LOG_ERROR,
            //     "[items] Cannot delete rollback snapshot item %u while undoing creation of item %u (owner %u). Rollback processing continues.",
            //     rollbackItem->ItemID(), m_itemId, m_ownerId)
            L2_LOG_ERROR(Log,
                "[items] Cannot delete rollback snapshot item %u while undoing creation of item %u (owner %u); rollback processing continues.",
                rollbackItem->ItemID(), m_itemId, m_ownerId);
        }
        if (!Delete(true, false)) {
            // Log.Add(
            //     LOG_ERROR, "[%s][%d] item delete fail[%u][%u]", "Item.cpp", 0x273, m_itemId, m_itemType
            // )
            // Log.Add(LOG_ERROR,
            //     "[items] Cannot delete item %u (type %u) while rolling back creation. Local transaction state will still be cleared.",
            //     m_itemId, m_itemType)
            L2_LOG_ERROR(Log,
                "[items] Cannot delete item %u (type %u) while rolling back creation; local transaction state will still be cleared.",
                m_itemId, m_itemType);
            runtime::process::ReportNamedCallStack(L"RollbackTran", 2, false);
        }
        --m_inTransactions;
        m_deleted = false;
        return true;
    }

    // An unknown non-zero mode still clears the transaction after logging the error.
    // Log.Add(LOG_ERROR, "[%s][%d] Rollback ERROR", "Item.cpp", 0x279)
    // Log.Add(LOG_ERROR,
    //     "[items] Unknown rollback mode %d for item %u (owner %u). Local transaction state will still be cleared and rollback reported complete.",
    //     static_cast<int>(transactMode), m_itemId, m_ownerId)
    L2_LOG_ERROR(Log,
        "[items] Unknown rollback mode %d for item %u (owner %u); local transaction state will still be cleared and rollback reported complete.",
        static_cast<int>(transactMode), m_itemId, m_ownerId);
    m_deleted = false;
    --m_inTransactions;
    return true;
}

bool CItem::CommitTransation(CWareHouseSP rollbackWarehouse, CWareHouseSP warehouse, CItem* rollbackItem, bool forceSave) {
    L2_GUARD_FUNCTION();

    if (m_inTransactions == 0) {
        // Log.Add(LOG_ERROR, "Item transaction error. no transaction. dbid[%u]", m_itemId)
        // Log.Add(LOG_ERROR,
        //     "[items] Cannot commit item %u (owner %u): no active item transaction.",
        //     m_itemId, m_ownerId)
        L2_LOG_ERROR(Log,
            "[items] Cannot commit item %u (owner %u): no active item transaction.",
            m_itemId, m_ownerId);
        runtime::process::ReportNamedCallStack(L"CItem::CommitTransation", 2, false);
        return false;
    }

    if (m_deleted) {
        warehouse->PopItem(static_cast<int>(m_itemId));
        if (!Delete(true, false)) {
            // Log.Add(LOG_ERROR, "[%s][%d] item delete fail[%u]", "Item.cpp", 0x298, m_itemId)
            // Log.Add(LOG_ERROR,
            //     "[items] Cannot delete item %u in the database after removing it from its warehouse. Commit processing continues.",
            //     m_itemId)
            L2_LOG_ERROR(Log,
                "[items] Cannot delete item %u in the database after removing it from its warehouse; commit processing continues.",
                m_itemId);
        }
    } else if (rollbackItem->OwnerID() == OwnerID() && rollbackItem->Warehouse() == Warehouse()) {
        switch (rollbackItem->GetTrasactMode()) {
        case TransactType_RollbackChanges:
            Save(forceSave);
            break;
        case TransactType_RollbackCreation:
            warehouse->PushItem(this);
            Save(true);
            break;
        default:
            // The target's recovered vararg call appears to omit this value.
            // supplying it keeps the intended diagnostic without carrying
            // undefined vararg behavior into the portable process.
            // Log.Add(
            //     LOG_ERROR, "unknown item transaction type[%d].", static_cast<int>(rollbackItem->GetTrasactMode())
            // )
            // Log.Add(LOG_ERROR,
            //     "[items] Unknown transaction mode %d for item %u (owner %u). No save attempted in this branch, local transaction state will still be cleared.",
            //     static_cast<int>(rollbackItem->GetTrasactMode()), m_itemId, m_ownerId)
            L2_LOG_ERROR(Log,
                "[items] Unknown transaction mode %d for item %u (owner %u); no save attempted in this branch, local transaction state will still be cleared.",
                static_cast<int>(rollbackItem->GetTrasactMode()), m_itemId, m_ownerId);
            break;
        }
    } else {
        if (rollbackItem->OwnerID() != 0) {
            rollbackWarehouse->PopItem(static_cast<int>(rollbackItem->ItemID()));
        }
        warehouse->PushItem(this);
        Save(true);
    }

    --m_inTransactions;
    m_deleted = false;
    return true;
}

bool CItem::CommitTransation(CItem* rollbackItem, bool forceSave) {
    L2_GUARD_FUNCTION();

    auto reportFailure = [] { runtime::process::ReportNamedCallStack(L"CItem::CommitTransation", 2, false); };

    auto resolveWarehouse = [&](CItem& item, bool rollbackSide) -> CWareHouseSP {
        const int warehouseType = item.Warehouse();
        if (warehouseType == WareHouseType_Inventory || warehouseType == WareHouseType_CharWarehouse ||
            warehouseType == WareHouseType_Manor || warehouseType == WareHouseType_Mail) {
            CUserSP user =
                g_userDb.GetUser(static_cast<int>(item.OwnerID()), true, false, runtime::process::CurrentDatabaseId());
            if (!user) {
                // Log.Add(
                //     LOG_ERROR,
                //     rollbackSide ? "Cannot find user. user db id [%u] "
                //                    "item[addr:%p] dbid[%u]"
                //                  : "Cannot find user. user db id [%u] "
                //                    "this[addr:%p] item dbid[%u]",
                //     item.OwnerID(), static_cast<void*>(&item), item.ItemID()
                // )
                // Log.Add(LOG_ERROR,
                //     "[items] Cannot resolve %s warehouse for item %u: character %u is unavailable (warehouse type %d).",
                //     rollbackSide ? "rollback" : "destination", item.ItemID(), item.OwnerID(), warehouseType)
                L2_LOG_ERROR(Log,
                    "[items] Cannot resolve %s warehouse for item %u: character %u is unavailable (warehouse type %d).",
                    rollbackSide ? "rollback" : "destination", item.ItemID(), item.OwnerID(), warehouseType);
                reportFailure();
                return {};
            }
            CWareHouseSP result = user->GetWareHouse(warehouseType, true);
            if (!result) {
                // Log.Add(LOG_ERROR, "Cannot find warehouse. dbid[%u]", item.ItemID())
                // Log.Add(LOG_ERROR,
                //     "[items] Cannot resolve %s warehouse for item %u: character %u has no available warehouse of type %d.",
                //     rollbackSide ? "rollback" : "destination", item.ItemID(), item.OwnerID(), warehouseType)
                L2_LOG_ERROR(Log,
                    "[items] Cannot resolve %s warehouse for item %u: character %u has no available warehouse of type %d.",
                    rollbackSide ? "rollback" : "destination", item.ItemID(), item.OwnerID(), warehouseType);
                reportFailure();
            }
            return result;
        }

        if (warehouseType == WareHouseType_Pledge) {
            CPledgeSP pledge =
                g_pledgeDb.GetPledge(static_cast<int>(item.OwnerID()), false, runtime::process::CurrentDatabaseId());
            if (!pledge) {
                if (rollbackSide) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     "Cannot find pledge. pledge db id [%u] "
                    //     "item(addr:%p) dbid[%u]",
                    //     item.OwnerID(), static_cast<void*>(&item), item.ItemID()
                    // )
                    // Log.Add(LOG_ERROR,
                    //     "[items] Cannot resolve rollback warehouse for item %u: clan %u is unavailable.",
                    //     item.ItemID(), item.OwnerID())
                    L2_LOG_ERROR(Log,
                        "[items] Cannot resolve rollback warehouse for item %u: clan %u is unavailable.",
                        item.ItemID(), item.OwnerID());
                } else {
                    // Log.Add(LOG_ERROR, "Cannot find pledge. dbid[%u]", item.ItemID())
                    // Log.Add(LOG_ERROR,
                    //     "[items] Cannot resolve destination warehouse for item %u: clan %u is unavailable.",
                    //     item.ItemID(), item.OwnerID())
                    L2_LOG_ERROR(Log,
                        "[items] Cannot resolve destination warehouse for item %u: clan %u is unavailable.",
                        item.ItemID(), item.OwnerID());
                }
                reportFailure();
                return {};
            }
            CWareHouseSP result = pledge->GetWareHouse();
            if (!result) {
                // Log.Add(LOG_ERROR, "Cannot find warehouse. dbid[%u]", item.ItemID())
                // Log.Add(LOG_ERROR,
                //     "[items] Cannot resolve %s warehouse for item %u: clan %u has no available warehouse.",
                //     rollbackSide ? "rollback" : "destination", item.ItemID(), item.OwnerID())
                L2_LOG_ERROR(Log,
                    "[items] Cannot resolve %s warehouse for item %u: clan %u has no available warehouse.",
                    rollbackSide ? "rollback" : "destination", item.ItemID(), item.OwnerID());
                reportFailure();
            }
            return result;
        }

        if (warehouseType == WareHouseType_Castle) {
            CCastleSP castle = g_castleDb.GetCastle(static_cast<int>(item.OwnerID()));
            if (!castle) {
                // This pledge wording is present in the shipping item path.
                // Log.Add(LOG_ERROR, "Cannot find pledge. dbid[%u]", item.ItemID())
                // Log.Add(LOG_ERROR,
                //     "[items] Cannot resolve %s warehouse for item %u: castle %u is unavailable.",
                //     rollbackSide ? "rollback" : "destination", item.ItemID(), item.OwnerID())
                L2_LOG_ERROR(Log,
                    "[items] Cannot resolve %s warehouse for item %u: castle %u is unavailable.",
                    rollbackSide ? "rollback" : "destination", item.ItemID(), item.OwnerID());
                reportFailure();
                return {};
            }
            CWareHouseSP result = castle->GetWareHouse();
            if (!result) {
                // Log.Add(LOG_ERROR, "Cannot find warehouse. dbid[%u]", item.ItemID())
                // Log.Add(LOG_ERROR,
                //     "[items] Cannot resolve %s warehouse for item %u: castle %u has no available warehouse.",
                //     rollbackSide ? "rollback" : "destination", item.ItemID(), item.OwnerID())
                L2_LOG_ERROR(Log,
                    "[items] Cannot resolve %s warehouse for item %u: castle %u has no available warehouse.",
                    rollbackSide ? "rollback" : "destination", item.ItemID(), item.OwnerID());
                reportFailure();
            }
            return result;
        }

        // Log.Add(LOG_ERROR, "Cannot find warehouse. dbid[%u]", item.ItemID())
        // Log.Add(LOG_ERROR,
        //     "[items] Cannot resolve %s warehouse for item %u (owner %u): unsupported warehouse type %d.",
        //     rollbackSide ? "rollback" : "destination", item.ItemID(), item.OwnerID(), warehouseType)
        L2_LOG_ERROR(Log,
            "[items] Cannot resolve %s warehouse for item %u (owner %u): unsupported warehouse type %d.",
            rollbackSide ? "rollback" : "destination", item.ItemID(), item.OwnerID(), warehouseType);
        reportFailure();
        return {};
    };

    CWareHouseSP rollbackWarehouse;
    if (rollbackItem->GetTrasactMode() != TransactType_RollbackCreation) {
        rollbackWarehouse = resolveWarehouse(*rollbackItem, true);
        if (!rollbackWarehouse)
            return false;
    }

    CWareHouseSP warehouse = resolveWarehouse(*this, false);
    if (!warehouse)
        return false;
    return CommitTransation(rollbackWarehouse, warehouse, rollbackItem, forceSave);
}

void CItem::SetDBID(std::uint32_t itemId) {
    L2_GUARD_FUNCTION();
    m_itemId = itemId;
}

void CItem::SetAmount(std::int64_t amount) {
    // Current Cached.exe 0x004e8708, exact body af0820d720630ab8...
    L2_GUARD_FUNCTION();
    if (amount < 0) {
        // Log.Add(
        //     LOG_ERROR, "Set amount is try below 0.[DBID: %d][Amount: %lld]", static_cast<int>(m_itemId),
        //     static_cast<long long>(m_amount)
        // )
        // Log.Add(LOG_ERROR,
        //     "[items] Negative quantity %lld requested for item %u (previous quantity %lld). Clamping to zero.",
        //     static_cast<long long>(amount), m_itemId, static_cast<long long>(m_amount))
        L2_LOG_ERROR(Log,
            "[items] Negative quantity %lld requested for item %u (previous quantity %lld); clamping to zero.",
            static_cast<long long>(amount), m_itemId, static_cast<long long>(m_amount));
        // Current Cached.exe 0x004e87bf reports the CacheD guard stack before
        // clamping the invalid amount.
        runtime::process::ReportCallStack(LOG_ERROR, false);
        amount = 0;
    }
    m_amount = std::min(amount, kMaximumItemAmount);
}

void CItem::SetBaseAttribute(const BaseAttribute& baseAttribute) {
    // Current 0x004e8298 marks the persisted attribute dirty before the
    // tail-called BaseAttribute assignment.
    m_baseAttributeDirty = true;
    m_baseAttribute = baseAttribute;
}

int CItem::AssembleAttribute(const BaseAttribute& baseAttribute, CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hhhhhhhh", static_cast<int>(baseAttribute.m_nAttackType), static_cast<int>(baseAttribute.m_nAttackValue),
        static_cast<int>(baseAttribute.m_nDefendValue[0]), static_cast<int>(baseAttribute.m_nDefendValue[1]),
        static_cast<int>(baseAttribute.m_nDefendValue[2]), static_cast<int>(baseAttribute.m_nDefendValue[3]),
        static_cast<int>(baseAttribute.m_nDefendValue[4]), static_cast<int>(baseAttribute.m_nDefendValue[5])
    );
}

const unsigned char* CItem::DisassembleAttribute(const unsigned char* packet, BaseAttribute& baseAttribute) {
    L2_GUARD_FUNCTION();
    return Disassemble(
        packet, "hhhhhhhh", &baseAttribute.m_nAttackType, &baseAttribute.m_nAttackValue, &baseAttribute.m_nDefendValue[0],
        &baseAttribute.m_nDefendValue[1], &baseAttribute.m_nDefendValue[2], &baseAttribute.m_nDefendValue[3],
        &baseAttribute.m_nDefendValue[4], &baseAttribute.m_nDefendValue[5]
    );
}

void CItem::MakeShapeShiftLogInfo(char16_t* destination) {
    L2_GUARD_FUNCTION();
    if (m_appearanceType == 0) {
        FormatUtf16To(destination, 32, u"not_shape_shifted");
    } else {
        FormatUtf16To(destination, 32, u"shape_shifted_(%d)", m_appearanceType);
    }
}

void CItem::MakeShapeShiftAndVariationLogInfo(char16_t* destination) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, 256> shapeShift{};
    std::array<char16_t, 256> variation{};
    if (m_appearanceType == 0) {
        FormatUtf16To(shapeShift.data(), shapeShift.size(), u"not_shape_shifted");
    } else {
        FormatUtf16To(shapeShift.data(), shapeShift.size(), u"shape_shifted_(%d)", m_appearanceType);
    }
    FormatUtf16To(
        variation.data(), variation.size(), u"%d:%d:%d", m_intensiveItemType, m_optionKey.variationOption1,
        m_optionKey.variationOption2
    );
    FormatUtf16To(destination, 512, u"%ls/%ls", shapeShift.data(), variation.data());
}

int CItem::AssembleEnsoulInfo(CPacketHelperEx<20480>& packet, EnsoulInfo ensoulInfo[]) {
    L2_GUARD_FUNCTION();

    int assembled = 0;
    for (int slotType = 1; slotType <= 2; ++slotType) {
        EnsoulInfo& slots = ensoulInfo[slotType];

        // PE literals at 0x006b5d28 and 0x006af9ec are respectively "c"
        // and "d": the count is one byte, each option id four bytes.
        assembled += packet.Assemble("c", static_cast<int>(slots.size()));
        for (int slotIndex = 1; slotIndex <= static_cast<int>(slots.size()); ++slotIndex) {
            int optionId = 0;
            const auto found = slots.find(slotIndex);
            if (found == slots.end()) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[Ensoul] Can't find EnsoulInfo. SlotType[%d], "
                //     "SlotIndex[%d]",
                //     slotType, slotIndex
                // )
                // Log.Add(LOG_ERROR,
                //     "[items] Ensoul slot %d:%d is missing. Serializing option ID zero.",
                //     slotType, slotIndex)
                L2_LOG_ERROR(Log,
                    "[items] Ensoul slot %d:%d is missing; serializing option ID zero.",
                    slotType, slotIndex);
            } else {
                optionId = found->second;
            }
            assembled += packet.Assemble("d", optionId);
        }
    }
    return assembled;
}

// void CItem::MakeEnsoulLogInfo(wchar_t* destination) {
//     L2_GUARD_FUNCTION()
//     int length = 0
//     for (int slotType = 1. SlotType <= 2. ++slotType) {
//         const EnsoulInfo& slots = m_ensoulInfo[slotType]
//         for (int slotIndex = 1, slotIndex <= static_cast<int>(slots.size()), ++slotIndex) {
//             const auto found = slots.find(slotIndex)
//             if (found == slots.end())
//                 continue.
//             const int written = std::swprintf(destination + length, 64, L"(%d:%d:%d)", slotType, slotIndex, found->second)
//             if (written > 0)
//                 length += written.
//         }
//     }
// }
void CItem::MakeEnsoulLogInfo(char16_t* destination, std::size_t capacity) {
    L2_GUARD_FUNCTION();
    if (!destination || capacity == 0) {
        // Log.Add(LOG_ERROR, "[items] Cannot format ensoul description for item %u: no output buffer.", m_itemId)
        L2_LOG_ERROR(Log, "[items] Cannot format ensoul description for item %u: no output buffer.", m_itemId);
        return;
    }
    destination[0] = u'\0';
    std::size_t length = 0;
    for (int slotType = 1; slotType <= 2; ++slotType) {
        const EnsoulInfo& slots = m_ensoulInfo[slotType];
        for (int slotIndex = 1; slotIndex <= static_cast<int>(slots.size()); ++slotIndex) {
            const auto found = slots.find(slotIndex);
            if (found == slots.end())
                continue;
            std::array<char16_t, 64> entry{};
            const int written = FormatUtf16To(entry.data(), entry.size(), u"(%d:%d:%d)", slotType, slotIndex, found->second);
            if (written <= 0 || static_cast<std::size_t>(written) >= entry.size()) {
                // Log.Add(LOG_ERROR, "[items] Cannot format ensoul slot %d:%d for item %u, description is incomplete.",
                //     slotType, slotIndex, m_itemId)
                L2_LOG_ERROR(Log, "[items] Cannot format ensoul slot %d:%d for item %u; description is incomplete.",
                    slotType, slotIndex, m_itemId);
                return;
            }
            if (static_cast<std::size_t>(written) >= capacity - length) {
                constexpr char16_t marker[] = u"[truncated]";
                const std::size_t markerLength = std::min(capacity - 1, sizeof(marker) / sizeof(char16_t) - 1);
                const std::size_t prefixLength = std::min(length, capacity - 1 - markerLength);
                std::char_traits<char16_t>::copy(destination + prefixLength, marker, markerLength);
                destination[prefixLength + markerLength] = u'\0';
                // Log.Add(LOG_WARN, "[items] Ensoul description for item %u truncated to fit a %zu-character buffer.",
                //     m_itemId, capacity)
                L2_LOG_WARN(Log, "[items] Ensoul description for item %u truncated to fit a %zu-character buffer.",
                    m_itemId, capacity);
                return;
            }
            std::char_traits<char16_t>::copy(destination + length, entry.data(), static_cast<std::size_t>(written) + 1);
            length += static_cast<std::size_t>(written);
        }
    }
}

void CItem::SetEnsoulInfo(int slotType, int slotIndex, int optionId) {
    // Current Cached.exe 0x004e91ec, exact body 303fdf414320b96d...
    L2_GUARD_FUNCTION();
    if (slotType < 1 || slotType > 2)
        return;
    m_ensoulInfo[slotType].insert_or_assign(slotIndex, optionId);
}

void CItem::CacheItem(CItem* item) {
    // Current Cached.exe 0x004e9814, exact body 94a041fcee7e3799...
    L2_GUARD_FUNCTION();
    g_itemCache.Insert(static_cast<int>(item->m_itemId), item, nullptr, -1);
}

bool CItem::CopyByDeleteChar() {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    return ExecuteItemWrite(
        connection,
        L"{Call lin_CopyItemByDeletedChar "
        L"(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}",
        m_ownerId, m_itemType, m_amount, m_enchant, m_eroded, m_bless, m_ident, m_wished, m_warehouse, m_itemId, m_optionKey,
        m_intensiveItemType, m_inventorySlotIndex, m_hLocked, m_useCount
    );
}

bool CItem::Delete(bool hardDelete, bool premiumRelated) {
    L2_GUARD_FUNCTION();

    // Current 0x004eb1b4 treats a non-hard delete solely as removal from the
    // process-wide cache and returns true even when the key was absent.
    if (!hardDelete) {
        g_itemCache.DeleteObject(static_cast<int>(m_itemId), nullptr, -1);
        return true;
    }

    int databaseResult = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&databaseResult);
    constexpr const wchar_t* statement = L"{Call lin_DeleteItem (?, NULL)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_itemId);

    const bool executedAndFetched = connection.ExecutePrepared(statement) && connection.Fetch();
    if (executedAndFetched && databaseResult == 1) {
        if (g_readFile.IsPremiumItem(static_cast<int>(m_itemType))) {
            // Current code supplies log type 1 and reason 0 or 2. The enum
            // type names survive, but their enumerator identifiers do not.
            LogPremiumRelated(
                static_cast<PremiumItemLogType>(1), m_amount, 0, static_cast<PremiumItemLogReason>(premiumRelated ? 2 : 0)
            );
        }
        g_itemCache.DeleteObject(static_cast<int>(m_itemId), nullptr, -1);
        return true;
    }

    // Log.Add(
    //     LOG_ERROR, "Item delete Error, id[%u] ret : [%d] nRes : [%d]", m_itemId, executedAndFetched ? 1 : 0,
    //     databaseResult
    // )
    // Log.Add(LOG_ERROR,
    //     "[items] Cannot delete item %u in database %d: %s (result %d). Item-cache removal skipped.",
    //     m_itemId, runtime::process::CurrentDatabaseId(), executedAndFetched ? "delete procedure did not return success" : "no procedure result was fetched", databaseResult)
    L2_LOG_ERROR(Log,
        "[items] Cannot delete item %u in database %d: %s (result %d); item-cache removal skipped.",
        m_itemId, runtime::process::CurrentDatabaseId(), executedAndFetched ? "delete procedure did not return success" : "no procedure result was fetched", databaseResult);
    runtime::process::ReportNamedCallStack(L"ItemDelete", 2, false);
    return false;
}

std::int64_t CItem::ChangeAmount(std::int64_t amount) {
    L2_GUARD_FUNCTION();

    // Current 0x004ea06c uses the legacy CacheD scale where
    // stackable is 4, not SharedFactory's ICT_STACKABLE value 2.
    if (g_readFile.GetConsumeType(static_cast<int>(m_itemType)) != 4) {
        // Log.Add(LOG_ERROR, "[%s][%d] CItem::ChangeAmount is for stackable", "Item.cpp", 420)
        // Log.Add(LOG_ERROR,
        //     "[items] Cannot change quantity for item %u (type %u): item type is not stackable. Request rejected.",
        //     m_itemId, m_itemType)
        L2_LOG_ERROR(Log,
            "[items] Cannot change quantity for item %u (type %u): item type is not stackable; request rejected.",
            m_itemId, m_itemType);
        return -1;
    }

    std::int64_t changedAmount = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&changedAmount);
    const std::int64_t databaseDelta = runtime::NegateInt64Wrapping(amount);
    const int changeType = 1;
    constexpr const wchar_t* statement = L"{Call lin_AmountChange (?, ?, ?, ?)}";
    if (ExecuteAmountChange(connection, statement, m_ownerId, m_itemId, databaseDelta, changeType) && connection.Fetch()) {
        m_lateSavingAmount = 0;
        return changedAmount;
    }

    // Log.Add(LOG_ERROR, "[%s][%d] fail to change amount for late saving item", "Item.cpp", 437)
    // Log.Add(LOG_ERROR,
    //     "[items] No quantity-change result for item %u (owner %u) in database %d. Requested amount %lld, database delta %lld, deferred amount retained.",
    //     m_itemId, m_ownerId, runtime::process::CurrentDatabaseId(), static_cast<long long>(amount), static_cast<long long>(databaseDelta))
    L2_LOG_ERROR(Log,
        "[items] No quantity-change result for item %u (owner %u) in database %d; requested amount %lld, database delta %lld, deferred amount retained.",
        m_itemId, m_ownerId, runtime::process::CurrentDatabaseId(), static_cast<long long>(amount), static_cast<long long>(databaseDelta));
    return -1;
}

bool CItem::SaveBaseAttribute() {
    L2_GUARD_FUNCTION();
    if (!m_baseAttributeDirty)
        return false;

    // Current 0x004ea22c clears this before attempting SQL and never restores
    // it on failure.
    m_baseAttributeDirty = false;
    DBConn connection(runtime::process::CurrentDatabaseId());
    return ExecuteBaseAttributeWrite(
        connection, L"{Call lin_UpdateUserItemAttribute (?, ?, ?, ?, ?, ?, ?, ?, ?)}", m_itemId, m_baseAttribute
    );
}

int CItem::GetDuration() {
    L2_GUARD_FUNCTION();
    int duration = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&duration);
    connection.Prepare(L"{Call lin_GetItemDuration (?)}");
    connection.BindParameter(&m_itemId);
    if (connection.ExecutePrepared(L"{Call lin_GetItemDuration (?)}")) {
        connection.Fetch();
    }
    return duration;
}

double CItem::LoadAgathionEnergy() {
    // Current unnamed 0x004eaad8 has no guard frame. It binds the result
    // before Prepare, binds m_itemId as the sole input, and returns zero
    // unless both ExecutePrepared and Fetch succeed.
    double energy = 0.0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&energy);
    constexpr const wchar_t* statement = L"{Call lin_BrLoadItemAgathionEnergy (?) }";
    connection.Prepare(statement);
    connection.BindParameter(&m_itemId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        return 0.0;
    }
    return energy;
}

void CItem::InitExtraInfo() {
    L2_GUARD_FUNCTION();

    const int duration = g_readFile.GetDurationItemDuration(static_cast<int>(m_itemType));
    const int period = g_readFile.GetPeriodItemPeriod(static_cast<int>(m_itemType));

    if (duration > 0) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_UpdateItemDuration (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&m_itemId);
        connection.BindParameter(&duration);
        connection.ExecutePrepared(statement);
    }

    if (period > 0) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        const std::time_t now = std::time(nullptr);
        const int registrationTime = now < 0 || now > INT32_MAX ? -1 : static_cast<int>(now);
        const std::uint32_t itemType = m_itemType;
        RegisterPeriodicItem(
            connection, L"{Call lin_RegisterPeriodicItem (?, ?, ?, ?)}", m_itemId, registrationTime, period, itemType
        );
    }
}

void CItem::LogPremiumRelated(
    PremiumItemLogType logType, std::int64_t amount, std::int64_t secondaryAmount, PremiumItemLogReason reason
) {
    L2_GUARD_FUNCTION();

    // Current 0x004ea5be reads the request database ID from TLS +0x10.
    const int worldId = runtime::process::CurrentDatabaseId();
    CUserSP user = g_userDb.GetUser(static_cast<int>(m_ownerId), true, true, worldId);
    const unsigned int accountId = user ? static_cast<unsigned int>(user->GetAccountID()) : 0U;

    DBConn connection(worldId);
    const wchar_t* statement = nullptr;
    if (static_cast<int>(logType) == 0) {
        statement = L"{Call lin_LogForPremiumItemCreate (?,?,?,?,?,?,?)}";
    } else if (static_cast<int>(logType) == 1) {
        statement = L"{Call lin_LogForPremiumItemDelete (?,?,?,?,?,?,?)}";
    }
    if (!statement)
        return;

    const int ownerId = static_cast<int>(m_ownerId);
    const int itemId = static_cast<int>(m_itemId);
    const unsigned int itemType = m_itemType;
    const int reasonValue = static_cast<int>(reason);
    ExecutePremiumItemLog(connection, statement, accountId, ownerId, itemId, amount, secondaryAmount, reasonValue, itemType);
}

void CItem::SetInventorySlotIndex(int inventorySlotIndex) {
    // Current Cached.exe 0x004ea69c, exact body eef4bd4a157d6da9...
    L2_GUARD_FUNCTION();
    if (m_inventorySlotIndex == inventorySlotIndex)
        return;

    m_inventorySlotIndex = inventorySlotIndex;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateItemSlotIndex (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_itemId);
    connection.BindParameter(&m_inventorySlotIndex);
    connection.ExecutePrepared(statement);
}

void CItem::SetApperanceType(int appearanceType, int expiration, ShapeShiftingJobType jobType) {
    L2_GUARD_FUNCTION();
    m_appearanceType = appearanceType;

    std::int32_t updateTime = 0;
    switch (static_cast<int>(jobType)) {
    case 0:
        updateTime = static_cast<std::int32_t>(std::time(nullptr));
        break;
    case 3:
        updateTime = -1;
        break;
    default:
        updateTime = 0;
        break;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateShapeShiftedItem (?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_itemId);
    connection.BindParameter(&m_appearanceType);
    connection.BindParameter(&expiration);
    connection.BindParameter(&updateTime);
    connection.ExecutePrepared(statement);
}

bool CItem::LoadEnsoulInfo() {
    L2_GUARD_FUNCTION();
    int slotType = 0;
    int slotIndex = 0;
    int optionId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&slotType);
    connection.Bind(&slotIndex);
    connection.Bind(&optionId);
    constexpr const wchar_t* statement = L"{Call lin_GetItemEnsoulInfo(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_itemId);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            SetEnsoulInfo(slotType, slotIndex, optionId);
        }
    }

    // Current 0x004eabc4 returns true even when execution fails.
    return true;
}

void CItem::SetEnsoulInfo(EnsoulInfo ensoulInfo[]) {
    L2_GUARD_FUNCTION();
    constexpr const wchar_t* statement = L"{Call lin_SetItemEnsoulInfo(?, ?, ?, ?)}";
    for (int slotType = 1; slotType <= 2; ++slotType) {
        for (const auto& entry : ensoulInfo[slotType]) {
            const int slotIndex = entry.first;
            const int optionId = entry.second;
            DBConn connection(runtime::process::CurrentDatabaseId());
            connection.Prepare(statement);
            connection.BindParameter(&m_itemId);
            connection.BindParameter(&slotType);
            connection.BindParameter(&slotIndex);
            connection.BindParameter(&optionId);
            if (connection.ExecutePrepared(statement)) {
                SetEnsoulInfo(slotType, slotIndex, optionId);
            }
        }
    }
}

CItemSP CItem::Load(std::uint32_t itemId) {
    L2_GUARD_FUNCTION();
    CItemSP result = g_itemCache.FindObject(static_cast<int>(itemId));
    if (result)
        return result;

    std::uint32_t ownerId = 0;
    std::uint32_t itemType = 0;
    long long amount = 0;
    int enchant = 0;
    int eroded = 0;
    int bless = 0;
    int ident = 0;
    int wished = 0;
    int warehouse = 0;
    ItemOptionKey optionKey;
    int intensiveItemType = 0;
    int inventorySlotIndex = -1;
    int appearanceType = 0;
    BaseAttribute baseAttribute;
    short hLocked = 0;
    short useCount = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&ownerId);
    connection.Bind(&itemType);
    connection.Bind(&amount);
    connection.Bind(&enchant);
    connection.Bind(&eroded);
    connection.Bind(&bless);
    connection.Bind(&ident);
    connection.Bind(&wished);
    connection.Bind(&warehouse);
    connection.Bind(&optionKey.variationOption1);
    connection.Bind(&optionKey.variationOption2);
    connection.Bind(&intensiveItemType);
    connection.Bind(&inventorySlotIndex);
    connection.Bind(&appearanceType);
    connection.Bind(&baseAttribute.m_nAttackType);
    connection.Bind(&baseAttribute.m_nAttackValue);
    for (short& value : baseAttribute.m_nDefendValue) {
        connection.Bind(&value);
    }
    connection.Bind(&hLocked);
    connection.Bind(&useCount);

    constexpr const wchar_t* statement = L"{Call lin_LoadItemByItemId (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&itemId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        return result;
    }

    CItem* loaded = new CItem(
        itemId, ownerId, itemType, amount, enchant, eroded, bless, ident, wished, warehouse, optionKey, intensiveItemType,
        baseAttribute, inventorySlotIndex, appearanceType, hLocked, useCount
    );
    loaded->LoadEnsoulInfo();
    g_itemCache.Insert(static_cast<int>(itemId), loaded, nullptr, -1);
    result.SetObject(loaded, ObjectRefType::FunctionChain);
    L2_RELEASE(loaded, ObjectRefType::CreateDelete);
    return result;
}

bool CItem::Save(bool force) {
    L2_GUARD_FUNCTION();
    if (!force && GetCacheDConfiguration().writeCache) {
        m_writeCachePending = true;
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    if (!ExecuteItemWrite(
            connection,
            L"{Call lin_UpdateUserItem "
            L"(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)}",
            m_ownerId, m_itemType, m_amount, m_enchant, m_eroded, m_bless, m_ident, m_wished, m_warehouse, m_itemId,
            m_optionKey, m_intensiveItemType, m_inventorySlotIndex, m_hLocked, m_useCount
        )) {
        return false;
    }

    SaveBaseAttribute();
    m_lateSavingAmount = 0;
    return true;
}

CItemSP CItem::Copy() {
    L2_GUARD_FUNCTION();
    CItem* copy = new CItem(
        m_itemId, m_ownerId, m_itemType, m_amount, m_enchant, m_eroded, m_bless, m_ident, m_wished, m_warehouse, m_optionKey,
        m_intensiveItemType, m_baseAttribute, m_inventorySlotIndex, m_appearanceType, m_hLocked, m_useCount
    );
    copy->SetEnsoulInfo(m_ensoulInfo);
    CItemSP result(copy, ObjectRefType::FunctionChain);
    L2_RELEASE(copy, ObjectRefType::CreateDelete);
    return result;
}

std::uint32_t CItem::ItemID() const noexcept {
    // Current Cached.exe 0x004e8260: return dword [this+0x1c].
    return m_itemId;
}

std::uint32_t CItem::OwnerID() const noexcept {
    // Current Cached.exe 0x004e8264: return dword [this+0x20].
    return m_ownerId;
}

void CItem::setOwnerID(std::uint32_t ownerId) noexcept {
    // Inlined at current CPetWarehouse::PetGetItem 0x005f1ffd.
    if (m_ownerId != ownerId) {
        m_inventorySlotIndex = -1;
    }
    m_ownerId = ownerId;
}

std::uint32_t CItem::ItemType() const noexcept {
    // Current Cached.exe 0x004e8268: return dword [this+0x24].
    return m_itemType;
}

std::int64_t CItem::Amount() const noexcept {
    // Current Cached.exe 0x004e826c: return qword [this+0x28].
    return m_amount;
}

int CItem::Enchant() const noexcept {
    // Current Cached.exe 0x004e8274: return dword [this+0x30].
    return m_enchant;
}

void CItem::SetEnchant(int enchantLevel) noexcept {
    // Original inline name kept by C1. Current artifact-enchant callers
    // compile this to the direct CItem +0x30 stores at 0x004c1dcc/0x004c1952.
    m_enchant = enchantLevel;
}

int CItem::Eroded() const noexcept {
    // Current Cached.exe 0x004e8278: return dword [this+0x34].
    return m_eroded;
}

int CItem::Bless() const noexcept {
    // Current Cached.exe 0x004e827c: return dword [this+0x38].
    return m_bless;
}

int CItem::Ident() const noexcept {
    // Current Cached.exe 0x004e8280: return dword [this+0x3c].
    return m_ident;
}

int CItem::Wished() const noexcept {
    // Current Cached.exe 0x004e8284: return dword [this+0x40].
    return m_wished;
}

int CItem::Warehouse() const noexcept {
    // Current Cached.exe 0x004e8288: return dword [this+0x44].
    return m_warehouse;
}

bool CItem::SetWarehouse(int warehouse) noexcept {
    // Current Cached.exe 0x004e828c stores dword [this+0x44] and returns true.
    m_warehouse = warehouse;
    return true;
}

TransactType CItem::GetTrasactMode() const noexcept { return m_transactMode; }

void CItem::SetTrasactMode(TransactType mode) noexcept {
    // Current Cached.exe 0x004e8294 stores dword [this+0x18]. The historical
    // misspelling is the surviving C1 source identifier.
    m_transactMode = mode;
}

ItemChangeScope CItem::ChangeScope() const noexcept { return m_changeScope; }

void CItem::SetChangeScope(ItemChangeScope scope) noexcept { m_changeScope = scope; }

std::int64_t CItem::PrivateStorePrice() const noexcept { return m_privateStorePrice; }

void CItem::SetPrivateStorePrice(std::int64_t price) noexcept { m_privateStorePrice = price; }

std::int64_t CItem::AuditAmount() const noexcept { return m_auditAmount; }

void CItem::SetAuditAmount(std::int64_t amount) noexcept { m_auditAmount = amount; }

bool CItem::IsUsable() const noexcept { return m_isUsable; }

void CItem::SetUsable(bool usable) noexcept { m_isUsable = usable; }

bool CItem::Deleted() const noexcept { return m_deleted; }

int CItem::InventorySlotIndex() const noexcept { return m_inventorySlotIndex; }

short CItem::HLocked() const noexcept { return m_hLocked; }

short CItem::UseCount() const noexcept { return m_useCount; }

const ItemOptionKey& CItem::OptionKey() const noexcept { return m_optionKey; }

const BaseAttribute& CItem::Attribute() const noexcept { return m_baseAttribute; }

int CItem::AppearanceType() const noexcept { return m_appearanceType; }

const CItem::EnsoulInfo* CItem::EnsoulSlots() const noexcept { return m_ensoulInfo; }
