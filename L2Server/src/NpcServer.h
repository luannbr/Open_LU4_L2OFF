#pragma once

#include "Server.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CNPC;
class NpcSocket;

class NpcServer : public Server {
public:
    L2_DECLARE_OBJECT_REGISTRATION(NpcServer)

    enum : int {
        NPC_SOCKET_MAX = 1,
    };

    struct NPCCreateParam {
        CNPC* pNPC;
        void* pDefine;
        std::int32_t makerId;
        FVector pos;
        std::int32_t dir;
        std::int32_t isMercenary;
        std::int32_t hp;
        std::int32_t mp;
        wchar_t* aiName;
        std::int32_t paramCount;
        std::int32_t bufLen;
        char* pBuf;
        std::int32_t airshipSID;
        std::int32_t inzoneSID;
    };

    NpcServer();
    ~NpcServer() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    L2_DECLARE_OBJECT_ALLOCATORS()

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    static NpcServer* Inst() noexcept;
    NpcSocket* GetNpcSocket();
    int GetNpcSocketCount() const { return m_nNpcSocket; }
    int RemoveEx(std::uint64_t socket);
    void SetConnected(int connected);
    bool IsConnect();
    virtual bool Connected() const noexcept;
    void Send(const char* format, ...);
    void SendNPCCreatePacket(const NPCCreateParam& parameter);

    bool Add(std::uint64_t socket, CSocket* client) override;

private:
    std::int32_t m_nNpcSocket;
    std::array<NpcSocket*, NPC_SOCKET_MAX> m_pNpcSockets;
    std::array<NpcSocket*, NPC_SOCKET_MAX> m_pSocketTable;
    std::array<CYieldLock, NPC_SOCKET_MAX> m_lockTable;
    CYieldLock m_npcSocketSect;
    volatile std::int32_t m_nCurrentSocket;
    volatile std::int32_t m_nConnected;
    std::int32_t m_ConnectTime;
    std::int32_t m_CloseTime;
    CYieldLock m_NPCCountLock;
    ::std::map<int, std::int64_t> m_mapNPCCount;

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(NpcServer)
};


extern NpcServer g_npcserver;

namespace NpcServerUtil {

void SendRequestDecoNpcSpawn(
    const char16_t* npcName, FVector position, int inZoneId, int heading, char decoType, int decoId, int decoLevel
);
void SendRequestNpcSpawn(const char16_t* npcName, FVector position, int inZoneId, const char16_t* aiName, int heading);
void SendRequestFakeNpcSpawn(const char16_t* npcName, FVector position, int inZoneId, int fakeNpcId, int heading);
void SendRequestDroidNpcSpawn(
    const char16_t* npcName,
    FVector position,
    int inZoneId,
    int heading,
    char droidType,
    unsigned int masterId,
    unsigned int groupId
);
void SendDeleteOlympiadTradePointReturned(int npcSid, int userSid, int olympiadPoint, int result);
void SendOlympiadMatchResult(int olympiadGameId, int result);
void SendRequestReservNpcSpawn(
    const char16_t* npcName, FVector position, int inZoneId, const char16_t* aiName, int heading, int reservationId
);

} // namespace NpcServerUtil
