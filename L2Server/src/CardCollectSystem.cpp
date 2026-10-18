#include "CardCollectSystem.h"
#include "CardCollectSystemParser.h"
#include "Object.h"
#include "ObjectDB.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"

#include "Shared/src/AtomicJob.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <string>

namespace {

bool ValidateProbability(const CCardCollectSystemData::RandomItemList& items) {
    double probability = 0.0;
    for (const CManipulateRandomItemData* const item : items) {
        probability += item->m_dProb;
    }
    if (probability > 100.0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Sum of Random Item Probability is over 100.0 "
        //     L"In CardCollectSystem Script File.")
        // L2_LOG_ERROR(Log,
        //     L"Sum of Random Item Probability is over 100.0 "
        //     L"In CardCollectSystem Script File.")
        L2_LOG_ERROR(
            Log,
            "Card collection random reward probabilities total %.17g%% (maximum 100%%); validation failed, but the copied "
            "reward list remains.",
            probability
        );
        return false;
    }
    return true;
}

} // namespace

CManipulateItemData::CManipulateItemData(char16_t* itemName, const std::int32_t amount) {
    // Current L2Server 0x0057ede4.
    L2_GUARD_FUNCTION();
    CObject* const object = g_ObjectDB.GetObjectByName(itemName);
    std::int32_t itemClassId = -1;
    if (object != nullptr && object->IsItem()) {
        itemClassId = object->GetClassId();
    } else {
        const std::wstring hostName = itemName != nullptr ? Utf16ToWide(itemName) : std::wstring();
        // Log.Add(
        //     LOG_ERROR,
        //     L"Undefined Item[%s] In CardCollectSystem Script File.",
        //     itemName != nullptr ? hostName.c_str() : nullptr)
        // L2_LOG_ERROR(Log,
        //     L"Undefined Item[%s] In CardCollectSystem Script File.",
        //     itemName != nullptr ? hostName.c_str() : nullptr)
        L2_LOG_ERROR(
            Log, "Card collection item '%s' is missing or not an item (amount %d); entry keeps invalid class ID -1.",
            itemName != nullptr ? WideToUtf8(std::wstring_view(hostName)).c_str() : "<null>", amount
        );
    }
    m_nItemClassID = itemClassId;
    m_nAmount = amount;
    m_dProb = 100.0;
}

CManipulateItemData::CManipulateItemData(char16_t* itemName, const std::int32_t amount, const double probability) {
    // Current L2Server 0x0057eef8.
    L2_GUARD_FUNCTION();
    CObject* const object = g_ObjectDB.GetObjectByName(itemName);
    std::int32_t itemClassId = -1;
    if (object != nullptr && object->IsItem()) {
        itemClassId = object->GetClassId();
    } else {
        const std::wstring hostName = itemName != nullptr ? Utf16ToWide(itemName) : std::wstring();
        // Log.Add(
        //     LOG_ERROR,
        //     L"Undefined Item[%s] In CardCollectSystem Script File.",
        //     itemName != nullptr ? hostName.c_str() : nullptr)
        // L2_LOG_ERROR(Log,
        //     L"Undefined Item[%s] In CardCollectSystem Script File.",
        //     itemName != nullptr ? hostName.c_str() : nullptr)
        L2_LOG_ERROR(
            Log,
            "Card collection item '%s' is missing or not an item (amount %d, probability %.17g%%); entry keeps invalid "
            "class ID -1.",
            itemName != nullptr ? WideToUtf8(std::wstring_view(hostName)).c_str() : "<null>", amount, probability
        );
    }
    m_nItemClassID = itemClassId;
    m_nAmount = amount;
    m_dProb = probability;
}

