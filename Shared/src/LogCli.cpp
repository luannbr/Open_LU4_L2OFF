#include "Shared/src/LogCli.h"

#include "L2Core/src/inc/Guard.h"
#if defined(L2_BUILD_CACHED)
#include "Cached.h"
#elif defined(L2_BUILD_L2NPC)
#include "L2NPC/src/Config.h"
#elif defined(L2_BUILD_L2SERVER)
#include "CConfig.h"
#else
#error "Shared/logcli.cpp must be compiled for a concrete server process"
#endif
#include "Shared/src/Runtime/L2ServerClient.h"
#include "Shared/src/Runtime/LocalLogScope.h"
#include "Shared/src/Runtime/NativeThreadHandle.h"
#include "Shared/src/Runtime/SocketTransport.h"
#include "Shared/src/IOC.h"
#include "Shared/src/Log.h"
#include "Shared/src/ThreadManager.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>

namespace {

#if defined(L2_BUILD_CACHED)
constexpr int kLogThreadId = 18;

int ProcessWorldId() { return GetCacheDConfiguration().worldId; }
#else
constexpr int kLogThreadId = ThreadID::LOG;

int ProcessWorldId() { return g_nWorldId; }

#if defined(L2_BUILD_L2SERVER)
std::string ProcessLogServerAddress() {
    // Current L2Server.exe 0x00810c9f copies the four bytes of
    // config.obj's in_addr directly into sockaddr_in. The portable client
    // accepts text, so keep those same target-memory bytes at the OS
    // transport boundary.
    const auto* const bytes = reinterpret_cast<const unsigned char*>(&logServerAddr);
    return std::to_string(bytes[0]) + "." + std::to_string(bytes[1]) + "." + std::to_string(bytes[2]) + "." +
        std::to_string(bytes[3]);
}
#endif
#endif

struct CLogCliPortableState;
struct CLogConnectionContext;

void OnPortableBytes(
    CLogCli& client, const std::shared_ptr<CLogConnectionContext>& connection, const std::uint8_t* data, int length
);
void OnPortableClose(CLogCli& client, const std::shared_ptr<CLogConnectionContext>& connection);

} // namespace

struct runtime::log::CLogCliPortableAccess {
    static ::CSPointer<CLogSocket>& Socket(CLogCli& client) { return client.m_spLogSocket; }
};

namespace {

struct CLogCliPortableState {
    CLogCliPortableState() : transport("logd") {}

    std::recursive_mutex section;
    network::L2ServerClient transport;
    ::portable::SocketTransportBinding socketTransport;
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::string host{"127.0.0.1"};
    int port = 3999;
    bool configured = false;
    bool ownerConstructed = false;
    bool entryReleased = false;
    bool stopping = false;
    bool connectionRequested = false;
    bool receivingEnabled = false;
    std::atomic<std::uint64_t> transportGeneration{0};
    std::uint32_t workerId = 0;
    void* workerHandle = nullptr;
};

// One context per successful native connection. The target gets this
// association from the completion key and OVERLAPPED owner. Keeping it here
// prevents a late old-connection completion from resolving through
// CLogCli::m_spLogSocket after that field has published a replacement.
struct CLogConnectionContext {
    explicit CLogConnectionContext(std::uint64_t value) noexcept : generation(value) {}

