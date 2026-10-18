#pragma once

#include "Item.h"
#include "WareHouseType.h"

#include "Shared/src/BrBuyProductResultType.h"
#include "Shared/src/BuySellResult.h"
#include "Shared/src/ItemConsumeType.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/Obj.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CWareHouse;
class CPetWarehouse;
class CLogOutCharMessage;
class CSocket;
namespace CommissionOwner {
enum Enum : int;
}
class EtcMani;
using CWareHouseSP = CSPointer<CWareHouse>;
using CPetWarehouseSP = CSPointer<CPetWarehouse>;

// The misspelling is part of the current PDB signature. The wrapper at
// 0x005de214 decodes exactly these five values and appends the pet flag before
// forwarding the carrier to CWareHouse::DestroyItem at 0x005dd678.
struct DestoryItemParam {
    int itemId = 0; // +0x00, database ID
    int itemServerId = 0; // +0x04, L2Server runtime ID
    int consumeType = 0; // +0x08, ItemConsumeType
    int userServerId = 0; // +0x0c, L2Server runtime ID
    std::int64_t amount = 0; // +0x10
    bool isPet = false; // +0x18
    std::array<std::byte, 7> padding{}; // +0x19
};


// Current guard signatures keep both this value type and the exact ordered
// xmap spelling. Machine accesses prove rollback first and changed/live item
// second. The original private member identifiers do not survive.
class CItemKey {
public:
    CItemKey() = default;
    CItemKey(CItem* rollbackItem, CItem* item) noexcept : rollbackItem(rollbackItem), item(item) {}

    CItem* rollbackItem = nullptr;
    CItem* item = nullptr;
};

using CItemTransactionOrderMap = std::map<int, CItemKey>;

// Shipping owner: WareHouse.obj/WareHouse.cpp. The current vtable, +0x08
// success flag, +0x10 xmap and +0x30 insertion counter fix the 0x38 layout.
class CTransaction {
public:
    CTransaction();
    virtual ~CTransaction();

    void SetCommit();
    void SetRollback();
    bool AddItem(CItemSP item);
    bool AddItemNew(CItemSP item);

    static void RollbackTransactOrderMap(CItemTransactionOrderMap& items);

private:
    bool m_success;
    CItemTransactionOrderMap m_items;
    int m_order;
};


// The warehouse owns one item-map bucket in the current image. This is not
// the 1024-bucket CItemMap used by the process-wide g_itemCache.
using CWareHouseItemMap = CObjectMap<CItem, 1, 5, RWLock, int>;


// Shipping owner: CacheD/WareHouse.{h,cpp}. The constructor is inlined at
// every current owner. Target layout and the four post-MemoryObject vtable
// slots are fixed by Cached.exe.
class CWareHouse : public MemoryObject {
public:
    // The enum type survives in the current method signature, but no
    // available PDB keeps its enumerator spellings. Keep the exact type
    // boundary without inventing public names for values 0 through 3.
    enum DeleteResult : int;

    CWareHouse();
    ~CWareHouse() override;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void Init(WareHouseType warehouseType, bool loadItems);

    virtual CItemSP GetItemByType(int itemType, int npcId);
    virtual CItemSP GetItem(int itemId);
    virtual CItemSP PopItem(int itemId);
    virtual bool PushItem(CItem* item);

    bool RemoveEquipped(int itemId);
    bool DeleteItem(int itemId, std::int64_t amount);
    bool ModifyItem(
        int itemId,
        unsigned int itemType,
        std::int64_t amount,
        int enchant,
        int eroded,
        int bless,
        int ident,
        int wished,
        int warehouse,
        ItemOptionKey optionKey,
        int intensiveItemType,
        BaseAttribute baseAttribute,
        short hLocked,
        short useCount
    );
    bool SeizeItem(int option, int itemId, int warehouse, int useSeizeItem2);
    bool SeizeItem(int option, int itemId, std::int64_t amount, int unused, int warehouse);
    bool MoveItemToChar(int itemId, CWareHouse* destinationWarehouse);
    bool MoveItemToChar(int itemId, std::int64_t amount, CWareHouse* destinationWarehouse);

