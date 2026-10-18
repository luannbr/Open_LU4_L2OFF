#pragma once

#include "Platform/CriticalSection.h"
#include "Platform/WindowsTypes.h"
#include "IOSocket.h"
#include "ServerList.h"

#include <cstdint>
#include <map>

enum SocketMode : std::int32_t {
    SM_READ_LEN = 0,
    SM_READ_BODY = 1,
    SM_CLOSE = 2,
};

enum IntSocketMode : std::int32_t {
    ISM_READ_LEN = 0,
    ISM_READ_BODY = 1,
    ISM_CLOSE = 2,
};

enum MiSocketMode : std::int32_t {
    MSM_READ_LEN = 0,
    MSM_READ_BODY = 1,
    MSM_CLOSE = 2,
};

class CSocketServer : public CIOSocket {
public:
    using PacketFunction = bool (*)(
        CSocketServer*,
        const unsigned char*);

    explicit CSocketServer(std::uint32_t socket);
    ~CSocketServer() override;

    static CSocketServer* Allocate(std::uint32_t socket);
    void OnCreate() override;
    void OnRead() override;
    void OnClose() override;
    in_addr getaddr();
    void SetAddress(in_addr address);
    void Send(const char* format, ...);
    const char* IP();

    std::uint32_t m_socket; // target +0x5c
    char serverid;          // target +0x60
    char GameCode;          // target +0x61
    std::int32_t opFlag;    // target +0x64
    bool bSetActiveServer;  // target +0x68
    CServerKickList mKickList; // target +0x6c

protected:
    std::int32_t packetLen;       // target +0x94
    SocketMode mode;              // target +0x98
    PacketFunction* packetTable;  // target +0x9c
    in_addr addr;                 // target +0xa0
    char* host;                   // target +0xa4
};

class CSocketInt : public CIOSocket {
public:
    using PacketFunction = void (*)(
        CSocketInt*,
        const char*);

    explicit CSocketInt(std::uint32_t socket);
    ~CSocketInt() override;

    static CSocketInt* Allocate(std::uint32_t socket);
    void OnCreate() override;
    void OnRead() override;
    void OnClose() override;
    void SetAddress(in_addr address);
    void Send(const char* format, ...);
    const char* IP();
    void Process(char* packet);
    void SendBuffer(const char* buffer, int length);

    std::uint32_t m_socket; // target +0x5c

protected:
    std::int32_t packetLen;      // target +0x60
    IntSocketMode mode;          // target +0x64
    PacketFunction* packetTable; // target +0x68
    in_addr addr;                // target +0x6c
    char* host;                  // target +0x70
    std::int32_t m_nTimeout;     // target +0x74
};

class CSocketMi : public CIOSocket {
public:
    explicit CSocketMi(std::uint32_t socket);
    ~CSocketMi() override;

    static CSocketMi* Allocate(std::uint32_t socket);
    void OnCreate() override;
    void OnRead() override;
    void OnClose() override;
    void SetAddress(in_addr address);
    void Send(const char* format, ...);
    const char* IP();
    void ProcessMonitorPacket(
        CSocketMi* socket,
        const unsigned char* packet);

    std::uint32_t m_socket; // target +0x5c

protected:
    std::int32_t packetLen; // target +0x60
    MiSocketMode mode;      // target +0x64
    in_addr addr;           // target +0x68
    char* host;             // target +0x6c
};

// Original owner: Src/ioserver.h / Src/ioserver.cpp. PDB type 0x7895.
class CIOServer : public CIOObject {
public:
    CIOServer();
    ~CIOServer() override;

    int Create(int port);
    void Close();
    void Stop();
    virtual CIOSocket* CreateSocket(
        std::uint32_t socket,
        sockaddr_in* address) = 0;
    void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped) override;
    void OnEventCallback() override;

    void* m_hAcceptEvent;    // target +0x08
    std::uint32_t m_hSocket; // target +0x0c
};

class CServer : public CIOServer {
public:
    using Allocator = CSocketServer* (*)(std::uint32_t socket);

    CServer();
    ~CServer() override;

    void Run(int port, Allocator allocator, bool restrictServer);
    void Stop();
    bool InShutdown();
    CSocketServer* FindSocket(in_addr address);
    CIOSocket* CreateSocket(
        std::uint32_t socket,
        sockaddr_in* address) override;
    void RemoveSocket(in_addr address);
    bool GetServerStatus(in_addr address);

private:
    std::map<std::uint32_t, CSocketServer*> socketmap; // target +0x10

public:
    void* listenThread;      // target +0x18
    bool shutdown;           // target +0x1c
    std::uint32_t listener;  // target +0x20
    void* completionPort;    // target +0x24
    authd::platform::CriticalSectionStorage32 sockSect; // target +0x28
    Allocator allocator;     // target +0x40
    bool restrict;           // target +0x44
};

class CServerInt : public CIOServer {
public:
    using Allocator = CSocketInt* (*)(std::uint32_t socket);

    CServerInt();
    ~CServerInt() override;

    void Run(int port, Allocator allocator);
    void Stop();
    bool InShutdown();
    CIOSocket* CreateSocket(
        std::uint32_t socket,
        sockaddr_in* address) override;
    void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped) override;

    void* listenThread;      // target +0x10
    bool shutdown;           // target +0x14
    std::uint32_t listener;  // target +0x18
    void* completionPort;    // target +0x1c
    authd::platform::CriticalSectionStorage32 sockSect; // target +0x20
    Allocator allocator;     // target +0x38
    bool restrict;           // target +0x3c
};

class CServerMi : public CIOServer {
public:
    using Allocator = CSocketMi* (*)(std::uint32_t socket);

    CServerMi();
    ~CServerMi() override;

    void Run(int port, Allocator allocator);
    void Stop();
    bool InShutdown();
    CIOSocket* CreateSocket(
        std::uint32_t socket,
        sockaddr_in* address) override;
    void OnIOCallback(
        int bSuccess,
        std::uint32_t dwTransferred,
        _OVERLAPPED* lpOverlapped) override;

    void* listenThread;      // target +0x10
    bool shutdown;           // target +0x14
    std::uint32_t listener;  // target +0x18
    void* completionPort;    // target +0x1c
    authd::platform::CriticalSectionStorage32 sockSect; // target +0x20
    Allocator allocator;     // target +0x38
    bool restrict;           // target +0x3c
};

extern CServer server;
extern CServerInt serverInt;
extern CServerMi serverMi;

// Exact free-function surface compiled by Src/csocketserver.cpp.
int Send2ClientSession(
    unsigned int sessionId,
    const char* format,
    ...);
