#pragma once

#include "Shared/src/Runtime/WindowsTypes.h"

#include <cstdint>

class CIOSocket;

namespace authgated::platform {

inline constexpr std::uint32_t kInvalidSocket =
    UINT32_C(0xffffffff);

// Platform boundary for CIOSocket::Connect. On the selected Win32 ABI the
// returned value is the SOCKET itself. Other hosts keep the native handle
// in Shared's transport sidecar and return its target-width diagnostic token.
bool ConnectNativeSocket(
    CIOSocket* owner,
    in_addr address,
    int port,
    std::uint32_t& targetSocket) noexcept;

// Used by CIOServer for accepted descriptors before it calls the recovered
// CIOSocket::Initialize path.
bool BindNativeSocket(
    CIOSocket* owner,
    std::uint64_t nativeSocket,
    std::uint32_t& targetSocket) noexcept;

void AbortiveCloseSocket(
    CIOSocket* owner,
    std::uint32_t targetSocket) noexcept;

} // namespace authgated::platform