    bool AddItemByAdmin(
        int* itemId,
        int itemType,
        std::int64_t amount,
        int enchant,
        int eroded,
        int bless,
        int ident,
        int wished,
        int warehouse,
        ItemOptionKey optionKey,
        int intensiveItemType,
        BaseAttribute baseAttribute,
        CItem::EnsoulInfo ensoulInfo[],
        short hLocked,
        short useCount,
        int appearanceType,
        int modifyPeriod,
        int period,
        int duration
    );
    bool AddItemByAdmin(
        int* itemId,
        int itemType,
        std::int64_t amount,
        int enchant,
        int eroded,
        int bless,
        int ident,
        int wished,
        int warehouse,
        ItemOptionKey optionKey,
        int intensiveItemType,
        BaseAttribute baseAttribute,
        short hLocked,
        short useCount,
        int appearanceType,
        int modifyPeriod,
        int period,
        int duration
    );

    CItemSP AddAssetItem(int itemType, std::int64_t amount);
    CItemSP GetAdena();
    bool AddAdena(std::int64_t amount);
    bool DelAdena(std::int64_t amount);
    BrBuyProductResultType GetPaymentItems(
        int paymentType, std::int64_t paymentAmount, int* paymentItemId, int* remainingPaymentAmount
    );
    BrBuyProductResultType GiveItems(
        int itemType,
        int productAmount,
        int itemAmount,
        std::int64_t warehouseNo,
        int productId,
        int* createdItemCount,
        CPacketHelperEx<20480>& packet
    );
    DeleteResult _DeleteAssetOrStackableItem(
        int itemType, std::int64_t amount, unsigned int& itemId, std::int64_t& remainingAmount
    );
    std::uint32_t PingTest();
    CItemSP DropItem(CItem* itemToDrop, std::int64_t amountToDrop);
    int PickItem(CSocket* socket, const std::uint8_t* packet, int requestContext);
    bool GetPackage(CSocket* socket, const std::uint8_t* packet, CWareHouse* sourceWarehouse);

    // Current post owners at 0x005f7f1c..0x006042c4. Their complete guard
    // signatures and WareHouse.cpp ownership survive in Cached.exe.
    bool WritePost(
        CSocket* socket,
        CWareHouse* destinationWarehouse,
        int itemCount,
        const int itemIds[],
        const std::int64_t amounts[],
        int resultingItemIds[],
        std::int64_t remainingAmounts[],
        int consumeTypes[],
        std::int64_t fee,
        int& adenaItemId,
        std::int64_t& remainingAdena,
        int auditContext
    );
    bool CancelPost(
        CSocket* socket,
        CWareHouse* destinationWarehouse,
        int itemCount,
        const int itemIds[],
        const std::int64_t amounts[],
        int consumeTypes[],
        int postId,
        int requestContext,
        int senderId,
        int senderServerId
    );
    bool TradeMoney(
        CSocket* socket,
        CWareHouse* recipientWarehouse,
        std::int64_t amount,
        int& recipientAdenaItemId,
        std::int64_t& recipientAdenaAmount,
        int& deletedPayerAdenaItemId,
        int& payerAdenaRetained
    );
    bool ReceivePost(
        CSocket* socket,
        CWareHouse* destinationWarehouse,
        int itemCount,
        const int itemIds[],
        const std::int64_t amounts[],
        int consumeTypes[],
        int postId,
        int requestContext,
        int senderId,
        int receiverId,
        int recipientAdenaItemId,
        std::int64_t recipientAdenaAmount,
        int deletedPayerAdenaItemId,
        int payerAdenaRetained,
        int replyContext,
        std::int64_t& transferredAdena,
        std::int64_t requiredMoney
    );
    bool DeletePost(
        CSocket* socket,
        CWareHouse* destinationWarehouse,
        int itemCount,
        const int itemIds[],
        const std::int64_t amounts[],
        int auditContext
    );
    int AssembleOpenPost(CPacketHelperEx<20480>& packet, int itemCount, int itemIds[]);

