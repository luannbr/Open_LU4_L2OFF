#include "Shared/src/Runtime/SystemMailTransport.h"

#include "Shared/src/Runtime/SocketTransport.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace runtime {
namespace {

std::atomic<bool> g_systemMailNetworkEnabled{false};

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;

NativeSocket LoadSocket(std::intptr_t value) { return static_cast<NativeSocket>(static_cast<std::uintptr_t>(value)); }

std::intptr_t StoreSocket(NativeSocket value) { return static_cast<std::intptr_t>(static_cast<std::uintptr_t>(value)); }

void CloseNativeSocket(NativeSocket socket) {
    if (socket != kInvalidSocket) {
        ::closesocket(socket);
    }
}

int NativeLastError() {
    // Current CacheD.exe 0x0059a482 calls KERNEL32 GetLastError here, not
    // WSAGetLastError. The current L2NPC body has the same import choice.
    return static_cast<int>(::GetLastError());
}
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;

NativeSocket LoadSocket(std::intptr_t value) { return static_cast<NativeSocket>(value); }

std::intptr_t StoreSocket(NativeSocket value) { return static_cast<std::intptr_t>(value); }

void CloseNativeSocket(NativeSocket socket) {
    if (socket != kInvalidSocket) {
        ::close(socket);
    }
}

int NativeLastError() { return errno; }
#endif

bool IsOpen(std::intptr_t value) { return value != -1; }

using SmtpSocketPtr = std::shared_ptr<SmtpSocket>;

struct SystemMailSocketRegistry {
    std::mutex mutex;
    std::unordered_map<const void*, SmtpSocketPtr> sockets;
};

SystemMailSocketRegistry& MailSocketRegistry() {
    // Recovered mail singletons can be destroyed during global teardown.
    // Preserve the target's process-lifetime socket infrastructure and avoid
    // a cross-translation-unit static destruction dependency.
    static auto* const registry = new SystemMailSocketRegistry;
    return *registry;
}

thread_local int g_lastSystemMailSocketError = 0;

SmtpSocketPtr FindSystemMailSocket(const void* owner) {
    SystemMailSocketRegistry& registry = MailSocketRegistry();
    std::lock_guard<std::mutex> guard(registry.mutex);
    const auto found = registry.sockets.find(owner);
    return found == registry.sockets.end() ? SmtpSocketPtr{} : found->second;
}

} // namespace

void ConfigureSystemMailNetwork(bool enabled) { g_systemMailNetworkEnabled.store(enabled, std::memory_order_release); }

bool SystemMailNetworkEnabled() { return g_systemMailNetworkEnabled.load(std::memory_order_acquire); }

SmtpSocket::~SmtpSocket() { Close(); }

int SmtpSocket::Initialize() {
    if (initialized_) {
        return 0;
    }
    const int result = ::portable::InitializeSocketSubsystem();
    if (result == 0) {
        initialized_ = true;
    }
    return result;
}

bool SmtpSocket::Open() {
    if (!initialized_) {
        return false;
    }
    if (IsOpen(socket_)) {
        CloseNativeSocket(LoadSocket(socket_));
        socket_ = -1;
    }

    const NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket == kInvalidSocket) {
        return false;
    }

#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
    const int enabled = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
    socket_ = StoreSocket(socket);
    return true;
}

bool SmtpSocket::SetReceiveTimeout(int milliseconds) {
    if (!IsOpen(socket_)) {
        return false;
    }
#if defined(_WIN32)
    const DWORD timeout = static_cast<DWORD>(milliseconds);
    return ::setsockopt(
               LoadSocket(socket_), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)
           ) == 0;
#else
    const timeval timeout{milliseconds / 1000, (milliseconds % 1000) * 1000};
    return ::setsockopt(LoadSocket(socket_), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0;
#endif
}

bool SmtpSocket::SetSendTimeout(int milliseconds) {
    if (!IsOpen(socket_)) {
        return false;
    }
#if defined(_WIN32)
    const DWORD timeout = static_cast<DWORD>(milliseconds);
    return ::setsockopt(
               LoadSocket(socket_), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)
           ) == 0;
