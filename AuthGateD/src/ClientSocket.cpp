#include "ClientSocket.h"

#include "AuthGateConfig.h"
#include "AuthGateSvcMgr.h"
#include "AuthPacket.h"
#include "AuthSocket.h"
#include "ClientKeyMatrix.h"
#include "ClientPacket.h"
#include "ClientSocketContainer.h"
#include "FileLog.h"
#include "RSAKeyPool.h"
#include "StateMonMgr.h"
#include "GlobalFunc.h"
#include "Lu4Login.h"

#include "Shared/src/Runtime/TickCount32.h"
#include "Shared/src/Runtime/LegacyCrtRandom.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <ctime>

namespace {

void StoreUInt32(
    unsigned char* destination,
    std::uint32_t value) noexcept {
    // The selected target is x86 and writes each matrix DWORD directly.
    // Spell the byte order out so the wire key is identical on every host.
    destination[0] = static_cast<unsigned char>(value);
    destination[1] = static_cast<unsigned char>(value >> 8U);
    destination[2] = static_cast<unsigned char>(value >> 16U);
    destination[3] = static_cast<unsigned char>(value >> 24U);
}

void CopyGameGuardData(
    _GG_AUTH_DATA& destination,
    const _GG_AUTH_DATA& source) noexcept {
    destination = source;
}

void LogInvalidGameGuard(
    std::uint32_t result,
    const _GG_AUTH_DATA& answer,
    const _GG_AUTH_DATA& query,
    const in_addr& address) {
    char16_t addressText[20]{};
    botLog.LogT(
        LOG_NORMAL,
        u"%d,%p%p%p%p,%p%p%p%p,%s",
        result,
        reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(answer.dwIndex)),
        reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(answer.dwValue1)),
        reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(answer.dwValue2)),
        reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(answer.dwValue3)),
        reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(query.dwIndex)),
        reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(query.dwValue1)),
        reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(query.dwValue2)),
        reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(query.dwValue3)),
        InetToStr(&address, addressText, 20));
}

// Pooled buffers keep the bytes of earlier packets. The encryption pads the
// body to 8 bytes (plus the checksum word) with whatever follows it, and the
// client reads into that padding: AuthD sends a login failure reason as one
// byte, the client reads four, and leftover bytes made an unknown reason that
// closed the client. Zero what the padding and checksum can cover.
void ZeroEncryptionPadding(CIOBuffer* buffer, int usedBytes) noexcept {
    constexpr int kPaddingBytes = 4 + 7 + 8;
    const int end = std::min(usedBytes + kPaddingBytes, static_cast<int>(CIOBuffer::kCapacity));
    std::fill(buffer->m_buffer + usedBytes, buffer->m_buffer + end, '\0');
}

} // namespace

// AuthGateD.exe 0x00407550, ClientSocket.cpp.
void CClientSocket::GenerateInitialKey(std::int16_t seed) {
    const std::uint16_t value = static_cast<std::uint16_t>(seed);
    const std::uint32_t low = value & UINT32_C(0xff);
    const std::uint32_t high = value >> 8U;
    const std::uint32_t mixed = low ^ high;
    for (std::uint32_t index = 0; index < 3; ++index) {
        StoreUInt32(
            mFirstKey.data() + index * 4U,
            sGenSourceMatrix[(index & UINT32_C(0xff)) ^ mixed] ^
                sGenSourceMatrix[low] ^
                sGenSourceMatrix[high]);
    }
    std::fill(
        mFirstKey.begin() + 12,
        mFirstKey.end(),
        static_cast<unsigned char>('l'));
}

// AuthGateD.exe 0x004075D0, ClientSocket.cpp.
void CClientSocket::GenerateSecondKey() {
    // AuthGateD.exe 0x0041B13F/0x0041B14C store the MSVC CRT seed in
    // _getptd()+0x14. LegacyCrt* keeps both that per-thread ownership and
    // the exact 214013/2531011 sequence on non-Windows hosts.
    LegacyCrtSrand(static_cast<std::uint32_t>(std::time(nullptr)));
    const std::uint32_t index =
        static_cast<std::uint32_t>(LegacyCrtRand()) & UINT32_C(0xff);
    StoreUInt32(mSecondKey.data(), sGenSourceMatrix[index]);
    StoreUInt32(
        mSecondKey.data() + 4,
        sGenSourceMatrix[index & UINT32_C(0xc1)]);
    StoreUInt32(
        mSecondKey.data() + 8,
        sGenSourceMatrix[index & UINT32_C(0xf2)]);
    StoreUInt32(
        mSecondKey.data() + 12,
        sGenSourceMatrix[index & UINT32_C(0x23)]);
}

