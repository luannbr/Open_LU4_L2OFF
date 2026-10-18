#include "ItemAssembler.h"

#include "EnchantOption.h"
#include "EnsoulData.h"
#include "ObjectDB.h"
#include "TradeItem.h"
#include "User.h"

#include "Shared/src/ItemMani.h"
#include "Shared/src/SharedFactory.h"
#include "Shared/src/Socket.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>

CItemDisassemblerFromCacheD::CItemDisassemblerFromCacheD()
    // : m_eAssembleType(0),
    :
    m_eAssembleType(ItemAssembleType::NONE),
    m_padding09{},
    m_nDBID(0),
    m_nClassID(0),
    m_padding14{},
    m_nAmount(0),
    m_nEnchanted(0),
    m_nDamaged(0),
    m_nIsBlessed(0),
    m_padding2a{},
    m_nIdent(0),
    m_nWished(0),
    m_padding32{},
    m_nDuration(0),
    m_nWareHouse(0),
    m_padding39{},
    m_hLocked(0),
    m_padding3e{},
    m_nVariation1(0),
    m_nVariation2(0),
    m_nIntensiveItemClassID(0),
    m_padding5e{},
    m_nShapeShiftedClassID(0),
    m_padding64{},
    m_mEnsoulInfo(),
    m_hUseCount(0),
    m_targetTailPadding{} {
    // Current L2Server 0x007ecdd0.
    L2_GUARD_FUNCTION();
}

CItemDisassemblerFromCacheD::~CItemDisassemblerFromCacheD() = default;

const unsigned char* CItemDisassemblerFromCacheD::DisassembleItemAssembleType(const unsigned char* packet) {
    // Current L2Server 0x007eb784.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "c", &m_eAssembleType);
}

const unsigned char* CItemDisassemblerFromCacheD::DisassembleBasicInfo(const unsigned char* packet) {
    // Current L2Server 0x007eb834. m_nWareHouse is a one-byte `c` lane.
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "ddQddhdhdch", &m_nDBID, &m_nClassID, &m_nAmount, &m_nEnchanted, &m_nDamaged, &m_nIsBlessed, &m_nIdent,
        &m_nWished, &m_nDuration, &m_nWareHouse, &m_hLocked
    );
}

const unsigned char* CItemDisassemblerFromCacheD::DisassembleVariation(const unsigned char* packet) {
    // Current L2Server 0x007eb958.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "ddd", &m_nVariation1, &m_nVariation2, &m_nIntensiveItemClassID);
}

const unsigned char* CItemDisassemblerFromCacheD::DisassembleAttribute(const unsigned char* packet) {
    // Current L2Server 0x007eba14. The ninth BaseAttribute lane is not
    // serialized and keeps its constructor value.
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "hhhhhhhh", &m_Attribute.m_nAttackType, &m_Attribute.m_nAttackValue, &m_Attribute.m_nDefendValue[0],
        &m_Attribute.m_nDefendValue[1], &m_Attribute.m_nDefendValue[2], &m_Attribute.m_nDefendValue[3],
        &m_Attribute.m_nDefendValue[4], &m_Attribute.m_nDefendValue[5]
    );
}

const unsigned char* CItemDisassemblerFromCacheD::DisassembleShapeShift(const unsigned char* packet) {
    // Current L2Server 0x007ebb00.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "d", &m_nShapeShiftedClassID);
}

const unsigned char* CItemDisassemblerFromCacheD::DisassembleEnsoul(const unsigned char* packet) {
    // Current L2Server 0x007ec924. Slot zero is untouched.
    L2_GUARD_FUNCTION();
    for (int slotType = 1; slotType < 3; ++slotType) {
        std::int8_t optionCount = 0;
        packet = ::Disassemble(packet, "c", &optionCount);
        for (int slotIndex = 1; slotIndex <= optionCount; ++slotIndex) {
            std::int32_t optionId = 0;
            packet = ::Disassemble(packet, "d", &optionId);
            const auto found = g_mEnsoulOptionData.find(optionId);
            if (found != g_mEnsoulOptionData.end()) {
                m_mEnsoulInfo[slotType].emplace(slotIndex, found->second);
            }
        }
    }
    return packet;
}

const unsigned char* CItemDisassemblerFromCacheD::DisassembleUseCount(const unsigned char* packet) {
    // Current L2Server 0x007ebbb0.
    L2_GUARD_FUNCTION();
    return ::Disassemble(packet, "h", &m_hUseCount);
}

const unsigned char* CItemDisassemblerFromCacheD::DisassembleItemInfoImpl(const unsigned char* packet) {
    // Current L2Server 0x007ebc64. The type lane is parsed directly here.
    // the standalone virtual endpoint remains separately published.
    L2_GUARD_FUNCTION();
    packet = ::Disassemble(packet, "c", &m_eAssembleType);
    packet = DisassembleBasicInfo(packet);
    // if ((m_eAssembleType & 0x01U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::VAIRATION)) != 0) {
        packet = DisassembleVariation(packet);
    }
    // if ((m_eAssembleType & 0x02U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::ATTRIBUTE)) != 0) {
        packet = DisassembleAttribute(packet);
    }
    // if ((m_eAssembleType & 0x08U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::SHAPESHIFT)) != 0) {
        packet = DisassembleShapeShift(packet);
    }
    // if ((m_eAssembleType & 0x10U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::ENSOUL)) != 0) {
        packet = DisassembleEnsoul(packet);
    }
    // if ((m_eAssembleType & 0x20U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::USECOUNT)) != 0) {
        packet = DisassembleUseCount(packet);
    }
    return packet;
}