CManipulateRandomItemData::CManipulateRandomItemData(
    char16_t* itemName, const std::int32_t amount, const double probability
) {
    // Current L2Server 0x0057eccc.
    L2_GUARD_FUNCTION();
    CObject* const object = g_ObjectDB.GetObjectByName(itemName);
    std::int32_t itemClassId = -1;
    if (object != nullptr && object->IsItem()) {
        itemClassId = object->GetClassId();
    } else {
        const std::wstring hostName = itemName != nullptr ? Utf16ToWide(itemName) : std::wstring();
        // Log.Add(
        //     LOG_ERROR,
        //     L"Undefined Item[%s] In CardCollectSystem Script File.",
        //     itemName != nullptr ? hostName.c_str() : nullptr)
        // L2_LOG_ERROR(Log,
        //     L"Undefined Item[%s] In CardCollectSystem Script File.",
        //     itemName != nullptr ? hostName.c_str() : nullptr)
        L2_LOG_ERROR(
            Log,
            "Card collection random reward item '%s' is missing or not an item (amount %d, probability %.17g%%); entry "
            "keeps invalid class ID -1.",
            itemName != nullptr ? WideToUtf8(std::wstring_view(hostName)).c_str() : "<null>", amount, probability
        );
    }
    m_nItemClassID = itemClassId;
    m_nAmount = amount;
    m_dProb = probability;
}

CCardCollectSystemData::CCardCollectSystemData(const std::int32_t id) :
    m_nID(id) {}

bool CCardCollectSystemData::SetSourceCardList(ItemList* items) {
    // Current L2Server 0x0057f344.
    L2_GUARD_FUNCTION();
    m_lSourceCardList = *items;
    return true;
}

bool CCardCollectSystemData::SetFixedCardList(ItemList* items) {
    // Current L2Server 0x0057f3f8.
    L2_GUARD_FUNCTION();
    m_lFixedRewardList = *items;
    return true;
}

bool CCardCollectSystemData::SetRandomCardList(RandomItemList* items) {
    // Current L2Server 0x0057f4ac.
    L2_GUARD_FUNCTION();
    m_lRandomRewardList = *items;
    return ValidateProbability(*items);
}

bool CCardCollectSystemData::SetSecondRandomCardList(RandomItemList* items) {
    // Current L2Server 0x0057f5ac.
    L2_GUARD_FUNCTION();
    m_lRandomRewardSecondList = *items;
    return ValidateProbability(*items);
}

CardCollectEventInfo::CardCollectEventInfo() {}

CCardCollectSystem::CCardCollectSystem() {}

CCardCollectSystem::~CCardCollectSystem() = default;

void CCardCollectSystem::AddData(CCardCollectSystemData* const data) {
    L2_GUARD_FUNCTION();
    if (data->m_nID < 1) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CardCollectSystem ID is Invalid!")
        // L2_LOG_ERROR(Log,
        //     L"CardCollectSystem ID is Invalid!")
        L2_LOG_ERROR(Log, "Card collection entry ignored: ID %d must be positive.", data->m_nID);
        return;
    }
    if (m_mCardCollectDataMap.find(data->m_nID) != m_mCardCollectDataMap.end()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"CardCollectSystem ID[%d] is Duplicated!",
        //     data->m_nID)
        // L2_LOG_ERROR(Log,
        //     L"CardCollectSystem ID[%d] is Duplicated!",
        //     data->m_nID)
        L2_LOG_ERROR(Log, "Duplicate card collection ID %d ignored; the registered entry is retained.", data->m_nID);
        return;
    }
    m_mCardCollectDataMap.emplace(data->m_nID, data);
}

void CCardCollectSystem::AddEventInfo(CardCollectEventInfo* const eventInfo) {
    L2_GUARD_FUNCTION();
    if (eventInfo == nullptr) {
        return;
    }

    m_vCardCollectEvent.push_back(eventInfo);
    for (const std::int32_t id : eventInfo->vIdList) {
        const auto found = m_mCardCollectDataMap.find(id);
        if (found == m_mCardCollectDataMap.end()) {
            return;
        }
        eventInfo->dataInfo.emplace(id, found->second);
    }
}

