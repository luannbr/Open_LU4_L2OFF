#include "MultiSell.h"

#include "Inventory.h"
#include "Item.h"
#include "MultiSellParser.h"
#include "NPC.h"
#include "ObjectDB.h"
#include "Residence.h"
#include "User.h"
#include "UserSocket.h"

#include "Portable/ClientProtocolRuntime.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

constexpr int kMultiSellPacketCapacity = 0x5000;
constexpr int kMultiSellPageSize = 0x28;

std::int32_t Signed32(std::uint32_t value) noexcept {
    std::int32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

std::int64_t Signed64(std::uint64_t value) noexcept {
    std::int64_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

void CopyUsableEnsoulSlots(
    std::map<int, EnsoulOptionData*> destination[3], const std::map<int, EnsoulOptionData*> source[3]
) {
    // Current CMultiSell owners copy only the two usable slots, 1 and 2.
    for (int slot = 1; slot <= 2; ++slot) {
        for (const auto& entry : source[slot]) {
            destination[slot].insert(entry);
        }
    }
}

int AppendEnsoulSlots(char* destination, int capacity, const std::map<int, EnsoulOptionData*> maps[3]) {
    int length = 0;
    for (int slot = 1; slot <= 2; ++slot) {
        const int count = static_cast<int>(maps[slot].size());
        int appended = ::Assemble(destination + length, capacity - length, "c", count);
        length += appended;
        for (int optionIndex = 1; optionIndex <= count; ++optionIndex) {
            const auto position = maps[slot].find(optionIndex);
            if (position != maps[slot].end()) {
                appended = ::Assemble(destination + length, capacity - length, "d", position->second->m_nId);
                length += appended;
            }
        }
    }
    return length;
}

} // namespace

CMultiSellDB g_MultiSellDB;

CMultiSellInfo::CMultiSellInfo() : m_nId(0), m_nTaxBase(0), m_bUsePledgePoint(false) {}

CMultiSellInfo::~CMultiSellInfo() = default;

CMultiSellKeepDataInfo::CMultiSellKeepDataInfo() : m_nEnchant(0), m_OptionKey{}, m_ensoulOptionDataMap{} {}

CMultiSellKeepDataInfo::CMultiSellKeepDataInfo(const CMultiSellKeepDataInfo& other) :
    m_nEnchant(other.m_nEnchant), m_OptionKey(other.m_OptionKey), m_Attribute(other.m_Attribute), m_ensoulOptionDataMap{} {
    CopyUsableEnsoulSlots(m_ensoulOptionDataMap, other.m_ensoulOptionDataMap);
}

bool CMultiSellKeepDataInfo::operator<(const CMultiSellKeepDataInfo& other) const {
    // Current L2Server 0x008577ec keeps this non-lexicographic shipped
    // condition literally.
    if (other.m_nEnchant <= m_nEnchant && other.m_OptionKey.variationOption1 <= m_OptionKey.variationOption1 &&
        (m_OptionKey.variationOption1 != other.m_OptionKey.variationOption1 ||
            other.m_OptionKey.variationOption2 <= m_OptionKey.variationOption2) &&
        (m_OptionKey.variationOption1 != other.m_OptionKey.variationOption1 ||
            m_OptionKey.variationOption2 != other.m_OptionKey.variationOption2 || !(m_Attribute < other.m_Attribute)) &&
        CItem::CompareOptionData(m_ensoulOptionDataMap, other.m_ensoulOptionDataMap)) {
        return false;
    }
    return true;
}

bool CMultiSellInfo::CheckUsingPledgePoint() {
    L2_GUARD_FUNCTION();

    for (const MultiSellItemStruct* item : m_vInputList) {
        if (item != nullptr && item->ItemClassId < 0) {
            m_bUsePledgePoint = true;
            return true;
        }
    }
    return false;
}

int CMultiSellInfo::GetTaxTo(CNPC* npc) {
    // Current L2Server 0x00857c90.
    L2_GUARD_FUNCTION();
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]NPC is null",
        //     "MultiSell.cpp",
        //     0x4db)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]NPC is null",
        //     "MultiSell.cpp",
        //     0x4db)
        L2_LOG_ERROR(Log, "Cannot calculate tax for multisell entry %d: NPC is null; returning zero tax.", m_nId);
        return 0;
    }

    std::uint32_t taxRate = 0;
    CResidence* const residence = g_ResidenceDB.GetResidence(npc->m_data->m_nResidenceID);
    if (residence != nullptr) {
        taxRate = residence->m_data->m_TaxInfoData[static_cast<std::size_t>(residence->m_data->m_ResidenceType)].m_nTaxRate;
    }

    std::int32_t taxBase = m_nTaxBase;
    if (taxBase < 1) {
        std::uint32_t wrappedBase = 0;
        for (const MultiSellItemStruct* itemData : m_vOutputList) {
            if (itemData->ItemClassId < 0) {
                continue;
            }
            CObject* const object = g_ObjectDB.GetObjectW(itemData->ItemClassId, false);
            if (object == nullptr) {
                continue;
            }
            CItem* const item = static_cast<CItem*>(object);
            const std::uint32_t priceLow = static_cast<std::uint32_t>(item->m_const->m_nDefaultPrice);
            const std::uint32_t countLow = static_cast<std::uint32_t>(itemData->Count);
            wrappedBase += priceLow * countLow;
        }
        taxBase = Signed32(wrappedBase);
    }

    return runtime::Win64CvttDoubleToInt32(static_cast<double>(taxBase) * static_cast<double>(Signed32(taxRate))) / 100;
}

