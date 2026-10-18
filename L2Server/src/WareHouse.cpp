#include "WareHouse.h"
#include "Creature.h"
#include "DB.h"
#include "Item.h"
#include "ItemAssembler.h"
#include "NPC.h"
#include "ObjectDB.h"
#include "Pet.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/SlotType.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "Shared/src/WarehouseValidation.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>


CWareHouseDeposit::CWareHouseDeposit(User* const user, CCreature* const npc, const int warehouseType, const int ownerId) :
    m_nActorServerId(0), m_nNpcServerId(0) {
    // Current L2Server 0x00c4ccf4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        m_nActorServerId = 0;
    } else {
        L2_ADD_REF(user, ObjectRefType::Anonymous);
        // Current 0x00c4cdd1 reads CObject::m_nId at +0x18 after AddRef.
        m_nActorServerId = static_cast<std::int32_t>(user->Id());
    }

    if (npc == nullptr) {
        m_nNpcServerId = 0;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Not NPC.",
        //     "WareHouse.cpp",
        //     0x32)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Not NPC.",
        //     "WareHouse.cpp",
        //     0x32)
        L2_LOG_ERROR(Log, "Warehouse deposit created without a keeper: NPC pointer is null.");
    } else if (!npc->IsNPC()) {
        m_nNpcServerId = 0;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Not NPC.",
        //     "WareHouse.cpp",
        //     0x2c)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Not NPC.",
        //     "WareHouse.cpp",
        //     0x2c)
        L2_LOG_ERROR(Log, "Warehouse deposit created without a keeper: supplied creature is not an NPC.");
    } else {
        L2_ADD_REF(npc, ObjectRefType::Anonymous);
        // Current 0x00c4ce06 reads CObject::m_nId at +0x18 after AddRef.
        m_nNpcServerId = static_cast<std::int32_t>(npc->Id());
    }
    m_nWarehouseType = warehouseType;
    m_nOwnerID = ownerId;
    m_nWarehouseID = 1;
}

CWareHouseDeposit::~CWareHouseDeposit() {
    // Current L2Server 0x00c4d1f8.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = GetUserSP();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x4d)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x4d)
        L2_LOG_ERROR(
            Log, "Warehouse deposit cleanup cannot release player object ID %d: player unavailable.",
            static_cast<int>(m_nActorServerId)
        );
    } else {
        L2_RELEASE(user, ObjectRefType::Anonymous);
    }

    CSPointer<CNPC> npc = GetNPCSP();
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x57)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x57)
        L2_LOG_ERROR(
            Log, "Warehouse deposit cleanup cannot release NPC object ID %d: keeper unavailable.",
            static_cast<int>(m_nNpcServerId)
        );
    } else {
        L2_RELEASE(npc, ObjectRefType::Anonymous);
    }
}

CSPointer<User> CWareHouseDeposit::GetUserSP() const {
    // Current L2Server 0x00c4cfb8, original WareHouse.h line 0x4b.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_nActorServerId);
    if (creature != nullptr && creature->IsPC()) {
        return CSPointer<User>(creature->CastUser(), ObjectRefType::FunctionLocal);
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid user[%d]",
    //     L"c:\\work\\lineage2build\\server\\main\\l2server\\WareHouse.h",
    //     0x4b,
    //     m_nActorServerId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid user[%d]",
    //     L"c:\\work\\lineage2build\\server\\main\\l2server\\WareHouse.h",
    //     0x4b,
    //     m_nActorServerId)
    // Noise: every caller logs the unavailable object with operation context.
    // L2_LOG_ERROR(Log,
    //     "Warehouse deposit player lookup failed for object ID %d: creature unavailable or not a player.",
    //     static_cast<int>(m_nActorServerId))
    return {};
}

CSPointer<CNPC> CWareHouseDeposit::GetNPCSP() const {
    // Current L2Server 0x00b7c3a4, original WareHouse.h line 0x5b.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_nNpcServerId);
    if (creature != nullptr && creature->IsNPC()) {
        return CSPointer<CNPC>(creature->CastNPC(), ObjectRefType::FunctionLocal);
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid NPC[%d]",
    //     L"c:\\work\\lineage2build\\server\\main\\l2server\\WareHouse.h",
    //     0x5b,
    //     m_nNpcServerId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid NPC[%d]",
    //     L"c:\\work\\lineage2build\\server\\main\\l2server\\WareHouse.h",
    //     0x5b,
    //     m_nNpcServerId)
    L2_LOG_ERROR(
        Log, "Warehouse deposit keeper lookup failed for object ID %d: creature unavailable or not an NPC.",
        static_cast<int>(m_nNpcServerId)
    );
    return {};
}

int CWareHouseDeposit::GetWarehouseKeeperClassId() {
    // Current L2Server 0x00b7c464, original WareHouse.h line 0x82.
    CSPointer<CNPC> npc = GetNPCSP();
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find NPC.",
        //     "c:\\work\\lineage2build\\server\\main\\l2server\\WareHouse.h",
        //     0x82)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find NPC.",
        //     "c:\\work\\lineage2build\\server\\main\\l2server\\WareHouse.h",
        //     0x82)
        L2_LOG_ERROR(
            Log, "Warehouse keeper class lookup failed for NPC object ID %d; returning zero.",
            static_cast<int>(m_nNpcServerId)
        );
        return 0;
    }
    return npc->GetClassId();
}

void CWareHouseDeposit::SendList(const WareHouseClass warehouseClass) {
    // Current L2Server 0x00c4d3a4.
    L2_GUARD_FUNCTION();
    CSPointer<CNPC> npc = GetNPCSP();
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x67)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x67)
        L2_LOG_WARN(
            Log, "Cannot request warehouse item list: NPC object ID %d is unavailable.", static_cast<int>(m_nNpcServerId)
        );
        return;
    }

    CSPointer<User> user = GetUserSP();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x6e)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x6e)
        L2_LOG_WARN(
            Log, "Cannot request warehouse item list: player object ID %d is unavailable.",
            static_cast<int>(m_nActorServerId)
        );
        return;
    }

    const int npcClassId = m_nWarehouseType == 3 ? npc->m_data->m_nClassID : 0;
    db.RequestLoadWareHouseItems(user.get(), m_nWarehouseID, nullptr, npcClassId, MemoryObjectID(), warehouseClass, 1);
}

void CWareHouseDeposit::SendListEx(const WareHouseClass) {
    // Current L2Server 0x00c4d630. The class argument is intentionally
    // unused by the current extended request.
    L2_GUARD_FUNCTION();
    CSPointer<CNPC> npc = GetNPCSP();
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x85)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x85)
        L2_LOG_WARN(
            Log, "Cannot request extended warehouse item list: NPC object ID %d is unavailable.",
            static_cast<int>(m_nNpcServerId)
        );
        return;
    }

    CSPointer<User> user = GetUserSP();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x8c)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x8c)
        L2_LOG_WARN(
            Log, "Cannot request extended warehouse item list: player object ID %d is unavailable.",
            static_cast<int>(m_nActorServerId)
        );
        return;
    }

    db.RequestLoadWarehouseEx(m_nWarehouseType, m_nOwnerID, user.get(), MemoryObjectID(), 1);
}

