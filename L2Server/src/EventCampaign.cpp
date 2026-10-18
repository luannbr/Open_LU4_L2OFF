#include "EventCampaign.h"
#include "Area.h"
#include "EventCampaignParser.h"
#include "HTMLCache.h"
#include "Item.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "PVPMatch.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Log.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Utils.h"

#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string>
#include <string_view>

// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     CEventCampaign::kMemoryClassId ==
//     L2ServerMemory::kCEventCampaignClassId)
// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     CEventCampaignManager::kMemoryClassId ==
//     L2ServerMemory::kCEventCampaignManagerClassId)

namespace {


void ShowEventCampaignHTML(User& user, const std::u16string& fileName) {
    const std::u16string hostFileName(runtime::StringView(fileName));
    std::u16string html;
    if (g_HTMLCache.GetHTMLFile(fileName.c_str(), html, user.m_data->m_CountryCode)) {
        const std::u16string hostHtml(runtime::StringView(html));
        user.ShowHTML(hostFileName.c_str(), hostHtml.c_str(), 0, HTML::LAUNCH, true);
    }
}

} // namespace

EventCampaignSerializer::EventCampaignSerializer() {}

void EventCampaignSerializer::AddExecutable(
    CEventCampaign* owner, const Executable executable, CExecutionArgument* argument, const wchar_t* executableName
) {
    m_Serializer.AddExecutable(owner, executable, argument, executableName);
}

CEventCampaignStepMultiInfo::CEventCampaignStepMultiInfo(const int id) :
    m_ID(id),
    m_StartPoint{},
    m_GoalCount(0),
    m_SuccessInfo(-1) {
    L2_GUARD_FUNCTION();
    m_ContextHTMLName = std::u16string();
    m_RewardHTMLName = std::u16string();
    m_EliteRewardHTMLName = std::u16string();
    m_FailedHTMLName = std::u16string();
    m_RewardReceivedHTMLName = std::u16string();
    m_GoalMap.clear();
    m_RewardList.clear();
    m_EliteRewardList.clear();
    m_AcceptList.clear();
}

void CEventCampaignStepMultiInfo::SetStartPosition(const int x, const int y, const int z) {
    L2_GUARD_FUNCTION();
    m_StartPoint.x = x;
    m_StartPoint.y = y;
    m_StartPoint.z = z;
}

void CEventCampaignStepMultiInfo::AllDataInit() {
    L2_GUARD_FUNCTION();
    m_SuccessInfo = -1;
    for (auto& entry : m_GoalMap) {
        entry.second.m_CurrentPoint = 0;
    }
}

void CEventCampaignStepMultiInfo::GiveStartItemToUser(User& user) {
    // Current L2Server 0x00588ab8.
    L2_GUARD_FUNCTION();
    const int requestCount = static_cast<int>(m_AcceptList.size());
    if (requestCount <= 0) {
        return;
    }

    ItemManip requests[100]{};
    for (int index = 0; index < requestCount; ++index) {
        const RewardItemDataType& source = m_AcceptList[static_cast<std::size_t>(index)];
        ItemManip& request = requests[index];
        request.mClassId = static_cast<int>(source.itemClassId);
        request.mCount = source.itemCount;
        if (request.mEnchanted == -1) {
            CObject* const object = g_ObjectDB.GetObjectW(request.mClassId, false);
            CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
            if (item != nullptr && item->m_data != nullptr) {
                request.mEnchanted = item->m_data->GetEnchanted();
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Item[%d] is not exist.",
                //     L"EventCampaign.cpp",
                //     0x101,
                //     request.mClassId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Item[%d] is not exist.",
                //     L"EventCampaign.cpp",
                //     0x101,
                //     request.mClassId)
                L2_LOG_ERROR(
                    Log,
                    "Campaign start item for character %u, group index %d: item class %d has no usable prototype; the item "
                    "request is retained without resolving its enchant level.",
                    user.GetDBID(), m_ID, request.mClassId
                );
            }
        }
    }

    L2_GUARD(user.m_ManipulateQueueLock);
    CPacketHelperEx<20480> packet;
    User::etcMsg message = user.SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = user.SaveManipulateQueue(
        ManipulateType::MANI_CAMPAIGN_ITEM, 0, requestCount, requests, nullptr, 0, packet, false, ItemGetReason::Etc, message
    );

    const User::ManiItemResultType result = user._SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        static_cast<void>(user._HandleItemManipulation(queue));
        if (user.GetTrade() != nullptr) {
            user.TradeCancel(false);
        }
        return;
    }
    if (result == User::MRT_JOBMAP) {
        if (user.GetTrade() != nullptr) {
            user.TradeCancel(false);
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Duple",
        //     L"EventCampaign.cpp",
        //     0x12b)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Duple",
        //     L"EventCampaign.cpp",
        //     0x12b)
        L2_LOG_WARN(
            Log,
            "Cannot queue campaign start items for character %u, group index %d: another campaign item job is already "
            "registered; this request is not added.",
            user.GetDBID(), m_ID
        );
        return;
    }
    if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"EventCampaign.cpp",
        //     0x130)
        // Noise: MRT_JOBNOTMAP accepts this campaign item job behind existing work. Normal queue insertion needs no operator
        // message. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"EventCampaign.cpp",
        //     0x130)
        return;
    }
    if (user.GetTrade() != nullptr) {
        user.TradeCancel(false);
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"EventCampaign.cpp",
    //     0x139)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"EventCampaign.cpp",
    //     0x139)
    L2_LOG_ERROR(
        Log,
        "Cannot dispatch campaign start items for character %u, group index %d: unexpected item-queue result %d for %d item "
        "requests.",
        user.GetDBID(), m_ID, static_cast<int>(result), requestCount
    );
}

void CEventCampaignStepMultiInfo::GiveRewardItemToUser(User& user) {
    // Current L2Server 0x00588f68.
    L2_GUARD_FUNCTION();
    const int requestCount = static_cast<int>(m_RewardList.size());
    if (requestCount <= 0) {
        return;
    }

    ItemManip requests[100]{};
    for (int index = 0; index < requestCount; ++index) {
        const RewardItemDataType& source = m_RewardList[static_cast<std::size_t>(index)];
        ItemManip& request = requests[index];
        request.mClassId = static_cast<int>(source.itemClassId);
        request.mCount = source.itemCount;
        if (request.mEnchanted == -1) {
            CObject* const object = g_ObjectDB.GetObjectW(request.mClassId, false);
            CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
            if (item != nullptr && item->m_data != nullptr) {
                request.mEnchanted = item->m_data->GetEnchanted();
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Item[%d] is not exist.",
                //     L"EventCampaign.cpp",
                //     0x15b,
                //     request.mClassId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Item[%d] is not exist.",
                //     L"EventCampaign.cpp",
                //     0x15b,
                //     request.mClassId)
                L2_LOG_ERROR(
                    Log,
                    "Campaign reward item for character %u, group index %d: item class %d has no usable prototype; the item "
                    "request is retained without resolving its enchant level.",
                    user.GetDBID(), m_ID, request.mClassId
                );
            }
        }
    }

    L2_GUARD(user.m_ManipulateQueueLock);
    CPacketHelperEx<20480> packet;
    User::etcMsg message = user.SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = user.SaveManipulateQueue(
        ManipulateType::MANI_CAMPAIGN_ITEM, 0, requestCount, requests, nullptr, 0, packet, false, ItemGetReason::Etc, message
    );

    const User::ManiItemResultType result = user._SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        static_cast<void>(user._HandleItemManipulation(queue));
        if (user.GetTrade() != nullptr) {
            user.TradeCancel(false);
        }
        return;
    }
    if (result == User::MRT_JOBMAP) {
        if (user.GetTrade() != nullptr) {
            user.TradeCancel(false);
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Duple",
        //     L"EventCampaign.cpp",
        //     0x185)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Duple",
        //     L"EventCampaign.cpp",
        //     0x185)
        L2_LOG_WARN(
            Log,
            "Cannot queue campaign reward items for character %u, group index %d: another campaign item job is already "
            "registered; this request is not added.",
            user.GetDBID(), m_ID
        );
        return;
    }
    if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"EventCampaign.cpp",
        //     0x18a)
        // Noise: MRT_JOBNOTMAP accepts this campaign item job behind existing work. Normal queue insertion needs no operator
        // message. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"EventCampaign.cpp",
        //     0x18a)
        return;
    }
    if (user.GetTrade() != nullptr) {
        user.TradeCancel(false);
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"EventCampaign.cpp",
    //     0x193)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"EventCampaign.cpp",
    //     0x193)
    L2_LOG_ERROR(
        Log,
        "Cannot dispatch campaign reward items for character %u, group index %d: unexpected item-queue result %d for %d "
        "item requests.",
        user.GetDBID(), m_ID, static_cast<int>(result), requestCount
    );
}