CItemDisassemblerLoadItem::CItemDisassemblerLoadItem() : m_nIndex(0), m_paddingD4{} {}

const unsigned char* CItemDisassemblerLoadItem::DisassembleBasicInfo(const unsigned char* packet) {
    // Current L2Server.exe 0x007ebd94 (188 bytes). The load-items envelope
    // appends its inventory index to the complete base basic-info record.
    L2_GUARD_FUNCTION();
    packet = CItemDisassemblerFromCacheD::DisassembleBasicInfo(packet);
    return ::Disassemble(packet, "d", &m_nIndex);
}

CItemDisassemblerItemMani::CItemDisassemblerItemMani() :
    m_nIndex(0),
    m_paddingD4{},
    m_nChangeAmount(0),
    m_eChangeScope(0),
    m_nGetReason(0),
    m_nShapeShiftedPeriod(0),
    m_paddingEc{} {}

const unsigned char* CItemDisassemblerItemMani::DisassembleBasicInfo(const unsigned char* packet) {
    // Current L2Server.exe 0x007ebe50. Unlike the load-item specialization,
    // this reply owns the complete basic envelope and does not call the base
    // basic-info slot.
    L2_GUARD_FUNCTION();
    return ::Disassemble(
        packet, "ddQddhdQddh", &m_nDBID, &m_nClassID, &m_nAmount, &m_nEnchanted, &m_nDamaged, &m_nIsBlessed, &m_nIndex,
        &m_nChangeAmount, &m_eChangeScope, &m_nGetReason, &m_hLocked
    );
}

const unsigned char* CItemDisassemblerItemMani::DisassembleShapeShift(const unsigned char* packet) {
    // Current L2Server.exe 0x007ebf80.
    L2_GUARD_FUNCTION();
    packet = CItemDisassemblerFromCacheD::DisassembleShapeShift(packet);
    return ::Disassemble(packet, "d", &m_nShapeShiftedPeriod);
}

CItemAssemblerSendToCacheD::CItemAssemblerSendToCacheD(const unsigned char assembleType) :
    m_eAssembleType(assembleType),
    m_nDBID(0),
    m_nClassID(0),
    m_nAmount(0),
    m_nEnchanted(0),
    m_nDamaged(0),
    m_nIsBlessed(0),
    m_nIdent(0),
    m_nWished(0),
    m_padding32{},
    m_nWareHouse(0),
    m_hLocked(0),
    m_nVariation1(0),
    m_nVariation2(0),
    m_nIntensiveItemClassID(0),
    m_nShapeShiftedClassID(0),
    m_hUseCount(0) {
    // Current L2Server 0x007eccbc clears only the two published ensoul
    // lanes after their independent map construction.
    L2_GUARD_FUNCTION();
    m_mEnsoulInfo[1].clear();
    m_mEnsoulInfo[2].clear();
}

CItemAssemblerSendToCacheD::CItemAssemblerSendToCacheD(CItem& item, const unsigned char assembleType) :
    m_eAssembleType(assembleType),
    m_nDBID(static_cast<int>(item.GetDBID())),
    m_nClassID(item.m_data->m_nClassID),
    m_nAmount(item.m_data->m_nCount),
    m_nEnchanted(item.m_data->GetEnchanted()),
    m_nDamaged(item.GetItemDamage()),
    m_nIsBlessed(item.m_data->m_bBlessed),
    m_nIdent(0),
    m_nWished(0),
    m_padding32{},
    m_nWareHouse(0),
    m_hLocked(item.m_data->GetLocked()),
    m_nVariation1(item.m_itemOption.itemOptionKey.variationOption1),
    m_nVariation2(item.m_itemOption.itemOptionKey.variationOption2),
    m_nIntensiveItemClassID(item.m_itemOption.intensiveItemClassID),
    m_Attribute(item.GetTotalAttribute()),
    m_nShapeShiftedClassID(item.m_data->GetShapeShiftedClassId()),
    m_hUseCount(item.m_data->GetUseCount()) {
    // Current L2Server 0x007edf5c is a complete, non-delegating owner.
    L2_GUARD_FUNCTION();
    m_mEnsoulInfo[1] = item.m_mEnsoulInfo[1];
    m_mEnsoulInfo[2] = item.m_mEnsoulInfo[2];
}

CItemAssemblerSendToCacheD::CItemAssemblerSendToCacheD(
    CItem& item, BaseAttribute& attribute, const unsigned char assembleType
) :
    m_eAssembleType(assembleType),
    m_nDBID(static_cast<int>(item.GetDBID())),
    m_nClassID(item.m_data->m_nClassID),
    m_nAmount(item.m_data->m_nCount),
    m_nEnchanted(item.m_data->GetEnchanted()),
    m_nDamaged(item.GetItemDamage()),
    m_nIsBlessed(item.m_data->m_bBlessed),
    m_nIdent(0),
    m_nWished(0),
    m_padding32{},
    m_nWareHouse(0),
    m_hLocked(item.m_data->GetLocked()),
    m_nVariation1(item.m_itemOption.itemOptionKey.variationOption1),
    m_nVariation2(item.m_itemOption.itemOptionKey.variationOption2),
    m_nIntensiveItemClassID(item.m_itemOption.intensiveItemClassID),
    m_Attribute(attribute),
    m_nShapeShiftedClassID(item.m_data->GetShapeShiftedClassId()),
    m_hUseCount(item.m_data->GetUseCount()) {
    // Current L2Server 0x007ee128 is the parallel non-delegating
    // BaseAttribute-reference overload.
    L2_GUARD_FUNCTION();
    m_mEnsoulInfo[1] = item.m_mEnsoulInfo[1];
    m_mEnsoulInfo[2] = item.m_mEnsoulInfo[2];
}

