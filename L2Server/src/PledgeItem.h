#pragma once

#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/Utils.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CPledge;
class PledgeMastery;
class User;
struct PledgeItemInfo;

namespace protocol {
struct PkPledgeItemInfo;
}

namespace PledgeItemStuff {
void RequestLoadPledgeItem(CPledge* pledge);
}

class PledgeItem : public MemoryObject {
public:
    struct Builder {
        static void ReloadInfo(PledgeItem* items);
    };

    struct ItemNode final {
        void SetItemNode(const PledgeItemInfo* info, std::int32_t expiryTime);

        int id;
        std::int32_t tExpiryTime;
        int nRemainCount;
        std::int32_t tLastCheckTime;
        const PledgeItemInfo* pInfo;
    };

    L2_DECLARE_OBJECT_REGISTRATION(PledgeItem)

    PledgeItem();
    ~PledgeItem() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_ADD_REF_OUT_OF_LINE()
    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    bool AddItem(int itemClassId, std::int32_t expiryTime);
    bool RemoveItem(int itemClassId);
    bool HasItem(int itemClassId) const;
    const PledgeItemInfo* FindInfo(int itemClassId) const;
    bool IncreaseItemCount(int itemClassId, int amount);
    bool DecreaseItemCount(int itemClassId, int amount, std::int32_t currentTime = 0);
    bool GetItemInfo(int itemClassId, int& remainCount, std::int32_t& expiryTime, std::int32_t& resetTime) const;
    void RefillItemCount(std::int32_t currentTime = 0);
    JobTransactionLock* GetTransactionLock() noexcept { return &m_TransactionLock; }
    void SelectItemIDs(std::vector<int>& itemIds, bool masteryOnly) const;
    void SelectExpiredItemIDs(std::int32_t now, std::vector<int>& itemIds) const;
    void FilterInfos(std::map<int, PledgeItemInfo*>& infos) const;

private:
    friend class CPledge;
    friend void PledgeItemStuff::RequestLoadPledgeItem(CPledge* pledge);

    mutable CYieldLock m_lock; // +0x18
    bool m_bLoadedFromDB; // +0x28
    std::byte m_padding29To30[7];
    std::map<int, ItemNode> m_Items; // +0x30
    JobTransactionLock m_TransactionLock; // +0x50
    std::uint32_t m_padding5cTo60;

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(PledgeItem)
};


namespace PledgeItemStuff {

namespace Builder {
void SendPledgeItemListTo(User* user);
}

bool IsAddableItem(int pledgeLevel, const PledgeMastery* mastery, const PledgeItemInfo* info);
bool IsAddableItemByAuto(int pledgeLevel, const PledgeMastery* mastery, const PledgeItemInfo* info);
bool IsAddableItemByManual(int pledgeLevel, const PledgeMastery* mastery, const PledgeItemInfo* info);
void SelectAddableItemsByAuto(int pledgeLevel, const PledgeMastery* mastery, std::map<int, PledgeItemInfo*>& infos);
void AddPledgeItemAndSave(CPledge* pledge, int itemClassId, int expiryTimeSeconds);
void AddPledgeItemByLevelAndMastery(CPledge* pledge);
void RequestLoadPledgeItem(CPledge* pledge);
void RequestRemovePledgeItem(CPledge* pledge, int itemClassId);
void RequestRemovePledgeItems(CPledge* pledge, bool masteryOnly);
void SendPledgeItemListTo(User* user);
void SendPledgeItemInfoTo(User* user, int itemClassId);
void RequestAddPledgeItem(User* user, int itemClassId);
bool RequestBuyPledgeItem(User* user, int itemClassId, std::int64_t amount);

} // namespace PledgeItemStuff
