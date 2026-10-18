#include "OlympiadNetwork.h"
#include "Shared/src/Runtime/OperatorLog.h"

#include "CConfig.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/NativeSocketTransport.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/platform/Runtime.h"
#include "Shared/src/proto/OlympiadPacket.h"
#include "Shared/src/proto/Stream.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#include <Windows.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

using OlympiadIncomingPacket = CPacketEx<DefaultIOBuffer, WOlympiadSocketBase, WOlympiadPacketCallback>;

constexpr int kMaximumOlympiadPayload = 0x4ffd;
constexpr int kMaximumOlympiadInboundBody = 0x4ffe;
constexpr std::int32_t kMaximumPendingWrite = 0x800000;

// explain the missing peer using the configured role and
// world IDs. This affects diagnostics only, without initializing a connection.
void LogUninitializedOlympiadPeer(const char* operation) {
    const WOlympiadServiceConfig& config = Singleton<WOlympiadServiceConfig>::inst();
    const char* role = "peer";
    const char* reason = "This world is allowed as a peer, but peer initialization has not completed.";
    LogType level = LOG_ERROR;
    if (!config.IsActive()) {
        role = "disabled";
        reason = "No nonzero host WorldID is loaded; World Olympiad is inactive. Check OlympiadData.xml and its loading "
                 "diagnostics.";
        level = LOG_NORMAL;
    } else if (config.IsHost()) {
        role = "host";
        reason = "This world uses the host network; a peer connection is not initialized for the host role.";
        level = LOG_WARN;
    } else if (!config.IsPeer()) {
        role = "not participating";
        reason = "This world has no matching Client WorldID in OlympiadData.xml <Allow>; peer initialization is skipped.";
        level = LOG_WARN;
    }
    L2_LOG(
        Log, level,
        "[WORLD_OLYMPIAD] %s: peer network is not initialized "
        "(local world ID=%d, configured host world ID=%d, role=%s). %s",
        operation, g_nWorldId, config.m_Server.idWorld, role, reason
    );
}

void CloseOlympiadNativeSocket(std::uint64_t socket) noexcept {
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(socket));
#else
    ::close(static_cast<int>(socket));
#endif
}

int CurrentOlympiadLastError() noexcept {
#if defined(_WIN32)
    // Current InnerHost::Connect calls Kernel32 GetLastError instead of
    // WSAGetLastError after the failed WinSock connect.
    return static_cast<int>(::GetLastError());
#else
    return errno;
#endif
}

CSocket* AllocateOlympiadPeerSocket(std::uint64_t socket) { return new WOlympiadSocketPeer(socket); }

} // namespace


WOlympiadSocketBase::WOlympiadSocketBase(std::uint64_t socket) :
    CSocket(socket, 0x400), m_cMaxProtocolNo(0), m_alignment0109To010c{}, m_idWorld(0) {
    L2_GUARD_FUNCTION();
}

WOlympiadSocketBase::~WOlympiadSocketBase() = default;

bool WOlympiadSocketBase::CheckPendingWrite() {
    L2_GUARD_FUNCTION();
    if (m_hSocket.load(std::memory_order_relaxed) == UINT64_C(0xffffffffffffffff)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]write error",
        //     L"OlympiadNetwork.cpp",
        //     0x20)
        L2_LOG_ERROR(Log, u"[WORLD_OLYMPIAD][%s][%d]write error", u"OlympiadNetwork.cpp", 0x20);
        return false;
    }
    const std::int32_t pending = m_nPendingWrite.load(std::memory_order_relaxed);
    if (pending <= kMaximumPendingWrite) {
        return true;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[WORLD_OLYMPIAD][%s][%d]Pending write > %d KB, Close!",
    //     L"OlympiadNetwork.cpp",
    //     0x26,
    //     0x2000)
    L2_LOG_ERROR(Log, u"[WORLD_OLYMPIAD][%s][%d]Pending write > %d KB, Close!", u"OlympiadNetwork.cpp", 0x26, 0x2000);
    runtime::process::ReportCallStack(LOG_ERROR, false);
    return false;
}

