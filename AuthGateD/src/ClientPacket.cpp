#include "ClientPacket.h"

#include "AuthGateConfig.h"
#include "AuthGateSvcMgr.h"
#include "AuthSocket.h"
#include "ClientPacketPool.h"
#include "ClientSocket.h"
#include "FileLog.h"
#include "LogSocket.h"
#include "RSAKeyPool.h"
#include "StateMonMgr.h"
#include "GlobalFunc.h"

#include "Shared/src/Runtime/LittleEndian.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

_GG_AUTH_DATA LoadGameGuardData(const unsigned char* source) noexcept {
    // Client packet bodies begin one byte after the opcode and therefore do
    // not guarantee the four-byte alignment that the x86 target tolerated.
    // Copying keeps the exact 16 wire bytes without invoking alignment
    // undefined behavior on stricter cross-platform hosts.
    _GG_AUTH_DATA result{};
    std::memcpy(&result, source, sizeof(result));
    return result;
}

std::uint32_t ApplyGameGuardResult(bool gameGuardCheckPassed, std::uint32_t value) noexcept {
    if (!config.useNotifyCSResult)
        return value;
    return gameGuardCheckPassed ? value & UINT32_C(0x7fffffff) : value | UINT32_C(0x80000000);
}

std::array<char, 92> FormatLegacyPointerWords(const unsigned char* source) {
    // StringCchPrintfA("%p%p%p%p") in the selected x86 CRT emits four
    // fixed-width, uppercase, prefix-free pointer values. These bytes become
    // the trial-list identity in RecvWebSessionLogin, so host %p cannot be
    // replaced here.
    std::array<char, 92> result{};
    std::snprintf(result.data(), result.size(), "%08X%08X%08X%08X", runtime::LoadLittleEndian32(source), runtime::LoadLittleEndian32(source + 4),
                  runtime::LoadLittleEndian32(source + 8), runtime::LoadLittleEndian32(source + 12));
    return result;
}

} // namespace

// AuthGateD.exe 0x00437110, ClientPacket.cpp. The order was read from the
// shipping image's 19-entry function-pointer table instead of inferred from
// handler names.
CClientPacket::PacketFunction CClientPacket::m_PacketTable[19] = {
    &CClientPacket::RecvLogin,
    &CClientPacket::RecvServerList,
    &CClientPacket::RecvAboutToPlay,
    &CClientPacket::RecvLogout,
    &CClientPacket::RecvLoginMd5,
    &CClientPacket::RecvServerListExt,
    &CClientPacket::RecvSCCheck,
    &CClientPacket::RecvGameGuard,
    &CClientPacket::RecvUpdateSession,
    &CClientPacket::RecvWebSessionLogin,
    &CClientPacket::RecvOTPCheck,
    &CClientPacket::RecvExternalTokenLogin,
    &CClientPacket::RecvAuxAuthentication,
    &CClientPacket::RecvThridPartyLogin,
    &CClientPacket::RecvAgreementCheck,
    &CClientPacket::RecvAgreement,
    &CClientPacket::RecvIovationAck,
    &CClientPacket::RecvThridPartySteamLogin,
    &CClientPacket::RecvTokenLogin,
};

// AuthGateD.exe 0x00406110, ClientPacket.cpp.
CClientPacket::CClientPacket() = default;

// AuthGateD.exe 0x00406130, ClientPacket.cpp.
CClientPacket::~CClientPacket() = default;

// AuthGateD.exe 0x00406140, ClientPacket.cpp.
CClientPacket* CClientPacket::Alloc() { return CClientPacketPool::g_ClientPacketPool->Alloc(); }

// AuthGateD.exe 0x00406150, ClientPacket.cpp.
void CClientPacket::OnIOCallback([[maybe_unused]] int success,
                                 std::uint32_t transferred,
                                 [[maybe_unused]] _OVERLAPPED* overlapped) {
    if (m_pFunc(m_pSocket, reinterpret_cast<const unsigned char*>(m_pBuf->m_buffer + transferred + 1U), m_packetLen - 1)) {
        m_pSocket->CloseSocket();
    }
    m_pSocket->ReleaseRef();
    if (m_pBuf->m_ref.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        m_pBuf->Free();
    }
    CClientPacketPool::g_ClientPacketPool->Free(this);
    CStateMonMgr::g_pStateMonMgr->clientPacketPendingCount.fetch_sub(1, std::memory_order_relaxed);
}

