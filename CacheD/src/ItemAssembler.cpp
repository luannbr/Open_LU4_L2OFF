#include "Shared/src/Log.h"

#include "ItemAssembler.h"

#include "CacheSocket.h"

#include "L2Core/src/inc/Guard.h"
#include "Portable/ProcessRuntime.h"
#include "Runtime.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>

CItemDisassemblerFromL2Server::CItemDisassemblerFromL2Server() { L2_GUARD_FUNCTION(); }

const unsigned char* CItemDisassemblerFromL2Server::DisassembleItemAssembleType(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "c", reinterpret_cast<char*>(&m_eAssembleType));
}

const unsigned char* CItemDisassemblerFromL2Server::DisassembleBasicInfo(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "ddQddhdhch", &m_nItemID, &m_nItemType, &m_nAmount, &m_nEnchant, &m_nEroded, &m_nBless, &m_nIdent,
        &m_nWished, reinterpret_cast<char*>(&m_nWarehouse), &m_hLocked
    );
}

const unsigned char* CItemDisassemblerFromL2Server::DisassembleVariation(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddd", &m_nVariation1, &m_nVariation2, &m_nIntensiveItemType);
}

const unsigned char* CItemDisassemblerFromL2Server::DisassembleAttribute(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "hhhhhhhh", &m_Attribute.m_nAttackType, &m_Attribute.m_nAttackValue, &m_Attribute.m_nDefendValue[0],
        &m_Attribute.m_nDefendValue[1], &m_Attribute.m_nDefendValue[2], &m_Attribute.m_nDefendValue[3],
        &m_Attribute.m_nDefendValue[4], &m_Attribute.m_nDefendValue[5]
    );
}

const unsigned char* CItemDisassemblerFromL2Server::DisassembleShapeShift(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "d", &m_nShapeShiftedClassID);
}

const unsigned char* CItemDisassemblerFromL2Server::DisassembleEnsoul(const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    for (int slotType = 1; slotType < 3; ++slotType) {
        int optionCount = 0;
        packet = ::Disassemble(packet, "c", reinterpret_cast<char*>(&optionCount));
        for (int option = 0; option < optionCount; ++option) {
            int slotIndex = 0;
            int optionId = 0;
            packet = ::Disassemble(packet, "dd", &slotIndex, &optionId);
            m_mEnsoulInfo[static_cast<std::size_t>(slotType)].emplace(slotIndex, optionId);
        }
    }
    return packet;
}

const unsigned char* CItemDisassemblerFromL2Server::DisassembleUseCount(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "h", &m_hUseCount);
}

const unsigned char* CItemDisassemblerFromL2Server::DisassembleItemInfoImpl(const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    packet = DisassembleItemAssembleType(packet);
    packet = DisassembleBasicInfo(packet);
    // if ((m_eAssembleType & 0x01U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned int>(ItemAssembleType::VAIRATION)) != 0) {
        packet = DisassembleVariation(packet);
    }
    // if ((m_eAssembleType & 0x02U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned int>(ItemAssembleType::ATTRIBUTE)) != 0) {
        packet = DisassembleAttribute(packet);
    }
    // if ((m_eAssembleType & 0x08U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned int>(ItemAssembleType::SHAPESHIFT)) != 0) {
        packet = DisassembleShapeShift(packet);
    }
    // if ((m_eAssembleType & 0x10U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned int>(ItemAssembleType::ENSOUL)) != 0) {
        packet = DisassembleEnsoul(packet);
    }
    // if ((m_eAssembleType & 0x20U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned int>(ItemAssembleType::USECOUNT)) != 0) {
        packet = DisassembleUseCount(packet);
    }
    return packet;
}

const unsigned char* CItemDisassemblerBuyItem::DisassembleBasicInfo(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    packet = CItemDisassemblerFromL2Server::DisassembleBasicInfo(packet);
    return ::Disassemble(packet, "Qd", &m_nPrice, &m_eConsumeType);
}

const unsigned char* CItemDisassemblerChangeItemData::DisassembleBasicInfo(const unsigned char* packet) {
    L2_GUARD_FUNCTION();
    packet = CItemDisassemblerFromL2Server::DisassembleBasicInfo(packet);
    return ::Disassemble(packet, "d", &m_nChangeType);
}

const unsigned char* CItemDisassemblerWareHouse::DisassembleBasicInfo(const unsigned char* packet) {
    // Current Cached.exe 0x004f20d8.
    L2_GUARD_FUNCTION();
    packet = CItemDisassemblerFromL2Server::DisassembleBasicInfo(packet);
    return ::Disassemble(packet, "d", &m_eConsumeType);
}

