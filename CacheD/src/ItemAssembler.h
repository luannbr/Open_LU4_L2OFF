#pragma once

// use the main protocol constants directly.
#include "Protocol/src/L2Constants.h"

#include "Item.h"

#include "Shared/src/ItemConsumeType.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstdint>
#include <vector>

class CSocket;

// Current Cached.exe 0x004f1968..0x004f2ac5. This is the CacheD-side
// counterpart of L2Server's direction-specific item assembler. It belongs in
// CacheD/ItemAssembler.{h,cpp}, not in Shared.
class CItemDisassemblerFromL2Server {
public:
    CItemDisassemblerFromL2Server();
    // Current deleting-destructor body 0x00610c64 is shared by the base and
    // all four derived disassemblers. It restores this vtable, destroys the
    // three 0x20-byte ensoul entries, then conditionally frees the object.
    virtual ~CItemDisassemblerFromL2Server() = default;

    virtual const unsigned char* DisassembleItemAssembleType(const unsigned char* packet);
    virtual const unsigned char* DisassembleBasicInfo(const unsigned char* packet);
    virtual const unsigned char* DisassembleVariation(const unsigned char* packet);
    virtual const unsigned char* DisassembleAttribute(const unsigned char* packet);
    virtual const unsigned char* DisassembleShapeShift(const unsigned char* packet);
    virtual const unsigned char* DisassembleEnsoul(const unsigned char* packet);
    virtual const unsigned char* DisassembleUseCount(const unsigned char* packet);
    virtual const unsigned char* DisassembleItemInfoImpl(const unsigned char* packet);

    // SaveItemsPacket reads these three members directly in the current
    // machine code. The spellings follow the paired assembler family. No
    // portable-only getters are introduced for those reads.
    // unsigned char m_eAssembleType = 0. // target +0x08
    unsigned char m_eAssembleType = ItemAssembleType::NONE; // target +0x08
    std::uint32_t m_nItemID = 0; // target +0x0c
    std::uint32_t m_nItemType = 0; // target +0x10
    std::int64_t m_nAmount = 0; // target +0x18
    int m_nEnchant = 0; // target +0x20
    int m_nEroded = 0; // target +0x24
    short m_nBless = 0; // target +0x28
    int m_nIdent = 0; // target +0x2c
    short m_nWished = 0; // target +0x30
    int m_nWarehouse = 0; // target +0x34. Wire writes low byte
    short m_hLocked = 0; // target +0x38
    int m_nVariation1 = 0; // target +0x3c
    int m_nVariation2 = 0; // target +0x40
    int m_nIntensiveItemType = 0; // target +0x44
    BaseAttribute m_Attribute; // target +0x48
    int m_nShapeShiftedClassID = 0; // target +0x5c
    std::array<CItem::EnsoulInfo, 3> m_mEnsoulInfo; // target +0x60
    short m_hUseCount = 0; // target +0xc0
    // MSVC does not reuse this base-class tail padding for derived members.
    // Make it explicit so Itanium-ABI hosts also begin derived state at the
    // target's +0xc8 instead of placing a 32-bit field at +0xc4.
    std::array<std::uint8_t, 6> m_targetTailPadding{}; // target +0xc2..+0xc7
};


// Current vtable 0x00769cf8 overrides only the basic-info slot with
// 0x004f1f58. L2Server's paired CItemAssemblerBuyItem fixes
// the two appended fields and their "Qd" order.
class CItemDisassemblerBuyItem : public CItemDisassemblerFromL2Server {
public:
    const unsigned char* DisassembleBasicInfo(const unsigned char* packet) override;

    std::int64_t m_nPrice = 0; // target +0xc8
    ItemConsumeType m_eConsumeType = static_cast<ItemConsumeType>(-1); // target +0xd0, ICT_UNKNOWN
};