// AuthGateD.exe 0x004061C0, ClientPacket.cpp.
bool CClientPacket::ByPassPacket(CClientSocket* socket, char opcode, const unsigned char* packet, int packetLength) {
    CIOBuffer* const buffer = CIOBuffer::Alloc();
    const int bodyLength = Assemble(buffer->m_buffer + 7, static_cast<int>(CIOBuffer::kCapacity - 7U), "cb",
                                    static_cast<int>(opcode), static_cast<std::uint32_t>(packetLength), packet);
    buffer->m_size = static_cast<std::uint32_t>(bodyLength + 7);
    // Noise: packet tracing stays on the recovered no-op LogDbgT route.
    // The diagnostic text below is dormant. Do not migrate it to the active
    // L2_LOG route merely to add a source basename.
    // Original: CFileLog::g_pDefaultFileLog->LogDbgT(LOG_DBG_PACKET, u"command(%d)", static_cast<int>(opcode))
    CFileLog::g_pDefaultFileLog->LogDbgT(LOG_DBG_PACKET,
        u"Preparing to forward client opcode %u to AuthD for session %d (payload %d bytes).",
        static_cast<unsigned int>(static_cast<unsigned char>(opcode)), socket->m_iSessionId, packetLength);
    return !CAuthSocket::g_pAuthSocket->SendCltPacket(socket->m_iSessionId, buffer);
}

// AuthGateD.exe 0x004012E0, ClientPacket.cpp (folded one-byte true return).
bool CClientPacket::RecvLoginMd5([[maybe_unused]] CClientSocket* socket,
                                 [[maybe_unused]] const unsigned char* packet,
                                 [[maybe_unused]] int packetLength) {
    return true;
}

