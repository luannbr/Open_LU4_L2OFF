// Shared native binding for the recovered CIOSocket owners.
#include "Shared/src/Runtime/SocketTransport.h"

#include <algorithm>
#include <cerrno>
#include <mutex>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#endif

namespace portable {
namespace {

struct SocketTransportRegistry {
    struct Entry {
        const void* socket = nullptr;
        SocketWriteSink writeSink;
        SocketCloseSink closeSink;
        SocketInitializeSink initializeSink;
        SocketGracefulCloseSink gracefulCloseSink;
        SocketReadArmSink readArmSink;
        std::size_t pendingReads = 0;
        bool bound = true;
    };

    std::mutex mutex;
    std::vector<Entry> entries;
};

#ifdef _WIN32
struct SocketSubsystemState {
    std::mutex mutex;
    unsigned references = 0;
};

SocketSubsystemState& SocketSubsystem() {
    static SocketSubsystemState* state = new SocketSubsystemState;
    return *state;
}
#endif

SocketTransportRegistry& Registry() {
    // Socket owners can release their last IO reference during process
    // teardown. Match the target's process-lifetime native transport tables.
    static SocketTransportRegistry* registry = new SocketTransportRegistry;
    return *registry;
}

void SetNotSocketError() {
#ifdef _WIN32
    ::WSASetLastError(WSAENOTSOCK);
#else
    errno = ENOTSOCK;
#endif
}

} // namespace

int InitializeSocketSubsystem() {
#ifdef _WIN32
    SocketSubsystemState& state = SocketSubsystem();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.references == 0) {
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0) {
            return result;
        }
    }
    ++state.references;
    return 0;
#else
    return 0;
#endif
}

void CleanupSocketSubsystem() {
#ifdef _WIN32
    SocketSubsystemState& state = SocketSubsystem();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.references != 0 && --state.references == 0) {
        WSACleanup();
    }
#endif
}

SocketTransportBinding::SocketTransportBinding(
    const void* socket,
    SocketWriteSink writeSink,
    SocketCloseSink closeSink,
    SocketInitializeSink initializeSink,
    SocketGracefulCloseSink gracefulCloseSink,
    SocketReadArmSink readArmSink
) {
    Reset(
        socket, std::move(writeSink), std::move(closeSink), std::move(initializeSink), std::move(gracefulCloseSink),
        std::move(readArmSink)
    );
}

SocketTransportBinding::~SocketTransportBinding() { Reset(); }

SocketTransportBinding::SocketTransportBinding(SocketTransportBinding&& other) noexcept : socket_(other.socket_) {
    other.socket_ = nullptr;
}

SocketTransportBinding& SocketTransportBinding::operator=(SocketTransportBinding&& other) noexcept {
    if (this != &other) {
        Reset();
        socket_ = other.socket_;
        other.socket_ = nullptr;
    }
    return *this;
}

void SocketTransportBinding::Reset(
    const void* socket,
    SocketWriteSink writeSink,
    SocketCloseSink closeSink,
    SocketInitializeSink initializeSink,
    SocketGracefulCloseSink gracefulCloseSink,
    SocketReadArmSink readArmSink
) {
    if (socket_) {
        UnbindSocketTransport(socket_);
        socket_ = nullptr;
    }
    if (socket &&
        BindSocketTransport(
            socket, std::move(writeSink), std::move(closeSink), std::move(initializeSink), std::move(gracefulCloseSink),
            std::move(readArmSink)
        )) {
        socket_ = socket;
    }
}

bool BindSocketTransport(
    const void* socket,
    SocketWriteSink writeSink,
    SocketCloseSink closeSink,
    SocketInitializeSink initializeSink,
    SocketGracefulCloseSink gracefulCloseSink,
    SocketReadArmSink readArmSink
) {
    if (!socket || (!writeSink && !closeSink && !initializeSink && !gracefulCloseSink && !readArmSink)) {
        return false;
    }

    SocketTransportRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    const auto found = std::find_if(
        registry.entries.begin(), registry.entries.end(),
        [socket](const SocketTransportRegistry::Entry& entry) { return entry.socket == socket; }
    );
    if (found != registry.entries.end()) {
        found->writeSink = std::move(writeSink);
        found->closeSink = std::move(closeSink);
        found->initializeSink = std::move(initializeSink);
        found->gracefulCloseSink = std::move(gracefulCloseSink);
        found->readArmSink = std::move(readArmSink);
        found->bound = true;
    } else {
        registry.entries.push_back(
            {socket, std::move(writeSink), std::move(closeSink), std::move(initializeSink), std::move(gracefulCloseSink),
                std::move(readArmSink), 0, true}
        );
    }
    return true;
}

bool InitializeSocketTransport(const void* socket) {
    if (!socket) {
        SetNotSocketError();
        return false;
    }

    SocketInitializeSink initializeSink;
    bool bound = false;
    {
        SocketTransportRegistry& registry = Registry();
        std::lock_guard<std::mutex> guard(registry.mutex);
        const auto found = std::find_if(
            registry.entries.begin(), registry.entries.end(),
            [socket](const SocketTransportRegistry::Entry& entry) { return entry.socket == socket; }
        );
        if (found != registry.entries.end() && found->bound) {
            initializeSink = found->initializeSink;
            bound = true;
        }
    }
    if (!bound) {
        SetNotSocketError();
        return false;
    }
    if (initializeSink) {
        initializeSink();
    }
    return true;
}

