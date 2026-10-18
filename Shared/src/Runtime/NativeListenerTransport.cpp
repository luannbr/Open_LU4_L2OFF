#include "Shared/src/Runtime/NativeListenerTransport.h"
#include <atomic>
#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/OperatorLog.h"

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#else
#include <fcntl.h>
#include <poll.h>
#endif

namespace portable {
namespace {

constexpr std::uint64_t kInvalidNativeSocket = UINT64_C(0xffffffffffffffff);

bool SetNonBlocking(std::uint64_t socket) noexcept {
#if defined(_WIN32)
    u_long enabled = 1;
    return ::ioctlsocket(static_cast<SOCKET>(socket), FIONBIO, &enabled) == 0;
#else
    const int descriptor = static_cast<int>(socket);
    const int flags = ::fcntl(descriptor, F_GETFL, 0);
    return flags >= 0 && ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

int WaitReadable(std::uint64_t socket) noexcept {
#if defined(_WIN32)
    fd_set readable;
    fd_set failed;
    FD_ZERO(&readable);
    FD_ZERO(&failed);
    FD_SET(static_cast<SOCKET>(socket), &readable);
    FD_SET(static_cast<SOCKET>(socket), &failed);
    timeval timeout{0, 100000};
    return ::select(0, &readable, nullptr, &failed, &timeout);
#else
    pollfd descriptor{};
    descriptor.fd = static_cast<int>(socket);
    descriptor.events = POLLIN;
    int result = 0;
    do {
        result = ::poll(&descriptor, 1, 100);
    } while (result < 0 && errno == EINTR);
    return result;
#endif
}

class NativeListenerTransport final {
public:
    NativeListenerTransport(void* owner, std::uint64_t socket, NativeListenerReadySink readySink) noexcept :
        owner_(owner), socket_(socket), readySink_(readySink) {}

    ~NativeListenerTransport() { Stop(); }

    bool Start() {
        if (!SetNonBlocking(socket_)) {
            return false;
        }
        try {
            worker_ = std::thread([this] { Run(); });
            return true;
        } catch (...) {
            SetNativeSocketLastError(10055);
            return false;
        }
    }

    void Acknowledge() {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            outstanding_ = false;
        }
        wake_.notify_all();
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if (stopping_) {
                return;
            }
            stopping_ = true;
            outstanding_ = false;
        }
        wake_.notify_all();
        if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
            worker_.join();
        }
    }

private:
    bool IsStopping() {
        std::lock_guard<std::mutex> guard(mutex_);
        return stopping_;
    }

    void Run() {
        // Diagnostic sequence only. Independent of recovered TLS IDs and socket handles.
        static std::atomic<unsigned long long> nextLogIndex{0};
        runtime::logging::SetIndexedThreadName("accept", nextLogIndex.fetch_add(1, std::memory_order_relaxed));
        for (;;) {
            if (IsStopping()) {
                return;
            }
            const int waitResult = WaitReadable(socket_);
            if (waitResult == 0) {
                continue;
            }
            if (waitResult < 0 && IsStopping()) {
                return;
            }

            {
                std::lock_guard<std::mutex> guard(mutex_);
                if (stopping_) {
                    return;
                }
                outstanding_ = true;
            }
            readySink_(owner_);

            std::unique_lock<std::mutex> guard(mutex_);
            wake_.wait(guard, [this] { return stopping_ || !outstanding_; });
            if (stopping_) {
                return;
            }
        }
    }

    void* const owner_;
    const std::uint64_t socket_;
    NativeListenerReadySink const readySink_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    bool outstanding_ = false;
    bool stopping_ = false;
};

struct ListenerRegistry {
    struct Entry {
        const void* owner = nullptr;
        std::shared_ptr<NativeListenerTransport> transport;
    };

    std::mutex mutex;
    std::vector<Entry> entries;
};

ListenerRegistry& Registry() {
    static auto* const registry = new ListenerRegistry;
    return *registry;
}

std::shared_ptr<NativeListenerTransport> Find(const void* owner) {
    ListenerRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    const auto found =
        std::find_if(registry.entries.begin(), registry.entries.end(), [owner](const ListenerRegistry::Entry& entry) {
            return entry.owner == owner;
        });
    return found == registry.entries.end() ? nullptr : found->transport;
}

} // namespace

bool BindNativeListenerTransport(void* owner, std::uint64_t nativeSocket, NativeListenerReadySink readySink) {
    if (!owner || !readySink || nativeSocket == kInvalidNativeSocket) {
        SetNativeSocketLastError(10038);
        return false;
    }

    auto transport = std::make_shared<NativeListenerTransport>(owner, nativeSocket, readySink);
    if (!transport->Start()) {
        return false;
    }

    std::shared_ptr<NativeListenerTransport> previous;
    ListenerRegistry& registry = Registry();
    {
        std::lock_guard<std::mutex> guard(registry.mutex);
        const auto found =
            std::find_if(registry.entries.begin(), registry.entries.end(), [owner](const ListenerRegistry::Entry& entry) {
                return entry.owner == owner;
            });
        if (found == registry.entries.end()) {
            registry.entries.push_back({owner, transport});
        } else {
            previous = std::move(found->transport);
            found->transport = transport;
        }
    }
    if (previous) {
        previous->Stop();
    }
    // The worker can observe an already-queued connection between Start and
    // registry publication. Release that first edge here if its consumer's
    // acknowledgement raced the publication. Later edges find the entry.
    transport->Acknowledge();
    return true;
}

void AcknowledgeNativeListenerTransport(const void* owner) {
    if (const auto transport = Find(owner)) {
        transport->Acknowledge();
    }
}

void UnbindNativeListenerTransport(const void* owner) {
    std::shared_ptr<NativeListenerTransport> transport;
    ListenerRegistry& registry = Registry();
    {
        std::lock_guard<std::mutex> guard(registry.mutex);
        const auto found =
            std::find_if(registry.entries.begin(), registry.entries.end(), [owner](const ListenerRegistry::Entry& entry) {
                return entry.owner == owner;
            });
        if (found != registry.entries.end()) {
            transport = std::move(found->transport);
            registry.entries.erase(found);
        }
    }
    if (transport) {
        transport->Stop();
    }
}

void StopNativeListenerTransports() {
    std::vector<std::shared_ptr<NativeListenerTransport>> transports;
    ListenerRegistry& registry = Registry();
    {
        std::lock_guard<std::mutex> guard(registry.mutex);
        transports.reserve(registry.entries.size());
        for (auto& entry : registry.entries) {
            transports.push_back(std::move(entry.transport));
        }
        registry.entries.clear();
    }
    for (const auto& transport : transports) {
        transport->Stop();
    }
}

} // namespace portable
