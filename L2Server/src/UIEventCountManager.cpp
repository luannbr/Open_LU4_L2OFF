#include "UIEventCountManager.h"

#include "L2Core/src/inc/Guard.h"

UIEventCountManager* UIEventCountManager::m_UIEventCountManager = nullptr;

UIEventCountManager::UIEventCountManager() : m_reservedIds{}, m_nUIEventCountArr{}, m_nCount(0) {
    for (auto& count : m_nUIEventCountArr) {
        count.store(0, std::memory_order_seq_cst);
    }
}

UIEventCountManager* UIEventCountManager::Inst() {
    L2_GUARD_FUNCTION();
    if (m_UIEventCountManager == nullptr) {
        m_UIEventCountManager = new UIEventCountManager;
    }
    return m_UIEventCountManager;
}

void UIEventCountManager::SaveUIEventCountToDB() {
    // Current L2Server 0x00afe934 is an empty guarded hook.
    L2_GUARD_FUNCTION();
}

void UIEventCountManager::PushReservedID(int) {
    // Current L2Server 0x00afe9b0 is an empty guarded hook.
    L2_GUARD_FUNCTION();
}
