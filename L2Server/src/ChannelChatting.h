#pragma once

#include "Shared/src/RWLock.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CChannelChattingSocket;
class User;

class ChannelChattingClient {
public:
    ChannelChattingClient();

    static ChannelChattingClient& Inst();

    bool SendKeepAlive();
    bool SendLogOut(User* user);

private:
    static ChannelChattingClient* m_pChannelChattingClient;

    [[maybe_unused]] bool enabled; // +0x00
    [[maybe_unused]] bool sendable; // +0x01
    [[maybe_unused]] bool supressed; // +0x02
    // Current construction leaves both padding lanes indeterminate.
    [[maybe_unused]] std::byte m_alignment03To08[5]; // +0x03
    RWLock lock; // +0x08
    CYieldLock m_lock; // +0x40
    [[maybe_unused]] std::uint32_t m_nConnectThread; // +0x50
    [[maybe_unused]] std::uint32_t m_alignment54To58; // +0x54
    [[maybe_unused]] void* m_hConnectThread; // +0x58
    CChannelChattingSocket* m_pChannelChattingSocket; // +0x60
};
