#pragma once

#include "IOBuffer.h"
#include "IOObject.h"
#include "IOSocket.h"
#include "Lock.h"

#include <cstdint>

enum SocketMode : std::int32_t;
class CPBSSocket;

// Original owner: Src/pbssocket.h. The socket table uses the derived type.
// CPBSPacket stores the ABI-equivalent CIOSocket callback from PDB 0x5565.
using PBSPacketHandler = bool (*)(
    CPBSSocket*,
    const unsigned char*);
using PBSPacketFunc = bool (*)(
    CIOSocket*,
    const unsigned char*);

// Original owner: Src/pbssocket.h. L2Authd.pdb type 0x4069, target
// size 0x68. Access and member order follow field list 0x4068.
class CPBSSocket : public CIOSocket {
public:
    explicit CPBSSocket(std::uint32_t socket);
    ~CPBSSocket() override;

    static CPBSSocket* Allocate(std::uint32_t socket);
    void OnCreate() override;
    void OnRead() override;
    void OnClose() override;
    void OnTimerCallback() override;
    bool Send(const char* format, ...);

protected:
    std::int32_t packetLen;
    SocketMode mode;
    PBSPacketHandler* packetTable;
};

// Original owner: Src/pbssocket.h. L2Authd.pdb type 0x6F94, target
// size 0x14.
class CPBSPacket : public CIOObject {
public:
    CPBSPacket();
    ~CPBSPacket() override;

    static CPBSPacket* Alloc();
    static void FreeAll();
    void Free();
    void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped) override;

    CIOSocket* m_pSocket;
    CIOBuffer* m_pBuf;
    PBSPacketFunc m_pFunc;

    static std::int32_t s_PendingPacket;
};

// Original owner: Src/pbssocket.h. L2Authd.pdb types 0x423A/0x4242.
class CPBSPacketPool {
public:
    class CSlot {
    public:
        CSlot();
        ~CSlot() = default;

        CPBSPacket* m_pPacket;
        CLock m_lock;
    };

    ~CPBSPacketPool();

    static CSlot s_slot[16];
    static std::int32_t s_NumAlloc;
    static std::int32_t s_NumFree;
};

extern CPBSPacketPool gPBSPacketPool;

#if defined(_WIN32)
void __stdcall PBSSocketTimerRoutine(
#else
void PBSSocketTimerRoutine(
#endif
    void* parameter,
    std::uint8_t timerOrWaitFired);