CMultiSellList::CMultiSellList() :
    m_nLastInfoId(0),
    m_nShowAll(1),
    m_nKeepEnchanted(0),
    m_nIsDutyfree(0),
    m_nShowType(0),
    m_nId(0),
    m_bUsePledgePoint(false),
    m_bShowVariationItem(false),
    m_nSSQFlag(0),
    m_nType(0),
    m_nKeepShapeShifted(0),
    m_bShowEnsoulItem(false),
    m_bKeepEnsoul(false),
    m_nIsWithoutNPC(0) {}

CMultiSellList::~CMultiSellList() = default;

int CMultiSellList::GenerateMultisellTypeSet() {
    // Current L2Server 0x00857864.
    L2_GUARD_FUNCTION();
    int typeSet = 0;
    if (m_nKeepEnchanted != 0) {
        // typeSet |= 0x04.
        typeSet |= MultisellConst::MTB_KEEP_ENCHANT;
    }
    if (m_bShowVariationItem) {
        // typeSet |= 0x10.
        typeSet |= MultisellConst::MTB_VARIATION_ITEM;
    }
    if (m_bShowEnsoulItem) {
        // typeSet |= 0x01.
        typeSet |= MultisellConst::MTB_SHOW_ENSOUL;
    }
    if (m_bKeepEnsoul) {
        // typeSet |= 0x02.
        typeSet |= MultisellConst::MTB_KEEP_ENSOUL;
    }
    if (m_nKeepShapeShifted != 0) {
        // typeSet |= 0x08.
        typeSet |= MultisellConst::MTB_SHAPE_SHIFT;
    }
    if (m_nShowAll != 0) {
        // typeSet |= 0x20.
        typeSet |= MultisellConst::MTB_IS_SHOW_ALL;
    }
    return typeSet;
}

bool CMultiSellList::IsKeyItem(CItem* item) {
    // Current L2Server 0x00857998.
    L2_GUARD_FUNCTION();
    if (item == nullptr || item->m_data == nullptr) {
        return false;
    }

    const bool ordinaryKey = !CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType) &&
        (item->m_data->m_Type == 0 || item->m_data->m_Type == 1 || item->m_data->m_Type == 2);
    const bool coupon = item->IsEtcItem() && item->m_const != nullptr && item->m_const->m_EtcitemType == EIT_COUPON;
    return ordinaryKey || coupon;
}