// AuthGateD.exe 0x00406230, ClientPacket.cpp.
bool CClientPacket::RecvLogin(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    if (config.loginType != 2 && config.loginType != 0) {
        // CFileLog::g_pDefaultFileLog->LogT(LOG_WARN, u"Disallowed gamesession login (current logintype: %d)",
        //                                   config.loginType)
        L2_LOG_WARN(*CFileLog::g_pDefaultFileLog,
            u"[login] Rejecting game-session login for session %d: configured login type %d does not allow this method (requires 0 or 2). "
            u"Requesting rejection 0x2d and delayed connection closure.",
            socket->m_iSessionId, config.loginType);
        socket->Send("cc", 1, 0x2d);
        socket->DelayedCloseSocket(config.appLaunchBanDelay * 1000);
        return false;
    }

    if (CAuthGateSvcMgr::g_pAuthGateSvcMgr->IsBlockedIP(socket->m_addr)) {
        socket->Send("cc", 1, 0x16);
        return true;
    }

    if (socket->m_lu4) {
        // The LU4 credentials block is encrypted with a key this server does
        // not have. AuthD takes account and password from lu4authhook.dll
        // (its LU4 validator) by client address; the account field carries
        // the marker "\x7fLU4" instead (Lu4Validator.h in AuthD).
        std::array<unsigned char, 0x22 + 4 + 3> empty{};
        empty[0] = 0x7f;
        empty[1] = 'L';
        empty[2] = 'U';
        empty[3] = '4';
        CIOBuffer* const buffer = CIOBuffer::Alloc();
        const int bodyLength = Assemble(buffer->m_buffer + 7, static_cast<int>(CIOBuffer::kCapacity - 7U), "cb", 0,
                                        static_cast<std::uint32_t>(empty.size()), empty.data());
        buffer->m_size = static_cast<std::uint32_t>(bodyLength + 7);
        return !CAuthSocket::g_pAuthSocket->SendCltPacket(socket->m_iSessionId, buffer);
    }

    const std::size_t encryptedBytes = config.useGCSideExtendAccount ? 0x100U : 0x80U;
    const int decryptedBytes = config.useGCSideExtendAccount ? 0x64 : 0x22;
    std::array<unsigned char, 0x100> credentials{};
    std::memcpy(credentials.data(), packet, encryptedBytes);

    if (!config.useGCSideExtendAccount) {
        if (!CRSAKeyPool::m_pInstance->Decrypt(socket->m_iRSAKeyIndex, credentials.data(), 0x22)) {
            return true;
        }
    } else {
        if (!CRSAKeyPool::m_pInstance->Decrypt(socket->m_iRSAKeyIndex, credentials.data(), 0x32)) {
            return true;
        }
        if (!CRSAKeyPool::m_pInstance->Decrypt(socket->m_iRSAKeyIndex, credentials.data() + 0x80, 0x32)) {
            return true;
        }
        std::memmove(credentials.data() + 0x32, credentials.data() + 0x80, 0x32);
    }

    const unsigned char* cursor = packet + encryptedBytes;
    const std::int32_t sessionId = static_cast<std::int32_t>(runtime::LoadLittleEndian32(cursor));
    if (!socket->CheckSessionId(sessionId, u"RecvLogin")) {
        return true;
    }
    cursor += 4;
    _GG_AUTH_DATA gameGuardAnswer = LoadGameGuardData(cursor);
    if (!socket->CheckGameGuardAnswer(&gameGuardAnswer)) {
        const std::uint32_t delay =
            CAuthGateSvcMgr::g_pAuthGateSvcMgr->AddBadUser(reinterpret_cast<const char*>(credentials.data()));
        // CLogSocket::s_pLogSocket->SendLog(reinterpret_cast<const char*>(credentials.data()))
        CLogSocket::s_pLogSocket->SendLog("[login credentials redacted]");
        socket->DelayedCloseSocket(static_cast<int>(delay * 1000U));
        return false;
    }
    cursor += sizeof(_GG_AUTH_DATA);

    std::uint32_t authFlags = runtime::LoadLittleEndian32(cursor);
    authFlags = ApplyGameGuardResult(socket->m_ggCSResult, authFlags);
    cursor += 4;
    if (CAuthGateSvcMgr::g_pAuthGateSvcMgr->IsBlockedTrial(reinterpret_cast<const char*>(credentials.data()))) {
        return true;
    }

    const int trailingLength =
        packetLength - static_cast<int>(encryptedBytes) - 4 - static_cast<int>(sizeof(_GG_AUTH_DATA)) - 4;
    CIOBuffer* const buffer = CIOBuffer::Alloc();
    const int bodyLength = Assemble(buffer->m_buffer + 7, static_cast<int>(CIOBuffer::kCapacity - 7U), "cbdb", 0,
                                    static_cast<std::uint32_t>(decryptedBytes), credentials.data(), authFlags,
                                    static_cast<std::uint32_t>(trailingLength), cursor);
    buffer->m_size = static_cast<std::uint32_t>(bodyLength + 7);
    // CFileLog::g_pDefaultFileLog->LogDbgT(LOG_DBG_PACKET, u"AQ_LOGIN account(%S)",
    //                                      reinterpret_cast<const char*>(credentials.data()))
    // Do not interpret decrypted credential bytes as a NUL-terminated diagnostic string.
    CFileLog::g_pDefaultFileLog->LogDbgT(LOG_DBG_PACKET,
        u"[login] Prepared game-session login for forwarding to AuthD (session %d).",
        socket->m_iSessionId);
    return !CAuthSocket::g_pAuthSocket->SendCltPacket(socket->m_iSessionId, buffer);
}

// AuthGateD.exe 0x00406500, ClientPacket.cpp.
bool CClientPacket::RecvServerList(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    return ByPassPacket(socket, 1, packet, packetLength);
}

// AuthGateD.exe 0x00406520, ClientPacket.cpp.
bool CClientPacket::RecvAboutToPlay(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    return ByPassPacket(socket, 2, packet, packetLength);
}

// AuthGateD.exe 0x00406540, ClientPacket.cpp.
bool CClientPacket::RecvLogout(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    return ByPassPacket(socket, 3, packet, packetLength);
}

// AuthGateD.exe 0x00406560, ClientPacket.cpp.
bool CClientPacket::RecvServerListExt(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    return ByPassPacket(socket, 5, packet, packetLength);
}