void WOlympiadSocketBase::OnClose() {
    L2_GUARD_FUNCTION();
    m_Mode = SocketMode::SM_CLOSE;
}

void WOlympiadSocketBase::OnCreate() {
    L2_GUARD_FUNCTION();
    Read(0);
}

void WOlympiadSocketBase::SendPacket(std::uint8_t protocol, network::IPacket& packet) {
    L2_GUARD_FUNCTION();
    if (m_Mode == SocketMode::SM_CLOSE) {
        return;
    }

    DefaultIOBuffer* const buffer = DefaultIOBuffer::Alloc();
    network::Stream stream(buffer->m_Buffer.data() + 3, kMaximumOlympiadPayload);
    if (!packet.Encode(&stream)) {
        buffer->Release();
        return;
    }

    const std::uint16_t frameSize = static_cast<std::uint16_t>(3 + (stream.GetCursor() - stream.GetBuffer()));
    buffer->m_Buffer[0] = static_cast<std::uint8_t>(frameSize);
    buffer->m_Buffer[1] = static_cast<std::uint8_t>(frameSize >> 8U);
    buffer->m_Buffer[2] = protocol;
    buffer->m_dwSize = frameSize;

    L2_ADD_REF(this, ObjectRefType::IoSocketWrite);
    AsyncSend(buffer);
}

void WOlympiadSocketBase::OnRead() {
    L2_GUARD_FUNCTION();
    std::uint32_t packetOffset = 0;
    const std::uint32_t available = m_pReadBuf->m_dwSize;

    if (m_Mode == SocketMode::SM_CLOSE) {
        Close();
        return;
    }

    while (packetOffset < available) {
        if (m_Mode == SocketMode::SM_READ_LEN) {
            if (available < packetOffset + 2U) {
                Read(available - packetOffset);
                return;
            }
            const std::uint16_t frameLength = static_cast<std::uint16_t>(m_pReadBuf->m_Buffer[packetOffset]) |
                static_cast<std::uint16_t>(m_pReadBuf->m_Buffer[packetOffset + 1U] << 8U);
            m_nPacketLen = static_cast<int>(frameLength) - 2;
            if (m_nPacketLen < 1 || m_nPacketLen > kMaximumOlympiadInboundBody) {
                const unsigned int socketFunctionLow =
                    static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(&::socket));
                // Log.Add(
                //     LOG_ERROR,
                //     L"[WORLD_OLYMPIAD][%s][%d]Socket %x: bad packet size %d",
                //     L"OlympiadNetwork.cpp",
                //     0x60,
                //     socketFunctionLow,
                //     m_nPacketLen)
                L2_LOG_ERROR(
                    Log, u"[WORLD_OLYMPIAD][%s][%d]Socket %x: bad packet size %d", u"OlympiadNetwork.cpp", 0x60,
                    socketFunctionLow, m_nPacketLen
                );
                m_Mode = SocketMode::SM_CLOSE;
                Close();
                return;
            }
            m_Mode = SocketMode::SM_READ_BODY;
            packetOffset += 2U;
        }

        if (m_Mode != SocketMode::SM_READ_BODY) {
            Close();
            return;
        }
        if (available < packetOffset + static_cast<std::uint32_t>(m_nPacketLen)) {
            Read(available - packetOffset);
            return;
        }

        m_nPacketCount = runtime::AddInt32Wrapping(m_nPacketCount, 1);
        const std::uint8_t protocol = m_pReadBuf->m_Buffer[packetOffset];
        if (protocol >= m_cMaxProtocolNo) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]unknown protocol: %d",
            //     L"OlympiadNetwork.cpp",
            //     0x79,
            //     static_cast<int>(protocol))
            L2_LOG_ERROR(
                Log, u"[WORLD_OLYMPIAD][%s][%d]unknown protocol: %d", u"OlympiadNetwork.cpp", 0x79,
                static_cast<int>(protocol)
            );
            m_Mode = SocketMode::SM_CLOSE;
            Close();
            return;
        }

        auto* const incoming = new OlympiadIncomingPacket;
        incoming->m_pSocket = this;
        incoming->m_pBuf = m_pReadBuf;
        incoming->m_pFunc = reinterpret_cast<WOlympiadPacketCallback>(m_pPacketTable[protocol]);
        L2_ADD_REF(this, ObjectRefType::IoRetain);
        m_pReadBuf->AddRef();
        CAbstractPacket::g_nPendingPacket.fetch_add(1, std::memory_order_seq_cst);
        incoming->PostObject(static_cast<int>(packetOffset));

        packetOffset += static_cast<std::uint32_t>(m_nPacketLen);
        m_Mode = SocketMode::SM_READ_LEN;
    }
    Read(0);
}