    bool BuyResetBeautyItem(CWareHouse* warehouse, int builder, std::int64_t adenaPrice, std::int64_t beautyCoinPrice);
    bool GetCommissionItem(int itemId, CPacketHelperEx<20480>& packet);
    bool WriteCommission(
        CSocket* socket,
        CPacketHelperEx<20480>& packet,
        CWareHouse* destinationWarehouse,
        int itemId,
        std::int64_t amount,
        int consumeType,
        std::int64_t commissionFee,
        int& resultingItemId,
        int& itemType,
        int premiumItemId,
        std::int64_t premiumItemAmount
    );
    bool CancelCommissionInven(CSocket* socket, CWareHouse* destinationWarehouse, int requestContext, int itemId);
    bool DeleteCommissionInven(
        CSocket* socket,
        CPacketHelperEx<20480>& packet,
        CWareHouse* destinationWarehouse,
        int requestContext,
        int itemId,
        std::int64_t commissionId,
        std::int64_t amount,
        int& resultingItemId,
        CommissionOwner::Enum owner,
        int& enchant
    );
    bool DeleteCommissionPost(
        CSocket* socket,
        CPacketHelperEx<20480>& packet,
        CWareHouse* destinationWarehouse,
        int requestContext,
        int itemId,
        std::int64_t commissionId,
        std::int64_t amount,
        int& resultingItemId,
        CommissionOwner::Enum owner,
        int& enchant
    );
    bool BuyCommissionMoney(
        CSocket* socket,
        CWareHouse* destinationWarehouse,
        std::int64_t price,
        std::int64_t itemAmount,
        int& resultingAdenaItemId,
        bool& transferSellerMoney,
        double feeRate
    );
    bool BuyCommissionRollBackMoney(CSocket* socket, CWareHouse* destinationWarehouse, std::int64_t amount, int adenaItemId);
    bool BuyCommissionItem(
        CSocket* socket,
        CPacketHelperEx<20480>& packet,
        CWareHouse* destinationWarehouse,
        int itemId,
        std::int64_t amount,
        int& itemType,
        int& enchant,
        int attributes[],
        char16_t attributeLog[]
    );

    CItemSP MakeNewItem(
        unsigned int ownerId,
        unsigned int itemType,
        std::int64_t amount,
        int enchant,
        int eroded,
        int bless,
        int ident,
        int wished,
        int warehouse,
        const ItemOptionKey& optionKey,
        int intensiveItemType,
        const BaseAttribute& baseAttribute,
        short hLocked,
        short useCount
    );
    CItemSP MakeNewPremiumItem(
        std::int64_t warehouseNo,
        unsigned int ownerId,
        unsigned int itemType,
        std::int64_t amount,
        int enchant,
        int eroded,
        int bless,
        int ident,
        int wished,
        int warehouse,
        const ItemOptionKey& optionKey,
        int intensiveItemType,
        const BaseAttribute& baseAttribute,
        short hLocked,
        short useCount
    );

    bool CreatePet(
        int collar,
        int npcClassId,
        std::int64_t experience,
        int enchant,
        double hp,
        double mp,
        int meal,
        int& itemId,
        int eroded
    );