bool CWareHouseDeposit::CheckItemRestrict(const unsigned char* packet) {
    // Current L2Server 0x00c4d830.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = GetUserSP();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x1d2)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x1d2)
        L2_LOG_WARN(
            Log, "Cannot check warehouse deposit item restrictions: player object ID %d is unavailable.",
            static_cast<int>(m_nActorServerId)
        );
        return false;
    }

    int itemCount = 0;
    const unsigned char* cursor = Disassemble(packet, "d", &itemCount);
    for (int index = 0; index < itemCount; ++index) {
        int databaseId = 0;
        std::int64_t amount = 0;
        cursor = Disassemble(cursor, "dQ", &databaseId, &amount);
        CItem* const item = user->m_inv.GetByDBID(databaseId);
        if (item != nullptr &&
            ((m_nWarehouseType != 1 && m_nWarehouseType != 2 && m_nWarehouseType != 3) || item->m_data->GetLocked() == 0) &&
            CItemRestrict::Inst().CheckItemRestrict(item->GetClassId())) {
            return true;
        }
    }
    return false;
}

bool CWareHouseDeposit::IsInRange() {
    // Current L2Server 0x00c4da9c.
    L2_GUARD_FUNCTION();
    CSPointer<CNPC> npc = GetNPCSP();
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x2fc)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x2fc)
        L2_LOG_WARN(
            Log, "Cannot check warehouse keeper distance: NPC object ID %d is unavailable.", static_cast<int>(m_nNpcServerId)
        );
        return false;
    }

    CSPointer<User> user = GetUserSP();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x303)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x303)
        L2_LOG_WARN(
            Log, "Cannot check warehouse keeper distance: player object ID %d is unavailable.",
            static_cast<int>(m_nActorServerId)
        );
        return false;
    }
    bool inRange = false;
    {
        GuardHelper aliveGuard(L"inrange_alive", CThreadManager::GetThreadID());
        if (npc->m_bLeft) {
            return false;
        }

        {
            GuardHelper actorGuard(L"inrange_actor", CThreadManager::GetThreadID());
            const FVector userPosition = user->GetPos();

            {
                GuardHelper targetGuard(L"inrange_target", CThreadManager::GetThreadID());
                const FVector npcPosition = npc->GetPos();

                {
                    GuardHelper sqrtGuard(L"inrange_sqrt", CThreadManager::GetThreadID());
                    const double deltaX = userPosition.x - npcPosition.x;
                    const double deltaY = userPosition.y - npcPosition.y;
                    inRange = std::sqrt(deltaX * deltaX + deltaY * deltaY) <= 250.0;
                }
            }
        }
    }
    return inRange;
}

