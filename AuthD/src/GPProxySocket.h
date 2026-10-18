#pragma once

#include "Config.h"
#include "IOBuffer.h"
#include "IOObject.h"
#include "IOSocket.h"
#include "Lock.h"

#include <cstdint>

enum SocketMode : std::int32_t;
class CGpProxySocket;

// Original owner: Src/gpproxysocket.h. L2Authd.pdb type 0x1423.
namespace EConnDetermine {

enum Type : std::int32_t {
    Keep = 0,
    Disconnect = 1,
};

} // namespace EConnDetermine

using GpProxyPacketFunction = EConnDetermine::Type (*)(
    CGpProxySocket*,
    const unsigned char*);

// Original owner: Src/gpproxysocket.h. L2Authd.pdb type 0x142D, target
// size 0x74. Field offsets and access follow field list 0x142B.
class CGpProxySocket : public CIOSocket {
public:
    CGpProxySocket(
        std::uint32_t socket,
        ConnectionInformation& connectionInformation);
    ~CGpProxySocket() override;

    static CGpProxySocket* Allocate(
        std::uint32_t socket,
        ConnectionInformation& connectionInformation);
    void OnCreate() override;
    void OnRead() override;
    void OnClose() override;
    void OnTimerCallback() override;
    bool Send(const char* format, ...);

    void* m_hReconnectTimer;
    in_addr m_host;
    std::uint16_t m_port;

protected:
    std::int32_t m_packetLen;
    SocketMode m_mode;
    GpProxyPacketFunction* m_packetTable;
};

// Original owner: Src/gpproxysocket.h. L2Authd.pdb type 0x63D1, target
// size 0x14. The named union is type 0x63D3.
class CGpProxyPacket : public CIOObject {
public:
    CGpProxyPacket();
    ~CGpProxyPacket() override;

    static CGpProxyPacket* Alloc();
    static void FreeAll();
    void Free();
    void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped) override;

    union {
        CGpProxyPacket* m_pNextPacket;
        CGpProxySocket* m_pSocket;
    } m_unionPtr;
    CIOBuffer* m_pBuf;
    GpProxyPacketFunction m_pFunc;

    static std::int32_t s_pendingPacket;
};

// Original owner: Src/gpproxysocket.h. L2Authd.pdb type 0x153B. The object
// itself is one byte. Its sixteen 0x24-byte slots are static storage.
class CGpProxyPacketPool {
public:
    class CSlot {
    public:
        CSlot();
        ~CSlot() = default;

        CGpProxyPacket* m_pPacket;
        CLock m_lock;
    };

    ~CGpProxyPacketPool();

    static CSlot s_slot[16];
    static std::int32_t s_numAlloc;
    static std::int32_t s_numFree;
};

extern CGpProxyPacketPool g_gpProxyPacketPool;

#if defined(_WIN32)
void __stdcall GpProxySocketTimerRoutine(
#else
void GpProxySocketTimerRoutine(
#endif
    void* parameter,
    std::uint8_t timerOrWaitFired);