bool CMultiSellList::HasKeyItem(CMultiSellInfo* info) {
    // Current L2Server 0x00857bb4.
    L2_GUARD_FUNCTION();
    if (info == nullptr) {
        return false;
    }
    for (const MultiSellItemStruct* itemData : info->m_vInputList) {
        if (itemData == nullptr || itemData->ItemClassId < 0) {
            continue;
        }
        CObject* const object = g_ObjectDB.GetObjectW(itemData->ItemClassId, false);
        if (object != nullptr && IsKeyItem(object->CastItem())) {
            return true;
        }
    }
    return false;
}

bool CMultiSellList::CheckSendable(CMultiSellInfo* info, User* user) {
    // Current L2Server 0x00857eec.
    L2_GUARD_FUNCTION();
    const bool hasKeyItem = HasKeyItem(info);
    if (user == nullptr || info == nullptr) {
        return false;
    }

    for (const MultiSellItemStruct* itemData : info->m_vInputList) {
        if (itemData == nullptr || itemData->ItemClassId < 0) {
            continue;
        }
        CObject* const object = g_ObjectDB.GetObjectW(itemData->ItemClassId, false);
        if (object == nullptr) {
            return false;
        }
        if (hasKeyItem && !IsKeyItem(object->CastItem())) {
            continue;
        }

        const bool includeVariation = m_nKeepEnchanted != 0 && m_bShowVariationItem;
        CItem* const item = user->m_inv.GetFirstItemByClassID(itemData->ItemClassId, includeVariation, m_bShowEnsoulItem);
        if (item == nullptr || item->m_data == nullptr || item->m_data->m_nCount < itemData->Count) {
            return false;
        }
    }
    return true;
}

bool CMultiSellList::IsAllowed(User* user, CNPC* npc) {
    // Current L2Server 0x00858050.
    L2_GUARD_FUNCTION();
    if (m_RequiedNpcList.empty()) {
        return true;
    }
    return npc != nullptr &&
        m_RequiedNpcList.find(static_cast<std::uint32_t>(npc->m_data->m_nClassID)) != m_RequiedNpcList.end() &&
        user->IsTalkableDistance(npc);
}

void CMultiSellList::SendMultiSellListPerPage(
    int* itemCount,
    int pageSize,
    int* page,
    User* user,
    CNPC* npc,
    int* bufferSize,
    char* buffer,
    int capacity,
    bool showLast
) {
    // Current L2Server 0x00857a94.
    if (*itemCount != pageSize) {
        return;
    }

    ++*page;
    if (m_nIsWithoutNPC == 0) {
        user->m_nMultisellNpcSid = static_cast<std::int32_t>(npc->Id());
    }
    CUserSocket* const socket = user->GetSocket();
    if (runtime::client_protocol::UsesNorthAmericanProtocol(socket)) {
        // Portable P152 compatibility: the exact NA 5.0.0.35 client
        // S_MULTI_SELL_LIST handler at 0x20342fb0 decodes
        // `cdcddddccd` after opcode 0xd0. The shipping P162 server omits
        // the legacy "new multisell window" byte between m_nType and the
        // type-set DWORD. Keep the exact P162 writer below and publish the
        // client-proven extra zero only to negotiated P152 sockets.
        socket->Send(
            "ccdcddddccdb", 0xd0, showLast ? 1 : 0, m_nId, static_cast<int>(static_cast<std::int8_t>(m_nShowType)), *page, 0,
            pageSize, *itemCount, static_cast<int>(static_cast<std::int8_t>(m_nType)), 0, GenerateMultisellTypeSet(),
            *bufferSize, buffer
        );
    } else {
        socket->Send(
            "ccdcddddcdb", 0xd0, showLast ? 1 : 0, m_nId, static_cast<int>(static_cast<std::int8_t>(m_nShowType)), *page, 0,
            pageSize, *itemCount, static_cast<int>(static_cast<std::int8_t>(m_nType)), GenerateMultisellTypeSet(),
            *bufferSize, buffer
        );
    }
    *bufferSize = 0;
    std::memset(buffer, 0, static_cast<std::size_t>(capacity));
    *itemCount = 0;
}

