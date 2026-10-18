#include "Platform/IOServerTransport.h"

#include "IOServer.h"
#include "Platform/IOSocketTransport.h"
#include "StateMonMgr.h"

#include "Shared/src/Runtime/NativeListenerTransport.h"
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/NativeTcpSocket.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>

namespace authgated::platform {
namespace {

struct PortableAcceptOperation {
    _OVERLAPPED overlapped{};
    AcceptedSocketInfo socket;
};

struct ListenerEntry {
    const CIOServer* owner = nullptr;
    std::uint64_t nativeSocket = runtime::kInvalidNativeSocket;
};

struct ListenerRegistry {
    std::mutex mutex;
    std::vector<ListenerEntry> entries;
};

ListenerRegistry& Registry() {
    static auto* const registry = new ListenerRegistry;
    return *registry;
}

std::uint64_t FindListener(const CIOServer* owner) noexcept {
    ListenerRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    const auto found = std::find_if(registry.entries.begin(), registry.entries.end(), [owner](const ListenerEntry& entry) {
        return entry.owner == owner;
    });
    return found == registry.entries.end() ? runtime::kInvalidNativeSocket : found->nativeSocket;
}

void StoreListener(const CIOServer* owner, std::uint64_t socket) {
    ListenerRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    const auto found = std::find_if(registry.entries.begin(), registry.entries.end(), [owner](const ListenerEntry& entry) {
        return entry.owner == owner;
    });
    if (found == registry.entries.end()) {
        registry.entries.push_back({owner, socket});
    } else {
        found->nativeSocket = socket;
    }
}

std::uint64_t TakeListener(const CIOServer* owner) noexcept {
    ListenerRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    const auto found = std::find_if(registry.entries.begin(), registry.entries.end(), [owner](const ListenerEntry& entry) {
        return entry.owner == owner;
    });
    if (found == registry.entries.end()) {
        return runtime::kInvalidNativeSocket;
    }
    const std::uint64_t socket = found->nativeSocket;
    registry.entries.erase(found);
    return socket;
}

void OnListenerReady(void* object) {
    auto* const owner = static_cast<CIOServer*>(object);
    CStateMonMgr* const state = CStateMonMgr::g_pStateMonMgr;
    if (state && state->acceptCallCount.load(std::memory_order_relaxed) <= 0) {
        ::portable::AcknowledgeNativeListenerTransport(owner);
        return;
    }

    const std::uint64_t listener = FindListener(owner);
    sockaddr_in remoteAddress{};
    const std::uint64_t accepted = runtime::AcceptTcpSocket(listener, remoteAddress);
    if (accepted != runtime::kInvalidNativeSocket) {
        auto operation = std::make_unique<PortableAcceptOperation>();
        operation->socket.nativeSocket = accepted;
        operation->socket.targetSocket = runtime::TargetSocketToken(accepted, kInvalidSocket);
        operation->socket.remoteAddress = remoteAddress;
        _OVERLAPPED* const overlapped = &operation.release()->overlapped;
        owner->OnIOCallback(1, 0, overlapped);
    }
    ::portable::AcknowledgeNativeListenerTransport(owner);
}

PortableAcceptOperation* OperationFrom(_OVERLAPPED* overlapped) noexcept {
    return reinterpret_cast<PortableAcceptOperation*>(overlapped);
}

} // namespace

bool OpenNativeListener(CIOServer* owner, int port, std::uint32_t& targetSocket, ListenerOpenFailure& failure) noexcept {
    targetSocket = kInvalidSocket;
    failure = {};
    const std::uint64_t nativeSocket = runtime::OpenTcpSocket();
    if (nativeSocket == runtime::kInvalidNativeSocket) {
        failure.stage = ListenerOpenStage::Socket;
        failure.error = ::portable::GetNativeSocketLastError();
        return false;
    }
    if (!runtime::BindTcpListener(nativeSocket, static_cast<std::uint16_t>(port))) {
        failure.stage = ListenerOpenStage::Bind;
        failure.error = ::portable::GetNativeSocketLastError();
        runtime::CloseTcpSocket(nativeSocket);
        return false;
    }
    if (!runtime::ListenTcpSocket(nativeSocket, SOMAXCONN)) {
        failure.stage = ListenerOpenStage::Listen;
        failure.error = ::portable::GetNativeSocketLastError();
        runtime::CloseTcpSocket(nativeSocket);
        return false;
    }

    StoreListener(owner, nativeSocket);
    if (!::portable::BindNativeListenerTransport(owner, nativeSocket, OnListenerReady)) {
        failure.stage = ListenerOpenStage::CompletionPort;
        failure.error = ::portable::GetNativeSocketLastError();
        TakeListener(owner);
        runtime::CloseTcpSocket(nativeSocket);
        return false;
    }
    targetSocket = runtime::TargetSocketToken(nativeSocket, kInvalidSocket);
    return true;
}

void CloseNativeListener(CIOServer* owner, [[maybe_unused]] std::uint32_t targetSocket) noexcept {
    ::portable::UnbindNativeListenerTransport(owner);
    runtime::CloseTcpSocket(TakeListener(owner));
}

bool GetAcceptedSocketInfo(_OVERLAPPED* overlapped, AcceptedSocketInfo& info) noexcept {
    if (!overlapped)
        return false;
    info = OperationFrom(overlapped)->socket;
    return info.nativeSocket != runtime::kInvalidNativeSocket;
}

void ReleaseAcceptedSocket(_OVERLAPPED* overlapped, bool closeSocket) noexcept {
    if (!overlapped)
        return;
    PortableAcceptOperation* const operation = OperationFrom(overlapped);
    if (closeSocket) {
        runtime::CloseTcpSocket(operation->socket.nativeSocket);
    }
    delete operation;
}

} // namespace authgated::platform