    bool DeleteAllItem(bool hardDelete);
    bool LoadItems();
    void SendDepositItemsList(
        CSocket* socket,
        int userSocketId,
        int contextId,
        int requestedIdent,
        int memoryId,
        int warehouseClass,
        int itemListGetType
    );
    void SendDepositItemsListEx(CSocket* socket, const std::uint8_t* packet);
    bool WithdrawItemsFromBankEx(CSocket* socket, const std::uint8_t* packet, CWareHouse* sourceWarehouse);
    int DepositItemsToBankEx(CSocket* socket, const std::uint8_t* packet, CWareHouse* destinationWarehouse);
    bool Withdraw(CSocket* socket, const std::uint8_t* packet, CWareHouse* inventory);
    bool Deposit(CSocket* socket, const std::uint8_t* packet, CWareHouse* inventory);
    BuySellResult BuyItems(CSocket* socket, const std::uint8_t* packet, unsigned int responseId);
    BuySellResult SellItems(CSocket* socket, const std::uint8_t* packet, unsigned int responseId);
    bool ChangeItemData(CSocket* socket, const std::uint8_t* packet);
    bool UseItem(CSocket* socket, const std::uint8_t* packet);
    bool DestroyItem(CSocket* socket, DestoryItemParam& parameters);
    bool DestroyItem(CSocket* socket, const std::uint8_t* packet, bool isPet);
    bool VariationItem(CSocket* socket, const std::uint8_t* packet);
    bool CancelVariationItem(CSocket* socket, const std::uint8_t* packet);
    bool CancelEquipmentAddedEffectAndDropItem(CSocket* socket, const std::uint8_t* packet);
    bool ChangeAttributeItem(CSocket* socket, const std::uint8_t* packet);
    bool ShapeShiftItem(CSocket* socket, const std::uint8_t* packet);
    bool ShapeShiftItemFixed(CSocket* socket, const std::uint8_t* packet);
    bool RestoreShapeShiftItem(CSocket* socket, const std::uint8_t* packet);
    bool ExpireShapeShiftItem(CSocket* socket, const std::uint8_t* packet);
    bool EnchantItem(CSocket* socket, const std::uint8_t* packet);
    bool EnchantItemFail(CSocket* socket, const std::uint8_t* packet);
    bool PrivateStoreCommit(CSocket* socket, const std::uint8_t* packet, CWareHouse* purchaserWarehouse);
    bool TradeItems(CSocket* socket, const std::uint8_t* packet, CWareHouse* targetWarehouse);
    bool TradeDivideAdena(CSocket* socket, const std::uint8_t* packet, CWareHouse* targetWarehouse);
    bool ManipulateItems(
        CSocket* socket,
        const std::uint8_t* packet,
        int& responseId,
        int& responseId2,
        std::vector<EtcMani*>& etcManis,
        CPacketHelperEx<20480>& itemPacket,
        CPacketHelperEx<20480>& etcManiPacket,
        unsigned int& changedItemCount,
        std::uint64_t& processedEtcManiCount
    );
    int ManipulatePointItems(CSocket* socket, const std::uint8_t* packet);
    bool ManipulatePremiumItems(CSocket* socket, const std::uint8_t* packet, int requestId, std::int64_t warehouseNo);
    bool hLockedItem(CSocket* socket, const std::uint8_t* packet);
    void SaveInventorySlotIndex(const std::uint8_t* packet);
    bool CrystallizeItem(CSocket* socket, const std::uint8_t* packet, bool isPet);
    bool UpgradeSystem(CSocket* socket, const std::uint8_t* packet);
    int RecipeMakeItem(CSocket* socket, const std::uint8_t* packet);
    int RecipeShopMakeItem(CSocket* socket, const std::uint8_t* packet, CWareHouse* makerWarehouse);
    bool EquipHenna(CSocket* socket, const std::uint8_t* packet);
    bool UnequipHenna(CSocket* socket, const std::uint8_t* packet);
    void AddProcuredCrop(CSocket* socket, const std::uint8_t* packet);
    bool BuySeed(CSocket* socket, const std::uint8_t* packet);
    bool SellCrop(CSocket* socket, const std::uint8_t* packet);
    bool UseRecipeItem(CSocket* socket, const std::uint8_t* packet);
    void SendList(CSocket* socket, unsigned int userSocketId);
    bool SendItemList(CSocket* socket, int userSocketId);
    void SendMonRaceTicketList(CSocket* socket, int command, int characterId, int userSocketId, int page);
    bool CheckMonRaceTicket(unsigned int itemId);
    bool DelMonRaceTicketFromDeposit(unsigned int itemId);
    void DelMonRaceTicketFromDB(unsigned int itemId);
    void DelMonRaceTicketWithoutReward(unsigned int itemId);
    std::int64_t DelMonRaceTicket(unsigned int itemId, int currentRaceId, int allowCurrentRace);
    void SaveUnsavedItems();
    void SaveLateSavingItems();

    std::int64_t GetCheckSumValue();
    void SetCheckSumOnDB();
    bool ItemcheckOnDB();
    std::int64_t GetCheckSumValueOnDB();
    bool ItemCheckBetweenMemoryAndDB();
    static void SetCheckSumOnDBForServerCrash();

    bool ItemsLoaded() const noexcept;
    std::uint32_t GetOwnerId() const noexcept;
    void SetOwnerId(std::uint32_t ownerId) noexcept;
    WareHouseType GetWarehouseNum();