// AuthGateD.exe 0x00407650, ClientSocket.cpp.
CClientSocket::CClientSocket(std::uint32_t socket) {
    m_hSocket = socket;
    CStateMonMgr::g_pStateMonMgr->socketCount.fetch_add(
        1,
        std::memory_order_relaxed);
    m_iRSAKeyIndex = CRSAKeyPool::m_pInstance->GetKeyIndex();
    m_mode = SM_READ_LEN;
    m_bAuthClose = false;
    m_iSessionId =
        CClientSocketContainer::g_pClientSocketContainer->GetSessionID();
    m_ggVersionPass = false;
    m_ggCSResult = true;
    AddRef();
}

// AuthGateD.exe 0x00407720, ClientSocket.cpp.
CClientSocket::~CClientSocket() {
    CStateMonMgr::g_pStateMonMgr->socketCount.fetch_sub(
        1,
        std::memory_order_relaxed);
}

// AuthGateD.exe 0x00416410, AuthGateSvcMgr.cpp.
CIOSocket* CClientSocket::Allocate(std::uint32_t socket) {
    return new CClientSocket(socket);
}

CIOSocket* CClientSocket::AllocateLu4(std::uint32_t socket) {
    auto* const client = new CClientSocket(socket);
    client->m_lu4 = true;
    return client;
}

// AuthGateD.exe 0x004077B0, ClientSocket.cpp.
void CClientSocket::OnClose(std::uint32_t socket) {
    if (!m_bAuthClose) {
        CAuthSocket::g_pAuthSocket->SendCltDisconnect(m_iSessionId);
    }
    m_mode = SM_CLOSE;
    CClientSocketContainer::g_pClientSocketContainer->RemoveSocket(socket);
}

