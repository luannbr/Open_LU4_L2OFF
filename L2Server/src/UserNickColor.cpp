#include "UserNickColor.h"

#include "L2Core/src/inc/Guard.h"

CNickColorManager g_NickColorManager;

CNickColorManager::CNickColorManager() {
    L2_GUARD_FUNCTION();

    m_vNickColorItemList.clear();
}

CNickColorManager::~CNickColorManager() = default;

void CNickColorManager::AddNickColorItem(int itemClassId) {
    L2_GUARD_FUNCTION();

    m_vNickColorItemList.push_back(itemClassId);
}

bool CNickColorManager::IsNickColorItem(int itemClassId) {
    L2_GUARD_FUNCTION();

    for (const int candidate : m_vNickColorItemList) {
        if (candidate == itemClassId) {
            return true;
        }
    }
    return false;
}