int CMultiSellList::AssembleOneMultisellItem(
    char* destination,
    int capacity,
    User* user,
    CMultiSellInfo* info,
    std::int64_t multiplier,
    bool keepEnchanted,
    int enchanted,
    std::int64_t tax,
    bool showVariation,
    const ItemOptionKey& optionKey,
    const BaseAttribute& attribute,
    bool showEnsoul,
    bool keepEnsoul,
    const std::map<int, EnsoulOptionData*> ensoulOptionDataMap[3]
) {
    // Current L2Server 0x00858a20. The multiplier and showEnsoul formal
    // parameters are present in the compiled signature but unused by the
    // current body.
    L2_GUARD_FUNCTION();
    static_cast<void>(multiplier);
    static_cast<void>(showEnsoul);

    std::array<char, kMultiSellPacketCapacity> inputBuffer{};
    std::array<char, kMultiSellPacketCapacity> outputBuffer{};
    int inputLength = 0;
    int outputLength = 0;
    int inputCount = 0;
    int outputCount = 0;
    bool allStackable = true;

    int previousKeyClassId = -1;
    int previousKeyItemIndex = -1;
    int inputKeyCount = 0;

    {
        GuardHelper inputListGuard(
            L"CMultiSellList::AssembleOneMultisellItem_InputList_Assemble", CThreadManager::GetThreadID()
        );

        for (const MultiSellItemStruct* itemData : info->m_vInputList) {
            if (itemData == nullptr) {
                continue;
            }

            if (itemData->ItemClassId < 0) {
                const int assembled = ::Assemble(
                    inputBuffer.data() + inputLength, kMultiSellPacketCapacity - inputLength, "dhQhddhhhhhhhhcc",
                    itemData->ItemClassId, -1, itemData->Count, 0, 0, 0, ATTRT_NONE, 0, 0, 0, 0, 0, 0, 0, 0, 0
                );
                if (assembled < 1) {
                    return 0;
                }
                inputLength += assembled;
                ++inputCount;
                continue;
            }

            CObject* const object = g_ObjectDB.GetObjectW(itemData->ItemClassId, false);
            CItem* item = object == nullptr ? nullptr : object->CastItem();
            if (item == nullptr || item->m_data == nullptr) {
                continue;
            }

            const std::uint64_t countBits = static_cast<std::uint64_t>(itemData->Count) +
                (itemData->ItemClassId == 0x39 ? static_cast<std::uint64_t>(tax) : 0U);
            const std::int64_t itemCount = Signed64(countBits);

            std::int16_t itemEnchant = 0;
            ItemOptionKey itemOption{};
            BaseAttribute itemAttribute;
            std::map<int, EnsoulOptionData*> itemEnsoul[3];

            if (IsKeyItem(item)) {
                ++inputKeyCount;
                if (!keepEnchanted) {
                    if (inputKeyCount < 3) {
                        CItem* selected = nullptr;
                        if (previousKeyClassId == itemData->ItemClassId) {
                            selected = user->m_inv.GetNextMinEnchantedItemByClassID(
                                itemData->ItemClassId, previousKeyItemIndex, false, false
                            );
                        } else {
                            selected = user->m_inv.GetMinEnchantedItemByClassID(itemData->ItemClassId, false, false);
                        }
                        if (selected != nullptr) {
                            previousKeyClassId = itemData->ItemClassId;
                            previousKeyItemIndex = selected->m_data->index;
                            item = selected;
                        }
                    }
                    itemEnchant = static_cast<std::int16_t>(item->m_data->GetEnchanted());
                    itemOption = item->m_itemOption.itemOptionKey;
                    itemAttribute = item->GetTotalAttribute();
                    CopyUsableEnsoulSlots(itemEnsoul, ensoulOptionDataMap);
                } else if (inputKeyCount == 1) {
                    itemEnchant = static_cast<std::int16_t>(enchanted);
                    itemOption = optionKey;
                    itemAttribute = attribute;
                    CopyUsableEnsoulSlots(itemEnsoul, ensoulOptionDataMap);
                } else {
                    itemEnchant = static_cast<std::int16_t>(item->m_data->GetEnchanted());
                    itemAttribute = item->GetTotalAttribute();
                }
            }

            allStackable = CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType) && allStackable;
            int assembled = ::Assemble(
                inputBuffer.data() + inputLength, kMultiSellPacketCapacity - inputLength, "dhQhddhhhhhhhh",
                itemData->ItemClassId, static_cast<int>(static_cast<std::int16_t>(item->m_data->m_Type)), itemCount,
                static_cast<int>(itemEnchant), itemOption.variationOption1, itemOption.variationOption2,
                static_cast<int>(itemAttribute.m_nAttackType), static_cast<int>(itemAttribute.m_nAttackValue),
                static_cast<int>(itemAttribute.m_nDefendValue[0]), static_cast<int>(itemAttribute.m_nDefendValue[1]),
                static_cast<int>(itemAttribute.m_nDefendValue[2]), static_cast<int>(itemAttribute.m_nDefendValue[3]),
                static_cast<int>(itemAttribute.m_nDefendValue[4]), static_cast<int>(itemAttribute.m_nDefendValue[5])
            );
            assembled += AppendEnsoulSlots(
                inputBuffer.data() + inputLength + assembled, kMultiSellPacketCapacity - inputLength - assembled, itemEnsoul
            );
            if (assembled < 1) {
                return 0;
            }
            inputLength += assembled;
            ++inputCount;
        }

        if (tax > 0) {
            CObject* const object = g_ObjectDB.GetObjectW(0x39, false);
            CItem* const adena = object == nullptr ? nullptr : object->CastItem();
            if (adena != nullptr && adena->m_data != nullptr) {
                const int assembled = ::Assemble(
                    inputBuffer.data() + inputLength, kMultiSellPacketCapacity - inputLength, "dhQhddhhhhhhhhcc", 0x39,
                    static_cast<int>(static_cast<std::int16_t>(adena->m_data->m_Type)), tax, adena->m_data->GetEnchanted(),
                    0, 0, ATTRT_NONE, 0, 0, 0, 0, 0, 0, 0, 0, 0
                );
                if (assembled < 1) {
                    return 0;
                }
                inputLength += assembled;
                ++inputCount;
            }
        }
    }

    int outputEnchant = 0;
    ItemOptionKey outputOption{};
    BaseAttribute outputAttribute;
    std::map<int, EnsoulOptionData*> outputEnsoul[3];
    int outputKeyCount = 0;

    {
        GuardHelper outputListGuard(
            L"CMultiSellList::AssembleOneMultisellItem_OutputList_Assemble", CThreadManager::GetThreadID()
        );

        for (const MultiSellItemStruct* itemData : info->m_vOutputList) {
            if (itemData == nullptr) {
                continue;
            }

            if (itemData->ItemClassId < 0) {
                // The current source passes 100.0 through a `d` vararg here.
                // On Win64 VAssemble consumes the low dword of that bit pattern,
                // which is zero. Publish the resulting wire value explicitly.
                const int assembled = ::Assemble(
                    outputBuffer.data() + outputLength, kMultiSellPacketCapacity - outputLength, "dQhQhdddhhhhhhhhcc",
                    itemData->ItemClassId, std::int64_t{0}, -1, itemData->Count, 0, 0, 0, 0, ATTRT_NONE, 0, 0, 0, 0, 0, 0, 0,
                    0, 0
                );
                if (assembled < 1) {
                    return 0;
                }
                outputLength += assembled;
                ++outputCount;
                continue;
            }

            CObject* const object = g_ObjectDB.GetObjectW(itemData->ItemClassId, false);
            CItem* const item = object == nullptr ? nullptr : object->CastItem();
            if (item == nullptr || item->m_data == nullptr || item->m_const == nullptr) {
                continue;
            }

            if (IsKeyItem(item)) {
                ++outputKeyCount;
                if (keepEnchanted && outputKeyCount == 1) {
                    outputAttribute = attribute;
                    outputEnchant = enchanted;
                }
                if (showVariation && outputKeyCount == 1) {
                    outputOption = optionKey;
                }
                if (itemData->nEnchant >= 0) {
                    outputEnchant = itemData->nEnchant;
                }
                if (keepEnsoul && outputKeyCount == 1) {
                    CopyUsableEnsoulSlots(outputEnsoul, ensoulOptionDataMap);
                }
            }

            allStackable = CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType) && allStackable;
            const int probability = itemData->Probability <= 0.0 || itemData->Probability >= 1.0
                ? runtime::Win64CvttDoubleToInt32(itemData->Probability)
                : 1;
            int assembled = ::Assemble(
                outputBuffer.data() + outputLength, kMultiSellPacketCapacity - outputLength, "dQhQhdddhhhhhhhh",
                itemData->ItemClassId, item->m_const->m_nDefaultPrice,
                static_cast<int>(static_cast<std::int16_t>(item->m_data->m_Type)), itemData->Count,
                static_cast<int>(static_cast<std::int16_t>(outputEnchant)), probability, outputOption.variationOption1,
                outputOption.variationOption2, static_cast<int>(outputAttribute.m_nAttackType),
                static_cast<int>(outputAttribute.m_nAttackValue), static_cast<int>(outputAttribute.m_nDefendValue[0]),
                static_cast<int>(outputAttribute.m_nDefendValue[1]), static_cast<int>(outputAttribute.m_nDefendValue[2]),
                static_cast<int>(outputAttribute.m_nDefendValue[3]), static_cast<int>(outputAttribute.m_nDefendValue[4]),
                static_cast<int>(outputAttribute.m_nDefendValue[5])
            );
            assembled += AppendEnsoulSlots(
                outputBuffer.data() + outputLength + assembled, kMultiSellPacketCapacity - outputLength - assembled,
                outputEnsoul
            );
            if (assembled < 1) {
                return 0;
            }
            outputLength += assembled;
            ++outputCount;
        }
    }

    GuardHelper oneItemGuard(
        L"CMultiSellList::AssembleOneMultisellItem_OneMultisellItem_Assemble", CThreadManager::GetThreadID()
    );

    int assembled = ::Assemble(
        destination, capacity, "dchddhhhhhhhh", info->m_nId, allStackable ? 1 : 0, outputEnchant,
        outputOption.variationOption1, outputOption.variationOption2, static_cast<int>(outputAttribute.m_nAttackType),
        static_cast<int>(outputAttribute.m_nAttackValue), static_cast<int>(outputAttribute.m_nDefendValue[0]),
        static_cast<int>(outputAttribute.m_nDefendValue[1]), static_cast<int>(outputAttribute.m_nDefendValue[2]),
        static_cast<int>(outputAttribute.m_nDefendValue[3]), static_cast<int>(outputAttribute.m_nDefendValue[4]),
        static_cast<int>(outputAttribute.m_nDefendValue[5])
    );
    assembled += AppendEnsoulSlots(destination + assembled, capacity - assembled, outputEnsoul);
    assembled += ::Assemble(
        destination + assembled, capacity - assembled, "hhbb", outputCount, inputCount, outputLength, outputBuffer.data(),
        inputLength, inputBuffer.data()
    );

    return assembled > 0 && assembled < capacity ? assembled : 0;
}

