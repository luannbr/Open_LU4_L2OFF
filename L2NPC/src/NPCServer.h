#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

class CNPC;
class CSocket;
class CSharedCreatureData;
struct FVector;

namespace l2npc {
class CServerSocket;
}

// The nearby PDB identifies class NPCServer and source NPCServer.cpp. The
// shipping machine code confirms the layout below. Default.cpp
// owns the one process-global instance named g_ns in the nearby source build.
class NPCServer {
public:
    // Nested nearby-PDB enum used by m_servertype.
    enum SERVERTYPE : std::int32_t {
        NORMAL_SERVER = 0,
        FREE_PK_SERVER = 1,
        NONE_PK_SERVER = 2,
    };

    using SocketAllocator = CSocket* (*)(std::uint64_t socket);

    NPCServer();
    ~NPCServer();

    CSocket* Connect(char* address, int port, SocketAllocator allocator);
    bool InitWinsock();
    bool InitServerSocket();
    bool CheckMutex();
    bool Load();
    bool LoadNpcposData(int serverType);
    void Run();
    void Start();
    void CloseServerSocket();
    void Send(const char* format, ...);
    void SendV(const char* format, va_list arguments);
    void SendBuffer(std::uint8_t* buffer, int length);
    int PendingWrite();
    int DBPendingWrite();
    void StackInformation(char* destination);

    char16_t* GetMapDir() { return m_szMap.data(); }

    bool IsConnected() { return m_pServerSocket != nullptr; }

    l2npc::CServerSocket* GetServerSocket() { return m_pServerSocket; }

    static void SendInstantTeleport(
        CSharedCreatureData& target,
        const FVector& destination,
        int heading,
        int argument4,
        std::int64_t argument5,
        TeleportSourceType::Enum sourceType,
        CNPC* source,
        int tailArgument
    );

protected:
    // Current EXE confirms this complete 0x3858-byte order.
    SERVERTYPE m_servertype;
    std::int32_t m_nNPCs;
    std::int32_t m_nCreated;
    std::int32_t m_nTamed;
    bool m_bShutDown;
    std::array<char16_t, 2048> m_szMap;
    std::int32_t m_nThreads;
    std::array<char, 2048> m_szServerAddress;
    std::int32_t m_nServerPort;
    // Win64 long is 32-bit.
    std::int32_t m_timeStart;
    std::array<char16_t, 2048> m_szUser;
    std::array<char16_t, 2048> m_szPassword;
    bool m_bViewDetail;
    std::int32_t m_nAdena;
    l2npc::CServerSocket* m_pServerSocket;
    void* m_hThread;

public:
    std::u16string m_NpcPosFile;
};

// Nearby-PDB global name and source owner: default.obj, not NPCServer.obj.
extern NPCServer g_ns;

unsigned NPCServerThread(void* argument);
void NPCServerThread_common();
void NPCServerThread_mailing();