// Current Cached.exe vtable 0x00769d48 overrides only the basic-info slot
// with 0x004f201c. The paired current L2Server assembler appends the same
// 32-bit change type after the base item record. TotalChecker2015 keeps the
// original member spelling.
class CItemDisassemblerChangeItemData : public CItemDisassemblerFromL2Server {
public:
    const unsigned char* DisassembleBasicInfo(const unsigned char* packet) override;

    int m_nChangeType = 0; // target +0xc8
};


// Current Cached.exe vtable 0x00769d58 and method 0x004f20d8. Warehouse
// requests append the consume type to the otherwise complete base item
// record. The paired current L2Server warehouse assembler independently
// confirms the single trailing dword.
class CItemDisassemblerWareHouse : public CItemDisassemblerFromL2Server {
public:
    const unsigned char* DisassembleBasicInfo(const unsigned char* packet) override;

    ItemConsumeType m_eConsumeType = static_cast<ItemConsumeType>(-1); // target +0xc8
};


// Current Cached.exe vtable 0x00769d68 and methods 0x004f2194/0x004f22a8.
// The field names are kept by the exact TotalChecker2015 PDB's paired
// L2Server-side CItemAssemblerItemMani. The current CacheD and L2Server
// machine code confirms the shifted Win64 offsets below.
class CItemDisassemblerItemMani : public CItemDisassemblerFromL2Server {
public:
    const unsigned char* DisassembleBasicInfo(const unsigned char* packet) override;
    const unsigned char* DisassembleShapeShift(const unsigned char* packet) override;

    ItemConsumeType m_eConsumeType = static_cast<ItemConsumeType>(-1); // target +0xc8
    int m_nIndex = 0; // target +0xcc
    int m_nGetReason = 0; // target +0xd0
    int m_nShapeShiftedPeriod = 0; // target +0xd4
};


// Shipping owner: CacheD/ItemAssembler.{h,cpp}. L2Server has a distinct,
// direction-specific ItemAssembler family. This CacheD owner therefore stays
// outside Shared even though both sides use Shared/packethelper.h.
class CItemAssemblerSendToL2Server {
public:
    CItemAssemblerSendToL2Server(CItem& item, unsigned char assembleType);
    CItemAssemblerSendToL2Server(CItem& item, BaseAttribute& attribute, unsigned char assembleType);
    // Current deleting-destructor body 0x00610c14 is shared by the base,
    // Amount, ItemMani and LoadItem vtables and performs the matching
    // three-entry ensoul cleanup before conditional object deletion.
    virtual ~CItemAssemblerSendToL2Server() = default;

    virtual int AssembleItemAssembleType(IPacketHelper& packet);
    virtual int AssembleBasicInfo(IPacketHelper& packet);
    virtual int AssembleVariation(IPacketHelper& packet);
    virtual int AssembleAttribute(IPacketHelper& packet);
    virtual int AssembleShapeShift(IPacketHelper& packet);
    virtual int AssembleEnsoul(IPacketHelper& packet);
    virtual int AssembleIUseCount(IPacketHelper& packet);
    virtual int AssembleItemInfoImpl(IPacketHelper& packet);

protected:
    // unsigned char m_eAssembleType = 0.
    unsigned char m_eAssembleType = ItemAssembleType::NONE;
    std::uint32_t m_nItemID = 0;
    std::uint32_t m_nItemType = 0;
    std::int64_t m_nAmount = 0;
    int m_nEnchant = 0;
    int m_nEroded = 0;
    short m_nBless = 0;
    int m_nIdent = 0;
    short m_nWished = 0;
    int m_nDuration = 0;
    // The target stores the complete int at +0x38 and serializes only its
    // low byte. Keeping this as a char would move m_hLocked and every
    // following member four bytes before their current Cached.exe offsets.
    int m_nWarehouse = 0;
    short m_hLocked = 0;
    int m_nVariation1 = 0;
    int m_nVariation2 = 0;
    int m_nIntensiveItemType = 0;
    BaseAttribute m_Attribute;
    int m_nShapeShiftedClassID = 0;
    std::array<CItem::EnsoulInfo, 3> m_mEnsoulInfo;
    short m_hUseCount = 0;