    std::uint64_t generation;
    // The target completion key carries the raw CLogSocket*. Its outstanding
    // WSARecv IoRetain, not an extra smart-pointer owner, keeps the object
    // alive until the queued completion has run.
    CLogSocket* socket = nullptr;
};

using CLogCliPortableStatePtr = std::shared_ptr<CLogCliPortableState>;
using CLogCliPortableStateMap = std::unordered_map<CLogCli*, CLogCliPortableStatePtr>;

std::mutex& PortableStateMapMutex() {
    static auto* const mutex = new std::mutex;
    return *mutex;
}

CLogCliPortableStateMap& PortableStateMap() {
    static auto* const states = new CLogCliPortableStateMap;
    return *states;
}

CLogCliPortableStatePtr PortableState(CLogCli* client) {
    std::lock_guard<std::mutex> guard(PortableStateMapMutex());
    CLogCliPortableStateMap& states = PortableStateMap();
    const auto found = states.find(client);
    if (found != states.end()) {
        return found->second;
    }
    CLogCliPortableStatePtr state = std::make_shared<CLogCliPortableState>();
    states.emplace(client, state);
    return state;
}

CLogCliPortableStatePtr PortableState(CLogCli& client) { return PortableState(&client); }

CLogCliPortableStatePtr DetachPortableState(CLogCli& client) {
    std::lock_guard<std::mutex> guard(PortableStateMapMutex());
    CLogCliPortableStateMap& states = PortableStateMap();
    const auto found = states.find(&client);
    if (found == states.end()) {
        return {};
    }
    CLogCliPortableStatePtr state = found->second;
    states.erase(found);
    return state;
}

void StopPortableClient(CLogCli& client, const CLogCliPortableStatePtr& state) {
    std::thread worker;
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        if (state->worker.joinable()) {
            state->stopping = true;
            state->connectionRequested = false;
            worker = std::move(state->worker);
        }
    }
    state->wake.notify_all();
    // Cancel a target-style blocking LogD connect before joining its worker.
    // L2ServerClient keeps the successful path blocking-equivalent but makes
    // this owner close observable by the native readiness wait.
    state->transportGeneration.fetch_add(1, std::memory_order_acq_rel);
    state->transport.Close();
    if (worker.joinable()) {
        worker.join();
    }

    ::CSPointer<CLogSocket> socket;
    {
        std::lock_guard<std::recursive_mutex> guard(state->section);
        socket = runtime::log::CLogCliPortableAccess::Socket(client);
    }
    if (socket) {
        // The native reader has been joined, so no IOCP-equivalent
        // cancellation completion will arrive to release its WSARecv keep.
        ::portable::CancelSocketReads(static_cast<CLogSocketBase*>(socket.get()));
        socket->Close();
    }
    {
        std::lock_guard<std::recursive_mutex> guard(state->section);
        runtime::log::CLogCliPortableAccess::Socket(client).Release();
    }
    state->socketTransport.Reset();
}

void OnPortableBytes(
    CLogCli& client, const std::shared_ptr<CLogConnectionContext>& connection, const std::uint8_t* data, int length
) {
    if (!connection || !data || length <= 0) {
        return;
    }

    const CLogCliPortableStatePtr state = PortableState(client);

    CLogSocket* const socket = connection->socket;
    if (!socket) {
        if (state->transportGeneration.load(std::memory_order_acquire) == connection->generation) {
            state->transport.RejectPeerProtocol();
        }
        return;
    }

    if (!::portable::FeedSocketRead(static_cast<CLogSocketBase*>(socket), data, static_cast<std::size_t>(length))) {
        state->transport.RejectPeerProtocol();
    }
}

void OnPortableClose(CLogCli& client, const std::shared_ptr<CLogConnectionContext>& connection) {
    if (!connection) {
        return;
    }

    const CLogCliPortableStatePtr state = PortableState(client);
    const int readError = state->transport.LastReadError();
    CLogSocket* const socket = connection->socket;
    if (socket) {
        ::portable::FeedSocketTerminal(static_cast<CLogSocketBase*>(socket), readError);
    }
}

} // namespace

CLogCli logcli;

CLogCli::CLogCli() {
    L2_GUARD_FUNCTION();
    const CLogCliPortableStatePtr state = PortableState(this);
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        state->ownerConstructed = true;
    }
    state->wake.notify_all();
}

CLogCli::~CLogCli() {
    L2_GUARD_FUNCTION();
    runtime::log::StopLogClient(*this);
    DetachPortableState(*this);
}

void CLogCli::Enter() { PortableState(*this)->section.lock(); }

void CLogCli::Leave() { PortableState(*this)->section.unlock(); }