CItemAssemblerSendToCacheD::~CItemAssemblerSendToCacheD() = default;

int CItemAssemblerSendToCacheD::AssembleItemAssembleType(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble("c", static_cast<int>(static_cast<std::int8_t>(m_eAssembleType)));
}

int CItemAssemblerSendToCacheD::AssembleBasicInfo(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "ddQddhdhch", m_nDBID, m_nClassID, m_nAmount, m_nEnchanted, m_nDamaged, static_cast<int>(m_nIsBlessed), m_nIdent,
        static_cast<int>(m_nWished), static_cast<int>(static_cast<std::int8_t>(m_nWareHouse)), static_cast<int>(m_hLocked)
    );
}

int CItemAssemblerSendToCacheD::AssembleVariation(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble("ddd", m_nVariation1, m_nVariation2, m_nIntensiveItemClassID);
}

int CItemAssemblerSendToCacheD::AssembleAttribute(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hhhhhhhh", static_cast<int>(m_Attribute.m_nAttackType), static_cast<int>(m_Attribute.m_nAttackValue),
        static_cast<int>(m_Attribute.m_nDefendValue[0]), static_cast<int>(m_Attribute.m_nDefendValue[1]),
        static_cast<int>(m_Attribute.m_nDefendValue[2]), static_cast<int>(m_Attribute.m_nDefendValue[3]),
        static_cast<int>(m_Attribute.m_nDefendValue[4]), static_cast<int>(m_Attribute.m_nDefendValue[5])
    );
}

int CItemAssemblerSendToCacheD::AssembleShapeShift(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble("d", m_nShapeShiftedClassID);
}

CItemAssemblerWareHouse::CItemAssemblerWareHouse(
    CItem& item,
    const int databaseId,
    const std::int64_t amount,
    const ::ItemConsumeType consumeType,
    const unsigned char assembleType
) :
    CItemAssemblerSendToCacheD(item, assembleType) {
    // Current L2Server 0x007ee494. The warehouse request deliberately
    // replaces the item's own DBID and amount with the requested pair.
    L2_GUARD_FUNCTION();
    m_eConsumeType = consumeType;
    m_nDBID = databaseId;
    m_nAmount = amount;
}

int CItemAssemblerWareHouse::AssembleBasicInfo(IPacketHelper& packet) {
    // Current L2Server 0x007eb500.
    L2_GUARD_FUNCTION();
    const int baseResult = CItemAssemblerSendToCacheD::AssembleBasicInfo(packet);
    return baseResult + packet.Assemble("d", static_cast<std::int32_t>(m_eConsumeType));
}

int CItemAssemblerSendToCacheD::AssembleEnsoul(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    int length = 0;
    for (int slotType = 1; slotType < 3; ++slotType) {
        const auto& options = m_mEnsoulInfo[slotType];
        length += packet.Assemble("c", static_cast<int>(options.size()));
        for (int slotIndex = 1; static_cast<std::size_t>(slotIndex) <= options.size(); ++slotIndex) {
            const auto it = options.find(slotIndex);
            if (it != options.end() && it->second != nullptr) {
                length += packet.Assemble("dd", it->first, it->second->m_nId);
            }
        }
    }
    return length;
}

int CItemAssemblerSendToCacheD::AssembleUseCount(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble("h", static_cast<int>(m_hUseCount));
}

int CItemAssemblerSendToCacheD::AssembleItemInfoImpl(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    int length = AssembleItemAssembleType(packet);
    length += AssembleBasicInfo(packet);
    // if ((m_eAssembleType & 0x01U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::VAIRATION)) != 0) {
        length += AssembleVariation(packet);
    }
    // if ((m_eAssembleType & 0x02U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::ATTRIBUTE)) != 0) {
        length += AssembleAttribute(packet);
    }
    // if ((m_eAssembleType & 0x08U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::SHAPESHIFT)) != 0) {
        length += AssembleShapeShift(packet);
    }
    // if ((m_eAssembleType & 0x10U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::ENSOUL)) != 0) {
        length += AssembleEnsoul(packet);
    }
    // if ((m_eAssembleType & 0x20U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::USECOUNT)) != 0) {
        length += AssembleUseCount(packet);
    }
    return length;
}

CItemAssemblerBuyItem::CItemAssemblerBuyItem(
    CItem& item,
    const std::int64_t amount,
    const std::int64_t price,
    BaseAttribute& attribute,
    const unsigned char assembleType
) :
    CItemAssemblerSendToCacheD(item, attribute, assembleType) {
    // Current L2Server 0x007ee2f8.
    L2_GUARD_FUNCTION();
    m_nAmount = amount;
    m_nPrice = price;
    m_eConsumeType = item.m_data->m_ItemConsumeType;
}

