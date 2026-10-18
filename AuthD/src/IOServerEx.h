#pragma once

#include "Platform/CriticalSection.h"
#include "Platform/WindowsTypes.h"
#include "IOObject.h"
#include "Lock.h"

#include <cstdint>
#include <map>
#include <queue>

class CClientSession;
class CSocketServerEx;

// Original owner: Src/ioserverex.h. PDB type 0x458A, target size 0x11C.
struct _OverlappedAccept {
    _OVERLAPPED ol;
    unsigned int sock;
    char buffer[256];
    _OverlappedAccept* next;
};

// Original owner: Src/ioserverex.h. PDB types 0x5BBC and 0x5BC2. The outer
// one-byte object exists only to drain its sixteen accept-operation pools at
// CRT shutdown.
class OverlappedPool {
public:
    class Slot {
    public:
        Slot();
        ~Slot() = default;

        _OverlappedAccept* m_pOverlapped;
        CLock m_lock;
    };

    static Slot m_slot[16];
    static std::int32_t m_alloc;
    static std::int32_t m_free;

    static _OverlappedAccept* Alloc();
    static void Free(_OverlappedAccept* overlapped);
    static void FreeAll();

    ~OverlappedPool();
};

extern OverlappedPool g_overlappedPool;

// Original owner: Src/ioserverex.h / Src/ioserverex.cpp. PDB type 0x64D0,
// target size 0x4C. Access and field order follow field list 0x64CF.
class CIOServerEx : public CIOObject {
public:
    using Allocator = CSocketServerEx* (*)(
        unsigned int socket,
        unsigned char gateId);

    CIOServerEx();
    ~CIOServerEx() override;

    int Create(int port);
    void Close();
    void Stop();
    virtual CSocketServerEx* CreateSocket(
        unsigned int socket,
        sockaddr_in* address);
    void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped) override;
    void OnEventCallback() override;

#if defined(_WIN32)
    static std::uint32_t __stdcall OnAcceptExCallback(
        void* parameter);
#else
    static std::uint32_t OnAcceptExCallback(
        void* parameter);
#endif

    bool WrapAcceptEx();
    void Run(int port, Allocator socketAllocator);

    bool RemoveSocket(unsigned char gateId);
    CSocketServerEx* FindSocket(unsigned char gateId);
    CClientSession* FindSession(unsigned int sessionId);
    bool RemoveSession(unsigned int sessionId);

    // Inline target helpers from Src/ioserverex.h:83-85.
    static unsigned int GetSessionId(
        char gateId,
        unsigned int socketId) {
        return static_cast<unsigned int>(gateId) *
                   0x01000000U +
               socketId;
    }

    static unsigned char GetGateId(
        unsigned int sessionId) {
        return static_cast<unsigned char>(
            sessionId >> 24U);
    }

    static unsigned int GetSocketId(
        unsigned int sessionId) {
        return sessionId & 0x00ffffffU;
    }

    unsigned char GetFreeGateId();

private:
    std::map<unsigned char, CSocketServerEx*> m_socketmap;
    authd::platform::CriticalSectionStorage32 sockSect;
    std::queue<unsigned char> m_GateIdPool;

public:
    std::uint32_t m_hSocket;
    std::uint32_t m_hAcceptSocket;
    void* m_acceptEvent;
    Allocator allocator;
};

extern void* g_hSocketTimer;
extern volatile std::int32_t g_AcceptExThread;
extern CIOServerEx serverEx;
