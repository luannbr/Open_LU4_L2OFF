#include "Shared/src/Runtime/NativeTcpSocket.h"

#if defined(_WIN32)
#include <WinSock2.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace runtime {

NativeSocket OpenTcpSocket() noexcept {
#if defined(_WIN32)
    const SOCKET socket = ::socket(AF_INET, SOCK_STREAM, 0);
    return socket == INVALID_SOCKET ? kInvalidNativeSocket : static_cast<NativeSocket>(socket);
#else
    const int socket = ::socket(AF_INET, SOCK_STREAM, 0);
    return socket < 0 ? kInvalidNativeSocket : static_cast<NativeSocket>(socket);
#endif
}

void CloseTcpSocket(NativeSocket socket) noexcept {
    if (socket == kInvalidNativeSocket) {
        return;
    }
#if defined(_WIN32)
    ::closesocket(static_cast<SOCKET>(socket));
#else
    ::close(static_cast<int>(socket));
#endif
}

int LegacySocketImportAddressToken() noexcept { return static_cast<int>(reinterpret_cast<std::intptr_t>(&::socket)); }

bool SetAbortiveLinger(NativeSocket socket) noexcept {
    if (socket == kInvalidNativeSocket) {
        return false;
    }
    linger option{};
    option.l_onoff = 1;
    option.l_linger = 0;
#if defined(_WIN32)
    return ::setsockopt(
               static_cast<SOCKET>(socket), SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&option), sizeof(option)
           ) != SOCKET_ERROR;
#else
    return ::setsockopt(static_cast<int>(socket), SOL_SOCKET, SO_LINGER, &option, sizeof(option)) == 0;
#endif
}

void AbortiveCloseTcpSocket(NativeSocket socket) noexcept {
    if (socket == kInvalidNativeSocket) {
        return;
    }
    SetAbortiveLinger(socket);
    CloseTcpSocket(socket);
}

bool ConnectTcpSocket(NativeSocket socket, in_addr address, std::uint16_t port) noexcept {
    if (socket == kInvalidNativeSocket) {
        return false;
    }
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr = address;
    endpoint.sin_port = htons(port);
#if defined(_WIN32)
    return ::connect(static_cast<SOCKET>(socket), reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) !=
        SOCKET_ERROR;
#else
    return ::connect(static_cast<int>(socket), reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) == 0;
#endif
}

NativeSocket AcceptTcpSocket(NativeSocket listener, sockaddr_in& remoteAddress) noexcept {
    if (listener == kInvalidNativeSocket) {
        return kInvalidNativeSocket;
    }
#if defined(_WIN32)
    int addressLength = sizeof(remoteAddress);
    const SOCKET accepted =
        ::accept(static_cast<SOCKET>(listener), reinterpret_cast<sockaddr*>(&remoteAddress), &addressLength);
    return accepted == INVALID_SOCKET ? kInvalidNativeSocket : static_cast<NativeSocket>(accepted);
#else
    socklen_t addressLength = sizeof(remoteAddress);
    const int accepted = ::accept(static_cast<int>(listener), reinterpret_cast<sockaddr*>(&remoteAddress), &addressLength);
    return accepted < 0 ? kInvalidNativeSocket : static_cast<NativeSocket>(accepted);
#endif
}

bool BindTcpSocket(NativeSocket socket, in_addr address, std::uint16_t port) noexcept {
    if (socket == kInvalidNativeSocket) {
        return false;
    }
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr = address;
    endpoint.sin_port = htons(port);
#if defined(_WIN32)
    return ::bind(static_cast<SOCKET>(socket), reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) !=
        SOCKET_ERROR;
#else
    return ::bind(static_cast<int>(socket), reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) == 0;
#endif
}

bool BindTcpListener(NativeSocket socket, std::uint16_t port) noexcept {
    in_addr address{};
    address.s_addr = htonl(INADDR_ANY);
    return BindTcpSocket(socket, address, port);
}

bool ListenTcpSocket(NativeSocket socket, int backlog) noexcept {
    if (socket == kInvalidNativeSocket) {
        return false;
    }
#if defined(_WIN32)
    return ::listen(static_cast<SOCKET>(socket), backlog) != SOCKET_ERROR;
#else
    return ::listen(static_cast<int>(socket), backlog) == 0;
#endif
}

std::uint32_t TargetSocketToken(NativeSocket socket, std::uint32_t invalidToken) noexcept {
    const std::uint32_t token = static_cast<std::uint32_t>(socket);
    return token == invalidToken ? invalidToken - 1U : token;
}

} // namespace runtime