int CItemAssemblerBuyItem::AssembleBasicInfo(IPacketHelper& packet) {
    // Current L2Server 0x007eb370.
    L2_GUARD_FUNCTION();
    int length = CItemAssemblerSendToCacheD::AssembleBasicInfo(packet);
    length += packet.Assemble("Qd", m_nPrice, static_cast<int>(m_eConsumeType));
    return length;
}

CItemAssemblerChangeItemData::CItemAssemblerChangeItemData(
    CItem& item, const int changeType, const std::int64_t amount, BaseAttribute& attribute, const unsigned char assembleType
) :
    CItemAssemblerSendToCacheD(item, attribute, assembleType) {
    // Current L2Server 0x007ee3d4.
    L2_GUARD_FUNCTION();
    m_nChangeType = changeType;
    m_nAmount = amount;
}

int CItemAssemblerChangeItemData::AssembleBasicInfo(IPacketHelper& packet) {
    // Current L2Server 0x007eb43c.
    L2_GUARD_FUNCTION();
    int length = CItemAssemblerSendToCacheD::AssembleBasicInfo(packet);
    length += packet.Assemble("d", m_nChangeType);
    return length;
}

CItemAssemblerItemMani::CItemAssemblerItemMani(
    CItem& item, InventoryParams& parameters, const int index, const unsigned char assembleType
) :
    CItemAssemblerSendToCacheD(assembleType) {
    // Current L2Server 0x007ee5ac writes the complete derived state only
    // after the base constructor and this guard boundary.
    L2_GUARD_FUNCTION();
    m_nDBID = item.GetDBID() == 0 ? 0 : static_cast<int>(item.GetDBID());
    m_nClassID = item.m_data->m_nClassID;
    m_nAmount = parameters.nCount;
    m_nEnchanted = std::max(parameters.nEnchanted, 0);
    m_nDamaged = parameters.nEroded;
    m_nIsBlessed = parameters.nBless;
    m_eConsumeType = item.m_data->m_ItemConsumeType;
    m_nIndex = index;
    m_nGetReason = static_cast<int>(parameters.m_Reason);
    m_hLocked = item.m_data->GetLocked();
    m_nVariation1 = parameters.key.variationOption1;
    m_nVariation2 = parameters.key.variationOption2;
    m_nIntensiveItemClassID = parameters.nIntensiveItemClassID;
    m_Attribute.m_nAttackType = parameters.attribute.m_nAttackType;
    m_Attribute.m_nAttackValue = parameters.attribute.m_nAttackValue;
    for (int attributeIndex = 0; attributeIndex < 6; ++attributeIndex) {
        m_Attribute.m_nDefendValue[attributeIndex] = parameters.attribute.m_nDefendValue[attributeIndex];
    }
    m_nShapeShiftedClassID = parameters.nShapeShiftedClassId;
    m_nShapeShiftedPeriod = parameters.nShapeShiftedPeriod;
    m_mEnsoulInfo[1] = parameters.mEnsoulOptionMap[1];
    m_mEnsoulInfo[2] = parameters.mEnsoulOptionMap[2];
    m_hUseCount = item.m_data->GetUseCount();
}

int CItemAssemblerItemMani::AssembleBasicInfo(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "ddQddhdddh", m_nDBID, m_nClassID, m_nAmount, m_nEnchanted, m_nDamaged, static_cast<int>(m_nIsBlessed),
        static_cast<int>(m_eConsumeType), m_nIndex, m_nGetReason, static_cast<int>(m_hLocked)
    );
}

int CItemAssemblerItemMani::AssembleShapeShift(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    int length = CItemAssemblerSendToCacheD::AssembleShapeShift(packet);
    length += packet.Assemble("d", m_nShapeShiftedPeriod);
    return length;
}

CItemAssemblerSendToCLNT::CItemAssemblerSendToCLNT()
    // : m_eAssembleType(0),
    :
    m_eAssembleType(ItemAssembleType::NONE),
    m_nServerID(0),
    m_nClassID(0),
    m_nSlotIndex(0),
    m_nAmount(0),
    m_eItemType(IT_WEAPON),
    m_nIsBlessed(0),
    m_bIsEquipped(false),
    m_eSlotBitType(0),
    m_nEnchanted(0),
    m_nDamaged(0),
    m_nDuration(-1),
    m_nPeriodLeft(-9999),
    m_nUseable(0),
    m_hLocked(0),
    m_nVariation1(0),
    m_nVariation2(0),
    m_nShapeShiftedClassID(0),
    m_mEnsoulInfo(),
    m_eAction(ITEM_ACTION_MAX),
    m_hUseCount(0),
    m_nReuseDelay(0),
    m_nRemainTimeMs(0) {
    L2_GUARD_FUNCTION();
}

