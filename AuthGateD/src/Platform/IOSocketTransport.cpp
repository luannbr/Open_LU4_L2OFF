#include "Platform/IOSocketTransport.h"

#include "IOSocket.h"

#include "Shared/src/Runtime/NativeSocketError.h"
#include "Shared/src/Runtime/NativeSocketTransport.h"
#include "Shared/src/Runtime/NativeTcpSocket.h"
#include "Shared/src/Runtime/SocketTransport.h"

#include <cstddef>
#include <cstdint>

namespace authgated::platform {

bool BindNativeSocket(CIOSocket* owner, std::uint64_t nativeSocket, std::uint32_t& targetSocket) noexcept {
    if (!owner || nativeSocket == runtime::kInvalidNativeSocket) {
        ::portable::SetNativeSocketLastError(10038);
        targetSocket = kInvalidSocket;
        return false;
    }

#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    targetSocket = static_cast<std::uint32_t>(nativeSocket);
    return true;
#else
    if (!::portable::BindNativeSocketTransport(
            owner, nativeSocket,
            [](void* socket, const std::uint8_t* data, std::size_t length) {
                return authgated::portable::FeedSocketRead(static_cast<CIOSocket*>(socket), data, length);
            },
            [](void* socket, int error) { authgated::portable::FeedSocketTerminal(static_cast<CIOSocket*>(socket), error); }
        )) {
        runtime::CloseTcpSocket(nativeSocket);
        targetSocket = kInvalidSocket;
        return false;
    }
    targetSocket = runtime::TargetSocketToken(nativeSocket, kInvalidSocket);
    return true;
#endif
}

bool ConnectNativeSocket(CIOSocket* owner, in_addr address, int port, std::uint32_t& targetSocket) noexcept {
    const runtime::NativeSocket nativeSocket = runtime::OpenTcpSocket();
    if (nativeSocket == runtime::kInvalidNativeSocket) {
        targetSocket = kInvalidSocket;
        return false;
    }
    if (!runtime::ConnectTcpSocket(nativeSocket, address, static_cast<std::uint16_t>(port))) {
        runtime::CloseTcpSocket(nativeSocket);
        targetSocket = kInvalidSocket;
        return false;
    }
    return BindNativeSocket(owner, nativeSocket, targetSocket);
}

void AbortiveCloseSocket(CIOSocket* owner, std::uint32_t targetSocket) noexcept {
    if (targetSocket == kInvalidSocket)
        return;

#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    runtime::AbortiveCloseTcpSocket(targetSocket);
#else
    ::portable::CloseSocketTransport(owner);
#endif
}

} // namespace authgated::platform