void CEventCampaignStepMultiInfo::GiveEliteRewardItemToUser(User& user) {
    // Current L2Server 0x00589418.
    L2_GUARD_FUNCTION();
    const int rewardCount = static_cast<int>(m_RewardList.size());
    const int eliteCount = static_cast<int>(m_EliteRewardList.size());
    ItemManip requests[100]{};

    if (rewardCount > 0) {
        for (int index = 0; index < rewardCount; ++index) {
            const RewardItemDataType& source = m_EliteRewardList[static_cast<std::size_t>(index)];
            ItemManip& request = requests[index];
            request.mClassId = static_cast<int>(source.itemClassId);
            request.mCount = source.itemCount;
            if (request.mEnchanted == -1) {
                CObject* const object = g_ObjectDB.GetObjectW(request.mClassId, false);
                CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
                if (item != nullptr && item->m_data != nullptr) {
                    request.mEnchanted = item->m_data->GetEnchanted();
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Item[%d] is not exist.",
                    //     L"EventCampaign.cpp",
                    //     0x1b5,
                    //     request.mClassId)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] Item[%d] is not exist.",
                    //     L"EventCampaign.cpp",
                    //     0x1b5,
                    //     request.mClassId)
                    L2_LOG_ERROR(
                        Log,
                        "Campaign elite reward item for character %u, group index %d: item class %d has no usable "
                        "prototype; the item request is retained without resolving its enchant level.",
                        user.GetDBID(), m_ID, request.mClassId
                    );
                }
            }
        }
        for (int index = 0; index < rewardCount; ++index) {
            const RewardItemDataType& source = m_RewardList[static_cast<std::size_t>(index)];
            ItemManip& request = requests[eliteCount + index];
            request.mClassId = static_cast<int>(source.itemClassId);
            request.mCount = source.itemCount;
            if (request.mEnchanted == -1) {
                CObject* const object = g_ObjectDB.GetObjectW(request.mClassId, false);
                CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
                if (item != nullptr && item->m_data != nullptr) {
                    request.mEnchanted = item->m_data->GetEnchanted();
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Item[%d] is not exist.",
                    //     L"EventCampaign.cpp",
                    //     0x1cb,
                    //     request.mClassId)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] Item[%d] is not exist.",
                    //     L"EventCampaign.cpp",
                    //     0x1cb,
                    //     request.mClassId)
                    L2_LOG_ERROR(
                        Log,
                        "Campaign regular item in an elite reward for character %u, group index %d: item class %d has no "
                        "usable prototype; the item request is retained without resolving its enchant level.",
                        user.GetDBID(), m_ID, request.mClassId
                    );
                }
            }
        }
    }

    const int requestCount = rewardCount + eliteCount;
    if (requestCount <= 0) {
        return;
    }
    L2_GUARD(user.m_ManipulateQueueLock);
    CPacketHelperEx<20480> packet;
    User::etcMsg message = user.SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = user.SaveManipulateQueue(
        ManipulateType::MANI_CAMPAIGN_ITEM, 0, requestCount, requests, nullptr, 0, packet, false, ItemGetReason::Etc, message
    );

    const User::ManiItemResultType result = user._SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        static_cast<void>(user._HandleItemManipulation(queue));
        if (user.GetTrade() != nullptr) {
            user.TradeCancel(false);
        }
        return;
    }
    if (result == User::MRT_JOBMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Duple",
        //     L"EventCampaign.cpp",
        //     0x1f2)
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Duple",
        //     L"EventCampaign.cpp",
        //     0x1f2)
        L2_LOG_WARN(
            Log,
            "Cannot queue campaign elite reward items for character %u, group index %d: another campaign item job is "
            "already registered; this request is not added.",
            user.GetDBID(), m_ID
        );
        return;
    }
    if (result == User::MRT_JOBNOTMAP) {
        if (user.GetTrade() != nullptr) {
            user.TradeCancel(false);
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"EventCampaign.cpp",
        //     0x1fb)
        // Noise: MRT_JOBNOTMAP accepts this campaign item job behind existing work. Normal queue insertion needs no operator
        // message. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"EventCampaign.cpp",
        //     0x1fb)
        return;
    }
    if (user.GetTrade() != nullptr) {
        user.TradeCancel(false);
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"EventCampaign.cpp",
    //     0x204)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"EventCampaign.cpp",
    //     0x204)
    L2_LOG_ERROR(
        Log,
        "Cannot dispatch campaign elite reward items for character %u, group index %d: unexpected item-queue result %d for "
        "%d item requests.",
        user.GetDBID(), m_ID, static_cast<int>(result), requestCount
    );
}

CEventCampaignStep::CEventCampaignStep(const int id) :
    m_MultiInfo{},
    m_PrevStep(nullptr),
    m_NextStep(nullptr),
    m_AdjustedGoalRate{},
    m_StepID(id),
    m_ActivateDuration(0),
    m_StartTime(0),
    m_EndTime(0),
    m_AlignmentC4ToC8{} {
    L2_GUARD_FUNCTION();
    m_StartHTMLName = std::u16string();
    for (std::size_t index = 0; index < m_MultiInfo.size(); ++index) {
        m_MultiInfo[index] = nullptr;
        m_AdjustedGoalRate[index] = 100;
    }
    m_NPCMakerList.clear();
    m_AreaList.clear();
}

void CEventCampaignStep::SetActivateDuration(const int minutes) {
    L2_GUARD_FUNCTION();
    m_ActivateDuration = minutes * 60;
}

void CEventCampaignStep::AllDataInit() {
    L2_GUARD_FUNCTION();
    m_StartTime = 0;
    m_EndTime = 0;
    for (CEventCampaignStepMultiInfo* const info : m_MultiInfo) {
        if (info != nullptr) {
            info->AllDataInit();
        }
    }
}

void CEventCampaignStep::OnStartWorking(const std::int32_t startTime) {
    L2_GUARD_FUNCTION();
    m_StartTime = startTime;
    m_EndTime = startTime + m_ActivateDuration;
    ActivateNPCMaker();
    ActivateArea();
}

void CEventCampaignStep::ActivateNPCMaker() {
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    for (const std::u16string& makerName : m_NPCMakerList) {
        packet.Assemble("S", makerName.c_str());
    }
    g_npcserver.Send("cccdb", 0xb8, 1, 1, static_cast<int>(m_NPCMakerList.size()), packet.GetLength(), packet.GetPacket());
}

void CEventCampaignStep::ActivateArea() {
    L2_GUARD_FUNCTION();
    for (CArea* const area : m_AreaList) {
        if (area != nullptr && !area->IsOn(0)) {
            area->SetOnOff(true);
        }
    }
}

void CEventCampaignStep::DeactivateNPCMaker(const bool reward) {
    // Current L2Server 0x00583064.
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    for (const std::u16string& makerName : m_NPCMakerList) {
        packet.Assemble("S", makerName.c_str());
    }
    g_npcserver.Send(
        "cccdb", 0xb8, 0, reward ? 1 : 0, static_cast<int>(m_NPCMakerList.size()), packet.GetLength(), packet.GetPacket()
    );
}

void CEventCampaignStep::DeactivateArea() {
    // Current L2Server 0x005831bc.
    L2_GUARD_FUNCTION();
    for (CArea* const area : m_AreaList) {
        if (area != nullptr) {
            area->SetOnOff(false);
        }
    }
}

void CEventCampaignStep::OnEndWorking(const bool reward) {
    // Current L2Server 0x00584240.
    L2_GUARD_FUNCTION();
    DeactivateNPCMaker(reward);
    DeactivateArea();
}

void CEventCampaignStep::OnStartReward(const std::int32_t startTime, const int rewardPeriod, const int successGroup) {
    // Current L2Server 0x00581508.
    L2_GUARD_FUNCTION();
    m_StartTime = startTime;
    m_EndTime = startTime + rewardPeriod;
    for (std::size_t index = 0; index < m_MultiInfo.size(); ++index) {
        if (m_MultiInfo[index] != nullptr) {
            m_MultiInfo[index]->m_SuccessInfo = static_cast<int>(index) == successGroup ? 1 : 0;
        }
    }
}

void CEventCampaignStep::OnStartFailed(const std::int32_t startTime, const int rewardPeriod) {
    L2_GUARD_FUNCTION();
    m_StartTime = startTime;
    m_EndTime = startTime + rewardPeriod;
}

void CEventCampaignStep::OnTickWorking() { L2_GUARD_FUNCTION(); }

void CEventCampaignStep::OnEndReward() {
    L2_GUARD_FUNCTION();
    m_StartTime = 0;
    m_EndTime = 0;
}

void CEventCampaignStep::OnEndFail() {
    L2_GUARD_FUNCTION();
    m_StartTime = 0;
    m_EndTime = 0;
}

int CEventCampaignStep::GetRandomGoalGroupId() const {
    // Current L2Server 0x00581384.
    L2_GUARD_FUNCTION();
    const double roll = ::genrand64_real2();
    int accumulatedRate = 0;
    for (int group = 0; group < 5; ++group) {
        accumulatedRate += m_AdjustedGoalRate[static_cast<std::size_t>(group)];
        if (static_cast<int>(roll * 101.0) <= accumulatedRate) {
            return group;
        }
    }
    return 0;
}

CEventCampaign::CEventCampaign(const int id) :
    m_JoinUserMap(),
    m_ProgressPacket(),
    m_EliteUserPacket(),
    m_WorkingStep(nullptr),
    m_Serializer(nullptr),
    m_ID(id),
    m_State(PERIOD_SLEEP),
    m_EliteUserCount(10),
    m_RewardPeriod(10),
    m_ExecutionPeriod(nullptr),
    m_StartCondition{1, 120},
    m_FirstStep(nullptr),
    m_EventStartTime(0),
    m_Alignment1E1B4To1E1B8{} {
    L2_GUARD_FUNCTION();
    m_StartCondition.m_UserMinLevel = 1;
    m_StartCondition.m_UserMaxLevel = 120;
    for (std::size_t index = 0; index < m_JoinUserMap.size(); ++index) {
        m_JoinUserMap[index].clear();
        m_ProgressPacket[index].Reset();
    }
    m_RewardUserMap.clear();
    m_EliteRewardUserList.clear();
    m_EliteUserPacket.Reset();
    m_Serializer = new EventCampaignSerializer;
}

CEventCampaign::~CEventCampaign() {
    L2_GUARD_FUNCTION();
    m_WorkingStep = nullptr;
    delete m_ExecutionPeriod;
    m_ExecutionPeriod = nullptr;
    for (std::size_t index = 0; index < m_JoinUserMap.size(); ++index) {
        m_JoinUserMap[index].clear();
        m_ProgressPacket[index].Reset();
    }
    m_RewardUserMap.clear();
    m_EliteRewardUserList.clear();
    m_EliteUserPacket.Reset();
    delete m_Serializer;
    m_Serializer = nullptr;
}

L2_DEFINE_OBJECT_CLASS_ID(CEventCampaign)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CEventCampaign)

