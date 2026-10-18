#include "DB.h"

#include "Item.h"
#include "User.h"


void CDB::ReqeustRecentProduct(User* const user, const int count) {
    // Current L2Server 0x004c7004, original owner br_db.cpp.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("chdd", 0xea, 0xd9, static_cast<int>(user->GetDBID()), count);
    }
}

void CDB::ReqeustInterestedProduct(User* const user) {
    // Current L2Server 0x004c7b40, original owner br_db.cpp.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("chd", 0xea, 0x152, static_cast<int>(user->GetDBID()));
    }
}

void CDB::ReqeustAddInterestedProduct(User* const user, const int productId) {
    // Current L2Server 0x004c7c08, original owner br_db.cpp.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("chdd", 0xea, 0x153, static_cast<int>(user->GetDBID()), productId);
    }
}

void CDB::ReqeustDeleteInterestedProduct(User* const user, const int productId) {
    // Current L2Server 0x004c7cd4, original owner br_db.cpp.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        user->SendDB("chdd", 0xea, 0x154, static_cast<int>(user->GetDBID()), productId);
    }
}

void CDB::RequestLoadItemAgathionEnergy(User* user, int itemServerId, int itemDatabaseId) {
    // Current L2Server 0x004c76c8.
    L2_GUARD_FUNCTION();
    if (user != nullptr) {
        const std::uint32_t userDatabaseId = user->GetDBID();
        user->SendDB("chddd", 0xea, 0xe6, userDatabaseId, itemServerId, itemDatabaseId);
    }
}

void CDB::RequestSaveItemAgathionEnergy(User* user, int itemServerId, int itemDatabaseId, double energy) {
    // Current L2Server 0x004c77a8.
    L2_GUARD_FUNCTION();
    user->SendDB("chddf", 0xea, 0xe7, itemServerId, itemDatabaseId, energy);
}

void CDB::SaveItemAgathionEnergy(User* user) {
    // Current L2Server 0x004c7888.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const int itemIndex = user->m_data->m_nSlotIndex[ST_AGATHION_MAIN];
    if (itemIndex == -1) {
        return;
    }

    CItem* const item = user->m_inv.GetByIndex(itemIndex);
    if (item == nullptr || item->m_const->m_dAgathionMaxEnergy <= 0.0) {
        return;
    }

    RequestSaveItemAgathionEnergy(user, item->m_data->m_nId, static_cast<int>(item->GetDBID()), item->GetAgathionEnergy());
}
