#pragma once

#include "Item.h"
#include "L2Packet.h"

#include "Protocol/src/L2Constants.h"
#include "Shared/src/ItemConsumeType.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class CSocket;
class CPackageItemPacketManager;
class CCommissionItem;
class User;
struct CPrivateStoreData;
struct InventoryParams;
enum ItemType : std::int32_t;

// Original ItemAssembler.cpp owner used by CacheD item replies. Current
// RTTI fixes one direct class, a nine-slot virtual surface and this complete
// 0xd0-byte layout.
class CItemDisassemblerFromCacheD {
public:
    CItemDisassemblerFromCacheD();
    virtual ~CItemDisassemblerFromCacheD();

    const unsigned char* DisassembleItemInfo(const unsigned char* packet) { return DisassembleItemInfoImpl(packet); }

protected:
    virtual const unsigned char* DisassembleItemAssembleType(const unsigned char* packet);
    virtual const unsigned char* DisassembleBasicInfo(const unsigned char* packet);
    virtual const unsigned char* DisassembleVariation(const unsigned char* packet);
    virtual const unsigned char* DisassembleAttribute(const unsigned char* packet);
    virtual const unsigned char* DisassembleShapeShift(const unsigned char* packet);
    virtual const unsigned char* DisassembleEnsoul(const unsigned char* packet);
    virtual const unsigned char* DisassembleUseCount(const unsigned char* packet);
    virtual const unsigned char* DisassembleItemInfoImpl(const unsigned char* packet);

public:
    std::uint8_t m_eAssembleType; // +0x08
    std::array<std::byte, 0x03> m_padding09; // +0x09
    std::int32_t m_nDBID; // +0x0c
    std::int32_t m_nClassID; // +0x10
    std::array<std::byte, 0x04> m_padding14; // +0x14
    std::int64_t m_nAmount; // +0x18
    std::int32_t m_nEnchanted; // +0x20
    std::int32_t m_nDamaged; // +0x24
    std::int16_t m_nIsBlessed; // +0x28
    std::array<std::byte, 0x02> m_padding2a; // +0x2a
    std::int32_t m_nIdent; // +0x2c
    std::int16_t m_nWished; // +0x30
    std::array<std::byte, 0x02> m_padding32; // +0x32
    std::int32_t m_nDuration; // +0x34
    std::uint8_t m_nWareHouse; // +0x38
    std::array<std::byte, 0x03> m_padding39; // +0x39
    std::int16_t m_hLocked; // +0x3c
    std::array<std::byte, 0x02> m_padding3e; // +0x3e
    std::int32_t m_nVariation1; // +0x40
    std::int32_t m_nVariation2; // +0x44
    std::int32_t m_nIntensiveItemClassID; // +0x48
    BaseAttribute m_Attribute; // +0x4c
    std::array<std::byte, 0x02> m_padding5e; // +0x5e
    std::int32_t m_nShapeShiftedClassID; // +0x60
    std::array<std::byte, 0x04> m_padding64; // +0x64
    std::map<int, EnsoulOptionData*> m_mEnsoulInfo[3]; // +0x68
    std::int16_t m_hUseCount; // +0xc8
    std::array<std::byte, 0x06> m_targetTailPadding; // +0xca
};


// Current L2Server.exe ItemAssembler.cpp owner used exclusively by the
// CacheD load-items reply. RTTI fixes CItemDisassemblerFromCacheD as the
// sole direct base. Only the basic-info slot is overridden.
class CItemDisassemblerLoadItem final : public CItemDisassemblerFromCacheD {
public:
    CItemDisassemblerLoadItem();

protected:
    const unsigned char* DisassembleBasicInfo(const unsigned char* packet) override;

public:
    std::int32_t m_nIndex; // +0xd0
    std::array<std::byte, 0x04> m_paddingD4; // +0xd4
};


