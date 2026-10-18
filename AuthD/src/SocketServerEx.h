#pragma once

#include "Platform/WindowsTypes.h"
#include "IOSocket.h"
#include "Lock.h"

#include <cstdint>
#include <map>

class CClientSession;
class CPacketServerEx;
enum SocketMode : std::int32_t;

// Original owner: Src/socketserverex.h / Src/SocketServerEx.cpp.
// L2Authd.pdb type 0x5F25, target size 0xB0. Declaration order and access
// follow field list 0x5F24 exactly. Method bodies belong to SocketServerEx.cpp.
class CSocketServerEx : public CIOSocket {
public:
    CSocketServerEx(
        unsigned int socket,
        unsigned char gateId);
    ~CSocketServerEx() override;

    static CSocketServerEx* Allocate(
        unsigned int socket,
        unsigned char gateId);
    void OnCreate() override;
    void OnRead() override;
    void OnClose() override;
    void OnTimerCallback() override {}

    void SetAddress(in_addr address);
    char* IP();
    in_addr getaddr() {
        return m_addr;
    }
    unsigned char GetGateId() {
        return m_GateId;
    }
    SocketMode GetMode() {
        return m_mode;
    }

    void SendCloseSession(unsigned int sessionId);
    void SendProcotolVersion();
    void CloseSessionPassively(unsigned int sessionId);
    bool RemoveSession(unsigned int sessionId);
    void RemoveSessionAll();
    CClientSession* CreateSession(
        unsigned int sessionId,
        in_addr address);
    CClientSession* FindSession(unsigned int sessionId);

    unsigned int m_socket;
    int m_lastIO;

protected:
    int m_packetLen;
    SocketMode m_mode;
    in_addr m_addr;
    char m_opcode;
    unsigned int m_sessionSock;
    in_addr m_sessionAddr;
    unsigned char m_GateId;
    CLock m_lock;
    std::map<unsigned int, CClientSession*> m_cltSessionMap;
    void* m_hSocketTimer;
    std::int32_t m_iPendingPacket;

private:
    friend class CPacketServerEx;
};

// Original owner: Src/socketserverex.h / Src/SocketServerEx.cpp.
// L2Authd.pdb procedure type 0x78AD.
int SendSocket(in_addr address, const char* format, ...);
