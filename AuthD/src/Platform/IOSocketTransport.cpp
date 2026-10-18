#include "Platform/IOSocketTransport.h"

#include "IOSocket.h"

#include "Shared/src/Runtime/NativeSocketTransport.h"
#include "Shared/src/Runtime/NativeTcpSocket.h"

#include <cstddef>
#include <cstdint>

namespace authd::platform {

void CloseNativeSocket(std::uint32_t socket) noexcept { runtime::CloseTcpSocket(socket); }

void AbortiveCloseNativeSocket(std::uint32_t socket) noexcept {
    if (socket == UINT32_C(0xffffffff)) {
        return;
    }

    runtime::AbortiveCloseTcpSocket(socket);
}

std::int32_t AcceptNativeSocket(std::uint32_t listener, sockaddr_in& address) noexcept {
    const runtime::NativeSocket accepted = runtime::AcceptTcpSocket(listener, address);
    return accepted == runtime::kInvalidNativeSocket ? -1 : static_cast<std::int32_t>(accepted);
}

bool ConnectNativeSocket(std::uint32_t& socket, in_addr address, std::uint16_t port) noexcept {
    const runtime::NativeSocket created = runtime::OpenTcpSocket();
    socket = static_cast<std::uint32_t>(created);
    if (created == runtime::kInvalidNativeSocket) {
        return false;
    }
    return runtime::ConnectTcpSocket(created, address, port);
}

void InitializeNativeSocket(CIOSocket* socket, std::uint32_t nativeSocket, void* completionPort) {
#if defined(_WIN32) && INTPTR_MAX == INT32_MAX
    socket->Initialize(completionPort);
#else
    const bool bound = ::portable::BindNativeSocketTransport(
        socket, nativeSocket,
        [](void* owner, const std::uint8_t* data, std::size_t length) {
            return authd::portable::FeedSocketRead(static_cast<CIOSocket*>(owner), data, length);
        },
        [](void* owner, int error) { authd::portable::FeedSocketTerminal(static_cast<CIOSocket*>(owner), error); }
    );
    if (!bound) {
        // Initialize owns the target's completion-port failure log and
        // CloseSocket/refcount path. The unassociated native descriptor is
        // still ours until the portable transport accepts it.
        CloseNativeSocket(nativeSocket);
    }
    socket->Initialize(completionPort);
#endif
}

} // namespace authd::platform
