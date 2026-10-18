#pragma once

#include <algorithm>
#include <mutex>
#include <vector>

namespace runtime::completion_association {

namespace detail {

struct Entry {
    const void* owner = nullptr;
    void* completionPort = nullptr;
};

struct Registry {
    std::mutex mutex;
    std::vector<Entry> entries;
};

inline Registry& GetRegistry() {
    // IOCP associations have process lifetime in the Windows targets. Keep
    // the portable registry alive while late socket completions drain during
    // static teardown as well.
    static auto* const registry = new Registry;
    return *registry;
}

} // namespace detail

// Portable sidecar for CreateIoCompletionPort's (native handle, completion
// key, port) association. Recovered owners keep their PDB field layout.
// this is host transport metadata, not product state.
inline void Associate(const void* owner, void* completionPort) {
    if (!owner) {
        return;
    }

    detail::Registry& registry = detail::GetRegistry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    const auto found = std::find_if(registry.entries.begin(), registry.entries.end(), [owner](const detail::Entry& entry) {
        return entry.owner == owner;
    });
    if (found == registry.entries.end()) {
        registry.entries.push_back({owner, completionPort});
    } else {
        found->completionPort = completionPort;
    }
}

inline void Forget(const void* owner) {
    if (!owner) {
        return;
    }

    detail::Registry& registry = detail::GetRegistry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    const auto found = std::find_if(registry.entries.begin(), registry.entries.end(), [owner](const detail::Entry& entry) {
        return entry.owner == owner;
    });
    if (found != registry.entries.end()) {
        registry.entries.erase(found);
    }
}

inline void* Find(const void* owner) {
    if (!owner) {
        return nullptr;
    }

    detail::Registry& registry = detail::GetRegistry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    const auto found = std::find_if(registry.entries.begin(), registry.entries.end(), [owner](const detail::Entry& entry) {
        return entry.owner == owner;
    });
    return found == registry.entries.end() ? nullptr : found->completionPort;
}

} // namespace runtime::completion_association
