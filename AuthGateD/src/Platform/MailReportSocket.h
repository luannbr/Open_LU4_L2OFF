#pragma once

#include <cstddef>
#include <cstdint>

class CMailReport;

namespace authgated::platform {

enum class MailHostLookupStatus {
    Resolved,
    InvalidAddress,
    HostNotFound,
    NoData,
    Failed,
};

struct MailHostLookupResult {
    MailHostLookupStatus status =
        MailHostLookupStatus::Failed;
    std::uint32_t networkAddress = 0;
    int nativeError = 0;
};

// AuthGateD.exe performs the numeric-address branch through inet_addr followed
// by gethostbyaddr, while alphabetic names use gethostbyname. Keep that
// product behavior here and keep native resolver types out of mailreport.cpp.
MailHostLookupResult ResolveMailServerIPv4(
    const char* server) noexcept;
bool ResolveLocalMailAddress(
    char* canonicalHostname,
    std::size_t hostnameCapacity,
    std::uint32_t& networkAddress) noexcept;

bool CreateMailSocket(CMailReport* owner) noexcept;
bool SetMailReceiveTimeout(
    CMailReport* owner,
    int milliseconds) noexcept;
bool SetMailSendTimeout(
    CMailReport* owner,
    int milliseconds) noexcept;
bool ConnectMailSocket(
    CMailReport* owner,
    std::uint32_t networkAddress,
    std::uint16_t port) noexcept;
int SendMailSocket(
    CMailReport* owner,
    const void* data,
    std::size_t length) noexcept;
int ReceiveMailSocket(
    CMailReport* owner,
    void* data,
    std::size_t capacity) noexcept;
int LastMailSocketError() noexcept;
void CloseMailSocket(CMailReport* owner) noexcept;

} // namespace authgated::platform
