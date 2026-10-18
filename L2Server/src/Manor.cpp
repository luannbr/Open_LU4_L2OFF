#include "Manor.h"
#include "DB.h"
#include "HtmlActionParamParser.h"
#include "Item.h"
#include "L2Time.h"
#include "ManorParser.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Residence.h"
#include "Shared/src/MemorySub.h"
#include "Shared/src/SharedFactory.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/CenteredSharedRWLock.h"
#include "Shared/src/Config.h"
#include "Shared/src/Exception.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>

namespace {


std::tm* ManorLocalTime32(const std::int32_t value) noexcept {
#if defined(_MSC_VER)
    __time32_t input = static_cast<__time32_t>(value);
    return ::_localtime32(&input);
#else
    const std::time_t input = static_cast<std::time_t>(value);
    return std::localtime(&input);
#endif
}

void ManorI64ToAscii(const std::int64_t value, char* const buffer, const std::size_t bufferSize) noexcept {
#if defined(_MSC_VER)
    static_cast<void>(::_i64toa_s(value, buffer, bufferSize, 10));
#else
    static_cast<void>(std::to_chars(buffer, buffer + bufferSize, value, 10));
#endif
}

void SetCurrentLocalTime(SYSTEMTIME& destination) {
    const runtime::SystemTime source = runtime::GetLocalTimeNow();
    static_assert(sizeof(source) == sizeof(destination));
    std::memcpy(&destination, &source, sizeof(destination));
}

std::u16string PledgeNameForLog(CPledge* const pledge) {
    const char16_t* const name = pledge != nullptr ? pledge->GetPledgeName() : nullptr;
    if (name == nullptr) {
        return {};
    }
    return std::u16string(name);
}

} // namespace

CManorDB g_ManorDB;
volatile std::int32_t g_nManorLock = 0;

CManor::CManor() {
    // Current L2Server 0x00824b98 initializes the scalar
    // fields only after both target-container envelopes are live.
    m_data = nullptr;
    m_nManorOwnerPledgeServerId = 0;
    std::memset(&m_SystemTime, 0, sizeof(m_SystemTime));
    m_dMaxChangeRate = 1.0;
    m_bManorSettingTime = false;
}

CManor::~CManor() {
    // Current L2Server 0x008242d4 clears the borrowed singleton pointer
    // before the crop vector and procure map unwind.
    m_data = nullptr;
}

// Current L2Server 0x0082437c. Class id 185 is fixed by the current
// memory-class registration order.
L2_DEFINE_OBJECT_RELEASE(CManor)

void CManor::Set(const ObjectFieldType field, const int value) {
    // Current L2Server 0x008201e4.
    L2_GUARD_FUNCTION();
    if (field == MANOR_ID) {
        m_data = g_ManorDB.RequireManorData(value);
        return;
    }
    if (field == RESIDENCE_ID) {
        if (m_data != nullptr) {
            m_data->m_nResidenceID = value;
        }
        const int manorId = m_data != nullptr ? m_data->m_nManorId : 0;
        CResidence* const residence = g_ResidenceDB.GetResidence(value);
        if (residence == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Cannot find residence in res[%d]manor[%d]",
            //     value,
            //     manorId)
            // L2_LOG_ERROR(Log,
            //     L"Cannot find residence in res[%d]manor[%d]",
            //     value,
            //     manorId)
            L2_LOG_ERROR(
                Log, "Manor %d: residence %d was not found; the residence ID is retained, but registration is skipped.",
                manorId, value
            );
            return;
        }
        if (residence->HaveManor(manorId)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Alread have manor in res[%d]manor[%d]",
            //     value,
            //     manorId)
            // L2_LOG_ERROR(Log,
            //     L"Alread have manor in res[%d]manor[%d]",
            //     value,
            //     manorId)
            L2_LOG_WARN(
                Log, "Manor %d is already registered with residence %d; the duplicate registration is ignored.", manorId,
                value
            );
            return;
        }
        residence->AddManor(manorId);
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%S][%d] unknown token[%d]",
    //     "Manor.cpp",
    //     0x139,
    //     static_cast<int>(field))
    // L2_LOG_ERROR(Log,
    //     L"[%S][%d] unknown token[%d]",
    //     "Manor.cpp",
    //     0x139,
    //     static_cast<int>(field))
    L2_LOG_ERROR(
        Log, "Manor %d: unsupported integer field %d; the value is ignored.", m_data != nullptr ? m_data->m_nManorId : 0,
        static_cast<int>(field)
    );
}

void CManor::Set(const ObjectFieldType field, const char16_t* const value) {
    // Current L2Server 0x0082036c uses wcscpy_s with a capacity of 0x28,
    // one code unit less than the shared carrier's physical extent.
    L2_GUARD_FUNCTION();
    if (field == OBJECT_FIELD_MANOR_NAME) {
        if (value == nullptr) {
            m_data->m_wsManorName[0] = u'\0';
            return;
        }
        const std::size_t length = std::char_traits<char16_t>::length(value);
        if (length >= 0x28U) {
            m_data->m_wsManorName[0] = u'\0';
            return;
        }
        std::copy_n(value, length + 1, m_data->m_wsManorName.begin());
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%S][%d] unknown token[%d]",
    //     "Manor.cpp",
    //     0x14d,
    //     static_cast<int>(field))
    // L2_LOG_ERROR(Log,
    //     L"[%S][%d] unknown token[%d]",
    //     "Manor.cpp",
    //     0x14d,
    //     static_cast<int>(field))
    L2_LOG_ERROR(
        Log, "Manor %d: unsupported text field %d; the value is ignored.", m_data != nullptr ? m_data->m_nManorId : 0,
        static_cast<int>(field)
    );
}

void CManor::Set(const ObjectFieldType field, ProcureStruct* const value) {
    // Current L2Server 0x00823764 uses map::insert and therefore does not
    // replace an already published crop entry.
    L2_GUARD_FUNCTION();
    if (field == PROCURE_LIST) {
        m_ProcureMap.emplace(value->nCropId, value);
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%S][%d] unknown token[%d]",
    //     "Manor.cpp",
    //     0x161,
    //     static_cast<int>(field))
    // L2_LOG_ERROR(Log,
    //     L"[%S][%d] unknown token[%d]",
    //     "Manor.cpp",
    //     0x161,
    //     static_cast<int>(field))
    L2_LOG_ERROR(
        Log, "Manor %d: unsupported procurement-list field %d; the value is ignored.",
        m_data != nullptr ? m_data->m_nManorId : 0, static_cast<int>(field)
    );
}

void CManor::AddCrop(
    const int cropId,
    const int seedId,
    const int itemId,
    const int seedLevel,
    const int seedMaxPerDay,
    const int cropMaxPerDay
) {
    // Current L2Server 0x00824c80.
    L2_GUARD_FUNCTION();
    auto* const cropData = static_cast<CropDataStruct*>(std::malloc(sizeof(CropDataStruct)));
    cropData->nCropId = cropId;
    cropData->nSeedId = seedId;
    cropData->nSeedLev = seedLevel;
    cropData->nSeedMaxPerDay = seedMaxPerDay;
    cropData->nCropMaxPerDay = cropMaxPerDay;

    int seedPrice = 0;
    CObject* seedObject = g_ObjectDB.GetObjectW(seedId, false);
    if (seedObject == nullptr) {
        std::free(cropData);
        return;
    }
    if (seedObject->IsItem()) {
        CItem* const seed = seedObject->CastItem();
        if (seed != nullptr) {
            seed->Set(ITEM_MANOR_ID, m_data != nullptr ? m_data->m_nManorId : 0);
            seed->Set(ITEM_MANOR_LEV, seedLevel);
            seed->Set(ITEM_MANOR_CROP_ID, cropId);
            seed->Set(ITEM_MANOR_CROP_ID_REPLACE, itemId);
            seedPrice = runtime::Int32FromBits(static_cast<std::uint32_t>(seed->m_const->m_nDefaultPrice));
        }
    }

    int cropPrice = 0;
    CObject* cropObject = g_ObjectDB.GetObjectW(cropId, false);
    if (cropObject == nullptr) {
        std::free(cropData);
        return;
    }
    if (cropObject->IsItem()) {
        CItem* const crop = cropObject->CastItem();
        if (crop != nullptr) {
            crop->Set(ITEM_MANOR_CROP_ID_REPLACE, itemId);
            cropPrice = runtime::Int32FromBits(static_cast<std::uint32_t>(crop->m_const->m_nDefaultPrice));
        }
    }

    for (int index = 0; index < 40; ++index) {
        if (m_data->m_nSeedIdList[index] != 0) {
            continue;
        }
        m_data->m_nSeedIdList[index] = seedId;
        SetMaxSeedCount(index, seedMaxPerDay);
        SetMaxCropCount(index, cropMaxPerDay);
        m_data->m_nCropIdList[index] = cropId;
        m_data->m_nSeedLevel[index] = seedLevel;
        m_data->m_nSeedPriceDefault[index] = seedPrice;
        m_data->m_nCropPriceDefault[index] = cropPrice;
        break;
    }
    m_CropList.push_back(cropData);
}

void CManor::ReadLock(const wchar_t* const file, const int line) {
    // Current L2Server 0x0081e448.
    L2_GUARD_FUNCTION();
    g_ManorLock.ReadLock(file, line, false);
}

void CManor::ReadUnlock() {
    // Current L2Server 0x0081e4f8.
    L2_GUARD_FUNCTION();
    g_ManorLock.ReadUnlock(nullptr, 0);
}

void CManor::WriteLock(const wchar_t* const file, const int line) {
    // Current L2Server 0x0081e2fc.
    L2_GUARD_FUNCTION();
    g_ManorLock.WriteLock(file, line);
}

void CManor::WriteUnlock(const wchar_t* const /*file*/, const int /*line*/) {
    // Current L2Server 0x0081e3a8 ignores its source arguments.
    L2_GUARD_FUNCTION();
    g_ManorLock.WriteUnlock(nullptr, 0);
}

bool CManor::CheckBuySeed(const int seedId, const std::int64_t count, User* const user) {
    // Current L2Server 0x0081df90, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    std::int64_t available = 0;
    for (int index = 0; index < 40; ++index) {
        if (m_data->m_nSeedIdList[index] == seedId) {
            available = m_data->m_nSeedRemainCount[index];
            break;
        }
    }
    if (count > available) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::INVALID_SEED_QUANTITY);
    }
    return count <= available;
}

std::int64_t CManor::GetSeedPrice(const int seedId) {
    // Current L2Server 0x0081e164, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    for (int index = 0; index < 40; ++index) {
        if (m_data->m_nSeedIdList[index] == seedId) {
            return m_data->m_nSeedPrice[index];
        }
    }
    return 0;
}

bool CManor::ManorCheckTime() {
    // Current L2Server 0x0081e230, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    const std::int32_t now = runtime::crt::Time32Now();
    const std::tm* const localTime = ManorLocalTime32(now);
    const int minutes = localTime->tm_hour * 60 + localTime->tm_min;
    if ((minutes < 265 || minutes > 275) && (minutes < 15 || minutes > 25)) {
        return g_nManorLock > 0;
    }
    return true;
}

bool CManor::CheckProcureCrop(const int cropId, const std::int64_t count) {
    // Current L2Server 0x0081e090, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    std::int64_t available = 0;
    for (int index = 0; index < 40; ++index) {
        if (m_data->m_nProcuringCropList[index] == cropId) {
            available = m_data->m_nProcuringCropCount[index];
            break;
        }
    }
    return count <= available;
}

std::int64_t CManor::GetCommissionForProcureCrop(const int cropId, const std::int64_t count) {
    // Current L2Server 0x0081e7f8, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    const int index = GetManorCropIndex(cropId);
    if (index == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find Crop index. m[%d] c[%d]",
        //     "Manor.cpp",
        //     0x4a9,
        //     m_data != nullptr ? m_data->m_nManorId : 0,
        //     cropId)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find Crop index. m[%d] c[%d]",
        //     "Manor.cpp",
        //     0x4a9,
        //     m_data != nullptr ? m_data->m_nManorId : 0,
        //     cropId)
        L2_LOG_ERROR(
            Log, "Manor %d: crop %d has no configured slot; the procurement commission is returned as zero.",
            m_data != nullptr ? m_data->m_nManorId : 0, cropId
        );
        return 0;
    }

    const std::int64_t priceTimesAmount = runtime::MultiplyInt64Wrapping(m_data->m_nProcuringCropPrice[index], count);
    return runtime::MultiplyInt64Wrapping(priceTimesAmount, 5) / 100;
}

void CManor::RollProcureDice(ProcureTempData* const data, User* const /*user*/) {
    // Current L2Server 0x00823874, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    struct RewardWeight {
        std::int32_t itemClassId;
        std::int32_t defaultPriceLow;
    };

    L2_WRITE_GUARD(*this);
    int cropIndex = -1;
    for (int index = 0; index < 40; ++index) {
        if (m_data->m_nProcuringCropList[index] == data->nCropId) {
            cropIndex = index;
            break;
        }
    }
    if (cropIndex == -1) {
        return;
    }

    const std::int64_t cropPrice = m_data->m_nProcuringCropPrice[cropIndex];
    if (cropPrice < 1) {
        return;
    }

    const auto found = m_ProcureMap.find(data->nCropId);
    if (found == m_ProcureMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]NO procure crop info. manor[%d], crop[%d]",
        //     "Manor.cpp",
        //     0x202,
        //     m_data != nullptr ? m_data->m_nManorId : 0,
        //     data->nCropId)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]NO procure crop info. manor[%d], crop[%d]",
        //     "Manor.cpp",
        //     0x202,
        //     m_data != nullptr ? m_data->m_nManorId : 0,
        //     data->nCropId)
        L2_LOG_ERROR(
            Log, "Manor %d: crop %d has no procurement reward definition; reward selection is skipped.",
            m_data != nullptr ? m_data->m_nManorId : 0, data->nCropId
        );
        return;
    }

    ProcureStruct* const procure = found->second;
    if (procure == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Procure crop info is invalid.manor[%d], crop[%d]",
        //     "Manor.cpp",
        //     0x208,
        //     m_data != nullptr ? m_data->m_nManorId : 0,
        //     data->nCropId)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Procure crop info is invalid.manor[%d], crop[%d]",
        //     "Manor.cpp",
        //     0x208,
        //     m_data != nullptr ? m_data->m_nManorId : 0,
        //     data->nCropId)
        L2_LOG_ERROR(
            Log, "Manor %d: crop %d has a null procurement reward definition; reward selection is skipped.",
            m_data != nullptr ? m_data->m_nManorId : 0, data->nCropId
        );
        return;
    }

    for (ProcureStructEach* const each : *procure->m_pProcureList) {
        if (each->nTypeId != m_data->m_nProcuringCropType[cropIndex]) {
            continue;
        }

        std::array<RewardWeight, 40> rewards{};
        std::array<double, 40> ratios{};
        int rewardCount = 0;
        for (const int itemClassId : *each->pProcureItemList) {
            RewardWeight& reward = rewards[rewardCount];
            reward.itemClassId = itemClassId;

            CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
            CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
            if (item != nullptr && item->m_const != nullptr) {
                reward.defaultPriceLow =
                    runtime::Int32FromBits(static_cast<std::uint32_t>(item->m_const->m_nDefaultPrice));
                ratios[rewardCount] = 1.0 / static_cast<double>(item->m_const->m_nDefaultPrice);
            }
            ++rewardCount;
        }

        for (int index = 0; index < rewardCount; ++index) {
            ratios[index] = static_cast<double>(cropPrice) * ratios[index] / static_cast<double>(rewardCount);
        }

        for (int index = 0; index < rewardCount; ++index) {
            const double expected = static_cast<double>(data->nAmount) * ratios[index];
            int amount = static_cast<int>(expected);
            if (genrand64_real1() + 0.0 <= expected - static_cast<double>(amount)) {
                ++amount;
            }
            if (amount > 0) {
                const auto inserted = data->mapOut.emplace(rewards[index].itemClassId, static_cast<std::int64_t>(amount));
                if (!inserted.second) {
                    inserted.first->second =
                        runtime::AddInt64Wrapping(inserted.first->second, static_cast<std::int64_t>(amount));
                }
            }
        }

        data->nPriceRate = runtime::Int32FromBits(static_cast<std::uint32_t>(cropPrice));
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%S][%d]Procure crop info is invalid.manor[%d], crop[%d]",
    //     "Manor.cpp",
    //     0x21d,
    //     m_data != nullptr ? m_data->m_nManorId : 0,
    //     data->nCropId)
    // L2_LOG_ERROR(Log,
    //     L"[%S][%d]Procure crop info is invalid.manor[%d], crop[%d]",
    //     "Manor.cpp",
    //     0x21d,
    //     m_data != nullptr ? m_data->m_nManorId : 0,
    //     data->nCropId)
    L2_LOG_ERROR(
        Log, "Manor %d: crop %d has no reward list for configured reward type %d; no rewards are selected.",
        m_data != nullptr ? m_data->m_nManorId : 0, data->nCropId, m_data->m_nProcuringCropType[cropIndex]
    );
}