CardCollectEventInfo* CCardCollectSystem::GetCurrentFirstEvent() {
    L2_GUARD_FUNCTION();
    const std::int32_t currentTime = runtime::crt::Time32Now();
    for (CardCollectEventInfo* const eventInfo : m_vCardCollectEvent) {
        if (eventInfo->nStartTime <= currentTime && currentTime <= eventInfo->nEndTime) {
            return eventInfo;
        }
    }
    return nullptr;
}

void CCardCollectSystem::MakePacket(CPacketHelperEx<20480>& packet, User& user) {
    // Current L2Server 0x0057fa24.
    L2_GUARD_FUNCTION();
    CardCollectEventInfo* const eventInfo = GetCurrentFirstEvent();
    if (eventInfo == nullptr) {
        return;
    }

    packet.Assemble("c", static_cast<int>(static_cast<std::int8_t>(eventInfo->nId)));
    packet.Assemble("d", static_cast<int>(eventInfo->vIdList.size()));

    for (const auto& entry : eventInfo->dataInfo) {
        CCardCollectSystemData* const data = entry.second;
        CPacketHelperEx<20480> sourceCards;
        std::uint8_t canAcquire = 1;
        for (const CManipulateItemData* const source : data->m_lSourceCardList) {
            const std::int64_t have = user.m_inv.GetItemAmount(source->m_nItemClassID, 0, true);
            canAcquire &= static_cast<std::uint8_t>(static_cast<std::int64_t>(source->m_nAmount) <= have);
            sourceCards.Assemble("ddQ", source->m_nItemClassID, source->m_nAmount, have);
        }

        packet.Assemble(
            "ddcb", data->m_nID, static_cast<int>(data->m_lSourceCardList.size()), static_cast<int>(canAcquire),
            sourceCards.GetLength(), sourceCards.GetPacket()
        );
    }
}

