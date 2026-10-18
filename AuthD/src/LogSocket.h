#pragma once

#include "Platform/WindowsTypes.h"
#include "IOBuffer.h"
#include "IOObject.h"
#include "IOSocket.h"
#include "Lock.h"

#include <cstdint>

enum SocketMode : std::int32_t;
class CLogSocket;

// Original owner: Src/logsocket.h. L2Authd.pdb type 0x85E6, target
// size 0x14. The nested callback spelling and all fields come from field
// list 0x85E5.
class CLogPacketServer : public CIOObject {
public:
    using LOGPacketFunc = bool (*)(
        CIOSocket*,
        const unsigned char*);

    CLogPacketServer();
    ~CLogPacketServer() override;

    static CLogPacketServer* Alloc();
    static void FreeAll();
    void Free();
    void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped) override;

    CIOSocket* m_pSocket;
    CIOBuffer* m_pBuf;
    LOGPacketFunc m_pFunc;

    static std::int32_t g_nPendingPacket;
};

// Original owner: Src/logsocket.h. L2Authd.pdb types 0x85D5 and 0x85DC.
class CLOGPacketServerPool {
public:
    class CSlot {
    public:
        CSlot();
        ~CSlot() = default;

        CLogPacketServer* m_pPacket;
        CLock m_lock;
    };

    ~CLOGPacketServerPool();

    static CSlot g_slot[16];
    static std::int32_t g_nAlloc;
    static std::int32_t g_nFree;
};

extern CLOGPacketServerPool theLOGPacketPool;

// Original owner: Src/logsocket.h. L2Authd.pdb type 0x67A8, field list
// 0x67A7, target size 0x88. SetAddress is compiled from this header at
// 0x00464BB0 in L2AuthD.obj. Getaddr is the matching un-compiled inline.
class CLogSocket : public CIOSocket {
public:
    explicit CLogSocket(std::uint32_t sock);
    ~CLogSocket() override;

    static CLogSocket* Allocate(std::uint32_t s);
    void OnCreate() override;
    void OnRead() override;
    void OnClose() override;
    void OnTimerCallback() override;

    in_addr getaddr() {
        return addr;
    }

    void SetAddress(in_addr address) {
        addr = address;
    }

    void Send(const char* format, ...);
    void Send2(const char* format, ...);
    const char* IP();

    std::uint32_t m_socket;
    char serverid;

protected:
    std::int32_t packetLen;
    SocketMode mode;
    bool (**packetTable)(
        CLogSocket*,
        const unsigned char*);
    in_addr addr;
    sockaddr_in LogDAddress;
    char* host;
};

extern bool LogDReconnect;
extern bool g_opFlag;
extern void* g_hLogDTimer;
extern CRWLock gLogLock;
extern CLogSocket* pLogSocket;

#if defined(_WIN32)
void __stdcall LOGDTimerRoutine(
    void* parameter,
    std::uint8_t timerOrWaitFired);
#else
void LOGDTimerRoutine(
    void* parameter,
    std::uint8_t timerOrWaitFired);
#endif