bool CManor::ProcureCrop(
    User* const user,
    const int itemServerId,
    const int cropId,
    const std::int64_t amount,
    const bool sameResidence,
    std::int64_t& commission
) {
    // Current L2Server 0x00824f2c, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    if (ManorCheckTime()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::MANOR_SYSTEM_UNDER_MAINTENANCE);
        return false;
    }

    const std::int64_t adena = user->m_inv.GetAdenaAmount();
    ItemAmountDataStruct rewardItems;
    ProcureTempData procure;
    CItem* const crop = user->m_inv.GetByServerID(itemServerId);
    if (crop == nullptr || crop->m_data->m_nCount < amount) {
        return false;
    }

    constexpr std::uint64_t kNonAssetAmountMinusOneMax = UINT64_C(0x1869e);
    constexpr std::uint64_t kAssetAmountMinusOneMax = UINT64_C(0xe8d4a50ffe);
    const ItemConsumeType cropConsumeType = crop->m_data->m_ItemConsumeType;
    const std::uint64_t amountMinusOne = static_cast<std::uint64_t>(amount) - UINT64_C(1);

    bool validAmount = false;
    if (cropConsumeType == ICT_ASSET) {
        validAmount = amountMinusOne <= kAssetAmountMinusOneMax;
    } else if (amountMinusOne <= kNonAssetAmountMinusOneMax) {
        std::array<char, 40> amountText;
        std::memset(amountText.data(), 0, 0x20);
        ManorI64ToAscii(amount, amountText.data(), 0x0b);
        validAmount = std::strlen(amountText.data()) <= 8U;
    }
    if (!validAmount) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid amount",
        //     L"Manor.cpp",
        //     0x2b2)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid amount",
        //     L"Manor.cpp",
        //     0x2b2)
        L2_LOG_WARN(
            Log,
            "Manor %d: rejected crop trade from character DB ID %u; quantity %lld is invalid for crop %d, consumption type "
            "%d.",
            m_data != nullptr ? m_data->m_nManorId : 0, static_cast<unsigned>(user->GetDBID()),
            static_cast<long long>(amount), cropId, static_cast<int>(cropConsumeType)
        );
        user->GetSocket()->SendSystemMessage(SystemMessageId::INPUT_QUANTITY_LIMIT_EXCEEDED);
        return false;
    }

    if (cropId > 0 && crop->m_data->m_nClassID != cropId) {
        return false;
    }
    if (!CheckProcureCrop(cropId, amount)) {
        return false;
    }

    if (!sameResidence) {
        commission = GetCommissionForProcureCrop(cropId, amount);
        if (adena < commission) {
            commission = -1;
            return false;
        }
    }
    if (crop->m_const->m_nManorCropIdReplace < 1) {
        return false;
    }

    procure.nCropId = cropId;
    procure.nAmount = amount;
    procure.mapOut.clear();
    RollProcureDice(&procure, user);

    for (const auto& reward : procure.mapOut) {
        bool merged = false;
        for (ItemAmountData* const existing : rewardItems.dataList) {
            if (existing->m_nItemClassId == reward.first) {
                existing->m_nItemAmount = runtime::AddInt64Wrapping(existing->m_nItemAmount, reward.second);
                merged = true;
                break;
            }
        }
        if (!merged) {
            rewardItems.Push(reward.first, reward.second);
        }
    }
    if (!user->m_inv.CheckAddableMulti(rewardItems, false)) {
        return false;
    }

    std::array<char, 0x5000> buffer{};
    int dataSize = 0;
    int itemCount = 0;
    if (CSharedItemData::IsStackable(cropConsumeType)) {
        const int written = ::Assemble(
            buffer.data(), static_cast<int>(buffer.size()), "dQd", cropId, runtime::NegateInt64Wrapping(amount),
            cropConsumeType
        );
        if (written > 0) {
            dataSize = written;
            itemCount = 1;
        }
    } else {
        for (int index = 0; static_cast<std::int64_t>(index) < amount; ++index) {
            const int written = ::Assemble(
                buffer.data() + dataSize, static_cast<int>(buffer.size()) - dataSize, "dQd", cropId, -1LL, cropConsumeType
            );
            if (written > 0) {
                dataSize += written;
                ++itemCount;
            }
        }
    }

    if (!sameResidence) {
        const int written = ::Assemble(
            buffer.data() + dataSize, static_cast<int>(buffer.size()) - dataSize, "dQd", 57,
            runtime::NegateInt64Wrapping(commission), ICT_ASSET
        );
        if (written > 0) {
            dataSize += written;
            ++itemCount;
        }
    }

    for (const ItemAmountData* const reward : rewardItems.dataList) {
        if (reward->m_nItemClassId <= 0 || reward->m_nItemAmount <= 0) {
            continue;
        }

        CObject* const object = g_ObjectDB.GetObjectW(reward->m_nItemClassId, false);
        CItem* const item = object != nullptr && object->IsItem() ? static_cast<CItem*>(object) : nullptr;
        if (item == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid item(classid : %d) at [%S], [%d]",
            //     reward->m_nItemClassId,
            //     "Manor.cpp",
            //     0x32c)
            // L2_LOG_ERROR(Log,
            //     L"invalid item(classid : %d) at [%S], [%d]",
            //     reward->m_nItemClassId,
            //     "Manor.cpp",
            //     0x32c)
            L2_LOG_ERROR(
                Log,
                "Manor %d: crop %d reward item class %d has no item prototype; this reward is omitted from the trade "
                "request.",
                m_data != nullptr ? m_data->m_nManorId : 0, cropId, reward->m_nItemClassId
            );
            continue;
        }

        const ItemConsumeType consumeType = item->m_data->m_ItemConsumeType;
        if (CSharedItemData::IsStackable(consumeType)) {
            const int written = ::Assemble(
                buffer.data() + dataSize, static_cast<int>(buffer.size()) - dataSize, "dQd", reward->m_nItemClassId,
                static_cast<long long>(reward->m_nItemAmount), consumeType
            );
            if (written > 0) {
                dataSize += written;
                ++itemCount;
            }
        } else {
            for (int index = 0; static_cast<std::int64_t>(index) < reward->m_nItemAmount; ++index) {
                const int written = ::Assemble(
                    buffer.data() + dataSize, static_cast<int>(buffer.size()) - dataSize, "dQd", reward->m_nItemClassId, 1LL,
                    consumeType
                );
                if (written > 0) {
                    dataSize += written;
                    ++itemCount;
                }
            }
        }
    }

    const int manorId = m_data != nullptr ? m_data->m_nManorId : 0;
    const int residenceId = m_data != nullptr ? m_data->m_nResidenceID : 0;
    return db.RequestProcureCrop(manorId, residenceId, user, itemCount, dataSize, buffer.data());
}

void CManor::SayNeedSeedMoney(User* const user) {
    // Current L2Server 0x0081f4c8, original owner Manor.cpp.
    L2_GUARD_FUNCTION();
    const std::int64_t amount = GetAdenaForNextSeed();
    CUserSocket* const socket = user->GetSocket();
    socket->SendSystemMessage_Q(SystemMessageId::NEXT_SEED_PURCHASE_COST, amount);
}

void CManor::SayNeedCropMoney(User* const user) {
    // Current L2Server 0x0081f58c, original owner Manor.cpp.
    L2_GUARD_FUNCTION();
    const std::int64_t amount = GetAdenaForNextCrop();
    CUserSocket* const socket = user->GetSocket();
    socket->SendSystemMessage_Q(SystemMessageId::NEXT_CROP_PURCHASE_COST, amount);
}

bool CManor::SellSeed(User* const user, const int itemCount, const unsigned char* const packet) {
    // Current L2Server 0x0082576c, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    if (ManorCheckTime()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::MANOR_SYSTEM_UNDER_MAINTENANCE);
        return false;
    }

    int bufferLength = 0;
    int itemClassId = 0;
    std::int64_t amount = 0;
    std::int64_t totalPrice = 0;
    std::int64_t totalCount = static_cast<std::int64_t>(itemCount);
    const std::int64_t adena = user->m_inv.GetAdenaAmount();
    ItemAmountDataStruct items;
    std::array<char, 0x5000> buffer;

    if (static_cast<std::uint32_t>(itemCount) > 39U) {
        return false;
    }

    const unsigned char* cursor = packet;
    for (int index = 0; index < itemCount; ++index) {
        cursor = ::Disassemble(cursor, "dQ", &itemClassId, &amount);
        items.Push(itemClassId, amount);
    }
    if (!user->m_inv.CheckAddableMulti(items, false)) {
        return false;
    }

    constexpr std::uint64_t kNonAssetAmountMinusOneMax = UINT64_C(0x1869e);
    constexpr std::uint64_t kAssetAmountMinusOneMax = UINT64_C(0xe8d4a50ffe);
    constexpr std::uint64_t kPriceMax = UINT64_C(0xe8d4a50fff);

    cursor = packet;
    for (int index = 0; index < itemCount; ++index) {
        cursor = ::Disassemble(cursor, "dQ", &itemClassId, &amount);

        CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
        if (object == nullptr || !object->IsItem()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid item(classid : %d) at [%S], [%d]",
            //     itemClassId,
            //     "Manor.cpp",
            //     0x3b1)
            // L2_LOG_ERROR(Log,
            //     L"invalid item(classid : %d) at [%S], [%d]",
            //     itemClassId,
            //     "Manor.cpp",
            //     0x3b1)
            L2_LOG_WARN(
                Log,
                "Manor %d: seed purchase from character DB ID %u contains unknown or non-item class %d; this entry is "
                "skipped and the purchase continues.",
                m_data != nullptr ? m_data->m_nManorId : 0, static_cast<unsigned>(user->GetDBID()), itemClassId
            );
            continue;
        }

        CItem* const item = static_cast<CItem*>(object);
        const ItemConsumeType consumeType = item->m_data->m_ItemConsumeType;
        const std::uint64_t amountMinusOne = static_cast<std::uint64_t>(amount) - UINT64_C(1);

        bool validAmount = false;
        if (consumeType == ICT_ASSET) {
            validAmount = amountMinusOne <= kAssetAmountMinusOneMax;
        } else if (amountMinusOne <= kNonAssetAmountMinusOneMax) {
            // The current function performs this extra decimal-width
            // check after already limiting the value to five digits.
            std::array<char, 40> amountText;
            std::memset(amountText.data(), 0, 0x20);
            ManorI64ToAscii(amount, amountText.data(), 0x0b);
            validAmount = std::strlen(amountText.data()) <= 8U;
        }

        if (!validAmount) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid amount at file[%s], line[%d] "
            //     L"ItemConsumeType[%d]/amount[%I64d]",
            //     L"Manor.cpp",
            //     0x3be,
            //     consumeType,
            //     amount)
            // L2_LOG_ERROR(Log,
            //     L"invalid amount at file[%s], line[%d] "
            //     L"ItemConsumeType[%d]/amount[%I64d]",
            //     L"Manor.cpp",
            //     0x3be,
            //     consumeType,
            //     amount)
            L2_LOG_WARN(
                Log,
                "Manor %d: rejected seed purchase from character DB ID %u; quantity %lld is invalid for item class %d, "
                "consumption type %d.",
                m_data != nullptr ? m_data->m_nManorId : 0, static_cast<unsigned>(user->GetDBID()),
                static_cast<long long>(amount), itemClassId, static_cast<int>(consumeType)
            );
            user->GetSocket()->SendSystemMessage(SystemMessageId::INPUT_QUANTITY_LIMIT_EXCEEDED);
            // user->ActionFailed(0)
            user->ActionFailed(AF_DEFAULT);
            return false;
        }

        const bool canBuy = CheckBuySeed(itemClassId, amount, user);
        const std::int64_t price = GetSeedPrice(itemClassId);
        if (!canBuy) {
            return false;
        }

        if (!CSharedItemData::IsStackable(consumeType)) {
            for (int expanded = 0; static_cast<std::int64_t>(expanded) < amount; ++expanded) {
                if (static_cast<std::uint64_t>(price) <= kPriceMax) {
                    totalPrice = runtime::AddInt64Wrapping(totalPrice, price);
                    const int written = ::Assemble(
                        buffer.data() + bufferLength, static_cast<int>(buffer.size()) - bufferLength, "dQd", itemClassId,
                        1LL, consumeType
                    );
                    if (written > 0) {
                        bufferLength += written;
                    }
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"invalid item price(classid : %d, %I64d, "
                    //     L"%I64d) at [%S], [%d]",
                    //     itemClassId,
                    //     price,
                    //     amount,
                    //     "Manor.cpp",
                    //     0x3e0)
                    // L2_LOG_ERROR(Log,
                    //     L"invalid item price(classid : %d, %I64d, "
                    //     L"%I64d) at [%S], [%d]",
                    //     itemClassId,
                    //     price,
                    //     amount,
                    //     "Manor.cpp",
                    //     0x3e0)
                    // Noise: The same invalid price is checked for every unit of one purchase entry. Report it only on the
                    // first unit.
                    if (expanded == 0) {
                        L2_LOG_ERROR(
                            Log,
                            "Manor %d: seed class %d has invalid unit price %lld adena for requested quantity %lld; these "
                            "non-stackable units are omitted and purchase processing continues.",
                            m_data != nullptr ? m_data->m_nManorId : 0, itemClassId, static_cast<long long>(price),
                            static_cast<long long>(amount)
                        );
                    }
                }
            }
            totalCount = runtime::AddInt64Wrapping(totalCount, runtime::SubtractInt64Wrapping(amount, 1));
        } else {
            const std::int64_t rowPrice = runtime::MultiplyInt64Wrapping(price, amount);
            if (static_cast<std::uint64_t>(rowPrice) <= kPriceMax) {
                totalPrice = runtime::AddInt64Wrapping(totalPrice, rowPrice);
                const int written = ::Assemble(
                    buffer.data() + bufferLength, static_cast<int>(buffer.size()) - bufferLength, "dQd", itemClassId,
                    static_cast<long long>(amount), consumeType
                );
                if (written > 0) {
                    bufferLength += written;
                }
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"invalid item price(classid : %d, %I64d, "
                //     L"%I64d) at [%S], [%d]",
                //     itemClassId,
                //     price,
                //     amount,
                //     "Manor.cpp",
                //     0x3ce)
                // L2_LOG_ERROR(Log,
                //     L"invalid item price(classid : %d, %I64d, "
                //     L"%I64d) at [%S], [%d]",
                //     itemClassId,
                //     price,
                //     amount,
                //     "Manor.cpp",
                //     0x3ce)
                L2_LOG_ERROR(
                    Log,
                    "Manor %d: seed class %d has invalid calculated total %lld adena (unit price %lld, quantity %lld); this "
                    "stack is omitted and purchase processing continues.",
                    m_data != nullptr ? m_data->m_nManorId : 0, itemClassId, static_cast<long long>(rowPrice),
                    static_cast<long long>(price), static_cast<long long>(amount)
                );
            }
        }
    }

    if (totalPrice < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid price at file[%s], line[%d]",
        //     L"Manor.cpp",
        //     0x3f3)
        // L2_LOG_ERROR(Log,
        //     L"invalid price at file[%s], line[%d]",
        //     L"Manor.cpp",
        //     0x3f3)
        L2_LOG_ERROR(
            Log,
            "Manor %d: seed purchase for character DB ID %u has negative calculated total %lld adena; the purchase is "
            "rejected.",
            m_data != nullptr ? m_data->m_nManorId : 0, static_cast<unsigned>(user->GetDBID()),
            static_cast<long long>(totalPrice)
        );
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }
    if (totalPrice > adena) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA);
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return false;
    }

    if (totalCount < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SellSeed TotalCount is strange "
        //     L"char_id(%d) cnt(%d) item(%d) amt(%d)",
        //     L"Manor.cpp",
        //     0x402,
        //     user->GetDBID(),
        //     totalCount,
        //     itemClassId,
        //     amount)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] SellSeed TotalCount is strange "
        //     L"char_id(%d) cnt(%d) item(%d) amt(%d)",
        //     L"Manor.cpp",
        //     0x402,
        //     user->GetDBID(),
        //     totalCount,
        //     itemClassId,
        //     amount)
        L2_LOG_ERROR(
            Log,
            "Manor %d: seed purchase for character DB ID %u has calculated item count %lld (last class %d, quantity %lld); "
            "the database request is still sent.",
            m_data != nullptr ? m_data->m_nManorId : 0, static_cast<unsigned>(user->GetDBID()),
            static_cast<long long>(totalCount), itemClassId, static_cast<long long>(amount)
        );
    }

    int residenceId = 0;
    int manorId = 0;
    if (m_data != nullptr) {
        residenceId = m_data->m_nResidenceID;
        manorId = m_data->m_nManorId;
    }
    return db.RequestBuySeed(
        manorId, residenceId, user, totalPrice, static_cast<int>(totalCount), bufferLength, buffer.data()
    );
}

