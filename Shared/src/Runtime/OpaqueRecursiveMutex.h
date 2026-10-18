#pragma once

#include <cstring>
#include <mutex>
#include <type_traits>

namespace runtime::opaque_recursive_mutex {

// Product classes keep their PDB-recorded critical-section storage. This
// helper shares only the non-Windows recursive-mutex backend and therefore
// works for both the Win32 0x18-byte AuthD carrier and Win64 0x28-byte Shared
// carrier without manufacturing a common product type.
template <typename Storage>
std::recursive_mutex* Load(const Storage& storage) noexcept {
    static_assert(std::is_trivially_copyable_v<Storage>);
    static_assert(sizeof(std::recursive_mutex*) <= sizeof(Storage));

    std::recursive_mutex* backend = nullptr;
    std::memcpy(&backend, &storage, sizeof(backend));
    return backend;
}

template <typename Storage>
void Store(Storage& storage, std::recursive_mutex* backend) noexcept {
    static_assert(std::is_trivially_copyable_v<Storage>);
    static_assert(sizeof(backend) <= sizeof(Storage));

    std::memset(&storage, 0, sizeof(storage));
    std::memcpy(&storage, &backend, sizeof(backend));
}

template <typename Storage>
void Initialize(Storage& storage) {
    Store(storage, new std::recursive_mutex);
}

template <typename Storage>
void Delete(Storage& storage) noexcept {
    delete Load(storage);
    Store(storage, nullptr);
}

template <typename Storage>
void Enter(Storage& storage) {
    Load(storage)->lock();
}

template <typename Storage>
void Leave(Storage& storage) noexcept {
    Load(storage)->unlock();
}

} // namespace runtime::opaque_recursive_mutex
