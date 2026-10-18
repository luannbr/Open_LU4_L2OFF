#pragma once

#include "ItemChangeScope.h"
#include "TransactType.h"

#include "Shared/src/BaseAttribute.h"
#include "Shared/src/ItemOptionKey.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CETC_MANI_USE_COUNT;
class EtcManiEnsoul;

class CItem;
class CItemAssemblerSendToL2Server;
class CAdminSocket;
class CPetWarehouse;
class CSocket;
class CUser;
class CWareHouse;
class CSaveUnsavedItemInWareHouse;
class CSaveLateSavingItemInWareHouse;

bool RequestDeletePet(CSocket* socket, const std::uint8_t* packet);
bool RequestSavePet(CSocket* socket, const std::uint8_t* packet);
bool SeizeItemProc(CAdminSocket* socket, int replyOpcode, int option, int itemId, std::int64_t amount, wchar_t auditText[]);
bool SeizeItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool SeizeItem2Packet(CAdminSocket* socket, const std::uint8_t* packet);
bool ChangePetNamePacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelSeizedItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
bool DelItemProc(CAdminSocket* socket, int replyOpcode, int itemId, std::int64_t amount, int deletePet, wchar_t* auditText);
bool MoveItemProc(
    CAdminSocket* socket,
    int replyOpcode,
    int sourceCharacterId,
    int itemId,
    std::int64_t amount,
    int destinationCharacterId,
    int destinationWarehouse,
    const wchar_t* auditText
);
bool MoveItem2Packet(CAdminSocket* socket, const std::uint8_t* packet);

using CItemSP = CSPointer<CItem>;
using CItemMap = CObjectMap<CItem, 1024, 5, RWLock, int>;
using CWareHouseSP = CSPointer<CWareHouse>;

// These current enum type names survive in guard signatures, but their
// enumerator names do not. Keep the ABI-visible 32-bit types without
// inventing source identifiers. Their observed values are used
// only inside the matching recovered implementations.
enum PremiumItemLogType : int;
enum PremiumItemLogReason : int;
enum ShapeShiftingJobType : int;


// Shipping owner: CacheD/Item.{h,cpp}. Cached.exe fixes the 0x108 layout,
// four-slot MemoryObject vtable, intrusive lifetime and every offset below.
class CItem : public MemoryObject {
public:
    using EnsoulInfo = std::map<int, int>;

    explicit CItem(std::uint32_t itemId);
    CItem(
        std::uint32_t itemId,
        std::uint32_t ownerId,
        std::uint32_t itemType,
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
        int inventorySlotIndex,
        int appearanceType,
        short hLocked,
        short useCount
    );
    // Current scalar deleting destructor 0x004eb154 destroys the three
    // EnsoulInfo xmaps at +0xa0 and then the MemoryObject base.
    ~CItem() override = default;

    L2_DECLARE_OBJECT_CLASS_ACCESSORS()
    static void* operator new(std::size_t requestedSize);
    static void* operator new(std::size_t requestedSize, bool objectAllocation);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    static void Init();
    static CItemSP Load(std::uint32_t itemId);
    static void CacheItem(CItem* item);

    CItemSP Set(
        std::uint32_t ownerId,
        std::uint32_t itemType,
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
        int inventorySlotIndex,
        int appearanceType,
        short hLocked,
        short useCount
    );
    CItemSP SetTransation();
    CItemSP Copy();

    bool TransactDelete();
    bool CopyByDeleteChar();
    bool Delete(bool hardDelete, bool premiumRelated);
    bool Save(bool force);
    bool SaveBaseAttribute();
    bool LoadEnsoulInfo();
    bool RollbackTransation(CItem* rollbackItem);
    bool CommitTransation(CWareHouseSP rollbackWarehouse, CWareHouseSP warehouse, CItem* rollbackItem, bool forceSave);
    bool CommitTransation(CItem* rollbackItem, bool forceSave);

    std::int64_t ChangeAmount(std::int64_t amount);
    int GetDuration();
    // Semantic name for current unnamed 0x004eaad8. The current body loads
    // the item's BR agathion energy and has no surviving guard/PDB identity.
    double LoadAgathionEnergy();
    void InitExtraInfo();
    void LogPremiumRelated(
        PremiumItemLogType logType, std::int64_t amount, std::int64_t secondaryAmount, PremiumItemLogReason reason
    );