void CManor::SetSeedSold(const int seedId, const std::int64_t count) {
    // Current L2Server 0x0081f650, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    for (int slot = 0; slot < 40; ++slot) {
        // The current instructions redundantly tests the same slot
        // forty times when it does not match. Preserve that loop literally.
        for (int repeatedRead = 0; repeatedRead < 40; ++repeatedRead) {
            if (seedId != m_data->m_nSeedList[slot]) {
                continue;
            }

            m_data->m_nSeedRemainCount[slot] = runtime::SubtractInt64Wrapping(m_data->m_nSeedRemainCount[slot], count);
            if (m_data->m_nSeedRemainCount[slot] < 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] manor error, sell seed, "
                //     L"[%d], [%d][%I64d]",
                //     "Manor.cpp",
                //     0x880,
                //     m_data != nullptr ? m_data->m_nManorId : 0,
                //     seedId,
                //     count)
                // L2_LOG_ERROR(Log,
                //     L"[%S][%d] manor error, sell seed, "
                //     L"[%d], [%d][%I64d]",
                //     "Manor.cpp",
                //     0x880,
                //     m_data != nullptr ? m_data->m_nManorId : 0,
                //     seedId,
                //     count)
                L2_LOG_ERROR(
                    Log,
                    "Manor %d: selling %lld units of seed %d left stock %lld in slot %d; seed stock is reset to zero and "
                    "income recording continues.",
                    m_data != nullptr ? m_data->m_nManorId : 0, static_cast<long long>(count), seedId,
                    static_cast<long long>(m_data->m_nSeedRemainCount[slot]), slot
                );
                m_data->m_nSeedRemainCount[slot] = 0;
            }

            m_data->m_nSeedSellBank = runtime::AddInt64Wrapping(
                m_data->m_nSeedSellBank, runtime::MultiplyInt64Wrapping(m_data->m_nSeedPrice[slot], count)
            );
            SaveNowManorInfo();
            break;
        }
    }
}

void CManor::SetCropProcured(const int cropId, const std::int64_t count) {
    // Current L2Server 0x0081f7a8, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    for (int slot = 0; slot < 40; ++slot) {
        // As in SetSeedSold, the target redundantly reloads one slot up to
        // forty times before advancing to the next slot.
        for (int repeatedRead = 0; repeatedRead < 40; ++repeatedRead) {
            if (cropId != m_data->m_nProcuringCropList[slot]) {
                continue;
            }

            m_data->m_nProcuringCropCount[slot] =
                runtime::SubtractInt64Wrapping(m_data->m_nProcuringCropCount[slot], count);
            if (m_data->m_nProcuringCropCount[slot] < 0) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] manor error, procure crop, "
                //     L"[%d], [%d][%I64d]",
                //     "Manor.cpp",
                //     0x89d,
                //     m_data != nullptr ? m_data->m_nManorId : 0,
                //     cropId,
                //     count)
                // L2_LOG_ERROR(Log,
                //     L"[%S][%d] manor error, procure crop, "
                //     L"[%d], [%d][%I64d]",
                //     "Manor.cpp",
                //     0x89d,
                //     m_data != nullptr ? m_data->m_nManorId : 0,
                //     cropId,
                //     count)
                L2_LOG_ERROR(
                    Log,
                    "Manor %d: procuring %lld units of crop %d left quota %lld in slot %d; current handling resets seed "
                    "stock in that slot and leaves the crop quota negative.",
                    m_data != nullptr ? m_data->m_nManorId : 0, static_cast<long long>(count), cropId,
                    static_cast<long long>(m_data->m_nProcuringCropCount[slot]), slot
                );

                // Current 0x0081f876 writes the parallel seed-remain lane,
                // not the now-negative crop-count lane. Keep that oddity.
                m_data->m_nSeedRemainCount[slot] = 0;
            } else {
                m_data->m_nProcuredCropList[slot] = runtime::AddInt64Wrapping(m_data->m_nProcuredCropList[slot], count);
            }
            break;
        }
    }
}

void CManor::SetInfo(
    const int residenceId,
    const std::int64_t seedSellBank,
    const std::int64_t procureBuyRemainBank,
    const std::uint8_t changeState,
    const SYSTEMTIME* const lastChanged
) {
    // Current L2Server 0x0081dc20, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    if (residenceId < 0) {
        return;
    }

    const int manorId = m_data != nullptr ? m_data->m_nManorId : 0;
    if (residenceId != m_data->m_nResidenceID) {
        CResidence* residence = g_ResidenceDB.GetResidence(m_data->m_nResidenceID);
        if (residence == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%S][%d]Cannot find residence in res[%d]manor[%d]",
            //     "Manor.cpp",
            //     0x5f,
            //     m_data->m_nResidenceID,
            //     manorId)
            // L2_LOG_ERROR(Log,
            //     L"[%S][%d]Cannot find residence in res[%d]manor[%d]",
            //     "Manor.cpp",
            //     0x5f,
            //     m_data->m_nResidenceID,
            //     manorId)
            L2_LOG_ERROR(
                Log,
                "Manor %d: previous residence %d was not found during reassignment to %d; reassignment and bank-state "
                "loading continue.",
                manorId, m_data->m_nResidenceID, residenceId
            );
        } else if (!residence->HaveManor(manorId)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Alread have manor in res[%d]manor[%d]",
            //     m_data->m_nResidenceID,
            //     manorId)
            // L2_LOG_ERROR(Log,
            //     L"Alread have manor in res[%d]manor[%d]",
            //     m_data->m_nResidenceID,
            //     manorId)
            L2_LOG_WARN(
                Log,
                "Manor %d is not registered with previous residence %d; reassignment to %d and bank-state loading continue.",
                manorId, m_data->m_nResidenceID, residenceId
            );
        } else {
            residence->DelManor(manorId);
        }

        m_data->m_nResidenceID = residenceId;
        if (residenceId > 0) {
            residence = g_ResidenceDB.GetResidence(m_data->m_nResidenceID);
            if (residence == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d]Cannot find residence in "
                //     L"res[%d]manor[%d]",
                //     "Manor.cpp",
                //     0x76,
                //     m_data->m_nResidenceID,
                //     manorId)
                // L2_LOG_ERROR(Log,
                //     L"[%S][%d]Cannot find residence in "
                //     L"res[%d]manor[%d]",
                //     "Manor.cpp",
                //     0x76,
                //     m_data->m_nResidenceID,
                //     manorId)
                L2_LOG_ERROR(
                    Log,
                    "Manor %d: new residence %d was not found; its ID is retained and bank-state loading continues without "
                    "registration.",
                    manorId, m_data->m_nResidenceID
                );
            } else if (!residence->HaveManor(manorId)) {
                residence->AddManor(manorId);
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Alread have manor in res[%d]manor[%d]",
                //     m_data->m_nResidenceID,
                //     manorId)
                // Noise: The new residence already lists this manor. No duplicate insertion is needed and the final
                // membership check still runs. L2_LOG_ERROR(Log,
                //     L"Alread have manor in res[%d]manor[%d]",
                //     m_data->m_nResidenceID,
                //     manorId)
            }
        }
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(m_data->m_nResidenceID);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find residence in res[%d]manor[%d]",
        //     "Manor.cpp",
        //     0x95,
        //     m_data->m_nResidenceID,
        //     manorId)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find residence in res[%d]manor[%d]",
        //     "Manor.cpp",
        //     0x95,
        //     m_data->m_nResidenceID,
        //     manorId)
        L2_LOG_ERROR(
            Log,
            "Manor %d: residence %d is unavailable after loading its ID; owner binding is skipped, but bank and "
            "change-state fields are still loaded.",
            manorId, m_data->m_nResidenceID
        );
    } else if (!residence->HaveManor(manorId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Don't has manor in res[%d]manor[%d]",
        //     m_data->m_nResidenceID,
        //     manorId)
        // L2_LOG_ERROR(Log,
        //     L"Don't has manor in res[%d]manor[%d]",
        //     m_data->m_nResidenceID,
        //     manorId)
        L2_LOG_ERROR(
            Log,
            "Manor %d is not registered with residence %d; owner binding is skipped, but bank and change-state fields are "
            "still loaded.",
            manorId, m_data->m_nResidenceID
        );
    } else {
        CPledge* const pledge = residence->GetResidenceOwnerPledge();
        if (pledge == nullptr) {
            m_data->m_nManorOwnerID = 0;
            m_data->m_nManorOwnerIndex = 0;
        } else {
            m_data->m_nManorOwnerID = pledge->GetDbId();
            m_data->m_nManorOwnerIndex = pledge->GetIndex();
        }
    }

    m_data->m_nSeedSellBank = seedSellBank;
    m_data->m_nProcureBuyRemainBank = procureBuyRemainBank;
    m_data->m_byChangeState = changeState;
    std::memcpy(&m_SystemTime, lastChanged, sizeof(m_SystemTime));
}

void CManor::SetSeedData(const std::int64_t currentData[40][10], const std::int64_t nextData[40][7]) {
    // Current L2Server 0x0081fe78, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    for (int row = 0; row < 40; ++row) {
        if (currentData[row][0] > 0) {
            const int seedId = static_cast<std::int32_t>(currentData[row][0]);
            const int index = GetManorSeedIndex(seedId);
            if (static_cast<std::uint32_t>(index) < 0x29U) {
                m_data->m_nSeedList[index] = seedId;
                m_data->m_nSeedPrice[index] = currentData[row][1];
                m_data->m_nSeedSetCount[index] = currentData[row][2];
                m_data->m_nSeedRemainCount[index] = currentData[row][3];
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] cannot find seed id, "
                //     L"manor[%d] seed[%d] now",
                //     "Manor.cpp",
                //     0xca,
                //     m_data != nullptr ? m_data->m_nManorId : 0,
                //     seedId)
                // L2_LOG_ERROR(Log,
                //     L"[%S][%d] cannot find seed id, "
                //     L"manor[%d] seed[%d] now",
                //     "Manor.cpp",
                //     0xca,
                //     m_data != nullptr ? m_data->m_nManorId : 0,
                //     seedId)
                L2_LOG_ERROR(
                    Log, "Manor %d: current-period seed %d has no configured slot; this database row is skipped.",
                    m_data != nullptr ? m_data->m_nManorId : 0, seedId
                );
            }
        }

        if (currentData[row][4] > 0) {
            const int cropId = static_cast<std::int32_t>(currentData[row][4]);
            const int index = GetManorCropIndex(cropId);
            if (static_cast<std::uint32_t>(index) < 0x29U) {
                m_data->m_nProcuringCropList[index] = cropId;
                m_data->m_nProcuringCropSetCount[index] = currentData[row][5];
                m_data->m_nProcuringCropCount[index] = currentData[row][6];
                m_data->m_nProcuringCropPrice[index] = currentData[row][7];
                m_data->m_nProcuringCropType[index] = static_cast<std::int32_t>(currentData[row][8]);
                m_data->m_nProcuredCropList[index] = currentData[row][9];
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] cannot find crop id, "
                //     L"manor[%d] seed[%d] now",
                //     "Manor.cpp",
                //     0xda,
                //     m_data != nullptr ? m_data->m_nManorId : 0,
                //     cropId)
                // L2_LOG_ERROR(Log,
                //     L"[%S][%d] cannot find crop id, "
                //     L"manor[%d] seed[%d] now",
                //     "Manor.cpp",
                //     0xda,
                //     m_data != nullptr ? m_data->m_nManorId : 0,
                //     cropId)
                L2_LOG_ERROR(
                    Log, "Manor %d: current-period crop %d has no configured slot; this database row is skipped.",
                    m_data != nullptr ? m_data->m_nManorId : 0, cropId
                );
            }
        }
    }

    for (int row = 0; row < 40; ++row) {
        if (nextData[row][0] > 0) {
            const int seedId = static_cast<std::int32_t>(nextData[row][0]);
            const int index = GetManorSeedIndex(seedId);
            if (static_cast<std::uint32_t>(index) < 0x29U) {
                m_data->m_nSeedList_n[index] = seedId;
                m_data->m_nSeedPrice_n[index] = nextData[row][1];
                m_data->m_nSeedSetCount_n[index] = nextData[row][2];
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] cannot find seed id, "
                //     L"manor[%d] seed[%d] next",
                //     "Manor.cpp",
                //     0xf0,
                //     m_data != nullptr ? m_data->m_nManorId : 0,
                //     seedId)
                // L2_LOG_ERROR(Log,
                //     L"[%S][%d] cannot find seed id, "
                //     L"manor[%d] seed[%d] next",
                //     "Manor.cpp",
                //     0xf0,
                //     m_data != nullptr ? m_data->m_nManorId : 0,
                //     seedId)
                L2_LOG_ERROR(
                    Log, "Manor %d: next-period seed %d has no configured slot; this database row is skipped.",
                    m_data != nullptr ? m_data->m_nManorId : 0, seedId
                );
            }
        }

        if (nextData[row][3] > 0) {
            const int cropId = static_cast<std::int32_t>(nextData[row][3]);
            const int index = GetManorCropIndex(cropId);
            if (static_cast<std::uint32_t>(index) < 0x29U) {
                m_data->m_nProcuringCropList_n[index] = cropId;
                m_data->m_nProcuringCropCount_n[index] = nextData[row][4];
                m_data->m_nProcuringCropPrice_n[index] = nextData[row][5];
                m_data->m_nProcuringCropType_n[index] = static_cast<std::int32_t>(nextData[row][6]);
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%S][%d] cannot find crop id, "
                //     L"manor[%d] crop[%d] next",
                //     "Manor.cpp",
                //     0xff,
                //     m_data != nullptr ? m_data->m_nManorId : 0,
                //     cropId)
                // L2_LOG_ERROR(Log,
                //     L"[%S][%d] cannot find crop id, "
                //     L"manor[%d] crop[%d] next",
                //     "Manor.cpp",
                //     0xff,
                //     m_data != nullptr ? m_data->m_nManorId : 0,
                //     cropId)
                L2_LOG_ERROR(
                    Log, "Manor %d: next-period crop %d has no configured slot; this database row is skipped.",
                    m_data != nullptr ? m_data->m_nManorId : 0, cropId
                );
            }
        }
    }
}

void CManor::ChcekDayChanged() {
    // Current L2Server 0x0081eb78 is an intentional guarded no-op.
    L2_GUARD_FUNCTION();
}

int CManor::GetManorSeedIndex(const int seedId) {
    // Current L2Server 0x0081e67c.
    L2_GUARD_FUNCTION();
    for (int index = 0; index < 40; ++index) {
        if (m_data->m_nSeedIdList[index] == seedId) {
            return index;
        }
    }
    return -1;
}

int CManor::GetManorCropIndex(const int cropId) {
    // Current L2Server 0x0081e738.
    L2_GUARD_FUNCTION();
    for (int index = 0; index < 40; ++index) {
        if (m_data->m_nCropIdList[index] == cropId) {
            return index;
        }
    }
    return -1;
}

int CManor::GetMaxCropCountById(const int cropId) {
    // Current L2Server 0x0081f8f4.
    L2_GUARD_FUNCTION();
    for (int index = 0; index < 40; ++index) {
        if (m_data->m_nCropIdList[index] == cropId) {
            return static_cast<int>(static_cast<double>(m_data->m_nCropMaxCount[index]) * m_dMaxChangeRate);
        }
    }
    return 0;
}

int CManor::GetMaxCropCount(const int index) {
    // Current L2Server 0x0081f9d4.
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(index) >= 40U) {
        return 0;
    }
    return static_cast<int>(static_cast<double>(m_data->m_nCropMaxCount[index]) * m_dMaxChangeRate);
}

void CManor::SetMaxCropCount(const int index, const int count) {
    // Current L2Server 0x0081fa94.
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(index) < 40U) {
        m_data->m_nCropMaxCount[index] = count;
    }
}

int CManor::GetMaxSeedCount(const int index) {
    // Current L2Server 0x0081fb40.
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(index) >= 40U) {
        return 0;
    }
    return static_cast<int>(static_cast<double>(m_data->m_nSeedMaxCount[index]) * m_dMaxChangeRate);
}

void CManor::SetMaxSeedCount(const int index, const int count) {
    // Current L2Server 0x0081fc00.
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(index) < 40U) {
        m_data->m_nSeedMaxCount[index] = count;
    }
}

std::int64_t CManor::CalcRemainSeedToAdena() {
    // Current L2Server 0x0081ebf4.
    L2_GUARD_FUNCTION();
    std::int64_t total = 0;
    for (int index = 0; index < 40; ++index) {
        const int itemClassId = m_data->m_nSeedList[index];
        if (itemClassId <= 0) {
            continue;
        }

        CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
        CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
        if (item == nullptr || item->m_const == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Item[%d] is not exist.",
            //     L"Manor.cpp",
            //     0x5cd,
            //     itemClassId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Item[%d] is not exist.",
            //     L"Manor.cpp",
            //     0x5cd,
            //     itemClassId)
            L2_LOG_ERROR(
                Log,
                "Manor %d: remaining-seed valuation skips class %d because its item prototype or constants are missing.",
                m_data != nullptr ? m_data->m_nManorId : 0, itemClassId
            );
            continue;
        }
        total += m_data->m_nSeedRemainCount[index] * item->m_const->m_nDefaultPrice;
    }
    return total;
}