CItemAssemblerSendToCLNT::CItemAssemblerSendToCLNT(CItem& item)
    // : m_eAssembleType(0),
    :
    m_eAssembleType(ItemAssembleType::NONE),
    m_nServerID(static_cast<int>(item.Id())),
    m_nClassID(item.m_data->m_nClassID),
    m_nSlotIndex(item.m_data->m_nInventorySlotIndex),
    m_nAmount(item.m_data->m_nCount),
    m_eItemType(item.m_data->m_Type),
    m_nIsBlessed(static_cast<std::int16_t>(item.m_data->m_bBlessed)),
    m_bIsEquipped(false),
    m_eSlotBitType(item.m_data->m_Slot),
    m_nEnchanted(static_cast<std::int16_t>(item.m_data->GetEnchanted())),
    m_nDamaged(item.GetItemDamage()),
    m_nDuration(item.GetDuration()),
    m_nPeriodLeft(-9999),
    m_nUseable(item.m_Useable),
    m_hLocked(item.m_data->GetLocked()),
    m_nVariation1(item.m_itemOption.itemOptionKey.variationOption1),
    m_nVariation2(item.m_itemOption.itemOptionKey.variationOption2),
    m_Attribute(item.GetTotalAttribute()),
    m_nShapeShiftedClassID(item.m_data->GetShapeShiftedClassId()),
    m_mEnsoulInfo(),
    m_eAction(item.m_const->m_enumDefaultAction),
    m_hUseCount(item.m_data->GetUseCount()),
    m_nReuseDelay(item.m_const->m_nReuseDelay),
    m_nRemainTimeMs(0) {
    L2_GUARD_FUNCTION();
    m_mEnsoulInfo[1] = item.m_mEnsoulInfo[1];
    m_mEnsoulInfo[2] = item.m_mEnsoulInfo[2];
}

CItemAssemblerSendToCLNT::~CItemAssemblerSendToCLNT() = default;

CItemAssemblerAmount::CItemAssemblerAmount(CItem& item, const std::int64_t amount) : CItemAssemblerSendToCLNT(item) {
    m_nAmount = amount;
}

CItemAssemblerAmount::CItemAssemblerAmount(CItem& item, const std::int64_t amount, BaseAttribute& attribute) :
    CItemAssemblerSendToCLNT(item) {
    m_nAmount = amount;
    m_Attribute = attribute;
}

CItemAssemblerPrivateStore::CItemAssemblerPrivateStore(
    CItem& item, CPrivateStoreData& storeData, std::int64_t amount, BaseAttribute& attribute
) {
    // Current L2Server 0x007edb84.
    L2_GUARD_FUNCTION();
    m_nServerID = storeData.nItemServerId;
    m_nClassID = storeData.nClassId;
    m_nAmount = amount;
    m_eItemType = item.m_data->m_Type;
    m_nIsBlessed = static_cast<std::int16_t>(storeData.nIsBlessed);
    m_eSlotBitType = item.m_data->m_Slot;
    m_nEnchanted = static_cast<std::int16_t>(storeData.nEnchanted);
    m_nDamaged = storeData.GetItemDamage();
    m_nDuration = item.GetDuration();
    m_nVariation1 = storeData.nVariation1;
    m_nVariation2 = storeData.nVariation2;
    m_Attribute = attribute;
    m_nUseable = 1;
    m_nShapeShiftedClassID = storeData.nShapeShiftingClassId;
    m_mEnsoulInfo[1] = item.m_mEnsoulInfo[1];
    m_mEnsoulInfo[2] = item.m_mEnsoulInfo[2];
}

CItemAssemblerCommission::CItemAssemblerCommission(CCommissionItem* const item) {
    // Current L2Server 0x007edd58.
    L2_GUARD_FUNCTION();
    m_nClassID = static_cast<int>(item->m_nItemClassID);
    m_nAmount = item->m_nAmount;
    m_nEnchanted = static_cast<std::int16_t>(item->m_nEnchant);
    m_nDamaged = static_cast<std::int16_t>(item->m_nDamaged);
    m_nIsBlessed = static_cast<std::int16_t>(item->m_bBless);
    m_nVariation1 = item->m_nVariationOpt1;
    m_nVariation2 = item->m_nVariationOpt2;
    m_nDuration = item->m_nDuration;
    m_Attribute.m_nAttackType = item->m_nBaseAttributeAttackType;
    m_Attribute.m_nAttackValue = item->m_nBaseAttributeAttackValue;
    std::copy_n(item->m_nBaseAttributeDefendValue, std::size(m_Attribute.m_nDefendValue), m_Attribute.m_nDefendValue);

    CObject* const object = g_ObjectDB.GetObjectW(m_nClassID, false);
    CItem* const itemObject = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
    if (itemObject == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] GetItem Failed. Item class[%d] nCount[%I64d]",
        //     L"ItemAssembler.cpp",
        //     0x4b7,
        //     m_nClassID,
        //     m_nAmount)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] GetItem Failed. Item class[%d] nCount[%I64d]",
        //     L"ItemAssembler.cpp",
        //     0x4b7,
        //     m_nClassID,
        //     m_nAmount)
        L2_LOG_ERROR(
            Log,
            "Cannot resolve commission item template %d (amount %lld): object missing or not an item; client item assembly "
            "continues without template attributes.",
            m_nClassID, static_cast<long long>(m_nAmount)
        );
    } else {
        itemObject->m_Useable = 1;
        m_eItemType = itemObject->m_data->m_Type;
        m_eSlotBitType = itemObject->m_data->m_Slot;
        m_Attribute += itemObject->GetTotalAttribute();
        m_nUseable = itemObject->m_Useable;
    }
    m_nShapeShiftedClassID = item->m_nShapeShiftingClassId;
    m_mEnsoulInfo[1] = item->mEnsoulOptionMap[1];
    m_mEnsoulInfo[2] = item->mEnsoulOptionMap[2];
}

bool CItemAssemblerSendToCLNT::IsAssembleVariation() {
    L2_GUARD_FUNCTION();
    return m_nVariation1 != 0 || m_nVariation2 != 0;
}