void CLogCli::LogSendMsg(LogType logType, char16_t* timestampedMessage) {
    // Current L2NPC.exe 0x0045f6cc. Current Cached.exe 0x004ef304.
    L2_GUARD_FUNCTION();
    const runtime::log::LocalLogScope localDiagnostics;
    m_spLogSocket->Send("cddS", 0, CLogSocket::kServerType, logType, timestampedMessage);
}

bool CLogCli::Init() {
    // Current EXE 0x0045fcd0: the target critical section covers both the
    // sendable check and the complete Connect call.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(*this);
        if (!Log.IsSendable()) {
            // Noise: Connect reports the attempt with its configured endpoint.
            // Log.Add(
            //     LOG_NORMAL,
            //     L"CLogCli::Init()  Connect()")
            Connect();
        }
    }
    // The exact CLogCli::Init returns true regardless of the result.
    return true;
}

bool CLogCli::Connect() {
    L2_GUARD_FUNCTION();
    const runtime::log::LocalLogScope localDiagnostics;

    const CLogCliPortableStatePtr state = PortableState(*this);
    std::string host;
    int port = 3999;
#if defined(L2_BUILD_L2SERVER)
    // Current L2Server.exe reads config.obj's address/port globals on each
    // Connect call. They are not snapshots of TLS or portable sidecar state.
    host = ProcessLogServerAddress();
    port = static_cast<int>(static_cast<unsigned short>(logServerPort));
#else
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        host = state->host;
        port = state->port;
    }