L2_DEFINE_OBJECT_NEW(CEventCampaign)

L2_DEFINE_OBJECT_ALLOCATOR(CEventCampaign, __GetClassName())

L2_DEFINE_OBJECT_RELEASE(CEventCampaign)

void CEventCampaign::SetEliteCount(const int count) {
    L2_GUARD_FUNCTION();
    m_EliteUserCount = count;
}

void CEventCampaign::SetRewardPeriod(const int minutes) {
    L2_GUARD_FUNCTION();
    m_RewardPeriod = minutes * 60;
}

void CEventCampaign::SetStartConditionLevel(const int minLevel, const int maxLevel) {
    L2_GUARD_FUNCTION();
    if (minLevel <= maxLevel) {
        if (static_cast<std::uint32_t>(minLevel) - 1U < 119U) {
            m_StartCondition.m_UserMinLevel = minLevel;
        }
        if (static_cast<std::uint32_t>(maxLevel) - 1U < 119U) {
            m_StartCondition.m_UserMaxLevel = maxLevel;
        }
    }
}

void CEventCampaign::SetAutoExecutionPeriod(EVENTCAMPAIGN::ExecutionPeriod* const period) {
    L2_GUARD_FUNCTION();
    m_ExecutionPeriod = period;
}

CEventCampaignStep* CEventCampaign::GetLastStep() {
    L2_GUARD_FUNCTION();
    CEventCampaignStep* step = m_FirstStep;
    for (CEventCampaignStep* next = m_FirstStep->m_NextStep; next != nullptr && step->m_StepID <= next->m_StepID;
        next = next->m_NextStep) {
        step = next;
    }
    return step;
}

bool CEventCampaign::AddStep(CEventCampaignStep* const step) {
    L2_GUARD_FUNCTION();
    if (m_FirstStep == nullptr) {
        m_FirstStep = step;
        return true;
    }

    CEventCampaignStep* current = GetLastStep();
    if (current == nullptr) {
        return true;
    }
    do {
        if (current->m_StepID == step->m_StepID) {
            return false;
        }
        if (current->m_StepID < step->m_StepID) {
            step->m_PrevStep = current;
            step->m_NextStep = current->m_NextStep;
            current->m_NextStep = step;
            return true;
        }
        if (current->m_NextStep == nullptr) {
            step->m_PrevStep = current;
            step->m_NextStep = nullptr;
            current->m_NextStep = step;
            return true;
        }
        current = current->m_NextStep;
    } while (true);
}

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(CEventCampaign, __async_arg_AsyncStartEventCampaign)

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(CEventCampaign, __async_arg_AsyncStopEventCampaign)

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(CEventCampaign, __async_arg_AsyncTimerExpired)

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(CEventCampaign, __async_arg_AsyncStartNewStep)

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CEventCampaign, __async_arg_AsyncUserEnterWorld)

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CEventCampaign, __async_arg_AsyncUserTryRequestCampaignHTML)

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CEventCampaign, __async_arg_AsyncTrySendProgressInfo)

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(CEventCampaign, __async_arg_AsyncInitNPCMaker)

L2_ASYNC_DEFINE_FIELDWISE_COPY_6(CEventCampaign, __async_arg_AsyncTryAddPoint)

L2_ASYNC_DEFINE_FIELDWISE_COPY_2(CEventCampaign, __async_arg_AsyncTryJoinEventCampaign)

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CEventCampaign, __async_arg_AsyncUserTryReceiveReward)

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(CEventCampaign, __async_arg_AsyncUserTrySendRanking)

bool CEventCampaign::CanExcute(const std::int32_t currentTime) {
    // Current L2Server 0x0058327c.
    L2_GUARD_FUNCTION();
    if (!m_ExecutionPeriod->CanExecute(currentTime)) {
        return false;
    }

    const std::tm* const local = runtime::crt::LocalTime32(currentTime);
    const int currentMinute = local->tm_hour * 60 + local->tm_min;
    bool result = false;
    for (const EventCampaignStartTimeData& start : m_StartTimeList) {
        if (start.m_StartTime <= currentMinute && currentMinute <= runtime::AddInt32Wrapping(start.m_StartTime, 20) &&
            runtime::Win64CvttDoubleToInt32(::genrand64_real2() * static_cast<double>(start.m_Dice) + 1.0) == 1) {
            result = true;
        }
    }
    return result;
}

bool CEventCampaign::CanStartEventCampaign(const User& user) const {
    L2_GUARD_FUNCTION();
    const int level = user.Level();
    return m_StartCondition.m_UserMinLevel <= level && level <= m_StartCondition.m_UserMaxLevel;
}

bool CEventCampaign::CanJoinEventCampaign(User& user) const {
    // Current L2Server 0x005822b0.
    L2_GUARD_FUNCTION();
    if (!CanStartEventCampaign(user)) {
        user.SendSystemMessage(SystemMessageId::CANNOT_JOIN_EVENT_CAMPAIGN);
        return false;
    }
    if (user.GetTrade() != nullptr) {
        user.SendSystemMessage(SystemMessageId::CANNOT_JOIN_EVENT_CAMPAIGN);
        return false;
    }
    if (user.GetPrivateStoreType() != 0) {
        user.SendSystemMessage(SystemMessageId::CANNOT_JOIN_EVENT_CAMPAIGN);
        return false;
    }
    if (!user.IsAlive()) {
        user.SendSystemMessage(SystemMessageId::CANNOT_JOIN_EVENT_CAMPAIGN);
        return false;
    }
    if (static_cast<float>(user.GetInventoryLimit()) * 0.8F < static_cast<float>(user.m_inv.GetTotalCount()) ||
        user.GetWeightLimit(69000.0) * 0.800000011920929 < static_cast<double>(user.m_inv.GetCarryingWeight())) {
        user.SendSystemMessage(SystemMessageId::CANNOT_JOIN_EVENT_CAMPAIGN_INVENTORY_WEIGHT_OR_QUANTITY_EXCEEDED);
        return false;
    }
    if (user.IsInDuel()) {
        user.SendSystemMessage(SystemMessageId::CANNOT_JOIN_EVENT_CAMPAIGN);
        return false;
    }
    if (user.IsRegisteredAtOlympiad()) {
        user.SendSystemMessage(SystemMessageId::CANNOT_JOIN_EVENT_CAMPAIGN);
        return false;
    }
    if (user.IsOlympiadPlaying()) {
        user.SendSystemMessage(SystemMessageId::CANNOT_JOIN_EVENT_CAMPAIGN);
        return false;
    }
    if (user.IsOlympiadPlayer()) {
        user.SendSystemMessage(SystemMessageId::CANNOT_JOIN_EVENT_CAMPAIGN);
        return false;
    }
    if (user.m_PledgeGameUser.IsPlaying()) {
        user.SendSystemMessage(SystemMessageId::CANNOT_JOIN_EVENT_CAMPAIGN);
        return false;
    }
    if (user.m_data->m_PVPMatchType != PVPMatchType::NONE) {
        user.SendSystemMessage(SystemMessageId::CANNOT_JOIN_EVENT_CAMPAIGN);
        return false;
    }
    if (user.IsInMySiegeBattleField() != 0 || user.m_nInstantZoneID != 0) {
        user.SendSystemMessage(SystemMessageId::CANNOT_JOIN_EVENT_CAMPAIGN_IN_THIS_AREA);
        return false;
    }
    CArea* const prison = CAreaDB::inst().FindArea(u"gm_prison");
    if (prison != nullptr && prison->m_FieldTerritory.IsInside(user.GetPos(), true)) {
        user.SendSystemMessage(SystemMessageId::CANNOT_JOIN_EVENT_CAMPAIGN_IN_THIS_AREA);
        return false;
    }
    if (user.IsTransformed(TransformType::Flying) != nullptr) {
        user.SendSystemMessage(SystemMessageId::CANNOT_JOIN_EVENT_CAMPAIGN_IN_FLIGHT_TRANSFORMATION);
        return false;
    }
    if (user.IsEinhasadeTransformed() || user.m_data->m_nKarma < 0) {
        user.SendSystemMessage(SystemMessageId::CANNOT_JOIN_EVENT_CAMPAIGN_WHILE_CHAOTIC);
        return false;
    }
    if (user.m_data->m_TrainingState == 1) {
        user.SendSystemMessage(SystemMessageId::CANNOT_TAKE_OTHER_ACTIONS_WHILE_IN_TRAINING_CAMP);
        return false;
    }
    return true;
}

int CEventCampaign::GetTimeLeft() const {
    L2_GUARD_FUNCTION();
    if (m_WorkingStep == nullptr) {
        return 0;
    }
    return m_WorkingStep->m_EndTime - runtime::crt::Time32Now();
}

void CEventCampaign::ChangeState(const int state) noexcept {
    L2_GUARD_FUNCTION();
    m_State = state;
}

void CEventCampaign::TimerExpired(const int id) {
    L2_GUARD_FUNCTION();
    if (id == 0) {
        __async_arg_AsyncTimerExpired argument;
        m_Serializer->AddExecutable(this, &CEventCampaign::InvokeAsyncTimerExpired, &argument, L"AsyncTimerExpired");
    }
    if (m_State != PERIOD_SLEEP) {
        AddTimer(2500, 0);
    }
}

