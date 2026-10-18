#pragma once

#include "Platform/WindowsTypes.h"
#include "Config.h"
#include "IOBuffer.h"
#include "IOObject.h"
#include "IOSocket.h"
#include "Lock.h"

#include <cstdint>

enum SocketMode : std::int32_t;
class CPASocket;

// Original owner: Src/pasocket.h. The PDB records the static dispatch table
// with CPASocket handlers, while CPAPacket stores the equivalent CIOSocket
// callback type (types 0x120D and 0x5575 respectively).
using PAPacketHandler = bool (*)(
    CPASocket*,
    const unsigned char*);
using PAPacketFunc = bool (*)(
    CIOSocket*,
    const unsigned char*);

// Original owner: Src/pasocket.h. L2Authd.pdb type 0x5EDC, target
// size 0x74. Field order and access follow field list 0x5EDB.
class CPASocket : public CIOSocket {
public:
    CPASocket(
        std::uint32_t socket,
        ConnectionInformation information);
    ~CPASocket() override;

    static CPASocket* Allocate(
        std::uint32_t socket,
        ConnectionInformation information);
    void OnCreate() override;
    void OnRead() override;
    void OnClose() override;
    void OnTimerCallback() override;
    bool Send(const char* format, ...);

    void* hPAReconnectTimer;
    in_addr host;
    std::uint16_t port;

protected:
    std::int32_t packetLen;
    SocketMode mode;
    PAPacketHandler* packetTable;
};

// Original owner: Src/pasocket.h. L2Authd.pdb type 0x5575, target
// size 0x14.
class CPAPacket : public CIOObject {
public:
    CPAPacket();
    ~CPAPacket() override;

    static CPAPacket* Alloc();
    static void FreeAll();
    void Free();
    void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped) override;

    CIOSocket* m_pSocket;
    CIOBuffer* m_pBuf;
    PAPacketFunc m_pFunc;

    static std::int32_t s_PendingPacket;
};

// Original owner: Src/pasocket.h. L2Authd.pdb types 0x13CF and 0x13C5.
class CPAPacketPool {
public:
    class CSlot {
    public:
        CSlot();
        ~CSlot() = default;

        CPAPacket* m_pPacket;
        CLock m_lock;
    };

    ~CPAPacketPool();

    static CSlot s_slot[16];
    static std::int32_t s_NumAlloc;
    static std::int32_t s_NumFree;
};

extern CPAPacketPool gPASPacketPool;

#if defined(_WIN32)
void __stdcall PASocketTimerRoutine(
#else
void PASocketTimerRoutine(
#endif
    void* parameter,
    std::uint8_t timerOrWaitFired);