void CWareHouseDeposit::Request(const unsigned char* packet) {
    // Current L2Server 0x00c4ed54.
    L2_GUARD_FUNCTION();
    if (CheckItemRestrict(packet)) {
        return;
    }
    if (m_nWarehouseType > 0) {
        RequestEx(packet);
        return;
    }

    CSPointer<User> user = GetUserSP();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x20b)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x20b)
        L2_LOG_WARN(
            Log, "Warehouse deposit request rejected: player object ID %d is unavailable.",
            static_cast<int>(m_nActorServerId)
        );
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    if (g_nCountryCode == 1 && socket != nullptr && socket->m_bEconomicRestriction) {
        user->SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }
    if (runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 1) == 1) {
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    CSPointer<CNPC> npc = GetNPCSP();
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x220)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x220)
        L2_LOG_WARN(
            Log, "Warehouse deposit request rejected: keeper object ID %d is unavailable.", static_cast<int>(m_nNpcServerId)
        );
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    int itemCount = 0;
    const unsigned char* cursor = Disassemble(packet, "d", &itemCount);
    if (itemCount > 200) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]item count is wrong!!!.",
        //     "WareHouse.cpp",
        //     0x22b)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]item count is wrong!!!.",
        //     "WareHouse.cpp",
        //     0x22b)
        L2_LOG_WARN(Log, "Warehouse deposit request rejected: %d item entries exceed the limit of 200.", itemCount);
        socket->Send("cd", 0x43, -3);
        {
            L2_GUARD(*user->m_lock);
            user->ReleaseEconomy();
        }
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }
    if (itemCount == 0) {
        socket->Send("cd", 0x43, -3);
        {
            L2_GUARD(*user->m_lock);
            user->ReleaseEconomy();
        }
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    std::set<int> databaseIds;
    std::array<char, 20480> requestPacket;
    int requestLength = 0;
    int depositItemCount = 0;
    std::int64_t requestedAdena = 0;
    bool accepted = false;

    const bool requestValid = [&] {
        L2_READ_GUARD(user->m_inv);
        for (int index = 0; index < itemCount; ++index) {
            int databaseId = 0;
            std::int64_t amount = 0;
            cursor = Disassemble(cursor, "dQ", &databaseId, &amount);

            if (!databaseIds.insert(databaseId).second) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d][Kick] Deposit to warehouse. but item is "
                //     L"duplicated. user[%s] item[%d:%d]",
                //     L"WareHouse.cpp",
                //     0x257,
                //     user->GetName(),
                //     databaseId,
                //     amount)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d][Kick] Deposit to warehouse. but item is "
                //     L"duplicated. user[%s] item[%d:%d]",
                //     L"WareHouse.cpp",
                //     0x257,
                //     user->GetName(),
                //     databaseId,
                //     amount)
                L2_LOG_WARN(
                    Log,
                    "Warehouse deposit request contains duplicate item database ID %d "
                    "(quantity %lld, player object ID %d); scheduling connection closure.",
                    databaseId, static_cast<long long>(amount), static_cast<int>(m_nActorServerId)
                );
                socket->SetCloseTimer();
                {
                    L2_GUARD(*user->m_lock);
                    user->ReleaseEconomy();
                }
                return false;
            }

            CItem* const item = user->m_inv.GetByDBID(databaseId);
            if (item == nullptr) {
                break;
            }

            const ItemConsumeType consumeType = item->m_data->m_ItemConsumeType;
            bool validAmount = false;
            if (consumeType == ICT_ASSET) {
                validAmount = amount >= 1 && amount <= 999999999999LL;
            } else if (amount >= 1 && amount <= 1000000) {
                std::array<char, 32> amountText{};
                const auto converted = std::to_chars(amountText.data(), amountText.data() + 11, amount);
                if (converted.ec == std::errc{}) {
                    *converted.ptr = '\0';
                    validAmount = std::strlen(amountText.data()) <= 8;
                }
            }
            if (!validAmount) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] invalid amount (Request)",
                //     L"WareHouse.cpp",
                //     0x274)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] invalid amount (Request)",
                //     L"WareHouse.cpp",
                //     0x274)
                L2_LOG_WARN(
                    Log,
                    "Warehouse deposit request rejected: quantity %lld is invalid for "
                    "item database ID %d (consume type %d).",
                    static_cast<long long>(amount), databaseId, static_cast<int>(consumeType)
                );
                socket->SendSystemMessage(SystemMessageId::INPUT_QUANTITY_LIMIT_EXCEEDED);
                socket->Send("cd", 0x43, -3);
                {
                    L2_GUARD(*user->m_lock);
                    user->ReleaseEconomy();
                }
                return false;
            }
            if (!item->CanKeep(WHC_NORMAL)) {
                break;
            }

            if (user->HavePet()) {
                CPet* const pet = user->GetPet();
                if (pet != nullptr && pet->GetDBID() == static_cast<unsigned int>(item->m_data->m_nDBID)) {
                    accepted = false;
                    break;
                }
            }

            const std::int64_t ownedAmount = item->m_data->m_nCount;
            if (amount > ownedAmount && ownedAmount > 0) {
                amount = ownedAmount;
            } else if (ownedAmount == 0) {
                break;
            }

            if (item->m_nClassId == 0x39) {
                requestedAdena += amount;
            }
            if (user->IsEquipedItem(item->GetIndex())) {
                break;
            }

            int slot = 0;
            while (slot < ST_MAX && (ST2SBT[slot] & item->m_data->m_Slot) == 0) {
                ++slot;
            }
            if (user->m_data->m_nSlotIndex[slot] == item->GetIndex()) {
                break;
            }
            if (item->IsPetCollarItem()) {
                CPet* const pet = user->GetPet();
                if (pet != nullptr && pet->GetDBID() == item->GetDBID()) {
                    break;
                }
            }

            if (ownedAmount >= amount) {
                const int assembled = ::Assemble(
                    requestPacket.data() + requestLength, static_cast<int>(requestPacket.size()) - requestLength, "dQd",
                    databaseId, amount, static_cast<int>(consumeType)
                );
                if (assembled > 0) {
                    requestLength += assembled;
                    ++depositItemCount;
                }
            }
            accepted = true;
        }
        return true;
    }();
    if (!requestValid) {
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    if (!accepted) {
        socket->Send("cd", 0x43, -2);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    CItem* const adena = user->m_inv.GetFirstItemByClassID(0x39, false, false);
    if (adena == nullptr) {
        socket->Send("cd", 0x43, -2);
        {
            L2_GUARD(*user->m_lock);
            user->ReleaseEconomy();
        }
        user->SendSystemMessage(SystemMessageId::NO_ADENA_ITEM_FOR_WAREHOUSE_FEE);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    const std::int64_t fee = static_cast<std::int64_t>(depositItemCount) * 30;
    if (adena->m_data->m_nCount < fee + requestedAdena) {
        socket->Send("cd", 0x43, -2);
        {
            L2_GUARD(*user->m_lock);
            user->ReleaseEconomy();
        }
        user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA_FOR_WAREHOUSE_DEPOSIT_AND_FEE);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    const int feeAssembled = ::Assemble(
        requestPacket.data() + requestLength, static_cast<int>(requestPacket.size()) - requestLength, "dQ", adena->GetDBID(),
        -fee
    );
    if (feeAssembled > 0) {
        requestLength += feeAssembled;
        ++depositItemCount;
    }
    if (depositItemCount < 1) {
        socket->Send("cd", 0x43, -2);
        {
            L2_GUARD(*user->m_lock);
            user->ReleaseEconomy();
        }
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    db.RequestDepositWareHouseItems(
        user->CastUser(), m_nWarehouseID, npc->m_data->m_nClassID, depositItemCount, user->GetPrivateBankLimit(),
        requestLength, requestPacket.data()
    );
}

void CWareHouseDeposit::RequestEx(const unsigned char* packet) {
    // Current L2Server 0x00c53214.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = GetUserSP();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x9e)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x9e)
        L2_LOG_WARN(
            Log, "Extended warehouse deposit rejected: player object ID %d is unavailable.",
            static_cast<int>(m_nActorServerId)
        );
        return;
    }
    if (runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 1) == 1) {
        user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    CSPointer<CNPC> npc = GetNPCSP();
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0xab)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0xab)
        L2_LOG_WARN(
            Log, "Extended warehouse deposit rejected: keeper object ID %d is unavailable.", static_cast<int>(m_nNpcServerId)
        );
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }
    if (m_nWarehouseType == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"error. warehouse typpe == 0 in RequestEx(const unsigned char* packet)")
        // L2_LOG_ERROR(Log,
        //     L"error. warehouse typpe == 0 in RequestEx(const unsigned char* packet)")
        L2_LOG_ERROR(Log, "Extended warehouse deposit rejected: ordinary warehouse type 0 was supplied.");
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    int itemCount = 0;
    const unsigned char* cursor = Disassemble(packet, "d", &itemCount);
    if (itemCount == 0) {
        socket->Send("cd", 0x43, -3);
        {
            L2_GUARD(*user->m_lock);
            user->ReleaseEconomy();
        }
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    CPacketHelperEx<20480> requestPacket;
    std::set<int> databaseIds;
    int assembledCount = 0;
    std::int64_t requestedAdena = 0;
    bool success = true;

    const bool requestValid = [&] {
        L2_READ_GUARD(user->m_inv);
        for (int index = 0; index < itemCount; ++index) {
            int databaseId = 0;
            std::int64_t amount = 0;
            cursor = Disassemble(cursor, "dQ", &databaseId, &amount);

            if (!databaseIds.insert(databaseId).second) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d][Kick] Deposit to pledge warehouse. but item is "
                //     L"duplicated. user[%s] item[%d:%d]",
                //     L"WareHouse.cpp",
                //     0xe4,
                //     user->GetName(),
                //     databaseId,
                //     amount)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d][Kick] Deposit to pledge warehouse. but item is "
                //     L"duplicated. user[%s] item[%d:%d]",
                //     L"WareHouse.cpp",
                //     0xe4,
                //     user->GetName(),
                //     databaseId,
                //     amount)
                L2_LOG_WARN(
                    Log,
                    "Extended warehouse deposit contains duplicate item database ID %d "
                    "(quantity %lld, player object ID %d); scheduling connection closure.",
                    databaseId, static_cast<long long>(amount), static_cast<int>(m_nActorServerId)
                );
                socket->SetCloseTimer();
                {
                    L2_GUARD(*user->m_lock);
                    user->ReleaseEconomy();
                }
                return false;
            }

            CItem* const item = user->m_inv.GetByDBID(databaseId);
            if (item == nullptr) {
                socket->Send("cd", 0x43, -2);
                success = false;
                break;
            }

            const ItemConsumeType consumeType = item->m_data->m_ItemConsumeType;
            bool validAmount = false;
            if (consumeType == ICT_ASSET) {
                validAmount = amount >= 1 && amount <= 999999999999LL;
            } else if (amount >= 1 && amount <= 1000000) {
                std::array<char, 32> amountText{};
                const auto converted = std::to_chars(amountText.data(), amountText.data() + 11, amount);
                if (converted.ec == std::errc{}) {
                    *converted.ptr = '\0';
                    validAmount = std::strlen(amountText.data()) <= 8;
                }
            }
            if (!validAmount) {
                // Log.Add(
                //     LOG_ERROR,
                //     "invalid amount (Request) at file[%s], line[%d]",
                //     "WareHouse.cpp",
                //     0x104)
                // L2_LOG_ERROR(Log,
                //     "invalid amount (Request) at file[%s], line[%d]",
                //     "WareHouse.cpp",
                //     0x104)
                L2_LOG_WARN(
                    Log,
                    "Extended warehouse deposit rejected: quantity %lld is invalid for "
                    "item database ID %d (consume type %d).",
                    static_cast<long long>(amount), databaseId, static_cast<int>(consumeType)
                );
                socket->SendSystemMessage(SystemMessageId::INPUT_QUANTITY_LIMIT_EXCEEDED);
                socket->Send("cd", 0x43, -3);
                {
                    L2_GUARD(*user->m_lock);
                    user->ReleaseEconomy();
                }
                return false;
            }

            const WareHouseClass warehouseClass = static_cast<WareHouseClass>(4 / m_nWarehouseType);
            if (!item->CanKeep(warehouseClass)) {
                success = false;
                break;
            }

            const std::int64_t ownedAmount = item->m_data->m_nCount;
            if (amount > ownedAmount && ownedAmount > 0) {
                amount = ownedAmount;
            } else if (ownedAmount == 0) {
                socket->Send("cd", 0x43, -2);
                success = false;
                break;
            }

            if (item->GetClassId() == 0x39) {
                requestedAdena += amount;
            }
            if (user->IsEquipedItem(item->GetIndex())) {
                socket->Send("cd", 0x43, -2);
                success = false;
                break;
            }
            int slot = 0;
            while (slot < ST_MAX && (ST2SBT[slot] & item->m_data->m_Slot) == 0) {
                ++slot;
            }
            if (user->m_data->m_nSlotIndex[slot] == item->GetIndex()) {
                socket->Send("cd", 0x43, -2);
                success = false;
                break;
            }
            if (item->m_itemOption.itemOptionKey.variationOption1 > 0 &&
                item->m_itemOption.itemOptionKey.variationOption2 > 0) {
                socket->Send("cd", 0x43, -2);
                success = false;
                break;
            }
            if (item->IsDuration()) {
                socket->Send("cd", 0x43, -2);
                success = false;
                break;
            }
            if (item->IsEtcItem() && !item->CastEtcItem()->m_bPetCollarTradable) {
                socket->Send("cd", 0x43, -2);
                success = false;
                break;
            }
            if (item->IsPetCollarItem() && user->m_nPetItemCount > 0) {
                success = false;
                break;
            }

            if (ownedAmount >= amount) {
                CItemAssemblerWareHouse assembler(*item, databaseId, amount, consumeType, 0);
                if (assembler.AssembleItemInfo(requestPacket) > 0) {
                    ++assembledCount;
                }
            }
        }
        return true;
    }();
    if (!requestValid) {
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    if (!success) {
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    CItem* const adena = user->m_inv.GetFirstItemByClassID(0x39, false, false);
    if (adena == nullptr) {
        socket->Send("cd", 0x43, -2);
        {
            L2_GUARD(*user->m_lock);
            user->ReleaseEconomy();
        }
        user->SendSystemMessage(SystemMessageId::NO_ADENA_ITEM_FOR_WAREHOUSE_FEE);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    const std::int64_t fee = static_cast<std::int64_t>(assembledCount) * 30;
    if (adena->m_data->m_nCount < fee + requestedAdena) {
        socket->Send("cd", 0x43, -2);
        {
            L2_GUARD(*user->m_lock);
            user->ReleaseEconomy();
        }
        user->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA_FOR_WAREHOUSE_DEPOSIT_AND_FEE);
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    CItemAssemblerWareHouse adenaAssembler(*adena, adena->GetDBID(), fee, adena->m_data->m_ItemConsumeType, 0);
    if (adenaAssembler.AssembleItemInfo(requestPacket) > 0) {
        ++assembledCount;
    }
    if (assembledCount < 1) {
        socket->Send("cd", 0x43, -2);
        {
            L2_GUARD(*user->m_lock);
            user->ReleaseEconomy();
        }
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_DEPOSIT_FAILED);
        return;
    }

    db.RequestDepositItemEx(
        m_nWarehouseType, m_nOwnerID, user->CastUser(), assembledCount, 200, requestPacket.GetLength(),
        requestPacket.GetPacket()
    );
}

CWareHouseWithdraw::CWareHouseWithdraw(User* const user, CCreature* const npc, const int warehouseType, const int ownerId) :
    m_nWarehouseID(1),
    m_nWarehouseType(warehouseType),
    m_nOwnerID(ownerId),
    m_nActorServerId(0),
    m_nNpcServerId(0),
    m_alignment4cTo50{} {
    // Current L2Server 0x00c509f4.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        L2_ADD_REF(user, ObjectRefType::Anonymous);
        // Current 0x00c50b2a reads CObject::m_nId at +0x18 after AddRef.
        m_nActorServerId = static_cast<std::int32_t>(user->Id());
    }

    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Not NPC.",
        //     "WareHouse.cpp",
        //     0x341)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Not NPC.",
        //     "WareHouse.cpp",
        //     0x341)
        L2_LOG_ERROR(Log, "Warehouse withdrawal created without a keeper: NPC pointer is null.");
    } else if (!npc->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Not NPC.",
        //     "WareHouse.cpp",
        //     0x33b)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Not NPC.",
        //     "WareHouse.cpp",
        //     0x33b)
        L2_LOG_ERROR(Log, "Warehouse withdrawal created without a keeper: supplied creature is not an NPC.");
    } else {
        L2_ADD_REF(npc, ObjectRefType::Anonymous);
        // Current 0x00c50b61 reads CObject::m_nId at +0x18 after AddRef.
        m_nNpcServerId = static_cast<std::int32_t>(npc->Id());
    }
}

