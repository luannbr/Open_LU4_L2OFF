#include "Platform/ServiceThreadRuntime.h"

#include "Shared/src/Runtime/OwnedThread.h"

namespace authgated::platform {

void* CreateSuspendedServiceThread(ServiceThreadProc procedure, void* parameter) noexcept {
    return runtime::CreateOwnedThread(procedure, parameter, true);
}

bool ResumeServiceThread(void* handle) noexcept { return runtime::ResumeOwnedThread(handle); }

void JoinAndDestroyServiceThread(void* handle) noexcept { runtime::DestroyOwnedThread(handle); }

std::uint32_t CurrentServiceThreadId() noexcept { return runtime::CurrentNativeThreadId(); }

} // namespace authgated::platform
