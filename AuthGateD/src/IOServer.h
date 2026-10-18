#pragma once

#include "AuthGateDTypes.h"
#include "IOObject.h"
#include "IOSocket.h"
#include "Lock.h"

#include <cstdint>
#include <unordered_map>

// Original owner: IOServer.h / IOServer.cpp. AuthGateD.pdb records 0x90
// bytes in the selected x86 ABI. std::unordered_map is the portable host
// representation of the target VC9 stdext::hash_map. Ownership and locking
// remain in this original class.
class CIOServer : public CIOObject {
public:
    using SocketAllocator = CIOSocket* (*)(std::uint32_t socket);

    CIOServer();
    ~CIOServer() override;

    CIOServer(const CIOServer&) = delete;
    CIOServer& operator=(const CIOServer&) = delete;

    int SetIOCP(void* completionPort);
    bool WrapAcceptEx();
    bool Run(
        int port,
        SocketAllocator allocator,
        int acceptCallNum,
        int socketLimit);
    void Restart();
    void Stop();

    virtual void OnIOCallback(
        int success,
        std::uint32_t transferred,
        _OVERLAPPED* overlapped);
    virtual CIOSocket* CreateSocket(
        std::uint32_t socket,
        sockaddr_in* address);
    virtual void AddSocket(
        std::uint32_t socket,
        CIOSocket* object);
    virtual void CheckSocketTimeout(
        int minutes,
        SocketTimeoutOpt option);
    virtual bool RemoveSocket(std::uint32_t socket);

protected:
    friend class CAuthSocket;

    std::uint32_t m_hSocket = UINT32_C(0xffffffff);
    void* m_hIOCP = nullptr;
    bool m_bRun = false;
    bool m_bPause = false;
    std::int32_t m_iSocketLimit = 0;
    std::int32_t m_iAcceptCallNum = 0;
    std::unordered_map<std::uint32_t, CIOSocket*> m_sockMap;
    CLock m_lock;
    SocketAllocator m_allocator = nullptr;
};