WOlympiadSocketHost::WOlympiadSocketHost(std::uint64_t socket) : WOlympiadSocketBase(socket) {
    m_pPacketTable = reinterpret_cast<PacketFunc*>(g_OSCPacketHandlerTable.GetData());
    m_cMaxProtocolNo = static_cast<std::uint8_t>(OSC_MAX);
}

void WOlympiadSocketHost::OnClose() {
    L2_GUARD_FUNCTION();
    WOlympiadSocketBase::OnClose();
    Singleton<WOlympiadNetworkPeer>::inst().OnCloseHost();
}

WOlympiadSocketPeer::WOlympiadSocketPeer(std::uint64_t socket) : WOlympiadSocketBase(socket) {
    m_pPacketTable = reinterpret_cast<PacketFunc*>(g_OCSPacketHandlerTable.GetData());
    m_cMaxProtocolNo = static_cast<std::uint8_t>(OCS_MAX);
}

void WOlympiadSocketPeer::OnClose() {
    L2_GUARD_FUNCTION();
    WOlympiadSocketBase::OnClose();
    Singleton<WOlympiadNetworkHost>::inst().Remove(m_Socket);
    if (m_idWorld != 0) {
        Singleton<WOlympiadNetworkHost>::inst().OnClosePeer(m_idWorld);
    }
}

void WOlympiadSocketPeer::OnUserSocketClosed() {
    L2_GUARD_FUNCTION();
}

WOlympiadNetworkHost::WOlympiadNetworkHost() :
    m_pCallback(nullptr) {
    L2_GUARD_FUNCTION();
}

WOlympiadNetworkHost::~WOlympiadNetworkHost() = default;

bool WOlympiadNetworkHost::Init(int, IWOlympiadNetworkHostCallback* callback) {
    // Current L2Server 0x008e2f84 keeps the public int parameter but reads
    // the process-global Olympiad listener port for Server::Run.
    L2_GUARD_FUNCTION();
    m_pCallback = callback;
    Run(g_nOlympiadPort, &AllocateOlympiadPeerSocket, false, 0, 0);
    return true;
}

CSPointer<WOlympiadSocketPeer> WOlympiadNetworkHost::FindSocket(int worldId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto found = m_Peers.find(worldId);
    if (found == m_Peers.end()) {
        return {};
    }
    return found->second;
}

void WOlympiadNetworkHost::BroadcastPacket(OSC_PROTOCOL protocol, network::IPacket& packet) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    for (auto& entry : m_Peers) {
        if (entry.second) {
            entry.second->SendPacket(static_cast<std::uint8_t>(protocol), packet);
        }
    }
}

bool WOlympiadNetworkHost::SendPacket(int worldId, OSC_PROTOCOL protocol, network::IPacket& packet) {
    L2_GUARD_FUNCTION();
    CSPointer<WOlympiadSocketPeer> socket = FindSocket(worldId);
    if (!socket) {
        return false;
    }
    socket->SendPacket(static_cast<std::uint8_t>(protocol), packet);
    return true;
}

