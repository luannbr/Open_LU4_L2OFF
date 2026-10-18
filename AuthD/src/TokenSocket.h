#pragma once

#include "IOBuffer.h"
#include "IOObject.h"
#include "IOSocket.h"
#include "Lock.h"

#include <cstdint>

enum SocketMode : std::int32_t;

// Original owner: Src/tokensocket.h. L2Authd.pdb type 0x5565.
using TokenPacketFunc = bool (*)(
    CIOSocket*,
    const unsigned char*);

// Original owner: Src/tokensocket.h. L2Authd.pdb type 0x73AE, target
// size 0x14.
class TokenPacket : public CIOObject {
public:
    TokenPacket();
    ~TokenPacket() override;

    static TokenPacket* Alloc();
    static void FreeAll();
    void Free();
    void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped) override;

    CIOSocket* m_pSocket;
    CIOBuffer* m_pBuf;
    TokenPacketFunc m_pFunc;

    static std::int32_t s_PendingPacket;
};

// Original owner: Src/tokensocket.h. L2Authd.pdb types 0x73A4 and 0x73B4.
class TokenPacketPool {
public:
    class CSlot {
    public:
        CSlot();
        ~CSlot() = default;

        TokenPacket* m_pPacket;
        CLock m_lock;
    };

    ~TokenPacketPool();

    static CSlot s_slot[16];
    static std::int32_t s_NumAlloc;
    static std::int32_t s_NumFree;
};

extern TokenPacketPool gTokenPacketPool;

// Original owner: Src/tokensocket.h. L2Authd.pdb type 0x73C2, target
// size 0x68.
class TokenSocket : public CIOSocket {
public:
    explicit TokenSocket(std::uint32_t socket);
    ~TokenSocket() override;

    static TokenSocket* Allocate(std::uint32_t socket);
    void OnCreate() override;
    void OnRead() override;
    void OnClose() override;
    bool Send(const char* format, ...);

protected:
    std::int32_t packetLen;
    SocketMode m_mode;
    TokenPacketFunc* packetTable;
};

#if defined(_WIN32)
void __stdcall TokenSocketTimerRoutine(
#else
void TokenSocketTimerRoutine(
#endif
    void* parameter,
    std::uint8_t timerOrWaitFired);
