#pragma once

#include <cstddef>
#include <cstdint>

namespace l2portable_detail {

// Win64 _RTL_CRITICAL_SECTION layout recorded by both server binaries. On
// non-Windows hosts DebugInfo owns the portable recursive-mutex backend.
struct CriticalSectionStorage {
    void* DebugInfo;
    std::int32_t LockCount;
    std::int32_t RecursionCount;
    void* OwningThread;
    void* LockSemaphore;
    std::uint64_t SpinCount;
};

// Cross-platform implementations of the matching Win32 operations.
// Owners keep CriticalSectionStorage as their physical member so this
// portability boundary does not invent a replacement owner class or change
// the recovered object layout.
void InitializeCriticalSection(CriticalSectionStorage& storage);
bool InitializeCriticalSectionAndSpinCount(CriticalSectionStorage& storage, std::uint32_t spinCount);
void DeleteCriticalSection(CriticalSectionStorage& storage) noexcept;
void EnterCriticalSection(CriticalSectionStorage& storage);
void LeaveCriticalSection(CriticalSectionStorage& storage) noexcept;


} // namespace l2portable_detail
