#pragma once

#include <cstdint>

namespace runtime {

using OwnedThreadProcedure = std::uint32_t (*)(void* parameter);

// Opaque host owner for the _beginthreadex-style handles used by the auth
// products. Product descriptors and callback signatures remain unchanged.
void* CreateOwnedThread(OwnedThreadProcedure procedure, void* parameter, bool suspended) noexcept;
bool ResumeOwnedThread(void* handle) noexcept;
void JoinOwnedThread(void* handle) noexcept;
void DestroyOwnedThread(void* handle) noexcept;
std::uint32_t OwnedThreadId(void* handle) noexcept;
std::uint32_t CurrentNativeThreadId() noexcept;

} // namespace runtime