const unsigned char* CItemDisassemblerItemMani::DisassembleBasicInfo(const unsigned char* packet) {
    // Current Cached.exe 0x004f2194. Unlike the other derived item
    // disassemblers, the shipping method decodes the complete basic record
    // itself instead of calling the base implementation.
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "ddQddhdddh", &m_nItemID, &m_nItemType, &m_nAmount, &m_nEnchant, &m_nEroded, &m_nBless, &m_eConsumeType,
        &m_nIndex, &m_nGetReason, &m_hLocked
    );
}

const unsigned char* CItemDisassemblerItemMani::DisassembleShapeShift(const unsigned char* packet) {
    // Current Cached.exe 0x004f22a8.
    L2_GUARD_FUNCTION();
    packet = CItemDisassemblerFromL2Server::DisassembleShapeShift(packet);
    return ::Disassemble(packet, "d", &m_nShapeShiftedPeriod);
}

CItemAssemblerSendToL2Server::CItemAssemblerSendToL2Server(CItem& item, unsigned char assembleType) :
    m_eAssembleType(assembleType),
    m_nItemID(item.ItemID()),
    m_nItemType(item.ItemType()),
    m_nAmount(item.Amount()),
    m_nEnchant(item.Enchant()),
    m_nEroded(item.Eroded()),
    m_nBless(static_cast<short>(item.Bless())),
    m_nIdent(item.Ident()),
    m_nWished(static_cast<short>(item.Wished())),
    m_nDuration(item.GetDuration()),
    m_nWarehouse(item.Warehouse()),
    m_hLocked(item.m_hLocked),
    m_nVariation1(item.m_optionKey.variationOption1),
    m_nVariation2(item.m_optionKey.variationOption2),
    m_nIntensiveItemType(item.m_intensiveItemType),
    m_Attribute(item.m_baseAttribute),
    m_nShapeShiftedClassID(item.m_appearanceType),
    m_hUseCount(item.m_useCount) {
    L2_GUARD_FUNCTION();

    m_mEnsoulInfo[1] = item.m_ensoulInfo[1];
    m_mEnsoulInfo[2] = item.m_ensoulInfo[2];
}

CItemAssemblerSendToL2Server::CItemAssemblerSendToL2Server(
    CItem& item, BaseAttribute& attribute, unsigned char assembleType
) :
    m_eAssembleType(assembleType),
    m_nItemID(item.ItemID()),
    m_nItemType(item.ItemType()),
    m_nAmount(item.Amount()),
    m_nEnchant(item.Enchant()),
    m_nEroded(item.Eroded()),
    m_nBless(static_cast<short>(item.Bless())),
    m_nIdent(item.Ident()),
    m_nWished(static_cast<short>(item.Wished())),
    m_nDuration(item.GetDuration()),
    m_nWarehouse(item.Warehouse()),
    m_hLocked(item.m_hLocked),
    m_nVariation1(item.m_optionKey.variationOption1),
    m_nVariation2(item.m_optionKey.variationOption2),
    m_nIntensiveItemType(item.m_intensiveItemType),
    m_Attribute(attribute),
    m_nShapeShiftedClassID(item.m_appearanceType),
    m_hUseCount(item.m_useCount) {
    L2_GUARD_FUNCTION();

    // Current 0x004f2d38 copies the two protocol-visible slot maps beginning
    // at slot type 1. Slot 0 is left empty and AssembleEnsoul
    // serializes only slot types 1 and 2.
    m_mEnsoulInfo[1] = item.m_ensoulInfo[1];
    m_mEnsoulInfo[2] = item.m_ensoulInfo[2];
}

CItemAssemblerAmount::CItemAssemblerAmount(CItem& item, std::int64_t amount, unsigned char assembleType) :
    CItemAssemblerSendToL2Server(item, assembleType) {
    // The current constructor is inlined immediately after the base
    // constructor calls at 0x0061fdb2 and 0x00620be5.
    m_nAmount = amount;
}

int CItemAssemblerSendToL2Server::AssembleItemAssembleType(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble("c", static_cast<int>(static_cast<signed char>(m_eAssembleType)));
}

int CItemAssemblerSendToL2Server::AssembleBasicInfo(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "ddQddhdhdch", static_cast<int>(m_nItemID), static_cast<int>(m_nItemType), static_cast<long long>(m_nAmount),
        m_nEnchant, m_nEroded, static_cast<int>(m_nBless), m_nIdent, static_cast<int>(m_nWished), m_nDuration,
        static_cast<int>(static_cast<signed char>(m_nWarehouse)), static_cast<int>(m_hLocked)
    );
}

