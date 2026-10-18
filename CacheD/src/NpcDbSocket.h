#pragma once

#include "CacheSocket.h"

#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/IOCs.h"
#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>

class CNpcDbSocket;
class CDX_PLEDGE_RAID_HUNT_NPC_INFO;

bool GiveEventData(CNpcDbSocket* socket, const std::uint8_t* packet);

class BossInfo;
class BossInfoDB;

using BossInfoSP = CSPointer<BossInfo>;


// Shipping NpcDbSocket.cpp owner. Cached.exe 0x004fafd4 fixes the 0xc0-byte
// extent and every offset. The semantic field names follow the exact
// lin_LoadNpcBoss result order because the usable PDB has no BossInfo type.
class BossInfo : public MemoryObject {
public:
    BossInfo();
    ~BossInfo() override;

    bool Save();
    bool UpdateDBValue(int db_value);

    // The current NpcDbSocket.cpp callbacks read and update these members
    // directly. The machine code contains no accessor calls. Keep that
    // source-level surface public instead of introducing portable-only
    // getters/setters around the recovered layout.
    friend class BossInfoDB;
    friend class CDX_PLEDGE_RAID_HUNT_NPC_INFO;

    std::u16string m_npcDbName; // +0x18
    std::int32_t m_alive; // +0x40
    std::int32_t m_hp; // +0x44
    std::int32_t m_mp; // +0x48
    std::int32_t m_posX; // +0x4c
    std::int32_t m_posY; // +0x50
    std::int32_t m_posZ; // +0x54
    std::int32_t m_deathTime; // +0x58, SQL time_low
    std::int32_t m_dbValue; // +0x5c, SQL i0
    std::int32_t m_npcId; // +0x60
    std::uint32_t m_targetPadding64; // +0x64
    std::u16string m_killerMpccMasterName; // +0x68
    std::int32_t m_killerMpccMemberCount; // +0x90
    std::uint32_t m_targetPadding94; // +0x94
    CIOCriticalSection m_lock; // +0x98
};



// Cached.exe 0x004ba198 allocates exactly a Singleton vptr followed by the
// shared 0x58-byte CObjectStringMap<BossInfo> instance.
class BossInfoDB : public Singleton<BossInfoDB> {
public:
    BossInfoDB() = default;
    // Current scalar deleting destructor 0x004ba234 destroys the
    // CObjectStringMap<BossInfo> at +0x08 and then the Singleton base.
    ~BossInfoDB() override = default;

    BossInfoSP GetInfo(const wchar_t* sNpcDbName);

private:
    CObjectStringMap<BossInfo> m_infos; // +0x08
};


using NpcDbPacketCallback = bool (*)(CNpcDbSocket*, const std::uint8_t*);
using NpcDbCallbackMap = FixedSizeCallbackMap<19, NpcDbPacketCallback>;

extern NpcDbCallbackMap gNpcDbPacketTable;

// Shipping owner: CacheD/NpcDbSocket.{h,cpp}. The class adds no fields to
// CSocket. Current 0x004fa064 only replaces the callback-table pointer at
// +0x108 with the 19-entry NPC-server table.
class CNpcDbSocket : public CSocket {
public:
    explicit CNpcDbSocket(std::uint64_t socket, int bufferSize = 0x5000);
    ~CNpcDbSocket() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    static CSocket* Allocate(std::uint64_t socket);

    void SetAddress(::in_addr address) override;
    void OnCreate() override;
    void Send(const char* format, ...) override;

    // Original current signature: SendBuffer(const char *,char *). va_list
    // is the portable spelling of the same incoming variadic cursor.
    void SendBuffer(const char* format, va_list arguments);

protected:
    void OnClose() override;
    void OnRead() override;

private:
    L2_DECLARE_OBJECT_REGISTRATION(CNpcDbSocket)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CNpcDbSocket)
};


using NpcDbCPacketEx =
    CPacketEx<DefaultIOBuffer, CNpcDbSocket, NpcDbPacketCallback, packet_ex_detail::CPacketExCacheDInstrumentation>;

// The current image emits a distinct CNPCPacket vtable and no extra state.
// Its one override restores the process-configured database selector before
// entering the common packet callback path.
class CNPCPacket final : public NpcDbCPacketEx {
public:
    void OnIOCallback(int success, std::uint32_t transferred, PortableOverlapped* overlapped) override;
};
