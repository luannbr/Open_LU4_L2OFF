#pragma once

#include "Shared/src/StandardContainers.h"

#include <vector>

class CNickColorManager {
public:
    using NickColorItemList = std::vector<int, std::allocator<int>>;

    CNickColorManager();
    ~CNickColorManager();

    void AddNickColorItem(int itemClassId);
    bool IsNickColorItem(int itemClassId);

private:
    NickColorItemList m_vNickColorItemList;
};

extern CNickColorManager g_NickColorManager;