std::int64_t CManor::CalcRemainCropToAdena() {
    // Current L2Server 0x0081ed48.
    L2_GUARD_FUNCTION();
    std::int64_t total = 0;
    for (int index = 0; index < 40; ++index) {
        if (m_data->m_nProcuringCropList[index] > 0) {
            total += m_data->m_nProcuringCropPrice[index] * m_data->m_nProcuringCropCount[index];
        }
    }
    return total;
}

std::int64_t CManor::CalcCropToAdena() {
    // Current L2Server 0x0081ee10.
    L2_GUARD_FUNCTION();
    std::int64_t total = 0;
    for (int index = 0; index < 40; ++index) {
        const std::int64_t procured = m_data->m_nProcuredCropList[index];
        if (procured <= 0) {
            continue;
        }
        const std::int64_t discounted = runtime::MultiplyInt64Wrapping(procured, 9) / 10;
        const std::int64_t quantity = discounted > 1 ? discounted : 1;
        total += m_data->m_nProcuringCropPrice[index] * quantity;
    }
    return total;
}

std::int64_t CManor::GetAdenaForNextSeed() {
    // Current L2Server 0x0081f2a4.
    L2_GUARD_FUNCTION();
    std::int64_t total = 0;
    for (int index = 0; index < 40; ++index) {
        const int itemClassId = m_data->m_nSeedList_n[index];
        if (itemClassId <= 0) {
            continue;
        }

        CObject* const object = g_ObjectDB.GetObjectW(itemClassId, false);
        CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
        if (item == nullptr || item->m_const == nullptr) {
            // The current body logs the current-period seed ID here rather
            // than the next-period ID. Retain that observable quirk.
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Item[%d] is not exist.",
            //     L"Manor.cpp",
            //     0x6c8,
            //     m_data->m_nSeedList[index])
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Item[%d] is not exist.",
            //     L"Manor.cpp",
            //     0x6c8,
            //     m_data->m_nSeedList[index])
            L2_LOG_ERROR(
                Log,
                "Manor %d: next-period seed funding skips class %d because its item prototype or constants are missing.",
                m_data != nullptr ? m_data->m_nManorId : 0, itemClassId
            );
            continue;
        }
        total += m_data->m_nSeedSetCount_n[index] * item->m_const->m_nDefaultPrice;
    }
    return total;
}

std::int64_t CManor::GetAdenaForNextCrop() {
    // Current L2Server 0x0081f400.
    L2_GUARD_FUNCTION();
    std::int64_t total = 0;
    for (int index = 0; index < 40; ++index) {
        if (m_data->m_nProcuringCropList_n[index] > 0) {
            total += m_data->m_nProcuringCropPrice_n[index] * m_data->m_nProcuringCropCount_n[index];
        }
    }
    return total;
}

void CManor::ClearSeedData() {
    // Current L2Server 0x0081eef8.
    L2_GUARD_FUNCTION();
    m_data->m_nSeedList.fill(0);
    m_data->m_nSeedPrice.fill(0);
    m_data->m_nSeedSetCount.fill(0);
    m_data->m_nSeedRemainCount.fill(0);
}

void CManor::ClearCropData() {
    // Current L2Server 0x0081efdc.
    L2_GUARD_FUNCTION();
    m_data->m_nProcuringCropList.fill(0);
    m_data->m_nProcuringCropSetCount.fill(0);
    m_data->m_nProcuringCropPrice.fill(0);
    m_data->m_nProcuringCropType.fill(0);
    m_data->m_nProcuringCropCount.fill(0);
    m_data->m_nProcuredCropList.fill(0);
}

void CManor::ClearNextSeedData() {
    // Current L2Server 0x0081f0ec.
    L2_GUARD_FUNCTION();
    m_data->m_nSeedList_n.fill(0);
    m_data->m_nSeedPrice_n.fill(0);
    m_data->m_nSeedSetCount_n.fill(0);
}

void CManor::ClearNextCropData() {
    // Current L2Server 0x0081f1bc.
    L2_GUARD_FUNCTION();
    m_data->m_nProcuringCropList_n.fill(0);
    m_data->m_nProcuringCropCount_n.fill(0);
    m_data->m_nProcuringCropPrice_n.fill(0);
    m_data->m_nProcuringCropType_n.fill(0);
}

void CManor::SaveNowManorInfo() {
    // Current L2Server 0x0081e92c.
    L2_GUARD_FUNCTION();
    const int manorId = m_data != nullptr ? m_data->m_nManorId : 0;
    const int residenceId = m_data != nullptr ? m_data->m_nResidenceID : 0;
    db.RequestSaveManorInfo(
        manorId, residenceId, runtime::Int32FromBits(m_data->m_nSeedSellBank),
        runtime::Int32FromBits(m_data->m_nProcureBuyRemainBank), m_data->m_byChangeState, &m_SystemTime
    );
}

void CManor::SaveNowSeedInfo(const int index) {
    // Current L2Server 0x0081ea0c.
    L2_GUARD_FUNCTION();
    {
        L2_WRITE_GUARD(*this);
        const int manorId = m_data != nullptr ? m_data->m_nManorId : 0;
        const int residenceId = m_data != nullptr ? m_data->m_nResidenceID : 0;
        db.RequestSaveManorNowSeed(
            manorId, residenceId, index, m_data->m_nSeedList[index], runtime::Int32FromBits(m_data->m_nSeedPrice[index]),
            runtime::Int32FromBits(m_data->m_nSeedSetCount[index]),
            runtime::Int32FromBits(m_data->m_nSeedRemainCount[index]), m_data->m_nProcuringCropList[index],
            runtime::Int32FromBits(m_data->m_nProcuringCropSetCount[index]),
            runtime::Int32FromBits(m_data->m_nProcuringCropCount[index]),
            runtime::Int32FromBits(m_data->m_nProcuringCropPrice[index]), m_data->m_nProcuringCropType[index],
            runtime::Int32FromBits(m_data->m_nProcuredCropList[index])
        );
    }
}

CPledge* CManor::GetManorOwnerPledge() {
    // Current L2Server 0x00820ad8.
    L2_GUARD_FUNCTION();
    if (m_nManorOwnerPledgeServerId == 0) {
        return nullptr;
    }

    if (CThreadManager::GetThreadID() < CThreadManager::GetThreadCount()) {
        return CPledge::GetObjectW(static_cast<std::uint32_t>(m_nManorOwnerPledgeServerId));
    }

    const int pledgeId = m_data != nullptr ? m_data->m_nManorOwnerID : 0;
    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeId);
    return pledge.get();
}

void CManor::SetPledge(CPledge* const pledge) {
    // Current L2Server 0x0081e598.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        m_data->m_nManorOwnerID = 0;
        m_data->m_nManorOwnerIndex = -1;
        m_nManorOwnerPledgeServerId = 0;
        return;
    }

    m_data->m_nManorOwnerID = static_cast<int>(pledge->GetDbId());
    m_data->m_nManorOwnerIndex = pledge->GetIndex();
    m_nManorOwnerPledgeServerId = static_cast<std::int32_t>(pledge->GetSmartID().Sid());
}

void CManor::SaveNextSeedInfo(const int index) {
    // Current L2Server 0x00820bf4.
    L2_GUARD_FUNCTION();
    const auto [manorId, residenceId] = [&] {
        L2_WRITE_GUARD(*this);
        const int manorId = m_data != nullptr ? m_data->m_nManorId : 0;
        const int residenceId = m_data != nullptr ? m_data->m_nResidenceID : 0;
        db.RequestSaveManorNextSeed(
            manorId, residenceId, index, m_data->m_nSeedList_n[index],
            runtime::Int32FromBits(m_data->m_nSeedPrice_n[index]),
            runtime::Int32FromBits(m_data->m_nSeedSetCount_n[index]), m_data->m_nProcuringCropList_n[index],
            runtime::Int32FromBits(m_data->m_nProcuringCropPrice_n[index]),
            runtime::Int32FromBits(m_data->m_nProcuringCropCount_n[index]), m_data->m_nProcuringCropType_n[index]
        );
        return std::array<int, 2>{manorId, residenceId};
    }();

    CPledge* const pledge = GetManorOwnerPledge();
    const std::u16string pledgeName = PledgeNameForLog(pledge);
    CLogFormatter formatter;
    formatter.WriteLog(
        0x10d, manorId, residenceId, m_data != nullptr ? m_data->m_nManorOwnerID : 0, 0, 0, 0, 0, u"", u"", u"",
        m_data->m_nSeedList_n[index], m_data->m_nSeedPrice_n[index], m_data->m_nSeedSetCount_n[index],
        m_data->m_nProcuringCropList_n[index], m_data->m_nProcuringCropPrice_n[index],
        m_data->m_nProcuringCropCount_n[index], m_data->m_nProcuringCropType_n[index], 0, 0, 0, u"", u"", pledgeName.c_str(),
        u"", 0
    );
}

void CManor::TransferSeedData() {
    // Current L2Server 0x0082060c.
    L2_GUARD_FUNCTION();
    for (int index = 0; index < 40; ++index) {
        m_data->m_nSeedList[index] = m_data->m_nSeedList_n[index];
        m_data->m_nSeedPrice[index] = m_data->m_nSeedPrice_n[index];
        const std::int64_t maximum = GetMaxSeedCount(index);
        if (m_data->m_nSeedSetCount_n[index] < maximum) {
            m_data->m_nSeedSetCount[index] = m_data->m_nSeedSetCount_n[index];
        } else {
            m_data->m_nSeedSetCount[index] = maximum;
            m_data->m_nSeedSetCount_n[index] = m_data->m_nSeedSetCount[index];
        }
        m_data->m_nSeedRemainCount[index] = m_data->m_nSeedSetCount[index];
    }
}

void CManor::TransferCropData() {
    // Current L2Server 0x0082074c.
    L2_GUARD_FUNCTION();
    for (int index = 0; index < 40; ++index) {
        m_data->m_nProcuringCropList[index] = m_data->m_nProcuringCropList_n[index];
        const std::int64_t maximum = GetMaxCropCount(index);
        if (m_data->m_nProcuringCropCount_n[index] < maximum) {
            m_data->m_nProcuringCropSetCount[index] = m_data->m_nProcuringCropCount_n[index];
        } else {
            m_data->m_nProcuringCropSetCount[index] = maximum;
            m_data->m_nProcuringCropCount_n[index] = m_data->m_nProcuringCropSetCount[index];
        }
        m_data->m_nProcuringCropCount[index] = m_data->m_nProcuringCropSetCount[index];
        m_data->m_nProcuringCropPrice[index] = m_data->m_nProcuringCropPrice_n[index];
        m_data->m_nProcuringCropType[index] = m_data->m_nProcuringCropType_n[index];
    }
}

void CManor::LogManorData() {
    // Current L2Server 0x00820460 writes one CSV row per active seed.
    L2_GUARD_FUNCTION();
    for (int index = 0; index < 40; ++index) {
        if (m_data->m_nSeedList[index] <= 0) {
            continue;
        }
        Log.AddT(
            LOG_BOTH,
            u"%I64d,%I64d,%I64d,%I64d,,,,,,,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,%I64d,,,,,",
            static_cast<std::int64_t>(0x10b), static_cast<std::int64_t>(m_data->m_nManorId),
            static_cast<std::int64_t>(m_data->m_nResidenceID), static_cast<std::int64_t>(m_data->m_nManorOwnerID),
            static_cast<std::int64_t>(index), static_cast<std::int64_t>(m_data->m_nSeedList[index]),
            m_data->m_nSeedPrice[index], m_data->m_nSeedSetCount[index], m_data->m_nSeedRemainCount[index],
            static_cast<std::int64_t>(m_data->m_nProcuringCropList[index]), m_data->m_nProcuringCropSetCount[index],
            m_data->m_nProcuringCropCount[index], m_data->m_nProcuringCropPrice[index],
            static_cast<std::int64_t>(m_data->m_nProcuringCropType[index]), m_data->m_nProcuredCropList[index]
        );
    }
}

void CManor::LogManorInfo() {
    // Current L2Server 0x008214fc.
    L2_GUARD_FUNCTION();
    const std::int64_t remainSeed = CalcRemainSeedToAdena();
    const std::int64_t remainCrop = CalcRemainCropToAdena();
    CPledge* const pledge = GetManorOwnerPledge();
    const std::u16string pledgeName = PledgeNameForLog(pledge);
    CLogFormatter formatter;
    formatter.WriteLog(
        0x10a, m_data != nullptr ? m_data->m_nManorId : 0, m_data != nullptr ? m_data->m_nResidenceID : 0,
        m_data != nullptr ? m_data->m_nManorOwnerID : 0, 0, 0, 0, 0, u"", u"", u"", m_data->m_nSeedSellBank,
        CalcCropToAdena(), remainSeed, remainCrop, 0, 0, 0, 0, 0, 0, u"", u"", pledgeName.c_str(), u"", 0
    );
}

void CManor::LogNextManorInfo(
    const std::int64_t nextSeedAdena, const std::int64_t nextCropAdena, const std::int64_t oldIncome
) {
    // Current L2Server 0x008217a8.
    L2_GUARD_FUNCTION();
    const std::int64_t nextTotal = nextSeedAdena + nextCropAdena;
    const int result = nextTotal == 0 ? 1 : (oldIncome < nextTotal ? 2 : 0);
    CPledge* const pledge = GetManorOwnerPledge();
    const std::u16string pledgeName = PledgeNameForLog(pledge);
    CLogFormatter formatter;
    formatter.WriteLog(
        0x10c, m_data != nullptr ? m_data->m_nManorId : 0, m_data != nullptr ? m_data->m_nResidenceID : 0,
        m_data != nullptr ? m_data->m_nManorOwnerID : 0, 0, 0, 0, 0, u"", u"", u"", nextSeedAdena, nextCropAdena, result, 0,
        0, 0, 0, 0, 0, 0, u"", u"", pledgeName.c_str(), u"", 0
    );
}

bool CManor::SetNextSeedInfo(const int seedId, const std::int64_t price, const std::int64_t count) {
    // Current L2Server 0x00820f58.
    L2_GUARD_FUNCTION();
    constexpr std::uint64_t kBudgetLimit = UINT64_C(1000000000000);

    const int index = GetManorSeedIndex(seedId);
    if (static_cast<unsigned int>(index) >= 0x29U) {
        return false;
    }

    if (static_cast<std::uint64_t>(price) * static_cast<std::uint64_t>(count) >= kBudgetLimit) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]invalid seed price(nSeedId : %d, %d, %d)",
        //     "Manor.cpp",
        //     0x527,
        //     seedId,
        //     price,
        //     count)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]invalid seed price(nSeedId : %d, %d, %d)",
        //     "Manor.cpp",
        //     0x527,
        //     seedId,
        //     price,
        //     count)
        L2_LOG_WARN(
            Log,
            "Manor %d: next-period seed %d failed the per-entry budget check (price %lld, quantity %lld; limit "
            "1000000000000 adena); the setting is rejected.",
            m_data != nullptr ? m_data->m_nManorId : 0, seedId, static_cast<long long>(price), static_cast<long long>(count)
        );
        return false;
    }

    std::uint64_t budget = 0;
    for (int slot = 0; slot < 40; ++slot) {
        if (m_data->m_nSeedIdList[slot] <= 0) {
            continue;
        }

        std::int64_t rowPrice = price;
        std::int64_t rowCount = count;
        if (m_data->m_nSeedList_n[slot] != seedId &&
            (m_data->m_nSeedIdList[slot] != seedId || m_data->m_nSeedList_n[slot] != 0)) {
            rowPrice = m_data->m_nSeedPrice_n[slot];
            rowCount = m_data->m_nSeedSetCount_n[slot];
        }
        budget += static_cast<std::uint64_t>(rowPrice) * static_cast<std::uint64_t>(rowCount);
        if (budget >= kBudgetLimit) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid price at file[%s], line[%d]",
            //     L"Manor.cpp",
            //     0x541)
            // L2_LOG_ERROR(Log,
            //     L"invalid price at file[%s], line[%d]",
            //     L"Manor.cpp",
            //     0x541)
            L2_LOG_WARN(
                Log,
                "Manor %d: next-period seed setting is rejected; calculated budget %llu adena reached the 1000000000000 "
                "limit while adding seed entries.",
                m_data != nullptr ? m_data->m_nManorId : 0, static_cast<unsigned long long>(budget)
            );
            return false;
        }
    }

    for (int slot = 0; slot < 40; ++slot) {
        if (m_data->m_nProcuringCropPrice_n[slot] < 1) {
            continue;
        }
        budget += static_cast<std::uint64_t>(m_data->m_nProcuringCropPrice_n[slot]) *
            static_cast<std::uint64_t>(m_data->m_nProcuringCropCount_n[slot]);
        if (budget >= kBudgetLimit) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid price at file[%s], line[%d]",
            //     L"Manor.cpp",
            //     0x550)
            // L2_LOG_ERROR(Log,
            //     L"invalid price at file[%s], line[%d]",
            //     L"Manor.cpp",
            //     0x550)
            L2_LOG_WARN(
                Log,
                "Manor %d: next-period seed setting is rejected; calculated budget %llu adena reached the 1000000000000 "
                "limit while adding crop entries.",
                m_data != nullptr ? m_data->m_nManorId : 0, static_cast<unsigned long long>(budget)
            );
            return false;
        }
    }

    {
        L2_WRITE_GUARD(*this);
        m_data->m_nSeedList_n[index] = seedId;
        m_data->m_nSeedPrice_n[index] = price;
        if (GetMaxSeedCount(index) < count) {
            m_data->m_nSeedSetCount_n[index] = GetMaxSeedCount(index);
        } else {
            m_data->m_nSeedSetCount_n[index] = count;
        }
    }
    SaveNextSeedInfo(index);
    return true;
}

