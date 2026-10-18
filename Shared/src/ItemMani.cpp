#include "Shared/src/ItemMani.h"

#if defined(L2_BUILD_L2SERVER)

#include "Item.h"
#include "ItemAssembler.h"
#include "L2Server/src/Inventory.h"
#include "Object.h"
#include "ObjectDB.h"
#include "Shared/src/SharedFactory.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <algorithm>
#include <cstdlib>

namespace {

CItem* ResolveItemPrototype(const int classId) {
    CObject* const object = g_ObjectDB.GetObjectW(classId, false);
    return object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
}

} // namespace

int MsgHelper_ManipulateItem(
    char* buffer, const int bufferSize, int& bufferPosition, CItem* item, InventoryParams& parameters
) {
    // Current L2Server 0x007f34ac.
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> itemPacket;
    CItemAssemblerItemMani assembler(*item, parameters, 0, 0x7f);
    assembler.AssembleItemInfo(itemPacket);

    const int assembled = ::Assemble(
        buffer + bufferPosition, bufferSize - bufferPosition, "b", itemPacket.GetLength(), itemPacket.GetPacket()
    );
    bufferPosition += assembled;
    if (assembled == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Packet buffer size[%d] overflow T_T",
        //     L"..\\Shared\\ItemMani.cpp",
        //     0x16f,
        //     bufferSize)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[items] Cannot append item-change record: buffer position %d, capacity %d bytes. Record was not appended.",
        //     bufferPosition,
        //     bufferSize)
        L2_LOG_ERROR(
            Log,
            u"[items] Cannot append item-change record: buffer position %d, capacity %d bytes; record was not appended.",
            bufferPosition, bufferSize
        );
    }
    return assembled != 0;
}

int MsgHelper_ManipulateItem(
    char* buffer,
    const int bufferSize,
    int& bufferPosition,
    CItem* item,
    const std::int64_t count,
    const ItemGetReason::Enum reason,
    const int enchanted,
    const int blessed,
    const int eroded,
    const ItemOptionKey key,
    const int intensiveItemClassId,
    const int shapeShiftedClassId,
    const int shapeShiftedPeriod
) {
    // Current L2Server 0x007f3688 ignores the nested
    // overload's packet-overflow result and returns one.
    L2_GUARD_FUNCTION();
    InventoryParams parameters;
    parameters.nCount = count;
    parameters.m_Reason = reason;
    parameters.nEnchanted = enchanted;
    parameters.nBless = blessed;
    parameters.nEroded = eroded;
    parameters.key = key;
    parameters.nIntensiveItemClassID = intensiveItemClassId;
    parameters.attribute = item->m_itemBaseAttribute;
    parameters.nShapeShiftedClassId = shapeShiftedClassId;
    parameters.nShapeShiftedPeriod = shapeShiftedPeriod;
    parameters.hLocked = item->m_data->GetLocked();
    parameters.hUseCount = item->m_data->GetUseCount();
    MsgHelper_ManipulateItem(buffer, bufferSize, bufferPosition, item, parameters);
    return 1;
}

bool SendSysMsg701(CCreature*, const ItemManip*) {
    // Current L2Server.exe 0x0046c3fc is exactly xor al, al, then ret. The
    // TotalChecker2015 type stream supplies the source-level function name.
    return false;
}

const unsigned char* ItemManip::Disassemble(const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    char checkVariation = 0;
    const unsigned char* result = ::Disassemble(
        packet, "ddQdddc", &mRelatedQuestID, &mClassId, &mCount, &mEnchanted, &mBlessed, &mEroded, &checkVariation
    );
    mCheckVariation = checkVariation != 0;
    return result;
}

void ItemManip::Dump() { L2_GUARD_FUNCTION(); }

bool ItemManip::IsDbId() const {
    L2_GUARD_FUNCTION();
    return (static_cast<std::uint32_t>(mClassId) >> 31U) != 0;
}