CWareHouseWithdraw::~CWareHouseWithdraw() {
    // Current L2Server 0x00c50d14. The FunctionLocal references acquired by
    // Get*SP are separate from the Anonymous references kept by ctor.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = GetUserSP();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x35a)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x35a)
        L2_LOG_ERROR(
            Log, "Warehouse withdrawal cleanup cannot release player object ID %d: player unavailable.",
            static_cast<int>(m_nActorServerId)
        );
    } else {
        L2_RELEASE(user, ObjectRefType::Anonymous);
    }

    CSPointer<CNPC> npc = GetNPCSP();
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x364)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x364)
        L2_LOG_ERROR(
            Log, "Warehouse withdrawal cleanup cannot release keeper object ID %d: NPC unavailable.",
            static_cast<int>(m_nNpcServerId)
        );
    } else {
        L2_RELEASE(npc, ObjectRefType::Anonymous);
    }
}

CSPointer<User> CWareHouseWithdraw::GetUserSP() const {
    // Current L2Server 0x00c4d078, original WareHouse.h line 0xac.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_nActorServerId);
    if (creature != nullptr && creature->IsPC()) {
        return CSPointer<User>(creature->CastUser(), ObjectRefType::FunctionLocal);
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid user[%d]",
    //     L"c:\\work\\lineage2build\\server\\main\\l2server\\WareHouse.h",
    //     0xac,
    //     m_nActorServerId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid user[%d]",
    //     L"c:\\work\\lineage2build\\server\\main\\l2server\\WareHouse.h",
    //     0xac,
    //     m_nActorServerId)
    // Noise: every caller logs the unavailable object with operation context.
    // L2_LOG_ERROR(Log,
    //     "Warehouse withdrawal cannot resolve player object ID %d: creature is missing or is not a player.",
    //     static_cast<int>(m_nActorServerId))
    return {};
}

CSPointer<CNPC> CWareHouseWithdraw::GetNPCSP() const {
    // Current L2Server 0x00c4d138, original WareHouse.h line 0xbc.
    CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(m_nNpcServerId);
    if (creature != nullptr && creature->IsNPC()) {
        return CSPointer<CNPC>(creature->CastNPC(), ObjectRefType::FunctionLocal);
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Invalid NPC[%d]",
    //     L"c:\\work\\lineage2build\\server\\main\\l2server\\WareHouse.h",
    //     0xbc,
    //     m_nNpcServerId)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Invalid NPC[%d]",
    //     L"c:\\work\\lineage2build\\server\\main\\l2server\\WareHouse.h",
    //     0xbc,
    //     m_nNpcServerId)
    // Noise: every caller logs the unavailable object with operation context.
    // L2_LOG_ERROR(Log,
    //     "Warehouse withdrawal cannot resolve keeper object ID %d: creature is missing or is not an NPC.",
    //     static_cast<int>(m_nNpcServerId))
    return {};
}

void CWareHouseWithdraw::SendList(const WareHouseClass warehouseClass) {
    // Current L2Server 0x00c4df48.
    L2_GUARD_FUNCTION();
    CSPointer<CNPC> npc = GetNPCSP();
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x373)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x373)
        L2_LOG_WARN(
            Log, "Warehouse withdrawal list request rejected: keeper object ID %d is unavailable.",
            static_cast<int>(m_nNpcServerId)
        );
        return;
    }

    CSPointer<User> user = GetUserSP();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x37a)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x37a)
        L2_LOG_WARN(
            Log, "Warehouse withdrawal list request rejected: player object ID %d is unavailable.",
            static_cast<int>(m_nActorServerId)
        );
        return;
    }

    if (runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 1) == 1) {
#if defined(_WIN32)
        const wchar_t* const accountName = reinterpret_cast<const wchar_t*>(user->m_sAccountName.data());
#else
        std::array<wchar_t, 16> accountNameBuffer{};
        const wchar_t* const accountName =
            CopyUtf16ToWide(user->m_sAccountName.data(), user->m_sAccountName.size(), accountNameBuffer);
#endif
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] UsePackageItemPacketWait. ACCOUNT(id[%d]/name[%s]) "
        //     L"// CHAR[%s] // ItemCount(%d/%d)",
        //     L"WareHouse.cpp",
        //     0x381,
        //     user->GetAccountID(),
        //     accountName,
        //     user->GetName(),
        //     user->m_lCurrentReceivedPackageItemCount,
        //     user->m_lTotalPackageItemCount)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] UsePackageItemPacketWait. ACCOUNT(id[%d]/name[%s]) "
        //     L"// CHAR[%s] // ItemCount(%d/%d)",
        //     L"WareHouse.cpp",
        //     0x381,
        //     user->GetAccountID(),
        //     accountName,
        //     user->GetName(),
        //     user->m_lCurrentReceivedPackageItemCount,
        //     user->m_lTotalPackageItemCount)
        // L2_LOG_WARN(Log,
        //     "Warehouse withdrawal list request deferred: package-item job is already active for player %s (account %s, ID
        //     %u. Received items %d of %d).", WideToUtf8(user->GetName()).c_str(), WideToUtf8(accountName).c_str(),
        //     static_cast<unsigned int>(user->GetAccountID()),
        //     static_cast<int>(user->m_lCurrentReceivedPackageItemCount),
        //     static_cast<int>(user->m_lTotalPackageItemCount))
        // L2_LOG_WARN(Log,
        //     "Warehouse withdrawal list request not processed: package-item job is already active for player %s (account
        //     %s, ID %u. Received items %d of %d).", WideToUtf8(user->GetName()).c_str(), WideToUtf8(accountName).c_str(),
        //     static_cast<unsigned int>(user->GetAccountID()),
        //     static_cast<int>(user->m_lCurrentReceivedPackageItemCount),
        //     static_cast<int>(user->m_lTotalPackageItemCount))
        L2_LOG_WARN(
            Log,
            "Warehouse withdrawal list request not processed: package-item job is already active for player %s (account %s, "
            "ID %u; received items %d of %d).",
            Utf16ToUtf8(user->GetName()).c_str(), WideToUtf8(accountName).c_str(),
            static_cast<unsigned int>(user->GetAccountID()), static_cast<int>(user->m_lCurrentReceivedPackageItemCount),
            static_cast<int>(user->m_lTotalPackageItemCount)
        );
        return;
    }

    const int npcClassId = m_nWarehouseType == 3 ? npc->m_data->m_nClassID : 0;
    db.RequestLoadWareHouseItems(user.get(), m_nWarehouseID, this, npcClassId, MemoryObjectID(), warehouseClass, 0);
}

