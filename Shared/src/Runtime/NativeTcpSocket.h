#pragma once

#include "Shared/src/Runtime/WindowsTypes.h"

#include <cstdint>

namespace runtime {

using NativeSocket = std::uint64_t;
inline constexpr NativeSocket kInvalidNativeSocket = UINT64_C(0xffffffffffffffff);

NativeSocket OpenTcpSocket() noexcept;
void CloseTcpSocket(NativeSocket socket) noexcept;
// The shipping error path logs the imported socket symbol's low 32 bits,
// instead of the socket returned by the failed call. Preserve that oddity.
int LegacySocketImportAddressToken() noexcept;
bool SetAbortiveLinger(NativeSocket socket) noexcept;
void AbortiveCloseTcpSocket(NativeSocket socket) noexcept;

bool ConnectTcpSocket(NativeSocket socket, in_addr address, std::uint16_t port) noexcept;
NativeSocket AcceptTcpSocket(NativeSocket listener, sockaddr_in& remoteAddress) noexcept;
bool BindTcpSocket(NativeSocket socket, in_addr address, std::uint16_t port) noexcept;
bool BindTcpListener(NativeSocket socket, std::uint16_t port) noexcept;
bool ListenTcpSocket(NativeSocket socket, int backlog) noexcept;

// The selected auth products keep 32-bit SOCKET fields. Portable hosts keep
// the real descriptor in a sidecar and expose this stable diagnostic token in
// the recovered field. Avoid manufacturing the target's invalid sentinel.
std::uint32_t TargetSocketToken(NativeSocket socket, std::uint32_t invalidToken = UINT32_C(0xffffffff)) noexcept;

} // namespace runtime