bool ItemManip::SimulateWithInventory(CInventory& inventory, std::pair<int, std::int64_t>& result) const {
    L2_GUARD_FUNCTION();

    int classId = mClassId;
    std::int64_t currentAmount = 0;
    if (IsDbId() || mDBId != 0) {
        const int databaseId = IsDbId() ? static_cast<int>(static_cast<std::uint32_t>(mClassId) & 0x7fffffffU) : mDBId;
        CItem* const actualItem = inventory.GetByDBID(runtime::Int32FromBits(databaseId));
        if (actualItem == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Item dbid=[%d] is not exist.",
            //     L"..\\Shared\\ItemMani.cpp",
            //     0x9b,
            //     databaseId)
            // Log.Add(
            //     LOG_WARN,
            //     L"[items] Cannot simulate inventory change: item DB ID %d was not found.",
            //     databaseId)
            L2_LOG_WARN(Log, u"[items] Cannot simulate inventory change: item DB ID %d was not found.", databaseId);
            return false;
        }
        classId = actualItem->m_nClassId;
        currentAmount = actualItem->m_data->m_nCount;
    } else {
        currentAmount = inventory.GetItemAmount(classId, mEnchanted, mCheckVariation);
    }

    CItem* const item = ResolveItemPrototype(classId);
    if (item == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item, class id=[%d] is not exist.",
        //     L"..\\Shared\\ItemMani.cpp",
        //     0xb1,
        //     classId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[items] Cannot simulate inventory change: item template %d was not found.",
        //     classId)
        L2_LOG_ERROR(Log, u"[items] Cannot simulate inventory change: item template %d was not found.", classId);
        return false;
    }

    constexpr std::int64_t kMaximumItemCount = 999999999999LL;
    const std::int64_t resultingAmount = runtime::AddInt64Wrapping(currentAmount, mCount);
    if (resultingAmount < 0 || mCount == 0 || resultingAmount > kMaximumItemCount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Cannot manipulate item. item[%d]'s amount is not enough, "
        //     L"count=[%I64d], request=[%I64d]",
        //     mClassId,
        //     currentAmount,
        //     mCount)
        // Log.Add(
        //     LOG_WARN,
        //     L"[items] Invalid inventory amount for selector %d (item template %d): current %lld, delta %lld, calculated
        //     %lld, %ls. Passing the request to the invalid-count handler.", mClassId, classId, static_cast<long
        //     long>(currentAmount), static_cast<long long>(mCount), static_cast<long long>(resultingAmount), resultingAmount
        //     < 0 ? L"result is negative" : mCount == 0 ? L"delta must be nonzero" : L"result exceeds 999999999999")
        L2_LOG_WARN(
            Log,
            u"[items] Invalid inventory amount for selector %d (item template %d): current %lld, delta %lld, calculated "
            u"%lld; %ls. Passing the request to the invalid-count handler.",
            mClassId, classId, static_cast<long long>(currentAmount), static_cast<long long>(mCount),
            static_cast<long long>(resultingAmount),
            resultingAmount < 0 ? L"result is negative"
                : mCount == 0   ? L"delta must be nonzero"
                                : L"result exceeds 999999999999"
        );
        return mOnInvalidCount(inventory.GetOwner(), this);
    }

    if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
        if (resultingAmount == 0) {
            result.first = -1;
        } else {
            result.first = resultingAmount == mCount ? 1 : 0;
        }
    } else {
        result.first = runtime::Int32FromBits(static_cast<std::uint32_t>(mCount));
    }

    const int itemWeight = item->m_data->m_ItemConsumeType == ICT_ASSET ? 0 : item->m_const->m_nWeight;
    result.second = runtime::MultiplyInt64Wrapping(static_cast<std::int64_t>(itemWeight), mCount);
    return true;
}

