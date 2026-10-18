#pragma once

#include "Platform/WindowsTypes.h"
#include "IOBuffer.h"
#include "IOObject.h"
#include "Lock.h"

#include <cstdint>

class CSocketServerEx;

// Original owner: Src/packetserverex.h / Src/packetserverex.cpp.
// L2Authd.pdb type 0x7A53, target size 0x1C. The two trailing members are
// part of the shipping object even though packetserverex.cpp does not read
// them in this build.
class CPacketServerEx : public CIOObject {
public:
    CPacketServerEx();
    ~CPacketServerEx() override;

    void Init(CSocketServerEx* socket, CIOBuffer* buffer, unsigned int sessionSocket);
    static CPacketServerEx* Alloc();
    static void FreeAll();
    void Free();

    void OnIOCallback(int bSuccess, std::uint32_t dwTransferred, _OVERLAPPED* lpOverlapped) override;

    static std::int32_t g_nPendingPacket;

private:
    CSocketServerEx* m_pSocket;
    CIOBuffer* m_pBuf;
    unsigned int m_sessionSock;
    [[maybe_unused]] in_addr m_sessionAddr;
    [[maybe_unused]] char m_opcode;
};

// Original owner: Src/packetserverex.h. PDB types 0x8445/0x844B. The outer
// one-byte object exists only so its destructor drains the sixteen pools at
// CRT shutdown.
class CPacketServerPoolEx {
public:
    class CSlot {
    public:
        CSlot();
        ~CSlot() = default;

        CPacketServerEx* m_pPacket;
        CLock m_lock;
    };

    ~CPacketServerPoolEx();

    static CSlot g_slot[16];
    static std::int32_t g_nAlloc;
    static std::int32_t g_nFree;
};

extern CPacketServerPoolEx thePacketPoolEx;