void CWareHouseWithdraw::SendListEx([[maybe_unused]] const WareHouseClass warehouseClass) {
    // Current L2Server 0x00c4e374.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = GetUserSP();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x397)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x397)
        L2_LOG_WARN(
            Log, "Extended warehouse withdrawal list request rejected: player object ID %d is unavailable.",
            static_cast<int>(m_nActorServerId)
        );
        return;
    }

    if (m_nWarehouseType == 2 && !user->HavePledgePower(PP_VIEW_WAREHOUSE)) {
        if (CUserSocket* const socket = user->GetSocket()) {
            socket->SendSystemMessage(SystemMessageId::NO_AUTHORITY);
        }
        return;
    }

    if (runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 1) == 1) {
#if defined(_WIN32)
        const wchar_t* const accountName = reinterpret_cast<const wchar_t*>(user->m_sAccountName.data());
#else
        std::array<wchar_t, 16> accountNameBuffer{};
        const wchar_t* const accountName =
            CopyUtf16ToWide(user->m_sAccountName.data(), user->m_sAccountName.size(), accountNameBuffer);
#endif
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] UsePackageItemPacketWait. ACCOUNT(id[%d]/name[%s]) "
        //     L"// CHAR[%s] // ItemCount(%d/%d)",
        //     L"WareHouse.cpp",
        //     0x3ac,
        //     user->GetAccountID(),
        //     accountName,
        //     user->GetName(),
        //     user->m_lCurrentReceivedPackageItemCount,
        //     user->m_lTotalPackageItemCount)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] UsePackageItemPacketWait. ACCOUNT(id[%d]/name[%s]) "
        //     L"// CHAR[%s] // ItemCount(%d/%d)",
        //     L"WareHouse.cpp",
        //     0x3ac,
        //     user->GetAccountID(),
        //     accountName,
        //     user->GetName(),
        //     user->m_lCurrentReceivedPackageItemCount,
        //     user->m_lTotalPackageItemCount)
        // L2_LOG_WARN(Log,
        //     "Extended warehouse withdrawal list request deferred: package-item job is already active for player %s
        //     (account %s, ID %u. Received items %d of %d).", WideToUtf8(user->GetName()).c_str(),
        //     WideToUtf8(accountName).c_str(),
        //     static_cast<unsigned int>(user->GetAccountID()),
        //     static_cast<int>(user->m_lCurrentReceivedPackageItemCount),
        //     static_cast<int>(user->m_lTotalPackageItemCount))
        // L2_LOG_WARN(Log,
        //     "Extended warehouse withdrawal list request not processed: package-item job is already active for player %s
        //     (account %s, ID %u. Received items %d of %d).", WideToUtf8(user->GetName()).c_str(),
        //     WideToUtf8(accountName).c_str(),
        //     static_cast<unsigned int>(user->GetAccountID()),
        //     static_cast<int>(user->m_lCurrentReceivedPackageItemCount),
        //     static_cast<int>(user->m_lTotalPackageItemCount))
        L2_LOG_WARN(
            Log,
            "Extended warehouse withdrawal list request not processed: package-item job is already active for player %s "
            "(account %s, ID %u; received items %d of %d).",
            Utf16ToUtf8(user->GetName()).c_str(), WideToUtf8(accountName).c_str(),
            static_cast<unsigned int>(user->GetAccountID()), static_cast<int>(user->m_lCurrentReceivedPackageItemCount),
            static_cast<int>(user->m_lTotalPackageItemCount)
        );
        return;
    }

    db.RequestLoadWarehouseEx(m_nWarehouseType, m_nOwnerID, user.get(), MemoryObjectID(), 0);
}