void CEventCampaign::OnTick() {
    L2_GUARD_FUNCTION();
    const auto periodState = [](const CEventCampaignStep& step, const std::int32_t now) {
        if (now < step.m_StartTime) {
            return -1;
        }
        if (now < step.m_EndTime) {
            return 0;
        }
        return 1;
    };

    if (m_State == PERIOD_WORKING) {
        const std::int32_t now = runtime::crt::Time32Now();
        const int state = periodState(*m_WorkingStep, now);
        if (state == 0) {
            m_WorkingStep->OnTickWorking();
            CollectProgressInfo();
        } else if (state == 1) {
            ChangeState(PERIOD_FAILED);
            const std::int32_t failedAt = runtime::crt::Time32Now();
            m_WorkingStep->OnEndWorking(false);
            m_WorkingStep->OnStartFailed(failedAt, m_RewardPeriod);
        }
        return;
    }

    bool finished = false;
    if (m_State == PERIOD_REWARD) {
        const int state = periodState(*m_WorkingStep, runtime::crt::Time32Now());
        if (state == 0) {
            CollectProgressInfo();
        } else if (state == 1) {
            m_WorkingStep->OnEndReward();
            finished = true;
        }
    } else if (m_State == PERIOD_FAILED) {
        const int state = periodState(*m_WorkingStep, runtime::crt::Time32Now());
        if (state == 0) {
            CollectProgressInfo();
        } else if (state == 1) {
            m_WorkingStep->OnEndFail();
            finished = true;
        }
    }

    if (!finished) {
        return;
    }
    InitJoinUserInfo();
    RewardAndUserMapDataInit();
    BroadcastFinishEventCampaignToAllUser();
    if (m_WorkingStep->m_NextStep == nullptr) {
        ChangeState(PERIOD_SLEEP);
    } else {
        __async_arg_AsyncStartNewStep argument;
        m_Serializer->AddExecutable(this, &CEventCampaign::InvokeAsyncStartNewStep, &argument, L"AsyncStartNewStep");
    }
}

void CEventCampaign::InitJoinUserInfo() {
    L2_GUARD_FUNCTION();
    for (const JoinUserMap& users : m_JoinUserMap) {
        for (const auto& entry : users) {
            CSPointer<User> user = g_UserDBIDMap.FindObject(
                std::CDualKey<int, int>(runtime::process::CurrentDatabaseId(), static_cast<int>(entry.first))
            );
            if (user) {
                user->InitEventCampaignInfo();
            }
        }
    }
}

void CEventCampaign::RewardAndUserMapDataInit() {
    L2_GUARD_FUNCTION();
    for (std::size_t index = 0; index < m_JoinUserMap.size(); ++index) {
        m_JoinUserMap[index].clear();
        m_ProgressPacket[index].Reset();
    }
    m_RewardUserMap.clear();
    m_EliteRewardUserList.clear();
    m_EliteUserPacket.Reset();
}

void CEventCampaign::InvokeAsyncStart(CEventCampaign* owner, CExecutionArgumentPad* argument) {
    owner->__async_AsyncStartEventCampaign(argument);
}

void CEventCampaign::InvokeAsyncStop(CEventCampaign* owner, CExecutionArgumentPad* argument) {
    owner->__async_AsyncStopEventCampaign(argument);
}

void CEventCampaign::InvokeAsyncTimerExpired(CEventCampaign* owner, CExecutionArgumentPad* argument) {
    owner->__async_AsyncTimerExpired(argument);
}

void CEventCampaign::InvokeAsyncStartNewStep(CEventCampaign* owner, CExecutionArgumentPad* argument) {
    owner->__async_AsyncStartNewStep(argument);
}

void CEventCampaign::InvokeAsyncUserEnterWorld(CEventCampaign* owner, CExecutionArgumentPad* argument) {
    owner->__async_AsyncUserEnterWorld(argument);
}

void CEventCampaign::InvokeAsyncUserTryRequestCampaignHTML(CEventCampaign* owner, CExecutionArgumentPad* argument) {
    owner->__async_AsyncUserTryRequestCampaignHTML(argument);
}

void CEventCampaign::InvokeAsyncTrySendProgressInfo(CEventCampaign* owner, CExecutionArgumentPad* argument) {
    owner->__async_AsyncTrySendProgressInfo(argument);
}

void CEventCampaign::InvokeAsyncInitNPCMaker(CEventCampaign* owner, CExecutionArgumentPad* argument) {
    owner->__async_AsyncInitNPCMaker(argument);
}

void CEventCampaign::InvokeAsyncTryJoinEventCampaign(CEventCampaign* owner, CExecutionArgumentPad* argument) {
    owner->__async_AsyncTryJoinEventCampaign(argument);
}

void CEventCampaign::InvokeAsyncUserTryReceiveReward(CEventCampaign* owner, CExecutionArgumentPad* argument) {
    owner->__async_AsyncUserTryReceiveReward(argument);
}

void CEventCampaign::InvokeAsyncUserTrySendRanking(CEventCampaign* owner, CExecutionArgumentPad* argument) {
    owner->__async_AsyncUserTrySendRanking(argument);
}

void CEventCampaign::InvokeAsyncTryAddPoint(CEventCampaign* owner, CExecutionArgumentPad* argument) {
    owner->__async_AsyncTryAddPoint(argument);
}

void CEventCampaign::TryStart() {
    L2_GUARD_FUNCTION();
    __async_arg_AsyncStartEventCampaign argument;
    m_Serializer->AddExecutable(
        this, &CEventCampaign::InvokeAsyncStart, &argument, L"CEventCampaign::AsyncStartEventCampaign"
    );
}

void CEventCampaign::TryStop() {
    // Current L2Server 0x00586dcc.
    L2_GUARD_FUNCTION();
    __async_arg_AsyncStopEventCampaign argument;
    m_Serializer->AddExecutable(this, &CEventCampaign::InvokeAsyncStop, &argument, L"AsyncStopEventCampaign");
}

void CEventCampaign::__async_AsyncStartEventCampaign(CExecutionArgumentPad*) {
    L2_GUARD_FUNCTION();
    if (m_State != PERIOD_SLEEP || m_FirstStep == nullptr) {
        return;
    }

    ChangeState(PERIOD_WORKING);
    CIOObject::SetTimeStamp(&m_EventStartTime);
    AddTimer(2500, 0);
    m_FirstStep->OnStartWorking(m_EventStartTime);
    m_WorkingStep = m_FirstStep;
    CollectProgressInfo();
    BroadcastStartEventCampaignToAllUser();
}

void CEventCampaign::__async_AsyncStopEventCampaign(CExecutionArgumentPad*) {
    // Current L2Server 0x0058684c.
    L2_GUARD_FUNCTION();
    if (m_WorkingStep == nullptr || m_State == PERIOD_SLEEP) {
        return;
    }

    if (m_State == PERIOD_WORKING) {
        ChangeState(PERIOD_SLEEP);
        m_WorkingStep->OnEndWorking(false);
    } else if (m_State == PERIOD_REWARD) {
        ChangeState(PERIOD_SLEEP);
        m_WorkingStep->OnEndReward();
    } else if (m_State == PERIOD_FAILED) {
        ChangeState(PERIOD_SLEEP);
        m_WorkingStep->OnEndFail();
    } else {
        return;
    }

    InitJoinUserInfo();
    BroadcastFinishEventCampaignToAllUser();
}

void CEventCampaign::__async_AsyncTimerExpired(CExecutionArgumentPad*) {
    L2_GUARD_FUNCTION();
    if (m_WorkingStep != nullptr) {
        OnTick();
    }
}

void CEventCampaign::__async_AsyncStartNewStep(CExecutionArgumentPad*) {
    L2_GUARD_FUNCTION();
    if ((m_State != PERIOD_REWARD && m_State != PERIOD_FAILED) || m_WorkingStep == nullptr ||
        m_WorkingStep->m_NextStep == nullptr) {
        return;
    }

    ChangeState(PERIOD_WORKING);
    std::int32_t startTime = 0;
    CIOObject::SetTimeStamp(&startTime);
    AddTimer(2500, 0);
    CEventCampaignStep* const next = m_WorkingStep->m_NextStep;
    next->OnStartWorking(startTime);
    m_WorkingStep = next;
    CollectProgressInfo();
    BroadcastStartEventCampaignToAllUser();
}

void CEventCampaign::AllDataInit() {
    L2_GUARD_FUNCTION();
    m_WorkingStep = nullptr;
    m_State = PERIOD_SLEEP;
    m_EventStartTime = 0;
    for (std::size_t index = 0; index < m_JoinUserMap.size(); ++index) {
        m_JoinUserMap[index].clear();
        m_ProgressPacket[index].Reset();
    }
    m_RewardUserMap.clear();
    m_EliteRewardUserList.clear();
    m_EliteUserPacket.Reset();
    for (CEventCampaignStep* step = m_FirstStep; step != nullptr; step = step->m_NextStep) {
        step->AllDataInit();
    }
}

int CEventCampaign::FindUserGoalGroup(User& user) {
    // Current L2Server 0x00585598.
    L2_GUARD_FUNCTION();
    if (m_WorkingStep != nullptr) {
        for (std::size_t group = 0; group < m_JoinUserMap.size(); ++group) {
            const JoinUserMap& users = m_JoinUserMap[group];
            if (users.find(user.GetDBID()) != users.end()) {
                return static_cast<int>(group);
            }
        }
    }
    return -1;
}

void CEventCampaign::ShowGoalInfo(User& user) {
    // Current L2Server 0x005856ac trusts the joined-group and
    // working-step relationship after the initial socket gate.
    L2_GUARD_FUNCTION();
    if (user.GetSocket() == nullptr) {
        return;
    }

    const int group = FindUserGoalGroup(user);
    CEventCampaignStepMultiInfo* const info = m_WorkingStep->m_MultiInfo[static_cast<std::size_t>(group)];
    for (const auto& goal : info->m_GoalMap) {
        std::array<char16_t, 128> message;
        FormatUtf16To(
            message.data(), message.size(), u"Progress(Goal Id : %d) : %d / %d", static_cast<int>(goal.first),
            goal.second.m_CurrentPoint, goal.second.m_GoalPoint
        );
        user.GetSocket()->SendSystemMessage(u"SYS", message.data());
    }
}