    std::uint64_t GetLockAddr();
    void WriteLock(const wchar_t* callerFile, int callerLine);
    void WriteUnlock();
    void ReadLock(const wchar_t* callerFile, int callerLine);
    void ReadUnlock();

private:
    friend class CLogOutCharMessage;
    friend class CSocket;
    friend bool RequestCharacterLogout(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestTrade(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestPrivateStoreCommit(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestRecipeMakeItem(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestRecipeShopMakeItem(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestUseRecipeItem(CSocket* socket, const std::uint8_t* packet);
    friend bool RequestPickItem(CSocket* socket, const std::uint8_t* packet);

    // Current helpers 0x005ceea8/0x005ce7f8 construct and destroy this exact
    // 0x88-byte nested value. PDB/guard material keeps ItemTradeInfo but
    // not its member identifiers. The names below describe the proven wire,
    // response and four CSPointer slots at their machine-confirmed offsets.
    struct ItemTradeInfo {
        int sourceTradeType = 0; // +0x00, request
        int sourceNewTradeType = 0; // +0x04, response
        int sourceItemId = 0; // +0x08, request
        int sourceResponseItemId = 0; // +0x0c, response
        int sourceServerToken = 0; // +0x10, request
        int sourceResponseServerToken = 0; // +0x14, response
        int targetTradeType = 0; // +0x18, request
        int targetNewTradeType = 0; // +0x1c, response
        int targetItemId = 0; // +0x20, request
        int targetResponseItemId = 0; // +0x24, response
        int targetServerToken = 0; // +0x28, request
        int targetResponseServerToken = 0; // +0x2c, response
        std::int64_t sourceNewAmount = 0; // +0x30, response
        std::int64_t targetNewAmount = 0; // +0x38, response
        std::int64_t amount = 0; // +0x40, request
        CItemSP sourceItem; // +0x48
        CItemSP sourceRollbackItem; // +0x58
        CItemSP targetItem; // +0x68
        CItemSP targetRollbackItem; // +0x78
    };


    bool _CheckItemTradeValidation(
        CWareHouse* sourceWarehouse,
        CWareHouse* targetWarehouse,
        int itemType,
        int sourceTradeType,
        int sourceItemId,
        int sourceServerToken,
        int targetTradeType,
        int targetItemId,
        int targetServerToken,
        std::int64_t amount
    );
    bool _TransactTradeItems(
        CWareHouse* sourceWarehouse,
        CWareHouse* targetWarehouse,
        std::set<unsigned int>& tradedSourceItems,
        CItemTransactionOrderMap& transactionItems,
        ItemTradeInfo& tradeInfo
    );
    void UpdateMarketPrice(CItem* item, std::int64_t price);

    void DeleteItemBySP(CItemSP item);
    CItemSP PopItemAndSave(CItem& item, std::int64_t amount);
    void ManipulateItemTransactionFailed(
        CSocket* socket,
        int responseId,
        CItemTransactionOrderMap& transactionItems,
        std::vector<EtcMani*>& etcManis,
        int responseId2
    );

    L2_DECLARE_OBJECT_REGISTRATION(CWareHouse)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CWareHouse)

protected:
    // The current derived CPetWarehouse methods access these members
    // directly. The matching C1 declaration also keeps this protected
    // boundary. Changing only access control does not alter the target ABI.

    RWLock m_lock; // +0x18
    CWareHouseItemMap m_items; // +0x50
    int m_itemNumber; // +0xb0
    std::uint32_t m_ownerId; // +0xb4
    bool m_itemsLoaded; // +0xb8

    // Current 0x005d3e00 proves this is the aggregate late-save flag. The
    // original member identifier is absent from all surviving symbols.
    bool m_unsavedItemsPending; // +0xb9
    bool m_lateSavingItemsPending; // +0xba
    std::uint8_t m_targetPadding; // +0xbb
    WareHouseType m_warehouseType; // +0xbc
};




// Current CPetWarehouse has the unchanged CWareHouse vtable surface followed
// by the two C1-named integers proven at target offsets +0xc0/+0xc4. Its
// destructor at 0x005009e0 only delegates to CWareHouse::~CWareHouse.
class CPetWarehouse : public CWareHouse {
public:
    CPetWarehouse() = default;
    ~CPetWarehouse() override = default;

    void SendPetItemList(CSocket* socket, int userSocketId, int contextId, int contextValue);
    void ClearItemWhenDespawn();
    // These two source identifiers survive in the matching C1 header. The
    // current CacheSocket.cpp owners inline them as stores to +0xc0/+0xc4.
    void SetPetId(std::uint32_t petId) noexcept;
    void SetNpcClassId(std::uint32_t npcClassId) noexcept;
    bool PetGetItem(CSocket* socket, const std::uint8_t* packet);
    bool PetDropItem(CItemSP itemToDrop);
    bool DepositToPet(
        CSocket* socket,
        int userServerId,
        int petServerId,
        CWareHouse* userWarehouse,
        int itemId,
        ItemConsumeType consumeType,
        std::int64_t count
    );
    bool WithdrawFromPet(
        CSocket* socket,
        int userServerId,
        int petServerId,
        CWareHouse* userWarehouse,
        int itemId,
        ItemConsumeType consumeType,
        std::int64_t count
    );
    bool PetUseItem(CSocket* socket, int petServerId, int itemId, ItemConsumeType consumeType);

private:

    std::uint32_t m_petId = 0; // +0xc0
    std::uint32_t m_npcClassId = 0; // +0xc4
};
