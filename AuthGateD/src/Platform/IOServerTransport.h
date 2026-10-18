#pragma once

#include "Shared/src/Runtime/WindowsTypes.h"

#include <cstdint>

class CIOServer;

namespace authgated::platform {

enum class ListenerOpenStage {
    None,
    Socket,
    Bind,
    Listen,
    CompletionPort,
};

struct ListenerOpenFailure {
    ListenerOpenStage stage = ListenerOpenStage::None;
    int error = 0;
};

bool OpenNativeListener(
    CIOServer* owner,
    int port,
    std::uint32_t& targetSocket,
    ListenerOpenFailure& failure) noexcept;
void CloseNativeListener(
    CIOServer* owner,
    std::uint32_t targetSocket) noexcept;

struct AcceptedSocketInfo {
    std::uint64_t nativeSocket =
        UINT64_C(0xffffffffffffffff);
    std::uint32_t targetSocket =
        UINT32_C(0xffffffff);
    sockaddr_in remoteAddress{};
};

bool GetAcceptedSocketInfo(
    _OVERLAPPED* overlapped,
    AcceptedSocketInfo& info) noexcept;

// Releases the host accept carrier. closeSocket is true until ownership of
// the accepted descriptor has moved into CIOSocket's transport.
void ReleaseAcceptedSocket(
    _OVERLAPPED* overlapped,
    bool closeSocket) noexcept;

} // namespace authgated::platform