void CEventCampaign::UserEnterWorld(User& user) {
    L2_GUARD_FUNCTION();
    if (m_WorkingStep == nullptr) {
        return;
    }

    L2_ADD_REF(&(user), ObjectRefType::EventCampaignSerializer);
    __async_arg_AsyncUserEnterWorld argument(&user);
    m_Serializer->AddExecutable(
        this, &CEventCampaign::InvokeAsyncUserEnterWorld, &argument, L"CEventCampaign::AsyncUserEnterWorld"
    );
}

void CEventCampaign::InitNPCMaker() {
    // Current L2Server 0x005842f0.
    L2_GUARD_FUNCTION();
    if (m_WorkingStep == nullptr) {
        return;
    }
    __async_arg_AsyncInitNPCMaker argument;
    m_Serializer->AddExecutable(this, &CEventCampaign::InvokeAsyncInitNPCMaker, &argument, L"AsyncInitNPCMaker");
}

void CEventCampaign::TryJoinEventCampaign(User& user, const int goalGroupId) {
    // Current L2Server 0x0058a7d8.
    L2_GUARD_FUNCTION();
    if (m_WorkingStep == nullptr) {
        return;
    }
    L2_ADD_REF(&(user), ObjectRefType::EventCampaignSerializer);
    __async_arg_AsyncTryJoinEventCampaign argument(&user, goalGroupId);
    m_Serializer->AddExecutable(
        this, &CEventCampaign::InvokeAsyncTryJoinEventCampaign, &argument, L"AsyncTryJoinEventCampaign"
    );
}

void CEventCampaign::UserTryRequestCampaignHTML(User& user) {
    // Current L2Server 0x005862ec.
    L2_GUARD_FUNCTION();
    if (m_WorkingStep == nullptr) {
        return;
    }
    L2_ADD_REF(&(user), ObjectRefType::EventCampaignSerializer);
    __async_arg_AsyncUserTryRequestCampaignHTML argument(&user);
    m_Serializer->AddExecutable(
        this, &CEventCampaign::InvokeAsyncUserTryRequestCampaignHTML, &argument, L"AsyncUserTryRequestCampaignHTML"
    );
}

void CEventCampaign::TrySendProgressInfo(User& user) {
    // Current L2Server 0x005864e4.
    L2_GUARD_FUNCTION();
    if (m_WorkingStep == nullptr) {
        return;
    }
    L2_ADD_REF(&(user), ObjectRefType::EventCampaignSerializer);
    __async_arg_AsyncTrySendProgressInfo argument(&user);
    m_Serializer->AddExecutable(
        this, &CEventCampaign::InvokeAsyncTrySendProgressInfo, &argument, L"AsyncTrySendProgressInfo"
    );
}

void CEventCampaign::UserTryReceiveReward(User& user) {
    // Current L2Server 0x0058a8e8.
    L2_GUARD_FUNCTION();
    if (m_WorkingStep == nullptr) {
        return;
    }
    L2_ADD_REF(&(user), ObjectRefType::EventCampaignSerializer);
    __async_arg_AsyncUserTryReceiveReward argument(&user);
    m_Serializer->AddExecutable(
        this, &CEventCampaign::InvokeAsyncUserTryReceiveReward, &argument, L"AsyncUserTryReceiveReward"
    );
}

void CEventCampaign::UserTrySendRanking(User& user) {
    // Current L2Server 0x005865e0.
    L2_GUARD_FUNCTION();
    if (m_WorkingStep == nullptr) {
        return;
    }
    L2_ADD_REF(&(user), ObjectRefType::EventCampaignSerializer);
    __async_arg_AsyncUserTrySendRanking argument(&user);
    m_Serializer->AddExecutable(this, &CEventCampaign::InvokeAsyncUserTrySendRanking, &argument, L"AsyncUserTrySendRanking");
}

void CEventCampaign::TryAddPoint(
    User& user, const int campaignId, const int stepId, const int groupId, const int goalId, const int point
) {
    // Current L2Server 0x0058bfd4.
    L2_GUARD_FUNCTION();
    if (m_WorkingStep == nullptr) {
        return;
    }
    L2_ADD_REF(&(user), ObjectRefType::EventCampaignSerializer);
    __async_arg_AsyncTryAddPoint argument(&user, campaignId, stepId, groupId, goalId, point);
    m_Serializer->AddExecutable(this, &CEventCampaign::InvokeAsyncTryAddPoint, &argument, L"AsyncTryAddPoint");
}

void CEventCampaign::__async_AsyncInitNPCMaker(CExecutionArgumentPad*) {
    // Current L2Server 0x0058382c.
    L2_GUARD_FUNCTION();
    if (m_WorkingStep != nullptr) {
        m_WorkingStep->ActivateNPCMaker();
    }
}

int CEventCampaign::IsAllGoalCompleted() const {
    // Current L2Server 0x00583578.
    L2_GUARD_FUNCTION();
    if (m_WorkingStep == nullptr) {
        return 0;
    }
    for (std::size_t group = 0; group < m_WorkingStep->m_MultiInfo.size(); ++group) {
        CEventCampaignStepMultiInfo* const info = m_WorkingStep->m_MultiInfo[group];
        if (info == nullptr) {
            continue;
        }
        bool complete = true;
        for (const auto& goal : info->m_GoalMap) {
            if (goal.second.m_CurrentPoint < goal.second.m_GoalPoint) {
                complete = false;
                break;
            }
        }
        if (complete) {
            return static_cast<int>(group);
        }
    }
    return -1;
}

void CEventCampaign::MakeRankingBoard() {
    // Current L2Server 0x0058368c.
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> rows;
    int rowCount = 0;
    for (const EventCampaignUserScoreInfo& elite : m_EliteRewardUserList) {
        if (m_EliteUserCount <= rowCount) {
            break;
        }
        rows.Assemble("S", elite.m_UserName.c_str());
        ++rowCount;
    }

    m_EliteUserPacket.Reset();
    m_EliteUserPacket.Assemble("dd", m_ID, m_WorkingStep->m_StepID);
    m_EliteUserPacket.Assemble("db", rows.GetAssembledCount(), rows.GetLength(), rows.GetPacket());
}

void CEventCampaign::MakeEliteRewardUserMap() {
    // Current L2Server 0x0058b730.
    L2_GUARD_FUNCTION();
    m_EliteRewardUserList.clear();

    auto reward = m_RewardUserMap.begin();
    if (reward == m_RewardUserMap.end()) {
        return;
    }

    m_EliteRewardUserList.push_back(reward->second);
    ++reward;
    for (; reward != m_RewardUserMap.end(); ++reward) {
        auto insertion = m_EliteRewardUserList.begin();
        while (insertion != m_EliteRewardUserList.end() && insertion->m_Score > reward->second.m_Score) {
            ++insertion;
        }
        m_EliteRewardUserList.insert(insertion, reward->second);
    }

    MakeRankingBoard();
}

void CEventCampaign::MakeRewardUserMap(const int successGroup) {
    // Current L2Server 0x0058b8d0.
    L2_GUARD_FUNCTION();
    if (m_WorkingStep == nullptr || m_WorkingStep->m_MultiInfo.data()[successGroup] == nullptr) {
        return;
    }
    m_RewardUserMap.clear();
    for (const auto& entry : m_JoinUserMap.data()[successGroup]) {
        m_RewardUserMap.emplace(entry.first, entry.second);
    }
    MakeEliteRewardUserMap();
}

void CEventCampaign::WorkingStepSuccess(const int successGroup) {
    // Current L2Server 0x0058bb08.
    L2_GUARD_FUNCTION();
    if (m_State != PERIOD_WORKING || m_WorkingStep == nullptr) {
        return;
    }
    MakeRewardUserMap(successGroup);
    ChangeState(PERIOD_REWARD);
    const std::int32_t now = runtime::crt::Time32Now();
    m_WorkingStep->OnEndWorking(true);
    m_WorkingStep->OnStartReward(now, m_RewardPeriod, successGroup);
}

void CEventCampaign::__async_AsyncTryAddPoint(CExecutionArgumentPad* argument) {
    // Current L2Server 0x0058bbfc.
    L2_GUARD_FUNCTION();
    User* user = nullptr;
    int groupId = 0;
    int goalId = 0;
    int point = 0;
    if (argument != nullptr) {
        std::memcpy(&user, argument->Buffer + 0x08, sizeof(user));
        std::memcpy(&groupId, argument->Buffer + 0x18, sizeof(groupId));
        std::memcpy(&goalId, argument->Buffer + 0x1c, sizeof(goalId));
        std::memcpy(&point, argument->Buffer + 0x20, sizeof(point));
    }

    if (m_State == PERIOD_WORKING && m_WorkingStep != nullptr && groupId >= 0 && groupId < 5) {
        CEventCampaignStepMultiInfo* const info = m_WorkingStep->m_MultiInfo[static_cast<std::size_t>(groupId)];
        if (info != nullptr) {
            auto goal = info->m_GoalMap.find(static_cast<unsigned int>(goalId));
            if (goal != info->m_GoalMap.end()) {
                goal->second.m_CurrentPoint += point;
                for (std::size_t oldGroup = 0; oldGroup < m_JoinUserMap.size(); ++oldGroup) {
                    auto position = m_JoinUserMap[oldGroup].find(user->GetDBID());
                    if (position == m_JoinUserMap[oldGroup].end()) {
                        continue;
                    }
                    if (oldGroup == static_cast<std::size_t>(groupId)) {
                        position->second.m_Score += point;
                    } else {
                        m_JoinUserMap[oldGroup].erase(position);
                        EventCampaignUserScoreInfo score;
                        const std::u16string userName = std::u16string(user->GetName());
                        score.m_UserDBID = user->GetDBID();
                        score.m_Score = point;
                        score.m_UserName.assign(std::u16string_view(userName));
                        m_JoinUserMap[static_cast<std::size_t>(groupId)].emplace(user->GetDBID(), score);
                        user->JoinEventCampaign(m_ID, m_WorkingStep->m_StepID, groupId);
                    }
                    break;
                }
                const int completedGroup = IsAllGoalCompleted();
                if (completedGroup != -1) {
                    WorkingStepSuccess(completedGroup);
                }
            }
        }
    }

    if (user != nullptr) {
        L2_RELEASE(user, ObjectRefType::EventCampaignSerializer);
    }
}