// Current L2Server.exe ItemAssembler.cpp owner used by the manipulate-item
// CacheD reply. RTTI fixes CItemDisassemblerFromCacheD as the sole direct
// base. Only the basic-info and shape-shift slots are overridden.
class CItemDisassemblerItemMani final : public CItemDisassemblerFromCacheD {
public:
    CItemDisassemblerItemMani();

protected:
    const unsigned char* DisassembleBasicInfo(const unsigned char* packet) override;
    const unsigned char* DisassembleShapeShift(const unsigned char* packet) override;

public:
    std::int32_t m_nIndex; // +0xd0
    std::array<std::byte, 0x04> m_paddingD4; // +0xd4
    std::int64_t m_nChangeAmount; // +0xd8
    std::int32_t m_eChangeScope; // +0xe0
    std::int32_t m_nGetReason; // +0xe4
    std::int32_t m_nShapeShiftedPeriod; // +0xe8
    std::array<std::byte, 0x04> m_paddingEc; // +0xec
};


// Direction-specific item envelope sent from L2Server to CacheD. CacheD's
// recovered CItemDisassemblerFromL2Server fixes this complete
// 0xc8-byte layout and nine-slot virtual surface.
class CItemAssemblerSendToCacheD {
public:
    explicit CItemAssemblerSendToCacheD(unsigned char assembleType);
    CItemAssemblerSendToCacheD(CItem& item, unsigned char assembleType);
    CItemAssemblerSendToCacheD(CItem& item, BaseAttribute& attribute, unsigned char assembleType);
    virtual ~CItemAssemblerSendToCacheD();

    int AssembleItemInfo(IPacketHelper& packet) { return AssembleItemInfoImpl(packet); }

protected:
    virtual int AssembleItemAssembleType(IPacketHelper& packet);
    virtual int AssembleBasicInfo(IPacketHelper& packet);
    virtual int AssembleVariation(IPacketHelper& packet);
    virtual int AssembleAttribute(IPacketHelper& packet);
    virtual int AssembleShapeShift(IPacketHelper& packet);
    virtual int AssembleEnsoul(IPacketHelper& packet);
    virtual int AssembleUseCount(IPacketHelper& packet);
    virtual int AssembleItemInfoImpl(IPacketHelper& packet);

    std::uint8_t m_eAssembleType; // +0x08
    std::array<std::uint8_t, 3> m_padding09;
    std::int32_t m_nDBID; // +0x0c
    std::int32_t m_nClassID; // +0x10
    std::array<std::uint8_t, 4> m_padding14;
    std::int64_t m_nAmount; // +0x18
    std::int32_t m_nEnchanted; // +0x20
    std::int32_t m_nDamaged; // +0x24
    std::int32_t m_nIsBlessed; // +0x28
    std::int32_t m_nIdent; // +0x2c
    std::int16_t m_nWished; // +0x30
    std::array<std::uint8_t, 2> m_padding32;
    std::int32_t m_nWareHouse; // +0x34
    std::int16_t m_hLocked; // +0x38
    std::array<std::uint8_t, 2> m_padding3a;
    std::int32_t m_nVariation1; // +0x3c
    std::int32_t m_nVariation2; // +0x40
    std::int32_t m_nIntensiveItemClassID; // +0x44
    BaseAttribute m_Attribute; // +0x48
    std::array<std::uint8_t, 2> m_padding5a;
    std::int32_t m_nShapeShiftedClassID; // +0x5c
    std::map<int, EnsoulOptionData*> m_mEnsoulInfo[3]; // +0x60
    std::int16_t m_hUseCount; // +0xc0
    std::array<std::uint8_t, 6> m_targetTailPadding; // +0xc2
};


// Exact current ItemAssembler.cpp owner used by User::OnBuyPacket. Current
// RTTI fixes CItemAssemblerSendToCacheD as the sole direct base and the
// 9-slot virtual surface is inherited unchanged except for AssembleBasicInfo.
class CItemAssemblerBuyItem final : public CItemAssemblerSendToCacheD {
public:
    CItemAssemblerBuyItem(
        CItem& item, std::int64_t amount, std::int64_t price, BaseAttribute& attribute, unsigned char assembleType
    );

protected:
    int AssembleBasicInfo(IPacketHelper& packet) override;

private:
    std::int64_t m_nPrice; // +0xc8
    ::ItemConsumeType m_eConsumeType; // +0xd0
    [[maybe_unused]] std::array<std::uint8_t, 4> m_paddingD4;
};