// AuthGateD.exe 0x004077F0, ClientSocket.cpp.
void CClientSocket::OnRead() {
    CIOBuffer* const readBuffer = m_pReadBuf;
    const int available = static_cast<int>(readBuffer->m_size);
    int offset = 0;

    if (m_mode == SM_CLOSE) {
        CloseSocket();
        return;
    }

    while (offset < available) {
        if (m_mode == SM_READ_LEN) {
            if (offset + 3 > available) {
                Read(static_cast<std::uint32_t>(available - offset));
                return;
            }
            const auto* const packet =
                reinterpret_cast<const unsigned char*>(
                    readBuffer->m_buffer + offset);
            m_packetLen =
                static_cast<std::int32_t>(packet[0]) +
                static_cast<std::int32_t>(packet[1]) * 0x100 -
                2;
            if (m_packetLen < 1 || m_packetLen > 0x1ffb) {
                // CFileLog::g_pDefaultFileLog->LogT(
                //     LOG_ERROR,
                //     u"%d: bad packet size %d",
                //     static_cast<std::int32_t>(m_hSocket),
                //     m_packetLen)
                // CFileLog::g_pDefaultFileLog->LogT(LOG_WARN,
                //     u"component=client event=invalid_packet_length socket=%u session=%d body_bytes=%d min_bytes=1 max_bytes=8187 action=close_connection",
                //     m_hSocket, m_iSessionId, m_packetLen)
                // CFileLog::g_pDefaultFileLog->LogT(LOG_WARN,
                //     u"[client] Invalid packet on socket %u, session %d: body length %d is outside 1..8187 bytes. Closing the connection.",
                //     m_hSocket, m_iSessionId, m_packetLen)
                L2_LOG_WARN(*CFileLog::g_pDefaultFileLog,
                    u"[client] Invalid packet on socket %u, session %d: body length %d is outside 1..8187 bytes. Closing the connection.",
                    m_hSocket, m_iSessionId, m_packetLen);
                CloseSocket();
                return;
            }
            offset += 2;
            m_mode = SM_READ_BODY;
            continue;
        }

        if (m_mode != SM_READ_BODY) {
            CloseSocket();
            return;
        }
        if (offset + m_packetLen > available) {
            Read(static_cast<std::uint32_t>(available - offset));
            return;
        }

        // Original behavior bug: AuthGateD.exe 0x407891 computes buffer + 2
        // even for later coalesced frames or a compacted partial body. Decrypt
        // the actual body offset, including zero after Read compacts it.
        // Original condition (the failure-handling body below is unchanged):
        // if (!mSecondCrypt.DecryptSecondary(
        //         reinterpret_cast<unsigned char*>(
        //             readBuffer->m_buffer + 2),
        //         m_packetLen)) {
        if (m_lu4) {
            // The LU4 server-select packet carries data where the checksum
            // should be; its fields decrypt correctly, so the check is skipped.
            BlowfishDecryptLocal(
                &mSecondCrypt.m_mtx,
                reinterpret_cast<unsigned char*>(readBuffer->m_buffer + offset),
                m_packetLen & ~7);
        } else if (!mSecondCrypt.DecryptSecondary(
                reinterpret_cast<unsigned char*>(
                    readBuffer->m_buffer + offset),
                m_packetLen)) {
            // CFileLog::g_pDefaultFileLog->LogDbgT(
            //     LOG_ERROR,
            //     u"unknown protocol,decrypt error")
            // CFileLog::g_pDefaultFileLog->LogT(LOG_WARN,
            //     u"component=client event=packet_decryption_failed socket=%u session=%d body_bytes=%d action=close_connection",
            //     m_hSocket, m_iSessionId, m_packetLen)
            // CFileLog::g_pDefaultFileLog->LogT(LOG_WARN,
            //     u"[client] Could not decrypt a packet on socket %u, session %d (%d body bytes). Closing the connection.",
            //     m_hSocket, m_iSessionId, m_packetLen)
            L2_LOG_WARN(*CFileLog::g_pDefaultFileLog,
                u"[client] Could not decrypt a packet on socket %u, session %d (%d body bytes). Closing the connection.",
                m_hSocket, m_iSessionId, m_packetLen);
            CloseSocket();
            return;
        }

        auto opcode = static_cast<unsigned char>(
            readBuffer->m_buffer[offset]);
        if (m_lu4) {
            const int mapped = lu4::MapClientOpcode(opcode);
            opcode = mapped < 0 ? 0xff : static_cast<unsigned char>(mapped);
        }
        if (opcode >= 0x13) {
            // CFileLog::g_pDefaultFileLog->LogT(
            //     LOG_ERROR,
            //     u"unknown protocol %d",
            //     static_cast<int>(opcode))
            // CFileLog::g_pDefaultFileLog->LogT(LOG_WARN,
            //     u"component=client event=unknown_opcode socket=%u session=%d opcode=%u action=close_connection",
            //     m_hSocket, m_iSessionId, static_cast<unsigned int>(opcode))
            // CFileLog::g_pDefaultFileLog->LogT(LOG_WARN,
            //     u"[client] Unknown opcode on socket %u, session %d: %u. Closing the connection.",
            //     m_hSocket, m_iSessionId, static_cast<unsigned int>(opcode))
            L2_LOG_WARN(*CFileLog::g_pDefaultFileLog,
                u"[client] Unknown opcode on socket %u, session %d: %u. Closing the connection.",
                m_hSocket, m_iSessionId, static_cast<unsigned int>(opcode));
            CloseSocket();
            return;
        }

        CClientPacket* const packet = CClientPacket::Alloc();
        packet->Init(
            this,
            readBuffer,
            static_cast<char>(opcode),
            m_packetLen);
        AddRef();
        readBuffer->m_ref.fetch_add(
            1,
            std::memory_order_relaxed);
        CStateMonMgr::g_pStateMonMgr->clientPacketPendingCount.fetch_add(
            1,
            std::memory_order_relaxed);
        packet->PostObject(
            offset,
            CAuthGateSvcMgr::g_pAuthGateSvcMgr->m_hWorkIOCP);

        offset += m_packetLen;
        m_mode = SM_READ_LEN;
        m_iLastIO = runtime::TickCount32();
    }

    Read(0);
}