bool CManor::SetNextProcureInfo(const int cropId, const std::int64_t price, const std::int64_t count, const int type) {
    // Current L2Server 0x00821200.
    L2_GUARD_FUNCTION();
    constexpr std::uint64_t kBudgetLimit = UINT64_C(1000000000000);

    const int index = GetManorCropIndex(cropId);
    if (index == -1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]Cannot find Crop index. m[%d] c[%d]",
        //     "Manor.cpp",
        //     0x571,
        //     m_data != nullptr ? m_data->m_nManorId : 0,
        //     cropId)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]Cannot find Crop index. m[%d] c[%d]",
        //     "Manor.cpp",
        //     0x571,
        //     m_data != nullptr ? m_data->m_nManorId : 0,
        //     cropId)
        L2_LOG_WARN(
            Log, "Manor %d: crop %d has no configured slot; the next-period procurement setting is rejected.",
            m_data != nullptr ? m_data->m_nManorId : 0, cropId
        );
        return false;
    }

    if (static_cast<std::uint64_t>(price) * static_cast<std::uint64_t>(count) >= kBudgetLimit) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]invalid crop price(nCropId : %d, %I64d, %I64d)",
        //     "Manor.cpp",
        //     0x578,
        //     cropId,
        //     price,
        //     count)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]invalid crop price(nCropId : %d, %I64d, %I64d)",
        //     "Manor.cpp",
        //     0x578,
        //     cropId,
        //     price,
        //     count)
        L2_LOG_WARN(
            Log,
            "Manor %d: next-period crop %d failed the per-entry budget check (price %lld, quantity %lld; limit "
            "1000000000000 adena); the setting is rejected.",
            m_data != nullptr ? m_data->m_nManorId : 0, cropId, static_cast<long long>(price), static_cast<long long>(count)
        );
        return false;
    }

    std::uint64_t budget = 0;
    for (int slot = 0; slot < 40; ++slot) {
        if (m_data->m_nSeedPrice_n[slot] > 0) {
            budget += static_cast<std::uint64_t>(m_data->m_nSeedPrice_n[slot]) *
                static_cast<std::uint64_t>(m_data->m_nSeedSetCount_n[slot]);
            if (budget >= kBudgetLimit) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"invalid price at file[%s], line[%d]",
                //     L"Manor.cpp",
                //     0x586)
                // L2_LOG_ERROR(Log,
                //     L"invalid price at file[%s], line[%d]",
                //     L"Manor.cpp",
                //     0x586)
                L2_LOG_WARN(
                    Log,
                    "Manor %d: next-period crop setting is rejected; calculated budget %llu adena reached the 1000000000000 "
                    "limit while adding seed entries.",
                    m_data != nullptr ? m_data->m_nManorId : 0, static_cast<unsigned long long>(budget)
                );
                return false;
            }
        }
    }

    for (int slot = 0; slot < 40; ++slot) {
        if (m_data->m_nCropIdList[slot] <= 0) {
            continue;
        }

        std::int64_t rowPrice = price;
        std::int64_t rowCount = count;
        if (m_data->m_nProcuringCropList_n[slot] != cropId &&
            (m_data->m_nCropIdList[slot] != cropId || m_data->m_nProcuringCropList_n[slot] != 0)) {
            rowPrice = m_data->m_nProcuringCropPrice_n[slot];
            rowCount = m_data->m_nProcuringCropCount_n[slot];
        }
        budget += static_cast<std::uint64_t>(rowPrice) * static_cast<std::uint64_t>(rowCount);
        if (budget >= kBudgetLimit) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"invalid price at file[%s], line[%d]",
            //     L"Manor.cpp",
            //     0x5a0)
            // L2_LOG_ERROR(Log,
            //     L"invalid price at file[%s], line[%d]",
            //     L"Manor.cpp",
            //     0x5a0)
            L2_LOG_WARN(
                Log,
                "Manor %d: next-period crop setting is rejected; calculated budget %llu adena reached the 1000000000000 "
                "limit while adding crop entries.",
                m_data != nullptr ? m_data->m_nManorId : 0, static_cast<unsigned long long>(budget)
            );
            return false;
        }
    }

    {
        L2_WRITE_GUARD(*this);
        m_data->m_nProcuringCropList_n[index] = cropId;
        m_data->m_nProcuringCropPrice_n[index] = price;
        if (GetMaxCropCount(index) < count) {
            m_data->m_nProcuringCropCount_n[index] = GetMaxCropCount(index);
        } else {
            m_data->m_nProcuringCropCount_n[index] = count;
        }
        m_data->m_nProcuringCropType_n[index] = type;
    }
    SaveNextSeedInfo(index);
    return true;
}

void CManor::CalcNextManor() {
    // Current L2Server 0x00821a54.
    L2_GUARD_FUNCTION();
    const int residenceId = m_data != nullptr ? m_data->m_nResidenceID : 0;
    if (residenceId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]can not find residence. manor[%d]",
        //     "Manor.cpp",
        //     0x6ee,
        //     m_data != nullptr ? m_data->m_nManorId : 0)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]can not find residence. manor[%d]",
        //     "Manor.cpp",
        //     0x6ee,
        //     m_data != nullptr ? m_data->m_nManorId : 0)
        L2_LOG_ERROR(
            Log, "Manor %d: cannot calculate the next period because residence ID %d is invalid; the operation is skipped.",
            m_data != nullptr ? m_data->m_nManorId : 0, residenceId
        );
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]can not find residence. resi[%d], manor[%d]",
        //     "Manor.cpp",
        //     0x6f4,
        //     residenceId,
        //     m_data != nullptr ? m_data->m_nManorId : 0)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]can not find residence. resi[%d], manor[%d]",
        //     "Manor.cpp",
        //     0x6f4,
        //     residenceId,
        //     m_data != nullptr ? m_data->m_nManorId : 0)
        L2_LOG_ERROR(
            Log, "Manor %d: cannot calculate the next period because residence %d was not found; the operation is skipped.",
            m_data != nullptr ? m_data->m_nManorId : 0, residenceId
        );
        return;
    }

    CPledge* const owner = residence->GetResidenceOwnerPledge();
    if (owner == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d][%S]no pledge. resi[%d]",
        //     "Manor.cpp",
        //     0x6fa,
        //     "CManor::CalcNextManor",
        //     residenceId)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d][%S]no pledge. resi[%d]",
        //     "Manor.cpp",
        //     0x6fa,
        //     "CManor::CalcNextManor",
        //     residenceId)
        // Noise: An unowned residence has no operating clan. Keep missing data or a declared owner that cannot be resolved
        // visible.
        if (residence->m_data == nullptr || residence->m_data->m_nResidenceOwnerID != 0 ||
            residence->m_nResidenceOwnerPledgeServerId != 0) {
            L2_LOG_WARN(
                Log,
                "Manor %d: cannot calculate the next period for residence %d because its owner clan is unavailable (clan DB "
                "ID %u, SID %u); the operation is skipped.",
                m_data != nullptr ? m_data->m_nManorId : 0, residenceId,
                residence->m_data != nullptr ? static_cast<unsigned>(residence->m_data->m_nResidenceOwnerID) : 0U,
                static_cast<unsigned>(residence->m_nResidenceOwnerPledgeServerId)
            );
        }
        return;
    }

    bool cleared = false;
    bool changed = false;
    {
        L2_WRITE_GUARD(*this);
        if (m_data->m_byChangeState == 0) {
            const std::int64_t nextSeedAdena = GetAdenaForNextSeed();
            const std::int64_t nextCropAdena = GetAdenaForNextCrop();
            const std::int64_t total = nextSeedAdena + nextCropAdena;
            const std::int64_t oldIncome = residence->m_data->m_nTaxIncome;
            User* const master = owner->GetMaster();
            if (oldIncome < total || oldIncome == 0) {
                if (master != nullptr && master->GetSocket() != nullptr) {
                    master->GetSocket()->SendSystemMessage(SystemMessageId::MANOR_CANNOT_OPERATE_INSUFFICIENT_FUNDS);
                }
                ClearNextSeedData();
                ClearNextCropData();
                cleared = true;
            } else {
                residence->ChangeTaxIncome(total);
                residence->SaveIncome(residence->IsCastle(), 5);
                if (master != nullptr && master->GetSocket() != nullptr) {
                    master->GetSocket()->SendSystemMessage_d(
                        SystemMessageId::ADENA_REQUIRED_TO_OPERATE_MANOR, runtime::Int32FromBits(total)
                    );
                }
            }
            LogNextManorInfo(nextSeedAdena, nextCropAdena, oldIncome);
            SetCurrentLocalTime(m_SystemTime);
            m_data->m_byChangeState = 1;
            changed = true;
        }
    }

    if (cleared) {
        for (int index = 0; index < 40; ++index) {
            SaveNowSeedInfo(index);
            SaveNextSeedInfo(index);
        }
    }
    if (changed) {
        {
            L2_WRITE_GUARD(*this);
            SaveNowManorInfo();
        }
    }
}

void CManor::TransferManorData() {
    // Current L2Server 0x00821d7c.
    L2_GUARD_FUNCTION();
    const int residenceId = m_data != nullptr ? m_data->m_nResidenceID : 0;
    if (residenceId < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]can not find residence. manor[%d]",
        //     "Manor.cpp",
        //     0x74a,
        //     m_data != nullptr ? m_data->m_nManorId : 0)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]can not find residence. manor[%d]",
        //     "Manor.cpp",
        //     0x74a,
        //     m_data != nullptr ? m_data->m_nManorId : 0)
        L2_LOG_ERROR(
            Log,
            "Manor %d: cannot transfer data to the next period because residence ID %d is invalid; the operation is "
            "skipped.",
            m_data != nullptr ? m_data->m_nManorId : 0, residenceId
        );
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d]can not find residence. resi[%d], manor[%d]",
        //     "Manor.cpp",
        //     0x750,
        //     residenceId,
        //     m_data != nullptr ? m_data->m_nManorId : 0)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d]can not find residence. resi[%d], manor[%d]",
        //     "Manor.cpp",
        //     0x750,
        //     residenceId,
        //     m_data != nullptr ? m_data->m_nManorId : 0)
        L2_LOG_ERROR(
            Log,
            "Manor %d: cannot transfer data to the next period because residence %d was not found; the operation is "
            "skipped.",
            m_data != nullptr ? m_data->m_nManorId : 0, residenceId
        );
        return;
    }

    CPledge* const owner = residence->GetResidenceOwnerPledge();
    if (owner == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d][%S]no pledge. resi[%d]",
        //     "Manor.cpp",
        //     0x756,
        //     "CManor::TransferManorData",
        //     residenceId)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d][%S]no pledge. resi[%d]",
        //     "Manor.cpp",
        //     0x756,
        //     "CManor::TransferManorData",
        //     residenceId)
        // Noise: An unowned residence has no operating clan. Keep missing data or a declared owner that cannot be resolved
        // visible.
        if (residence->m_data == nullptr || residence->m_data->m_nResidenceOwnerID != 0 ||
            residence->m_nResidenceOwnerPledgeServerId != 0) {
            L2_LOG_WARN(
                Log,
                "Manor %d: cannot transfer data to the next period for residence %d because its owner clan is unavailable "
                "(clan DB ID %u, SID %u); the operation is skipped.",
                m_data != nullptr ? m_data->m_nManorId : 0, residenceId,
                residence->m_data != nullptr ? static_cast<unsigned>(residence->m_data->m_nResidenceOwnerID) : 0U,
                static_cast<unsigned>(residence->m_nResidenceOwnerPledgeServerId)
            );
        }
        return;
    }

    bool saveRows = false;
    bool saveManor = false;
    {
        L2_WRITE_GUARD(*this);
        if (m_data->m_byChangeState == 1) {
            const int ownerId = residence->m_data != nullptr ? static_cast<int>(residence->m_data->m_nResidenceOwnerID) : 0;
            const std::int64_t remainSeed = CalcRemainSeedToAdena();
            LogManorInfo();
            LogManorData();
            ClearSeedData();
            residence->ChangeTaxIncome(-remainSeed);

            const std::int64_t remainCrop = CalcRemainCropToAdena();
            std::array<char, 0x1000> buffer{};
            int byteCount = 0;
            int rowCount = 0;
            for (int index = 0; index < 40; ++index) {
                const int cropClassId = m_data->m_nProcuringCropList[index];
                if (cropClassId <= 0 || m_data->m_nProcuredCropList[index] <= 0) {
                    continue;
                }

                CObject* const object = g_ObjectDB.GetObjectW(cropClassId, false);
                CItem* const item = object != nullptr && object->IsItem() ? static_cast<CItem*>(object) : nullptr;
                if (item == nullptr || item->m_const == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Item[%d] is not exist.",
                    //     L"Manor.cpp",
                    //     0x77c,
                    //     cropClassId)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] Item[%d] is not exist.",
                    //     L"Manor.cpp",
                    //     0x77c,
                    //     cropClassId)
                    L2_LOG_ERROR(
                        Log,
                        "Manor %d: procured crop class %d has no usable item prototype; its transfer entry is skipped and "
                        "the period rollover continues.",
                        m_data != nullptr ? m_data->m_nManorId : 0, cropClassId
                    );
                    continue;
                }

                const std::int32_t procured = runtime::Int32FromBits(m_data->m_nProcuredCropList[index]);
                const std::int32_t discounted = runtime::MultiplyInt32Wrapping(procured, 9) / 10;
                const int amount = discounted > 1 ? discounted : 1;
                const int written = ::Assemble(
                    buffer.data() + byteCount, static_cast<int>(buffer.size()) - byteCount, "dd",
                    item->m_const->m_nManorCropIdReplace, amount
                );
                if (written > 0) {
                    byteCount += written;
                    ++rowCount;
                }
            }

            if (rowCount > 0) {
                db.RequestManorMoveCrop(
                    m_data != nullptr ? m_data->m_nManorId : 0, residenceId, ownerId, rowCount, byteCount, buffer.data()
                );
            }
            ClearCropData();

            User* const master = owner->GetMaster();
            if (master != nullptr && master->GetSocket() != nullptr) {
                master->GetSocket()->SendSystemMessage_Q(SystemMessageId::FUNDS_REMAINING_AFTER_SEED_SALES, remainSeed);
                master->GetSocket()->SendSystemMessage_Q(SystemMessageId::SEED_SALE_AMOUNT, m_data->m_nSeedSellBank);
                master->GetSocket()->SendSystemMessage_Q(SystemMessageId::REMAINING_CROP_PURCHASE_FUNDS, remainCrop);
            }

            residence->ChangeTaxIncome(-remainCrop);
            residence->ChangeTaxIncome(-m_data->m_nSeedSellBank);
            m_data->m_nSeedSellBank = 0;
            residence->SaveIncome(residence->IsCastle(), 6);
            TransferSeedData();
            TransferCropData();
            saveRows = true;
            SetCurrentLocalTime(m_SystemTime);
            m_data->m_byChangeState = 0;
            saveManor = true;
        }
    }

    if (saveRows) {
        for (int index = 0; index < 40; ++index) {
            SaveNowSeedInfo(index);
            SaveNextSeedInfo(index);
        }
    }
    if (saveManor) {
        {
            L2_WRITE_GUARD(*this);
            SaveNowManorInfo();
        }
    }
}

bool CManorPacketHandler::RequestManorList(CSocket* const socket, const std::uint8_t* const /*packet*/) {
    // Current L2Server 0x00822b30, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    std::array<char, 0x5000> buffer;
    int dataSize = 0;
    int manorCount = 0;
    for (const auto& entry : g_ManorDB.m_ManorIDMap) {
        CManor* const manor = entry.second;
        const int manorId = manor->m_data != nullptr ? manor->m_data->m_nManorId : 0;
        dataSize += ::Assemble(buffer.data() + dataSize, static_cast<int>(buffer.size()) - dataSize, "d", manorId);
        ++manorCount;
    }
    socket->Send("chdb", 0xfe, 0x22, manorCount, dataSize, buffer.data());
    return false;
}

