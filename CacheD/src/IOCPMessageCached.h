#pragma once

#include "Shared/src/IOCPMessage.h"

#include <atomic>
#include <cstdint>

class CPledge;
class CUser;

// Current data globals 0x007e1d38/0x007e1d3c use -1 as the not-started
// sentinel. Their original identifiers are absent from the surviving PDB.
extern std::atomic<std::int32_t> g_nPendingCharacterItemChecks;
extern std::atomic<std::int32_t> g_nPendingPledgeItemChecks;
extern std::atomic<std::int32_t> g_nPendingLogoutCharacters;

// Shipping owner: CacheD/IOCPMessage_cached.cpp. Each message is the exact
// 0x18-byte Shared CIOCPMessage followed by one raw object pointer.
class CItemCheckOnCharLoadedMessage final : public CIOCPMessage {
public:
    explicit CItemCheckOnCharLoadedMessage(CUser* user) noexcept;
    void OnReceive(int argument) override;

private:
    CUser* m_user;
};

class CItemCheckOnPledgeLoadedMessage final : public CIOCPMessage {
public:
    explicit CItemCheckOnPledgeLoadedMessage(CPledge* pledge) noexcept;
    void OnReceive(int argument) override;

private:
    CPledge* m_pledge;
};

class CLogOutCharMessage final : public CIOCPMessage {
public:
    explicit CLogOutCharMessage(CUser* user) noexcept;
    void OnReceive(int argument) override;

private:
    CUser* m_user;
};