// AuthGateD.exe 0x004079D0, ClientSocket.cpp.
bool CClientSocket::CheckSessionId(
    int sessionId,
    const char16_t* methodName) {
    if (sessionId == m_iSessionId) {
        return true;
    }
    // CFileLog::g_pDefaultFileLog->LogT(
    //     LOG_WARN,
    //     u"Session id mismatched.(%s) sessionId:%d, m_iSessionId:%d, IP:%x",
    //     methodName,
    //     sessionId,
    //     m_iSessionId,
    //     m_addr.s_addr)
    char16_t addressText[20]{};
    // CFileLog::g_pDefaultFileLog->LogT(LOG_WARN,
    //     u"component=client event=session_mismatch operation=%s socket=%u session=%d received_session=%d peer=%s action=reject_request",
    //     methodName, m_hSocket, m_iSessionId, sessionId,
    //     InetToStr(&m_addr, addressText, 20))
    // CFileLog::g_pDefaultFileLog->LogT(LOG_WARN,
    //     u"[client] Rejecting %s on socket %u: expected session %d, received %d from %s.",
    //     methodName, m_hSocket, m_iSessionId, sessionId,
    //     InetToStr(&m_addr, addressText, 20))
    L2_LOG_WARN(*CFileLog::g_pDefaultFileLog,
        u"[client] Rejecting %s on socket %u: expected session %d, received %d from %s.",
        methodName, m_hSocket, m_iSessionId, sessionId,
        InetToStr(&m_addr, addressText, 20));
    return false;
}

// AuthGateD.exe 0x00407A10, ClientSocket.cpp.
bool CClientSocket::GetGameGuardQuery(_GG_AUTH_DATA* query) {
    if (!query) {
        return false;
    }
    if (!config.useGameGuard) {
        *query = {};
        return true;
    }

    const std::uint32_t result = m_csa.GetAuthQuery();
    CopyGameGuardData(*query, m_csa.m_AuthQuery);
    if (result != 0 && config.checkGameGuard) {
        // CFileLog::g_pDefaultFileLog->LogDbgT(
        //     LOG_ERROR,
        //     u"GameGuard GetAuthQuery failed. %d",
        //     result)
        // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
        //     u"component=gameguard event=query_failed socket=%u session=%d provider_code=%u enforcement=enabled",
        //     m_hSocket, m_iSessionId, result)
        // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
        //     u"[gameguard] Query failed on socket %u, session %d (provider code %u). GameGuard checking is enforced.",
        //     m_hSocket, m_iSessionId, result)
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"[gameguard] Query failed on socket %u, session %d (provider code %u). GameGuard checking is enforced.",
            m_hSocket, m_iSessionId, result);
        return false;
    }
    return true;
}

// AuthGateD.exe 0x00407AC0, ClientSocket.cpp.
bool CClientSocket::CheckGameGuardAnswer(_GG_AUTH_DATA* answer) {
    if (!m_ggVersionPass) {
        return false;
    }
    if (!config.useGameGuard) {
        return true;
    }

    CopyGameGuardData(m_csa.m_AuthAnswer, *answer);
    const std::uint32_t result = m_csa.CheckAuthAnswer();
    if (result == 0) {
        return true;
    }

    m_ggCSResult = false;
    if (config.ggLogInvalid) {
        LogInvalidGameGuard(
            result,
            *answer,
            m_csa.m_AuthQuery,
            m_addr);
    }
    return !config.checkGameGuard;
}

// AuthGateD.exe 0x00407C10, ClientSocket.cpp.
bool CClientSocket::CheckGameGuardVersion(_GG_AUTH_DATA* answer) {
    if (!config.useGameGuard) {
        m_ggVersionPass = true;
        return true;
    }

    CopyGameGuardData(m_csa.m_AuthAnswer, *answer);
    const std::uint32_t result = m_csa.CheckAuthAnswer();
    if (result != 0) {
        if (config.ggLogInvalid) {
            LogInvalidGameGuard(
                result,
                *answer,
                m_csa.m_AuthQuery,
                m_addr);
        }
        m_ggVersionPass = false;
        if (config.checkGameGuard) {
            return false;
        }
    }

    m_ggVersionPass = true;
    return true;
}