bool WOlympiadNetworkHost::RegisterPeer(CSPointer<WOlympiadSocketPeer> socket) {
    L2_GUARD_FUNCTION();
    if (!socket) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadNetwork.cpp",
        //     0x11d,
        //     L"WO_ASSERT_TRUE_RETURN")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadNetwork.cpp",
        //     0x11d,
        //     L"WO_ASSERT_TRUE_RETURN")
        L2_LOG_ERROR(Log, "[Olympiad] Cannot register peer: socket object is unavailable.");
        return false;
    }
    L2_GUARD(m_lock);
    const auto found = m_Peers.find(socket->m_idWorld);
    if (found == m_Peers.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]peer is connected: %d",
        //     L"OlympiadNetwork.cpp",
        //     0x129,
        //     socket->m_idWorld)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]peer is connected: %d",
        //     L"OlympiadNetwork.cpp",
        //     0x129,
        //     socket->m_idWorld)
        L2_LOG_INFO(Log, "[Olympiad] Registering connected peer for world %d.", socket->m_idWorld);
        m_Peers[socket->m_idWorld] = socket;
        return true;
    }
    return false;
}

void WOlympiadNetworkHost::UnregisterPeer(int worldId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_lock);
    const auto found = m_Peers.find(worldId);
    if (found != m_Peers.end()) {
        m_Peers.erase(found);
    }
}

bool WOlympiadNetworkHost::OnClosePeer(int worldId) {
    L2_GUARD_FUNCTION();
    if (m_pCallback) {
        m_pCallback->OnPeerDisconnected(worldId);
    }
    L2_GUARD(m_lock);
    return m_Peers.erase(worldId) != 0U;
}

WOlympiadNetworkPeer::InnerHost::InnerHost() :
    m_Server{}, m_bConnected(false), m_bSendable(false), m_pSocket(nullptr), m_nLogTick(0) {}

bool WOlympiadNetworkPeer::InnerHost::Connect() {
    L2_GUARD_FUNCTION();
    if (m_bConnected) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]Already connected to Host",
        //     L"OlympiadNetwork.cpp",
        //     0x22b)
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]Already connected to Host",
        //     L"OlympiadNetwork.cpp",
        //     0x22b)
        L2_LOG_WARN(Log, "[Olympiad] Connection attempt skipped: already connected to host world %d.", m_Server.idWorld);
        return false;
    }

#if defined(_WIN32)
    const SOCKET created = ::socket(AF_INET, SOCK_STREAM, 0);
#else
    const int created = ::socket(AF_INET, SOCK_STREAM, 0);
#endif
    const std::uint64_t nativeSocket = static_cast<std::uint64_t>(created);

#if defined(_WIN32)
    // Current writes only family/address/port. WinSock ignores sin_zero.
    ::sockaddr_in remoteAddress;
#else
    // Darwin's sockaddr_in has an additional sin_len representation lane.
    ::sockaddr_in remoteAddress{};