void CEventCampaign::__async_AsyncTryJoinEventCampaign(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00589f8c.
    L2_GUARD_FUNCTION();
    User* user = nullptr;
    int goalGroupId = 0;
    std::memcpy(&user, argument->Buffer + 0x08, sizeof(user));
    std::memcpy(&goalGroupId, argument->Buffer + 0x10, sizeof(goalGroupId));

    if (m_State == PERIOD_WORKING && m_WorkingStep != nullptr && CanJoinEventCampaign(*user)) {
        if (goalGroupId == -1) {
            goalGroupId = m_WorkingStep->GetRandomGoalGroupId();
        }

        EventCampaignUserScoreInfo score;
        const std::u16string userName = std::u16string(user->GetName());
        score.m_UserDBID = user->GetDBID();
        score.m_Score = 0;
        score.m_UserName.assign(std::u16string_view(userName));
        m_JoinUserMap[static_cast<std::size_t>(goalGroupId)].emplace(user->GetDBID(), score);

        user->JoinEventCampaign(m_ID, m_WorkingStep->m_StepID, goalGroupId);
        CEventCampaignStepMultiInfo* const info = m_WorkingStep->m_MultiInfo[static_cast<std::size_t>(goalGroupId)];
        info->GiveStartItemToUser(*user);
        const int x = info->m_StartPoint.x;
        const int y = info->m_StartPoint.y;
        const int z = info->m_StartPoint.z;
        user->StopMove();
        user->DoNothing();
        static_cast<void>(user->TeleportToLocation(x, y, z, 0, 0x10000, TeleportSourceType::Unknown, nullptr, 0));
        SendProgressInfoToUserOnWorking(user);
    }

    if (user != nullptr) {
        L2_RELEASE(user, ObjectRefType::EventCampaignSerializer);
    }
}

void CEventCampaign::__async_AsyncUserTryReceiveReward(CExecutionArgumentPad* argument) {
    // Current L2Server 0x0058a2c8.
    L2_GUARD_FUNCTION();
    User* user = nullptr;
    std::memcpy(&user, argument->Buffer + 0x08, sizeof(user));

    if (m_WorkingStep != nullptr && m_State == PERIOD_REWARD) {
        const auto reward = m_RewardUserMap.find(user->GetDBID());
        if (reward != m_RewardUserMap.end()) {
            int elitePosition = 0;
            int position = 0;
            for (const EventCampaignUserScoreInfo& elite : m_EliteRewardUserList) {
                ++position;
                if (m_EliteUserCount < position) {
                    break;
                }
                if (elite.m_UserDBID == user->GetDBID()) {
                    elitePosition = position;
                }
            }

            const int goalGroupId = user->GetEventCampaignGoalGroup();
            CEventCampaignStepMultiInfo* const info = m_WorkingStep->m_MultiInfo[static_cast<std::size_t>(goalGroupId)];
            if (elitePosition < 1) {
                info->GiveRewardItemToUser(*user);
            } else {
                info->GiveEliteRewardItemToUser(*user);
            }
            m_RewardUserMap.erase(reward);
        }
    }

    if (user != nullptr) {
        L2_RELEASE(user, ObjectRefType::EventCampaignSerializer);
    }
}

void CEventCampaign::__async_AsyncUserTrySendRanking(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00585e2c.
    L2_GUARD_FUNCTION();
    User* user = nullptr;
    std::memcpy(&user, argument->Buffer + 0x08, sizeof(user));

    const int goalGroupId = user->GetEventCampaignGoalGroup();
    CEventCampaignStepMultiInfo* const info = m_WorkingStep->m_MultiInfo[static_cast<std::size_t>(goalGroupId)];
    if (info != nullptr && m_WorkingStep != nullptr && m_State == PERIOD_REWARD && info->m_SuccessInfo == 1 &&
        m_JoinUserMap[static_cast<std::size_t>(goalGroupId)].find(user->GetDBID()) !=
            m_JoinUserMap[static_cast<std::size_t>(goalGroupId)].end()) {
        SendRankingInfoToUser(user);
    }

    if (user != nullptr) {
        L2_RELEASE(user, ObjectRefType::EventCampaignSerializer);
    }
}

void CEventCampaign::__async_AsyncUserTryRequestCampaignHTML(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00585a48.
    L2_GUARD_FUNCTION();
    User* user = nullptr;
    std::memcpy(&user, argument->Buffer + 0x08, sizeof(user));

    if (m_State == PERIOD_WORKING) {
        SendHTMLToUserOnWorking(user);
    } else if (m_State == PERIOD_REWARD) {
        SendHTMLToUserOnReward(user);
    } else if (m_State == PERIOD_FAILED) {
        SendHTMLToUserOnFailed(user);
    }

    if (user != nullptr) {
        L2_RELEASE(user, ObjectRefType::EventCampaignSerializer);
    }
}

void CEventCampaign::__async_AsyncTrySendProgressInfo(CExecutionArgumentPad* argument) {
    // Current L2Server 0x00585d14.
    L2_GUARD_FUNCTION();
    User* user = nullptr;
    std::memcpy(&user, argument->Buffer + 0x08, sizeof(user));

    if (m_State == PERIOD_WORKING) {
        SendProgressInfoToUserOnWorking(user);
    } else if (m_State == PERIOD_REWARD) {
        SendProgressInfoToUserOnReward(user);
    } else if (m_State == PERIOD_FAILED) {
        SendProgressInfoToUserOnFailed(user);
    }

    if (user != nullptr) {
        L2_RELEASE(user, ObjectRefType::EventCampaignSerializer);
    }
}

void CEventCampaign::__async_AsyncUserEnterWorld(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();
    User* user = nullptr;
    if (argument != nullptr) {
        std::memcpy(&user, argument->Buffer + 0x08, sizeof(user));
    }

    if (user != nullptr && m_WorkingStep != nullptr && m_State == PERIOD_WORKING) {
        const unsigned int userDbId = user->GetDBID();
        bool joined = false;
        for (std::size_t group = 0; group < m_JoinUserMap.size(); ++group) {
            if (m_JoinUserMap[group].find(userDbId) != m_JoinUserMap[group].end()) {
                user->JoinEventCampaign(m_ID, m_WorkingStep->m_StepID, static_cast<int>(group));
                SendProgressInfoToUserOnWorking(user);
                joined = true;
                break;
            }
        }
        if (!joined) {
            BroadcastStartEventCampaignToUser(*user);
        }
    }

    if (user != nullptr) {
        L2_RELEASE(user, ObjectRefType::EventCampaignSerializer);
    }
}

void CEventCampaign::CollectProgressInfo() {
    L2_GUARD_FUNCTION();
    if (m_WorkingStep == nullptr) {
        return;
    }

    for (std::size_t group = 0; group < m_WorkingStep->m_MultiInfo.size(); ++group) {
        CEventCampaignStepMultiInfo* const info = m_WorkingStep->m_MultiInfo[group];
        if (info == nullptr) {
            continue;
        }

        CPacketHelperEx<20480>& packet = m_ProgressPacket[group];
        packet.Reset();
        packet.Assemble("dddd", m_ID, m_WorkingStep->m_StepID, info->m_ID, GetTimeLeft());
        packet.Assemble("d", info->m_GoalCount);
        for (const auto& goal : info->m_GoalMap) {
            packet.Assemble("ddd", static_cast<int>(goal.first), goal.second.m_GoalPoint, goal.second.m_CurrentPoint);
        }
    }
}

void CEventCampaign::BroadcastStartEventCampaignToAllUser() {
    // Current L2Server 0x00584780 is the physical expansion of
    // FOR_ALL_OBJECT_IN_OBJECT_MAP, including both nested guard frames.
    L2_GUARD_FUNCTION();
    EventCampaignSuggestPacket packet(*this);
    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
        if (CanStartEventCampaign(*user)) {
            packet.Send(*user);
        }
    });
}

void CEventCampaign::BroadcastStartEventCampaignToUser(User& user) {
    L2_GUARD_FUNCTION();
    EventCampaignSuggestPacket packet(*this);
    packet.Send(user);
}

void CEventCampaign::BroadcastFinishEventCampaignToAllUser() {
    // Current L2Server 0x005849dc is the physical expansion of
    // FOR_ALL_OBJECT_IN_OBJECT_MAP, including both nested guard frames.
    L2_GUARD_FUNCTION();
    EventCampaignFinishPacket packet(*this);
    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, { packet.Send(*user); });
}

void CEventCampaign::SendHTMLToUserOnWorking(User* const user) {
    // Current L2Server 0x00584c28.
    L2_GUARD_FUNCTION();
    if (user == nullptr || m_State != PERIOD_WORKING) {
        return;
    }

    for (std::size_t group = 0; group < m_JoinUserMap.size(); ++group) {
        if (m_JoinUserMap[group].find(user->GetDBID()) == m_JoinUserMap[group].end()) {
            continue;
        }

        const int goalGroupId = user->GetEventCampaignGoalGroup();
        CEventCampaignStepMultiInfo* const info = m_WorkingStep->m_MultiInfo[static_cast<std::size_t>(goalGroupId)];
        if (info != nullptr) {
            ShowEventCampaignHTML(*user, info->m_ContextHTMLName);
        }
        return;
    }

    ShowEventCampaignHTML(*user, m_WorkingStep->m_StartHTMLName);
}

