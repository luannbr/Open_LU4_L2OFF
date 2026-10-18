#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>

class IPacketHelper;
class User;
struct CPrivateStoreData;

class CPrivateStoreMarket : public Singleton<CPrivateStoreMarket> {
public:
    using IntSet = std::set<int>;
    using ItemToMerchantMap = std::map<int, IntSet*>;
    using PrivateStoreMap = std::map<int, CPrivateStoreData*>;

    CPrivateStoreMarket();
    ~CPrivateStoreMarket() override;

    void InsertItems(int merchantId, bool wholesale, PrivateStoreMap& items);
    void RemoveItems(int merchantId, bool wholesale, PrivateStoreMap& items);

    bool InsertMerchant(const int merchantId) {
        L2_GUARD(m_Lock);
        return _InsertMerchant(merchantId);
    }
    bool _InsertMerchant(int merchantId);

    bool InsertItem(const int itemClassId, const int merchantId) {
        L2_GUARD(m_Lock);
        return _InsertItem(itemClassId, merchantId);
    }
    bool _InsertItem(int itemClassId, int merchantId);

    bool RemoveMerchant(const int merchantId) {
        L2_GUARD(m_Lock);
        return _RemoveMerchant(merchantId);
    }
    bool _RemoveMerchant(int merchantId);

    bool RemoveItem(const int itemClassId, const int merchantId) {
        L2_GUARD(m_Lock);
        return _RemoveItem(itemClassId, merchantId);
    }
    bool _RemoveItem(int itemClassId, int merchantId);

    void Clear();

    int GetItemCount() {
        L2_GUARD(m_Lock);
        return static_cast<int>(m_ItemToMerchantMap.size());
    }
    int GetMerchantCount(const int itemClassId) {
        L2_GUARD(m_Lock);
        const auto found = m_ItemToMerchantMap.find(itemClassId);
        return found == m_ItemToMerchantMap.end() ? 0 : static_cast<int>(found->second->size());
    }

    void AssembleMerchant(User& user, IPacketHelper& packet);
    int QueryAllItems(int maximumMerchants, IPacketHelper& packet);
    void QuerySpecificItems(int itemClassId, IPacketHelper& packet);

private:
    IntSet m_Merchants; // +0x08..+0x27
    ItemToMerchantMap m_ItemToMerchantMap; // +0x28..+0x47
    CYieldLock m_Lock; // +0x48..+0x57
};