bool CManorPacketHandler::ProcureCropList(CSocket* const socket, const std::uint8_t* const packet) {
    // Current L2Server 0x00827a00, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    int cropCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "d", &cropCount);
    const std::uint8_t* const cropRows = cursor;

    auto* const userSocket = static_cast<CUserSocket*>(socket);
    User* const user = userSocket->GetUser();
    if (user == nullptr) {
        return true;
    }

    CObject* const targetObject = [&] {
        L2_GUARD(*user->m_lock);
        return user->m_spTarget.GetObjectW();
    }();
    if (targetObject == nullptr) {
        return false;
    }
    if (cropCount >= 41) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] crop count: %d, name [%s]",
        //     L"Manor.cpp",
        //     0xc68,
        //     cropCount,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] crop count: %d, name [%s]",
        //     L"Manor.cpp",
        //     0xc68,
        //     cropCount,
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Rejected manor crop trade from character '%s': %d entries exceed the limit of 40.",
        //     WideToUtf8(user->GetName()).c_str(), cropCount)
        L2_LOG_WARN(
            Log, "Rejected manor crop trade from character '%s': %d entries exceed the limit of 40.",
            Utf16ToUtf8(user->GetName()).c_str(), cropCount
        );
        return false;
    }
    if (!TalkChannelCache::inst().mChannels[3].IsValidChannel(user->Id(), targetObject->Id(), 0xfa, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user[%s] request ProcureCropList with invalid talk channel.",
        //     L"Manor.cpp",
        //     0xc6e,
        //     user->GetName())
        // Noise: An expired or missing cached NPC dialogue is an ordinary stale-menu refusal. The existing validation still
        // rejects the request. L2_LOG_ERROR(Log,
        //     L"[%s][%d] user[%s] request ProcureCropList with invalid talk channel.",
        //     L"Manor.cpp",
        //     0xc6e,
        //     user->GetName())
        return false;
    }

    CCreature* const targetCreature = static_cast<CCreature*>(targetObject);
    CSPointer<CCreature> target = CCreatureFactory::Inst().FindObjectSP(targetCreature->m_data->index);
    CSPointer<CCreature> requester = CCreatureFactory::Inst().FindObjectSP(user->m_data->index);
    if (!target || !target->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]invalid target npc",
        //     L"Manor.cpp",
        //     0xc76)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]invalid target npc",
        //     L"Manor.cpp",
        //     0xc76)
        L2_LOG_WARN(
            Log,
            "Cannot trade crops for character DB ID %u: the target no longer resolves to an NPC; the request is rejected.",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    if (!requester || !requester->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]invalid user",
        //     L"Manor.cpp",
        //     0xc7b)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]invalid user",
        //     L"Manor.cpp",
        //     0xc7b)
        L2_LOG_WARN(
            Log,
            "Cannot trade crops for character DB ID %u: the requester no longer resolves to a player; the request is "
            "rejected.",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }

    CManor* const timeManor = g_ManorDB.GetManor(1);
    if (timeManor == nullptr || timeManor->ManorCheckTime()) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::MANOR_SYSTEM_UNDER_MAINTENANCE);
        return false;
    }

    std::int64_t totalCommission = 0;
    cursor = cropRows;
    for (int index = 0; index < cropCount; ++index) {
        int itemServerId = 0;
        int cropId = 0;
        int manorId = 0;
        std::int64_t amount = 0;
        cursor = ::Disassemble(cursor, "dddQ", &itemServerId, &cropId, &manorId, &amount);

        CManor* const manor = g_ManorDB.GetManor(manorId);
        const std::uint64_t amountMinusOne = static_cast<std::uint64_t>(amount) - UINT64_C(1);
        if (manor == nullptr || amountMinusOne > UINT64_C(0xf423e)) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]Hack? manor id(%d) sell count(%I64d)",
            //     L"Manor.cpp",
            //     0xc9d,
            //     manorId,
            //     amount)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d]Hack? manor id(%d) sell count(%I64d)",
            //     L"Manor.cpp",
            //     0xc9d,
            //     manorId,
            //     amount)
            L2_LOG_WARN(
                Log, "Rejected crop trade from character DB ID %u: manor %d, crop %d, quantity %lld; %s.",
                static_cast<unsigned>(user->GetDBID()), manorId, cropId, static_cast<long long>(amount),
                manor == nullptr ? "manor lookup failed" : "quantity is outside 1..999999"
            );
            return false;
        }
        if (target->m_data->m_nResidenceID != manorId) {
            totalCommission =
                runtime::AddInt64Wrapping(totalCommission, manor->GetCommissionForProcureCrop(cropId, amount));
        }
    }

    const std::int64_t adena = [&] {
        L2_READ_GUARD(user->m_inv);
        return user->m_inv.GetAdenaAmount();
    }();
    if (totalCommission > adena) {
        userSocket->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA_FOR_OTHER_CASTLE_CROP_PURCHASE);
        return false;
    }

    cursor = cropRows;
    User* const requesterUser = requester->CastUser();
    for (int index = 0; index < cropCount; ++index) {
        int itemServerId = 0;
        int cropId = 0;
        int manorId = 0;
        std::int64_t amount = 0;
        cursor = ::Disassemble(cursor, "dddQ", &itemServerId, &cropId, &manorId, &amount);

        CManor* const manor = g_ManorDB.GetManor(manorId);
        if (manor == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d]Hack? invalid manor id(%d)",
            //     L"Manor.cpp",
            //     0xcd3,
            //     manorId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d]Hack? invalid manor id(%d)",
            //     L"Manor.cpp",
            //     0xcd3,
            //     manorId)
            L2_LOG_WARN(
                Log,
                "Crop trade for character DB ID %u skips crop %d because manor %d is unavailable; other entries continue.",
                static_cast<unsigned>(user->GetDBID()), cropId, manorId
            );
            continue;
        }

        std::int64_t commission = 0;
        const bool sameResidence = target->m_data->m_nResidenceID == manorId;
        const bool procured = manor->ProcureCrop(requesterUser, itemServerId, cropId, amount, sameResidence, commission);
        socket->Send(
            "chccdcQ", 0x62, procured ? SystemMessageId::CROPS_TRADED : SystemMessageId::CROP_TRADE_FAILED, 2, 3, cropId, 6,
            amount
        );
        if (procured && commission > 0) {
            // The current variadic call supplies an int64 value to the final
            // `d` field. Keep the call-site mismatch literally.
            socket->Send("chccd", 0x62, SystemMessageId::ADENA_PAID_FOR_CROP_PROCUREMENT_COMMISSION, 1, 6, commission);
        }
    }
    return false;
}

bool CManorPacketHandler::SetSeed(CSocket* const socket, const std::uint8_t* const packet) {
    // Current L2Server 0x00828208, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    int manorId;
    int seedCount;
    const std::uint8_t* cursor = ::Disassemble(packet, "dd", &manorId, &seedCount);
    CManor* const manor = g_ManorDB.GetManor(manorId);
    if (manor == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid manor(%d)",
        //     L"Manor.cpp",
        //     0xcef,
        //     manorId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid manor(%d)",
        //     L"Manor.cpp",
        //     0xcef,
        //     manorId)
        L2_LOG_WARN(Log, "Cannot set seed sales for manor %d: manor lookup failed; the request is rejected.", manorId);
        return false;
    }

    auto* const userSocket = static_cast<CUserSocket*>(socket);
    User* const user = userSocket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (seedCount >= 41) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] seed count: %d, name [%s]",
        //     L"Manor.cpp",
        //     0xcf7,
        //     seedCount,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] seed count: %d, name [%s]",
        //     L"Manor.cpp",
        //     0xcf7,
        //     seedCount,
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Rejected manor seed-setting request from character '%s': %d entries exceed the limit of 40.",
        //     WideToUtf8(user->GetName()).c_str(), seedCount)
        L2_LOG_WARN(
            Log, "Rejected manor seed-setting request from character '%s': %d entries exceed the limit of 40.",
            Utf16ToUtf8(user->GetName()).c_str(), seedCount
        );
        return false;
    }
    if (!g_L2Time.IsManorSettingEnableTime() || manor->m_data->m_byChangeState != 0) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::MANOR_CONFIGURATION_UNAVAILABLE_DURING_RESTRICTED_HOURS);
        return false;
    }

    CObject* const targetObject = [&] {
        L2_GUARD(*user->m_lock);
        return user->m_spTarget.GetObjectW();
    }();
    if (targetObject == nullptr) {
        return false;
    }
    if (!TalkChannelCache::inst().mChannels[3].IsValidChannel(user->Id(), targetObject->Id(), 0xfa, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user[%s] request ShowProcureCropDetail with invalid talk channel.",
        //     L"Manor.cpp",
        //     0xd0d,
        //     user->GetName())
        // Noise: An expired or missing cached NPC dialogue is an ordinary stale-menu refusal. The existing validation still
        // rejects the request. L2_LOG_ERROR(Log,
        //     L"[%s][%d] user[%s] request ShowProcureCropDetail with invalid talk channel.",
        //     L"Manor.cpp",
        //     0xd0d,
        //     user->GetName())
        return false;
    }
    if (!user->IsMyManor(manorId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] hack? you(%s)'re not manor(%d) owner.",
        //     L"Manor.cpp",
        //     0xd13,
        //     user->GetName(),
        //     manorId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] hack? you(%s)'re not manor(%d) owner.",
        //     L"Manor.cpp",
        //     0xd13,
        //     user->GetName(),
        //     manorId)
        // L2_LOG_WARN(Log,
        //     "Rejected seed sales setting for manor %d: character '%s' failed the ownership check.",
        //     manorId, WideToUtf8(user->GetName()).c_str())
        L2_LOG_WARN(
            Log, "Rejected seed sales setting for manor %d: character '%s' failed the ownership check.", manorId,
            Utf16ToUtf8(user->GetName()).c_str()
        );
        return false;
    }

    CCreature* const targetCreature = static_cast<CCreature*>(targetObject);
    CSPointer<CCreature> target = CCreatureFactory::Inst().FindObjectSP(targetCreature->m_data->index);
    CSPointer<CCreature> requester = CCreatureFactory::Inst().FindObjectSP(user->m_data->index);
    if (!target || !target->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]invalid target npc",
        //     L"Manor.cpp",
        //     0xd1b)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]invalid target npc",
        //     L"Manor.cpp",
        //     0xd1b)
        L2_LOG_WARN(
            Log,
            "Cannot set seed sales for character DB ID %u: the target no longer resolves to an NPC; the request is "
            "rejected.",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    if (!requester || !requester->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]invalid user",
        //     L"Manor.cpp",
        //     0xd20)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]invalid user",
        //     L"Manor.cpp",
        //     0xd20)
        L2_LOG_WARN(
            Log,
            "Cannot set seed sales for character DB ID %u: the requester no longer resolves to a player; the request is "
            "rejected.",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }

    const int residenceId = target->m_data->m_nResidenceID;
    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr || !residence->HaveManor(manorId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]invalid residence id(%d)",
        //     L"Manor.cpp",
        //     0xd26,
        //     residenceId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]invalid residence id(%d)",
        //     L"Manor.cpp",
        //     0xd26,
        //     residenceId)
        L2_LOG_WARN(
            Log, "Cannot set seed sales for manor %d: NPC residence %d %s; the request is rejected.", manorId, residenceId,
            residence == nullptr ? "was not found" : "does not list this manor"
        );
        return false;
    }

    int seedId = 0;
    std::int64_t count;
    std::int64_t price;
    for (int index = 0; index < seedCount; ++index) {
        cursor = ::Disassemble(cursor, "dQQ", &seedId, &count, &price);
        residence->SetNextSeedInfo(target->m_data->index, user->m_data->index, residenceId, manorId, seedId, count, price);
    }
    manor->SayNeedSeedMoney(requester->CastUser());
    return false;
}

bool CManorPacketHandler::SetCrop(CSocket* const socket, const std::uint8_t* const packet) {
    // Current L2Server 0x00828910, original owner Manor.cpp.
    L2_GUARD_FUNCTION();

    int manorId = 0;
    int cropCount = 0;
    const std::uint8_t* cursor = ::Disassemble(packet, "dd", &manorId, &cropCount);
    CManor* const manor = g_ManorDB.GetManor(manorId);
    if (manor == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid manor(%d)",
        //     L"Manor.cpp",
        //     0xd4e,
        //     manorId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid manor(%d)",
        //     L"Manor.cpp",
        //     0xd4e,
        //     manorId)
        L2_LOG_WARN(Log, "Cannot set crop procurement for manor %d: manor lookup failed; the request is rejected.", manorId);
        return false;
    }

    auto* const userSocket = static_cast<CUserSocket*>(socket);
    User* const user = userSocket->GetUser();
    if (user == nullptr) {
        return true;
    }
    if (cropCount >= 41) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] crop count: %d, name [%s]",
        //     L"Manor.cpp",
        //     0xd56,
        //     cropCount,
        //     user->GetName())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] crop count: %d, name [%s]",
        //     L"Manor.cpp",
        //     0xd56,
        //     cropCount,
        //     user->GetName())
        // L2_LOG_WARN(Log,
        //     "Rejected manor crop-setting request from character '%s': %d entries exceed the limit of 40.",
        //     WideToUtf8(user->GetName()).c_str(), cropCount)
        L2_LOG_WARN(
            Log, "Rejected manor crop-setting request from character '%s': %d entries exceed the limit of 40.",
            Utf16ToUtf8(user->GetName()).c_str(), cropCount
        );
        return false;
    }
    if (!g_L2Time.IsManorSettingEnableTime() || manor->m_data->m_byChangeState != 0) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::MANOR_CONFIGURATION_UNAVAILABLE_DURING_RESTRICTED_HOURS);
        return false;
    }

    CObject* const targetObject = [&] {
        L2_GUARD(*user->m_lock);
        return user->m_spTarget.GetObjectW();
    }();
    if (targetObject == nullptr) {
        return false;
    }
    if (!TalkChannelCache::inst().mChannels[3].IsValidChannel(user->Id(), targetObject->Id(), 0xfa, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user[%s] request ShowProcureCropDetail with invalid talk channel.",
        //     L"Manor.cpp",
        //     0xd6c,
        //     user->GetName())
        // Noise: An expired or missing cached NPC dialogue is an ordinary stale-menu refusal. The existing validation still
        // rejects the request. L2_LOG_ERROR(Log,
        //     L"[%s][%d] user[%s] request ShowProcureCropDetail with invalid talk channel.",
        //     L"Manor.cpp",
        //     0xd6c,
        //     user->GetName())
        return false;
    }
    if (!user->IsMyManor(manorId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] hack? you(%s)'re not manor(%d) owner.",
        //     L"Manor.cpp",
        //     0xd72,
        //     user->GetName(),
        //     manorId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] hack? you(%s)'re not manor(%d) owner.",
        //     L"Manor.cpp",
        //     0xd72,
        //     user->GetName(),
        //     manorId)
        // L2_LOG_WARN(Log,
        //     "Rejected crop procurement setting for manor %d: character '%s' failed the ownership check.",
        //     manorId, WideToUtf8(user->GetName()).c_str())
        L2_LOG_WARN(
            Log, "Rejected crop procurement setting for manor %d: character '%s' failed the ownership check.", manorId,
            Utf16ToUtf8(user->GetName()).c_str()
        );
        return false;
    }

    CCreature* const targetCreature = static_cast<CCreature*>(targetObject);
    CSPointer<CCreature> target = CCreatureFactory::Inst().FindObjectSP(targetCreature->m_data->index);
    CSPointer<CCreature> requester = CCreatureFactory::Inst().FindObjectSP(user->m_data->index);
    if (!target || !target->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]invalid target npc",
        //     L"Manor.cpp",
        //     0xd7a)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]invalid target npc",
        //     L"Manor.cpp",
        //     0xd7a)
        L2_LOG_WARN(
            Log,
            "Cannot set crop procurement for character DB ID %u: the target no longer resolves to an NPC; the request is "
            "rejected.",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }
    if (!requester || !requester->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]invalid user",
        //     L"Manor.cpp",
        //     0xd7f)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]invalid user",
        //     L"Manor.cpp",
        //     0xd7f)
        L2_LOG_WARN(
            Log,
            "Cannot set crop procurement for character DB ID %u: the requester no longer resolves to a player; the request "
            "is rejected.",
            static_cast<unsigned>(user->GetDBID())
        );
        return false;
    }

    const int residenceId = target->m_data->m_nResidenceID;
    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence == nullptr || !residence->HaveManor(manorId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d]invalid residence id(%d)",
        //     L"Manor.cpp",
        //     0xd85,
        //     residenceId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d]invalid residence id(%d)",
        //     L"Manor.cpp",
        //     0xd85,
        //     residenceId)
        L2_LOG_WARN(
            Log, "Cannot set crop procurement for manor %d: NPC residence %d %s; the request is rejected.", manorId,
            residenceId, residence == nullptr ? "was not found" : "does not list this manor"
        );
        return false;
    }

    for (int index = 0; index < cropCount; ++index) {
        int cropId = 0;
        std::int64_t count = 0;
        std::int64_t price = 0;
        std::uint8_t type = 0;
        cursor = ::Disassemble(cursor, "dQQc", &cropId, &count, &price, &type);
        residence->SetNextCropInfo(
            target->m_data->index, user->m_data->index, residenceId, manorId, cropId, count, price, type
        );
    }
    manor->SayNeedCropMoney(requester->CastUser());
    return false;
}