bool CItemAssemblerSendToCLNT::IsAssembleAttribute() {
    L2_GUARD_FUNCTION();
    return m_Attribute.m_nAttackType != ATTRT_NONE || m_Attribute.m_nAttackValue > 0 ||
        std::any_of(std::begin(m_Attribute.m_nDefendValue), std::end(m_Attribute.m_nDefendValue), [](short value) {
            return value > 0;
        });
}

bool CItemAssemblerSendToCLNT::IsAssembleEnchantOption() {
    L2_GUARD_FUNCTION();
    const CEnchantOption* const option =
        CEnchantOptionDB::Inst().GetEnchantOption(m_nClassID, static_cast<int>(m_nEnchanted));
    if (option == nullptr) {
        return false;
    }
    const EnchantOptionId& id = option->GetOptionId();
    return id.at[0] != 0 || id.at[1] != 0 || id.at[2] != 0;
}

bool CItemAssemblerSendToCLNT::IsAssembleShapeShift() {
    L2_GUARD_FUNCTION();
    return m_nClassID == 0x115a || m_nShapeShiftedClassID != 0;
}

bool CItemAssemblerSendToCLNT::IsAssembleEnsoul() {
    L2_GUARD_FUNCTION();
    return !m_mEnsoulInfo[1].empty() || !m_mEnsoulInfo[2].empty();
}

bool CItemAssemblerSendToCLNT::IsAssembleUseCount() {
    L2_GUARD_FUNCTION();
    return m_eAction == ITEM_ACTION_USE_COUNT;
}

bool CItemAssemblerSendToCLNT::IsAssembleReuseDelay() {
    L2_GUARD_FUNCTION();
    return m_nReuseDelay != 0;
}

int CItemAssemblerSendToCLNT::AssembleItemAssembleType(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    // m_eAssembleType = 0.
    m_eAssembleType = ItemAssembleType::NONE;
    if (IsAssembleVariation()) {
        // m_eAssembleType |= 0x01.
        m_eAssembleType |= ItemAssembleType::VAIRATION;
    }
    if (IsAssembleAttribute()) {
        // m_eAssembleType |= 0x02.
        m_eAssembleType |= ItemAssembleType::ATTRIBUTE;
    }
    if (IsAssembleEnchantOption()) {
        // m_eAssembleType |= 0x04.
        m_eAssembleType |= ItemAssembleType::ENCHANTOPTION;
    }
    if (IsAssembleShapeShift()) {
        // m_eAssembleType |= 0x08.
        m_eAssembleType |= ItemAssembleType::SHAPESHIFT;
    }
    if (IsAssembleEnsoul()) {
        // m_eAssembleType |= 0x10.
        m_eAssembleType |= ItemAssembleType::ENSOUL;
    }
    if (IsAssembleUseCount()) {
        // m_eAssembleType |= 0x20.
        m_eAssembleType |= ItemAssembleType::USECOUNT;
    }
    if (IsAssembleReuseDelay()) {
        // m_eAssembleType |= 0x40.
        m_eAssembleType |= ItemAssembleType::REUSEDELAY;
    }
    return packet.Assemble("c", static_cast<int>(m_eAssembleType));
}

int CItemAssemblerSendToCLNT::AssembleBasicInfo(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "ddcQcchQccddch", m_nServerID, m_nClassID, m_nSlotIndex, m_nAmount,
        static_cast<int>(static_cast<std::int8_t>(m_eItemType)), static_cast<int>(static_cast<std::int8_t>(m_nIsBlessed)),
        m_bIsEquipped, m_eSlotBitType, static_cast<int>(static_cast<std::int8_t>(m_nEnchanted)),
        static_cast<int>(static_cast<std::int8_t>(m_nDamaged)), m_nDuration, m_nPeriodLeft,
        static_cast<int>(static_cast<std::int8_t>(m_nUseable)), static_cast<int>(m_hLocked)
    );
}

int CItemAssemblerSendToCLNT::AssembleVariation(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble("dd", m_nVariation1, m_nVariation2);
}

int CItemAssemblerSendToCLNT::AssembleAttribute(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble(
        "hhhhhhhh", static_cast<int>(m_Attribute.m_nAttackType), static_cast<int>(m_Attribute.m_nAttackValue),
        static_cast<int>(m_Attribute.m_nDefendValue[0]), static_cast<int>(m_Attribute.m_nDefendValue[1]),
        static_cast<int>(m_Attribute.m_nDefendValue[2]), static_cast<int>(m_Attribute.m_nDefendValue[3]),
        static_cast<int>(m_Attribute.m_nDefendValue[4]), static_cast<int>(m_Attribute.m_nDefendValue[5])
    );
}

int CItemAssemblerSendToCLNT::AssembleEnchantOption(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    const CEnchantOption* const option =
        CEnchantOptionDB::Inst().GetEnchantOption(m_nClassID, static_cast<int>(m_nEnchanted));
    const EnchantOptionId* const id = option != nullptr ? &option->GetOptionId() : nullptr;
    int length = 0;
    for (int index = 0; index < 3; ++index) {
        length += packet.Assemble("d", id != nullptr ? id->at[index] : 0);
    }
    return length;
}

int CItemAssemblerSendToCLNT::AssembleShapeShift(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    if (m_nClassID == 0x115a) {
        const std::uint32_t combined = static_cast<std::uint16_t>(m_nEnchanted) |
            (static_cast<std::uint32_t>(static_cast<std::uint16_t>(m_nDamaged)) << 16);
        return packet.Assemble("d", combined);
    }
    return packet.Assemble("d", m_nShapeShiftedClassID);
}