#endif
    remoteAddress.sin_family = AF_INET;
    remoteAddress.sin_addr.s_addr = m_Server.nInternalAddress;
    remoteAddress.sin_port = htons(static_cast<std::uint16_t>(m_Server.nPort));
    if (::connect(created, reinterpret_cast<const ::sockaddr*>(&remoteAddress), sizeof(remoteAddress)) == -1) {
        const std::uint32_t now = runtime::GetTickCount();
        if (now - m_nLogTick > 60000U) {
            const int error = CurrentOlympiadLastError();
            // Log.Add(
            //     LOG_ERROR,
            //     L"[WORLD_OLYMPIAD][%s][%d]Can't connect to Host[%d] : %d",
            //     L"OlympiadNetwork.cpp",
            //     0x23c,
            //     m_Server.idWorld,
            //     error)
            // L2_LOG_ERROR(Log,
            //     L"[WORLD_OLYMPIAD][%s][%d]Can't connect to Host[%d] : %d",
            //     L"OlympiadNetwork.cpp",
            //     0x23c,
            //     m_Server.idWorld,
            //     error)
#if defined(_WIN32)
            constexpr const char* errorDomain = "GetLastError (legacy source, not WSAGetLastError)";
#else
            constexpr const char* errorDomain = "errno";
#endif
            L2_LOG_ERROR(
                Log,
                "[Olympiad] Cannot connect to host world %d, port %u: %s %d. Closing attempted socket; failure reports are "
                "limited to one per minute.",
                m_Server.idWorld, static_cast<unsigned int>(static_cast<std::uint16_t>(m_Server.nPort)), errorDomain, error
            );
            m_nLogTick = runtime::GetTickCount();
        }
        CloseOlympiadNativeSocket(nativeSocket);
        return false;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Connected to Host: %d",
    //     L"OlympiadNetwork.cpp",
    //     0x244,
    //     m_Server.idWorld)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]Connected to Host: %d",
    //     L"OlympiadNetwork.cpp",
    //     0x244,
    //     m_Server.idWorld)
    L2_LOG_INFO(Log, "[Olympiad] TCP connection established to host world %d; attaching transport.", m_Server.idWorld);
    auto* const socket = new WOlympiadSocketHost(nativeSocket);
    const bool bound = portable::BindNativeSocketTransport(
        socket, nativeSocket,
        [](void* owner, const std::uint8_t* data, std::size_t length) {
            return portable::FeedSocketRead(static_cast<WOlympiadSocketHost*>(owner), data, length);
        },
        [](void* owner, int error) { portable::FeedSocketTerminal(static_cast<WOlympiadSocketHost*>(owner), error); }
    );
    if (!bound) {
        CloseOlympiadNativeSocket(nativeSocket);
        L2_RELEASE(socket, ObjectRefType::CreateDelete);
        return false;
    }
    m_pSocket = socket;
    socket->Initialize();
    socket->m_idWorld = m_Server.idWorld;
    return true;
}

void WOlympiadNetworkPeer::InnerHost::Reconnect() {
    L2_GUARD_FUNCTION();
    if (m_bConnected && m_pSocket) {
        return;
    }

    const std::uint32_t now = runtime::GetTickCount();
    if (now - m_nLogTick > 60000U) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"[WORLD_OLYMPIAD][%s][%d]Connecting host[%d]...",
        //     L"OlympiadNetwork.cpp",
        //     0x1f0,
        //     m_Server.idWorld)
        L2_LOG_INFO(Log, u"[WORLD_OLYMPIAD][%s][%d]Connecting host[%d]...", u"OlympiadNetwork.cpp", 0x1f0, m_Server.idWorld);
    }
    if (!Connect()) {
        m_bConnected = false;
        m_bSendable = false;
        m_pSocket = nullptr;
        return;
    }

    m_bConnected = true;
    m_bSendable = true;
    olympiad::OCQ_VERSION version{};
    version.nWorldId = g_nWorldId;
    m_pSocket->SendPacket(static_cast<std::uint8_t>(OCS_PROTOCOL::OCQ_VERSION), version);
}

bool WOlympiadNetworkPeer::InnerHost::SendPacket(OCS_PROTOCOL protocol, network::IPacket& packet) {
    L2_GUARD_FUNCTION();
    if (!m_pSocket || !m_bConnected || !m_bSendable) {
        return false;
    }
    m_pSocket->SendPacket(static_cast<std::uint8_t>(protocol), packet);
    return true;
}

WOlympiadNetworkPeer::WOlympiadNetworkPeer() :
    m_pHost(nullptr), m_nThreadId(0), m_hThread(nullptr), m_pCallback(nullptr) {
    L2_GUARD_FUNCTION();
}

WOlympiadNetworkPeer::~WOlympiadNetworkPeer() {
    delete m_pHost;
    m_pHost = nullptr;
}