    // MSVC starts CItemAssemblerItemMani at the full +0xd0 base extent.
    // Itanium ABIs otherwise reuse the six tail-padding bytes after +0xc8.
    std::array<std::uint8_t, 6> m_targetBaseExtentPadding{};
};


// Current Cached.exe vtable 0x00769c58. The class has no additional target
// state and does not override an assembly slot: its inline constructor copies
// the item through the base constructor and replaces only the base amount at
// +0x18. DepositItemsToBankEx uses it to describe the transferred amount
// while retaining every other field from the source item.
class CItemAssemblerAmount : public CItemAssemblerSendToL2Server {
public:
    CItemAssemblerAmount(CItem& item, std::int64_t amount, unsigned char assembleType);
};


// Current Cached.exe vtable 0x00769ca8 and methods 0x004f1794/0x004f18a4.
// The inlined constructor at 0x005d76e8 fixes the five appended fields and
// their order. This is the response-side counterpart of
// CItemDisassemblerItemMani used by CWareHouse::ManipulateItems.
class CItemAssemblerItemMani : public CItemAssemblerSendToL2Server {
public:
    CItemAssemblerItemMani(
        CItem& item,
        int index,
        std::int64_t changeAmount,
        ItemChangeScope changeScope,
        int getReason,
        int shapeShiftedPeriod,
        unsigned char assembleType
    );

    int AssembleBasicInfo(IPacketHelper& packet) override;
    int AssembleShapeShift(IPacketHelper& packet) override;

private:
    int m_nIndex = 0; // target +0xd0
    std::int64_t m_nChangeAmount = 0; // target +0xd8
    ItemChangeScope m_eChangeScope = ItemChangeScope_Unknown; // target +0xe0
    int m_nGetReason = 0; // target +0xe4
    int m_nShapeShiftedPeriod = 0; // target +0xe8
};


class CItemAssemblerLoadItem : public CItemAssemblerSendToL2Server {
public:
    CItemAssemblerLoadItem(CItem& item, unsigned char assembleType);

    int AssembleBasicInfo(IPacketHelper& packet) override;

private:
    int m_nInventorySlotIndex = 0;
};

// PDB-confirmed original carrier and member spellings. On the Win64 target
// this is exactly 0x5018 bytes: a four-byte count, four-byte alignment gap and
// CPacketHelperEx<20480>.
struct PACKAGE_ITEM_PACKETHELPER {
    int nItemCount = 0;
    CPacketHelperEx<20480> ItemPacketHelper;
};


using PackageItemPacketHelperList = std::vector<PACKAGE_ITEM_PACKETHELPER, std::allocator<PACKAGE_ITEM_PACKETHELPER>>;

// Current 0x004f31f0..0x004f336c and the surviving VS2015 PDB agree on this
// owner and its four state members. The target std::vector has three active
// pointer cells followed by the xallocator's inline pointer-sized state, so
// the first integer begins at +0x28 instead of +0x20.
class CPackageItemPacketManager {
public:
    CPackageItemPacketManager();
    virtual ~CPackageItemPacketManager();

    void Init();
    void CheckLimitPackageItemPacketDataSize();
    int PackageAssemble(const char* format, ...);
    int PackageAssembleItem(CItemAssemblerSendToL2Server& itemAssembler);
    void SendPacket_PackageItemData(CSocket* socket, unsigned char protocol, IPacketHelper& header);
    int GetTotalPackageItemLength() const;

    // The shipping warehouse callers update these directly after a successful
    // item assembly. Keeping that source contract avoids moving behavior into
    // a portable-only helper.
    PackageItemPacketHelperList m_vPackageItemPacketHelperList;
    int m_nCurrentPacketHelperIndex = -1;
    int m_nTotalPackageItemCount = 0;
    int m_nAssembleItemLengthPerItem = 0;

protected:
    void AddNewPackageItemPacketHelper();
};
