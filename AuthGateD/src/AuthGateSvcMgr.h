#pragma once

#include "AuthSocket.h"
#include "BadUser.h"
#include "BlowfishWrapper.h"
#include "ClientSocketContainer.h"
#include "IOBufferPool.h"
#include "IPList.h"
#include "LogSocket.h"
#include "RSAKeyPool.h"
#include "ThreadPool.h"
#include "TryList.h"

#include <cstdint>

// Original owner: AuthGateSvcMgr.h / AuthGateSvcMgr.cpp. The matching x86
// PDB records a 0x27c8-byte aggregate. Portable STL/OpenSSL/handle carriers
// change host byte offsets, while ownership and construction order remain the
// target order shown below.
class CAuthGateSvcMgr {
public:
    CAuthGateSvcMgr();
    ~CAuthGateSvcMgr();

    bool IsBlockedIP(in_addr address);
    bool IsBlockedTrial(const char* userId);
    std::uint32_t AddBadUser(const char* userId);
    bool Run();
    void Stop();
    void Reconnect();
    void ReconnectLogD();
    bool QueueThreadPoolWorkItem(
        void (*callback)(void*),
        void* parameter,
        std::uint32_t delayMilliseconds);
    static std::uint32_t IOWorkerProc(void* parameter);
    static std::uint32_t WorkerProc(void* parameter);
    bool Init();

    static CAuthGateSvcMgr* g_pAuthGateSvcMgr;

private:
    friend class CAuthSocket;
    friend class CClientSocket;
    friend class CWindowMgr;

    CRSAKeyPool m_RSAKeyPool;
    CBlowfishWrapper m_BlowfishWrapper;
    CIPList m_blockedIPList;
    CIOBufferPool m_bufferPool;
    TryList m_tryList;
    CBadUser m_badUsers;
    ThreadPool m_threadPool;
    CClientSocketContainer m_ClientSocketContainer;
    CLu4ClientListener m_lu4Listener;
    void* m_hIOCP = nullptr;
    void** m_hIOThread = nullptr;
    std::int32_t m_iIOThreadCount = 0;
    void* m_hWorkIOCP = nullptr;
    void** m_hThread = nullptr;
    std::int32_t m_iThreadCount = 0;
    CAuthSocket m_authSocket;
    CLogSocket m_logdSocket;
    bool m_bOn = false;
};

extern CAuthGateSvcMgr g_svcmgr;
