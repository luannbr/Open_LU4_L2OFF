#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace runtime {

enum class SmtpConnectResult {
    Connected,
    ResolveFailed,
    ConnectFailed,
};

// External mail is a deployment capability, not part of the recovered
// target ABI. Keep it disabled until the process composition root has seen
// an explicit opt-in together with an explicit host and recipient list.
void ConfigureSystemMailNetwork(bool enabled);
bool SystemMailNetworkEnabled();

// Cross-platform owner of the native socket used by Shared/SystemMail.cpp.
// SMTP framing and the target's send/receive order remain in SystemMail.cpp.
// this class replaces only Winsock's platform operations.
class SmtpSocket {
public:
    SmtpSocket() = default;
    ~SmtpSocket();

    SmtpSocket(const SmtpSocket&) = delete;
    SmtpSocket& operator=(const SmtpSocket&) = delete;

    int Initialize();
    bool Open();
    bool SetReceiveTimeout(int milliseconds);
    bool SetSendTimeout(int milliseconds);
    bool BindAny();
    SmtpConnectResult Connect(const char* host, std::uint16_t port);
    SmtpConnectResult ConnectIPv4(std::uint32_t networkAddress, std::uint16_t port);
    bool SetAbortiveClose();

    int Send(const void* data, std::size_t length);
    int Receive(void* data, std::size_t capacity);
    bool GetLocalIPv4(std::array<unsigned char, 4>& address) const;
    int LastError() const;
    void Close();

private:
    std::intptr_t socket_ = -1;
    bool initialized_ = false;
};

bool GetHostComputerName(char* destination, std::size_t capacity);

// Process-wide sidecar used by recovered product classes whose PDB layouts
// contain only a target-width SOCKET field. The target class remains the
// owner. This registry owns the native-width portable socket.
bool CreateSystemMailSocket(const void* owner) noexcept;
bool SetSystemMailReceiveTimeout(const void* owner, int milliseconds) noexcept;
bool SetSystemMailSendTimeout(const void* owner, int milliseconds) noexcept;
bool ConnectSystemMailSocket(const void* owner, std::uint32_t networkAddress, std::uint16_t port) noexcept;
int SendSystemMailSocket(const void* owner, const void* data, std::size_t length) noexcept;
int ReceiveSystemMailSocket(const void* owner, void* data, std::size_t capacity) noexcept;
std::uint32_t SystemMailSocketToken(const void* owner) noexcept;
int LastSystemMailSocketError() noexcept;
void CloseSystemMailSocket(const void* owner) noexcept;

} // namespace runtime
