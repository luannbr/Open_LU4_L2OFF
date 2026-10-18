#pragma once

#include "AccountDB.h"

#include "Shared/src/Runtime/RawAtomic.h"

#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <mutex>

// Original owner: Src/clientsession.h. L2Authd.pdb type 0x197B, target
// size 0x16B8. Access, overloads, handler spelling and field order follow
// field list 0x197A exactly.
class CClientSession {
public:
    explicit CClientSession(unsigned int sessionId);
    ~CClientSession();

    static CClientSession* Allocate(
        unsigned int sessionId);

    void OnCreate();
    void OnRead(const unsigned char* packet);
    void OnClose(unsigned int sessionId);
    void OnTimerCallback();

    void SetAddress(in_addr address);
    void SetUid(int uid) {
        m_uid = uid;
    }
    void SetLastIO(std::uint32_t lastIO) {
        m_lastIO = static_cast<std::int32_t>(lastIO);
    }
    void SetUserMode(UserMode mode) {
        um_mode = mode;
    }
    void SetPreLoginUser(PreLoginUser& user) {
        std::memcpy(&m_preLoginUser, &user, 0x1694);
    }

    void Send(
        const char* sendMessage,
        int messageLength);
    void Send(
        const char* format,
        std::va_list arguments);
    void Send(const char* format, ...);
    void SendFail(
        char command,
        char result,
        LoginUser* user);

    int GetMd5Key() {
        return m_oneTimeKey;
    }
    const char* IP();
    in_addr getaddr() {
        return m_addr;
    }
    unsigned int GetSessionId() {
        return m_sessionId;
    }

    bool LoginPacket(const unsigned char* packet);
    bool SelectServerPacket(const unsigned char* packet);
    bool LogoutPacket(const unsigned char* packet);
    bool ServerListPacket(const unsigned char* packet);
    bool ServerListExtPacket(const unsigned char* packet);
    bool CheckSecurityCardPacket(const unsigned char* packet);
    bool UpdateSessionPacket(const unsigned char* packet);
    bool WebSessionLoginPacket(const unsigned char* packet);
    bool OTPCheckPacket(const unsigned char* packet);
    bool ExternalTokenLoginPacket(const unsigned char* packet);
    bool AuxAuthenticationAckPacket(const unsigned char* packet);
    bool RecvThridPartyLoginPacket(const unsigned char* packet);
    bool RecvIovationPacket(const unsigned char* packet);

    int UpdateOtpFailCount() {
        const unsigned char updated =
            static_cast<unsigned char>(
                m_preLoginUser.otpFailCount + 1U);
        m_preLoginUser.otpFailCount = updated;
        return updated;
    }
    void ResetOtpAnswer() {
        m_preLoginUser.otpAnswer = -1;
    }
    int GetOtpFailCount() {
        return m_preLoginUser.otpFailCount;
    }

    void CloseSessionActively();
    void CloseSessionPassively();

    static std::int32_t GetTryLoginCnt() {
        return g_nTryLoginCnt;
    }
    static void ResetTryLoginCnt() {
        runtime::raw_atomic::ExchangeI32(
            &g_nTryLoginCnt,
            0);
    }

    // Present in PDB field list 0x197A but not compiled or referenced by the
    // selected EXE. It remains declaration-only until a body is evidenced.
    int GetGlobalActUid();

    bool RecvThridPartySteamLoginPacket(
        const unsigned char* packet);
    bool RecvLoginTokenPacket(
        const unsigned char* packet);

    int AddRef();
    void ReleaseRef();

protected:
    static std::int32_t g_nTryLoginCnt;

    std::uint32_t m_sessionId;
    UserMode um_mode;
    std::int32_t m_uid;
    std::int32_t m_lastIO;
    std::int32_t m_packetLen;
    void* m_TimerHandle;
    in_addr m_addr;
    std::int32_t m_oneTimeKey;
    std::int32_t m_refcount;
    PreLoginUser m_preLoginUser;

private:
    // Original behavior bug fix: the donor had no in-flight selection state
    // (SelectServerPacket 0x41E7E0 / AboutToPlay 0x40EE60). See AUTH-SELECT-001 in docs/SHIPPING_BUG_FIXES.md.
    // One selection remains owned by this session until PlayFail or closure.
    // PlayOk keeps it reserved: late clicks must not invalidate its ticket.
    std::mutex m_serverSelectionMutex;
    bool m_serverSelectionReserved = false;
    // Portable: the login came from an LU4 client (Lu4Validator.h).
    bool m_lu4 = false;

    // Called under m_serverSelectionMutex by all outgoing packet overloads.
    void RecordServerSelectionReply(unsigned char opcode) {
        if (opcode == 6) {
            m_serverSelectionReserved = false;
        } else if (opcode == 7) {
            m_serverSelectionReserved = true;
        }
    }
};

#if defined(_WIN32)
void __stdcall ClientSessionTimerCallback(
#else
void ClientSessionTimerCallback(
#endif
    void* parameter,
    std::uint8_t timerOrWaitFired);
