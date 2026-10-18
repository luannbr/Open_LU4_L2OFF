#pragma once

#include "IOBuffer.h"
#include "IOObject.h"
#include "IOSocket.h"
#include "Lock.h"

#include <cstdint>

enum SocketMode : std::int32_t;

// Original owner: Src/wsmsocket.h. L2Authd.pdb type 0x5565.
using WSMPacketFunc = bool (*)(
    CIOSocket*,
    const unsigned char*);

// Original owner: Src/wsmsocket.h. L2Authd.pdb type 0x69DC, target
// size 0x14.
class CWSMPacket : public CIOObject {
public:
    CWSMPacket();
    ~CWSMPacket() override;

    static CWSMPacket* Alloc();
    static void FreeAll();
    void Free();
    void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped) override;

    CIOSocket* m_pSocket;
    CIOBuffer* m_pBuf;
    WSMPacketFunc m_pFunc;

    static std::int32_t s_PendingPacket;
};

// Original owner: Src/wsmsocket.h. L2Authd.pdb types 0x6CC8 and 0x6CCE.
class CWSMPacketPool {
public:
    class CSlot {
    public:
        CSlot();
        ~CSlot() = default;

        CWSMPacket* m_pPacket;
        CLock m_lock;
    };

    ~CWSMPacketPool();

    static CSlot s_slot[16];
    static std::int32_t s_NumAlloc;
    static std::int32_t s_NumFree;
};

// This spelling, including the extra S, is the PDB global name.
extern CWSMPacketPool gWSMSPacketPool;

// Original owner: Src/wsmsocket.h. L2Authd.pdb type 0x644E, target
// size 0x68.
class CWSMSocket : public CIOSocket {
public:
    explicit CWSMSocket(std::uint32_t socket);
    ~CWSMSocket() override;

    static CWSMSocket* Allocate(std::uint32_t socket);
    void OnCreate() override;
    void OnRead() override;
    void OnClose() override;
    void OnTimerCallback() override;
    bool Send(const char* format, ...);

protected:
    std::int32_t packetLen;
    SocketMode mode;
    WSMPacketFunc* packetTable;
};

#if defined(_WIN32)
void __stdcall WSMSocketTimerRoutine(
#else
void WSMSocketTimerRoutine(
#endif
    void* parameter,
    std::uint8_t timerOrWaitFired);
