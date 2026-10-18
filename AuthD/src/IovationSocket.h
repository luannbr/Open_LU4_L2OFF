#pragma once

#include "IOBuffer.h"
#include "IOObject.h"
#include "IOSocket.h"
#include "Lock.h"

#include <cstdint>

enum SocketMode : std::int32_t;
class IovationSocket;

// Original owner: Src/iovationsocket.h. L2Authd.pdb type 0x8910, target
// size 0x14. The generic callback spelling is recorded by nested type 0x5565.
class IovationPacket : public CIOObject {
public:
    using IovationPacketFunc = bool (*)(
        CIOSocket*,
        const unsigned char*);

    IovationPacket();
    ~IovationPacket() override;

    static IovationPacket* Alloc();
    static void FreeAll();
    void Free();
    void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped) override;

    CIOSocket* m_pSocket;
    CIOBuffer* m_pBuf;
    IovationPacketFunc m_pFunc;

    static std::int32_t s_PendingPacket;
};

// Original owner: Src/iovationsocket.h. PDB types 0x8906 and 0x88F5.
class IovationPacketPool {
public:
    class CSlot {
    public:
        CSlot();
        ~CSlot() = default;

        IovationPacket* m_pPacket;
        CLock m_lock;
    };

    ~IovationPacketPool();

    static CSlot s_slot[16];
    static std::int32_t s_NumAlloc;
    static std::int32_t s_NumFree;
};

extern IovationPacketPool gIovationPacketPool;

// Original owner: Src/iovationsocket.h. L2Authd.pdb type 0x8900, target
// size 0x68. The class name has no leading C in the selected PDB.
class IovationSocket : public CIOSocket {
public:
    using PacketFunction = bool (*)(
        IovationSocket*,
        const unsigned char*);

    explicit IovationSocket(std::uint32_t socket);
    ~IovationSocket() override;

    static IovationSocket* Allocate(std::uint32_t socket);
    void OnCreate() override;
    void OnRead() override;
    void OnClose() override;
    bool Send(const char* format, ...);

protected:
    std::int32_t packetLen;
    SocketMode m_mode;
    PacketFunction* packetTable;
};

#if defined(_WIN32)
void __stdcall IovationSocketTimerRoutine(
#else
void IovationSocketTimerRoutine(
#endif
    void* parameter,
    std::uint8_t timerOrWaitFired);
bool UnknownPacket(
    IovationSocket* socket,
    const unsigned char* packet);
bool RecvCheckTransactionDetailAck(
    IovationSocket* socket,
    const unsigned char* packet);
