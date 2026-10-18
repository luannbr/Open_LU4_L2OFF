#pragma once

#include <cstddef>
#include <cstdint>

namespace authd::platform {

// The authoritative AuthD is a 32-bit executable. Its PDB records Win32
// _RTL_CRITICAL_SECTION as six 32-bit words (24 bytes). The portable backend
// pointer is encoded inside those same 24 bytes, so CLock and CRWLock keep
// their recorded offsets even when the host process is 64-bit.
struct CriticalSectionStorage32 {
    std::uint32_t opaque[6];
};

void InitializeCriticalSection(
    CriticalSectionStorage32& storage);
bool InitializeCriticalSectionAndSpinCount(
    CriticalSectionStorage32& storage,
    std::uint32_t spinCount);
void DeleteCriticalSection(
    CriticalSectionStorage32& storage) noexcept;
void EnterCriticalSection(
    CriticalSectionStorage32& storage);
void LeaveCriticalSection(
    CriticalSectionStorage32& storage) noexcept;


} // namespace authd::platform