    void SetDBID(std::uint32_t itemId);
    void SetAmount(std::int64_t amount);
    void SetBaseAttribute(const BaseAttribute& baseAttribute);
    void SetInventorySlotIndex(int inventorySlotIndex);
    void SetApperanceType(int appearanceType, int expiration, ShapeShiftingJobType jobType);
    void SetEnsoulInfo(int slotType, int slotIndex, int optionId);
    void SetEnsoulInfo(EnsoulInfo ensoulInfo[]);

    static int AssembleAttribute(const BaseAttribute& baseAttribute, CPacketHelperEx<20480>& packet);
    static const unsigned char* DisassembleAttribute(const unsigned char* packet, BaseAttribute& baseAttribute);
    static int AssembleEnsoulInfo(CPacketHelperEx<20480>& packet, EnsoulInfo ensoulInfo[]);
    void MakeShapeShiftLogInfo(char16_t* destination);
    void MakeShapeShiftAndVariationLogInfo(char16_t* destination);
    // void MakeEnsoulLogInfo(wchar_t* destination)
    void MakeEnsoulLogInfo(char16_t* destination, std::size_t capacity);

    // Current 0x004e8260..0x004e8294 tiny accessors/mutators. Their source
    // names are kept by the C1 Item.h and their offsets/widths by the
    // complete current bodies. SetBaseAttribute is the separate 0x004e8298.
    std::uint32_t ItemID() const noexcept;
    std::uint32_t OwnerID() const noexcept;
    // Current CPetWarehouse::PetGetItem inlines the later-revision owner
    // transition: changing owner also invalidates the inventory-slot index.
    // The source identifier survives in the matching C1 Item owner.
    void setOwnerID(std::uint32_t ownerId) noexcept;
    std::uint32_t ItemType() const noexcept;
    std::int64_t Amount() const noexcept;
    int Enchant() const noexcept;
    void SetEnchant(int enchantLevel) noexcept;
    int Eroded() const noexcept;
    int Bless() const noexcept;
    int Ident() const noexcept;
    int Wished() const noexcept;
    int Warehouse() const noexcept;
    bool SetWarehouse(int warehouse) noexcept;
    TransactType GetTrasactMode() const noexcept;
    void SetTrasactMode(TransactType mode) noexcept;