// Exact current ItemAssembler.cpp owner used by
// AssembleForDeleteItemInInventoryBeforeCommit. Current RTTI fixes the same
// complete nine-slot base surface with only AssembleBasicInfo overridden.
class CItemAssemblerChangeItemData final : public CItemAssemblerSendToCacheD {
public:
    CItemAssemblerChangeItemData(
        CItem& item, int changeType, std::int64_t amount, BaseAttribute& attribute, unsigned char assembleType
    );

protected:
    int AssembleBasicInfo(IPacketHelper& packet) override;

private:
    std::int32_t m_nChangeType; // +0xc8
    [[maybe_unused]] std::array<std::uint8_t, 4> m_paddingCc;
};


// Exact current ItemAssembler.cpp owner used by warehouse withdrawals.
// Current RTTI fixes CItemAssemblerSendToCacheD as the sole direct base.
// only the basic-info slot is overridden and the complete extent is 0xd0.
class CItemAssemblerWareHouse final : public CItemAssemblerSendToCacheD {
public:
    CItemAssemblerWareHouse(
        CItem& item, int databaseId, std::int64_t amount, ::ItemConsumeType consumeType, unsigned char assembleType
    );

protected:
    int AssembleBasicInfo(IPacketHelper& packet) override;

private:
    ::ItemConsumeType m_eConsumeType; // +0xc8
    [[maybe_unused]] std::array<std::uint8_t, 4> m_paddingCc;
};


class CItemAssemblerItemMani final : public CItemAssemblerSendToCacheD {
public:
    CItemAssemblerItemMani(CItem& item, InventoryParams& parameters, int index, unsigned char assembleType);

protected:
    int AssembleBasicInfo(IPacketHelper& packet) override;
    int AssembleShapeShift(IPacketHelper& packet) override;

private:
    ::ItemConsumeType m_eConsumeType; // +0xc8
    std::int32_t m_nIndex; // +0xcc
    std::int32_t m_nGetReason; // +0xd0
    std::int32_t m_nShapeShiftedPeriod; // +0xd4
};


class CItemAssemblerSendToCLNT {
public:
    CItemAssemblerSendToCLNT();
    explicit CItemAssemblerSendToCLNT(CItem& item);
    virtual ~CItemAssemblerSendToCLNT();

    int AssembleItemInfo(IPacketHelper& packet) { return AssembleItemInfoImpl(packet); }

protected:
    friend class CPackageItemPacketManager;

    virtual int AssembleItemAssembleType(IPacketHelper& packet);
    virtual int AssembleBasicInfo(IPacketHelper& packet);
    virtual int AssembleVariation(IPacketHelper& packet);
    virtual int AssembleAttribute(IPacketHelper& packet);
    virtual int AssembleEnchantOption(IPacketHelper& packet);
    virtual int AssembleShapeShift(IPacketHelper& packet);
    virtual int AssembleEnsoul(IPacketHelper& packet);
    virtual int AssembleUseCount(IPacketHelper& packet);
    virtual int AssembleReuseDelay(IPacketHelper& packet);
    virtual int AssembleItemInfoImpl(IPacketHelper& packet);

    bool IsAssembleVariation();
    bool IsAssembleAttribute();
    bool IsAssembleEnchantOption();
    bool IsAssembleShapeShift();
    bool IsAssembleEnsoul();
    bool IsAssembleUseCount();
    bool IsAssembleReuseDelay();

    std::uint8_t m_eAssembleType; // +0x08
    std::array<std::uint8_t, 3> m_padding09;
    std::int32_t m_nServerID; // +0x0c
    std::int32_t m_nClassID; // +0x10
    std::int32_t m_nSlotIndex; // +0x14
    std::int64_t m_nAmount; // +0x18
    ItemType m_eItemType; // +0x20
    std::int16_t m_nIsBlessed; // +0x24
    bool m_bIsEquipped; // +0x26
    std::uint8_t m_padding27;
    std::int64_t m_eSlotBitType; // +0x28
    std::int16_t m_nEnchanted; // +0x30
    std::int16_t m_nDamaged; // +0x32
    std::int32_t m_nDuration; // +0x34
    std::int32_t m_nPeriodLeft; // +0x38
    std::int16_t m_nUseable; // +0x3c
    std::int16_t m_hLocked; // +0x3e
    std::int32_t m_nVariation1; // +0x40
    std::int32_t m_nVariation2; // +0x44
    BaseAttribute m_Attribute; // +0x48
    std::array<std::uint8_t, 2> m_padding5a;
    std::int32_t m_nShapeShiftedClassID; // +0x5c
    std::map<int, EnsoulOptionData*> m_mEnsoulInfo[3]; // +0x60
    ItemDefaultActionEnum m_eAction; // +0xc0
    std::int16_t m_hUseCount; // +0xc4
    std::array<std::uint8_t, 2> m_paddingc6;
    std::int32_t m_nReuseDelay; // +0xc8
    std::int32_t m_nRemainTimeMs; // +0xcc
};