// AuthGateD.exe 0x00407D50, ClientSocket.cpp.
bool CClientSocket::SendProtocolVer() {
    CIOBuffer* const buffer = CIOBuffer::Alloc();
    if (m_lu4) {
        // Sent as captured, length included and unencrypted.
        const std::vector<unsigned char>& init = lu4::InitPacket();
        std::copy(init.begin(), init.end(), buffer->m_buffer);
        buffer->m_size = static_cast<std::uint32_t>(init.size());
        Write(buffer);
        m_iLastIO = runtime::TickCount32();
        return true;
    }
    m_csa.Init();

    _GG_AUTH_DATA query{};
    if (!GetGameGuardQuery(&query)) {
        // The target leaks this allocation on the error path. Returning the
        // pool reference is the portable resource-safety correction.
        buffer->Free();
        return false;
    }

    std::array<char, CRSAKeyPool::kModulusBytes> publicKey{};
    CRSAKeyPool::m_pInstance->GetNKey(
        m_iRSAKeyIndex,
        publicKey.data());

    const std::uint32_t authProtocol = static_cast<std::uint32_t>(
        CAuthSocket::g_pAuthSocket->m_protocolVer);
    int encryptedLength = Assemble(
        buffer->m_buffer + 2,
        static_cast<int>(CIOBuffer::kCapacity),
        "cddbbbcccc",
        0,
        static_cast<std::uint32_t>(m_iSessionId),
        authProtocol,
        static_cast<std::uint32_t>(publicKey.size()),
        publicKey.data(),
        static_cast<std::uint32_t>(sizeof(query)),
        &query,
        static_cast<std::uint32_t>(mSecondKey.size()),
        mSecondKey.data(),
        config.loginType,
        static_cast<int>(config.companyCode[0]),
        static_cast<int>(config.companyCode[1]),
        static_cast<int>(config.companyCode[2]));
    ZeroEncryptionPadding(buffer, 2 + encryptedLength);
    encryptedLength += 4;
    mFirstCrypt.EncryptPrimary(
        reinterpret_cast<unsigned char*>(buffer->m_buffer + 2),
        &encryptedLength);

    const std::uint32_t packetLength =
        static_cast<std::uint32_t>(encryptedLength + 2);
    buffer->m_buffer[0] = static_cast<char>(packetLength);
    buffer->m_buffer[1] = static_cast<char>(packetLength >> 8U);
    buffer->m_size = packetLength;
    Write(buffer);
    m_iLastIO = runtime::TickCount32();
    return true;
}