void CEventCampaign::SendHTMLToUserOnReward(User* const user) {
    // Current L2Server 0x00584e84.
    L2_GUARD_FUNCTION();
    if (user == nullptr || m_State != PERIOD_REWARD) {
        return;
    }

    const int goalGroupId = user->GetEventCampaignGoalGroup();
    CEventCampaignStepMultiInfo* const info = m_WorkingStep->m_MultiInfo[static_cast<std::size_t>(goalGroupId)];
    if (info == nullptr) {
        return;
    }

    if (info->m_SuccessInfo == 1) {
        if (m_RewardUserMap.find(user->GetDBID()) == m_RewardUserMap.end()) {
            ShowEventCampaignHTML(*user, info->m_RewardReceivedHTMLName);
        } else {
            ShowEventCampaignHTML(*user, info->m_RewardHTMLName);
        }
    } else if (info->m_SuccessInfo == 0) {
        ShowEventCampaignHTML(*user, info->m_FailedHTMLName);
    }
}

void CEventCampaign::SendHTMLToUserOnFailed(User* const user) {
    // Current L2Server 0x00582b94.
    if (user == nullptr || m_State != PERIOD_FAILED) {
        return;
    }

    const int goalGroupId = user->GetEventCampaignGoalGroup();
    CEventCampaignStepMultiInfo* const info = m_WorkingStep->m_MultiInfo[static_cast<std::size_t>(goalGroupId)];
    if (info != nullptr) {
        ShowEventCampaignHTML(*user, info->m_FailedHTMLName);
    }
}

void CEventCampaign::SendProgressInfoToUserOnWorking(User* const user) {
    // Current L2Server 0x005850fc.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    const int group = user->GetEventCampaignGoalGroup();
    EventCampaignProgressInfoPacket packet(*this, m_ProgressPacket[static_cast<std::size_t>(group)]);
    packet.Send(*user);
}

void CEventCampaign::SendProgressInfoToUserOnReward(User* const user) {
    // Current L2Server 0x005851ec.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const int goalGroupId = user->GetEventCampaignGoalGroup();
    CEventCampaignStepMultiInfo* const info = m_WorkingStep->m_MultiInfo[static_cast<std::size_t>(goalGroupId)];
    if (info == nullptr) {
        return;
    }

    CPacketHelperEx<20480>& progress = m_ProgressPacket[static_cast<std::size_t>(goalGroupId)];
    if (info->m_SuccessInfo == 0) {
        EventCampaignFailedPacket packet(*this, progress);
        packet.Send(*user);
    } else if (info->m_SuccessInfo == 1) {
        if (m_RewardUserMap.find(user->GetDBID()) == m_RewardUserMap.end()) {
            EventCampaignRewardReceivedPacket packet(*this, progress);
            packet.Send(*user);
        } else {
            EventCampaignRewardStartPacket packet(*this, progress);
            packet.Send(*user);
        }
    }
}

void CEventCampaign::SendProgressInfoToUserOnFailed(User* const user) {
    // Current L2Server 0x005853bc.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    const int goalGroupId = user->GetEventCampaignGoalGroup();
    EventCampaignFailedPacket packet(*this, m_ProgressPacket[static_cast<std::size_t>(goalGroupId)]);
    packet.Send(*user);
}

void CEventCampaign::SendRankingInfoToUser(User* const user) {
    // Current L2Server 0x005854ac.
    L2_GUARD_FUNCTION();
    if (m_EliteUserPacket.GetLength() > 0) {
        EventCampaignRankingInfoPacket packet(*this, m_EliteUserPacket);
        packet.Send(*user);
    }
}

EventCampaignPacket::EventCampaignPacket(CEventCampaign& campaign) noexcept : m_Campaign(&campaign), m_Buffer(nullptr) {}

EventCampaignPacket::~EventCampaignPacket() {
    L2_GUARD_FUNCTION();
    if (m_Buffer != nullptr) {
        m_Buffer->Release();
        m_Buffer = nullptr;
    }
}

void EventCampaignPacket::Send(User& user) {
    L2_GUARD_FUNCTION();
    if (m_Buffer != nullptr) {
        m_Buffer->AddRef();
        user.GetSocket()->AsyncSendConstBuffer(m_Buffer);
    }
}

void EventCampaignPacket::MakeConstBuffer(const char* format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    m_Buffer = User::MakeConstBuffer(format, arguments);
    va_end(arguments);
}

EventCampaignSuggestPacket::EventCampaignSuggestPacket(CEventCampaign& campaign) : EventCampaignPacket(campaign) {
    L2_GUARD_FUNCTION();
    MakeConstBuffer(
        "chcdd", 0xfe, 0x13b, 0, campaign.GetID(), campaign.m_WorkingStep != nullptr ? campaign.m_WorkingStep->GetId() : 0
    );
}

EventCampaignFinishPacket::EventCampaignFinishPacket(CEventCampaign& campaign) : EventCampaignPacket(campaign) {
    L2_GUARD_FUNCTION();
    if (campaign.m_WorkingStep != nullptr) {
        MakeConstBuffer("chcdd", 0xfe, 0x13b, 1, campaign.GetID(), campaign.m_WorkingStep->GetId());
    }
}

EventCampaignProgressInfoPacket::EventCampaignProgressInfoPacket(CEventCampaign& campaign, CPacketHelperEx<20480>& packet) :
    EventCampaignPacket(campaign), m_Packet(&packet) {
    L2_GUARD_FUNCTION();
    MakeConstBuffer("chccb", 0xfe, 0x13b, 3, 0, packet.GetLength(), packet.GetPacket());
}

EventCampaignRewardStartPacket::EventCampaignRewardStartPacket(CEventCampaign& campaign, CPacketHelperEx<20480>& packet) :
    EventCampaignPacket(campaign), m_Packet(&packet) {
    // Current L2Server 0x00583dac.
    L2_GUARD_FUNCTION();
    MakeConstBuffer("chccb", 0xfe, 0x13b, 3, 1, packet.GetLength(), packet.GetPacket());
}

EventCampaignRewardReceivedPacket::EventCampaignRewardReceivedPacket(
    CEventCampaign& campaign, CPacketHelperEx<20480>& packet
) :
    EventCampaignPacket(campaign), m_Packet(&packet) {
    // Current L2Server 0x00583ea4.
    L2_GUARD_FUNCTION();
    MakeConstBuffer("chccb", 0xfe, 0x13b, 3, 2, packet.GetLength(), packet.GetPacket());
}

EventCampaignFailedPacket::EventCampaignFailedPacket(CEventCampaign& campaign, CPacketHelperEx<20480>& packet) :
    EventCampaignPacket(campaign), m_Packet(&packet) {
    // Current L2Server 0x00583f9c.
    L2_GUARD_FUNCTION();
    MakeConstBuffer("chccb", 0xfe, 0x13b, 3, 3, packet.GetLength(), packet.GetPacket());
}

EventCampaignRankingInfoPacket::EventCampaignRankingInfoPacket(CEventCampaign& campaign, CPacketHelperEx<20480>& packet) :
    EventCampaignPacket(campaign), m_Packet(&packet) {
    // Current L2Server 0x00584090.
    L2_GUARD_FUNCTION();
    MakeConstBuffer("chcb", 0xfe, 0x13b, 4, packet.GetLength(), packet.GetPacket());
}

CEventCampaignManager::CEventCampaignManager() :
    m_OnGoingEventCampaign(nullptr), m_LoadFinished(0) {
    L2_GUARD_FUNCTION();
}

CEventCampaignManager::~CEventCampaignManager() = default;

L2_DEFINE_OBJECT_CLASS_ID(CEventCampaignManager)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CEventCampaignManager)

L2_DEFINE_OBJECT_NEW(CEventCampaignManager)

L2_DEFINE_OBJECT_ALLOCATOR(CEventCampaignManager, __GetClassName())

L2_DEFINE_OBJECT_RELEASE(CEventCampaignManager)

CEventCampaignManager* CEventCampaignManager::Inst() {
    static CEventCampaignManager* const instance = new CEventCampaignManager;
    return instance;
}

void CEventCampaignManager::Load() {
    // Current L2Server 0x00581da4 uses an interlocked exchange before
    // publishing the first five-second timer.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(reinterpret_cast<volatile std::int32_t*>(&m_LoadFinished), 1);
    AddTimer(5000, 0);
}

bool CEventCampaignManager::AddEventCampaign(CEventCampaign* const campaign) {
    L2_GUARD_FUNCTION();
    return m_EventCamapigns.emplace(campaign->GetID(), campaign).second;
}