#endif

    const std::uint64_t generation = state->transportGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    const auto connection = std::make_shared<CLogConnectionContext>(generation);
    CLogCli* const owner = this;
    state->transport.SetReceiveSink(
        [owner, connection](const std::uint8_t* data, int length) { OnPortableBytes(*owner, connection, data, length); },
        [owner, connection] { OnPortableClose(*owner, connection); }
    );
    network::L2ServerClient::ConnectOptions connectOptions;
    // Current CLogCli::Connect at 0x0045fa14 creates an IPv4 socket from
    // the inet_addr value stored by NPCServer::Load and blocks in connect.
    // Unlike NPCServer/CDBNPC it relies on the socket's implicit local bind.
    connectOptions.startReceiving = false;
    connectOptions.blocking = true;
    connectOptions.numericIPv4Only = true;
    // Log.Add(LOG_NORMAL, "[LogD] Connecting to %s:%u...", host.c_str(),
    //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)))
    L2_LOG_INFO(
        Log, "[LogD] Connecting to %s:%u...", host.c_str(), static_cast<unsigned int>(static_cast<std::uint16_t>(port))
    );
    // Connect is the bool-only wrapper around this same operation. Retain the
    // result so failure diagnostics can identify the stage and captured error.
    // if (!state->transport.Connect(
    //         host,
    //         port,
    //         connectOptions)) {
    const auto result = state->transport.ConnectWithResult(host, port, connectOptions);
    if (!result) {
        const char* stage = "connecting";
        const char* errorDomain = "transport";
        switch (result.failure) {
            case network::SocketConnectFailure::Initialize:
                stage = "initializing the transport";
                break;
            case network::SocketConnectFailure::ResolveAddress:
                stage = "resolving the address";
                errorDomain = "getaddrinfo";
                break;
            case network::SocketConnectFailure::CreateSocket:
                stage = "creating the socket";
                break;
            case network::SocketConnectFailure::BindSocket:
                stage = "binding the socket";
                break;
            case network::SocketConnectFailure::ConnectSocket:
            case network::SocketConnectFailure::None:
                break;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"Can't connect to log server")
        // Zero can be returned on cancellation. It is not proof of OS success.
        // Log.Add(LOG_ERROR,
        //     "[LogD] Could not connect to %s:%u while %s (%s code %d%s).",
        //     host.c_str(), static_cast<unsigned int>(static_cast<std::uint16_t>(port)),
        //     stage, errorDomain, result.error,
        //     result.error == 0 ? ", no error code was reported" : "")
        L2_LOG_ERROR(
            Log, "[LogD] Could not connect to %s:%u while %s (%s code %d%s).", host.c_str(),
            static_cast<unsigned int>(static_cast<std::uint16_t>(port)), stage, errorDomain, result.error,
            result.error == 0 ? "; no error code was reported" : ""
        );
        return false;
    }

    auto* const socket = new CLogSocket(generation, logdbPacketTable);
    connection->socket = socket;
    const std::weak_ptr<CLogCliPortableState> weakState = state;
    state->socketTransport.Reset(
        socket,
        [weakState, generation](const std::uint8_t* data, int length, ::portable::SocketWriteCompletion completion) {
            const CLogCliPortableStatePtr locked = weakState.lock();
            if (!locked || locked->transportGeneration.load(std::memory_order_acquire) != generation) {
                SetSocketLastError(10038);
                return ::portable::SocketWriteSubmission::Failed();
            }
            return locked->transport.SubmitRawWrite(data, length, std::move(completion));
        },
        [weakState, generation] {
            if (const CLogCliPortableStatePtr locked = weakState.lock()) {
                if (locked->transportGeneration.load(std::memory_order_acquire) != generation) {
                    return;
                }
                locked->transport.RejectPeerProtocol();
            }
        },
        [weakState, generation] {
            const CLogCliPortableStatePtr locked = weakState.lock();
            if (!locked || locked->transportGeneration.load(std::memory_order_acquire) != generation) {
                return;
            }
            locked->transport.InitializeTargetSocket();
        },
        [weakState, generation] {
            const CLogCliPortableStatePtr locked = weakState.lock();
            if (!locked || locked->transportGeneration.load(std::memory_order_acquire) != generation) {
                return;
            }
            locked->transport.RejectPeerProtocol(false);
        },
        [weakState, generation](std::size_t capacity) {
            const CLogCliPortableStatePtr locked = weakState.lock();
            if (!locked || locked->transportGeneration.load(std::memory_order_acquire) != generation) {
                return false;
            }
            locked->transport.SetNextReadCapacity(static_cast<int>(capacity));
            return true;
        }
    );

    // Current EXE allocates a fresh 0xd0 CLogSocket, keeps it in a local
    // ORT_FUNCTION_CHAIN CSPointer, copy-assigns that carrier into the member,
    // then releases the local before virtual Initialize.
    {
        ::CSPointer<CLogSocket> replacement;
        replacement.SetObject(socket, ::ObjectRefType::FunctionChain);
        m_spLogSocket = replacement;
    }
    socket->Initialize();
    if (socket->m_hSocket.load(std::memory_order_acquire) == UINT64_C(0xffffffffffffffff)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Connected to log server, "
        //     L"But Some error has occurred")
        // Log.Add(LOG_ERROR,
        //     "[LogD] Connected to %s:%u, but the socket closed during initialization. "
        //     "the handshake was not started.", host.c_str(),
        //     static_cast<unsigned int>(static_cast<std::uint16_t>(port)))
        L2_LOG_ERROR(
            Log,
            "[LogD] Connected to %s:%u, but the socket closed during initialization; "
            "the handshake was not started.",
            host.c_str(), static_cast<unsigned int>(static_cast<std::uint16_t>(port))
        );
        return false;
    }

    bool receive = false;
    {
        // StartLogClientReceiving can run while this LOG worker is inside
        // connect(). Re-read after publication so that wake cannot be lost
        // when ConnectWithResult initializes its new reader as disabled.
        std::lock_guard<std::mutex> guard(state->mutex);
        receive = state->receivingEnabled;
    }
    if (receive) {
        state->transport.StartReceiving();
    }

    // Current CLogCli::Connect sends this through virtual CLogSocket::Send.
    socket->Send("cd", 1, 1);
    return true;
}