#else
    const timeval timeout{milliseconds / 1000, (milliseconds % 1000) * 1000};
    return ::setsockopt(LoadSocket(socket_), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
#endif
}

bool SmtpSocket::BindAny() {
    if (!IsOpen(socket_)) {
        return false;
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = 0;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    return ::bind(LoadSocket(socket_), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == 0;
}

SmtpConnectResult SmtpSocket::Connect(const char* host, std::uint16_t port) {
    if (!IsOpen(socket_) || !host) {
        return SmtpConnectResult::ConnectFailed;
    }

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(port);

    if (host[0] >= '0' && host[0] <= '9') {
        remote.sin_addr.s_addr = ::inet_addr(host);
    } else {
        hostent* const resolved = ::gethostbyname(host);
        if (!resolved || !resolved->h_addr_list || !resolved->h_addr_list[0]) {
            return SmtpConnectResult::ResolveFailed;
        }
        std::memcpy(&remote.sin_addr.s_addr, resolved->h_addr_list[0], static_cast<std::size_t>(resolved->h_length));
    }

    return ConnectIPv4(remote.sin_addr.s_addr, port);
}

SmtpConnectResult SmtpSocket::ConnectIPv4(std::uint32_t networkAddress, std::uint16_t port) {
    if (!IsOpen(socket_)) {
        return SmtpConnectResult::ConnectFailed;
    }

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(port);
    remote.sin_addr.s_addr = networkAddress;
    return ::connect(LoadSocket(socket_), reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) == 0
        ? SmtpConnectResult::Connected
        : SmtpConnectResult::ConnectFailed;
}

bool SmtpSocket::SetAbortiveClose() {
    if (!IsOpen(socket_)) {
        return false;
    }
    linger option{};
    option.l_onoff = 1;
    option.l_linger = 0;
#if defined(_WIN32)
    return ::setsockopt(
               LoadSocket(socket_), SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&option), sizeof(option)
           ) == 0;
#else
    return ::setsockopt(LoadSocket(socket_), SOL_SOCKET, SO_LINGER, &option, sizeof(option)) == 0;
#endif
}

int SmtpSocket::Send(const void* data, std::size_t length) {
    if (!IsOpen(socket_) || !data || length == 0) {
        return 0;
    }
    const int count = static_cast<int>(std::min<std::size_t>(length, static_cast<std::size_t>(INT_MAX)));
#if defined(_WIN32)
    return ::send(LoadSocket(socket_), static_cast<const char*>(data), count, 0);
#else
    int flags = 0;
#if defined(MSG_NOSIGNAL)
    flags = MSG_NOSIGNAL;
#endif
    return static_cast<int>(::send(LoadSocket(socket_), data, static_cast<std::size_t>(count), flags));
#endif
}

int SmtpSocket::Receive(void* data, std::size_t capacity) {
    if (!IsOpen(socket_) || !data || capacity == 0) {
        return 0;
    }
    const int count = static_cast<int>(std::min<std::size_t>(capacity, static_cast<std::size_t>(INT_MAX)));
#if defined(_WIN32)
    return ::recv(LoadSocket(socket_), static_cast<char*>(data), count, 0);
#else
    return static_cast<int>(::recv(LoadSocket(socket_), data, static_cast<std::size_t>(count), 0));
#endif
}

bool SmtpSocket::GetLocalIPv4(std::array<unsigned char, 4>& address) const {
    address.fill(0);
    if (!IsOpen(socket_)) {
        return false;
    }

    sockaddr_in local{};
#if defined(_WIN32)
    int length = sizeof(local);
#else
    socklen_t length = sizeof(local);
#endif
    if (::getsockname(LoadSocket(socket_), reinterpret_cast<sockaddr*>(&local), &length) != 0) {
        return false;
    }
    std::memcpy(address.data(), &local.sin_addr.s_addr, address.size());
    return true;
}

int SmtpSocket::LastError() const { return NativeLastError(); }

void SmtpSocket::Close() {
    if (IsOpen(socket_)) {
        CloseNativeSocket(LoadSocket(socket_));
        socket_ = -1;
    }
    if (initialized_) {
        ::portable::CleanupSocketSubsystem();
        initialized_ = false;
    }
}

bool GetHostComputerName(char* destination, std::size_t capacity) {
    if (!destination || capacity == 0) {
        return false;
    }
#if defined(_WIN32)
    DWORD length = static_cast<DWORD>(std::min<std::size_t>(capacity, static_cast<std::size_t>(MAXDWORD)));
    return ::GetComputerNameA(destination, &length) != FALSE;
#else
    if (::gethostname(destination, capacity) != 0) {
        return false;
    }
    return true;
#endif
}

bool CreateSystemMailSocket(const void* owner) noexcept {
    if (!owner || !SystemMailNetworkEnabled()) {
        return false;
    }
    try {
        auto socket = std::make_shared<SmtpSocket>();
        if (socket->Initialize() != 0 || !socket->Open()) {
            g_lastSystemMailSocketError = socket->LastError();
            return false;
        }
        SystemMailSocketRegistry& registry = MailSocketRegistry();
        {
            std::lock_guard<std::mutex> guard(registry.mutex);
            registry.sockets[owner] = std::move(socket);
        }
        g_lastSystemMailSocketError = 0;
        return true;
    } catch (...) {
        return false;
    }
}

bool SetSystemMailReceiveTimeout(const void* owner, int milliseconds) noexcept {
    const SmtpSocketPtr socket = FindSystemMailSocket(owner);
    if (!socket || !socket->SetReceiveTimeout(milliseconds)) {
        g_lastSystemMailSocketError = socket ? socket->LastError() : 0;
        return false;
    }
    return true;
}

bool SetSystemMailSendTimeout(const void* owner, int milliseconds) noexcept {
    const SmtpSocketPtr socket = FindSystemMailSocket(owner);
    if (!socket || !socket->SetSendTimeout(milliseconds)) {
        g_lastSystemMailSocketError = socket ? socket->LastError() : 0;
        return false;
    }
    return true;
}

bool ConnectSystemMailSocket(const void* owner, std::uint32_t networkAddress, std::uint16_t port) noexcept {
    const SmtpSocketPtr socket = FindSystemMailSocket(owner);
    if (!socket || socket->ConnectIPv4(networkAddress, port) != SmtpConnectResult::Connected) {
        g_lastSystemMailSocketError = socket ? socket->LastError() : 0;
        return false;
    }
    return true;
}

int SendSystemMailSocket(const void* owner, const void* data, std::size_t length) noexcept {
    const SmtpSocketPtr socket = FindSystemMailSocket(owner);
    if (!socket) {
        return 0;
    }
    const int result = socket->Send(data, length);
    if (result <= 0) {
        g_lastSystemMailSocketError = socket->LastError();
    }
    return result;
}

int ReceiveSystemMailSocket(const void* owner, void* data, std::size_t capacity) noexcept {
    const SmtpSocketPtr socket = FindSystemMailSocket(owner);
    if (!socket) {
        return 0;
    }
    const int result = socket->Receive(data, capacity);
    if (result <= 0) {
        g_lastSystemMailSocketError = socket->LastError();
    }
    return result;
}

std::uint32_t SystemMailSocketToken(const void* owner) noexcept {
    const SmtpSocketPtr socket = FindSystemMailSocket(owner);
    if (!socket) {
        return UINT32_C(0xffffffff);
    }
    // The concrete native handle remains private. The recovered field is
    // used only as an invalid/open discriminator after this host boundary.
    return 0;
}

int LastSystemMailSocketError() noexcept { return g_lastSystemMailSocketError; }

void CloseSystemMailSocket(const void* owner) noexcept {
    SmtpSocketPtr socket;
    {
        SystemMailSocketRegistry& registry = MailSocketRegistry();
        std::lock_guard<std::mutex> guard(registry.mutex);
        const auto found = registry.sockets.find(owner);
        if (found == registry.sockets.end()) {
            return;
        }
        socket = std::move(found->second);
        registry.sockets.erase(found);
    }
    socket->SetAbortiveClose();
    socket->Close();
}

} // namespace runtime