CManorDB::CManorDB() {
    // Current L2Server 0x0082967c.
    L2_GUARD_FUNCTION();
}

CManorData* CManorDB::RequireManorData(const int manorId) {
    // Current L2Server 0x0081fcac performs two singleton
    // lookups in this order.
    L2_GUARD_FUNCTION();
    CManorData::Inst(manorId).m_nManorId = manorId;
    return &CManorData::Inst(manorId);
}

void CManorDB::Add(CManor* const manor) {
    // Current L2Server 0x00823db8 publishes by manor id, or by zero when the
    // already-created owner has no shared data pointer.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Sect);
        const int manorId = manor->m_data != nullptr ? manor->m_data->m_nManorId : 0;
        m_ManorIDMap.emplace(manorId, manor);
    }
}

void CManorDB::LoadFromDB() {
    // Current L2Server 0x008224f8. Keep the target's uninitialized packet
    // storage and count only rows which Assemble actually publishes.
    L2_GUARD_FUNCTION();
    char packet[0x2000];
    int packetSize = 0;
    int manorCount = 0;

    {
        L2_GUARD(m_Sect);
        for (const auto& entry : m_ManorIDMap) {
            if (entry.second != nullptr) {
                const int written = ::Assemble(packet + packetSize, 0x2000 - packetSize, "d", entry.first);
                if (written > 0) {
                    packetSize += written;
                    ++manorCount;
                }
            }
        }
    }
    db.RequestLoadAllManor(manorCount, packetSize, packet);
}

CManor* CManorDB::GetManor(std::int32_t manorId) {
    // Current L2Server 0x00822ca0.
    L2_GUARD_FUNCTION();

    CManor* const manor = [&]() -> CManor* {
        L2_GUARD(m_Sect);
        const auto found = m_ManorIDMap.find(manorId);
        if (found == m_ManorIDMap.end()) {
            return nullptr;
        }
        CManor* const result = found->second;
        L2_ADD_REF(result, ObjectRefType::Anonymous);
        return result;
    }();
    if (manor == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] undefined (or unregistered) Manor[%d]",
        //     L"Manor.cpp",
        //     0x930,
        //     manorId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] undefined (or unregistered) Manor[%d]",
        //     L"Manor.cpp",
        //     0x930,
        //     manorId)
        L2_LOG_ERROR(Log, "Manor %d is not registered; lookup returns no object.", manorId);
        runtime::process::ReportCallStack(LOG_ERROR, false);
        return nullptr;
    }
    return manor;
}

void CManorDB::MakeDefaultCropInfo() {
    // Current L2Server 0x008262b0.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_Sect);
        for (const auto& manorEntry : m_ManorIDMap) {
            CManor* const manor = manorEntry.second;
            sDefaultCropInfo info;
            for (CropDataStruct* const crop : manor->m_CropList) {
                info.nCropId = 0;
                info.nLevel = 0;
                info.vType[0].clear();
                info.vType[1].clear();
                info.nSeedPrice = 0;
                info.nCropPrice = 0;

                const auto existing = m_mapCrop2DefaultCropInfo.find(crop->nCropId);
                if (existing != m_mapCrop2DefaultCropInfo.end()) {
                    m_mapSeed2DefaultCropInfo.emplace(crop->nSeedId, &existing->second);
                    continue;
                }

                info.nCropId = crop->nCropId;
                const int cropIndex = manor->GetManorCropIndex(crop->nCropId);
                info.nLevel = crop->nSeedLev;
                info.nCropPrice = manor->m_data->m_nCropPriceDefault[cropIndex];
                const int seedIndex = manor->GetManorSeedIndex(crop->nSeedId);
                info.nSeedPrice = manor->m_data->m_nSeedPriceDefault[seedIndex];

                const auto procure = manor->m_ProcureMap.find(crop->nCropId);
                if (procure == manor->m_ProcureMap.end()) {
                    // Preserve the current malformed argument order literally.
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] crop(%d) not exists in pManor->m_ProcureMap",
                    //     crop->nCropId,
                    //     L"Manor.cpp",
                    //     0x9f2)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] crop(%d) not exists in pManor->m_ProcureMap",
                    //     crop->nCropId,
                    //     L"Manor.cpp",
                    //     0x9f2)
                    L2_LOG_ERROR(
                        Log,
                        "Manor %d: crop %d has no procurement reward definition; its default crop record is still "
                        "registered with empty reward lists.",
                        manorEntry.first, crop->nCropId
                    );
                } else {
                    ProcureStruct* const procureInfo = procure->second;
                    if (procureInfo->m_pProcureList != nullptr) {
                        for (ProcureStructEach* const each : *procureInfo->m_pProcureList) {
                            if (each->nTypeId == 1 || each->nTypeId == 2) {
                                info.vType[each->nTypeId - 1] = *each->pProcureItemList;
                            } else {
                                // Preserve the current malformed argument order.
                                // Log.Add(
                                //     LOG_ERROR,
                                //     L"[%s][%d] type id %d is not supported",
                                //     each->nTypeId,
                                //     L"Manor.cpp",
                                //     0xa03)
                                // L2_LOG_ERROR(Log,
                                //     L"[%s][%d] type id %d is not supported",
                                //     each->nTypeId,
                                //     L"Manor.cpp",
                                //     0xa03)
                                L2_LOG_ERROR(
                                    Log,
                                    "Manor %d: crop %d has unsupported reward type %d (expected 1 or 2); this reward list "
                                    "is ignored.",
                                    manorEntry.first, crop->nCropId, each->nTypeId
                                );
                            }
                        }
                    }
                }

                const auto inserted = m_mapCrop2DefaultCropInfo.emplace(crop->nCropId, info);
                if (!inserted.second) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] why not ?",
                    //     L"Manor.cpp",
                    //     0xa10)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] why not ?",
                    //     L"Manor.cpp",
                    //     0xa10)
                    L2_LOG_ERROR(
                        Log,
                        "Manor %d: default crop %d insertion found an existing entry; seed %d is not linked to that entry "
                        "by this operation.",
                        manorEntry.first, crop->nCropId, crop->nSeedId
                    );
                } else {
                    m_mapSeed2DefaultCropInfo.emplace(crop->nSeedId, &inserted.first->second);
                }
            }
        }
    }
}

void CManorDB::Load() {
    // Current L2Server 0x00829028.
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    const wchar_t* const fileName = L"..\\Script\\manordata.txt";
#else
    const wchar_t* const fileName = L"../script/manordata.txt";
#endif

    CSecureStream input(fileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"Manor.cpp",
        //     0x9b1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"Manor.cpp",
        //     0x9b1)
        L2_LOG_ERROR(Log, "Cannot read manordata.txt; manor script loading is skipped.");
        return;
    }

    CParserForManor parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForManor::create failed",
        //     L"Manor.cpp",
        //     0x9ba)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForManor::create failed",
        //     L"Manor.cpp",
        //     0x9ba)
        L2_LOG_ERROR(Log, "Cannot initialize the manordata.txt parser; manor script loading is skipped.");
        return;
    }

    parser.m_lexer.yybegin(0);
    if (parser.yyparse() == 0) {
        MakeDefaultCropInfo();
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing Manor file failed after line[%d]",
        //     L"Manor.cpp",
        //     0x9c6,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing Manor file failed after line[%d]",
        //     L"Manor.cpp",
        //     0x9c6,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "manordata.txt parsing failed near line %lld; processed declarations remain in memory and default crop indexes "
            "are not built.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

void CManorMenuParser::Parse(User* const user, const char16_t* const command) {
    // Current L2Server 0x008297fc.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [User *pUser] is nullptr",
        //     L"Manor.cpp",
        //     0xa25)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] [User *pUser] is nullptr",
        //     L"Manor.cpp",
        //     0xa25)
        L2_LOG_WARN(Log, "Cannot handle the manor menu command: no character is bound; the command is ignored.");
        return;
    }

    CNPC* const npc = user->m_spTalkTarget.GetNPC();
    if (npc == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] [CNPC *pNPC = pUser->m_spTalkTarget.GetNPC()] is nullptr",
        //     L"Manor.cpp",
        //     0xa2d)
        // Noise: A manor menu command can outlive its NPC conversation target. The existing null-target return is
        // sufficient. L2_LOG_ERROR(Log,
        //     L"[%s][%d] [CNPC *pNPC = pUser->m_spTalkTarget.GetNPC()] is nullptr",
        //     L"Manor.cpp",
        //     0xa2d)
        return;
    }
    if (!user->IsTalkableDistance(npc)) {
        // user->ActionFailed(0)
        user->ActionFailed(AF_DEFAULT);
        return;
    }
    if (npc->m_data == nullptr) {
        return;
    }

    const CHTMLActionParamParser parameters = CHTMLActionParamParser::Parse(command);
    const int ask = parameters.GetIntValue(u"ask");
    const int state = parameters.GetIntValue(u"state");
    const int time = parameters.GetIntValue(u"time");
    if (ask != 9 && state != -1 && g_ManorDB.GetManor(state) == nullptr) {
        return;
    }

    const int npcSharedIndex = npc->m_data->index;
    const int userSharedIndex = user->m_data->index;
    CSPointer<CCreature> npcPrototype = CCreatureFactory::Inst().FindObjectSP(npcSharedIndex);
    CSPointer<CCreature> userPrototype = CCreatureFactory::Inst().FindObjectSP(userSharedIndex);
    if (!npcPrototype || !npcPrototype->IsNPC() || !userPrototype || !userPrototype->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid arguments",
        //     L"Manor.cpp",
        //     0xa51)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid arguments",
        //     L"Manor.cpp",
        //     0xa51)
        L2_LOG_WARN(
            Log,
            "Cannot forward the manor menu command: NPC shared index %d or character shared index %d no longer resolves to "
            "the required type.",
            npcSharedIndex, userSharedIndex
        );
        return;
    }

    g_npcserver.Send("cddddd", 0x4f, userSharedIndex, npcSharedIndex, ask, state, time);
}

void CManorDB::CalcNextAllManor() {
    L2_GUARD_FUNCTION();
    g_nManorLock = 1;
    for (const auto& entry : m_ManorIDMap) {
        if (entry.second != nullptr) {
            entry.second->CalcNextManor();
        }
    }
    g_nManorLock = 0;
}

void CManorDB::TransferAllManorData() {
    L2_GUARD_FUNCTION();
    g_nManorLock = 1;
    for (const auto& entry : m_ManorIDMap) {
        if (entry.second != nullptr) {
            entry.second->TransferManorData();
        }
    }
    g_nManorLock = 0;
}

CManorFuncHandler::CManorFuncHandler(
    const std::uint8_t function, const int npcId, const int userId, const int manorId, const int time, const int cropId
) :
    m_byFunc(function),
    m_nNpcId(npcId),
    m_nUserId(userId),
    m_nManorId(manorId),
    m_nTime(time != 0 ? 1U : 0U),
    m_nCropId(cropId) {
    // Current L2Server 0x00822258.
}

bool CManorFuncHandler::PreCheck() {
    // Current L2Server 0x00822694.
    L2_GUARD_FUNCTION();
    if (m_byFunc >= 7U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid func id(%d)",
        //     L"Manor.cpp",
        //     0xa6e,
        //     static_cast<int>(m_byFunc))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid func id(%d)",
        //     L"Manor.cpp",
        //     0xa6e,
        //     static_cast<int>(m_byFunc))
        L2_LOG_WARN(
            Log, "Cannot prepare manor function %u: supported function IDs are 0..6; the operation is rejected.",
            static_cast<unsigned>(m_byFunc)
        );
        return false;
    }

    m_pUser = CCreatureFactory::Inst().FindObjectSP(m_nUserId);
    if (!m_pUser || !m_pUser->IsPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid user id(%d)",
        //     L"Manor.cpp",
        //     0xa75,
        //     m_nUserId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid user id(%d)",
        //     L"Manor.cpp",
        //     0xa75,
        //     m_nUserId)
        L2_LOG_WARN(
            Log,
            "Cannot prepare manor function %u: character shared index %d does not resolve to a player; the operation is "
            "rejected.",
            static_cast<unsigned>(m_byFunc), m_nUserId
        );
        return false;
    }

    m_pNpc = CCreatureFactory::Inst().FindObjectSP(m_nNpcId);
    if (!m_pNpc || !m_pNpc->IsNPC()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid npc id(%d)",
        //     L"Manor.cpp",
        //     0xa7c,
        //     m_nNpcId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid npc id(%d)",
        //     L"Manor.cpp",
        //     0xa7c,
        //     m_nNpcId)
        L2_LOG_WARN(
            Log,
            "Cannot prepare manor function %u: NPC shared index %d does not resolve to an NPC; the operation is rejected.",
            static_cast<unsigned>(m_byFunc), m_nNpcId
        );
        return false;
    }
    return m_pUser->IsTalkableDistance(m_pNpc.get());
}

bool CManorFuncHandler::IsChamberlain() {
    // Current L2Server 0x00820898.
    L2_GUARD_FUNCTION();
    if (!m_pNpc || !m_pNpc->IsNPC()) {
        return false;
    }
    CNPC* const npc = m_pNpc->CastNPC();
    return static_cast<std::int32_t>(npc->m_data->m_nClass) == 0x49;
}

int CManorFuncHandler::MakeProcureTypeBuffer(char* const buffer, const int bufferSize, const sDefaultCropInfo& info) {
    // Current L2Server 0x008209a0.
    L2_GUARD_FUNCTION();
    int length = 0;
    for (const auto& types : info.vType) {
        length += ::Assemble(buffer + length, bufferSize - length, "c", static_cast<int>(types.size()));
        for (const int type : types) {
            length += ::Assemble(buffer + length, bufferSize - length, "d", type);
        }
    }
    return length;
}

bool CManorFuncHandler::InitManor(CManor*& manor, CManorData*& data, [[maybe_unused]] const wchar_t* const file, const int) {
    // Current L2Server 0x00822e3c.
    L2_GUARD_FUNCTION();
    manor = g_ManorDB.GetManor(m_nManorId);
    if (manor == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid manor(%d)",
        //     file,
        //     line,
        //     m_nManorId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid manor(%d)",
        //     file,
        //     line,
        //     m_nManorId)
        L2_LOG_WARN(
            Log, "Cannot show manor %d for function %u: manor lookup failed; no response is sent.", m_nManorId,
            static_cast<unsigned>(m_byFunc)
        );
        return false;
    }
    data = manor->m_data;
    if (data == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid manor data(%d)",
        //     file,
        //     line,
        //     m_nManorId)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid manor data(%d)",
        //     file,
        //     line,
        //     m_nManorId)
        L2_LOG_ERROR(
            Log, "Cannot show manor %d for function %u: shared manor data is missing; no response is sent.", m_nManorId,
            static_cast<unsigned>(m_byFunc)
        );
        return false;
    }
    return true;
}

void CManorFuncHandler::ShowManorDefaultInfo() {
    // Current L2Server 0x0082292c.
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> buffer;
    int length = 0;
    for (const auto& entry : g_ManorDB.m_mapCrop2DefaultCropInfo) {
        const sDefaultCropInfo& info = entry.second;
        length += ::Assemble(
            buffer.data() + length, 0x5000 - length, "dddd", info.nCropId, info.nLevel, info.nSeedPrice, info.nCropPrice
        );
        length += MakeProcureTypeBuffer(buffer.data() + length, 0x5000 - length, info);
    }

    User* const user = m_pUser->CastUser();
    user->GetSocket()->Send(
        "chcdb", 0xfe, 0x25, IsChamberlain(), static_cast<int>(g_ManorDB.m_mapCrop2DefaultCropInfo.size()), length,
        buffer.data()
    );
}

