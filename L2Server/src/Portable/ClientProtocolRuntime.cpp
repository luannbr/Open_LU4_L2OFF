#include "Portable/ClientProtocolRuntime.h"

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace runtime::client_protocol {

namespace {

struct RuntimeState final {
    std::mutex lock;
    std::unordered_map<const CUserSocket*, std::int32_t> revisions;
    std::atomic<int> lu4Sockets{0};
};

RuntimeState& State() {
    // CUserSocket globals can be destroyed after function-local statics.
    // Keep this process-lifetime compatibility state available until exit.
    static RuntimeState* const state = new RuntimeState;
    return *state;
}

} // namespace

void RecordNegotiatedRevision(const CUserSocket* socket, const std::int32_t revision) {
    if (socket == nullptr) {
        return;
    }

    RuntimeState& state = State();
    const std::lock_guard<std::mutex> guard(state.lock);
    const auto existing = state.revisions.find(socket);
    const bool wasLu4 = existing != state.revisions.end() && existing->second == kLu4Revision;
    if (wasLu4) {
        return;
    }
    if (revision == kCurrentRevision) {
        state.revisions.erase(socket);
    } else {
        state.revisions[socket] = revision;
        if (revision == kLu4Revision) {
            state.lu4Sockets.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

std::int32_t NegotiatedRevision(const CUserSocket* socket) noexcept {
    if (socket == nullptr) {
        return kCurrentRevision;
    }

    RuntimeState& state = State();
    const std::lock_guard<std::mutex> guard(state.lock);
    const auto iterator = state.revisions.find(socket);
    return iterator != state.revisions.end() ? iterator->second : kCurrentRevision;
}

bool HasNorthAmericanClients() noexcept {
    RuntimeState& state = State();
    const std::lock_guard<std::mutex> guard(state.lock);
    for (const auto& [socket, revision] : state.revisions) {
        if (socket != nullptr && revision == kNorthAmericanRevision) {
            return true;
        }
    }
    return false;
}

void ForgetSocket(const CUserSocket* socket) noexcept {
    if (socket == nullptr) {
        return;
    }

    RuntimeState& state = State();
    const std::lock_guard<std::mutex> guard(state.lock);
    const auto existing = state.revisions.find(socket);
    if (existing != state.revisions.end()) {
        if (existing->second == kLu4Revision) {
            state.lu4Sockets.fetch_sub(1, std::memory_order_relaxed);
        }
        state.revisions.erase(existing);
    }
}

bool HasLu4Clients() noexcept {
    return State().lu4Sockets.load(std::memory_order_relaxed) > 0;
}

} // namespace runtime::client_protocol
