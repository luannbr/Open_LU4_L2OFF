#pragma once

#include "OlympiadStuff.h"
#include "Server.h"

#include "Shared/src/OlympiadDefine.h"
#include "Shared/src/Socket.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/ThreadSafeObjectMap.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

namespace network {
struct IPacket;
}

class WOlympiadSocketBase;
class WOlympiadSocketHost;
class WOlympiadSocketPeer;

using WOlympiadPacketCallback = bool (*)(WOlympiadSocketBase*, const std::uint8_t*);
using OSCPacketCallback = bool (*)(WOlympiadSocketHost*, const std::uint8_t*);
using OCSPacketCallback = bool (*)(WOlympiadSocketPeer*, const std::uint8_t*);

using OSCCallbackMap = FixedSizeCallbackMap<OSC_MAX, OSCPacketCallback>;
using OCSCallbackMap = FixedSizeCallbackMap<OCS_MAX, OCSPacketCallback>;

extern OSCCallbackMap g_OSCPacketHandlerTable;
extern OCSCallbackMap g_OCSPacketHandlerTable;

struct IWOlympiadNetworkHostCallback {
    virtual void OnPeerDisconnected(int worldId) = 0;
};

struct IWOlympiadNetworkPeerCallback {
    virtual void OnCloseHost() = 0;
};

class WOlympiadSocketBase : public CSocket {
public:
    explicit WOlympiadSocketBase(std::uint64_t socket);
    ~WOlympiadSocketBase() override;

    L2_DECLARE_OBJECT_TYPE(WOlympiadSocketBase)

    bool CheckPendingWrite() override;
    void OnClose() override;
    void OnCreate() override;
    void OnRead() override;

    void SendPacket(std::uint8_t protocol, network::IPacket& packet);

    std::uint8_t m_cMaxProtocolNo; // +0x108
    std::uint8_t m_alignment0109To010c[3];
    int m_idWorld; // +0x10c
};

class WOlympiadSocketHost final : public WOlympiadSocketBase {
public:
    explicit WOlympiadSocketHost(std::uint64_t socket);
    ~WOlympiadSocketHost() override = default;

    L2_DECLARE_OBJECT_TYPE(WOlympiadSocketHost)

    void OnClose() override;
};

class WOlympiadSocketPeer final : public WOlympiadSocketBase {
public:
    explicit WOlympiadSocketPeer(std::uint64_t socket);
    ~WOlympiadSocketPeer() override = default;

    L2_DECLARE_OBJECT_TYPE(WOlympiadSocketPeer)

    void OnClose() override;
    void OnUserSocketClosed();
};

class WOlympiadNetworkHost : public Server, public Singleton<WOlympiadNetworkHost> {
public:
    WOlympiadNetworkHost();
    ~WOlympiadNetworkHost() override;

    L2_DECLARE_OBJECT_TYPE(WOlympiadNetworkHost)

    bool Init(int port, IWOlympiadNetworkHostCallback* callback);
    CSPointer<WOlympiadSocketPeer> FindSocket(int worldId);
    void BroadcastPacket(OSC_PROTOCOL protocol, network::IPacket& packet);
    bool SendPacket(int worldId, OSC_PROTOCOL protocol, network::IPacket& packet);
    bool RegisterPeer(CSPointer<WOlympiadSocketPeer> socket);
    void UnregisterPeer(int worldId);
    bool OnClosePeer(int worldId);

private:
    CYieldLock m_lock; // +0xa8
    std::map<int, CSPointer<WOlympiadSocketPeer>> m_Peers; // +0xb8
    IWOlympiadNetworkHostCallback* m_pCallback; // +0xd8
};

class WOlympiadNetworkPeer : public Singleton<WOlympiadNetworkPeer> {
public:
    struct InnerHost {
        WOlympiadServiceConfig::Server m_Server;
        bool m_bConnected;
        bool m_bSendable;
        WOlympiadSocketHost* m_pSocket;
        std::uint32_t m_nLogTick;

        InnerHost();
        bool Connect();
        void Reconnect();
        bool SendPacket(OCS_PROTOCOL protocol, network::IPacket& packet);
    };

    WOlympiadNetworkPeer();
    ~WOlympiadNetworkPeer() override;

    bool Init(IWOlympiadNetworkPeerCallback* callback);
    bool IsAvailable() const;
    bool SendPacket(OCS_PROTOCOL protocol, network::IPacket& packet);
    void Reconnect();
    void OnCloseHost();

private:
    static void ConnectThread(WOlympiadNetworkPeer* self);

    InnerHost* m_pHost; // +0x08
    std::uint32_t m_nThreadId; // +0x10
    [[maybe_unused]] void* m_hThread; // +0x18
    CYieldLock m_lock; // +0x20
    IWOlympiadNetworkPeerCallback* m_pCallback; // +0x30
};
