#pragma once

#include "IOBuffer.h"
#include "IOObject.h"
#include "IOSocket.h"
#include "Lock.h"

#include <cstdint>

enum SocketMode : std::int32_t;

// Original owner: Src/gradesocket.h. L2Authd.pdb type 0x8C53, target
// size 0x14.
class GradePacket : public CIOObject {
public:
    using GradePacketFunc = bool (*)(
        CIOSocket*,
        const unsigned char*);

    GradePacket();
    ~GradePacket() override;

    static GradePacket* Alloc();
    static void FreeAll();
    void Free();
    void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped) override;

    CIOSocket* m_pSocket;
    CIOBuffer* m_pBuf;
    GradePacketFunc m_pFunc;

    static std::int32_t s_PendingPacket;
};

// Original owner: Src/gradesocket.h. L2Authd.pdb types 0x8C39 and 0x8C3F.
class GradePacketPool {
public:
    class CSlot {
    public:
        CSlot();
        ~CSlot() = default;

        GradePacket* m_pPacket;
        CLock m_lock;
    };

    ~GradePacketPool();

    static CSlot s_slot[16];
    static std::int32_t s_NumAlloc;
    static std::int32_t s_NumFree;
};

extern GradePacketPool gGradePacketPool;

// Original owner: Src/gradesocket.h. L2Authd.pdb type 0x8C49, target
// size 0x64.
class GradeSocket : public CIOSocket {
public:
    explicit GradeSocket(std::uint32_t socket);
    ~GradeSocket() override;

    static GradeSocket* Allocate(std::uint32_t socket);
    void OnCreate() override;
    void OnRead() override;
    void OnClose() override;
    bool Send(const char* format, ...);

protected:
    std::int32_t packetLen;
    SocketMode m_mode;
};

#if defined(_WIN32)
void __stdcall GradeSocketTimerRoutine(
#else
void GradeSocketTimerRoutine(
#endif
    void* lpParam,
    std::uint8_t TimerOrWaitFired);
