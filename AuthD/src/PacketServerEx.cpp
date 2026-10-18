#include "PacketServerEx.h"

#include "Shared/src/ScopedLockGuards.h"

#include "Shared/src/Runtime/RawAtomic.h"
#include "ClientSession.h"
#include "IOServerEx.h"
#include "SocketServerEx.h"

#include <cstdint>

CPacketServerPoolEx::CSlot CPacketServerPoolEx::g_slot[16];
std::int32_t CPacketServerPoolEx::g_nAlloc = -1;
std::int32_t CPacketServerPoolEx::g_nFree = 0;
std::int32_t CPacketServerEx::g_nPendingPacket = 0;
CPacketServerPoolEx thePacketPoolEx;

CPacketServerEx::CPacketServerEx() = default;
CPacketServerEx::~CPacketServerEx() = default;

// L2Authd.exe 0x0046B7C0, Src/packetserverex.cpp:33-37.
void CPacketServerEx::Init(
    CSocketServerEx* socket,
    CIOBuffer* buffer,
    unsigned int sessionSocket) {
    m_pSocket = socket;
    m_pBuf = buffer;
    m_sessionSock = sessionSocket;
}

// L2Authd.exe 0x0046B7F0, Src/packetserverex.cpp:40-54.
CPacketServerEx* CPacketServerEx::Alloc() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::IncrementI32(
                &CPacketServerPoolEx::g_nAlloc)) &
        0x0fU;
    CPacketServerPoolEx::CSlot& slot =
        CPacketServerPoolEx::g_slot[slotIndex];
    CPacketServerEx* packet = nullptr;
    {
        L2_GUARD(slot.m_lock);
        packet = slot.m_pPacket;
        if (packet != nullptr) {
            slot.m_pPacket =
                reinterpret_cast<CPacketServerEx*>(
                    packet->m_pSocket);
        }
    }
    if (packet == nullptr) {
        packet = new CPacketServerEx;
    }
    return packet;
}

// L2Authd.exe 0x0046B8C0, Src/packetserverex.cpp:67-78.
void CPacketServerEx::FreeAll() {
    for (CPacketServerPoolEx::CSlot& slot :
         CPacketServerPoolEx::g_slot) {
        {
            L2_GUARD(slot.m_lock);
            while (slot.m_pPacket != nullptr) {
                CPacketServerEx* const packet =
                    slot.m_pPacket;
                slot.m_pPacket =
                    reinterpret_cast<CPacketServerEx*>(
                        packet->m_pSocket);
                delete packet;
            }
        }
    }
}

// L2Authd.exe 0x0046B950, Src/packetserverex.cpp:57-64.
void CPacketServerEx::Free() {
    const std::uint32_t slotIndex =
        static_cast<std::uint32_t>(
            runtime::raw_atomic::DecrementI32(
                &CPacketServerPoolEx::g_nFree)) &
        0x0fU;
    CPacketServerPoolEx::CSlot& slot =
        CPacketServerPoolEx::g_slot[slotIndex];
    {
        L2_GUARD(slot.m_lock);
        m_pSocket = reinterpret_cast<CSocketServerEx*>(
            slot.m_pPacket);
        slot.m_pPacket = this;
    }
}

// L2Authd.exe 0x0046B9A0, Src/packetserverex.cpp:81-102.
void CPacketServerEx::OnIOCallback(
    [[maybe_unused]] int bSuccess,
    std::uint32_t dwTransferred,
    [[maybe_unused]] _OVERLAPPED* lpOverlapped) {
    CIOBuffer* const packet = m_pBuf;
    const unsigned int sessionId =
        CIOServerEx::GetSessionId(
            static_cast<char>(m_pSocket->GetGateId()),
            m_sessionSock);
    CClientSession* const session =
        m_pSocket->FindSession(sessionId);
    if (session == nullptr) {
        m_pSocket->SendCloseSession(sessionId);
    } else {
        session->OnRead(
            reinterpret_cast<const unsigned char*>(
                packet->m_buffer + dwTransferred));
        session->ReleaseRef();
    }
    runtime::raw_atomic::DecrementI32(
        &m_pSocket->m_iPendingPacket);
    m_pSocket->ReleaseRef();
    m_pBuf->Release();
    runtime::raw_atomic::DecrementI32(
        &g_nPendingPacket);
    Free();
}

// L2Authd.exe 0x0046BA80, Src/packetserverex.cpp:18.
CPacketServerPoolEx::CSlot::CSlot()
    : m_pPacket(nullptr),
      m_lock(eCustomSpinLock, 0) {}

// L2Authd.exe 0x0046BAD0, Src/packetserverex.cpp:23.
CPacketServerPoolEx::~CPacketServerPoolEx() {
    CPacketServerEx::FreeAll();
}
