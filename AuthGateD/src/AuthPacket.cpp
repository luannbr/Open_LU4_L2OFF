#include "AuthPacket.h"

#include "AuthPacketPool.h"
#include "AuthSocket.h"
#include "ClientSocketContainer.h"
#include "StateMonMgr.h"

CAuthPacket::CAuthPacket() = default;
CAuthPacket::~CAuthPacket() = default;

// AuthGateD.exe 0x00405430, AuthPacket.cpp.
void CAuthPacket::Init(CAuthSocket* socket, std::uint32_t sessionId, CIOBuffer* buffer, int packetLength) {
    m_pSocket = socket;
    m_sessionId = sessionId;
    m_pBuf = buffer;
    m_packetLen = packetLength;
}

// AuthGateD.exe 0x00405450, AuthPacket.cpp.
CAuthPacket* CAuthPacket::Alloc() { return CAuthPacketPool::g_AuthPacketPool->Alloc(); }

// AuthGateD.exe 0x00405460, AuthPacket.cpp.
void CAuthPacket::OnIOCallback([[maybe_unused]] int success,
                               std::uint32_t transferred,
                               [[maybe_unused]] _OVERLAPPED* overlapped) {
    CClientSocketContainer::g_pClientSocketContainer->SendPacket(
        static_cast<std::int32_t>(m_sessionId), reinterpret_cast<const unsigned char*>(m_pBuf->m_buffer + transferred),
        m_packetLen);
    m_pSocket->ReleaseRef();
    if (m_pBuf->m_ref.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        m_pBuf->Free();
    }
    CAuthPacketPool::g_AuthPacketPool->Free(this);
    CStateMonMgr::g_pStateMonMgr->authPacketPendingCount.fetch_sub(1, std::memory_order_relaxed);
}
