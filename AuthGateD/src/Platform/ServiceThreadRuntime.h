#pragma once

#include <cstdint>

namespace authgated::platform {

using ServiceThreadProc = std::uint32_t (*)(void* parameter);

// Host representation of _beginthreadex(..., CREATE_SUSPENDED, ...). The
// opaque pointer occupies the original HANDLE slots in CAuthGateSvcMgr.
void* CreateSuspendedServiceThread(
    ServiceThreadProc procedure,
    void* parameter) noexcept;
bool ResumeServiceThread(void* handle) noexcept;
void JoinAndDestroyServiceThread(void* handle) noexcept;
std::uint32_t CurrentServiceThreadId() noexcept;

} // namespace authgated::platform