int CItemAssemblerSendToCLNT::AssembleEnsoul(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    int length = 0;
    for (int slotType = 1; slotType < 3; ++slotType) {
        const auto& options = m_mEnsoulInfo[slotType];
        length += packet.Assemble("c", static_cast<int>(options.size()));
        for (int slotIndex = 1; slotIndex <= static_cast<int>(options.size()); ++slotIndex) {
            const auto it = options.find(slotIndex);
            const int optionId = it != options.end() && it->second != nullptr ? it->second->m_nId : 0;
            length += packet.Assemble("d", optionId);
        }
    }
    return length;
}

int CItemAssemblerSendToCLNT::AssembleUseCount(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble("h", static_cast<int>(m_hUseCount));
}

int CItemAssemblerSendToCLNT::AssembleReuseDelay(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    return packet.Assemble("d", m_nRemainTimeMs);
}

int CItemAssemblerSendToCLNT::AssembleItemInfoImpl(IPacketHelper& packet) {
    L2_GUARD_FUNCTION();
    int length = AssembleItemAssembleType(packet);
    length += AssembleBasicInfo(packet);
    // if ((m_eAssembleType & 0x01U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::VAIRATION)) != 0) {
        length += AssembleVariation(packet);
    }
    // if ((m_eAssembleType & 0x02U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::ATTRIBUTE)) != 0) {
        length += AssembleAttribute(packet);
    }
    // if ((m_eAssembleType & 0x04U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::ENCHANTOPTION)) != 0) {
        length += AssembleEnchantOption(packet);
    }
    // if ((m_eAssembleType & 0x08U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::SHAPESHIFT)) != 0) {
        length += AssembleShapeShift(packet);
    }
    // if ((m_eAssembleType & 0x10U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::ENSOUL)) != 0) {
        length += AssembleEnsoul(packet);
    }
    // if ((m_eAssembleType & 0x20U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::USECOUNT)) != 0) {
        length += AssembleUseCount(packet);
    }
    // if ((m_eAssembleType & 0x40U) != 0) {
    if ((m_eAssembleType & static_cast<unsigned>(ItemAssembleType::REUSEDELAY)) != 0) {
        length += AssembleReuseDelay(packet);
    }
    return length;
}

int CItemAssemblerFromDB::AssembleItemInfoImpl(IPacketHelper& packet) {
    // Current L2Server 0x007eabe0.
    L2_GUARD_FUNCTION();
    int length = CItemAssemblerSendToCLNT::AssembleItemInfoImpl(packet);
    length += packet.Assemble("d", m_nItemDBID);
    return length;
}

void CItemAssemblerFromDB::SetItemConst(CItem& item) {
    // Current L2Server 0x007eaca4.
    L2_GUARD_FUNCTION();
    m_eItemType = item.m_data->m_Type;
    m_eSlotBitType = item.m_data->m_Slot;
    m_Attribute += item.GetTotalAttribute();
    m_nUseable = item.m_Useable;
    m_eAction = item.m_const->m_enumDefaultAction;
    m_nReuseDelay = item.m_const->m_nReuseDelay;
}

const unsigned char* CItemAssemblerFromDB::DisassembleFromDB(User& user, const unsigned char* packet) {
    // Current L2Server 0x007ed864.
    L2_GUARD_FUNCTION();

    CItemDisassemblerFromCacheD item;
    packet = item.DisassembleItemInfo(packet);
    m_nItemDBID = item.m_nDBID;
    m_nClassID = item.m_nClassID;
    m_nAmount = item.m_nAmount;
    m_nIsBlessed = item.m_nIsBlessed;
    m_nEnchanted = static_cast<std::int16_t>(item.m_nEnchanted);
    m_nDamaged = static_cast<std::int16_t>(item.m_nDamaged);
    m_nDuration = item.m_nDuration;
    m_hLocked = item.m_hLocked;
    m_nVariation1 = item.m_nVariation1;
    m_nVariation2 = item.m_nVariation2;
    m_Attribute = item.m_Attribute;
    m_nShapeShiftedClassID = item.m_nShapeShiftedClassID;
    m_mEnsoulInfo[1] = item.m_mEnsoulInfo[1];
    m_mEnsoulInfo[2] = item.m_mEnsoulInfo[2];
    m_hUseCount = item.m_hUseCount;

    CObject* const object = g_ObjectDB.GetObjectW(m_nClassID, false);
    CItem* const itemObject = static_cast<CItem*>(object);
    if (itemObject == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] GetItem Failed. Item class[%d] nCount[%I64d]",
        //     L"ItemAssembler.cpp",
        //     0x416,
        //     m_nClassID,
        //     m_nAmount)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] GetItem Failed. Item class[%d] nCount[%I64d]",
        //     L"ItemAssembler.cpp",
        //     0x416,
        //     m_nClassID,
        //     m_nAmount)
        L2_LOG_ERROR(
            Log,
            "Cannot resolve item template %d for DB item %u (amount %lld), character %u; client item assembly continues "
            "without template and remaining-time data.",
            m_nClassID, static_cast<unsigned>(m_nItemDBID), static_cast<long long>(m_nAmount),
            static_cast<unsigned>(user.GetDBID())
        );
    } else {
        itemObject->m_Useable = 1;
        SetItemConst(*itemObject);
        m_nPeriodLeft = user.GetPeriodLeft(m_nItemDBID, m_nShapeShiftedClassID);
        m_nRemainTimeMs = static_cast<std::int32_t>(user.GetItemRemindTimeMs(m_nItemDBID));
    }
    return packet;
}

