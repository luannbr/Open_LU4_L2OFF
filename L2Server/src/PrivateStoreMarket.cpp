#include "PrivateStoreMarket.h"

#include "Shared/src/ThreadSafeLibrary.h"
#include "User.h"

#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/PacketHelper.h"

#include <algorithm>

CPrivateStoreMarket::CPrivateStoreMarket() {}

CPrivateStoreMarket::~CPrivateStoreMarket() = default;

void CPrivateStoreMarket::AssembleMerchant(User& user, IPacketHelper& packet) {
    // Current L2Server 0x009f1b80.
    L2_GUARD_FUNCTION();
    const FVector position = user.GetPos();
    packet.Assemble(
        "dSddd", static_cast<int>(user.m_nId), user.GetName(), runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z)
    );
}

bool CPrivateStoreMarket::_RemoveMerchant(const int merchantId) {
    // Current L2Server 0x009f1d44.
    L2_GUARD_FUNCTION();
    return m_Merchants.erase(merchantId) != 0;
}

bool CPrivateStoreMarket::_RemoveItem(const int itemClassId, const int merchantId) {
    // Current L2Server 0x009f1de4.
    L2_GUARD_FUNCTION();
    const auto found = m_ItemToMerchantMap.find(itemClassId);
    if (found == m_ItemToMerchantMap.end()) {
        return false;
    }
    found->second->erase(merchantId);
    return true;
}

void CPrivateStoreMarket::RemoveItems(const int merchantId, const bool wholesale, PrivateStoreMap& items) {
    // Current L2Server 0x009f1ed8. The second argument is kept by the
    // original ABI but does not select a different index in this owner.
    L2_GUARD_FUNCTION();
    static_cast<void>(wholesale);
    L2_GUARD(m_Lock);
    _RemoveMerchant(merchantId);
    for (const auto& entry : items) {
        if (entry.second != nullptr) {
            _RemoveItem(entry.second->nClassId, merchantId);
        }
    }
}

bool CPrivateStoreMarket::_InsertMerchant(const int merchantId) {
    // Current L2Server 0x009f2010.
    L2_GUARD_FUNCTION();
    return m_Merchants.insert(merchantId).second;
}

bool CPrivateStoreMarket::_InsertItem(const int itemClassId, const int merchantId) {
    // Current L2Server 0x009f20b8.
    L2_GUARD_FUNCTION();
    auto found = m_ItemToMerchantMap.find(itemClassId);
    IntSet* merchants = nullptr;
    if (found == m_ItemToMerchantMap.end()) {
        merchants = new IntSet;
        m_ItemToMerchantMap[itemClassId] = merchants;
    } else {
        merchants = found->second;
    }
    return merchants->insert(merchantId).second;
}

int CPrivateStoreMarket::QueryAllItems(const int maximumMerchants, IPacketHelper& packet) {
    // Current L2Server 0x009f2258.
    L2_GUARD_FUNCTION();

    IntSet merchantSnapshot;
    {
        L2_GUARD(m_Lock);
        const int count = std::min(static_cast<int>(m_Merchants.size()), std::min(maximumMerchants, 30));
        auto position = m_Merchants.begin();
        for (int index = 0; index < count && position != m_Merchants.end(); ++index, ++position) {
            merchantSnapshot.insert(*position);
        }
    }

    CPacketHelperEx<20480> merchantPacket;
    CPacketHelperEx<20480> combinedItemPacket;
    CPacketHelperEx<20480> itemPacket;
    int merchantCount = 0;
    int totalItemCount = 0;
    for (const int merchantId : merchantSnapshot) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(merchantId);
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user == nullptr) {
            continue;
        }

        ++merchantCount;
        AssembleMerchant(*user, merchantPacket);
        int itemCount = 0;
        user->GetPrivateStoreItems(itemPacket, itemCount, -1);
        combinedItemPacket.Assemble(
            "ddb", static_cast<int>(user->m_nId), itemCount, itemPacket.GetLength(), itemPacket.GetPacket()
        );
        itemPacket.Reset();
        totalItemCount += itemCount;
        if (totalItemCount > 100) {
            break;
        }
    }

    packet.Assemble(
        "dbdb", merchantCount, merchantPacket.GetLength(), merchantPacket.GetPacket(), totalItemCount,
        combinedItemPacket.GetLength(), combinedItemPacket.GetPacket()
    );
    return totalItemCount;
}

void CPrivateStoreMarket::QuerySpecificItems(const int itemClassId, IPacketHelper& packet) {
    // Current L2Server 0x009f261c.
    L2_GUARD_FUNCTION();

    IntSet merchantSnapshot;
    {
        L2_GUARD(m_Lock);
        const auto found = m_ItemToMerchantMap.find(itemClassId);
        if (found != m_ItemToMerchantMap.end()) {
            merchantSnapshot = *found->second;
        }
    }

    CPacketHelperEx<20480> merchantPacket;
    CPacketHelperEx<20480> combinedItemPacket;
    CPacketHelperEx<20480> itemPacket;
    int merchantCount = 0;
    int totalItemCount = 0;
    for (const int merchantId : merchantSnapshot) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(merchantId);
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user == nullptr) {
            continue;
        }

        ++merchantCount;
        AssembleMerchant(*user, merchantPacket);
        int itemCount = 0;
        user->GetPrivateStoreItems(itemPacket, itemCount, itemClassId);
        combinedItemPacket.Assemble(
            "ddb", static_cast<int>(user->m_nId), itemCount, itemPacket.GetLength(), itemPacket.GetPacket()
        );
        itemPacket.Reset();
        totalItemCount += itemCount;
        if (totalItemCount > 100) {
            break;
        }
    }

    packet.Assemble(
        "dbdb", merchantCount, merchantPacket.GetLength(), merchantPacket.GetPacket(), totalItemCount,
        combinedItemPacket.GetLength(), combinedItemPacket.GetPacket()
    );
}

void CPrivateStoreMarket::InsertItems(const int merchantId, const bool wholesale, PrivateStoreMap& items) {
    // Current L2Server 0x009f2b88.
    L2_GUARD_FUNCTION();
    static_cast<void>(wholesale);
    L2_GUARD(m_Lock);
    _InsertMerchant(merchantId);
    for (const auto& entry : items) {
        if (entry.second != nullptr) {
            _InsertItem(entry.second->nClassId, merchantId);
        }
    }
}

void CPrivateStoreMarket::Clear() {
    // Current L2Server 0x009f2cc0.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    for (auto& entry : m_ItemToMerchantMap) {
        delete entry.second;
    }
    m_Merchants.clear();
    m_ItemToMerchantMap.clear();
}