void UnbindSocketTransport(const void* socket) {
    if (!socket) {
        return;
    }

    SocketTransportRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    const auto found = std::find_if(
        registry.entries.begin(), registry.entries.end(),
        [socket](const SocketTransportRegistry::Entry& entry) { return entry.socket == socket; }
    );
    if (found == registry.entries.end()) {
        return;
    }
    found->writeSink = {};
    found->closeSink = {};
    found->initializeSink = {};
    found->gracefulCloseSink = {};
    found->readArmSink = {};
    found->bound = false;
    if (found->pendingReads == 0) {
        registry.entries.erase(found);
    }
}

bool ArmSocketReadTransport(const void* socket, std::size_t capacity) {
    if (!socket) {
        SetNotSocketError();
        return false;
    }

    SocketReadArmSink readArmSink;
    {
        SocketTransportRegistry& registry = Registry();
        std::lock_guard<std::mutex> guard(registry.mutex);
        const auto found = std::find_if(
            registry.entries.begin(), registry.entries.end(),
            [socket](const SocketTransportRegistry::Entry& entry) { return entry.socket == socket; }
        );
        if (found == registry.entries.end() || !found->bound) {
            SetNotSocketError();
            return false;
        }
        ++found->pendingReads;
        readArmSink = found->readArmSink;
    }

    if (readArmSink && !readArmSink(capacity)) {
        ConsumeSocketReadTransport(socket);
        return false;
    }
    return true;
}

bool ConsumeSocketReadTransport(const void* socket) {
    if (!socket) {
        return false;
    }

    SocketTransportRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    const auto found = std::find_if(
        registry.entries.begin(), registry.entries.end(),
        [socket](const SocketTransportRegistry::Entry& entry) { return entry.socket == socket; }
    );
    if (found == registry.entries.end() || found->pendingReads == 0) {
        return false;
    }
    --found->pendingReads;
    if (found->pendingReads == 0 && !found->bound) {
        registry.entries.erase(found);
    }
    return true;
}

std::size_t CancelSocketReadTransport(const void* socket) {
    if (!socket) {
        return 0;
    }

    SocketTransportRegistry& registry = Registry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    const auto found = std::find_if(
        registry.entries.begin(), registry.entries.end(),
        [socket](const SocketTransportRegistry::Entry& entry) { return entry.socket == socket; }
    );
    if (found == registry.entries.end()) {
        return 0;
    }
    const std::size_t pending = found->pendingReads;
    found->pendingReads = 0;
    if (!found->bound) {
        registry.entries.erase(found);
    }
    return pending;
}

void CloseSocketTransport(const void* socket) {
    if (!socket) {
        return;
    }

    SocketCloseSink closeSink;
    {
        SocketTransportRegistry& registry = Registry();
        std::lock_guard<std::mutex> guard(registry.mutex);
        const auto found = std::find_if(
            registry.entries.begin(), registry.entries.end(),
            [socket](const SocketTransportRegistry::Entry& entry) { return entry.socket == socket; }
        );
        if (found != registry.entries.end()) {
            closeSink = std::move(found->closeSink);
            found->writeSink = {};
            found->initializeSink = {};
            found->gracefulCloseSink = {};
            found->readArmSink = {};
            found->bound = false;
            if (found->pendingReads == 0) {
                registry.entries.erase(found);
            }
        }
    }
    if (closeSink) {
        closeSink();
    }
}

void GracefulCloseSocketTransport(const void* socket) {
    if (!socket) {
        return;
    }

    SocketGracefulCloseSink gracefulCloseSink;
    SocketCloseSink fallbackCloseSink;
    {
        SocketTransportRegistry& registry = Registry();
        std::lock_guard<std::mutex> guard(registry.mutex);
        const auto found = std::find_if(
            registry.entries.begin(), registry.entries.end(),
            [socket](const SocketTransportRegistry::Entry& entry) { return entry.socket == socket; }
        );
        if (found != registry.entries.end()) {
            gracefulCloseSink = std::move(found->gracefulCloseSink);
            if (!gracefulCloseSink) {
                fallbackCloseSink = std::move(found->closeSink);
            }
            found->writeSink = {};
            found->closeSink = {};
            found->initializeSink = {};
            found->readArmSink = {};
            found->bound = false;
            if (found->pendingReads == 0) {
                registry.entries.erase(found);
            }
        }
    }
    if (gracefulCloseSink) {
        gracefulCloseSink();
    } else if (fallbackCloseSink) {
        fallbackCloseSink();
    }
}

SocketWriteSubmission WriteSocketTransport(
    const void* socket, const std::uint8_t* data, int length, SocketWriteCompletion completion
) {
    if (!socket || !data || length <= 0) {
        return SocketWriteSubmission::Failed();
    }

    SocketWriteSink writeSink;
    {
        SocketTransportRegistry& registry = Registry();
        std::lock_guard<std::mutex> guard(registry.mutex);
        const auto found = std::find_if(
            registry.entries.begin(), registry.entries.end(),
            [socket](const SocketTransportRegistry::Entry& entry) { return entry.socket == socket; }
        );
        if (found != registry.entries.end()) {
            writeSink = found->writeSink;
        }
    }
    if (!writeSink) {
        // The target still calls WSASend with INVALID_SOCKET after a
        // concurrent Close and observes WSAENOTSOCK immediately.
        SetNotSocketError();
        return SocketWriteSubmission::Failed();
    }
    return writeSink(data, length, std::move(completion));
}

} // namespace portable
