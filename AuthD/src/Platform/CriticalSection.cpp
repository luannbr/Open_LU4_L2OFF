#include "Platform/CriticalSection.h"

#include "Shared/src/Runtime/OpaqueRecursiveMutex.h"

namespace authd::platform {

void InitializeCriticalSection(
    CriticalSectionStorage32& storage) {
    runtime::opaque_recursive_mutex::Initialize(storage);
}

bool InitializeCriticalSectionAndSpinCount(
    CriticalSectionStorage32& storage,
    [[maybe_unused]] std::uint32_t spinCount) {
    // Win32 uses spinCount only before falling back to the same recursive
    // critical-section wait. std::recursive_mutex supplies that blocking
    // portion on every supported host.
    InitializeCriticalSection(storage);
    return true;
}

void DeleteCriticalSection(
    CriticalSectionStorage32& storage) noexcept {
    runtime::opaque_recursive_mutex::Delete(storage);
}

void EnterCriticalSection(
    CriticalSectionStorage32& storage) {
    runtime::opaque_recursive_mutex::Enter(storage);
}

void LeaveCriticalSection(
    CriticalSectionStorage32& storage) noexcept {
    runtime::opaque_recursive_mutex::Leave(storage);
}

} // namespace authd::platform