// AuthGateD.exe 0x00406580, ClientPacket.cpp.
bool CClientPacket::RecvSCCheck(CClientSocket* socket, const unsigned char* packet, [[maybe_unused]] int packetLength) {
    const std::uint32_t userId = runtime::LoadLittleEndian32(packet);
    std::array<unsigned char, 0x80> encryptedCard{};
    std::memcpy(encryptedCard.data(), packet + 4, encryptedCard.size());
    if (!CRSAKeyPool::m_pInstance->Decrypt(socket->m_iRSAKeyIndex, encryptedCard.data(), 4)) {
        return true;
    }

    CIOBuffer* const buffer = CIOBuffer::Alloc();
    const int bodyLength = Assemble(buffer->m_buffer + 7, static_cast<int>(CIOBuffer::kCapacity - 7U), "cdb", 6, userId,
                                    std::uint32_t{4}, encryptedCard.data());
    buffer->m_size = static_cast<std::uint32_t>(bodyLength + 7);
    // CFileLog::g_pDefaultFileLog->LogDbgT(LOG_DBG_PACKET, u"AQ_SCCHECK uid(%d), card(%d)", userId,
    //                                      runtime::LoadLittleEndian32(encryptedCard.data()))
    // Encrypted card bytes are not needed to correlate this request.
    CFileLog::g_pDefaultFileLog->LogDbgT(LOG_DBG_PACKET,
        u"[login] Prepared security-card check for forwarding to AuthD (session %d, user %d).",
        socket->m_iSessionId, userId);
    return !CAuthSocket::g_pAuthSocket->SendCltPacket(socket->m_iSessionId, buffer);
}

// AuthGateD.exe 0x00406660, ClientPacket.cpp.
bool CClientPacket::RecvGameGuard(CClientSocket* socket, const unsigned char* packet, [[maybe_unused]] int packetLength) {
    const std::int32_t sessionId = static_cast<std::int32_t>(runtime::LoadLittleEndian32(packet));
    if (!socket->CheckSessionId(sessionId, u"RecvGameGuard")) {
        return true;
    }
    _GG_AUTH_DATA gameGuardAnswer = LoadGameGuardData(packet + 4);
    if (!socket->CheckGameGuardVersion(&gameGuardAnswer)) {
        socket->DelayedCloseSocket(5000);
        return false;
    }

    _GG_AUTH_DATA query{};
    if (!socket->GetGameGuardQuery(&query)) {
        return true;
    }
    socket->Send("cdb", 0x0b, static_cast<std::uint32_t>(sessionId), static_cast<std::uint32_t>(sizeof(query)), &query);
    // Original: CFileLog::g_pDefaultFileLog->LogDbgT(LOG_DBG_PACKET, u"AC_GAMEGUARD sessionid(%d)", sessionId)
    CFileLog::g_pDefaultFileLog->LogDbgT(LOG_DBG_PACKET,
        u"Attempted to send the next GameGuard query to client session %d.", sessionId);
    return false;
}

// AuthGateD.exe 0x004066F0, ClientPacket.cpp.
bool CClientPacket::RecvUpdateSession(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    return ByPassPacket(socket, 8, packet, packetLength);
}

// AuthGateD.exe 0x00406710, ClientPacket.cpp.
bool CClientPacket::RecvOTPCheck(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    return ByPassPacket(socket, 0x0a, packet, packetLength);
}

