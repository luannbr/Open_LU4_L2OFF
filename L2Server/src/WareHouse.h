#pragma once

#include "Economy.h"

#include "Shared/src/MemorySub.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CCreature;
class CDBSocket;
class CNPC;
class CSocket;
class User;

bool LoadWarehouseItemsPacket(CDBSocket* socket, const std::uint8_t* packet);
bool ReplyWarehouseItemsEx(CDBSocket* socket, const std::uint8_t* packet);
bool WarehouseDepositPacket(CSocket* socket, const std::uint8_t* packet);
bool RequestPackageSend(CSocket* socket, const std::uint8_t* packet);

enum WareHouseClass : std::int32_t {
    WHC_NONE = 0,
    WHC_NORMAL = 1,
    WHC_PLEDGE = 2,
    WHC_CASTLE = 4,
    WHC_ETC = 8,
};


// Exact current WareHouse.cpp deposit owner. Current RTTI fixes one
// CEconomy base, the complete nine-slot virtual surface and this 0x40-byte
// layout.
class CWareHouseDeposit final : public CEconomy {
public:
    CWareHouseDeposit(User* user, CCreature* npc, int warehouseType, int ownerId);
    ~CWareHouseDeposit() override;

    L2_DECLARE_OBJECT_TYPE(CWareHouseDeposit)

    bool IsWarehouse() override { return true; }
    virtual void SendList(WareHouseClass warehouseClass);
    virtual void SendListEx(WareHouseClass warehouseClass);
    virtual void Request(const unsigned char* packet);
    virtual void RequestEx(const unsigned char* packet);

    bool CheckItemRestrict(const unsigned char* packet);
    bool IsInRange();
    int GetWarehouseKeeperClassId();

private:
    friend class User;
    friend bool ::WarehouseDepositPacket(CSocket*, const std::uint8_t*);
    friend bool ::RequestPackageSend(CSocket*, const std::uint8_t*);

    CSPointer<User> GetUserSP() const;
    CSPointer<CNPC> GetNPCSP() const;

    std::int32_t m_nWarehouseID; // +0x28
    std::int32_t m_nWarehouseType; // +0x2c
    std::int32_t m_nOwnerID; // +0x30
    std::int32_t m_nActorServerId; // +0x34
    std::int32_t m_nNpcServerId; // +0x38
    std::byte m_alignment3cTo40[0x04]; // +0x3c
};

// Exact current WareHouse.cpp withdrawal owner. Current RTTI fixes one
// CEconomy base, the complete nine-slot virtual surface, and this 0x70-byte
// layout. The map records CacheD item DBID -> current item class ID while a
// paged warehouse reply is active.
class CWareHouseWithdraw final : public CEconomy {
public:
    CWareHouseWithdraw(User* user, CCreature* npc, int warehouseType, int ownerId);
    ~CWareHouseWithdraw() override;

    L2_DECLARE_OBJECT_TYPE(CWareHouseWithdraw)

    bool IsWarehouse() override { return true; }
    virtual void SendList(WareHouseClass warehouseClass);
    virtual void SendListEx(WareHouseClass warehouseClass);
    virtual void Request(const unsigned char* packet, int itemCount);
    virtual void RequestEx(const unsigned char* packet, int itemCount);

    bool IsInRange();
    int GetClassId(int itemDatabaseId);
    int AddItemId(int itemDatabaseId, int itemClassId);

private:
    friend bool ::LoadWarehouseItemsPacket(CDBSocket*, const std::uint8_t*);
    friend bool ::ReplyWarehouseItemsEx(CDBSocket*, const std::uint8_t*);

    CSPointer<User> GetUserSP() const;
    CSPointer<CNPC> GetNPCSP() const;

    std::int32_t m_nWarehouseID; // +0x28
    std::int32_t m_nWarehouseType; // +0x2c
    std::int32_t m_nOwnerID; // +0x30
    std::int32_t m_nActorServerId; // +0x34
    std::int32_t m_nNpcServerId; // +0x38
    CYieldLock m_WarehouseLock; // +0x3c
    std::byte m_alignment4cTo50[0x04]; // +0x4c
    std::map<int, int> m_mapWarehouse; // +0x50
};