bool ItemManip::MakeDBPacket(
    CInventory& inventory,
    char* buffer,
    const int bufferSize,
    int& bufferPosition,
    int& itemCount,
    const ItemGetReason::Enum reason
) const {
    L2_GUARD_FUNCTION();

    if (IsDbId() || mDBId != 0) {
        std::uint32_t databaseId = 0;
        if (IsDbId()) {
            databaseId = static_cast<std::uint32_t>(mClassId) & 0x7fffffffU;
        } else if (mDBId != 0) {
            databaseId = static_cast<std::uint32_t>(mDBId);
        }
        CItem* const actualItem = inventory.GetByDBID(databaseId);
        if (actualItem == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Item, dbid=[%d] is not exist.",
            //     L"..\\Shared\\ItemMani.cpp",
            //     0x199,
            //     runtime::Int32FromBits(databaseId))
            // Log.Add(
            //     LOG_WARN,
            //     L"[items] Cannot encode inventory change: item DB ID %u was not found.",
            //     static_cast<unsigned int>(databaseId))
            L2_LOG_WARN(
                Log, u"[items] Cannot encode inventory change: item DB ID %u was not found.",
                static_cast<unsigned int>(databaseId)
            );
            return false;
        }
        if (!CSharedItemData::IsStackable(actualItem->m_data->m_ItemConsumeType) && mCount != -1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid item manipulation, class id=[%d], "
            //     L"count=[%I64d]",
            //     L"..\\Shared\\ItemMani.cpp",
            //     0x1a2,
            //     actualItem->m_data->m_nClassID,
            //     mCount)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[items] Cannot encode inventory change for nonstackable item template %d by DB ID: delta %lld must be
            //     -1.", actualItem->m_data->m_nClassID, static_cast<long long>(mCount))
            L2_LOG_ERROR(
                Log,
                u"[items] Cannot encode inventory change for nonstackable item template %d by DB ID: delta %lld must be -1.",
                actualItem->m_data->m_nClassID, static_cast<long long>(mCount)
            );
            return false;
        }

        const ItemOptionKey emptyKey{};
        if (!MsgHelper_ManipulateItem(
                buffer, bufferSize, bufferPosition, actualItem, mCount, reason, mEnchanted, mBlessed, mEroded, emptyKey, 0,
                0, -1
            )) {
            // Noise: this MsgHelper overload always returns one. The inner overload reports append failures. This duplicate
            // failure branch cannot be reached. Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Buffer Overflow in ItemManipulation!!!",
            //     L"..\\Shared\\ItemMani.cpp",
            //     0x1ad)
            return false;
        }
        itemCount = runtime::AddInt32Wrapping(itemCount, 1);
        return true;
    }

    CObject* const object = g_ObjectDB.GetObjectW(mClassId, false);
    if (object == nullptr || !object->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item, class id=[%d] is not exist.",
        //     L"..\\Shared\\ItemMani.cpp",
        //     0x1c1,
        //     mClassId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[items] Cannot encode inventory change: template %d is missing or is not an item.",
        //     mClassId)
        L2_LOG_ERROR(Log, u"[items] Cannot encode inventory change: template %d is missing or is not an item.", mClassId);
        return false;
    }
    CItem* const prototype = object->CastItem();

    const ItemOptionKey emptyKey{};
    if (!CSharedItemData::IsStackable(prototype->m_data->m_ItemConsumeType)) {
        if (mCount < 1) {
            ItemPointerList items;
            items.reserve(static_cast<std::size_t>(runtime::AbsoluteInt64Wrapping(mCount)));
            const int requestedCount =
                runtime::Int32FromBits(static_cast<std::uint32_t>(runtime::AbsoluteInt64Wrapping(mCount)));
            if (!inventory.GetItemListByClassID(items, mClassId, requestedCount, mEnchanted, !mCheckVariation)) {
                const std::int64_t requestedForLog = runtime::AbsoluteInt64Wrapping(mCount);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Item, class id=[%d], count=[%I64d] "
                //     L"is not exist.",
                //     L"..\\Shared\\ItemMani.cpp",
                //     0x1f1,
                //     mClassId,
                //     requestedForLog)
                // Log.Add(
                //     LOG_WARN,
                //     L"[items] Cannot select %lld nonstackable items of template %d from inventory. Passing the request to
                //     the invalid-count handler.", static_cast<long long>(requestedForLog), mClassId)
                L2_LOG_WARN(
                    Log,
                    u"[items] Cannot select %lld nonstackable items of template %d from inventory; passing the request to "
                    u"the invalid-count handler.",
                    static_cast<long long>(requestedForLog), mClassId
                );
                return mOnInvalidCount(inventory.GetOwner(), this);
            }

            std::int32_t index = 0;
            if (!items.empty()) {
                do {
                    const CSPointer<CItem>& item = items[static_cast<std::size_t>(index)];
                    if (!MsgHelper_ManipulateItem(
                            buffer, bufferSize, bufferPosition, item.get(), -1, reason, 0, 0, 0, emptyKey, 0, 0, -1
                        )) {
                        // Noise: this MsgHelper overload always returns one. The inner overload reports append failures.
                        // This duplicate failure branch cannot be reached. Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] Buffer Overflow in ItemManipulation!!!",
                        //     L"..\\Shared\\ItemMani.cpp",
                        //     0x200)
                        return false;
                    }
                    itemCount = runtime::AddInt32Wrapping(itemCount, 1);
                    index = runtime::AddInt32Wrapping(index, 1);
                } while (static_cast<std::size_t>(static_cast<std::int64_t>(index)) < items.size());
            }
            return true;
        }

        std::int32_t index = 0;
        do {
            if (!MsgHelper_ManipulateItem(
                    buffer, bufferSize, bufferPosition, prototype, 1, reason, mEnchanted, mBlessed, mEroded, emptyKey, 0, 0,
                    -1
                )) {
                // Noise: this MsgHelper overload always returns one. The inner overload reports append failures. This
                // duplicate failure branch cannot be reached. Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Buffer Overflow in ItemManipulation!!!",
                //     L"..\\Shared\\ItemMani.cpp",
                //     0x1e1)
                return false;
            }
            itemCount = runtime::AddInt32Wrapping(itemCount, 1);
            index = runtime::AddInt32Wrapping(index, 1);
        } while (static_cast<std::int64_t>(index) < mCount);
        return true;
    }

    if (!MsgHelper_ManipulateItem(
            buffer, bufferSize, bufferPosition, prototype, mCount, reason, mEnchanted, mBlessed, mEroded, emptyKey, 0, 0, -1
        )) {
        // Noise: this MsgHelper overload always returns one. The inner overload reports append failures. This duplicate
        // failure branch cannot be reached. Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Buffer Overflow in ItemManipulation!!!",
        //     L"..\\Shared\\ItemMani.cpp",
        //     0x1d0)
        return false;
    }
    itemCount = runtime::AddInt32Wrapping(itemCount, 1);
    return true;
}