bool WOlympiadNetworkPeer::Init(IWOlympiadNetworkPeerCallback* callback) {
    L2_GUARD_FUNCTION();
    m_pCallback = callback;
    if (!m_pHost) {
        m_pHost = new InnerHost;
    }
    m_pHost->m_Server = Singleton<WOlympiadServiceConfig>::inst().m_Server;

    std::thread worker(&WOlympiadNetworkPeer::ConnectThread, this);
    m_nThreadId = static_cast<std::uint32_t>(std::hash<std::thread::id>{}(worker.get_id()));
    worker.detach();
    return true;
}

bool WOlympiadNetworkPeer::IsAvailable() const {
    L2_GUARD_FUNCTION();
    if (m_pHost == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadNetwork.cpp",
        //     0x2b6,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        // keep the recovered assertion log, but report the
        // missing peer's configuration and the unchanged unavailable result.
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadNetwork.cpp",
        //     0x2b6,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        LogUninitializedOlympiadPeer("Availability check returned false");
        return false;
    }
    return m_pHost->m_pSocket && m_pHost->m_bConnected && m_pHost->m_bSendable;
}

bool WOlympiadNetworkPeer::SendPacket(OCS_PROTOCOL protocol, network::IPacket& packet) {
    L2_GUARD_FUNCTION();
    if (m_pHost == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadNetwork.cpp",
        //     0x2cb,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        // configuration-aware diagnostics. Recovered log kept.
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadNetwork.cpp",
        //     0x2cb,
        //     L"WO_ASSERT_NOT_NULL_RETURN")
        LogUninitializedOlympiadPeer("Packet send skipped");
        return false;
    }
    return m_pHost->SendPacket(protocol, packet);
}

void WOlympiadNetworkPeer::Reconnect() {
    L2_GUARD_FUNCTION();
    if (m_pHost == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadNetwork.cpp",
        //     0x293,
        //     L"WO_ASSERT_NOT_NULL")
        // configuration-aware diagnostics. Recovered log kept.
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadNetwork.cpp",
        //     0x293,
        //     L"WO_ASSERT_NOT_NULL")
        LogUninitializedOlympiadPeer("Reconnect skipped");
        return;
    }
    m_pHost->Reconnect();
}

void WOlympiadNetworkPeer::OnCloseHost() {
    L2_GUARD_FUNCTION();
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]closed host",
    //     L"OlympiadNetwork.cpp",
    //     0x29d)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][WORLD_OLYMPIAD][%s][%d]closed host",
    //     L"OlympiadNetwork.cpp",
    //     0x29d)
    L2_LOG_WARN(Log, "[Olympiad] Host connection closed; cause not supplied by callback.");
    if (m_pHost == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadNetwork.cpp",
        //     0x29e,
        //     L"WO_ASSERT_NOT_NULL")
        // L2_LOG_ERROR(Log,
        //     L"[WORLD_OLYMPIAD][%s][%d]assert falied: %s",
        //     L"OlympiadNetwork.cpp",
        //     0x29e,
        //     L"WO_ASSERT_NOT_NULL")
        L2_LOG_ERROR(
            Log, "[Olympiad] Cannot process host closure: host state is unavailable; close callback will not be forwarded."
        );
        return;
    }
    m_pHost->m_bConnected = false;
    m_pHost->m_bSendable = false;
    m_pHost->m_pSocket = nullptr;
    if (m_pCallback) {
        m_pCallback->OnCloseHost();
    }
}

void WOlympiadNetworkPeer::ConnectThread(WOlympiadNetworkPeer* self) {
    // Diagnostic sequence only. Independent of recovered TLS IDs and socket handles.
    static std::atomic<unsigned long long> nextLogIndex{0};
    runtime::logging::SetIndexedThreadName("olympiad-connect", nextLogIndex.fetch_add(1, std::memory_order_relaxed));
    while (self) {
        self->Reconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
}
