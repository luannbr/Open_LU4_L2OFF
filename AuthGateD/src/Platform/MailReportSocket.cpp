#include "Platform/MailReportSocket.h"

#include "Shared/src/Runtime/SystemMailTransport.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#endif

namespace authgated::platform {
namespace {

int HostLookupError() noexcept {
#if defined(_WIN32)
    return ::WSAGetLastError();
#else
    if (h_errno == HOST_NOT_FOUND)
        return 11001;
#if defined(NO_DATA)
    if (h_errno == NO_DATA)
        return 11004;
#elif defined(NO_ADDRESS)
    if (h_errno == NO_ADDRESS)
        return 11004;
#endif
    return h_errno;
#endif
}

MailHostLookupStatus HostLookupStatus(int error) noexcept {
    if (error == 11001) {
        return MailHostLookupStatus::HostNotFound;
    }
    if (error == 11004) {
        return MailHostLookupStatus::NoData;
    }
    return MailHostLookupStatus::Failed;
}

bool CopyHostEntry(
    const hostent* entry, std::uint32_t& networkAddress, char* canonicalHostname, std::size_t hostnameCapacity
) noexcept {
    if (!entry || !entry->h_addr_list || !entry->h_addr_list[0] || entry->h_length < 4) {
        return false;
    }
    std::memcpy(&networkAddress, entry->h_addr_list[0], sizeof(networkAddress));
    if (canonicalHostname && hostnameCapacity != 0) {
        canonicalHostname[0] = '\0';
        if (entry->h_name) {
            const std::size_t length = std::min(std::strlen(entry->h_name), hostnameCapacity - 1U);
            std::memcpy(canonicalHostname, entry->h_name, length);
            canonicalHostname[length] = '\0';
        }
    }
    return true;
}

} // namespace

MailHostLookupResult ResolveMailServerIPv4(const char* server) noexcept {
    MailHostLookupResult result;
    if (!server || server[0] == '\0')
        return result;

    hostent* entry = nullptr;
    if (std::isalpha(static_cast<unsigned char>(server[0])) != 0) {
        entry = ::gethostbyname(server);
    } else {
        const std::uint32_t address = ::inet_addr(server);
        if (address == INADDR_NONE) {
            result.status = MailHostLookupStatus::InvalidAddress;
            return result;
        }
        entry = ::gethostbyaddr(reinterpret_cast<const char*>(&address), sizeof(address), AF_INET);
    }

    if (!entry) {
        result.nativeError = HostLookupError();
        result.status = HostLookupStatus(result.nativeError);
        return result;
    }
    if (!CopyHostEntry(entry, result.networkAddress, nullptr, 0)) {
        result.nativeError = HostLookupError();
        result.status = MailHostLookupStatus::Failed;
        return result;
    }
    result.status = MailHostLookupStatus::Resolved;
    return result;
}

bool ResolveLocalMailAddress(char* canonicalHostname, std::size_t hostnameCapacity, std::uint32_t& networkAddress) noexcept {
    std::array<char, 24> localHostname{};
    if (!runtime::GetHostComputerName(localHostname.data(), localHostname.size())) {
        return false;
    }
    hostent* const entry = ::gethostbyname(localHostname.data());
    return CopyHostEntry(entry, networkAddress, canonicalHostname, hostnameCapacity);
}

bool CreateMailSocket(CMailReport* owner) noexcept { return runtime::CreateSystemMailSocket(owner); }

bool SetMailReceiveTimeout(CMailReport* owner, int milliseconds) noexcept {
    return runtime::SetSystemMailReceiveTimeout(owner, milliseconds);
}

bool SetMailSendTimeout(CMailReport* owner, int milliseconds) noexcept {
    return runtime::SetSystemMailSendTimeout(owner, milliseconds);
}

bool ConnectMailSocket(CMailReport* owner, std::uint32_t networkAddress, std::uint16_t port) noexcept {
    return runtime::ConnectSystemMailSocket(owner, networkAddress, port);
}

int SendMailSocket(CMailReport* owner, const void* data, std::size_t length) noexcept {
    return runtime::SendSystemMailSocket(owner, data, length);
}

int ReceiveMailSocket(CMailReport* owner, void* data, std::size_t capacity) noexcept {
    return runtime::ReceiveSystemMailSocket(owner, data, capacity);
}

int LastMailSocketError() noexcept { return runtime::LastSystemMailSocketError(); }

void CloseMailSocket(CMailReport* owner) noexcept { runtime::CloseSystemMailSocket(owner); }

} // namespace authgated::platform
