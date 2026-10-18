#include "Lu4Validator.h"

#include "Account.h"
#include "Config.h"
#include "Log.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kNoSocket = INVALID_SOCKET;
inline void CloseHandleSocket(SocketHandle socket) { ::closesocket(socket); }
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kNoSocket = -1;
inline void CloseHandleSocket(SocketHandle socket) { ::close(socket); }
#endif

namespace lu4 {
namespace {

using Clock = std::chrono::steady_clock;

struct Pending {
    std::string account;
    std::string password;
    Clock::time_point expires;
};

constexpr auto kLifetime = std::chrono::minutes(5);
// This AuthD's login carrier holds 14 account and 16 password characters.
constexpr std::size_t kMaxAccount = 14;
constexpr std::size_t kMaxPassword = 16;

std::mutex g_mutex;
std::map<std::uint32_t, Pending> g_pending;

bool Validate(const std::string& account, const std::string& password) {
    if (account.empty() || password.empty() || account.size() > kMaxAccount || password.size() > kMaxPassword) {
        return false;
    }
    char name[kMaxAccount + 1]{};
    char secret[kMaxPassword + 1]{};
    std::memcpy(name, account.data(), account.size());
    std::memcpy(secret, password.data(), password.size());
    CAccount record;
    return record.CheckPassword(name, secret, config.newEncrypt, config.autoCreateAccounts) == 0;
}

void Handle(SocketHandle client, in_addr address) {
#if defined(_WIN32)
    DWORD timeout = 5000;
    ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#endif
    std::string line;
    char chunk[256];
    while (line.find('\n') == std::string::npos && line.size() < 512) {
        const int received = ::recv(client, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            break;
        }
        line.append(chunk, static_cast<std::size_t>(received));
    }
    line = line.substr(0, line.find('\n'));
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    bool accepted = false;
    std::string account;
    const std::size_t first = line.find('\t');
    const std::size_t second = first == std::string::npos ? first : line.find('\t', first + 1);
    if (line.compare(0, first, "AUTH") == 0 && second != std::string::npos) {
        account = line.substr(first + 1, second - first - 1);
        const std::string password = line.substr(second + 1);
        std::transform(account.begin(), account.end(), account.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        accepted = Validate(account, password);
        if (accepted) {
            const std::lock_guard guard(g_mutex);
            g_pending[address.s_addr] = Pending{account, password, Clock::now() + kLifetime};
        }
    }
    char addressText[INET_ADDRSTRLEN]{};
    ::inet_ntop(AF_INET, &address, addressText, sizeof(addressText));
    L2_LOG_INFO(Log, "[lu4] Credential check from %s for account '%s': %s.", addressText,
                account.empty() ? "?" : account.c_str(), accepted ? "accepted" : "rejected");
    const char* const reply = accepted ? "OK\n" : "FAIL\n";
    ::send(client, reply, static_cast<int>(std::strlen(reply)), 0);
    CloseHandleSocket(client);
}

void Listen(int port) {
    const SocketHandle listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(static_cast<std::uint16_t>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &local.sin_addr);
    if (listener == kNoSocket || ::bind(listener, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0 ||
        ::listen(listener, 16) != 0) {
        L2_LOG_ERROR(Log, "[lu4] Cannot listen on 127.0.0.1:%d for LU4 credential checks; LU4 logins will fail.", port);
        if (listener != kNoSocket) {
            CloseHandleSocket(listener);
        }
        return;
    }
    L2_LOG_INFO(Log, "[lu4] LU4 credential validator listening on 127.0.0.1:%d.", port);
    for (;;) {
        sockaddr_in peer{};
#if defined(_WIN32)
        int peerLength = sizeof(peer);
#else
        socklen_t peerLength = sizeof(peer);
#endif
        const SocketHandle client = ::accept(listener, reinterpret_cast<sockaddr*>(&peer), &peerLength);
        if (client == kNoSocket) {
            continue;
        }
        std::thread(Handle, client, peer.sin_addr).detach();
    }
}

} // namespace

void StartValidator(int port) { std::thread(Listen, port).detach(); }

bool TakeCredentials(in_addr address, std::string& account, std::string& password) {
    const std::lock_guard guard(g_mutex);
    const auto found = g_pending.find(address.s_addr);
    if (found == g_pending.end()) {
        return false;
    }
    const bool valid = found->second.expires > Clock::now();
    if (valid) {
        account = found->second.account;
        password = found->second.password;
    }
    g_pending.erase(found);
    return valid;
}

} // namespace lu4