// AuthGateD.exe 0x00406730, ClientPacket.cpp.
bool CClientPacket::RecvExternalTokenLogin(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    if (CAuthGateSvcMgr::g_pAuthGateSvcMgr->IsBlockedIP(socket->m_addr)) {
        socket->Send("cc", 1, 0x16);
        return true;
    }

    const std::uint32_t externalType = runtime::LoadLittleEndian32(packet);
    std::array<unsigned char, 0x80> token{};
    std::memcpy(token.data(), packet + 4, token.size());
    if (!CRSAKeyPool::m_pInstance->Decrypt(socket->m_iRSAKeyIndex, token.data(), 0x40)) {
        return true;
    }

    const unsigned char* cursor = packet + 0x84;
    const std::int32_t sessionId = static_cast<std::int32_t>(runtime::LoadLittleEndian32(cursor));
    if (!socket->CheckSessionId(sessionId, u"RecvExternalTokenLogin")) {
        return true;
    }
    cursor += 4;
    _GG_AUTH_DATA gameGuardAnswer = LoadGameGuardData(cursor);
    if (!socket->CheckGameGuardAnswer(&gameGuardAnswer)) {
        const std::uint32_t delay =
            CAuthGateSvcMgr::g_pAuthGateSvcMgr->AddBadUser(reinterpret_cast<const char*>(token.data()));
        // CLogSocket::s_pLogSocket->SendLog(reinterpret_cast<const char*>(token.data()))
        CLogSocket::s_pLogSocket->SendLog("[external login token redacted]");
        socket->DelayedCloseSocket(static_cast<int>(delay * 1000U));
        return false;
    }
    cursor += sizeof(_GG_AUTH_DATA);

    std::uint32_t authFlags = runtime::LoadLittleEndian32(cursor);
    authFlags = ApplyGameGuardResult(socket->m_ggCSResult, authFlags);
    cursor += 4;
    if (CAuthGateSvcMgr::g_pAuthGateSvcMgr->IsBlockedTrial(reinterpret_cast<const char*>(token.data()))) {
        return true;
    }

    const int trailingLength = packetLength - 0x9c;
    CIOBuffer* const buffer = CIOBuffer::Alloc();
    const int bodyLength =
        Assemble(buffer->m_buffer + 7, static_cast<int>(CIOBuffer::kCapacity - 7U), "cdbdb", 0x0b, externalType,
                 std::uint32_t{0x40}, token.data(), authFlags, static_cast<std::uint32_t>(trailingLength), cursor);
    buffer->m_size = static_cast<std::uint32_t>(bodyLength + 7);
    // The target evaluates 64 byte loads for a no-op LogDbgT call here. The
    // folded logging body at 0x004179F0 has no observable production effect.
    return !CAuthSocket::g_pAuthSocket->SendCltPacket(socket->m_iSessionId, buffer);
}

// AuthGateD.exe 0x00406B20, ClientPacket.cpp.
bool CClientPacket::RecvAuxAuthentication(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    return ByPassPacket(socket, 0x0c, packet, packetLength);
}

// AuthGateD.exe 0x00406B40, ClientPacket.cpp.
bool CClientPacket::RecvThridPartyLogin(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    if (CAuthGateSvcMgr::g_pAuthGateSvcMgr->IsBlockedIP(socket->m_addr)) {
        socket->Send("cc", 1, 0x16);
        return true;
    }

    const std::uint32_t provider = runtime::LoadLittleEndian32(packet);
    std::array<unsigned char, 0x400> providerData{};
    std::memcpy(providerData.data(), packet + 4, providerData.size());
    const auto sessionId = static_cast<std::int32_t>(runtime::LoadLittleEndian32(packet + 0x404));
    if (!socket->CheckSessionId(sessionId, u"RecvThirdPartyLogin")) {
        return true;
    }

    CIOBuffer* const buffer = CIOBuffer::Alloc();
    const int bodyLength =
        Assemble(buffer->m_buffer + 7, static_cast<int>(CIOBuffer::kCapacity - 7U), "cdbdb", 0x0d, provider,
                 std::uint32_t{0x400}, providerData.data(), static_cast<std::uint32_t>(sessionId),
                 static_cast<std::uint32_t>(packetLength - 0x418), packet + 0x418);
    buffer->m_size = static_cast<std::uint32_t>(bodyLength + 7);
    return !CAuthSocket::g_pAuthSocket->SendCltPacket(socket->m_iSessionId, buffer);
}

// AuthGateD.exe 0x00406C50, ClientPacket.cpp.
bool CClientPacket::RecvAgreementCheck(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    return ByPassPacket(socket, 0x0e, packet, packetLength);
}

// AuthGateD.exe 0x00406C70, ClientPacket.cpp.
bool CClientPacket::RecvAgreement(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    return ByPassPacket(socket, 0x0f, packet, packetLength);
}

// AuthGateD.exe 0x00406C90, ClientPacket.cpp.
bool CClientPacket::RecvIovationAck(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    return ByPassPacket(socket, 0x10, packet, packetLength);
}

