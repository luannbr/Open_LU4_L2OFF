#pragma once

#include "WareHouse.h"

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CPet;
class CPetDB;
class CAdminSocket;
class CSocket;
class CUser;

bool GetPetListPacket(CAdminSocket* socket, const std::uint8_t* packet);

bool RequestWithdrawPet(CSocket* socket, const std::uint8_t* packet);
bool RequestDepositPet(CSocket* socket, const std::uint8_t* packet);
bool RequestSavePet(CSocket* socket, const std::uint8_t* packet);
bool RequestChangePetName(CSocket* socket, const std::uint8_t* packet);

using CPetSP = CSPointer<CPet>;
using CPetKey = std::CDualKey<int, int>;
using CPetMap = CObjectMap<CPet, 1024, 5, RWLock, CPetKey>;


// Shipping owner: CacheD/Pet.{h,cpp}. Current constructor 0x004ffc58 and
// CPetDB::GetPet 0x00500a10 fix the complete 0xa0-byte state. In particular,
// hp/mp are truncated into int32 fields in this revision, while experience,
// SP and down-exp are int64 values. The fixed char16_t array keeps the
// target's two-byte wchar_t layout on every portable host.
class CPet : public MemoryObject {
public:
    CPet();
    // Current scalar deleting destructor 0x004fff20 releases the
    // CObjectStorage<CPetWarehouse> entry at +0x88, then MemoryObject.
    ~CPet() override = default;

    // Current inline body 0x0042c194 delegates directly to the Shared
    // CObjectStorage<CPetWarehouse> owner. The source name is kept by
    // the matching C1 owner. No guard literal survives for the inline body.
    CPetWarehouseSP GetWarehouse();
    std::uint32_t NpcClassId() const noexcept;
    int Level();
    void LoadItems(int ownerId);
    bool Save();
    bool SetName(const wchar_t* name, int& result);

private:
    friend class CPetDB;
    // Current CPetWarehouse::PetGetItem reads the owning character directly
    // from CPet+0x9c when composing its route-6 record.
    friend class CPetWarehouse;
    // Current CUser::OnRequestEvolvePet reads and updates CPet state directly.
    friend class CUser;
    // These current CacheSocket.cpp handlers access CPet fields directly.
    // Friend declarations keep that observed source-level relationship
    // without inventing an accessor surface that is absent from the image.
    friend bool RequestWithdrawPet(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestDepositPet(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestSavePet(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestChangePetName(CSocket* socket, const std::uint8_t* packet);
    friend bool GetPetListPacket(CAdminSocket* socket, const std::uint8_t* packet);

    std::uint32_t m_id = 0; // +0x18
    std::uint32_t m_npcClassId = 0; // +0x1c
    std::int32_t m_hp = 0; // +0x20
    std::int32_t m_mp = 0; // +0x24
    std::int64_t m_exp = 0; // +0x28
    std::int64_t m_spellPoint = 0; // +0x30
    std::int32_t m_meal = 0; // +0x38
    bool m_isSpawned = false; // +0x3c
    bool m_itemsLoaded = false; // +0x3d
    std::int64_t m_downExp = 0; // +0x40
    std::uint32_t m_wasDead = 0; // +0x48
    std::int32_t m_deathCount = 0; // +0x4c
    std::array<char16_t, 25> m_nickName{}; // +0x50
    CObjectStorage<CPetWarehouse> m_warehouse; // +0x88
    std::int32_t m_slot1 = 0; // +0x90
    std::int32_t m_slot2 = 0; // +0x94
    std::int32_t m_slot3 = 0; // +0x98
    std::uint32_t m_ownerId = 0; // +0x9c
};



// Current CPetDB contains only this world-qualified Shared object map. The
// key order is {pet DBID, world ID}. Current map machine code hashes the
// secondary/world component exactly as Shared's CDualKey instantiation does.
class CPetDB {
public:
    CPetDB() = default;
    ~CPetDB() = default;

    CPetSP GetPet(int petId, int worldId);
    CPetSP ReloadPet(int petId, int worldId);
    bool DeletePet(int petId, bool deleteItems, int worldId);

private:
    CPetMap m_pets;
};


extern CPetDB g_petDb;