// Exact ItemAssembler.cpp owner used by CacheD item replies that must be
// published to the client. Current RTTI fixes CItemAssemblerSendToCLNT as
// the sole direct base and the complete eleven-slot virtual surface. Only
// AssembleItemInfoImpl is overridden. The CacheD database id is the sole
// appended state at +0xd0.
class CItemAssemblerFromDB : public CItemAssemblerSendToCLNT {
public:
    CItemAssemblerFromDB() : m_nItemDBID(0), m_paddingD4{} {}

    const unsigned char* DisassembleFromDB(User& user, const unsigned char* packet);

    // The matching project type record publishes both accessors. Current
    // warehouse callbacks inline the matching +0xd0/+0x10 reads.
    int GetDBId() const { return m_nItemDBID; }
    int GetClassId() const { return m_nClassID; }

protected:
    int AssembleItemInfoImpl(IPacketHelper& packet) override;
    void SetItemConst(CItem& item);

public:
    std::int32_t m_nItemDBID; // +0xd0

protected:
    std::array<std::uint8_t, 4> m_paddingD4;
};


// Current RTTI fixes the warehouse-list assembler as a distinct final
// source type layered directly over CItemAssemblerFromDB. It adds no state
// and inherits the complete eleven-slot virtual surface.
class CItemAssemblerWarehouseList final : public CItemAssemblerFromDB {
public:
    CItemAssemblerWarehouseList() = default;
    ~CItemAssemblerWarehouseList() override = default;
};


class CItemAssembler final : public CItemAssemblerSendToCLNT {
public:
    CItemAssembler(User& user, CItem& item, bool equipped);
};


class CItemAssemblerAmount final : public CItemAssemblerSendToCLNT {
public:
    CItemAssemblerAmount(CItem& item, std::int64_t amount);
    CItemAssemblerAmount(CItem& item, std::int64_t amount, BaseAttribute& attribute);
};


class CItemAssemblerPrivateStore final : public CItemAssemblerSendToCLNT {
public:
    CItemAssemblerPrivateStore(CItem& item, CPrivateStoreData& storeData, std::int64_t amount, BaseAttribute& attribute);
};


class CItemAssemblerCommission final : public CItemAssemblerSendToCLNT {
public:
    explicit CItemAssemblerCommission(CCommissionItem* item);
};


struct PACKAGE_ITEM_PACKETHELPER {
    int nItemCount = 0;
    CPacketHelperEx<20480> ItemPacketHelper;
};


using PackageItemPacketHelperList = std::vector<PACKAGE_ITEM_PACKETHELPER, std::allocator<PACKAGE_ITEM_PACKETHELPER>>;

class CPackageItemPacketManager {
public:
    CPackageItemPacketManager();
    virtual ~CPackageItemPacketManager();

    void Init();
    void CheckLimitPackageItemPacketDataSize();
    int PackageAssemble(const char* format, ...);
    int PackageAssembleItem(CItemAssemblerSendToCLNT* itemAssembler);
    void SendPacket_PackageItemData(
        CSocket* socket, S_PROTOCOL protocol, PackageItemPacket::PACKAGE_ITEM_PACKET_TYPE packetType
    );
    void SendPacket_PackageItemData(
        CSocket* socket,
        S_PROTOCOL protocol,
        S_EX_PROTOCOL extendedProtocol,
        PackageItemPacket::PACKAGE_ITEM_PACKET_TYPE packetType
    );
    void SetAssembleItemLengthPerItem(int length);
    void IncreasePackageItemCount();
    int GetTotalPackageItemCount();
    int GetTotalPackageItemLength();

    PackageItemPacketHelperList m_vPackageItemPacketHelperList;
    int m_nCurrentPacketHelperIndex;
    int m_nTotalPackageItemCount;
    int m_nAssembleItemLengthPerItem;

protected:
    void AddNewPackageItemPacketHelper();
};