bool CWareHouseWithdraw::IsInRange() {
    // Current L2Server 0x00c4e6f0.
    L2_GUARD_FUNCTION();
    CSPointer<CNPC> npc = GetNPCSP();
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x558)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x558)
        L2_LOG_WARN(
            Log, "Warehouse withdrawal distance check failed: keeper object ID %d is unavailable.",
            static_cast<int>(m_nNpcServerId)
        );
        return false;
    }

    CSPointer<User> user = GetUserSP();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x55f)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x55f)
        L2_LOG_WARN(
            Log, "Warehouse withdrawal distance check failed: player object ID %d is unavailable.",
            static_cast<int>(m_nActorServerId)
        );
        return false;
    }
    if (npc->m_bLeft) {
        return false;
    }

    const FVector userPosition = user->GetPos();
    const FVector npcPosition = npc->GetPos();
    const double deltaX = userPosition.x - npcPosition.x;
    const double deltaY = userPosition.y - npcPosition.y;
    return std::sqrt(deltaX * deltaX + deltaY * deltaY) <= 250.0;
}

int CWareHouseWithdraw::GetClassId(const int itemDatabaseId) {
    // Current L2Server 0x00c4ea5c.
    L2_GUARD_FUNCTION();
    const int classId = [&] {
        L2_GUARD(m_WarehouseLock);
        const auto found = m_mapWarehouse.find(itemDatabaseId);
        const int classId = found != m_mapWarehouse.end() ? found->second : 0;
        return classId;
    }();
    return classId;
}

int CWareHouseWithdraw::AddItemId(const int itemDatabaseId, const int itemClassId) {
    // Current L2Server 0x00c4eb68.
    L2_GUARD_FUNCTION();
    CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
    if (object == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Error. Cannot getobject, [%d]",
        //     itemClassId)
        // L2_LOG_ERROR(Log,
        //     L"Error. Cannot getobject, [%d]",
        //     itemClassId)
        L2_LOG_ERROR(
            Log,
            "Warehouse item registration failed: prototype class %d is missing for item database ID %d; returning consume "
            "type 0.",
            itemClassId, itemDatabaseId
        );
        return 0;
    }

    {
        GuardHelper addItemIdOne(L"additemid 1", CThreadManager::GetThreadID());
        L2_GUARD(m_WarehouseLock);
        {
            GuardHelper addItemIdTwo(L"additemid 2", CThreadManager::GetThreadID());
            m_mapWarehouse.emplace(itemDatabaseId, itemClassId);
        }
    }

    const CItem* const item = static_cast<CItem*>(object);
    return static_cast<int>(item->m_data->m_ItemConsumeType);
}