// AuthGateD.exe 0x00406CB0, ClientPacket.cpp.
bool CClientPacket::RecvThridPartySteamLogin(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    if (CAuthGateSvcMgr::g_pAuthGateSvcMgr->IsBlockedIP(socket->m_addr)) {
        socket->Send("cc", 1, 0x16);
        return true;
    }

    const std::uint32_t provider = runtime::LoadLittleEndian32(packet);
    std::array<unsigned char, 0x400> providerData{};
    std::memcpy(providerData.data(), packet + 4, providerData.size());
    const auto sessionId = static_cast<std::int32_t>(runtime::LoadLittleEndian32(packet + 0x404));
    if (!socket->CheckSessionId(sessionId, u"RecvThirdPartySteamLogin")) {
        return true;
    }

    CIOBuffer* const buffer = CIOBuffer::Alloc();
    const int bodyLength =
        Assemble(buffer->m_buffer + 7, static_cast<int>(CIOBuffer::kCapacity - 7U), "cdbdb", 0x11, provider,
                 std::uint32_t{0x400}, providerData.data(), static_cast<std::uint32_t>(sessionId),
                 static_cast<std::uint32_t>(packetLength - 0x418), packet + 0x418);
    buffer->m_size = static_cast<std::uint32_t>(bodyLength + 7);
    return !CAuthSocket::g_pAuthSocket->SendCltPacket(socket->m_iSessionId, buffer);
}

// AuthGateD.exe 0x00406DC0, ClientPacket.cpp.
bool CClientPacket::RecvTokenLogin(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    if (config.loginType != 2 && config.loginType != 4) {
        // CFileLog::g_pDefaultFileLog->LogT(LOG_WARN, u"Disallowed token login (current logintype: %d)", config.loginType)
        L2_LOG_WARN(*CFileLog::g_pDefaultFileLog,
            u"[login] Rejecting token login for session %d: configured login type %d does not allow this method (requires 2 or 4). "
            u"Returning to the dispatcher for connection closure without a rejection packet.",
            socket->m_iSessionId, config.loginType);
        return true;
    }
    if (CAuthGateSvcMgr::g_pAuthGateSvcMgr->IsBlockedIP(socket->m_addr)) {
        socket->Send("cc", 1, 0x16);
        return true;
    }

    const auto sessionId = static_cast<std::int32_t>(runtime::LoadLittleEndian32(packet));
    if (!socket->CheckSessionId(sessionId, u"RecvTokenLogin")) {
        return true;
    }

    const unsigned char* const gameGuardAnswerBytes = packet + 4;
    const char* const strings = reinterpret_cast<const char*>(packet + 0x14);
    std::array<char, 0x21> account{};
    std::array<char, 0x800> token{};
    const int accountBytes = LNStrNCpy(account.data(), strings, static_cast<int>(account.size()));
    const int tokenBytes = LNStrNCpy(token.data(), strings + accountBytes, static_cast<int>(token.size()));

    _GG_AUTH_DATA gameGuardAnswer = LoadGameGuardData(gameGuardAnswerBytes);
    if (!socket->CheckGameGuardAnswer(&gameGuardAnswer)) {
        const std::uint32_t delay = CAuthGateSvcMgr::g_pAuthGateSvcMgr->AddBadUser(token.data());
        // CLogSocket::s_pLogSocket->SendLog(token.data())
        CLogSocket::s_pLogSocket->SendLog("[login token redacted]");
        socket->DelayedCloseSocket(static_cast<int>(delay * 1000U));
        return false;
    }

    const int trailingLength = packetLength - 0x14 - accountBytes - tokenBytes;
    const unsigned char* const trailing = packet + 0x14 + accountBytes + tokenBytes;
    CIOBuffer* const buffer = CIOBuffer::Alloc();
    const int bodyLength = Assemble(buffer->m_buffer + 7, static_cast<int>(CIOBuffer::kCapacity - 7U), "cdssb", 0x12,
                                    static_cast<std::uint32_t>(sessionId), account.data(), token.data(),
                                    static_cast<std::uint32_t>(trailingLength), trailing);
    buffer->m_size = static_cast<std::uint32_t>(bodyLength + 7);
    return !CAuthSocket::g_pAuthSocket->SendCltPacket(socket->m_iSessionId, buffer);
}

