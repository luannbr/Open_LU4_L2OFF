#include "CriticalSectionStorage.h"

#include "Shared/src/Runtime/OpaqueRecursiveMutex.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace l2portable_detail {

namespace {

#if !defined(_WIN32)
void InitializePortableCriticalSection(CriticalSectionStorage& storage, std::uint32_t spinCount) {
    runtime::opaque_recursive_mutex::Initialize(storage);
    storage.LockCount = -1;
    storage.SpinCount = spinCount;
}
#endif

} // namespace

void InitializeCriticalSection(CriticalSectionStorage& storage) {
#if defined(_WIN32)
    static_assert(sizeof(CRITICAL_SECTION) == sizeof(storage));
    ::InitializeCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(&storage));
#else
    InitializePortableCriticalSection(storage, 0);
#endif
}

bool InitializeCriticalSectionAndSpinCount(CriticalSectionStorage& storage, std::uint32_t spinCount) {
#if defined(_WIN32)
    static_assert(sizeof(CRITICAL_SECTION) == sizeof(storage));
    return ::InitializeCriticalSectionAndSpinCount(reinterpret_cast<LPCRITICAL_SECTION>(&storage), spinCount) != FALSE;
#else
    InitializePortableCriticalSection(storage, spinCount);
    return true;
#endif
}

void DeleteCriticalSection(CriticalSectionStorage& storage) noexcept {
#if defined(_WIN32)
    ::DeleteCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(&storage));
#else
    runtime::opaque_recursive_mutex::Delete(storage);
#endif
}

void EnterCriticalSection(CriticalSectionStorage& storage) {
#if defined(_WIN32)
    ::EnterCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(&storage));
#else
    runtime::opaque_recursive_mutex::Enter(storage);
#endif
}

void LeaveCriticalSection(CriticalSectionStorage& storage) noexcept {
#if defined(_WIN32)
    ::LeaveCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(&storage));
#else
    runtime::opaque_recursive_mutex::Leave(storage);
#endif
}

} // namespace l2portable_detail