void CWareHouseWithdraw::Request(const unsigned char* packet, const int itemCount) {
    // Current L2Server 0x00c50ef4.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = GetUserSP();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x3bf)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x3bf)
        L2_LOG_WARN(
            Log, "Warehouse withdrawal rejected: player object ID %d is unavailable.", static_cast<int>(m_nActorServerId)
        );
        return;
    }

    if (m_nWarehouseType >= 1) {
        RequestEx(packet, itemCount);
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    if (g_nCountryCode == 1 && socket != nullptr && socket->m_bEconomicRestriction) {
        user->SendSystemMessage(SystemMessageId::ACCOUNT_NOT_PAID);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_WITHDRAWAL_FAILED);
        return;
    }

    if (runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 1) == 1) {
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_WITHDRAWAL_FAILED);
        return;
    }

    if (itemCount < 1 || itemCount > warehouse::kMaxWithdrawalItems) {
        socket->Send("cd", 0x43, -3);
        {
            L2_WRITE_GUARD(user->m_inv);
            user->ReleaseEconomy();
        }
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_WITHDRAWAL_FAILED);
        return;
    }

    std::set<int> databaseIds;
    std::array<char, 20480> requestPacket{};
    int requestLength = 0;
    const unsigned char* cursor = packet;
    for (int index = 0; index < itemCount; ++index) {
        int databaseId = 0;
        std::int64_t amount = 0;
        cursor = Disassemble(cursor, "dQ", &databaseId, &amount);

        if (!databaseIds.insert(databaseId).second) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][Kick] Withdraw to warehouse. but item is "
            //     L"duplicated. user[%s] item[%d:%d]",
            //     L"WareHouse.cpp",
            //     0x3f4,
            //     user->GetName(),
            //     databaseId,
            //     amount)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][Kick] Withdraw to warehouse. but item is "
            //     L"duplicated. user[%s] item[%d:%d]",
            //     L"WareHouse.cpp",
            //     0x3f4,
            //     user->GetName(),
            //     databaseId,
            //     amount)
            L2_LOG_WARN(
                Log,
                "Warehouse withdrawal contains duplicate item database ID %d (quantity %lld, player object ID %d); "
                "scheduling connection closure.",
                databaseId, static_cast<long long>(amount), static_cast<int>(m_nActorServerId)
            );
            socket->SetCloseTimer();
            {
                L2_WRITE_GUARD(user->m_inv);
                user->ReleaseEconomy();
            }
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
            user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_WITHDRAWAL_FAILED);
            return;
        }

        int consumeType = 0;
        int itemClassId = 0;
        const bool itemFound = [&] {
            L2_GUARD(m_WarehouseLock);
            const auto found = m_mapWarehouse.find(databaseId);
            if (found == m_mapWarehouse.end()) {
                return false;
            }
            itemClassId = found->second;
            return true;
        }();
        if (itemFound) {

            CItem* const item = static_cast<CItem*>(g_ObjectDB.GetObjectW(itemClassId, false));
            if (item != nullptr) {
                consumeType = static_cast<int>(item->m_data->m_ItemConsumeType);

                if (!warehouse::IsValidWithdrawalAmount(item->m_data->m_ItemConsumeType, amount)) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] invalid amount (Request) item_type[%d] "
                    //     L"amt[%I64d]",
                    //     L"WareHouse.cpp",
                    //     0x415,
                    //     itemClassId,
                    //     amount)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] invalid amount (Request) item_type[%d] "
                    //     L"amt[%I64d]",
                    //     L"WareHouse.cpp",
                    //     0x415,
                    //     itemClassId,
                    //     amount)
                    L2_LOG_WARN(
                        Log,
                        "Warehouse withdrawal rejected: quantity %lld is invalid for item database ID %d (class %d, consume "
                        "type %d).",
                        static_cast<long long>(amount), databaseId, itemClassId, consumeType
                    );
                    socket->SendSystemMessage(SystemMessageId::INPUT_QUANTITY_LIMIT_EXCEEDED);
                    {
                        L2_WRITE_GUARD(user->m_inv);
                        user->ReleaseEconomy();
                    }
                    runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
                    user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_WITHDRAWAL_FAILED);
                    return;
                }
            }
        }

        const int assembled = ::Assemble(
            requestPacket.data() + requestLength, static_cast<int>(requestPacket.size()) - requestLength, "dQd", databaseId,
            amount, consumeType
        );
        if (assembled > 0) {
            requestLength += assembled;
        }
    }

    CSPointer<CNPC> npc = GetNPCSP();
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x428)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find NPC.",
        //     "WareHouse.cpp",
        //     0x428)
        L2_LOG_WARN(
            Log, "Warehouse withdrawal rejected: keeper object ID %d is unavailable.", static_cast<int>(m_nNpcServerId)
        );
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::WAREHOUSE_ITEM_WITHDRAWAL_FAILED);
        return;
    }

    const int npcClassId = npc->m_data->m_nClassID;
    if (runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 1) == 1) {
#if defined(_WIN32)
        const wchar_t* const accountName = reinterpret_cast<const wchar_t*>(user->m_sAccountName.data());
#else
        std::array<wchar_t, 16> accountNameBuffer{};
        const wchar_t* const accountName =
            CopyUtf16ToWide(user->m_sAccountName.data(), user->m_sAccountName.size(), accountNameBuffer);
#endif
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] UsePackageItemPacketWait. ACCOUNT(id[%d]/name[%s]) "
        //     L"// CHAR[%s] // ItemCount(%d/%d)",
        //     L"WareHouse.cpp",
        //     0x433,
        //     user->GetAccountID(),
        //     accountName,
        //     user->GetName(),
        //     user->m_lCurrentReceivedPackageItemCount,
        //     user->m_lTotalPackageItemCount)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] UsePackageItemPacketWait. ACCOUNT(id[%d]/name[%s]) "
        //     L"// CHAR[%s] // ItemCount(%d/%d)",
        //     L"WareHouse.cpp",
        //     0x433,
        //     user->GetAccountID(),
        //     accountName,
        //     user->GetName(),
        //     user->m_lCurrentReceivedPackageItemCount,
        //     user->m_lTotalPackageItemCount)
        // L2_LOG_WARN(Log,
        //     "Warehouse withdrawal deferred: package-item job is already active for player %s (account %s, ID %u. Received
        //     items %d of %d).", WideToUtf8(user->GetName()).c_str(), WideToUtf8(accountName).c_str(), static_cast<unsigned
        //     int>(user->GetAccountID()), static_cast<int>(user->m_lCurrentReceivedPackageItemCount),
        //     static_cast<int>(user->m_lTotalPackageItemCount))
        // L2_LOG_WARN(Log,
        //     "Warehouse withdrawal not processed: package-item job is already active for player %s (account %s, ID %u.
        //     received items %d of %d).", WideToUtf8(user->GetName()).c_str(), WideToUtf8(accountName).c_str(),
        //     static_cast<unsigned int>(user->GetAccountID()),
        //     static_cast<int>(user->m_lCurrentReceivedPackageItemCount),
        //     static_cast<int>(user->m_lTotalPackageItemCount))
        L2_LOG_WARN(
            Log,
            "Warehouse withdrawal not processed: package-item job is already active for player %s (account %s, ID %u; "
            "received items %d of %d).",
            Utf16ToUtf8(user->GetName()).c_str(), WideToUtf8(accountName).c_str(),
            static_cast<unsigned int>(user->GetAccountID()), static_cast<int>(user->m_lCurrentReceivedPackageItemCount),
            static_cast<int>(user->m_lTotalPackageItemCount)
        );
        return;
    }

    db.RequestWithdrawWareHouseItems(user.get(), m_nWarehouseID, npcClassId, itemCount, requestLength, requestPacket.data());
}