bool CCardCollectSystem::CheckAndReward(User& user, const int id) {
    // Current L2Server 0x0057fc74.
    L2_GUARD_FUNCTION();

    CardCollectEventInfo* const eventInfo = GetCurrentFirstEvent();
    if (eventInfo == nullptr) {
        return false;
    }
    if (eventInfo->nType == EVENT_NPC &&
        !TalkChannelCache::inst().mChannels[18].IsValidChannel(user.Id(), user.m_spTalkTarget.GetId(), 0xfa, 0)) {
        return false;
    }

    if (static_cast<double>(user.m_data->m_nInventoryWeight) > user.GetWeightLimit(69000.0) * 0.800000011920929 ||
        static_cast<float>(user.m_inv.GetTotalCount()) > static_cast<float>(user.GetInventoryLimit()) * 0.8F) {
        user.SendSystemMessage(SystemMessageId::INVENTORY_WEIGHT_AND_SLOT_USAGE_MUST_BE_BELOW_80_PERCENT);
        return false;
    }

    std::array<ItemManip, 100> requests{};
    int requestCount = 0;
    const auto dataPosition = eventInfo->dataInfo.find(id);
    if (dataPosition != eventInfo->dataInfo.end()) {
        CCardCollectSystemData* const data = dataPosition->second;
        for (const CManipulateItemData* const item : data->m_lSourceCardList) {
            requests[static_cast<std::size_t>(requestCount)].mClassId = item->m_nItemClassID;
            requests[static_cast<std::size_t>(requestCount)].mCount = -static_cast<std::int64_t>(item->m_nAmount);
            ++requestCount;
        }

        const double fixedRoll = genrand64_real1();
        for (const CManipulateItemData* const item : data->m_lFixedRewardList) {
            if (fixedRoll * 100.0 <= item->m_dProb) {
                requests[static_cast<std::size_t>(requestCount)].mClassId = item->m_nItemClassID;
                requests[static_cast<std::size_t>(requestCount)].mCount = item->m_nAmount;
                ++requestCount;
                break;
            }
        }

        const double randomRoll = genrand64_real1();
        double accumulatedProbability = 0.0;
        for (const CManipulateRandomItemData* const item : data->m_lRandomRewardList) {
            accumulatedProbability += item->m_dProb;
            if (randomRoll * 100.0 <= accumulatedProbability) {
                requests[static_cast<std::size_t>(requestCount)].mClassId = item->m_nItemClassID;
                requests[static_cast<std::size_t>(requestCount)].mCount = item->m_nAmount;
                ++requestCount;
                break;
            }
        }

        const double secondRandomRoll = genrand64_real1();
        accumulatedProbability = 0.0;
        for (const CManipulateRandomItemData* const item : data->m_lRandomRewardSecondList) {
            accumulatedProbability += item->m_dProb;
            if (secondRandomRoll * 100.0 <= accumulatedProbability) {
                requests[static_cast<std::size_t>(requestCount)].mClassId = item->m_nItemClassID;
                requests[static_cast<std::size_t>(requestCount)].mCount = item->m_nAmount;
                ++requestCount;
                break;
            }
        }
    }

    CPacketHelperEx<20480> packet;
    L2_GUARD(user.m_ManipulateQueueLock);
    User::etcMsg message = user.SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    AtomicShowCardCollectSystem* const job = new AtomicShowCardCollectSystem(static_cast<int>(user.Id()));
    User::ManipulateQueue queue = user.SaveManipulateQueue(
        ManipulateType::MANI_CARD_COLLECT, 0, requestCount, requests.data(), job, 0, packet, false, ItemGetReason::Etc,
        message
    );

    const User::ManiItemResultType result = user._SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        if (!user._HandleItemManipulation(queue)) {
            job->Destroy();
        }
    } else if (result == User::MRT_JOBMAP) {
        job->Destroy();
        user.SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
    } else if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"CardCollectSystem.cpp",
        //     0x1cc)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"CardCollectSystem.cpp",
        //     0x1cc)
        // Noise: Normal reward queue insertion waits behind existing work. It is not an error or proof of delivery.
    } else {
        job->Destroy();
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"CardCollectSystem.cpp",
        //     0x1d5)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
        //     L"CardCollectSystem.cpp",
        //     0x1d5)
        L2_LOG_ERROR(
            Log,
            "Card collection reward request %d for character %u failed with queue result %d (%d item operations); "
            "completion job destroyed.",
            id, static_cast<unsigned>(user.GetDBID()), static_cast<int>(result), requestCount
        );
    }
    return false;
}

bool CCardCollectSystem::LoadCardCollectSystemData() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kDataPath = L"..\\Script\\card_collect_system.txt";
#else
    constexpr const wchar_t* kDataPath = L"../script/card_collect_system.txt";
#endif

    CSecureStream input(kDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"CardCollectSystem.cpp",
        //     0xab)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from Objects file failed",
        //     L"CardCollectSystem.cpp",
        //     0xab)
        L2_LOG_ERROR(Log, "Cannot read card_collect_system.txt; card collection loading stops.");
        return false;
    }

    CParserForCardCollectSystem parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForCategory::create failed",
        //     L"CardCollectSystem.cpp",
        //     0xb4)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForCategory::create failed",
        //     L"CardCollectSystem.cpp",
        //     0xb4)
        L2_LOG_ERROR(Log, "Cannot initialize card_collect_system.txt parser; card collection loading stops.");
        return false;
    }

    const int result = parser.yyparse();
    if (result != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing CategoryData file failed after line[%d]",
        //     L"CardCollectSystem.cpp",
        //     0xbf,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing CategoryData file failed after line[%d]",
        //     L"CardCollectSystem.cpp",
        //     0xbf,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse card_collect_system.txt at script line %lld; data registered before the error is retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
    return result == 0;
}
