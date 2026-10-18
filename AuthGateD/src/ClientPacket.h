#pragma once

#include "IOBuffer.h"
#include "IOObject.h"
#include "Shared/src/Runtime/WindowsTypes.h"

#include <cstdint>

class CClientSocket;

// AuthGateD.pdb type length 0x1c in the selected x86 ABI.
class CClientPacket : public CIOObject {
public:
    using PacketFunction = bool (*)(CClientSocket*, const unsigned char*, int);

    CClientPacket();
    ~CClientPacket() override;

    static CClientPacket* Alloc();
    void Init(CClientSocket* socket, CIOBuffer* buffer, char opcode, int packetLength);
    virtual void OnIOCallback(int success, std::uint32_t transferred, _OVERLAPPED* overlapped);

    static bool ByPassPacket(CClientSocket* socket, char opcode, const unsigned char* packet, int packetLength);
    static bool RecvLoginMd5(CClientSocket*, const unsigned char*, int);
    static bool RecvLogin(CClientSocket*, const unsigned char*, int);
    static bool RecvServerList(CClientSocket*, const unsigned char*, int);
    static bool RecvAboutToPlay(CClientSocket*, const unsigned char*, int);
    static bool RecvLogout(CClientSocket*, const unsigned char*, int);
    static bool RecvServerListExt(CClientSocket*, const unsigned char*, int);
    static bool RecvSCCheck(CClientSocket*, const unsigned char*, int);
    static bool RecvGameGuard(CClientSocket*, const unsigned char*, int);
    static bool RecvUpdateSession(CClientSocket*, const unsigned char*, int);
    static bool RecvOTPCheck(CClientSocket*, const unsigned char*, int);
    static bool RecvExternalTokenLogin(CClientSocket*, const unsigned char*, int);
    static bool RecvAuxAuthentication(CClientSocket*, const unsigned char*, int);
    static bool RecvThridPartyLogin(CClientSocket*, const unsigned char*, int);
    static bool RecvAgreementCheck(CClientSocket*, const unsigned char*, int);
    static bool RecvAgreement(CClientSocket*, const unsigned char*, int);
    static bool RecvIovationAck(CClientSocket*, const unsigned char*, int);
    static bool RecvThridPartySteamLogin(CClientSocket*, const unsigned char*, int);
    static bool RecvTokenLogin(CClientSocket*, const unsigned char*, int);
    static bool RecvWebSessionLogin(CClientSocket*, const unsigned char*, int);

    CClientSocket* m_pSocket = nullptr;
    CIOBuffer* m_pBuf = nullptr;
    PacketFunction m_pFunc = nullptr;
    std::int32_t m_packetLen = 0;

    static PacketFunction m_PacketTable[19];
};