// AuthGateD.exe 0x00407E80, ClientSocket.cpp.
void CClientSocket::Send(const char* format, ...) {
    if (m_mode == SM_CLOSE) {
        return;
    }

    CIOBuffer* const buffer = CIOBuffer::Alloc();
    va_list arguments;
    va_start(arguments, format);
    const int bodyLength = Assemble(
        buffer->m_buffer + 2,
        static_cast<int>(CIOBuffer::kCapacity - 2U),
        format,
        arguments);
    va_end(arguments);

    if (bodyLength == 0) {
        // const std::u16string legacyFormat =
        //     authgated::portable::LegacyWidePacketFormat(format)
        // CFileLog::g_pDefaultFileLog->LogT(
        //     LOG_ERROR,
        //     u"%d: assemble too large packet. format %s",
        //     static_cast<std::int32_t>(m_hSocket),
        //     legacyFormat.c_str())
        // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
        //     u"component=client event=packet_assembly_failed socket=%u session=%d body_capacity_bytes=%u action=discard_packet",
        //     m_hSocket, m_iSessionId, static_cast<unsigned int>(CIOBuffer::kCapacity - 2U))
        // CFileLog::g_pDefaultFileLog->LogT(LOG_ERROR,
        //     u"[client] Could not assemble a packet on socket %u, session %d (body capacity %u bytes). Discarding the packet.",
        //     m_hSocket, m_iSessionId, static_cast<unsigned int>(CIOBuffer::kCapacity - 2U))
        // Original: L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
        //     u"[client] Could not assemble a packet on socket %u, session %d (body capacity %u bytes). Discarding the packet.",
        //     m_hSocket, m_iSessionId, static_cast<unsigned int>(CIOBuffer::kCapacity - 2U))
        L2_LOG_ERROR(*CFileLog::g_pDefaultFileLog,
            u"Cannot assemble a client reply on socket %u, session %d (format '%S', body capacity %u bytes). Discarding the packet before encryption or sending.",
            m_hSocket, m_iSessionId, format, static_cast<unsigned int>(CIOBuffer::kCapacity - 2U));
        // Same safety correction as CLogSocket::Send's identical branch.
        buffer->Free();
        return;
    }

    // Noise: per-packet byte counts add no outcome. The recovered debug sink is a no-op.
    // CFileLog::g_pDefaultFileLog->LogDbgT(
    //     LOG_WARN,
    //     u"Send %d byte",
    //     bodyLength + 2)
    ZeroEncryptionPadding(buffer, 2 + bodyLength);
    // The LU4 service aligns the body itself (no extra 4 bytes), as captured.
    int encryptedLength = m_lu4 ? bodyLength : bodyLength + 4;
    mSecondCrypt.EncryptSecondary(
        reinterpret_cast<unsigned char*>(buffer->m_buffer + 2),
        &encryptedLength);
    const std::uint32_t packetLength =
        static_cast<std::uint32_t>(encryptedLength + 2);
    buffer->m_buffer[0] = static_cast<char>(packetLength);
    buffer->m_buffer[1] = static_cast<char>(packetLength >> 8U);
    buffer->m_size = packetLength;
    Write(buffer);
    m_iLastIO = runtime::TickCount32();
}

// AuthGateD.exe 0x00407F50, ClientSocket.cpp.
void CClientSocket::CloseSocketByAuthD() {
    m_bAuthClose = true;
    CloseSocket();
}

// AuthGateD.exe 0x00407F60, ClientSocket.cpp.
void CClientSocket::CloseClientSocket(void* parameter) {
    auto* const socket = static_cast<CClientSocket*>(parameter);
    socket->CloseSocket();
    socket->ReleaseRef();
}

// AuthGateD.exe 0x00407FA0, ClientSocket.cpp.
void CClientSocket::OnCreate() {
    CAuthSocket::g_pAuthSocket->SendCltConnect(
        m_iSessionId,
        m_addr);
    if (config.useGameGuard) {
        m_csa.Init();
    }
    GenerateInitialKey(0x4bd);
    GenerateSecondKey();
    mFirstCrypt.Init(mFirstKey.data(), 16);
    mSecondCrypt.Init(mSecondKey.data(), 16);
    if (m_lu4) {
        // The LU4 client derives its key from the official Init (Lu4Login.h).
        mSecondCrypt.m_mtx = lu4::KeyTable();
    }
    if (!SendProtocolVer()) {
        CloseSocket();
        return;
    }
    OnRead();
}

// AuthGateD.exe 0x00408030, ClientSocket.cpp.
void CClientSocket::DelayedCloseSocket(int delayMilliseconds) {
    AddRef();
    if (!CAuthGateSvcMgr::g_pAuthGateSvcMgr
             ->QueueThreadPoolWorkItem(
                 CloseClientSocket,
                 this,
                 static_cast<std::uint32_t>(delayMilliseconds))) {
        // L2_LOG_WARN(*CFileLog::g_pDefaultFileLog,
        //     u"[client] Cannot schedule socket closure after %u ms: the timer queue is full. Closing immediately.",
        //     static_cast<unsigned int>(static_cast<std::uint32_t>(delayMilliseconds)))
        L2_LOG_WARN(*CFileLog::g_pDefaultFileLog,
            u"[client] Cannot schedule closure of socket %u, session %d after %u ms: the timer queue is full. Closing immediately.",
            m_hSocket, m_iSessionId, static_cast<unsigned int>(static_cast<std::uint32_t>(delayMilliseconds)));
        CloseSocket();
        ReleaseRef();
    }
}