void CWareHouseWithdraw::RequestEx(const unsigned char* packet, const int itemCount) {
    // Fix the source bug in L2Server 0x00c51ea4. Validate the requested
    // amount instead of the prototype class ID.
    L2_GUARD_FUNCTION();
    CSPointer<User> user = GetUserSP();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x446)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find User.",
        //     "WareHouse.cpp",
        //     0x446)
        L2_LOG_WARN(
            Log, "Extended warehouse withdrawal rejected: player object ID %d is unavailable.",
            static_cast<int>(m_nActorServerId)
        );
        return;
    }

    if (m_nWarehouseType == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]error. warehouse typpe == 0",
        //     "WareHouse.cpp",
        //     0x44c)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]error. warehouse typpe == 0",
        //     "WareHouse.cpp",
        //     0x44c)
        L2_LOG_ERROR(Log, "Extended warehouse withdrawal rejected: ordinary warehouse type 0 was supplied.");
        user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_WITHDRAWAL_FAILED);
        return;
    }

    if (runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 1) == 1) {
        return;
    }

    CUserSocket* const socket = user->GetSocket();
    if (m_nWarehouseType == 2 && !user->m_data->m_bPledgeMaster) {
        if (socket != nullptr) {
            socket->SendSystemMessage(SystemMessageId::ONLY_CLAN_LEADER_CAN_ACT);
        }
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_WITHDRAWAL_FAILED);
        return;
    }

    if (itemCount < 1 || itemCount > warehouse::kMaxWithdrawalItems) {
        socket->Send("cd", 0x43, -3);
        {
            L2_WRITE_GUARD(user->m_inv);
            user->ReleaseEconomy();
        }
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_WITHDRAWAL_FAILED);
        return;
    }

    CPacketHelperEx<20480> requestPacket;
    std::set<int> databaseIds;
    const unsigned char* cursor = packet;
    for (int index = 0; index < itemCount; ++index) {
        int databaseId = 0;
        std::int64_t amount = 0;
        cursor = Disassemble(cursor, "dQ", &databaseId, &amount);

        if (!databaseIds.insert(databaseId).second) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][Kick] Withdraw to pledge warehouse. but item is "
            //     L"duplicated. user[%s] item[%d:%d]",
            //     L"WareHouse.cpp",
            //     0x48b,
            //     user->GetName(),
            //     databaseId,
            //     amount)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][Kick] Withdraw to pledge warehouse. but item is "
            //     L"duplicated. user[%s] item[%d:%d]",
            //     L"WareHouse.cpp",
            //     0x48b,
            //     user->GetName(),
            //     databaseId,
            //     amount)
            L2_LOG_WARN(
                Log,
                "Extended warehouse withdrawal contains duplicate item database ID %d (quantity %lld, player object ID %d); "
                "scheduling connection closure.",
                databaseId, static_cast<long long>(amount), static_cast<int>(m_nActorServerId)
            );
            socket->SetCloseTimer();
            {
                L2_WRITE_GUARD(user->m_inv);
                user->ReleaseEconomy();
            }
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
            user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_WITHDRAWAL_FAILED);
            return;
        }

        int itemClassId = 0;
        const bool itemFound = [&] {
            L2_GUARD(m_WarehouseLock);
            const auto found = m_mapWarehouse.find(databaseId);
            if (found == m_mapWarehouse.end()) {
                return false;
            }
            itemClassId = found->second;
            return true;
        }();
        if (!itemFound) {
            {
                L2_WRITE_GUARD(user->m_inv);
                user->ReleaseEconomy();
            }
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
            user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_WITHDRAWAL_FAILED);
            return;
        }

        CItem* const item = static_cast<CItem*>(g_ObjectDB.GetObjectW(itemClassId, false));
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid nItemClassID(%d) (pItem == nullptr)",
            //     L"WareHouse.cpp",
            //     0x4df,
            //     itemClassId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid nItemClassID(%d) (pItem == nullptr)",
            //     L"WareHouse.cpp",
            //     0x4df,
            //     itemClassId)
            L2_LOG_ERROR(
                Log, "Extended warehouse withdrawal rejected: item prototype class %d is missing for database ID %d.",
                itemClassId, databaseId
            );
            {
                L2_WRITE_GUARD(user->m_inv);
                user->ReleaseEconomy();
            }
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
            user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_WITHDRAWAL_FAILED);
            return;
        }

        const ItemConsumeType consumeType = item->m_data->m_ItemConsumeType;
        if (!warehouse::IsValidWithdrawalAmount(consumeType, amount)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid amount (Request) ",
            //     L"WareHouse.cpp",
            //     0x4f6)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid amount (Request) ",
            //     L"WareHouse.cpp",
            //     0x4f6)
            // L2_LOG_ERROR(Log,
            //     "Extended warehouse withdrawal rejected: prototype class ID %d failed the quantity-range check (item "
            //     "database ID %d, consume type %d, requested quantity %lld).",
            //     prototypeClassId, databaseId, static_cast<int>(consumeType), static_cast<long long>(amount))
            L2_LOG_ERROR(
                Log,
                "Extended warehouse withdrawal rejected: quantity %lld is invalid for item database ID %d "
                "(class %d, consume type %d).",
                static_cast<long long>(amount), databaseId, itemClassId, static_cast<int>(consumeType)
            );
            socket->SendSystemMessage(SystemMessageId::INPUT_QUANTITY_LIMIT_EXCEEDED);
            {
                L2_WRITE_GUARD(user->m_inv);
                user->ReleaseEconomy();
            }
            runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
            user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_WITHDRAWAL_FAILED);
            return;
        }

        CItemAssemblerWareHouse assembler(*item, databaseId, amount, consumeType, 0);
        assembler.AssembleItemInfo(requestPacket);
    }

    CSPointer<CNPC> npc = GetNPCSP();
    if (!npc) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]Cannot find NPC.",
        //     L"WareHouse.cpp",
        //     0x501)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]Cannot find NPC.",
        //     L"WareHouse.cpp",
        //     0x501)
        L2_LOG_WARN(
            Log, "Extended warehouse withdrawal rejected: keeper object ID %d is unavailable.",
            static_cast<int>(m_nNpcServerId)
        );
        runtime::raw_atomic::ExchangeI32(&user->m_lWaitItemJob, 0);
        user->SendSystemMessage(SystemMessageId::CLAN_WAREHOUSE_ITEM_WITHDRAWAL_FAILED);
        return;
    }

    const int npcClassId = npc->m_data->m_nClassID;
    if (runtime::raw_atomic::ExchangeI32(&user->m_lPackageItemPacketJobFlag, 1) == 1) {
#if defined(_WIN32)
        const wchar_t* const accountName = reinterpret_cast<const wchar_t*>(user->m_sAccountName.data());
#else
        std::array<wchar_t, 16> accountNameBuffer{};
        const wchar_t* const accountName =
            CopyUtf16ToWide(user->m_sAccountName.data(), user->m_sAccountName.size(), accountNameBuffer);
#endif
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] UsePackageItemPacketWait. ACCOUNT(id[%d]/name[%s]) "
        //     L"// CHAR[%s] // ItemCount(%d/%d)",
        //     L"WareHouse.cpp",
        //     0x50c,
        //     user->GetAccountID(),
        //     accountName,
        //     user->GetName(),
        //     user->m_lCurrentReceivedPackageItemCount,
        //     user->m_lTotalPackageItemCount)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] UsePackageItemPacketWait. ACCOUNT(id[%d]/name[%s]) "
        //     L"// CHAR[%s] // ItemCount(%d/%d)",
        //     L"WareHouse.cpp",
        //     0x50c,
        //     user->GetAccountID(),
        //     accountName,
        //     user->GetName(),
        //     user->m_lCurrentReceivedPackageItemCount,
        //     user->m_lTotalPackageItemCount)
        // L2_LOG_WARN(Log,
        //     "Extended warehouse withdrawal deferred: package-item job is already active for player %s (account %s, ID %u.
        //     received items %d of %d).", WideToUtf8(user->GetName()).c_str(), WideToUtf8(accountName).c_str(),
        //     static_cast<unsigned int>(user->GetAccountID()),
        //     static_cast<int>(user->m_lCurrentReceivedPackageItemCount),
        //     static_cast<int>(user->m_lTotalPackageItemCount))
        // L2_LOG_WARN(Log,
        //     "Extended warehouse withdrawal not processed: package-item job is already active for player %s (account %s, ID
        //     %u. Received items %d of %d).", WideToUtf8(user->GetName()).c_str(), WideToUtf8(accountName).c_str(),
        //     static_cast<unsigned int>(user->GetAccountID()),
        //     static_cast<int>(user->m_lCurrentReceivedPackageItemCount),
        //     static_cast<int>(user->m_lTotalPackageItemCount))
        L2_LOG_WARN(
            Log,
            "Extended warehouse withdrawal not processed: package-item job is already active for player %s (account %s, ID "
            "%u; received items %d of %d).",
            Utf16ToUtf8(user->GetName()).c_str(), WideToUtf8(accountName).c_str(),
            static_cast<unsigned int>(user->GetAccountID()), static_cast<int>(user->m_lCurrentReceivedPackageItemCount),
            static_cast<int>(user->m_lTotalPackageItemCount)
        );
        return;
    }

    db.RequestWithdrawItemEx(
        m_nWarehouseType, m_nOwnerID, user.get(), itemCount, static_cast<int>(user->m_data->m_nPledgeID), npcClassId,
        requestPacket.GetLength(), requestPacket.GetPacket()
    );
}