CItemAssemblerItemMani::CItemAssemblerItemMani(
    CItem& item,
    int index,
    std::int64_t changeAmount,
    ItemChangeScope changeScope,
    int getReason,
    int shapeShiftedPeriod,
    unsigned char assembleType
) :
    CItemAssemblerSendToL2Server(item, assembleType),
    m_nIndex(index),
    m_nChangeAmount(changeAmount),
    m_eChangeScope(changeScope),
    m_nGetReason(getReason),
    m_nShapeShiftedPeriod(shapeShiftedPeriod) {}

int CItemAssemblerItemMani::AssembleBasicInfo(IPacketHelper& packet) {
    // Current Cached.exe 0x004f1794. Unlike the ordinary item assembler,
    // the manipulate response replaces ident/wished/duration/warehouse with
    // the transaction metadata below.
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "ddQddhdQddh", static_cast<int>(m_nItemID), static_cast<int>(m_nItemType), static_cast<long long>(m_nAmount),
        m_nEnchant, m_nEroded, static_cast<int>(m_nBless), m_nIndex, static_cast<long long>(m_nChangeAmount),
        static_cast<int>(m_eChangeScope), m_nGetReason, static_cast<int>(m_hLocked)
    );
}

int CItemAssemblerItemMani::AssembleShapeShift(IPacketHelper& packet) {
    // Current Cached.exe 0x004f18a4 appends the requested period after the
    // base shape-shift class ID.
    L2_GUARD_FUNCTION();
    const int baseLength = CItemAssemblerSendToL2Server::AssembleShapeShift(packet);
    return baseLength + packet.Assemble("d", m_nShapeShiftedPeriod);
}

int CItemAssemblerSendToL2Server::AssembleVariation(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble("ddd", m_nVariation1, m_nVariation2, m_nIntensiveItemType);
}

int CItemAssemblerSendToL2Server::AssembleAttribute(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hhhhhhhh", static_cast<int>(m_Attribute.m_nAttackType), static_cast<int>(m_Attribute.m_nAttackValue),
        static_cast<int>(m_Attribute.m_nDefendValue[0]), static_cast<int>(m_Attribute.m_nDefendValue[1]),
        static_cast<int>(m_Attribute.m_nDefendValue[2]), static_cast<int>(m_Attribute.m_nDefendValue[3]),
        static_cast<int>(m_Attribute.m_nDefendValue[4]), static_cast<int>(m_Attribute.m_nDefendValue[5])
    );
}

int CItemAssemblerSendToL2Server::AssembleShapeShift(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();

    int shapeShiftedClassId = m_nShapeShiftedClassID;
    if (m_nItemType == 0x115aU) {
        shapeShiftedClassId = (m_nEroded << 16) | static_cast<unsigned short>(m_nEnchant);
    }
    return packet.Assemble("d", shapeShiftedClassId);
}

int CItemAssemblerSendToL2Server::AssembleEnsoul(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();

    int assembledLength = 0;
    for (int slotType = 1; slotType < 3; ++slotType) {
        const CItem::EnsoulInfo& slot = m_mEnsoulInfo[static_cast<std::size_t>(slotType)];
        assembledLength += packet.Assemble("c", static_cast<int>(slot.size()));

        for (int slotIndex = 1; slotIndex <= static_cast<int>(slot.size()); ++slotIndex) {
            int optionId = 0;
            const auto option = slot.find(slotIndex);
            if (option == slot.end()) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[Ensoul] Can't find EnsoulInfo. ItemID[%u], "
                //     "SlotType[%d], SlotIndex[%d]",
                //     m_nItemID, slotType, slotIndex
                // )
                // Log.Add(LOG_ERROR,
                //     "[items] Ensoul slot %d:%d is missing for item %u. Serializing option ID zero.",
                //     slotType, slotIndex, m_nItemID)
                L2_LOG_ERROR(Log,
                    "[items] Ensoul slot %d:%d is missing for item %u; serializing option ID zero.",
                    slotType, slotIndex, m_nItemID);
            } else {
                optionId = option->second;
            }
            assembledLength += packet.Assemble("d", optionId);
        }
    }
    return assembledLength;
}

int CItemAssemblerSendToL2Server::AssembleIUseCount(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble("h", static_cast<int>(m_hUseCount));
}