bool ItemManip::MakeDBPacket(char* buffer, const int bufferSize, int& bufferPosition, int& itemCount) const {
    L2_GUARD_FUNCTION();

    CObject* const object = g_ObjectDB.GetObjectW(mClassId, false);
    if (object == nullptr || !object->IsItem()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Item, class id=[%d] is not exist.",
        //     L"..\\Shared\\ItemMani.cpp",
        //     0x21b,
        //     mClassId)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[items] Cannot encode warehouse change: template %d is missing or is not an item.",
        //     mClassId)
        L2_LOG_ERROR(Log, u"[items] Cannot encode warehouse change: template %d is missing or is not an item.", mClassId);
        return false;
    }
    CItem* const prototype = object->CastItem();

    if (IsDbId() || mDBId != 0) {
        std::uint32_t databaseId = 0;
        if (IsDbId()) {
            databaseId = static_cast<std::uint32_t>(mClassId) & 0x7fffffffU;
        } else if (mDBId != 0) {
            databaseId = static_cast<std::uint32_t>(mDBId);
        }
        prototype->SetDBID(databaseId);
        if (!CSharedItemData::IsStackable(prototype->m_data->m_ItemConsumeType) && mCount != -1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Invalid item manipulation, class id=[%d], "
            //     L"count=[%I64d]",
            //     L"..\\Shared\\ItemMani.cpp",
            //     0x22a,
            //     prototype->m_data->m_nClassID,
            //     mCount)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[items] Cannot encode warehouse change for nonstackable item template %d by DB ID: delta %lld must be
            //     -1.", prototype->m_data->m_nClassID, static_cast<long long>(mCount))
            L2_LOG_ERROR(
                Log,
                u"[items] Cannot encode warehouse change for nonstackable item template %d by DB ID: delta %lld must be -1.",
                prototype->m_data->m_nClassID, static_cast<long long>(mCount)
            );
            return false;
        }

        const ItemOptionKey emptyKey{};
        if (!MsgHelper_ManipulateItem(
                buffer, bufferSize, bufferPosition, prototype, mCount, ItemGetReason::Etc, mEnchanted, mBlessed, mEroded,
                emptyKey, 0, 0, -1
            )) {
            // Noise: this MsgHelper overload always returns one. The inner overload reports append failures. This duplicate
            // failure branch cannot be reached. Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Buffer Overflow in ItemManipulation!!!",
            //     L"..\\Shared\\ItemMani.cpp",
            //     0x234)
            return false;
        }
        itemCount = runtime::AddInt32Wrapping(itemCount, 1);
        return true;
    }

    const ItemOptionKey emptyKey{};
    if (!CSharedItemData::IsStackable(prototype->m_data->m_ItemConsumeType)) {
        if (mCount > 0) {
            std::int32_t index = 0;
            do {
                if (!MsgHelper_ManipulateItem(
                        buffer, bufferSize, bufferPosition, prototype, 1, ItemGetReason::Etc, mEnchanted, mBlessed, mEroded,
                        emptyKey, 0, 0, -1
                    )) {
                    // Noise: this MsgHelper overload always returns one. The inner overload reports append failures. This
                    // duplicate failure branch cannot be reached. Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Buffer Overflow in ItemManipulation!!!",
                    //     L"..\\Shared\\ItemMani.cpp",
                    //     0x256)
                    return false;
                }
                itemCount = runtime::AddInt32Wrapping(itemCount, 1);
                index = runtime::AddInt32Wrapping(index, 1);
            } while (static_cast<std::int64_t>(index) < mCount);
            return true;
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Nonstackable item cannot be deleted from user warehouse",
        //     L"..\\Shared\\ItemMani.cpp",
        //     0x261)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[items] Cannot encode warehouse change: nonstackable item template %d has delta %lld. Removal by template is
        //     unsupported and zero is not an addition.", mClassId, static_cast<long long>(mCount))
        L2_LOG_ERROR(
            Log,
            u"[items] Cannot encode warehouse change: nonstackable item template %d has delta %lld; removal by template is "
            u"unsupported and zero is not an addition.",
            mClassId, static_cast<long long>(mCount)
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        return false;
    }

    if (!MsgHelper_ManipulateItem(
            buffer, bufferSize, bufferPosition, prototype, mCount, ItemGetReason::Etc, mEnchanted, mBlessed, mEroded,
            emptyKey, 0, 0, -1
        )) {
        // Noise: this MsgHelper overload always returns one. The inner overload reports append failures. This duplicate
        // failure branch cannot be reached. Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Buffer Overflow in ItemManipulation!!!",
        //     L"..\\Shared\\ItemMani.cpp",
        //     0x246)
        return false;
    }
    itemCount = runtime::AddInt32Wrapping(itemCount, 1);
    return true;
}

void ItemManipList::Push(const int count, const ItemManip* manipulations) {
    L2_GUARD_FUNCTION();
    if (count <= 0 || manipulations == nullptr) {
        return;
    }
    for (int index = 0; index < count; ++index) {
        m_vInputList.push_back(manipulations[index]);
    }
}

void ItemManipList::Push(const ItemManip& manipulation) { m_vInputList.push_back(manipulation); }

void ItemManipList::Serialize() {
    L2_GUARD_FUNCTION();
    std::sort(m_vInputList.begin(), m_vInputList.end(), [](const ItemManip& left, const ItemManip& right) {
        return left.mClassId < right.mClassId;
    });

    m_vOutputList.clear();
    m_vOutputList.reserve(m_vInputList.size());
    for (const ItemManip& manipulation : m_vInputList) {
        if (!m_vOutputList.empty()) {
            ItemManip& previous = m_vOutputList.back();
            if (previous.mClassId == manipulation.mClassId && previous.mEnchanted == manipulation.mEnchanted &&
                previous.mDBId == manipulation.mDBId) {
                previous.mCount += manipulation.mCount;
                continue;
            }
        }
        m_vOutputList.push_back(manipulation);
    }

    m_vOutputList.erase(
        std::remove_if(
            m_vOutputList.begin(), m_vOutputList.end(),
            [](const ItemManip& manipulation) { return manipulation.mCount == 0; }
        ),
        m_vOutputList.end()
    );
}

InventoryParams::InventoryParams() : key(), mEnsoulOptionMap() {
    nItemClassID = 0;
    nCount = 0;
    m_Reason = ItemGetReason::Etc;
    nEnchanted = 0;
    nBless = 0;
    nEroded = 0;

    const ItemOptionKey emptyKey{};
    key = emptyKey;
    nIntensiveItemClassID = 0;

    const BaseAttribute emptyAttribute;
    attribute = emptyAttribute;
    nShapeShiftedClassId = 0;
    nShapeShiftedPeriod = -1;
    hLocked = 0;
    hUseCount = 0;
}

InventoryParams::InventoryParams(const InventoryParams& other) :
    nItemClassID(other.nItemClassID),
    nCount(other.nCount),
    m_Reason(other.m_Reason),
    nEnchanted(other.nEnchanted),
    nBless(other.nBless),
    nEroded(other.nEroded),
    key(other.key),
    nIntensiveItemClassID(other.nIntensiveItemClassID),
    attribute(other.attribute),
    nShapeShiftedClassId(other.nShapeShiftedClassId),
    nShapeShiftedPeriod(other.nShapeShiftedPeriod),
    mEnsoulOptionMap{other.mEnsoulOptionMap[0], other.mEnsoulOptionMap[1], other.mEnsoulOptionMap[2]},
    hLocked(other.hLocked),
    hUseCount(other.hUseCount) {}


#elif defined(L2_BUILD_L2NPC)

#include "L2Core/src/inc/Guard.h"

int ItemManip::Assemble(char* outBuffer, int bufferSize) {
    // Current L2NPC.exe 0x0050f254. The call result remains in EAX across
    // the inlined guard teardown and is returned unchanged.
    L2_GUARD_FUNCTION();
    return ::Assemble(
        outBuffer, bufferSize, "ddQdddc", mRelatedQuestID, mClassId, static_cast<long long>(mCount), mEnchanted, mBlessed,
        mEroded, mCheckVariation ? 1 : 0
    );
}

bool SendSysMsg701(CCreature*, const ItemManip*) {
    // Current L2NPC.exe 0x0041f61c is exactly xor al,al, then ret.
    return false;
}

#endif