void CEventCampaignManager::Init() {
    // Current L2Server 0x0058846c.
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\eventcampaign.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/eventcampaign.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[EventCampaign][%s][%d] Read from eventcampaign file failed",
        //     L"EventCampaign.cpp",
        //     0x9c8)
        // L2_LOG_ERROR(Log,
        //     L"[EventCampaign][%s][%d] Read from eventcampaign file failed",
        //     L"EventCampaign.cpp",
        //     0x9c8)
        L2_LOG_ERROR(Log, "Cannot read eventcampaign.txt; campaign initialization stops before parsing.");
        return;
    }

    CParserForEventCampaign parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[EventCampaign][%s][%d] CParserForEventQuest.create failed",
        //     L"EventCampaign.cpp",
        //     0x9d1)
        // L2_LOG_ERROR(Log,
        //     L"[EventCampaign][%s][%d] CParserForEventQuest.create failed",
        //     L"EventCampaign.cpp",
        //     0x9d1)
        L2_LOG_ERROR(Log, "Cannot create the eventcampaign.txt parser; campaign initialization stops before parsing.");
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() == 0) {
        for (const auto& entry : m_EventCamapigns) {
            const std::size_t insertionIndex =
                static_cast<std::size_t>(::genrand64_real2() * static_cast<double>(m_OnSleepingEventCampaigns.size() + 1));
            m_OnSleepingEventCampaigns.insert(
                m_OnSleepingEventCampaigns.begin() + static_cast<std::ptrdiff_t>(insertionIndex), entry.second
            );
        }
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[EventCampaign][%s][%d] Parsing eventcampaign file failed after line[%d]",
    //     L"EventCampaign.cpp",
    //     0x9de,
    //     parser.m_lexer.line)
    // L2_LOG_ERROR(Log,
    //     L"[EventCampaign][%s][%d] Parsing eventcampaign file failed after line[%d]",
    //     L"EventCampaign.cpp",
    //     0x9de,
    //     parser.m_lexer.line)
    L2_LOG_ERROR(
        Log,
        "eventcampaign.txt: parsing failed after line %lld; %llu registered campaign definitions are retained, but this "
        "load skips scheduling them.",
        static_cast<long long>(parser.m_lexer.line), static_cast<unsigned long long>(m_EventCamapigns.size())
    );
}

bool CEventCampaignManager::UserPacketHandler(CSocket* const baseSocket, const unsigned char* const packet) {
    // Current L2Server 0x00588720.
    L2_GUARD_FUNCTION();
    CEventCampaignManager* const manager = Inst();
    if (manager->m_LoadFinished == 1) {
        auto* const socket = static_cast<CUserSocket*>(baseSocket);
        User* const user = socket != nullptr ? socket->GetUser() : nullptr;
        if (user == nullptr) {
            return true;
        }

        std::int8_t action = 0;
        std::int32_t campaignId = 0;
        std::int32_t stepId = 0;
        ::Disassemble(packet, "cdd", &action, &campaignId, &stepId);
        if (action == 2) {
            manager->OnClientRequestHTML(*user, campaignId, stepId);
        } else if (action == 3) {
            manager->OnRequestProgressInfo(*user, campaignId, stepId);
        }
    }
    return false;
}

bool CEventCampaignManager::IsWorkingEventCampaignOnlyWorkingState(const int campaignId) const {
    // Current L2Server 0x00581e58.
    L2_GUARD_FUNCTION();
    return m_OnGoingEventCampaign != nullptr && campaignId == m_OnGoingEventCampaign->m_ID &&
        m_OnGoingEventCampaign->m_WorkingStep != nullptr &&
        m_OnGoingEventCampaign->m_State == CEventCampaign::PERIOD_WORKING;
}

bool CEventCampaignManager::IsWorkingEventCampaign(const int campaignId, const int stepId) const {
    // Current L2Server 0x00581f18.
    L2_GUARD_FUNCTION();
    return m_OnGoingEventCampaign != nullptr && campaignId == m_OnGoingEventCampaign->m_ID &&
        m_OnGoingEventCampaign->m_WorkingStep != nullptr && stepId == m_OnGoingEventCampaign->m_WorkingStep->m_StepID;
}

bool CEventCampaignManager::IsWorkingEventCampaign(const int campaignId, const int stepId, const int goalGroupId) const {
    // Current L2Server 0x00581fdc.
    L2_GUARD_FUNCTION();
    return m_OnGoingEventCampaign != nullptr && campaignId == m_OnGoingEventCampaign->m_ID &&
        m_OnGoingEventCampaign->m_WorkingStep != nullptr && stepId == m_OnGoingEventCampaign->m_WorkingStep->m_StepID &&
        goalGroupId >= 0 && goalGroupId < 5 &&
        m_OnGoingEventCampaign->m_WorkingStep->m_MultiInfo[static_cast<std::size_t>(goalGroupId)] != nullptr;
}

void CEventCampaignManager::InitNPCMaker() {
    // Current L2Server 0x005843b8.
    L2_GUARD_FUNCTION();
    if (m_LoadFinished == 1 && m_OnGoingEventCampaign != nullptr) {
        m_OnGoingEventCampaign->InitNPCMaker();
    }
}

void CEventCampaignManager::OnAddEventCampaignPoint(
    User& user, const int campaignId, const int stepId, const int goalGroupId, const int goalId, const int point
) {
    // Current L2Server 0x0058c10c.
    L2_GUARD_FUNCTION();
    if (IsWorkingEventCampaign(campaignId, stepId, goalGroupId)) {
        m_OnGoingEventCampaign->TryAddPoint(user, campaignId, stepId, goalGroupId, goalId, point);
    }
}

void CEventCampaignManager::OnAcceptEventCampaign(
    User& user, const int campaignId, const int stepId, const int goalGroupId
) {
    // Current L2Server 0x0058ab8c.
    L2_GUARD_FUNCTION();
    if (IsWorkingEventCampaign(campaignId, stepId)) {
        m_OnGoingEventCampaign->TryJoinEventCampaign(user, goalGroupId);
    }
}

void CEventCampaignManager::OnClientRequestHTML(User& user, const int campaignId, const int stepId) {
    // Current L2Server 0x00586a30.
    L2_GUARD_FUNCTION();
    if (IsWorkingEventCampaign(campaignId, stepId)) {
        m_OnGoingEventCampaign->UserTryRequestCampaignHTML(user);
    }
}

void CEventCampaignManager::OnRequestProgressInfo(User& user, const int campaignId, const int stepId) {
    // Current L2Server 0x00586bbc.
    L2_GUARD_FUNCTION();
    if (IsWorkingEventCampaign(campaignId, stepId)) {
        m_OnGoingEventCampaign->TrySendProgressInfo(user);
    }
}

void CEventCampaignManager::OnRequestReward(User& user, const int campaignId, const int stepId, int) {
    // Current L2Server 0x0058ac6c. The goal-group argument is ignored by
    // the shipped body after the two-key campaign/step gate.
    L2_GUARD_FUNCTION();
    if (IsWorkingEventCampaign(campaignId, stepId)) {
        m_OnGoingEventCampaign->UserTryReceiveReward(user);
    }
}

void CEventCampaignManager::OnRequestRanking(User& user, const int campaignId, const int stepId) {
    // Current L2Server 0x00586c98.
    L2_GUARD_FUNCTION();
    if (IsWorkingEventCampaign(campaignId, stepId)) {
        m_OnGoingEventCampaign->UserTrySendRanking(user);
    }
}

int CEventCampaignManager::OnGoingEventCampaignId() {
    // Current L2Server 0x005820c4.
    L2_GUARD_FUNCTION();
    return m_OnGoingEventCampaign == nullptr ? -1 : m_OnGoingEventCampaign->m_ID;
}

int CEventCampaignManager::OnGoingEventCampaignStepId() {
    // Current L2Server 0x00582160.
    L2_GUARD_FUNCTION();
    return m_OnGoingEventCampaign == nullptr || m_OnGoingEventCampaign->m_WorkingStep == nullptr
        ? -1
        : m_OnGoingEventCampaign->m_WorkingStep->m_StepID;
}

int CEventCampaignManager::OnJoinEventCampaignGoalGroupId(User& user) {
    // Current L2Server 0x00585fec.
    L2_GUARD_FUNCTION();
    return m_OnGoingEventCampaign == nullptr ? -1 : m_OnGoingEventCampaign->FindUserGoalGroup(user);
}

void CEventCampaignManager::OnShowProgressInfo(User& user) {
    // Current L2Server 0x005860a0.
    L2_GUARD_FUNCTION();
    if (m_OnGoingEventCampaign != nullptr) {
        m_OnGoingEventCampaign->ShowGoalInfo(user);
    }
}

void CEventCampaignManager::OnStopEventCampaign() {
    // Current L2Server 0x0058a674.
    L2_GUARD_FUNCTION();
    if (m_OnGoingEventCampaign != nullptr) {
        m_OnGoingEventCampaign->TryStop();
        CheckExecutionEventCampaign(false, -1);
    }
}

void CEventCampaignManager::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    CheckExecutionEventCampaign(false, -1);
    AddTimer(900000, 0);
}

void CEventCampaignManager::OnUserEnterWorld(User& user) {
    L2_GUARD_FUNCTION();
    if (m_OnGoingEventCampaign != nullptr) {
        m_OnGoingEventCampaign->UserEnterWorld(user);
    }
}

void CEventCampaignManager::CheckExecutionEventCampaign(const bool force, const int campaignId) {
    L2_GUARD_FUNCTION();
    if (m_OnGoingEventCampaign == nullptr) {
        const std::int32_t currentTime = runtime::crt::Time32Now();
        for (auto position = m_OnSleepingEventCampaigns.begin(); position != m_OnSleepingEventCampaigns.end(); ++position) {
            CEventCampaign* const campaign = *position;
            if (!((force && campaign->GetID() == campaignId) || (!force && campaign->CanExcute(currentTime)))) {
                continue;
            }

            m_OnGoingEventCampaign = campaign;
            campaign->TryStart();
            m_OnSleepingEventCampaigns.erase(position);
            break;
        }
        return;
    }

    if (m_OnGoingEventCampaign->GetState() == CEventCampaign::PERIOD_SLEEP) {
        m_OnGoingEventCampaign->AllDataInit();
        const std::size_t insertionIndex =
            static_cast<std::size_t>(::genrand64_real2() * static_cast<double>(m_OnSleepingEventCampaigns.size() + 1));
        m_OnSleepingEventCampaigns.insert(
            m_OnSleepingEventCampaigns.begin() + static_cast<std::ptrdiff_t>(insertionIndex), m_OnGoingEventCampaign
        );
        m_OnGoingEventCampaign = nullptr;
    }
}