void CMultiSellList::SendList(User* user, CNPC* npc, bool showLast) {
    // Current L2Server 0x0085a678.
    L2_GUARD_FUNCTION();
    if (user == nullptr || (m_nIsWithoutNPC == 0 && npc == nullptr) || !IsAllowed(user, npc)) {
        return;
    }

    std::array<char, kMultiSellPacketCapacity> buffer{};
    int bufferSize = 0;
    int itemCount = 0;
    int page = 0;

    std::set<CMultiSellKeepDataInfo> keepDataSet;

    for (const auto& entry : m_mapMultiSellInfo) {
        CMultiSellInfo* const info = entry.second;
        if (info == nullptr || (m_nShowAll == 0 && !CheckSendable(info, user))) {
            continue;
        }

        const std::int64_t tax = m_nIsDutyfree == 0 && m_nIsWithoutNPC == 0 ? info->GetTaxTo(npc) : 0;
        if (info->m_vInputList.empty() || info->m_vInputList.front() == nullptr ||
            (m_bShowVariationItem && info->m_vInputList.front()->ItemClassId < 0)) {
            continue;
        }

        keepDataSet.clear();
        const int firstClassId = info->m_vInputList.front()->ItemClassId;
        CItem* item = user->m_inv.GetFirstItemByClassID(firstClassId, m_bShowVariationItem, m_bShowEnsoulItem);

        if (m_nKeepEnchanted != 0 && item == nullptr) {
            continue;
        }

        if (m_nKeepEnchanted == 0 || !IsKeyItem(item) || firstClassId < 0) {
            ++itemCount;
            const ItemOptionKey emptyOption{};
            const BaseAttribute emptyAttribute;
            std::map<int, EnsoulOptionData*> emptyEnsoul[3];
            const int assembled = AssembleOneMultisellItem(
                buffer.data() + bufferSize, kMultiSellPacketCapacity - bufferSize, user, info, itemCount,
                m_nKeepEnchanted != 0, 0, tax, false, emptyOption, emptyAttribute, false, false, emptyEnsoul
            );
            if (assembled < 1) {
                return;
            }
            bufferSize += assembled;
            SendMultiSellListPerPage(
                &itemCount, kMultiSellPageSize, &page, user, npc, &bufferSize, buffer.data(), kMultiSellPacketCapacity,
                showLast
            );
            continue;
        }

        while (item != nullptr) {
            CMultiSellKeepDataInfo keepData;
            keepData.m_nEnchant = item->m_data->GetEnchanted();
            keepData.m_Attribute = item->GetTotalAttribute();

            if (!user->IsEquipedItem(item->m_data->index) && item->m_data->GetLocked() == 0) {
                if (m_bShowVariationItem) {
                    keepData.m_OptionKey = item->m_itemOption.itemOptionKey;
                }
                if (m_bShowEnsoulItem) {
                    CopyUsableEnsoulSlots(keepData.m_ensoulOptionDataMap, item->m_mEnsoulInfo);
                }
                keepDataSet.insert(keepData);
            }

            item = user->m_inv.GetNextItemByClassID(item->m_data->index, m_bShowVariationItem, m_bShowEnsoulItem);
        }

        for (const CMultiSellKeepDataInfo& keepData : keepDataSet) {
            ++itemCount;
            const int assembled = AssembleOneMultisellItem(
                buffer.data() + bufferSize, kMultiSellPacketCapacity - bufferSize, user, info, itemCount, true,
                keepData.m_nEnchant, tax, m_bShowVariationItem, keepData.m_OptionKey, keepData.m_Attribute,
                m_bShowEnsoulItem, m_bKeepEnsoul, keepData.m_ensoulOptionDataMap
            );
            if (assembled < 1) {
                return;
            }
            bufferSize += assembled;
            SendMultiSellListPerPage(
                &itemCount, kMultiSellPageSize, &page, user, npc, &bufferSize, buffer.data(), kMultiSellPacketCapacity,
                showLast
            );
        }
    }

    if (m_nIsWithoutNPC == 0) {
        user->m_nMultisellNpcSid = static_cast<std::int32_t>(npc->Id());
    }
    CUserSocket* const socket = user->GetSocket();
    if (runtime::client_protocol::UsesNorthAmericanProtocol(socket)) {
        // See SendMultiSellListPerPage: P152 keeps one legacy byte that
        // is absent from the pinned P162 shipping packet.
        socket->Send(
            "ccdcddddccdb", 0xd0, showLast ? 1 : 0, m_nId, static_cast<int>(static_cast<std::int8_t>(m_nShowType)), page + 1,
            1, kMultiSellPageSize, itemCount, static_cast<int>(static_cast<std::int8_t>(m_nType)), 0,
            GenerateMultisellTypeSet(), bufferSize, buffer.data()
        );
    } else {
        socket->Send(
            "ccdcddddcdb", 0xd0, showLast ? 1 : 0, m_nId, static_cast<int>(static_cast<std::int8_t>(m_nShowType)), page + 1,
            1, kMultiSellPageSize, itemCount, static_cast<int>(static_cast<std::int8_t>(m_nType)),
            GenerateMultisellTypeSet(), bufferSize, buffer.data()
        );
    }
}

