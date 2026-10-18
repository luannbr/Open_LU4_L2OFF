#pragma once

#include "AuthGateDTypes.h"
#include "BlowfishWrapper.h"
#include "GameGuard.h"
#include "IOSocket.h"

#include <array>
#include <cstdint>

class CClientPacket;
class CClientSocketContainer;

// Original owner: ClientSocket.h / ClientSocket.cpp. AuthGateD.pdb records
// 0x21b4 bytes in the selected x86 ABI. The two 0x1048 Blowfish matrices are
// held by the live socket exactly as in the target.
class CClientSocket final : public CIOSocket {
public:
    explicit CClientSocket(std::uint32_t socket);
    ~CClientSocket() override;

    static CIOSocket* Allocate(std::uint32_t socket);
    static CIOSocket* AllocateLu4(std::uint32_t socket);

    void OnClose(std::uint32_t socket) override;
    void OnRead() override;
    void OnCreate() override;

    bool CheckSessionId(
        int sessionId,
        const char16_t* methodName);
    bool GetGameGuardQuery(_GG_AUTH_DATA* query);
    bool CheckGameGuardAnswer(_GG_AUTH_DATA* answer);
    bool CheckGameGuardVersion(_GG_AUTH_DATA* answer);
    bool SendProtocolVer();
    void Send(const char* format, ...);
    void CloseSocketByAuthD();
    static void CloseClientSocket(void* parameter);
    void DelayedCloseSocket(int delayMilliseconds);

private:
    friend class CClientPacket;
    friend class CClientSocketContainer;

    void GenerateInitialKey(std::int16_t seed);
    void GenerateSecondKey();

    CCSAuth2 m_csa;
    SocketMode m_mode = SM_READ_LEN;
    std::int32_t m_packetLen = 0;
    std::int32_t m_iRSAKeyIndex = 0;
    bool m_bAuthClose = false;
    std::int32_t m_iSessionId = 0;
    bool m_ggVersionPass = false;
    bool m_ggCSResult = true;
    // Portable: accepted on lu4Port; speaks the LU4 login wire (Lu4Login.h).
    bool m_lu4 = false;
    CBlowfishWrapper mFirstCrypt;
    CBlowfishWrapper mSecondCrypt;
    std::array<unsigned char, 16> mFirstKey{};
    std::array<unsigned char, 16> mSecondKey{};
};