void CLogCli::ConfirmConnection() {
    // Current L2NPC.exe 0x0045f578. Current Cached.exe 0x004ef1b0.
    L2_GUARD_FUNCTION();
    const runtime::log::LocalLogScope localDiagnostics;
    L2_GUARD(*this);
    if (!m_spLogSocket) {
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Connect to log server falied, "
        //     L"next connection will be 10 min")
        // Reconnection is driven by later log writes, not a ten-minute timer.
        // The product-specific Sendable stores below remain as recovered.
        // Log.Add(LOG_ERROR,
        //     "[LogD] Received connection confirmation without an active log socket.")
        L2_LOG_ERROR(Log, "[LogD] Received connection confirmation without an active log socket.");
#if defined(L2_BUILD_L2NPC)
        // Current L2NPC.exe contains the distinct true store. Cached.exe and
        // current L2Server.exe 0x00810848 both write false here.
        Log.Sendable(true);
#else
        Log.Sendable(false);
#endif
    } else {
        Log.Sendable(true);
        // Log.Add(
        //     LOG_NORMAL,
        //     L"Connected to log server")
        // Log.Add(LOG_NORMAL, "[LogD] Connection confirmed, forwarding logs to LogD.")
        L2_LOG_INFO(Log, "[LogD] Connection confirmed; forwarding logs to LogD.");
        Log.CloseLogFile();
        m_spLogSocket->Send("cdd", 3, ProcessWorldId(), CLogSocket::kServerType);
    }
}

runtime::log::LogClientThreadIdentity runtime::log::StartLogClientConnectionWorker(CLogCli* client) {
    LogClientThreadIdentity identity;
    if (!client) {
        return identity;
    }

    const CLogCliPortableStatePtr state = PortableState(client);
    std::lock_guard<std::mutex> guard(state->mutex);
    if (!state->worker.joinable()) {
        state->stopping = false;
        try {
            state->worker = std::thread([state] {
                {
                    std::unique_lock<std::mutex> startupGuard(state->mutex);
                    state->wake.wait(startupGuard, [&state] {
                        return state->stopping || (state->ownerConstructed && state->entryReleased);
                    });
                    if (state->stopping) {
                        CThreadManager::SetThread(kLogThreadId, nullptr);
                        return;
                    }
                }
                ConnectThread(nullptr);
            });
            state->workerHandle = runtime::NativeThreadHandle(state->worker);
            state->workerId = runtime::NativeThreadId(state->worker);
        } catch (const std::system_error&) {
            state->workerHandle = nullptr;
            state->workerId = 0;
        }
    }
    identity.id = state->workerId;
    identity.handle = state->workerHandle;
    return identity;
}

void runtime::log::ConfigureLogClient(CLogCli& client, std::string host, int port) {
    const CLogCliPortableStatePtr state = PortableState(client);
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        state->host = std::move(host);
        state->port = port;
        state->configured = true;
    }
    const LogClientThreadIdentity identity = StartLogClientConnectionWorker(&client);
    CThreadManager::SetThread(kLogThreadId, identity.handle);
}

void runtime::log::WakeLogClientConnectionWorker(CLogCli& client) {
    const CLogCliPortableStatePtr state = PortableState(client);
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        if (!state->configured || state->stopping) {
            return;
        }
        state->connectionRequested = true;
        state->entryReleased = true;
    }
    state->wake.notify_one();
}

bool runtime::log::WaitForLogClientConnectionRequest(CLogCli& client) {
    const CLogCliPortableStatePtr state = PortableState(client);
    std::unique_lock<std::mutex> guard(state->mutex);
    state->wake.wait(guard, [&state] { return state->stopping || state->connectionRequested; });
    if (state->stopping) {
        return false;
    }
    state->connectionRequested = false;
    return true;
}

void runtime::log::StartLogClientReceiving(CLogCli& client) {
    const CLogCliPortableStatePtr state = PortableState(client);
    {
        std::lock_guard<std::mutex> guard(state->mutex);
        state->receivingEnabled = true;
    }
    state->transport.StartReceiving();
}

void runtime::log::StopLogClient(CLogCli& client) { StopPortableClient(client, PortableState(client)); }