    ItemChangeScope ChangeScope() const noexcept;
    void SetChangeScope(ItemChangeScope scope) noexcept;
    std::int64_t PrivateStorePrice() const noexcept;
    void SetPrivateStorePrice(std::int64_t price) noexcept;
    std::int64_t AuditAmount() const noexcept;
    void SetAuditAmount(std::int64_t amount) noexcept;
    bool IsUsable() const noexcept;
    void SetUsable(bool usable) noexcept;
    bool Deleted() const noexcept;
    int InventorySlotIndex() const noexcept;
    short HLocked() const noexcept;
    short UseCount() const noexcept;
    const ItemOptionKey& OptionKey() const noexcept;
    const BaseAttribute& Attribute() const noexcept;
    int AppearanceType() const noexcept;
    const EnsoulInfo* EnsoulSlots() const noexcept;

private:
    friend class CItemAssemblerSendToL2Server;
    // Current DepositToPet/WithdrawFromPet copy the source item's complete
    // creation state while moving between the user and pet warehouses.
    friend class CPetWarehouse;
    friend class CUser;
    friend class CWareHouse;
    friend class CSaveUnsavedItemInWareHouse;
    friend class CSaveLateSavingItemInWareHouse;
    // Current RequestDeletePet reads CItem +0x70 directly while composing
    // the three-part deletion audit. Do not expose an unobserved accessor.
    friend bool RequestDeletePet(CSocket* socket, const std::uint8_t* packet);
    // Current RequestSavePet mode 2 writes CItem +0x34 directly before
    // Save(true). No surviving current accessor name exists for that store.
    friend bool RequestSavePet(CSocket* socket, const std::uint8_t* packet);
    // Both current admin helpers capture +0x70 directly before removing the
    // item from g_itemCache. Preserve that observed source relationship
    // without adding an accessor absent from the current image.
    friend bool ::SeizeItemProc(
        CAdminSocket* socket, int replyOpcode, int option, int itemId, std::int64_t amount, wchar_t auditText[]
    );
    // Current AQ 28 reads CItem +0x70 directly after the original
    // four-integer warehouse seizure. Preserve that observed owner access
    // without publishing an accessor absent from Cached.exe.
    friend bool ::SeizeItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
    // Current AQ 53 reads CItem +0x70 directly before removing the item from
    // the process cache. Preserve that source relationship without an alias.
    friend bool ::SeizeItem2Packet(CAdminSocket* socket, const std::uint8_t* packet);
    // Current AQ 88 writes the pet-collar eroded field at CItem+0x34
    // directly before Save(true). Keep the observed admin owner access.
    friend bool ::ChangePetNamePacket(CAdminSocket* socket, const std::uint8_t* packet);
    // Current AQ 130 reads CItem +0x70 directly while composing the same
    // variation audit as the original administrative delete helper.
    friend bool ::DelSeizedItemPacket(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool ::DelItemProc(
        CAdminSocket* socket, int replyOpcode, int itemId, std::int64_t amount, int deletePet, wchar_t* auditText
    );
    // Current MoveItemProc reads CItem +0x70 while composing the exact
    // five-part administrative transfer audit. Keep the observed owner
    // relationship without publishing an accessor absent from Cached.exe.
    friend bool ::MoveItemProc(
        CAdminSocket* socket,
        int replyOpcode,
        int sourceCharacterId,
        int itemId,
        std::int64_t amount,
        int destinationCharacterId,
        int destinationWarehouse,
        const wchar_t* auditText
    );
    // Current AQ 40 reads CItem +0x70 directly after its whole-item move.
    friend bool ::MoveItem2Packet(CAdminSocket* socket, const std::uint8_t* packet);
    friend bool RequestConvertItem(CSocket* socket, const std::uint8_t* packet);
    // Current EtcMani.cpp writes CItem +0x100 directly after the database
    // update. Keep that observed inline access without adding an alias API.
    friend class CETC_MANI_USE_COUNT;
    // EtcManiEnsoul::Rollback clears the two live xmaps at CItem +0xa0
    // directly before rebuilding them from its captured rollback maps.
    friend class EtcManiEnsoul;

    void InitItem(std::uint32_t itemId);

    L2_DECLARE_OBJECT_REGISTRATION(CItem)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CItem)

    TransactType m_transactMode; // +0x18
    std::uint32_t m_itemId; // +0x1c
    std::uint32_t m_ownerId; // +0x20
    std::uint32_t m_itemType; // +0x24
    std::int64_t m_amount; // +0x28
    int m_enchant; // +0x30
    int m_eroded; // +0x34
    int m_bless; // +0x38
    int m_ident; // +0x3c
    int m_wished; // +0x40
    int m_warehouse; // +0x44
    int m_inTransactions; // +0x48
    int m_inventorySlotIndex; // +0x4c
    short m_hLocked; // +0x50
    ItemChangeScope m_changeScope; // +0x54
    std::int64_t m_privateStorePrice; // +0x58

    // Current GetPackage writes the parsed 64-bit package amount here at
    // 0x005f43c5/0x005f4631/0x005f4683/0x005f4a68 and reads it at
    // 0x005f4fe0 for audit record 0x3b2. The matching C1 Item owner keeps
    // the source identifier m_auditAmount and the two inlined accessors.
    std::int64_t m_auditAmount; // +0x60

    ItemOptionKey m_optionKey; // +0x68
    int m_intensiveItemType; // +0x70
    bool m_isUsable; // +0x74
    // Current CItem::Save(bool) associates +0x75 with setting/writecache,
    // while WareHouse's separate LateSaving path accumulates its amount at
    // +0x78. The two mechanisms must not share one semantic name.
    bool m_writeCachePending; // +0x75
    std::int64_t m_lateSavingAmount; // +0x78
    bool m_deleted; // +0x80
    BaseAttribute m_baseAttribute; // +0x82
    bool m_baseAttributeDirty; // +0x94
    int m_appearanceType; // +0x98
    EnsoulInfo m_ensoulInfo[3]; // +0xa0
    short m_useCount; // +0x100
};



extern CItemMap g_itemCache;
