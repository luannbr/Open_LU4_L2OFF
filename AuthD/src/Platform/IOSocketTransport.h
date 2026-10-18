#pragma once

#include "Platform/WindowsTypes.h"

#include <cstdint>

class CIOSocket;

namespace authd::platform {

// Native descriptor operations are the platform boundary behind the exact
// Src/ioserver.cpp and Src/ioserverex.cpp product owners. Keeping them here
// avoids manufacturing a second socket implementation in either owner.
void CloseNativeSocket(std::uint32_t socket) noexcept;

// Cross-platform boundary for the target's SO_LINGER {1, 0} followed by
// closesocket() reconnect-failure path.
void AbortiveCloseNativeSocket(std::uint32_t socket) noexcept;

std::int32_t AcceptNativeSocket(
    std::uint32_t listener,
    sockaddr_in& address) noexcept;

// Creates the same AF_INET/SOCK_STREAM descriptor as the target and reports
// connect() separately because target callers still construct/close their
// CIOSocket owner after a failed connect.
bool ConnectNativeSocket(
    std::uint32_t& socket,
    in_addr address,
    std::uint16_t port) noexcept;

void InitializeNativeSocket(
    CIOSocket* socket,
    std::uint32_t nativeSocket,
    void* completionPort);

} // namespace authd::platform
