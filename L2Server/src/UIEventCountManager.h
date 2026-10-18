#pragma once

#include "Shared/src/YieldLock.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

class UIEventCountManager {
public:
    static UIEventCountManager* Inst();

    void SaveUIEventCountToDB();
    void PushReservedID(int id);

private:
    UIEventCountManager();

    static UIEventCountManager* m_UIEventCountManager;

    [[maybe_unused]] std::array<std::int32_t, 20> m_reservedIds; // +0x00
    std::array<std::atomic<std::int32_t>, 20> m_nUIEventCountArr; // +0x50
    [[maybe_unused]] std::int32_t m_nCount; // +0xa0
    CYieldLock m_lock; // +0xa4
};