CItemAssembler::CItemAssembler(User& user, CItem& item, bool equipped) : CItemAssemblerSendToCLNT(item) {
    L2_GUARD_FUNCTION();
    m_bIsEquipped = equipped;
    m_nPeriodLeft = user.GetPeriodLeft(item);
    m_nRemainTimeMs = user.GetItemRemindTimeMs(item.m_data->m_nDBID);
}

CPackageItemPacketManager::CPackageItemPacketManager() :
    m_nCurrentPacketHelperIndex(-1),
    m_nTotalPackageItemCount(0),
    m_nAssembleItemLengthPerItem(0) {
    L2_GUARD_FUNCTION();
    Init();
}

CPackageItemPacketManager::~CPackageItemPacketManager() {
    L2_GUARD_FUNCTION();
    Init();
}

void CPackageItemPacketManager::Init() {
    L2_GUARD_FUNCTION();
    m_vPackageItemPacketHelperList.clear();
    m_nCurrentPacketHelperIndex = -1;
    m_nTotalPackageItemCount = 0;
    m_nAssembleItemLengthPerItem = 0;
}

void CPackageItemPacketManager::AddNewPackageItemPacketHelper() {
    L2_GUARD_FUNCTION();
    PACKAGE_ITEM_PACKETHELPER packetHelper;
    m_vPackageItemPacketHelperList.push_back(packetHelper);
    m_nCurrentPacketHelperIndex = static_cast<int>(m_vPackageItemPacketHelperList.size()) - 1;
}

void CPackageItemPacketManager::CheckLimitPackageItemPacketDataSize() {
    L2_GUARD_FUNCTION();
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
    L2_GUARD_FUNCTION();
    PACKAGE_ITEM_PACKETHELPER& current =
        m_vPackageItemPacketHelperList[static_cast<std::size_t>(m_nCurrentPacketHelperIndex)];
    va_list arguments;
    va_start(arguments, format);
    const int length = current.ItemPacketHelper.AssembleV(format, arguments);
    va_end(arguments);
    return length;
}

int CPackageItemPacketManager::PackageAssembleItem(CItemAssemblerSendToCLNT* itemAssembler) {
    PACKAGE_ITEM_PACKETHELPER& current =
        m_vPackageItemPacketHelperList[static_cast<std::size_t>(m_nCurrentPacketHelperIndex)];
    const int length = itemAssembler->AssembleItemInfoImpl(current.ItemPacketHelper);
    return length;
}

void CPackageItemPacketManager::SendPacket_PackageItemData(
    CSocket* socket, S_PROTOCOL protocol, PackageItemPacket::PACKAGE_ITEM_PACKET_TYPE packetType
) {
    L2_GUARD_FUNCTION();
    for (PACKAGE_ITEM_PACKETHELPER& package : m_vPackageItemPacketHelperList) {
        socket->Send(
            "ccddb", static_cast<int>(static_cast<std::int8_t>(protocol)),
            static_cast<int>(static_cast<std::int8_t>(packetType)), m_nTotalPackageItemCount, package.nItemCount,
            package.ItemPacketHelper.GetLength(), package.ItemPacketHelper.GetPacket()
        );
    }
}

void CPackageItemPacketManager::SendPacket_PackageItemData(
    CSocket* socket,
    S_PROTOCOL protocol,
    S_EX_PROTOCOL extendedProtocol,
    PackageItemPacket::PACKAGE_ITEM_PACKET_TYPE packetType
) {
    L2_GUARD_FUNCTION();
    for (PACKAGE_ITEM_PACKETHELPER& package : m_vPackageItemPacketHelperList) {
        socket->Send(
            "chcddb", static_cast<int>(static_cast<std::int8_t>(protocol)),
            static_cast<int>(static_cast<std::int16_t>(extendedProtocol)),
            static_cast<int>(static_cast<std::int8_t>(packetType)), m_nTotalPackageItemCount, package.nItemCount,
            package.ItemPacketHelper.GetLength(), package.ItemPacketHelper.GetPacket()
        );
    }
}

void CPackageItemPacketManager::SetAssembleItemLengthPerItem(int length) {
    L2_GUARD_FUNCTION();
    if (m_nTotalPackageItemCount == 0) {
        m_nAssembleItemLengthPerItem += length;
    }
}

void CPackageItemPacketManager::IncreasePackageItemCount() {
    L2_GUARD_FUNCTION();
    ++m_vPackageItemPacketHelperList[static_cast<std::size_t>(m_nCurrentPacketHelperIndex)].nItemCount;
    ++m_nTotalPackageItemCount;
}

int CPackageItemPacketManager::GetTotalPackageItemCount() {
    L2_GUARD_FUNCTION();
    int count = 0;
    for (const PACKAGE_ITEM_PACKETHELPER& package : m_vPackageItemPacketHelperList) {
        count += package.nItemCount;
    }
    return count;
}

int CPackageItemPacketManager::GetTotalPackageItemLength() {
    L2_GUARD_FUNCTION();
    int length = 0;
    for (const PACKAGE_ITEM_PACKETHELPER& package : m_vPackageItemPacketHelperList) {
        length += package.ItemPacketHelper.GetLength();
    }
    return length;
}
