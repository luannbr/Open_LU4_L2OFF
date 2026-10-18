#pragma once

#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CConnectThreadManager {
private:
    std::uint32_t m_nConnectThread; // +0x00
    [[maybe_unused]] std::uint32_t m_alignment04To08;
    void* m_hConnectThread; // +0x08

    CConnectThreadManager();
    ~CConnectThreadManager() = default;

public:
    // The matching-source donor kept a dummy lock here, but the pinned
    // current image allocates 0x20 bytes and constructs the exact 0x10-byte
    // CYieldLock state at +0x10. Current-machine layout wins that conflict.
    CYieldLock m_lock; // +0x10

    static CConnectThreadManager* m_Inst;

    static CConnectThreadManager& Inst();
    void CreateThread();
};