void CManorFuncHandler::ShowSeedInfo() {
    // Current L2Server 0x00823098.
    L2_GUARD_FUNCTION();
    CManor* manor = nullptr;
    CManorData* data = nullptr;
    if (!InitManor(manor, data, L"Manor.cpp", 0xab3)) {
        return;
    }

    const auto& seedList = m_nTime != 0 ? data->m_nSeedList_n : data->m_nSeedList;
    const auto& prices = m_nTime != 0 ? data->m_nSeedPrice_n : data->m_nSeedPrice;
    const auto& setCounts = m_nTime != 0 ? data->m_nSeedSetCount_n : data->m_nSeedSetCount;
    const auto& remainCounts = m_nTime != 0 ? data->m_nSeedSetCount_n : data->m_nSeedRemainCount;

    std::array<char, 0x5000> buffer;
    int length = 0;
    int count = 0;
    {
        L2_READ_GUARD(*manor);
        for (int index = 0; index < 40; ++index) {
            const int seedId = seedList[static_cast<std::size_t>(index)];
            if (seedId <= 0) {
                continue;
            }
            const auto found = g_ManorDB.m_mapSeed2DefaultCropInfo.find(seedId);
            if (found == g_ManorDB.m_mapSeed2DefaultCropInfo.end()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] seed(%d) not found in m_mapSeed2DefaultCropInfo",
                //     L"Manor.cpp",
                //     0xacb,
                //     seedId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] seed(%d) not found in m_mapSeed2DefaultCropInfo",
                //     L"Manor.cpp",
                //     0xacb,
                //     seedId)
                L2_LOG_ERROR(
                    Log,
                    "Manor %d: seed %d has no default crop information; this entry is omitted from the seed-sale "
                    "information.",
                    m_nManorId, seedId
                );
                continue;
            }
            const sDefaultCropInfo& info = *found->second;
            length += ::Assemble(
                buffer.data() + length, 0x5000 - length, "dQQQd", seedId, prices[static_cast<std::size_t>(index)],
                setCounts[static_cast<std::size_t>(index)], remainCounts[static_cast<std::size_t>(index)], info.nLevel
            );
            length += MakeProcureTypeBuffer(buffer.data() + length, 0x5000 - length, info);
            ++count;
        }
    }

    User* const user = m_pUser->CastUser();
    user->GetSocket()->Send("chcdddb", 0xfe, 0x23, IsChamberlain(), m_nManorId, m_nTime, count, length, buffer.data());
}

void CManorFuncHandler::ShowCropInfo() {
    // Current L2Server 0x008233e4.
    L2_GUARD_FUNCTION();
    CManor* manor = nullptr;
    CManorData* data = nullptr;
    if (!InitManor(manor, data, L"Manor.cpp", 0xae5)) {
        return;
    }

    const auto& cropList = m_nTime != 0 ? data->m_nProcuringCropList_n : data->m_nProcuringCropList;
    const auto& setCounts = m_nTime != 0 ? data->m_nProcuringCropCount_n : data->m_nProcuringCropSetCount;
    const auto& counts = m_nTime != 0 ? data->m_nProcuringCropCount_n : data->m_nProcuringCropCount;
    const auto& prices = m_nTime != 0 ? data->m_nProcuringCropPrice_n : data->m_nProcuringCropPrice;
    const auto& types = m_nTime != 0 ? data->m_nProcuringCropType_n : data->m_nProcuringCropType;

    std::array<char, 0x5000> buffer;
    int length = 0;
    int count = 0;
    {
        L2_READ_GUARD(*manor);
        for (int index = 0; index < 40; ++index) {
            const int cropId = cropList[static_cast<std::size_t>(index)];
            if (cropId <= 0) {
                continue;
            }
            const auto found = g_ManorDB.m_mapCrop2DefaultCropInfo.find(cropId);
            if (found == g_ManorDB.m_mapCrop2DefaultCropInfo.end()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] crop(%d) not found in m_mapCrop2DefaultCropInfo",
                //     L"Manor.cpp",
                //     0xb00,
                //     cropId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] crop(%d) not found in m_mapCrop2DefaultCropInfo",
                //     L"Manor.cpp",
                //     0xb00,
                //     cropId)
                L2_LOG_ERROR(
                    Log,
                    "Manor %d: crop %d has no default crop information; this entry is omitted from the procurement "
                    "information.",
                    m_nManorId, cropId
                );
                continue;
            }
            const sDefaultCropInfo& info = found->second;
            length += ::Assemble(
                buffer.data() + length, 0x5000 - length, "dQQQcd", cropId, setCounts[static_cast<std::size_t>(index)],
                counts[static_cast<std::size_t>(index)], prices[static_cast<std::size_t>(index)],
                static_cast<int>(static_cast<std::uint8_t>(types[static_cast<std::size_t>(index)])), info.nLevel
            );
            length += MakeProcureTypeBuffer(buffer.data() + length, 0x5000 - length, info);
            ++count;
        }
    }

    User* const user = m_pUser->CastUser();
    user->GetSocket()->Send("chcdddb", 0xfe, 0x24, IsChamberlain(), m_nManorId, m_nTime, count, length, buffer.data());
}

void CManorFuncHandler::ShowSeedSetting() {
    // Current L2Server 0x00826764.
    L2_GUARD_FUNCTION();
    CManor* manor = nullptr;
    CManorData* data = nullptr;
    if (!InitManor(manor, data, L"Manor.cpp", 0xb30)) {
        return;
    }
    User* const user = m_pUser->CastUser();
    if (!user->IsMyManor(m_nManorId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] hack? you(%s)'re not manor(%d) owner.",
        //     L"Manor.cpp",
        //     0xb33,
        //     user->GetName(),
        //     m_nManorId)
        // Noise: Viewing manor settings requires ownership. An ordinary permission refusal is not evidence of an attack.
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] hack? you(%s)'re not manor(%d) owner.",
        //     L"Manor.cpp",
        //     0xb33,
        //     user->GetName(),
        //     m_nManorId)
        return;
    }

    std::array<char, 0x5000> buffer;
    int length = 0;
    int count = 0;
    {
        L2_READ_GUARD(*manor);
        for (int index = 0; index < 40; ++index) {
            const std::size_t slot = static_cast<std::size_t>(index);
            const int seedId = data->m_nSeedIdList[slot];
            if (seedId <= 0) {
                continue;
            }
            const int maximumCount = manor->GetMaxSeedCount(index);
            const int defaultPrice = data->m_nSeedPriceDefault[slot];
            const int minimumPrice = static_cast<int>(static_cast<double>(defaultPrice) * 0.6);
            const int maximumPrice = runtime::MultiplyInt32Wrapping(defaultPrice, 10);
            const auto found = g_ManorDB.m_mapSeed2DefaultCropInfo.find(seedId);
            if (found == g_ManorDB.m_mapSeed2DefaultCropInfo.end()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] seed(%d) not found in m_mapSeed2DefaultCropInfo",
                //     L"Manor.cpp",
                //     0xb4b,
                //     seedId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] seed(%d) not found in m_mapSeed2DefaultCropInfo",
                //     L"Manor.cpp",
                //     0xb4b,
                //     seedId)
                L2_LOG_ERROR(
                    Log,
                    "Manor %d: seed %d has no default crop information; this entry is omitted from the seed-sale settings.",
                    m_nManorId, seedId
                );
                continue;
            }
            const sDefaultCropInfo& info = *found->second;
            length += ::Assemble(buffer.data() + length, 0x5000 - length, "dd", seedId, info.nLevel);
            length += MakeProcureTypeBuffer(buffer.data() + length, 0x5000 - length, info);
            length += ::Assemble(
                buffer.data() + length, 0x5000 - length, "ddddQQQQ", maximumCount, defaultPrice, minimumPrice, maximumPrice,
                data->m_nSeedSetCount[slot], data->m_nSeedPrice[slot], data->m_nSeedSetCount_n[slot],
                data->m_nSeedPrice_n[slot]
            );
            ++count;
        }
    }

    TalkChannelCache::inst().mChannels[3].Cache(m_pUser->Id(), m_pNpc->Id(), 0);
    m_pUser->CastUser()->GetSocket()->Send("chddb", 0xfe, 0x26, m_nManorId, count, length, buffer.data());
}

void CManorFuncHandler::ShowCropSetting() {
    // Current L2Server 0x00826c58.
    L2_GUARD_FUNCTION();
    CManor* manor = nullptr;
    CManorData* data = nullptr;
    if (!InitManor(manor, data, L"Manor.cpp", 0xb6a)) {
        return;
    }
    User* const user = m_pUser->CastUser();
    if (!user->IsMyManor(m_nManorId)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] hack? you(%s)'re not manor(%d) owner.",
        //     L"Manor.cpp",
        //     0xb6d,
        //     user->GetName(),
        //     m_nManorId)
        // Noise: Viewing manor settings requires ownership. An ordinary permission refusal is not evidence of an attack.
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] hack? you(%s)'re not manor(%d) owner.",
        //     L"Manor.cpp",
        //     0xb6d,
        //     user->GetName(),
        //     m_nManorId)
        return;
    }

    std::array<char, 0x5000> buffer;
    int length = 0;
    int count = 0;
    {
        L2_READ_GUARD(*manor);
        for (int index = 0; index < 40; ++index) {
            const std::size_t slot = static_cast<std::size_t>(index);
            const int cropId = data->m_nCropIdList[slot];
            if (cropId <= 0) {
                continue;
            }
            const int maximumCount = manor->GetMaxCropCount(index);
            const int defaultPrice = data->m_nCropPriceDefault[slot];
            const int minimumPrice = static_cast<int>(static_cast<double>(defaultPrice) * 0.6);
            const int maximumPrice = runtime::MultiplyInt32Wrapping(defaultPrice, 10);
            const auto found = g_ManorDB.m_mapCrop2DefaultCropInfo.find(cropId);
            if (found == g_ManorDB.m_mapCrop2DefaultCropInfo.end()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] crop(%d) not found in m_mapCrop2DefaultCropInfo",
                //     L"Manor.cpp",
                //     0xb87,
                //     cropId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] crop(%d) not found in m_mapCrop2DefaultCropInfo",
                //     L"Manor.cpp",
                //     0xb87,
                //     cropId)
                L2_LOG_ERROR(
                    Log,
                    "Manor %d: crop %d has no default crop information; this entry is omitted from the procurement "
                    "settings.",
                    m_nManorId, cropId
                );
                continue;
            }
            const sDefaultCropInfo& info = found->second;
            length += ::Assemble(buffer.data() + length, 0x5000 - length, "dd", cropId, info.nLevel);
            length += MakeProcureTypeBuffer(buffer.data() + length, 0x5000 - length, info);
            length += ::Assemble(
                buffer.data() + length, 0x5000 - length, "ddddQQcQQc", maximumCount, defaultPrice, minimumPrice,
                maximumPrice, data->m_nProcuringCropSetCount[slot], data->m_nProcuringCropPrice[slot],
                static_cast<int>(static_cast<std::uint8_t>(data->m_nProcuringCropType[slot])),
                data->m_nProcuringCropCount_n[slot], data->m_nProcuringCropPrice_n[slot],
                static_cast<int>(static_cast<std::uint8_t>(data->m_nProcuringCropType_n[slot]))
            );
            ++count;
        }
    }

    TalkChannelCache::inst().mChannels[3].Cache(m_pUser->Id(), m_pNpc->Id(), 0);
    m_pUser->CastUser()->GetSocket()->Send("chddb", 0xfe, 0x2b, m_nManorId, count, length, buffer.data());
}

void CManorFuncHandler::ShowProcureCropList() {
    // Current L2Server 0x00827170.
    L2_GUARD_FUNCTION();
    CManor* manor = nullptr;
    CManorData* data = nullptr;
    if (!InitManor(manor, data, L"Manor.cpp", 0xba8)) {
        return;
    }

    User* const user = m_pUser->CastUser();
    std::array<char, 0x5000> buffer;
    int length = 0;
    int count = 0;
    {
        L2_READ_GUARD(*manor);
        L2_READ_GUARD(user->m_inv);
        for (CSharedItemData* itemData = user->m_inv.GetFirst(); itemData != nullptr;
            itemData = CItemFactory::Inst().GetNext(itemData)) {
            if (itemData->m_Type != IT_ETCITEM) {
                continue;
            }
            CSPointer<CItem> item = CItemFactory::Inst().FindObjectSP(itemData->index);
            if (!item || item->m_const->m_EtcitemType != 0x11) {
                continue;
            }

            const int cropId = itemData->m_nClassID;
            const auto found = g_ManorDB.m_mapCrop2DefaultCropInfo.find(cropId);
            if (found == g_ManorDB.m_mapCrop2DefaultCropInfo.end()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] crop(%d) not found in m_mapCrop2DefaultCropInfo",
                //     L"Manor.cpp",
                //     0xbc1,
                //     cropId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] crop(%d) not found in m_mapCrop2DefaultCropInfo",
                //     L"Manor.cpp",
                //     0xbc1,
                //     cropId)
                L2_LOG_ERROR(
                    Log,
                    "Manor %d: crop %d has no default crop information; this entry is omitted from the tradeable crop list.",
                    m_nManorId, cropId
                );
                continue;
            }
            const sDefaultCropInfo& info = found->second;
            length += ::Assemble(buffer.data() + length, 0x5000 - length, "ddd", itemData->m_nId, cropId, info.nLevel);
            length += MakeProcureTypeBuffer(buffer.data() + length, 0x5000 - length, info);

            const int index = manor->GetManorCropIndex(cropId);
            int procureManorId = -1;
            std::int64_t procureCount = 0;
            std::int64_t procurePrice = 0;
            std::uint8_t procureType = 0;
            if (index != -1 && data->m_nProcuringCropCount[static_cast<std::size_t>(index)] > 0) {
                const std::size_t slot = static_cast<std::size_t>(index);
                procureManorId = m_nManorId;
                procureCount = data->m_nProcuringCropCount[slot];
                procurePrice = data->m_nProcuringCropPrice[slot];
                procureType = static_cast<std::uint8_t>(data->m_nProcuringCropType[slot]);
            }
            length += ::Assemble(
                buffer.data() + length, 0x5000 - length, "dQQcQ", procureManorId, procureCount, procurePrice,
                static_cast<int>(procureType), itemData->m_nCount
            );
            ++count;
        }
    }

    CNPC* const npc = static_cast<CNPC*>(m_pNpc.get());
    TalkChannelCache::inst().mChannels[3].Cache(user->Id(), npc->Id(), 0);
    user->GetSocket()->Send("chddb", 0xfe, 0x2c, m_nManorId, count, length, buffer.data());
}

void CManorFuncHandler::ShowProcureCropDetail() {
    // Current L2Server 0x008276c0.
    L2_GUARD_FUNCTION();
    User* const user = m_pUser->CastUser();
    CCreature* const npc = m_pNpc.get();
    if (!TalkChannelCache::inst().mChannels[3].IsValidChannel(user->Id(), npc->Id(), 0xfa, 0)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] user[%s] request ShowProcureCropDetail with invalid talk channel.",
        //     L"Manor.cpp",
        //     0xbf2,
        //     user->GetName())
        // Noise: An expired or missing cached NPC dialogue is an ordinary stale-menu refusal. The existing validation still
        // rejects the request. L2_LOG_ERROR(Log,
        //     L"[%s][%d] user[%s] request ShowProcureCropDetail with invalid talk channel.",
        //     L"Manor.cpp",
        //     0xbf2,
        //     user->GetName())
        return;
    }

    std::array<char, 0x5000> buffer;
    int length = 0;
    int count = 0;
    for (const auto& entry : g_ManorDB.m_ManorIDMap) {
        CManor* const manor = entry.second;
        {
            L2_READ_GUARD(*manor);
            const int index = manor->GetManorCropIndex(m_nCropId);
            if (index != -1) {
                CManorData* const data = manor->m_data;
                const int residenceId = data != nullptr ? data->m_nResidenceID : 0;
                const std::size_t slot = static_cast<std::size_t>(index);
                const std::int64_t procureCount = data->m_nProcuringCropCount[slot];
                const std::int64_t procurePrice = data->m_nProcuringCropPrice[slot];
                if (procureCount > 0) {
                    length += ::Assemble(
                        buffer.data() + length, 0x5000 - length, "dQQc", residenceId, procureCount, procurePrice,
                        static_cast<int>(static_cast<std::uint8_t>(data->m_nProcuringCropType[slot]))
                    );
                    ++count;
                }
            }
        }
    }
    user->GetSocket()->Send("chddb", 0xfe, 0x79, m_nCropId, count, length, buffer.data());
}

void CManorFuncHandler::Execute() {
    // Current L2Server 0x00829214.
    L2_GUARD_FUNCTION();
    switch (m_byFunc) {
        case 0:
            ShowSeedInfo();
            break;
        case 1:
            ShowCropInfo();
            break;
        case 2:
            ShowManorDefaultInfo();
            break;
        case 3:
            ShowSeedSetting();
            break;
        case 4:
            ShowCropSetting();
            break;
        case 5:
            ShowProcureCropList();
            break;
        case 6:
            ShowProcureCropDetail();
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid func id(%d)",
            //     L"Manor.cpp",
            //     0xaa8,
            //     static_cast<int>(m_byFunc))
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid func id(%d)",
            //     L"Manor.cpp",
            //     0xaa8,
            //     static_cast<int>(m_byFunc))
            L2_LOG_WARN(
                Log, "Cannot execute manor function %u: supported function IDs are 0..6; no response is sent.",
                static_cast<unsigned>(m_byFunc)
            );
            break;
    }
}