// AuthGateD.exe 0x00406FD0, ClientPacket.cpp.
void CClientPacket::Init(CClientSocket* socket, CIOBuffer* buffer, char opcode, int packetLength) {
    m_pSocket = socket;
    m_pBuf = buffer;
    const int index = static_cast<signed char>(opcode);
    m_pFunc = index >= 0 && index < 19 ? m_PacketTable[index] : nullptr;
    m_packetLen = packetLength;
}

// AuthGateD.exe 0x00407010, ClientPacket.cpp.
bool CClientPacket::RecvWebSessionLogin(CClientSocket* socket, const unsigned char* packet, int packetLength) {
    if (config.loginType != 2 && config.loginType != 1) {
        // CFileLog::g_pDefaultFileLog->LogT(LOG_WARN, u"Disallowed websession login (current logintype: %d)",
        //                                   config.loginType)
        L2_LOG_WARN(*CFileLog::g_pDefaultFileLog,
            u"[login] Rejecting web-session login for session %d: configured login type %d does not allow this method (requires 1 or 2). "
            u"Returning to the dispatcher for connection closure without a rejection packet.",
            socket->m_iSessionId, config.loginType);
        return true;
    }
    if (CAuthGateSvcMgr::g_pAuthGateSvcMgr->IsBlockedIP(socket->m_addr)) {
        socket->Send("cc", 1, 0x16);
        return true;
    }

    std::array<unsigned char, 0x80> webSession{};
    std::memcpy(webSession.data(), packet, webSession.size());
    if (!CRSAKeyPool::m_pInstance->Decrypt(socket->m_iRSAKeyIndex, webSession.data(), 0x20)) {
        return true;
    }

    const unsigned char* cursor = packet + 0x80;
    const auto sessionId = static_cast<std::int32_t>(runtime::LoadLittleEndian32(cursor));
    if (!socket->CheckSessionId(sessionId, u"RecvWebSessionLogin")) {
        return true;
    }
    cursor += 4;
    _GG_AUTH_DATA gameGuardAnswer = LoadGameGuardData(cursor);
    if (!socket->CheckGameGuardAnswer(&gameGuardAnswer)) {
        const std::uint32_t delay =
            CAuthGateSvcMgr::g_pAuthGateSvcMgr->AddBadUser(reinterpret_cast<const char*>(webSession.data()));
        // CLogSocket::s_pLogSocket->SendLog(reinterpret_cast<const char*>(webSession.data()))
        CLogSocket::s_pLogSocket->SendLog("[web login session redacted]");
        socket->DelayedCloseSocket(static_cast<int>(delay * 1000U));
        return false;
    }
    cursor += sizeof(_GG_AUTH_DATA);

    std::uint32_t authFlags = runtime::LoadLittleEndian32(cursor);
    authFlags = ApplyGameGuardResult(socket->m_ggCSResult, authFlags);
    cursor += 4;
    const std::array<char, 92> trialIdentity = FormatLegacyPointerWords(webSession.data());
    if (CAuthGateSvcMgr::g_pAuthGateSvcMgr->IsBlockedTrial(trialIdentity.data())) {
        return true;
    }

    CIOBuffer* const buffer = CIOBuffer::Alloc();
    const int bodyLength =
        Assemble(buffer->m_buffer + 7, static_cast<int>(CIOBuffer::kCapacity - 7U), "cbdb", 9, std::uint32_t{0x20},
                 webSession.data(), authFlags, static_cast<std::uint32_t>(packetLength - 0x98), cursor);
    buffer->m_size = static_cast<std::uint32_t>(bodyLength + 7);
    // CFileLog::g_pDefaultFileLog->LogDbgT(LOG_DBG_PACKET, u"AQ_WEBSESSION_LOGIN wsidBuffer(%S)",
    //                                      reinterpret_cast<const char*>(webSession.data()))
    // Keep web-session credentials out of diagnostics. This buffer need not be NUL-terminated.
    CFileLog::g_pDefaultFileLog->LogDbgT(LOG_DBG_PACKET,
        u"[login] Prepared web-session login for forwarding to AuthD (session %d).",
        socket->m_iSessionId);
    return !CAuthSocket::g_pAuthSocket->SendCltPacket(socket->m_iSessionId, buffer);
}