CMultiSellInfo* CMultiSellList::find(std::int32_t infoId) {
    L2_GUARD_FUNCTION();

    const auto position = m_mapMultiSellInfo.find(infoId);
    return position == m_mapMultiSellInfo.end() ? nullptr : position->second;
}

void CMultiSellList::Add(CMultiSellInfo* info) {
    L2_GUARD_FUNCTION();

    ++m_nLastInfoId;
    info->m_nId = m_nLastInfoId;
    if (info->CheckUsingPledgePoint()) {
        m_bUsePledgePoint = true;
    }
    m_mapMultiSellInfo.emplace(info->m_nId, info);
}

void CMultiSellList::ClearMultiSellInfo() {
    L2_GUARD_FUNCTION();

    for (auto& entry : m_mapMultiSellInfo) {
        delete entry.second;
    }
    m_mapMultiSellInfo.clear();
}

void CMultiSellList::ClearNPCList() {
    L2_GUARD_FUNCTION();
    m_RequiedNpcList.clear();
}

void CMultiSellList::AddRequiredNPC(std::uint32_t npcId) {
    L2_GUARD_FUNCTION();
    m_RequiedNpcList.insert(npcId);
}

CMultiSellList* CMultiSellDB::find(std::int32_t listId) {
    L2_GUARD_FUNCTION();

    const auto position = m_mapMultiSellList.find(listId);
    return position == m_mapMultiSellList.end() ? nullptr : position->second;
}

void CMultiSellDB::Add(CMultiSellList* list) { m_mapMultiSellList.emplace(list->m_nId, list); }

void CMultiSellDB::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\multisell.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/multisell.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from MultiSell file failed",
        //     L"MultiSell.cpp",
        //     0x4b7)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from MultiSell file failed",
        //     L"MultiSell.cpp",
        //     0x4b7)
        L2_LOG_ERROR(Log, "Cannot read multisell.txt; multisell loading stops.");
        return;
    }

    CParserForMultiSell parser;
    if (!parser.Create()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] objectParser.create failed",
        //     L"MultiSell.cpp",
        //     0x4bf)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] objectParser.create failed",
        //     L"MultiSell.cpp",
        //     0x4bf)
        L2_LOG_ERROR(Log, "Cannot initialize the multisell.txt parser; multisell loading stops.");
        return;
    }

    parser.m_lexer.yybegin(4);
    parser.m_lexer.yyin = input.GetStream();
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing MultiSell file failed after line[%d]",
        //     L"MultiSell.cpp",
        //     0x4cb,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing MultiSell file failed after line[%d]",
        //     L"MultiSell.cpp",
        //     0x4cb,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse multisell.txt at script line %lld; already inserted multisell entries are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}