int CItemAssemblerSendToL2Server::AssembleItemInfoImpl(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();

    int assembledLength = AssembleItemAssembleType(packet);
    assembledLength += AssembleBasicInfo(packet);
    // if ((m_eAssembleType & 0x01U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned int>(ItemAssembleType::VAIRATION)) != 0) {
        assembledLength += AssembleVariation(packet);
    }
    // if ((m_eAssembleType & 0x02U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned int>(ItemAssembleType::ATTRIBUTE)) != 0) {
        assembledLength += AssembleAttribute(packet);
    }
    // if ((m_eAssembleType & 0x08U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned int>(ItemAssembleType::SHAPESHIFT)) != 0) {
        assembledLength += AssembleShapeShift(packet);
    }
    // if ((m_eAssembleType & 0x10U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned int>(ItemAssembleType::ENSOUL)) != 0) {
        assembledLength += AssembleEnsoul(packet);
    }
    // if ((m_eAssembleType & 0x20U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned int>(ItemAssembleType::USECOUNT)) != 0) {
        assembledLength += AssembleIUseCount(packet);
    }
    return assembledLength;
}

CItemAssemblerLoadItem::CItemAssemblerLoadItem(CItem& item, unsigned char assembleType) :
    CItemAssemblerSendToL2Server(item, assembleType), m_nInventorySlotIndex(item.InventorySlotIndex()) {}

int CItemAssemblerLoadItem::AssembleBasicInfo(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return CItemAssemblerSendToL2Server::AssembleBasicInfo(packet) + packet.Assemble("d", m_nInventorySlotIndex);
}

CPackageItemPacketManager::CPackageItemPacketManager() { Init(); }

CPackageItemPacketManager::~CPackageItemPacketManager() { Init(); }

void CPackageItemPacketManager::Init() {
    m_vPackageItemPacketHelperList.clear();
    m_nCurrentPacketHelperIndex = -1;
    m_nTotalPackageItemCount = 0;
    m_nAssembleItemLengthPerItem = 0;
}

void CPackageItemPacketManager::AddNewPackageItemPacketHelper() {
    m_vPackageItemPacketHelperList.emplace_back();
    m_nCurrentPacketHelperIndex = static_cast<int>(m_vPackageItemPacketHelperList.size()) - 1;
}

void CPackageItemPacketManager::CheckLimitPackageItemPacketDataSize() {
    if (!m_vPackageItemPacketHelperList.empty()) {
        const PACKAGE_ITEM_PACKETHELPER& current =
            m_vPackageItemPacketHelperList[static_cast<std::size_t>(m_nCurrentPacketHelperIndex)];
        if (current.ItemPacketHelper.GetLength() + m_nAssembleItemLengthPerItem < 0x4c00) {
            return;
        }
    }
    AddNewPackageItemPacketHelper();
}

int CPackageItemPacketManager::PackageAssemble(const char* format, ...) {
    // Current CacheD 0x004f2370. The source identifier is independently
    // kept by the same ItemAssembler owner in the paired VS2015 PDB.
    PACKAGE_ITEM_PACKETHELPER& current =
        m_vPackageItemPacketHelperList[static_cast<std::size_t>(m_nCurrentPacketHelperIndex)];
    va_list arguments;
    va_start(arguments, format);
    const int assembled = current.ItemPacketHelper.AssembleV(format, arguments);
    va_end(arguments);
    return assembled;
}

int CPackageItemPacketManager::PackageAssembleItem(CItemAssemblerSendToL2Server& itemAssembler) {
    // Current CacheD 0x004f23d0 calls the assembler's +0x40 virtual
    // slot against the current package helper and returns that result.
    PACKAGE_ITEM_PACKETHELPER& current =
        m_vPackageItemPacketHelperList[static_cast<std::size_t>(m_nCurrentPacketHelperIndex)];
    return itemAssembler.AssembleItemInfoImpl(current.ItemPacketHelper);
}

void CPackageItemPacketManager::SendPacket_PackageItemData(CSocket* socket, unsigned char protocol, IPacketHelper& header) {
    if (m_vPackageItemPacketHelperList.empty()) {
        socket->Send(
            "cbdd", static_cast<int>(protocol), header.GetLength(), header.GetPacket(), m_nTotalPackageItemCount, 0
        );
        return;
    }

    for (PACKAGE_ITEM_PACKETHELPER& package : m_vPackageItemPacketHelperList) {
        socket->Send(
            "cbddb", static_cast<int>(protocol), header.GetLength(), header.GetPacket(), m_nTotalPackageItemCount,
            package.nItemCount, package.ItemPacketHelper.GetLength(), package.ItemPacketHelper.GetPacket()
        );
    }
}

int CPackageItemPacketManager::GetTotalPackageItemLength() const {
    // Current Cached.exe 0x005ce90c.
    int totalLength = 0;
    for (const PACKAGE_ITEM_PACKETHELPER& package : m_vPackageItemPacketHelperList) {
        totalLength += package.ItemPacketHelper.GetLength();
    }
    return totalLength;
}
