#include "DynamicQuest.h"
#include "DynamicQuestParser.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Shared/src/MemorySub.h"

#include "Area.h"
#include "AtomicJobServerSide.h"
#include "DB.h"
#include "Default.h"
#include "Door.h"
#include "IOCPMessageServer.h"
#include "Item.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "Party.h"
#include "Residence.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Config.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <iterator>

namespace {

void NoOp(DynamicQuestInfo&) {}

void NoOp(DynamicQuestInfo&, User&) {}

void NoOp(DynamicQuestInfo&, CCampaign*) {}

void NoOp(DynamicQuestInfo&, const DynamicQuestInfo::PointParam&) {}

template <typename TValue>
TValue ReadExecutionArgument(const CExecutionArgumentPad* argument, std::size_t offset) {
    TValue value{};
    std::memcpy(&value, argument->Buffer + offset, sizeof(value));
    return value;
}


} // namespace

DynamicQuestSetting::AutoExecution::AutoExecution() : m_pPeriod(nullptr) {}

DynamicQuestSetting::AutoExecution::~AutoExecution() {
    delete m_pPeriod;
    m_pPeriod = nullptr;
}

bool DynamicQuestSetting::AutoExecution::CanExecute(DynamicQuestInfo& info, std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    if (m_pPeriod != nullptr && !m_pPeriod->CanExecute(currentTime)) {
        return false;
    }
    return CanExecuteByCastle() && CanExecuteImpl(info, currentTime);
}

bool DynamicQuestSetting::AutoExecution::CanExecuteByCastle() {
    L2_GUARD_FUNCTION();
    if (m_CastleCond.m_nCastleId <= 0) {
        return true;
    }
    CResidence* const residence = g_ResidenceDB.GetResidence(m_CastleCond.m_nCastleId);
    return residence != nullptr && residence->m_data != nullptr &&
        residence->m_data->m_ResidenceType == m_CastleCond.m_nCastleType;
}

bool DynamicQuestSetting::ManualExecution::CanExecuteImpl(DynamicQuestInfo&, std::int32_t) { return true; }

DynamicQuestSetting::AutoExecutionByTimer::AutoExecutionByTimer() :
    m_ExecuteDayList(0), m_alignment1cTo20{} {}

DynamicQuestSetting::AutoExecutionByTimer::~AutoExecutionByTimer() { m_ExecuteDayList = 0; }

bool DynamicQuestSetting::AutoExecutionByTimer::CanExecuteImpl(DynamicQuestInfo& info, std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    const std::time_t hostTime = static_cast<std::time_t>(currentTime);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &hostTime);
#else
    localtime_r(&hostTime, &localTime);
#endif

    const unsigned int weekDay = static_cast<unsigned int>(localTime.tm_wday) & 31U;
    if ((static_cast<unsigned int>(m_ExecuteDayList) >> weekDay & 1U) == 0U) {
        return false;
    }

    const int minuteOfDay = localTime.tm_hour * 60 + localTime.tm_min;
    for (const StartTimeData& start : m_StartTimeData) {
        if (minuteOfDay < start.startTime || minuteOfDay > start.startTime + 20 ||
            info.IsAlreadyExecuted(currentTime, m_ExecuteDayList, start.startTime)) {
            continue;
        }

        DynamicQuestManager* const manager = DynamicQuestManager::Inst();
        double probability = start.dice;
        double randomNo = 0.0;
        if (manager->m_nLastPickStartTime == start.startTime) {
            probability += manager->m_nLastPickPrevProb;
            randomNo = manager->m_nLastPickRandomNo;
        } else {
            randomNo = genrand64_real1() * 100.0;
        }

        if (randomNo <= probability) {
            manager->m_nLastPickStartTime = 0;
            manager->m_nLastPickRandomNo = 0.0;
            manager->m_nLastPickPrevProb = 0.0;
            return true;
        }
        manager->SetPickInformation(start.startTime, randomNo, probability);
    }
    return false;
}

DynamicQuest::DynamicQuest(int id) :
    m_Id(id),
    m_alignment0cTo10{},
    m_pCampaignData(nullptr),
    m_pExecutionSetting(nullptr),
    m_TopUserCount(0),
    m_alignment34To38{},
    m_QuestType(DynamicQuestType::NORMAL_QUEST),
    m_nRelatedCastleID(0) {}

DynamicQuest::~DynamicQuest() {
    L2_GUARD_FUNCTION();
    delete m_pCampaignData;
    m_pCampaignData = nullptr;
    delete m_pExecutionSetting;
    m_pExecutionSetting = nullptr;
    for (QuestCondition* const condition : m_StartConditionList) {
        delete condition;
    }
    m_StartConditionList.clear();
}

DynamicContent::DynamicContent(int id) : DynamicQuest(id) { m_Setting = DynamicQuestManager::Inst()->DefaultSetting[0]; }

DynamicContent::~DynamicContent() = default;

DynamicQuestType::Enum DynamicContent::GetType() const { return DynamicQuestType::DYNAMIC_CONTENTS; }

int DynamicContent::GetEliteCount1() const {
    return m_Setting.ELITE_COUNT1 != 0 ? static_cast<int>(m_Setting.ELITE_COUNT1)
                                       : static_cast<int>(DynamicQuestManager::Inst()->DefaultSetting[0].ELITE_COUNT1);
}

int DynamicContent::GetEliteCount2() const {
    return m_Setting.ELITE_COUNT2 != 0 ? static_cast<int>(m_Setting.ELITE_COUNT2)
                                       : static_cast<int>(DynamicQuestManager::Inst()->DefaultSetting[0].ELITE_COUNT2);
}

int DynamicContent::GetRewardCount1() const {
    return m_Setting.REWARD_COUNT1 != 0 ? static_cast<int>(m_Setting.REWARD_COUNT1)
                                        : static_cast<int>(DynamicQuestManager::Inst()->DefaultSetting[0].REWARD_COUNT1);
}

bool DynamicContent::CanExecutedByUser(User&) { return true; }

bool DynamicContent::CanExecutedByCondition(DynamicQuestInfo& info, std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    return m_pExecutionSetting != nullptr && m_pExecutionSetting->CanExecute(info, currentTime);
}

bool DynamicContent::IsInRange(User&) { return true; }

void DynamicContent::AddRange(CTerritory*) {}

ZoneQuest::ZoneQuest(int id) : DynamicQuest(id) {
    m_Setting = DynamicQuestManager::Inst()->DefaultSetting[1];
}

ZoneQuest::~ZoneQuest() {
    // Current L2Server.exe 0x007027dc.
    L2_GUARD_FUNCTION();
    m_TerritoryList.clear();
}

DynamicQuestType::Enum ZoneQuest::GetType() const { return DynamicQuestType::ZONE_QUEST; }

int ZoneQuest::GetEliteCount1() const {
    return m_Setting.ELITE_COUNT1 != 0 ? static_cast<int>(m_Setting.ELITE_COUNT1)
                                       : static_cast<int>(DynamicQuestManager::Inst()->DefaultSetting[1].ELITE_COUNT1);
}

int ZoneQuest::GetEliteCount2() const {
    return m_Setting.ELITE_COUNT2 != 0 ? static_cast<int>(m_Setting.ELITE_COUNT2)
                                       : static_cast<int>(DynamicQuestManager::Inst()->DefaultSetting[1].ELITE_COUNT2);
}

int ZoneQuest::GetRewardCount1() const {
    return m_Setting.REWARD_COUNT1 != 0 ? static_cast<int>(m_Setting.REWARD_COUNT1)
                                        : static_cast<int>(DynamicQuestManager::Inst()->DefaultSetting[1].REWARD_COUNT1);
}

bool ZoneQuest::CanExecutedByUser(User& user) {
    L2_GUARD_FUNCTION();
    CCampaign* const campaign = GetFirstCampaign();
    return campaign != nullptr && campaign->CanStartQuest(user);
}

bool ZoneQuest::CanExecutedByCondition(DynamicQuestInfo&, std::int32_t) { return true; }

bool ZoneQuest::IsInRange(User& user) {
    L2_GUARD_FUNCTION();
    if (user.GetInZoneID() != 0) {
        return false;
    }
    const FVector position = user.GetPos();
    for (CTerritory* const territory : m_TerritoryList) {
        if (territory != nullptr && territory->IsInsideRough(position, true)) {
            return true;
        }
    }
    return false;
}

void ZoneQuest::AddRange(CTerritory* territory) {
    L2_GUARD_FUNCTION();
    m_TerritoryList.push_back(territory);
}

void CCampaignDataOneTime::PostLoad() {}

CCampaign* CCampaignDataOneTime::GetNextCampaignOnFailed(const CCampaign&) { return nullptr; }

AtomicJobList* QuestOperation_ItemOnAcceptDynamicQuest::MakeAtomicJobList(User& user, CQuestBase& quest) {
    L2_GUARD_FUNCTION();
    const int userId = static_cast<int>(user.Id());
    auto* const result = new AtomicJobList(m_ItemList.empty() ? 1 : 2, userId);
    if (!m_ItemList.empty()) {
        result->AddAtomicJob(new CAtomicItemJobCheck(userId));
    }
    result->AddAtomicJob(new CAtomicQuestAccept(userId, &quest));
    return result;
}

void QuestOperation_ItemOnAcceptDynamicQuest::LogOnSuccess(User&, ItemManip (&)[100]) { L2_GUARD_FUNCTION(); }

AtomicJobList* QuestOperation_ItemOnFinishDynamicQuest::MakeAtomicJobList(User& user, CQuestBase& quest) {
    L2_GUARD_FUNCTION();
    const int userId = static_cast<int>(user.Id());
    auto* const result = new AtomicJobList(m_ItemList.empty() ? 1 : 2, userId);
    if (!m_ItemList.empty()) {
        result->AddAtomicJob(new CAtomicItemJobCheck(userId));
    }
    result->AddAtomicJob(new CAtomicQuestFinish(userId, &quest));
    return result;
}

void QuestOperation_ItemOnFinishDynamicQuest::LogOnSuccess(User&, ItemManip (&)[100]) { L2_GUARD_FUNCTION(); }

void QuestOperation_ItemOnFinishDynamicQuest::Do(User& user, CQuestBase& quest) {
    L2_GUARD_FUNCTION();

    AtomicJobList* const job = MakeAtomicJobList(user, quest);
    const int requestCount = static_cast<int>(m_ItemList.size());
    if (requestCount < 1) {
        if (job->CanDo()) {
            job->Do();
        }
        job->Destroy();
        return;
    }

    const std::int32_t adjustedRate = quest.GetAdjustedRewardRate();
    const std::int32_t rateWithBase = runtime::AddInt32Wrapping(adjustedRate, 100);
    const std::int64_t multiplier = rateWithBase / 100;

    ItemManip requests[100]{};
    for (int index = 0; index < requestCount; ++index) {
        const ItemDataType& source = m_ItemList[static_cast<std::size_t>(index)];
        const std::int64_t scaledCount = runtime::MultiplyInt64Wrapping(source.itemCount, multiplier);
        const std::int64_t adjustedCount = runtime::Win64CvttDoubleToInt64(static_cast<double>(scaledCount) + 0.5);
        int enchant = source.itemEnchant;
        if (enchant == -1) {
            CObject* const object = g_ObjectDB.GetObjectW(static_cast<std::int32_t>(source.itemClassId), false);
            CItem* const item = object != nullptr && object->IsItem() ? object->CastItem() : nullptr;
            if (item != nullptr && item->m_data != nullptr) {
                enchant = item->m_data->GetEnchanted();
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Item[%d] is not exist.",
                //     L"DynamicQuest.cpp",
                //     0xcc,
                //     source.itemClassId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Item[%d] is not exist.",
                //     L"DynamicQuest.cpp",
                //     0xcc,
                //     source.itemClassId)
                L2_LOG_ERROR(
                    Log,
                    "Dynamic quest %d, character DBID %u: cannot resolve default enchant for reward item class %u because "
                    "no usable item prototype exists; the request for %lld items retains enchant -1.",
                    quest.GetId(), static_cast<unsigned int>(user.GetDBID()), source.itemClassId,
                    static_cast<long long>(adjustedCount)
                );
            }
        }
        requests[index] = ItemManip(quest.GetId(), static_cast<int>(source.itemClassId), adjustedCount, enchant, 0, 0, true);
    }

    L2_GUARD(user.m_ManipulateQueueLock);
    CPacketHelperEx<20480> packet;
    User::etcMsg message = user.SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = user.SaveManipulateQueue(
        ManipulateType::MANI_QUEST, quest.GetId(), requestCount, requests, job, 0, packet, false,
        ItemGetReason::DQuestReward, message
    );

    const User::ManiItemResultType result = user._SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        if (user._HandleItemManipulation(queue)) {
            LogOnSuccess(user, requests);
        } else {
            job->Destroy();
        }
        if (user.GetTrade() != nullptr) {
            user.TradeCancel(false);
        }
        return;
    }
    if (result == User::MRT_JOBMAP) {
        job->Destroy();
        user.SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        if (user.GetTrade() != nullptr) {
            user.TradeCancel(false);
        }
        return;
    }
    if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"DynamicQuest.cpp",
        //     0x104)
        // Noise: MRT_JOBNOTMAP means the reward job was queued behind existing work. It is an ordinary asynchronous outcome,
        // not a completed reward or an error. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"DynamicQuest.cpp",
        //     0x104)
        LogOnSuccess(user, requests);
        return;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"DynamicQuest.cpp",
    //     0x10a)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"DynamicQuest.cpp",
    //     0x10a)
    L2_LOG_ERROR(
        Log,
        "Dynamic quest %d, character DBID %u: reward-item queue returned unexpected result %d for %d entries; the job is "
        "destroyed and an active trade is canceled.",
        quest.GetId(), static_cast<unsigned int>(user.GetDBID()), static_cast<int>(result), requestCount
    );
    job->Destroy();
    if (user.GetTrade() != nullptr) {
        user.TradeCancel(false);
    }
}

CampaignPacket::CampaignPacket(CCampaign& campaign) noexcept : campaign(campaign), m_pBuffer(nullptr) {}

CampaignPacket::~CampaignPacket() {
    L2_GUARD_FUNCTION();
    if (m_pBuffer != nullptr) {
        m_pBuffer->Release();
        m_pBuffer = nullptr;
    }
}

void CampaignPacket::Send(User& user) {
    L2_GUARD_FUNCTION();
    if (m_pBuffer != nullptr) {
        m_pBuffer->AddRef();
        user.GetSocket()->AsyncSendConstBuffer(m_pBuffer);
    }
}

void CampaignPacket::MakeConstBuffer(const char* format, ...) {
    L2_GUARD_FUNCTION();
    va_list arguments;
    va_start(arguments, format);
    m_pBuffer = User::MakeConstBuffer(format, arguments);
    va_end(arguments);
}

CampaignSuggestPacket::CampaignSuggestPacket(CCampaign& campaign) : CampaignPacket(campaign) {
    L2_GUARD_FUNCTION();
    DynamicQuest* const dynamicQuest = campaign.GetDynamicQuest();
    MakeConstBuffer(
        "chccdd", 0xfe, 0xe9, static_cast<int>(dynamicQuest->GetType()), 0, dynamicQuest->GetId(), campaign.GetId()
    );
}

CampaignFinishedPacket::CampaignFinishedPacket(CCampaign& campaign) : CampaignPacket(campaign) {
    L2_GUARD_FUNCTION();
    DynamicQuest* const dynamicQuest = campaign.GetDynamicQuest();
    if (dynamicQuest != nullptr) {
        MakeConstBuffer(
            "chccdd", 0xfe, 0xe9, static_cast<int>(dynamicQuest->GetType()), 1, dynamicQuest->GetId(), campaign.GetId()
        );
    }
}

CampaignHelperPacket::CampaignHelperPacket(CCampaign& campaign, CPacketHelperEx<20480>& packet) :
    CampaignPacket(campaign), m_Packet(packet) {
    L2_GUARD_FUNCTION();
    MakeConstBuffer("chb", 0xfe, 0xe9, m_Packet.GetLength(), m_Packet.GetPacket());
}

CCampaign::CCampaign(int id, int duration) :
    CCampaignBase(id),
    m_pConditionInven_Elite1(nullptr),
    m_pConditionInven_Elite2(nullptr),
    m_pConditionInven_Basic(nullptr),
    m_pFinishOperation_Item_Elite1(nullptr),
    m_pFinishOperation_Item_Elite2(nullptr),
    m_pFinishOperation_Item_Basic(nullptr),
    m_pCampaignData(nullptr),
    m_pPrevCampaign(nullptr),
    m_pNextCampaign(nullptr),
    m_GoalCount(0),
    m_AdjustedGoalRate(0),
    m_AdjustedRewardRate(0),
    m_Duration(duration),
    m_pProgressInfo(nullptr) {
    m_pAcceptOperation_Item = new QuestOperation_ItemOnAcceptDynamicQuest;
    m_pFinishOperation_Item1 = new QuestOperation_ItemOnFinishDynamicQuest;
    m_pFinishOperation_Item2 = new QuestOperation_ItemOnFinishDynamicQuest;
    m_pFinishOperation_Item_Elite1 = new QuestOperation_ItemOnFinishDynamicQuest;
    m_pFinishOperation_Item_Elite2 = new QuestOperation_ItemOnFinishDynamicQuest;
    m_pFinishOperation_Item_Basic = new QuestOperation_ItemOnFinishDynamicQuest;
}

CCampaignBase::~CCampaignBase() {
    L2_GUARD_FUNCTION();

    for (QuestOperation* const operation : m_StartOperationList) {
        delete operation;
    }
    m_StartOperationList.clear();

    for (QuestOperation* const operation : m_AcceptOperationList) {
        delete operation;
    }
    m_AcceptOperationList.clear();

    for (QuestOperation* const operation : m_FinishOperationList) {
        delete operation;
    }
    m_FinishOperationList.clear();

    // Current L2Server clears this list without deleting its
    // pointed-to conditions before CQuestBase::~CQuestBase sees it.
    m_StartConditionList.clear();

    for (QuestCondition* const condition : m_AcceptConditionList) {
        delete condition;
    }
    m_AcceptConditionList.clear();

    for (QuestCondition* const condition : m_FinishConditionList) {
        delete condition;
    }
    m_FinishConditionList.clear();

    delete m_pAcceptOperation_Item;
    delete m_pFinishOperation_Item1;
    delete m_pFinishOperation_Item2;
}

CCampaign::~CCampaign() {
    // Current L2Server.exe 0x0070b554.
    L2_GUARD_FUNCTION();

    delete m_pAcceptOperation_Item;
    delete m_pFinishOperation_Item1;
    delete m_pFinishOperation_Item2;
    delete m_pFinishOperation_Item_Elite1;
    delete m_pFinishOperation_Item_Elite2;
    delete m_pFinishOperation_Item_Basic;

    m_pAcceptOperation_Item = nullptr;
    m_pFinishOperation_Item1 = nullptr;
    m_pFinishOperation_Item2 = nullptr;
    m_pConditionInven_Elite1 = nullptr;
    m_pConditionInven_Elite2 = nullptr;
    m_pConditionInven_Basic = nullptr;
    m_pFinishOperation_Item_Elite1 = nullptr;
    m_pFinishOperation_Item_Elite2 = nullptr;
    m_pFinishOperation_Item_Basic = nullptr;
    m_pCampaignData = nullptr;
    m_pPrevCampaign = nullptr;
    m_pNextCampaign = nullptr;
    m_pProgressInfo = nullptr;

    m_FailedOperation.clear();
    m_InquiryOperation.clear();
    m_SuggestRewardOperation.clear();
    m_SuggestRewardOperation_Elite.clear();
    // Current leaves m_SuggestRewardOperation_Basic for its
    // ordinary member destructor instead of clearing it here.
    m_SuggestRewardOperation_Received.clear();
    m_MakerList.clear();
    m_OpenDoorList.clear();
    m_AreaOnList.clear();
    m_GoalMap.clear();
    m_AdjustedGoalRate = 0;
    m_AdjustedRewardRate = 0;
}

int CCampaign::GetOnetimeQuestId() const { return -1; }

int CCampaign::GetAdjustedRewardRate() { return m_AdjustedRewardRate; }

DynamicQuest* CCampaign::GetDynamicQuest() {
    L2_GUARD_FUNCTION();
    return m_pCampaignData->GetDynamicQuest();
}

CCampaign* CCampaign::GetNextCampaignOnSuccess() {
    L2_GUARD_FUNCTION();
    return m_pCampaignData->GetNextCampaignOnSuccess(*this);
}

CCampaign* CCampaign::GetNextCampaignOnFailed() {
    L2_GUARD_FUNCTION();
    return m_pCampaignData->GetNextCampaignOnFailed(*this);
}

void CCampaign::OnInquiry(User& user) {
    L2_GUARD_FUNCTION();
    DoOperationList(user, m_InquiryOperation, *this);
}

void CCampaign::OnRequestFailedHTML(User& user) {
    L2_GUARD_FUNCTION();
    DoOperationList(user, m_FailedOperation, *this);
}

void CCampaign::OnAcceptOperation(User& user) {
    L2_GUARD_FUNCTION();
    if (m_pProgressInfo != nullptr) {
        m_pProgressInfo->OnAcceptCampaign(user);
    }
}

void CCampaign::OnFinishOperation(User& user) {
    L2_GUARD_FUNCTION();
    if (m_pProgressInfo != nullptr) {
        m_pProgressInfo->OnFinishCampaign(user);
    }
}

void CCampaign::ActivateArea() {
    L2_GUARD_FUNCTION();
    for (CArea* const area : m_AreaOnList) {
        if (area != nullptr && !area->IsOn(0)) {
            area->SetOnOff(true);
        }
    }
}

void CCampaign::ActivateDoor() {
    L2_GUARD_FUNCTION();
    for (CDoor* const door : m_OpenDoorList) {
        if (door != nullptr && !door->IsOpened()) {
            door->Open(false, true);
        }
    }
}

void CCampaign::ActivateNPCMaker() {
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    for (const std::u16string& maker : m_MakerList) {
        packet.Assemble("S", maker.c_str());
    }
    g_npcserver.Send("cccdb", 0xab, 1, 1, packet.GetAssembledCount(), packet.GetLength(), packet.GetPacket());
}

void CCampaign::SendCampaignInfo() {
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    for (const std::u16string& maker : m_MakerList) {
        packet.Assemble("S", maker.c_str());
    }
    g_npcserver.Send(
        "cdddb", 0xb2, m_pProgressInfo->GetDQuestId(), m_Id, packet.GetAssembledCount(), packet.GetLength(),
        packet.GetPacket()
    );
}

void CCampaign::DeactivateArea() {
    L2_GUARD_FUNCTION();
    for (CArea* const area : m_AreaOnList) {
        if (area != nullptr) {
            area->SetOnOff(false);
        }
    }
}

void CCampaign::DeactivateDoor() {
    L2_GUARD_FUNCTION();
    for (CDoor* const door : m_OpenDoorList) {
        if (door != nullptr) {
            door->Close(false, true);
        }
    }
}

void CCampaign::DeactivateNPCMaker(bool success) {
    L2_GUARD_FUNCTION();
    CPacketHelperEx<20480> packet;
    for (const std::u16string& maker : m_MakerList) {
        packet.Assemble("S", maker.c_str());
    }
    g_npcserver.Send(
        "cccdb", 0xab, 0, static_cast<int>(success), packet.GetAssembledCount(), packet.GetLength(), packet.GetPacket()
    );
}

void CCampaign::ShowGoal(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    DynamicQuest* const dynamicQuest = GetDynamicQuest();
    if (socket == nullptr || dynamicQuest == nullptr) {
        return;
    }

    char16_t context[] = u"SYS";
    char16_t message[128]{};
    socket->SendSystemMessage(context, u"-- Dynamic Quest Goal --");
    FormatUtf16To(message, std::size(message), u"Quest ID : %d", dynamicQuest->GetId());
    socket->SendSystemMessage(context, message);
    FormatUtf16To(message, std::size(message), u"Campaign ID : %d", m_Id);
    socket->SendSystemMessage(context, message);
    for (const auto& goal : m_GoalMap) {
        FormatUtf16To(message, std::size(message), u"Goal ID : %d Amount : %d", static_cast<int>(goal.first), goal.second);
        socket->SendSystemMessage(context, message);
    }
}

void CCampaign::OnCompleted(bool success) {
    L2_GUARD_FUNCTION();
    DeactivateArea();
    DeactivateDoor();
    DeactivateNPCMaker(success);

    DynamicQuest* const dynamicQuest = GetDynamicQuest();
    CLogFormatter formatter;
    formatter.WriteLog2(
        dynamicQuest->IsCastleQuest() ? 0x1f99 : 0x1f90, nullptr, nullptr, dynamicQuest->GetId(), m_Id, success ? 1 : 0, 0,
        0, 0, 0
    );
}

QuestOperation_Item* CCampaign::_GetItemOperation(User& user) {
    L2_GUARD_FUNCTION();
    if (m_pProgressInfo->_IsEliteUser1(user)) {
        return m_pFinishOperation_Item_Elite1;
    }
    if (m_pProgressInfo->_IsEliteUser2(user)) {
        return m_pFinishOperation_Item_Elite2;
    }
    if (m_pProgressInfo->_IsRewardUser1(user)) {
        return m_pFinishOperation_Item1;
    }
    return m_pFinishOperation_Item2;
}

bool CCampaign::CanStart(User& user) {
    L2_GUARD_FUNCTION();
    return m_pProgressInfo == nullptr || !m_pProgressInfo->_IsParticipant(user);
}

bool CCampaign::CanAccept(User& user) {
    L2_GUARD_FUNCTION();
    return !m_pProgressInfo->_IsParticipant(user);
}

bool CCampaign::CanFinish(User& user) {
    L2_GUARD_FUNCTION();
    if (m_pProgressInfo == nullptr || !m_pProgressInfo->_CanReceiveReward(user)) {
        return false;
    }
    if (m_pProgressInfo->_IsEliteUser1(user)) {
        return m_pConditionInven_Elite1 != nullptr && m_pConditionInven_Elite1->IsValid(user, *this);
    }
    if (m_pProgressInfo->_IsEliteUser2(user)) {
        return m_pConditionInven_Elite2 != nullptr && m_pConditionInven_Elite2->IsValid(user, *this);
    }
    return true;
}

void CCampaign::TryFinish(User& user) {
    L2_GUARD_FUNCTION();
    if (!CanFinishQuest(user)) {
        return;
    }
    QuestOperation_Item* const operation = _GetItemOperation(user);
    if (operation == nullptr) {
        DoFinishOperation(user);
    } else {
        operation->Do(user, *this);
    }
}

void CCampaign::OnRequestReward(User& user) {
    L2_GUARD_FUNCTION();
    OperationList* operations = nullptr;
    if (m_pProgressInfo->_IsRewardReceived(user)) {
        operations = &m_SuggestRewardOperation_Received;
    } else if (m_pProgressInfo->_IsEliteUser1(user) || m_pProgressInfo->_IsEliteUser2(user)) {
        operations = &m_SuggestRewardOperation_Elite;
    } else if (m_pProgressInfo->_IsRewardUser1(user) || m_pProgressInfo->_CanReceiveReward(user)) {
        operations = &m_SuggestRewardOperation;
    }
    if (operations != nullptr) {
        DoOperationList(user, *operations, *this);
    }
}

int CCampaign::GetGoal(unsigned int goalId) {
    L2_GUARD_FUNCTION();
    const int original = m_GoalMap[goalId];
    const std::int32_t product = runtime::MultiplyInt32Wrapping(m_AdjustedGoalRate, original);
    const int adjusted =
        runtime::Win64CvttDoubleToInt32(static_cast<double>(original) + static_cast<double>(product) / 100.0);
    return adjusted < 1 ? m_GoalMap[goalId] : adjusted;
}

void CCampaign::OnStarted() {
    L2_GUARD_FUNCTION();
    ActivateArea();
    ActivateDoor();
    ActivateNPCMaker();

    int goalTotal = 0;
    for (const auto& goal : m_GoalMap) {
        goalTotal = runtime::AddInt32Wrapping(goalTotal, goal.second);
    }

    DynamicQuest* const dynamicQuest = GetDynamicQuest();
    if (m_pProgressInfo != nullptr) {
        SharedDynamicQuestInfo& shared = m_pProgressInfo->GetSharedInfo();
        shared.m_DynamicQuestID = dynamicQuest->GetId();
        shared.m_CampaignID = m_Id;
        shared.m_Progress = 0.0;
        shared.m_Goal = goalTotal;
        shared.m_Point = 0;

        DynamicQuestManager* const manager = DynamicQuestManager::Inst();
        if (manager->m_DynamicQuestIdExcludedFromAdjusting.find(dynamicQuest->GetId()) ==
            manager->m_DynamicQuestIdExcludedFromAdjusting.end()) {
            m_AdjustedGoalRate = manager->GetAdjustedGoalRate();
            m_AdjustedRewardRate = manager->GetAdjustedRewardRate();
        } else {
            m_AdjustedGoalRate = 0;
            m_AdjustedRewardRate = 0;
        }
    }

    CLogFormatter formatter;
    formatter.WriteLog2(
        dynamicQuest->IsCastleQuest() ? 0x1f98 : 0x1f8f, nullptr, nullptr, dynamicQuest->GetId(), m_Id, 0, 0, 0, 0, 0
    );
}

CCampaignData::CCampaignData() : m_pDynamicQuest(nullptr), m_pFirstCampaign(nullptr) {}

CCampaignData::~CCampaignData() {
    L2_GUARD_FUNCTION();
    m_pDynamicQuest = nullptr;
    CCampaign* campaign = m_pFirstCampaign;
    while (campaign != nullptr) {
        CCampaign* const next = campaign->m_pNextCampaign;
        delete campaign;
        campaign = next;
    }
    m_pFirstCampaign = nullptr;
}

bool CCampaignData::AddCampaign(CCampaign* campaign) {
    L2_GUARD_FUNCTION();
    campaign->m_pCampaignData = this;
    if (m_pFirstCampaign == nullptr) {
        m_pFirstCampaign = campaign;
        return true;
    }

    CCampaign* current = GetLastCampaign();
    while (current != nullptr) {
        if (current->GetId() == campaign->GetId()) {
            return false;
        }
        if (current->GetId() < campaign->GetId()) {
            campaign->m_pPrevCampaign = current;
            campaign->m_pNextCampaign = current->m_pNextCampaign;
            current->m_pNextCampaign = campaign;
            return true;
        }
        if (current->m_pNextCampaign == nullptr) {
            campaign->m_pPrevCampaign = current;
            current->m_pNextCampaign = campaign;
            return true;
        }
        current = current->m_pNextCampaign;
    }
    return true;
}

CCampaign* CCampaignData::GetLastCampaign() {
    L2_GUARD_FUNCTION();
    CCampaign* last = m_pFirstCampaign;
    if (last == nullptr) {
        return nullptr;
    }
    for (CCampaign* next = last->m_pNextCampaign; next != nullptr && last->GetId() <= next->GetId();
        next = next->m_pNextCampaign) {
        last = next;
    }
    return last;
}

CCampaign* CCampaignData::GetNextCampaignOnSuccess(const CCampaign& campaign) {
    L2_GUARD_FUNCTION();
    return campaign.m_pNextCampaign;
}

CCampaignDataCycle::~CCampaignDataCycle() {
    L2_GUARD_FUNCTION();
    m_pDynamicQuest = nullptr;
    CCampaign* const first = m_pFirstCampaign;
    if (first == nullptr) {
        return;
    }
    CCampaign* campaign = first->m_pNextCampaign;
    while (campaign != nullptr && campaign != first) {
        CCampaign* const next = campaign->m_pNextCampaign;
        delete campaign;
        campaign = next;
    }
    delete first;
    m_pFirstCampaign = nullptr;
}

void CCampaignDataCycle::PostLoad() {
    L2_GUARD_FUNCTION();
    if (CCampaign* const last = GetLastCampaign()) {
        last->m_pNextCampaign = m_pFirstCampaign;
    }
}

CCampaign* CCampaignDataCycle::GetNextCampaignOnFailed(const CCampaign& campaign) {
    L2_GUARD_FUNCTION();
    return campaign.m_pPrevCampaign != nullptr ? campaign.m_pPrevCampaign : m_pFirstCampaign;
}

CCampaign* DynamicQuest::GetFirstCampaign() {
    return m_pCampaignData != nullptr ? m_pCampaignData->GetFirstCampaign() : nullptr;
}

CCampaign* DynamicQuest::GetLastCampaign() {
    return m_pCampaignData != nullptr ? m_pCampaignData->GetLastCampaign() : nullptr;
}

void DynamicQuest::AddCampaignData(CCampaignData* campaignData) {
    L2_GUARD_FUNCTION();
    if (campaignData->GetFirstCampaign() == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] No campaign in dynamic quest",
        //     L"DynamicQuest.cpp",
        //     0x49b,
        //     m_Id)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] No campaign in dynamic quest",
        //     L"DynamicQuest.cpp",
        //     0x49b,
        //     m_Id)
        L2_LOG_ERROR(
            Log, "Dynamic quest %d: campaign data has no first campaign; the supplied campaign data is not attached.", m_Id
        );
        return;
    }
    campaignData->SetDynamicQuest(this);
    m_pCampaignData = campaignData;
    campaignData->PostLoad();
}

bool DynamicQuest::IsValid(CQuestBase& quest, const ConditionList& conditions, User& user) {
    L2_GUARD_FUNCTION();
    for (QuestCondition* const condition : conditions) {
        if (!condition->IsValid(user, quest)) {
            return false;
        }
    }
    return true;
}

// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     L2ServerMemory::kDynamicQuestInfoClassId ==
//     DynamicQuestInfo::kMemoryClassId)
// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     L2ServerMemory::kDynamicContentInfoClassId ==
//     DynamicContentInfo::kMemoryClassId)
// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     L2ServerMemory::kZoneQuestInfoClassId ==
//     ZoneQuestInfo::kMemoryClassId)

L2_DEFINE_OBJECT_CLASS_ID(DynamicQuestInfo)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(DynamicQuestInfo)

L2_DEFINE_OBJECT_ALLOCATOR(DynamicQuestInfo, __GetClassName())

L2_DEFINE_OBJECT_NEW(DynamicQuestInfo)

DynamicQuestInfo::DynamicQuestInfo(DynamicQuest* dynamicQuest) :
    s(nullptr),
    m_CampaignStartTime(0),
    m_CampaignWorkingEndTime(0),
    m_DQuestStartTime(0),
    m_alignment2cTo30{},
    m_pStateHandler(DynamicQuestState::Handler[PERIOD_SLEEP]),
    m_nServerUserCount(0),
    m_nAdjustedRewardRate(100),
    m_pCampaign(nullptr),
    m_pDynamicQuest(dynamicQuest),
    m_LastExecutedTime(0),
    m_alignment94To98{},
    m_ProgressPacket(),
    m_ScoreBoardPacket() {
    _ClearResultSummary();
}

DynamicQuestInfo::~DynamicQuestInfo() {
    L2_GUARD_FUNCTION();
    m_pStateHandler = nullptr;
    m_pCampaign = nullptr;
    m_pDynamicQuest = nullptr;
    m_CampaignStartTime = 0;
    m_CampaignWorkingEndTime = 0;
    m_DQuestStartTime = 0;
    if (s != nullptr) {
        L2_RELEASE(s, ObjectRefType::CreateDelete);
        s = nullptr;
    }
    _ClearResultSummary();
}

L2_DEFINE_OBJECT_RELEASE(DynamicQuestInfo)

L2_DEFINE_OBJECT_CLASS_ID(DynamicContentInfo)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(DynamicContentInfo)

L2_DEFINE_OBJECT_ALLOCATOR(DynamicContentInfo, __GetClassName())

L2_DEFINE_OBJECT_NEW(DynamicContentInfo)

DynamicContentInfo::DynamicContentInfo(DynamicQuest* dynamicQuest) : DynamicQuestInfo(dynamicQuest) {}

L2_DEFINE_OBJECT_RELEASE(DynamicContentInfo)

L2_DEFINE_OBJECT_CLASS_ID(ZoneQuestInfo)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(ZoneQuestInfo)

L2_DEFINE_OBJECT_ALLOCATOR(ZoneQuestInfo, __GetClassName())

L2_DEFINE_OBJECT_NEW(ZoneQuestInfo)

ZoneQuestInfo::ZoneQuestInfo(DynamicQuest* dynamicQuest) :
    DynamicQuestInfo(dynamicQuest), m_SerialId(0), m_alignment1916cTo19170{} {}

ZoneQuestInfo::~ZoneQuestInfo() = default;

L2_DEFINE_OBJECT_RELEASE(ZoneQuestInfo)

void DynamicQuestInfo::Init(int lastExecutedTime) {
    m_LastExecutedTime = lastExecutedTime;
    AddTimer(5000, 0);
}

int DynamicQuestInfo::GetDQuestId() const { return m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0; }

int DynamicQuestInfo::GetCampaignId() const { return m_pCampaign != nullptr ? m_pCampaign->GetId() : 0; }

int DynamicQuestInfo::GetDuration() const { return m_pCampaign != nullptr ? m_pCampaign->GetDuration() : 0; }

int DynamicQuestInfo::GetRewardPeriod() const { return m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetRewardPeriod() : 0; }

DynamicQuestType::QuestType DynamicQuestInfo::GetQuestType() const {
    return m_pDynamicQuest != nullptr ? m_pDynamicQuest->QuestType() : DynamicQuestType::NORMAL_QUEST;
}

bool DynamicQuestInfo::CanExecutedByCondition(std::int32_t currentTime) {
    L2_GUARD_FUNCTION();
    DynamicQuestStateHandler* const handler = GetStateHandler();
    return handler != nullptr && handler->CanExecutedByCondition() &&
        m_pDynamicQuest->CanExecutedByCondition(*this, currentTime);
}

bool DynamicQuestInfo::IsAlreadyExecuted(std::int32_t now, int bindingDayMask, int resetMinuteOfDay) const {
    L2_GUARD_FUNCTION();
    if (m_LastExecutedTime < 1) {
        return false;
    }

    std::time_t hostNow = static_cast<std::time_t>(now);
    std::tm local{};
#if defined(_WIN32)
    if (::localtime_s(&local, &hostNow) != 0) {
        return false;
    }
#else
    if (::localtime_r(&hostNow, &local) == nullptr) {
        return false;
    }
#endif
    const int elapsedMinutes = ResetBindingDayHelper::FindLatestResetBindingDay(
        bindingDayMask, resetMinuteOfDay, local.tm_wday, local.tm_hour * 60 + local.tm_min
    );
    return now - elapsedMinutes * 60 < m_LastExecutedTime;
}

bool DynamicQuestInfo::IsSleep() {
    L2_GUARD_FUNCTION();
    DynamicQuestStateHandler* const handler = GetStateHandler();
    return handler == nullptr || handler->CanExecutedByCondition();
}

int DynamicQuestInfo::GetTimeLeft() {
    L2_GUARD_FUNCTION();
    DynamicQuestStateHandler* const handler = GetStateHandler();
    return handler != nullptr ? handler->GetTimeLeft(*this) : 0;
}

void DynamicQuestInfo::ShowProgressInfo(CUserSocket* const socket) {
    // Current L2Server 0x00706ee4.
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        return;
    }

    for (const auto& entry : m_GlobalGoalMap) {
        const int goalId = static_cast<int>(entry.first);
        const int progress = entry.second;
        const int goal = m_pCampaign->GetGoal(entry.first);
        std::array<char16_t, 128> message{};
        FormatUtf16To(message.data(), message.size(), u"Progress(Goad ID : %d) : %d / %d", goalId, progress, goal);
        socket->SendSystemMessage(u"SYS", message.data());
    }
}

int DynamicQuestInfo::GetGoal(int goalId) const {
    return m_pCampaign != nullptr ? const_cast<CCampaign*>(m_pCampaign)->GetGoal(static_cast<unsigned int>(goalId)) : 0;
}

int DynamicQuestInfo::GetPoint(int goalId) const {
    const auto position = m_GlobalGoalMap.find(static_cast<unsigned int>(goalId));
    return position != m_GlobalGoalMap.end() ? position->second : 0;
}

SharedDynamicQuestInfo& DynamicQuestInfo::GetSharedInfo() {
    L2_GUARD_FUNCTION();
    int index = 0;
    if (m_pDynamicQuest != nullptr) {
        const unsigned int candidate = static_cast<unsigned int>(m_pDynamicQuest->GetId());
        if (candidate < 100U) {
            index = static_cast<int>(candidate);
        }
    }
    return *SharedDynamicQuestInfo::GetInfo(index);
}

bool DynamicQuestInfo::_CanReceiveReward(User& user) const {
    L2_GUARD_FUNCTION();
    return m_ResultSet.find(static_cast<int>(user.GetDBID())) != m_ResultSet.end();
}

bool DynamicQuestInfo::_IsParticipant(User& user) const {
    L2_GUARD_FUNCTION();
    const auto position = m_UserGoalMap.lower_bound(UserGoalKey(user.GetDBID(), 0));
    return position != m_UserGoalMap.end() && position->first.m_PrimaryKey == user.GetDBID();
}

bool DynamicQuestInfo::_IsEliteUser1(User& user) const {
    L2_GUARD_FUNCTION();
    return m_EliteSet1.find(static_cast<int>(user.GetDBID())) != m_EliteSet1.end();
}

bool DynamicQuestInfo::_IsEliteUser2(User& user) const {
    L2_GUARD_FUNCTION();
    return m_EliteSet2.find(static_cast<int>(user.GetDBID())) != m_EliteSet2.end();
}

bool DynamicQuestInfo::_IsRewardUser1(User& user) const {
    L2_GUARD_FUNCTION();
    return m_RewardSet1.find(static_cast<int>(user.GetDBID())) != m_RewardSet1.end();
}

bool DynamicQuestInfo::_IsRewardReceived(User& user) const {
    L2_GUARD_FUNCTION();
    return _IsParticipant(user) && !_CanReceiveReward(user);
}

void DynamicQuestInfo::_OnCampaignFinished() {
    L2_GUARD_FUNCTION();
    _BroadcastCampaignFinished();
}

void DynamicQuestInfo::_UserTryRequestRewardHTML(User& user) {
    L2_GUARD_FUNCTION();
    if (m_pCampaign != nullptr) {
        m_pCampaign->OnRequestReward(user);
    }
}

void DynamicQuestInfo::_TryNoticeRewardPeriod(User& user) {
    L2_GUARD_FUNCTION();
    if (_CanReceiveReward(user)) {
        _NoticeRewardPeriod(user);
    } else if (_IsRewardReceived(user)) {
        _NoticeRewardReceived(user);
    }
}

void DynamicQuestInfo::_TryNoticeCampaignFailed(User& user) {
    L2_GUARD_FUNCTION();
    if (m_pDynamicQuest != nullptr && m_pDynamicQuest->CanExecutedByUser(user)) {
        _NoticeCampaignFailed(user);
    }
}

void DynamicQuestInfo::_NoticeCampaignStarted(User& user) {
    L2_GUARD_FUNCTION();
    CampaignSuggestPacket packet(*m_pCampaign);
    packet.Send(user);
}

void DynamicQuestInfo::_NoticeRewardPeriod(User& user) {
    L2_GUARD_FUNCTION();
    CampaignRewardStartedPacket packet(*m_pCampaign, m_ProgressPacket[1]);
    packet.Send(user);
}

void DynamicQuestInfo::_NoticeRewardReceived(User& user) {
    L2_GUARD_FUNCTION();
    CampaignRewardReceivedPacket packet(*m_pCampaign, m_ProgressPacket[2]);
    packet.Send(user);
}

void DynamicQuestInfo::_NoticeCampaignFailed(User& user) {
    L2_GUARD_FUNCTION();
    CampaignFailedPacket packet(*m_pCampaign, m_ProgressPacket[3]);
    packet.Send(user);
}

void DynamicQuestInfo::_SendProgressInfo(User& user) {
    L2_GUARD_FUNCTION();
    CampaignProgressPacket packet(*m_pCampaign, m_ProgressPacket[0]);
    packet.Send(user);
}

void DynamicQuestInfo::_TrySendScoreBoard(User& user) {
    L2_GUARD_FUNCTION();
    if (m_ScoreBoardPacket.GetLength() > 0) {
        CampaignScoreBoardPacket packet(*m_pCampaign, m_ScoreBoardPacket);
        packet.Send(user);
    }
}

void DynamicQuestInfo::_OnReceiveReward(User& user) {
    L2_GUARD_FUNCTION();
    m_ResultSet.erase(static_cast<int>(user.GetDBID()));
    CampaignRewardReceivedPacket packet(*m_pCampaign, m_ProgressPacket[2]);
    packet.Send(user);
}

void DynamicQuestInfo::_BroadcastCampaignFinished() {
    // Current L2Server 0x00703060.
    L2_GUARD_FUNCTION();
    if (m_pCampaign == nullptr) {
        return;
    }
    CampaignFinishedPacket packet(*m_pCampaign);
    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
        if (user->IsInRealWorld() && m_pDynamicQuest->CanExecutedByUser(*user)) {
            packet.Send(*user);
        }
    });
}

void DynamicQuestInfo::_BroadcastRewardPeriod() {
    L2_GUARD_FUNCTION();
    CampaignRewardStartedPacket packet(*m_pCampaign, m_ProgressPacket[1]);
    for (const int userDbId : m_ResultSet) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, userDbId));
        if (user && m_pDynamicQuest->CanExecutedByUser(*user.get())) {
            packet.Send(*user.get());
        }
    }
}

void DynamicQuestInfo::_BroadcastCampaignFailed() {
    L2_GUARD_FUNCTION();
    CampaignFailedPacket packet(*m_pCampaign, m_ProgressPacket[3]);
    int previousUserDbId = 0;
    for (const auto& entry : m_UserGoalMap) {
        const int userDbId = static_cast<int>(entry.first.m_PrimaryKey);
        if (userDbId == previousUserDbId) {
            continue;
        }
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, userDbId));
        if (user && m_pDynamicQuest->CanExecutedByUser(*user.get())) {
            packet.Send(*user.get());
        }
        previousUserDbId = userDbId;
    }
}

bool DynamicQuestInfo::_IsAllGoalCompleted() {
    L2_GUARD_FUNCTION();
    if (m_pCampaign == nullptr) {
        return false;
    }
    for (const auto& goal : m_GlobalGoalMap) {
        if (goal.second < m_pCampaign->GetGoal(static_cast<int>(goal.first))) {
            return false;
        }
    }
    return true;
}

void DynamicQuestInfo::_StartDynamicQuest() {
    L2_GUARD_FUNCTION();
    if (m_pDynamicQuest == nullptr) {
        return;
    }
    CCampaign* const firstCampaign = m_pDynamicQuest->GetFirstCampaign();
    if (!_ChangeState(PERIOD_SLEEP, PERIOD_WORKING, firstCampaign)) {
        return;
    }

    AddTimer(5000, 0);
    CIOObject::SetTimeStamp(&m_DQuestStartTime);
    m_nServerUserCount = g_nServerUserCount;
    m_nAdjustedRewardRate = DynamicQuestManager::Inst()->GetAdjustedRewardRate();
}

void DynamicQuestInfo::_ClearResultSummary() {
    L2_GUARD_FUNCTION();
    m_GlobalGoalMap.clear();
    m_UserGoalMap.clear();
    if (m_pCampaign != nullptr) {
        for (const auto& goal : m_pCampaign->m_GoalMap) {
            m_GlobalGoalMap[goal.first] = 0;
        }
    }
    m_ResultSet.clear();
    m_EliteSet1.clear();
    m_EliteSet2.clear();
    m_RewardSet1.clear();
    for (auto& packet : m_ProgressPacket) {
        packet.Reset();
    }
    m_ScoreBoardPacket.Reset();
}

void DynamicQuestInfo::_MakeResultSummary() {
    L2_GUARD_FUNCTION();
    ScoreMap score;
    _MakeScoreMap(score, true);
    _MakeEliteList(score);
    _MakeScoreBoard(score);
}

void DynamicQuestInfo::_OnAcceptCampaign(User& user) {
    L2_GUARD_FUNCTION();
    if (m_pDynamicQuest == nullptr || m_pCampaign == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] null campaign!",
        //     L"DynamicQuest.cpp",
        //     0x6ff)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] null campaign!",
        //     L"DynamicQuest.cpp",
        //     0x6ff)
        L2_LOG_ERROR(
            Log,
            "Cannot record dynamic-quest campaign acceptance for character DBID %u: dynamic quest present: %s, campaign "
            "present: %s; acceptance bookkeeping is skipped.",
            static_cast<unsigned int>(user.GetDBID()), m_pDynamicQuest != nullptr ? "yes" : "no",
            m_pCampaign != nullptr ? "yes" : "no"
        );
        return;
    }

    for (const auto& goal : m_pCampaign->m_GoalMap) {
        UserGoalData data;
        const std::u16string targetName = ::std::u16string(user.GetName());
        data.charName.assign(targetName);
        m_UserGoalMap.emplace(UserGoalKey(user.GetDBID(), goal.first), std::move(data));
    }

    _OnAcceptCampaignImpl(user);
    _SendProgressInfo(user);
    if (m_pDynamicQuest->QuestType() == DynamicQuestType::DAILY_QUEST) {
        user.SetDailyQuest(m_pDynamicQuest->GetId(), true);
    }

    CLogFormatter formatter;
    formatter.WriteLog2(
        m_pDynamicQuest->IsCastleQuest() ? 0x1f9a : 0x1f91, &user, nullptr, m_pDynamicQuest->GetId(), m_pCampaign->GetId(),
        0, 0, 0, 0, 0
    );
}

void DynamicQuestInfo::_TryAddPoint(const PointParam& point) {
    L2_GUARD_FUNCTION();
    const int goalLimit = m_pCampaign->GetGoal(point.goalId);
    int awardedPoint = point.point;

    const auto userGoal = m_UserGoalMap.find(UserGoalKey(point.userDBId, point.goalId));
    if (userGoal == m_UserGoalMap.end()) {
        return;
    }
    const auto globalGoal = m_GlobalGoalMap.find(point.goalId);
    if (globalGoal == m_GlobalGoalMap.end() || globalGoal->second >= goalLimit) {
        return;
    }

    globalGoal->second = runtime::AddInt32Wrapping(globalGoal->second, awardedPoint);
    if (globalGoal->second > goalLimit) {
        awardedPoint = globalGoal->second - goalLimit;
        globalGoal->second = goalLimit;
    }
    userGoal->second.point.at[static_cast<int>(point.pointType)] =
        runtime::AddInt32Wrapping(userGoal->second.point.at[static_cast<int>(point.pointType)], awardedPoint);

    CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(point.userDBId)));
    if (user) {
        const int userTotal = runtime::AddInt32Wrapping(userGoal->second.point.at[0], userGoal->second.point.at[1]);
        char16_t pointTypeText[2]{};
        char16_t normalPointText[16]{};
        char16_t extraPointText[16]{};
        FormatUtf16To(pointTypeText, std::size(pointTypeText), u"%d", static_cast<int>(point.pointType));
        FormatUtf16To(normalPointText, std::size(normalPointText), u"%d", userGoal->second.point.at[0]);
        FormatUtf16To(extraPointText, std::size(extraPointText), u"%d", userGoal->second.point.at[1]);

        CLogFormatter formatter;
        formatter.WriteLog1(
            m_pDynamicQuest->IsCastleQuest() ? 0x1f9b : 0x1f92, user.get(), nullptr, pointTypeText, normalPointText,
            extraPointText, m_pDynamicQuest->GetId(), m_pCampaign->GetId(), point.goalId, awardedPoint, userTotal,
            globalGoal->second, goalLimit
        );
    }
    if (_IsAllGoalCompleted()) {
        _CampaignSuccess();
    }
}

void DynamicQuestInfo::SetCastleCampaignResult(bool success) {
    L2_GUARD_FUNCTION();
    if (m_pDynamicQuest == nullptr || m_pDynamicQuest->GetRelatedCastleID() < 1) {
        return;
    }

    const int castleId = m_pDynamicQuest->GetRelatedCastleID();
    CResidence* const castle = g_ResidenceDB.GetResidence(castleId);
    if (castle == nullptr || !castle->IsCastle()) {
        return;
    }
    castle->SetCampaignResult(success);
    for (int residenceId = 1; residenceId < 10; ++residenceId) {
        CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
        if (residence != nullptr && residence->IsCastle() && residence->m_data != nullptr &&
            residence->m_data->m_nAgitParentCastleId == castleId) {
            residence->SetCampaignResult(success);
        }
    }
}

void DynamicQuestInfo::_OnCampaignSuccess() {
    L2_GUARD_FUNCTION();
    m_pCampaign->OnCompleted(true);
    m_LastExecutedTime = m_DQuestStartTime;
    db.RequestSaveDynamicQuest(m_pDynamicQuest->GetId(), m_DQuestStartTime);
    _MakeSnapshot(DynamicQuestType::REWARD_START, false);
    _MakeSnapshot(DynamicQuestType::REWARD_RECEIVED, true);
    _MakeResultSummary();
    _BroadcastRewardPeriod();
    SetCastleCampaignResult(true);
}

void DynamicQuestInfo::_OnCampaignFailed() {
    L2_GUARD_FUNCTION();
    m_pCampaign->OnCompleted(false);
    _MakeSnapshot(DynamicQuestType::FAILED, false);
    ScoreMap score;
    _MakeScoreMap(score, true);
    _BroadcastCampaignFailed();
    SetCastleCampaignResult(false);
}

void DynamicQuestInfo::_OnCampaignStarted() {
    L2_GUARD_FUNCTION();
    CIOObject::SetTimeStamp(&m_CampaignStartTime);
    _ClearResultSummary();
    m_pCampaign->OnStarted();
    _MakeSnapshot(DynamicQuestType::ProgressInfoType(0), false);
    _BroadcastCampaignStarted();
}

DynamicContentSerializer::DynamicContentSerializer() : DynamicQuestSerializer() {}

void DynamicContentSerializer::AddExecutable(
    DynamicQuestInfo* owner, Executable executable, CExecutionArgument* argument, const wchar_t* executableName
) {
    s.AddExecutable(owner, executable, argument, executableName);
}

ZoneQuestSerializer::ZoneQuestSerializer() : DynamicQuestSerializer() {}

void ZoneQuestSerializer::AddExecutable(
    DynamicQuestInfo* owner, Executable executable, CExecutionArgument* argument, const wchar_t* executableName
) {
    s.AddExecutable(owner, executable, argument, executableName);
}

void DynamicContentInfo::MakeSerializer() { s = new DynamicContentSerializer(); }

void ZoneQuestInfo::MakeSerializer() { s = new ZoneQuestSerializer(); }

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(DynamicQuestInfo, __async_arg_AsyncStartDynamicQuest)

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(DynamicQuestInfo, __async_arg_AsyncInitNPCMaker)

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(DynamicQuestInfo, __async_arg_AsyncStopDynamicQuest)

L2_ASYNC_DEFINE_EMPTY_2_SLOT_COPY(DynamicQuestInfo, __async_arg_AsyncTimerExpired)

L2_ASYNC_DEFINE_FIELDWISE_COPY_1(DynamicQuestInfo, __async_arg_AsyncContinueCampaign)
L2_ASYNC_DEFINE_FIELDWISE_COPY_1(DynamicQuestInfo, __async_arg_AsyncChangeCampaign)
L2_ASYNC_DEFINE_FIELDWISE_COPY_1(DynamicQuestInfo, __async_arg_AsyncUserTryJoinInCampaign)
L2_ASYNC_DEFINE_FIELDWISE_COPY_1(DynamicQuestInfo, __async_arg_AsyncUserTryRequestCampaignHTML)
L2_ASYNC_DEFINE_FIELDWISE_COPY_1(DynamicQuestInfo, __async_arg_AsyncUserTryAcceptCampaign)
L2_ASYNC_DEFINE_FIELDWISE_COPY_1(DynamicQuestInfo, __async_arg_AsyncOnUserAcceptCampaign)
L2_ASYNC_DEFINE_FIELDWISE_COPY_1(DynamicQuestInfo, __async_arg_AsyncUserTryReceiveReward)
L2_ASYNC_DEFINE_FIELDWISE_COPY_1(DynamicQuestInfo, __async_arg_AsyncOnUserReceiveReward)
L2_ASYNC_DEFINE_FIELDWISE_COPY_4(DynamicQuestInfo, __async_arg_AsyncTryAddPoint)
L2_ASYNC_DEFINE_FIELDWISE_COPY_1(DynamicQuestInfo, __async_arg_AsyncTrySendProgressInfo)
L2_ASYNC_DEFINE_FIELDWISE_COPY_1(DynamicQuestInfo, __async_arg_AsyncTrySendScoreBoard)
L2_ASYNC_DEFINE_FIELDWISE_COPY_1(DynamicQuestInfo, __async_arg_AsyncResetUserGoalMap)

DynamicQuestInfo* DynamicQuest::MakeInfo() {
    DynamicQuestInfo* const info = MakeInfoImpl();
    info->MakeSerializer();
    return info;
}

DynamicQuestInfo* DynamicContent::MakeInfoImpl() {
    L2_GUARD_FUNCTION();
    return new DynamicContentInfo(this);
}

DynamicQuestInfo* ZoneQuest::MakeInfoImpl() {
    L2_GUARD_FUNCTION();
    return new ZoneQuestInfo(this);
}

void CUserZoneQuestHandler::SetCampaign(ZoneQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    const int dynamicQuestId = info.m_pDynamicQuest != nullptr ? info.m_pDynamicQuest->GetId() : 0;
    for (User::OnGoingZoneQuest& ongoing : user.m_OnGoingZoneQuest) {
        if (ongoing.dQuestId == dynamicQuestId || ongoing.dQuestId == 0) {
            ongoing.dQuestId = dynamicQuestId;
            ongoing.serialId = info.m_SerialId;
            return;
        }
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Too many ZoneQuest. user(%s)",
    //     L"DynamicQuest.cpp",
    //     0xda3,
    //     user.GetName())
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Too many ZoneQuest. user(%s)",
    //     L"DynamicQuest.cpp",
    //     0xda3,
    //     user.GetName())
    // L2_LOG_ERROR(Log,
    //     "Cannot associate zone quest %d (serial %d) with character '%s' (DBID %u): all %llu zone-quest slots belong to
    //     other quests. The association is not stored.", dynamicQuestId, info.m_SerialId,
    //     WideToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()), static_cast<unsigned long
    //     long>(user.m_OnGoingZoneQuest.size()))
    L2_LOG_ERROR(
        Log,
        "Cannot associate zone quest %d (serial %d) with character '%s' (DBID %u): all %llu zone-quest slots belong to "
        "other quests; the association is not stored.",
        dynamicQuestId, info.m_SerialId, Utf16ToUtf8(user.GetName()).c_str(), static_cast<unsigned int>(user.GetDBID()),
        static_cast<unsigned long long>(user.m_OnGoingZoneQuest.size())
    );
}

bool CUserZoneQuestHandler::IsSetUser(ZoneQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    const int dynamicQuestId = info.m_pDynamicQuest != nullptr ? info.m_pDynamicQuest->GetId() : 0;
    return std::any_of(
        user.m_OnGoingZoneQuest.begin(), user.m_OnGoingZoneQuest.end(),
        [dynamicQuestId, &info](const User::OnGoingZoneQuest& ongoing) {
            return ongoing.dQuestId == dynamicQuestId && ongoing.serialId == info.m_SerialId;
        }
    );
}

void CUserZoneQuestHandler::ResetCampaign(ZoneQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    const int dynamicQuestId = info.m_pDynamicQuest != nullptr ? info.m_pDynamicQuest->GetId() : 0;
    for (User::OnGoingZoneQuest& ongoing : user.m_OnGoingZoneQuest) {
        if (ongoing.dQuestId != dynamicQuestId) {
            continue;
        }
        ongoing.dQuestId = 0;
        ongoing.serialId = 0;
        DynamicQuestInfo::__async_arg_AsyncResetUserGoalMap argument(&user);
        info.s->AddExecutable(
            &info, [](DynamicQuestInfo* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncResetUserGoalMap(pad); },
            &argument, L"AsyncResetUserGoalMap"
        );
        return;
    }
}

void DynamicContentInfo::OnUserEnterWorld(User& user) {
    L2_GUARD_FUNCTION();
    UserTryJoinInCampaign(user);
}

void DynamicContentInfo::OnUserMove(User&) {}

void DynamicContentInfo::_UserTryStartAndJoinInDynamicQuest(User&) {}

void DynamicContentInfo::_TryNoticeCampaignStarted(User& user) {
    L2_GUARD_FUNCTION();
    if (_IsParticipant(user)) {
        _SendProgressInfo(user);
    } else if (m_pCampaign->CanStartQuest(user)) {
        _NoticeCampaignStarted(user);
    }
}

void DynamicContentInfo::_UserTryRequestInquiryHTML(User& user) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00701164 assumes m_pCampaign.
    if (_IsParticipant(user)) {
        m_pCampaign->OnInquiry(user);
    } else {
        m_pCampaign->TryStart(user);
    }
}

void DynamicContentInfo::TryAcceptCampaign(User& user) {
    L2_GUARD_FUNCTION();
    L2_ADD_REF(&(user), ObjectRefType::DynamicQuestSerializer);
    __async_arg_AsyncUserTryAcceptCampaign argument(&user);
    s->AddExecutable(
        this,
        [](DynamicQuestInfo* owner, CExecutionArgumentPad* pad) {
            static_cast<DynamicContentInfo*>(owner)->__async_AsyncUserTryAcceptCampaign(pad);
        },
        &argument, L"AsyncUserTryAcceptCampaign"
    );
}

void DynamicContentInfo::_TrySendScoreBoardOnWorking(User&) {}

void DynamicContentInfo::_BroadcastCampaignStarted() {
    // Current L2Server 0x00703724.
    L2_GUARD_FUNCTION();
    CampaignSuggestPacket packet(*m_pCampaign);
    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
        if (m_pCampaign->CanStartQuest(*user)) {
            packet.Send(*user);
        }
    });
}

void DynamicContentInfo::_BroadcastRewardPeriod() {
    L2_GUARD_FUNCTION();
    if (m_pCampaign == nullptr || m_pDynamicQuest == nullptr) {
        return;
    }
    CampaignRewardStartedPacket packet(*m_pCampaign, m_ProgressPacket[1]);
    for (const int userDbId : m_ResultSet) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, userDbId));
        if (!user || !m_pDynamicQuest->CanExecutedByUser(*user.get())) {
            continue;
        }
        if (user->m_data != nullptr) {
            user->m_data->m_OnGoingDynamicContent = 0;
        }
        packet.Send(*user.get());
    }
}

void DynamicContentInfo::_BroadcastCampaignFailed() {
    L2_GUARD_FUNCTION();
    if (m_pCampaign == nullptr || m_pDynamicQuest == nullptr) {
        return;
    }
    CampaignFailedPacket packet(*m_pCampaign, m_ProgressPacket[3]);
    unsigned int previousUserDbId = 0;
    for (const auto& entry : m_UserGoalMap) {
        const unsigned int userDbId = entry.first.m_PrimaryKey;
        if (userDbId == previousUserDbId) {
            continue;
        }
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(userDbId)));
        if (user && m_pDynamicQuest->CanExecutedByUser(*user.get())) {
            if (user->m_data != nullptr) {
                user->m_data->m_OnGoingDynamicContent = 0;
            }
            packet.Send(*user.get());
        }
        previousUserDbId = userDbId;
    }
}

void DynamicContentInfo::_MakeSnapshot(DynamicQuestType::ProgressInfoType type, bool) {
    L2_GUARD_FUNCTION();
    const int packetIndex = static_cast<int>(type);
    if (packetIndex < 0 || packetIndex >= DynamicQuestType::MAX_INFOTYPE || m_pCampaign == nullptr ||
        m_pDynamicQuest == nullptr) {
        return;
    }

    CPacketHelperEx<20480>& packet = m_ProgressPacket[static_cast<std::size_t>(packetIndex)];
    packet.Reset();
    packet.Assemble(
        "ccddcd", static_cast<int>(m_pDynamicQuest->GetType()), 2, m_pDynamicQuest->GetId(), m_pCampaign->GetId(),
        packetIndex, GetTimeLeft()
    );
    packet.Assemble("d", m_pCampaign->GetGoalCount());

    int currentTotal = 0;
    int goalTotal = 0;
    for (const auto& goal : m_GlobalGoalMap) {
        const int goalId = static_cast<int>(goal.first);
        const int required = m_pCampaign->GetGoal(goal.first);
        packet.Assemble("ddd", goalId, goal.second, required);
        currentTotal = runtime::AddInt32Wrapping(currentTotal, goal.second);
        goalTotal = runtime::AddInt32Wrapping(goalTotal, required);
    }

    SharedDynamicQuestInfo& shared = GetSharedInfo();
    if (goalTotal != 0) {
        // Current 0x00706cd4 performs the integer quotient before cvtsi2sd.
        shared.m_Progress = static_cast<double>(currentTotal / goalTotal) * 100.0;
    }
    shared.m_Point = currentTotal;
}

void DynamicContentInfo::_MakeScoreMap(ScoreMap& score, bool reward) {
    L2_GUARD_FUNCTION();
    auto position = m_UserGoalMap.begin();
    while (position != m_UserGoalMap.end()) {
        const unsigned int userDbId = position->first.m_PrimaryKey;
        ScoreData data;
        data.charName = position->second.charName;

        do {
            for (std::size_t pointIndex = 0; pointIndex < data.point.at.size(); ++pointIndex) {
                data.point.at[pointIndex] =
                    runtime::AddInt32Wrapping(data.point.at[pointIndex], position->second.point.at[pointIndex]);
            }
            ++position;
        } while (position != m_UserGoalMap.end() && position->first.m_PrimaryKey == userDbId);

        const int total = data.point.Sum();
        if (total <= 0) {
            continue;
        }
        score.emplace(ScoreKey(total, userDbId), std::move(data));
        if (reward) {
            m_ResultSet.insert(static_cast<int>(userDbId));
        }
    }
}

void DynamicContentInfo::_MakeEliteList(const ScoreMap& score) {
    L2_GUARD_FUNCTION();
    if (m_pDynamicQuest == nullptr) {
        return;
    }
    const int eliteCount1 = m_pDynamicQuest->GetEliteCount1();
    const int eliteCount2 = m_pDynamicQuest->GetEliteCount2();
    const int rewardCount1 = m_pDynamicQuest->GetRewardCount1();
    if (eliteCount1 <= 0 || eliteCount2 <= 0 || rewardCount1 <= 0) {
        return;
    }

    int phase = 1;
    int count = 0;
    int boundaryScore = 0;
    for (auto position = score.rbegin(); position != score.rend(); ++position) {
        const int points = position->first.first;
        const int userDbId = static_cast<int>(position->first.second);
        DynamicQuestUserSet* destination = nullptr;
        int limit = 0;

        if (phase == 1) {
            limit = eliteCount1;
            if (count >= limit && points < boundaryScore) {
                phase = 2;
                count = 0;
                boundaryScore = 0;
            }
        }
        if (phase == 2) {
            limit = eliteCount2;
            if (count >= limit && points < boundaryScore) {
                phase = 3;
                count = 0;
                boundaryScore = 0;
            }
        }
        if (phase == 3) {
            limit = rewardCount1;
            if (count >= limit && points < boundaryScore) {
                break;
            }
        }

        destination = phase == 1 ? &m_EliteSet1 : (phase == 2 ? &m_EliteSet2 : &m_RewardSet1);
        ++count;
        if (count == limit) {
            boundaryScore = points;
        }
        destination->insert(userDbId);
    }
}

void DynamicContentInfo::_MakeScoreBoard(const ScoreMap& score) {
    L2_GUARD_FUNCTION();
    if (m_pDynamicQuest == nullptr || m_pCampaign == nullptr) {
        return;
    }
    CPacketHelperEx<20480> rows;
    int count = 0;
    const int limit = m_pDynamicQuest->GetEliteCount1();
    for (auto position = score.rbegin(); position != score.rend() && count < limit; ++position, ++count) {
        rows.Assemble("S", position->second.charName.c_str());
    }

    m_ScoreBoardPacket.Reset();
    m_ScoreBoardPacket.Assemble(
        "ccdd", static_cast<int>(m_pDynamicQuest->GetType()), 3, m_pDynamicQuest->GetId(), m_pCampaign->GetId()
    );
    m_ScoreBoardPacket.Assemble("db", rows.GetAssembledCount(), rows.GetLength(), rows.GetPacket());
}

void DynamicContentInfo::_OnAcceptCampaignImpl(User& user) {
    L2_GUARD_FUNCTION();
    if (m_pDynamicQuest != nullptr && user.m_data != nullptr) {
        user.m_data->m_OnGoingDynamicContent = m_pDynamicQuest->GetId();
    }
}

void DynamicContentInfo::OutOfRangeOnSendProgressInfo(User&) {}

void DynamicContentInfo::_OnSetCamapaign() {}

void ZoneQuestInfo::OnUserEnterWorld(User& user) {
    L2_GUARD_FUNCTION();
    if (m_pDynamicQuest != nullptr && m_pDynamicQuest->IsInRange(user)) {
        UserTryJoinInCampaign(user);
    }
}

void ZoneQuestInfo::OnUserMove(User& user) {
    L2_GUARD_FUNCTION();
    if (!CUserZoneQuestHandler::IsSetUser(*this, user)) {
        UserTryJoinInCampaign(user);
    }
}

void ZoneQuestInfo::_UserTryStartAndJoinInDynamicQuest(User&) {
    L2_GUARD_FUNCTION();
    _StartDynamicQuest();
}

void ZoneQuestInfo::_TryNoticeCampaignStarted(User& user) {
    L2_GUARD_FUNCTION();
    if (CUserZoneQuestHandler::IsSetUser(*this, user)) {
        return;
    }
    if (_IsParticipant(user)) {
        CUserZoneQuestHandler::SetCampaign(*this, user);
        _SendProgressInfo(user);
    } else if (m_pCampaign->CanStartQuest(user)) {
        _TryAcceptCampaign(user);
    }
}

void ZoneQuestInfo::_UserTryRequestInquiryHTML(User& user) {
    L2_GUARD_FUNCTION();
    // Current L2Server.exe 0x00701260 has no campaign null fallback.
    if (_IsParticipant(user)) {
        m_pCampaign->OnInquiry(user);
    }
}

void ZoneQuestInfo::TryAcceptCampaign(User&) {}

void ZoneQuestInfo::_TrySendScoreBoardOnWorking(User& user) {
    L2_GUARD_FUNCTION();
    _TrySendScoreBoard(user);
}

void ZoneQuestInfo::_BroadcastCampaignStarted() {}

void ZoneQuestInfo::_MakeSnapshot(DynamicQuestType::ProgressInfoType type, bool force) {
    L2_GUARD_FUNCTION();
    const int packetIndex = static_cast<int>(type);
    if (packetIndex < 0 || packetIndex >= DynamicQuestType::MAX_INFOTYPE || m_pCampaign == nullptr ||
        m_pDynamicQuest == nullptr) {
        return;
    }

    CPacketHelperEx<20480>& packet = m_ProgressPacket[static_cast<std::size_t>(packetIndex)];
    packet.Reset();
    packet.Assemble(
        "ccddcdd", static_cast<int>(m_pDynamicQuest->GetType()), 2, m_pDynamicQuest->GetId(), m_pCampaign->GetId(),
        packetIndex, GetTimeLeft(), _GetUserCount()
    );
    packet.Assemble("d", m_pCampaign->GetGoalCount());

    int currentTotal = 0;
    int goalTotal = 0;
    for (const auto& goal : m_GlobalGoalMap) {
        const int goalId = static_cast<int>(goal.first);
        const int required = m_pCampaign->GetGoal(goal.first);
        packet.Assemble("ddd", goalId, goal.second, required);
        currentTotal = runtime::AddInt32Wrapping(currentTotal, goal.second);
        goalTotal = runtime::AddInt32Wrapping(goalTotal, required);
    }
    if (goalTotal != 0) {
        GetSharedInfo().m_Progress = static_cast<double>(currentTotal / goalTotal) * 100.0;
    }
    if (!force) {
        ScoreMap score;
        _MakeScoreMap(score, false);
        _MakeScoreBoard(score);
    }
}

void ZoneQuestInfo::_MakeScoreMap(ScoreMap& score, bool reward) {
    L2_GUARD_FUNCTION();
    std::map<unsigned int, ScoreData> partyScore;

    auto position = m_UserGoalMap.begin();
    while (position != m_UserGoalMap.end()) {
        const unsigned int userDbId = position->first.m_PrimaryKey;
        auto groupEnd = position;
        do {
            ++groupEnd;
        } while (groupEnd != m_UserGoalMap.end() && groupEnd->first.m_PrimaryKey == userDbId);

        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(userDbId)));
        if (!user) {
            while (position != groupEnd) {
                position = m_UserGoalMap.erase(position);
            }
            continue;
        }

        unsigned int scoreOwnerId = userDbId;
        std::u16string scoreOwnerName = position->second.charName;
        if (CParty* const party = user->GetParty()) {
            if (User* const master = party->GetMaster()) {
                scoreOwnerId = master->GetDBID();
                const char16_t* const masterName = master->GetName();
                scoreOwnerName.assign(::std::u16string(masterName));
            }
        }

        auto inserted = partyScore.emplace(scoreOwnerId, ScoreData{});
        ScoreData& data = inserted.first->second;
        if (inserted.second) {
            data.charName = scoreOwnerName;
        }
        data.userDBIdList.push_back(userDbId);
        for (; position != groupEnd; ++position) {
            for (std::size_t pointIndex = 0; pointIndex < data.point.at.size(); ++pointIndex) {
                data.point.at[pointIndex] =
                    runtime::AddInt32Wrapping(data.point.at[pointIndex], position->second.point.at[pointIndex]);
            }
        }
        if (reward) {
            m_ResultSet.insert(static_cast<int>(userDbId));
        }
    }

    for (const auto& entry : partyScore) {
        const int total = entry.second.point.Sum();
        score.emplace(ScoreKey(total, entry.first), entry.second);
    }
}

void ZoneQuestInfo::_MakeEliteList(const ScoreMap& score) {
    L2_GUARD_FUNCTION();
    if (m_pDynamicQuest == nullptr) {
        return;
    }
    const int eliteCount1 = m_pDynamicQuest->GetEliteCount1();
    const int eliteCount2 = m_pDynamicQuest->GetEliteCount2();
    const int rewardCount1 = m_pDynamicQuest->GetRewardCount1();
    if (eliteCount1 <= 0 || eliteCount2 <= 0 || rewardCount1 <= 0) {
        return;
    }

    int phase = 1;
    int count = 0;
    int boundaryScore = 0;
    for (auto position = score.rbegin(); position != score.rend(); ++position) {
        const int points = position->first.first;
        int limit = phase == 1 ? eliteCount1 : (phase == 2 ? eliteCount2 : rewardCount1);
        if (count >= limit && points < boundaryScore) {
            if (phase == 3) {
                break;
            }
            ++phase;
            count = 0;
            boundaryScore = 0;
            limit = phase == 2 ? eliteCount2 : rewardCount1;
        }

        DynamicQuestUserSet& destination = phase == 1 ? m_EliteSet1 : (phase == 2 ? m_EliteSet2 : m_RewardSet1);
        ++count;
        if (count == limit) {
            boundaryScore = points;
        }
        for (const unsigned int userDbId : position->second.userDBIdList) {
            destination.insert(static_cast<int>(userDbId));
        }
    }
}

void ZoneQuestInfo::_MakeScoreBoard(const ScoreMap& score) {
    L2_GUARD_FUNCTION();
    if (m_pDynamicQuest == nullptr || m_pCampaign == nullptr) {
        return;
    }
    CPacketHelperEx<20480> rows;
    int rank = 0;
    for (auto position = score.rbegin(); position != score.rend(); ++position) {
        // Current 0x0070431c preincrements and stops when the value reaches
        // MAX_SCORE_BOARD_COUNT, so at most 249 rows are serialized.
        if (++rank >= MAX_SCORE_BOARD_COUNT) {
            break;
        }
        rows.Assemble(
            "Sddd", position->second.charName.c_str(), position->first.first, position->second.point.at[0],
            position->second.point.at[1]
        );
    }

    m_ScoreBoardPacket.Reset();
    m_ScoreBoardPacket.Assemble(
        "ccdddd", static_cast<int>(m_pDynamicQuest->GetType()), 3, m_pDynamicQuest->GetId(), m_pCampaign->GetId(),
        GetTimeLeft(), _GetUserCount()
    );
    m_ScoreBoardPacket.Assemble("db", rows.GetAssembledCount(), rows.GetLength(), rows.GetPacket());
}

void ZoneQuestInfo::_OnAcceptCampaignImpl(User& user) {
    L2_GUARD_FUNCTION();
    CUserZoneQuestHandler::SetCampaign(*this, user);
}

void ZoneQuestInfo::OutOfRangeOnSendProgressInfo(User& user) {
    L2_GUARD_FUNCTION();
    CampaignFinishedPacket packet(*m_pCampaign);
    packet.Send(user);
    CUserZoneQuestHandler::ResetCampaign(*this, user);
}

int ZoneQuestInfo::_GetUserCount() const {
    L2_GUARD_FUNCTION();
    if (m_pCampaign == nullptr || m_pCampaign->GetGoalCount() == 0) {
        return 0;
    }
    return static_cast<int>(m_UserGoalMap.size()) / static_cast<int>(m_pCampaign->GetGoalCount());
}

void ZoneQuestInfo::_OnSetCamapaign() {
    // Current L2Server.exe 0x007092c4 uses InterlockedExchange on the
    // concrete ZoneQuestInfo serial lane.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(&m_SerialId, DynamicQuestManager::Inst()->MakeSerialId());
}

DynamicQuestStateHandler* DynamicQuestInfo::GetStateHandler() {
    L2_GUARD_FUNCTION();
    // A compare-exchange with nullptr is the sequentially consistent read.
    return runtime::raw_atomic::CompareExchangePointer<DynamicQuestStateHandler>(&m_pStateHandler, nullptr, nullptr);
}

void DynamicQuestInfo::_OnCampaignWorkingEnd() {
    L2_GUARD_FUNCTION();
    CIOObject::SetTimeStamp(&m_CampaignWorkingEndTime);
}

void DynamicQuestInfo::_ChangeStateHandler(
    DynamicQuestStateHandler& previous, DynamicQuestStateHandler& next, CCampaign* campaign
) {
    L2_GUARD_FUNCTION();
    previous._OnEnd(*this);
    if (campaign != nullptr) {
        _SetCampaign(campaign);
        campaign->SendCampaignInfo();
    }
    next._OnStart(*this);
}

bool DynamicQuestInfo::_ChangeState(State expected, State next, CCampaign* campaign) {
    L2_GUARD_FUNCTION();
    DynamicQuestStateHandler* expectedHandler = DynamicQuestState::Handler[expected];
    DynamicQuestStateHandler* const nextHandler = DynamicQuestState::Handler[next];
    const bool changed =
        runtime::raw_atomic::CompareExchangePointer(&m_pStateHandler, nextHandler, expectedHandler) == expectedHandler;
    if (changed) {
        _ChangeStateHandler(*expectedHandler, *nextHandler, campaign);
    }
    return changed;
}

void DynamicQuestInfo::_OnCampaignStoped() {
    L2_GUARD_FUNCTION();
    _ChangeState(PERIOD_FAILED, PERIOD_SLEEP, nullptr);
}

void DynamicQuestInfo::_ContinueCampaign(CCampaign* campaign) {
    L2_GUARD_FUNCTION();
    _ChangeState(PERIOD_WAIT, PERIOD_WORKING, campaign);
}

void DynamicQuestInfo::_ChangeCampaign(State state, CCampaign* campaign) {
    L2_GUARD_FUNCTION();
    if (campaign == nullptr) {
        return;
    }
    if (_ChangeState(state, PERIOD_WAIT, nullptr)) {
        if (m_pCampaign != nullptr) {
            m_pCampaign->OnCompleted(false);
        }
        _ContinueCampaign(campaign);
    }
}

void DynamicQuestInfo::_CampaignSuccess() {
    L2_GUARD_FUNCTION();
    _ChangeState(PERIOD_WORKING, PERIOD_REWARD, nullptr);
}

void DynamicQuestInfo::_CampaignFailed() {
    L2_GUARD_FUNCTION();
    _ChangeState(PERIOD_WORKING, PERIOD_FAILED, nullptr);
}

void DynamicQuestInfo::_RewardFinished() {
    L2_GUARD_FUNCTION();
    CCampaign* const previous = m_pCampaign;
    CCampaign* const next = previous->GetNextCampaignOnSuccess();
    const State nextState = next != nullptr ? PERIOD_WAIT : PERIOD_SLEEP;
    if (_ChangeState(PERIOD_REWARD, nextState, nullptr) && next != nullptr) {
        (new CDelayedCampaignStarter(previous, next))->Post(0);
    }
}

void DynamicQuestInfo::_CampaignFailedFinished() {
    L2_GUARD_FUNCTION();
    CCampaign* const previous = m_pCampaign;
    CCampaign* const next = previous->GetNextCampaignOnFailed();
    const State nextState = next != nullptr ? PERIOD_WAIT : PERIOD_SLEEP;
    if (_ChangeState(PERIOD_FAILED, nextState, nullptr) && next != nullptr) {
        (new CDelayedCampaignStarter(previous, next))->Post(0);
    }
}

void DynamicQuestInfo::_UserTryRequestFailedHTML(User& user) {
    L2_GUARD_FUNCTION();
    m_pCampaign->OnRequestFailedHTML(user);
}

void DynamicQuestInfo::_TryAcceptCampaign(User& user) {
    L2_GUARD_FUNCTION();
    m_pCampaign->TryAccept(user);
}

void DynamicQuestInfo::_TryReceiveReward(User& user) {
    L2_GUARD_FUNCTION();
    m_pCampaign->OnRequestReward(user);
}

void DynamicQuestInfo::__async_AsyncStartDynamicQuest(CExecutionArgumentPad*) {
    L2_GUARD_FUNCTION();
    if (DynamicQuestStateHandler* const handler = GetStateHandler()) {
        handler->_StartDynamicQuest(*this);
    }
}

void DynamicQuestInfo::__async_AsyncStopDynamicQuest(CExecutionArgumentPad*) {
    L2_GUARD_FUNCTION();
    if (DynamicQuestStateHandler* const handler = GetStateHandler()) {
        handler->_StopDynamicQuest(*this);
    }
}

void DynamicQuestInfo::__async_AsyncContinueCampaign(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();
    CCampaign* const campaign = ReadExecutionArgument<CCampaign*>(argument, 8);
    if (campaign == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] null campaign!",
        //     L"DynamicQuest.cpp",
        //     0x8d5)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] null campaign!",
        //     L"DynamicQuest.cpp",
        //     0x8d5)
        L2_LOG_ERROR(
            Log, "Dynamic quest %d: campaign continuation received a null campaign; the continuation request is ignored.",
            m_pDynamicQuest != nullptr ? m_pDynamicQuest->GetId() : 0
        );
        return;
    }
    if (DynamicQuestStateHandler* const handler = GetStateHandler()) {
        handler->_ContinueCampaign(*this, campaign);
    }
}

void DynamicQuestInfo::__async_AsyncChangeCampaign(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();
    const int campaignId = ReadExecutionArgument<int>(argument, 8);
    for (CCampaign* campaign = m_pDynamicQuest->GetFirstCampaign(); campaign != nullptr;
        campaign = campaign->m_pNextCampaign) {
        if (campaign->GetId() == campaignId) {
            if (DynamicQuestStateHandler* const handler = GetStateHandler()) {
                handler->_ChangeCampaign(*this, campaign);
            }
            break;
        }
    }
}

void DynamicQuestInfo::__async_AsyncTryAddPoint(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();
    if (DynamicQuestStateHandler* const handler = GetStateHandler()) {
        const PointParam point(
            ReadExecutionArgument<unsigned int>(argument, 8), ReadExecutionArgument<unsigned int>(argument, 12),
            ReadExecutionArgument<int>(argument, 16), ReadExecutionArgument<DynamicQuestType::PointType>(argument, 20)
        );
        handler->_TryAddPoint(*this, point);
    }
}

void DynamicQuestInfo::__async_AsyncTimerExpired(CExecutionArgumentPad*) {
    L2_GUARD_FUNCTION();
    if (DynamicQuestStateHandler* const handler = GetStateHandler()) {
        handler->_OnTick(*this);
    }
}

void DynamicQuestInfo::__async_AsyncInitNPCMaker(CExecutionArgumentPad*) {
    L2_GUARD_FUNCTION();
    if (m_pCampaign != nullptr) {
        m_pCampaign->ActivateNPCMaker();
        m_pCampaign->SendCampaignInfo();
    }
}

#define L2_DQUEST_USER_ASYNC_BODY(Method, HandlerMethod)                                                                    \
    void DynamicQuestInfo::Method(CExecutionArgumentPad* argument) {                                                        \
        User* const user = ReadExecutionArgument<User*>(argument, 8);                                                       \
        if (DynamicQuestStateHandler* const handler = GetStateHandler()) {                                                  \
            handler->HandlerMethod(*this, *user);                                                                           \
        }                                                                                                                   \
        if (user != nullptr) {                                                                                              \
            L2_RELEASE(user, ObjectRefType::DynamicQuestSerializer);                                                        \
        }                                                                                                                   \
    }

L2_DQUEST_USER_ASYNC_BODY(__async_AsyncUserTryJoinInCampaign, _UserTryStartCampaign)
L2_DQUEST_USER_ASYNC_BODY(__async_AsyncUserTryRequestCampaignHTML, _UserTryRequestCampaignHTML)
L2_DQUEST_USER_ASYNC_BODY(__async_AsyncUserTryAcceptCampaign, _UserTryAcceptCampaign)
L2_DQUEST_USER_ASYNC_BODY(__async_AsyncOnUserAcceptCampaign, _OnUserAcceptCampaign)
L2_DQUEST_USER_ASYNC_BODY(__async_AsyncUserTryReceiveReward, _UserTryReceiveReward)
L2_DQUEST_USER_ASYNC_BODY(__async_AsyncTrySendProgressInfo, _TrySendProgressInfo)
L2_DQUEST_USER_ASYNC_BODY(__async_AsyncTrySendScoreBoard, _TrySendScoreBoard)

#undef L2_DQUEST_USER_ASYNC_BODY

void DynamicQuestInfo::__async_AsyncOnUserReceiveReward(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();
    User* const user = ReadExecutionArgument<User*>(argument, 8);
    _OnReceiveReward(*user);
    if (user != nullptr) {
        L2_RELEASE(user, ObjectRefType::DynamicQuestSerializer);
    }
}

void DynamicQuestInfo::__async_AsyncResetUserGoalMap(CExecutionArgumentPad* argument) {
    L2_GUARD_FUNCTION();
    User* const user = ReadExecutionArgument<User*>(argument, 8);
    if (user == nullptr) {
        return;
    }
    const unsigned int userDbId = user->GetDBID();
    for (const auto& goal : m_pCampaign->m_GoalMap) {
        m_UserGoalMap.erase(UserGoalKey(userDbId, goal.first));
    }
}

void DynamicQuestInfo::_SetCampaign(CCampaign* campaign) {
    L2_GUARD_FUNCTION();
    m_pCampaign = campaign;
    m_pDynamicQuest = campaign->GetDynamicQuest();
    campaign->m_pProgressInfo = this;
    _OnSetCamapaign();
}

void DynamicQuestInfo::TimerExpired(int id) {
    L2_GUARD_FUNCTION();
    if (id == 0) {
        __async_arg_AsyncTimerExpired argument;
        s->AddExecutable(
            this, [](DynamicQuestInfo* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncTimerExpired(pad); },
            &argument, L"AsyncTimerExpired"
        );
    }
    if (!IsSleep()) {
        AddTimer(5000, 0);
    }
}

void DynamicQuestInfo::TryStart() {
    L2_GUARD_FUNCTION();
    __async_arg_AsyncStartDynamicQuest argument;
    s->AddExecutable(
        this, [](DynamicQuestInfo* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncStartDynamicQuest(pad); },
        &argument, L"AsyncStartDynamicQuest"
    );
}

void DynamicQuestInfo::TryStop() {
    L2_GUARD_FUNCTION();
    __async_arg_AsyncStopDynamicQuest argument;
    s->AddExecutable(
        this, [](DynamicQuestInfo* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncStopDynamicQuest(pad); },
        &argument, L"AsyncStopDynamicQuest"
    );
}

void DynamicQuestInfo::TryContinue(CCampaign* campaign) {
    L2_GUARD_FUNCTION();
    __async_arg_AsyncContinueCampaign argument(campaign);
    s->AddExecutable(
        this, [](DynamicQuestInfo* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncContinueCampaign(pad); },
        &argument, L"AsyncContinueCampaign"
    );
}

void DynamicQuestInfo::TryChangeCampaign(int campaignId) {
    L2_GUARD_FUNCTION();
    __async_arg_AsyncChangeCampaign argument(campaignId);
    s->AddExecutable(
        this, [](DynamicQuestInfo* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncChangeCampaign(pad); },
        &argument, L"AsyncChangeCampaign"
    );
}

void DynamicQuestInfo::TryAddPoint(User& user, unsigned int goalId, int point, DynamicQuestType::PointType pointType) {
    L2_GUARD_FUNCTION();
    if (!m_pDynamicQuest->CanExecutedByUser(user)) {
        return;
    }
    __async_arg_AsyncTryAddPoint argument(user.GetDBID(), goalId, point, pointType);
    s->AddExecutable(
        this, [](DynamicQuestInfo* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncTryAddPoint(pad); }, &argument,
        L"AsyncTryAddPoint"
    );
}

#define L2_DQUEST_ENQUEUE_USER(Method, ArgumentType, AsyncMethod, Name)                                                     \
    void DynamicQuestInfo::Method(User& user) {                                                                             \
        L2_GUARD_FUNCTION();                                                                                                \
        L2_ADD_REF(&(user), ObjectRefType::DynamicQuestSerializer);                                                         \
        ArgumentType argument(&user);                                                                                       \
        s->AddExecutable(                                                                                                   \
            this, [](DynamicQuestInfo* owner, CExecutionArgumentPad* pad) { owner->AsyncMethod(pad); }, &argument, Name     \
        );                                                                                                                  \
    }

L2_DQUEST_ENQUEUE_USER(
    UserTryJoinInCampaign,
    __async_arg_AsyncUserTryJoinInCampaign,
    __async_AsyncUserTryJoinInCampaign,
    L"AsyncUserTryJoinInCampaign"
)
L2_DQUEST_ENQUEUE_USER(
    UserTryRequestCampaignHTML,
    __async_arg_AsyncUserTryRequestCampaignHTML,
    __async_AsyncUserTryRequestCampaignHTML,
    L"AsyncUserTryRequestCampaignHTML"
)
L2_DQUEST_ENQUEUE_USER(
    TrySendScoreBoard, __async_arg_AsyncTrySendScoreBoard, __async_AsyncTrySendScoreBoard, L"AsyncTrySendScoreBoard"
)
L2_DQUEST_ENQUEUE_USER(
    OnAcceptCampaign, __async_arg_AsyncOnUserAcceptCampaign, __async_AsyncOnUserAcceptCampaign, L"AsyncOnUserAcceptCampaign"
)
L2_DQUEST_ENQUEUE_USER(
    OnFinishCampaign, __async_arg_AsyncOnUserReceiveReward, __async_AsyncOnUserReceiveReward, L"AsyncOnUserReceiveReward"
)

#undef L2_DQUEST_ENQUEUE_USER

void DynamicQuestInfo::TryReceiveReward(User& user) {
    if (!m_pDynamicQuest->CanExecutedByUser(user)) {
        return;
    }
    L2_ADD_REF(&(user), ObjectRefType::DynamicQuestSerializer);
    __async_arg_AsyncUserTryReceiveReward argument(&user);
    s->AddExecutable(
        this, [](DynamicQuestInfo* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncUserTryReceiveReward(pad); },
        &argument, L"AsyncUserTryReceiveReward"
    );
}

void DynamicQuestInfo::TrySendProgressInfo(User& user) {
    L2_GUARD_FUNCTION();
    if (m_pDynamicQuest->CanExecutedByUser(user) &&
        m_pDynamicQuest->IsValid(*m_pCampaign, m_pCampaign->m_StartConditionList, user)) {
        L2_ADD_REF(&(user), ObjectRefType::DynamicQuestSerializer);
        __async_arg_AsyncTrySendProgressInfo argument(&user);
        s->AddExecutable(
            this, [](DynamicQuestInfo* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncTrySendProgressInfo(pad); },
            &argument, L"AsyncTrySendProgressInfo"
        );
        return;
    }
    OutOfRangeOnSendProgressInfo(user);
}

void DynamicQuestInfo::InitNPCMaker() {
    L2_GUARD_FUNCTION();
    __async_arg_AsyncInitNPCMaker argument;
    s->AddExecutable(
        this, [](DynamicQuestInfo* owner, CExecutionArgumentPad* pad) { owner->__async_AsyncInitNPCMaker(pad); }, &argument,
        L"AsyncInitNPCMaker"
    );
}

DynamicQuestStateHandler::~DynamicQuestStateHandler() = default;
DynamicQuestState_Sleep::~DynamicQuestState_Sleep() = default;
DynamicQuestState_Wait::~DynamicQuestState_Wait() = default;
DynamicQuestState_Working::~DynamicQuestState_Working() = default;
DynamicQuestState_End::~DynamicQuestState_End() = default;
DynamicQuestState_Reward::~DynamicQuestState_Reward() = default;
DynamicQuestState_Failed::~DynamicQuestState_Failed() = default;

void DynamicQuestState_Sleep::_OnStart(DynamicQuestInfo& info) { info._OnCampaignFinished(); }

void DynamicQuestState_Sleep::_OnTick(DynamicQuestInfo& info) { NoOp(info); }

void DynamicQuestState_Sleep::_OnEnd(DynamicQuestInfo& info) { NoOp(info); }

bool DynamicQuestState_Sleep::CanExecutedByCondition() { return true; }

int DynamicQuestState_Sleep::GetTimeLeft(DynamicQuestInfo&) { return 0; }

void DynamicQuestState_Sleep::_StartDynamicQuest(DynamicQuestInfo& info) {
    L2_GUARD_FUNCTION();
    info._StartDynamicQuest();
}

void DynamicQuestState_Sleep::_StopDynamicQuest(DynamicQuestInfo& info) { NoOp(info); }

void DynamicQuestState_Sleep::_ContinueCampaign(DynamicQuestInfo& info, CCampaign* campaign) { NoOp(info, campaign); }

void DynamicQuestState_Sleep::_ChangeCampaign(DynamicQuestInfo& info, CCampaign* campaign) {
    L2_GUARD_FUNCTION();
    info._ChangeCampaign(DynamicQuestInfo::PERIOD_SLEEP, campaign);
}

void DynamicQuestState_Sleep::_UserTryStartCampaign(DynamicQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    info._UserTryStartAndJoinInDynamicQuest(user);
}

void DynamicQuestState_Sleep::_UserTryRequestCampaignHTML(DynamicQuestInfo& info, User& user) { NoOp(info, user); }

void DynamicQuestState_Sleep::_UserTryAcceptCampaign(DynamicQuestInfo& info, User& user) { NoOp(info, user); }

void DynamicQuestState_Sleep::_OnUserAcceptCampaign(DynamicQuestInfo& info, User& user) { NoOp(info, user); }

void DynamicQuestState_Sleep::_UserTryReceiveReward(DynamicQuestInfo& info, User& user) { NoOp(info, user); }

void DynamicQuestState_Sleep::_TryAddPoint(DynamicQuestInfo& info, const DynamicQuestInfo::PointParam& point) {
    NoOp(info, point);
}

void DynamicQuestState_Sleep::_TrySendProgressInfo(DynamicQuestInfo& info, User& user) { NoOp(info, user); }

void DynamicQuestState_Sleep::_TrySendScoreBoard(DynamicQuestInfo& info, User& user) { NoOp(info, user); }

bool DynamicQuestState_Wait::CanExecutedByCondition() { return true; }

void DynamicQuestState_Wait::_StartDynamicQuest(DynamicQuestInfo& info) { NoOp(info); }

void DynamicQuestState_Wait::_StopDynamicQuest(DynamicQuestInfo& info) { NoOp(info); }

void DynamicQuestState_Wait::_ContinueCampaign(DynamicQuestInfo& info, CCampaign* campaign) {
    L2_GUARD_FUNCTION();
    info._ContinueCampaign(campaign);
}

void DynamicQuestState_Wait::_ChangeCampaign(DynamicQuestInfo& info, CCampaign* campaign) {
    L2_GUARD_FUNCTION();
    info._ChangeCampaign(DynamicQuestInfo::PERIOD_WAIT, campaign);
}

void DynamicQuestState_Wait::_UserTryStartCampaign(DynamicQuestInfo& info, User& user) { NoOp(info, user); }

void DynamicQuestState_Working::_OnStart(DynamicQuestInfo& info) { info._OnCampaignStarted(); }

void DynamicQuestState_Working::_OnTick(DynamicQuestInfo& info) {
    L2_GUARD_FUNCTION();
    if (GetTimeLeft(info) < 1) {
        info._CampaignFailed();
        return;
    }
    info._MakeSnapshot(DynamicQuestType::DURING_PROGRESS, false);
}

void DynamicQuestState_Working::_OnEnd(DynamicQuestInfo& info) { info._OnCampaignWorkingEnd(); }

bool DynamicQuestState_Working::CanExecutedByCondition() { return false; }

int DynamicQuestState_Working::GetTimeLeft(DynamicQuestInfo& info) {
    L2_GUARD_FUNCTION();
    if (info.m_pCampaign == nullptr) {
        return 0;
    }
    return info.m_pCampaign->m_Duration + info.m_CampaignStartTime - runtime::crt::Time32Now();
}

void DynamicQuestState_Working::_StartDynamicQuest(DynamicQuestInfo& info) { NoOp(info); }

void DynamicQuestState_Working::_StopDynamicQuest(DynamicQuestInfo& info) {
    L2_GUARD_FUNCTION();
    info._CampaignFailed();
    info._OnCampaignStoped();
}

void DynamicQuestState_Working::_ContinueCampaign(DynamicQuestInfo& info, CCampaign* campaign) { NoOp(info, campaign); }

void DynamicQuestState_Working::_ChangeCampaign(DynamicQuestInfo& info, CCampaign* campaign) {
    L2_GUARD_FUNCTION();
    info._ChangeCampaign(DynamicQuestInfo::PERIOD_WORKING, campaign);
}

void DynamicQuestState_Working::_UserTryStartCampaign(DynamicQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    info._TryNoticeCampaignStarted(user);
}

void DynamicQuestState_Working::_UserTryRequestCampaignHTML(DynamicQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    info._UserTryRequestInquiryHTML(user);
}

void DynamicQuestState_Working::_UserTryAcceptCampaign(DynamicQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    info._TryAcceptCampaign(user);
}

void DynamicQuestState_Working::_OnUserAcceptCampaign(DynamicQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    info._OnAcceptCampaign(user);
}

void DynamicQuestState_Working::_UserTryReceiveReward(DynamicQuestInfo& info, User& user) { NoOp(info, user); }

void DynamicQuestState_Working::_TryAddPoint(DynamicQuestInfo& info, const DynamicQuestInfo::PointParam& point) {
    L2_GUARD_FUNCTION();
    info._TryAddPoint(point);
}

void DynamicQuestState_Working::_TrySendProgressInfo(DynamicQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    info._SendProgressInfo(user);
}

void DynamicQuestState_Working::_TrySendScoreBoard(DynamicQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    info._TrySendScoreBoardOnWorking(user);
}

int DynamicQuestState_End::GetTimeLeft(DynamicQuestInfo& info) {
    L2_GUARD_FUNCTION();
    if (info.m_pDynamicQuest == nullptr) {
        return 0;
    }
    return info.m_pDynamicQuest->GetRewardPeriod() + info.m_CampaignWorkingEndTime - runtime::crt::Time32Now();
}

void DynamicQuestState_Reward::_OnStart(DynamicQuestInfo& info) { info._OnCampaignSuccess(); }

void DynamicQuestState_Reward::_OnTick(DynamicQuestInfo& info) {
    L2_GUARD_FUNCTION();
    if (GetTimeLeft(info) < 1) {
        info._RewardFinished();
        return;
    }
    info._MakeSnapshot(DynamicQuestType::REWARD_START, false);
    info._MakeSnapshot(DynamicQuestType::REWARD_RECEIVED, true);
}

void DynamicQuestState_Reward::_OnEnd(DynamicQuestInfo& info) { NoOp(info); }

bool DynamicQuestState_Reward::CanExecutedByCondition() { return false; }

void DynamicQuestState_Reward::_StartDynamicQuest(DynamicQuestInfo& info) { NoOp(info); }

void DynamicQuestState_Reward::_StopDynamicQuest(DynamicQuestInfo& info) { NoOp(info); }

void DynamicQuestState_Reward::_ContinueCampaign(DynamicQuestInfo& info, CCampaign* campaign) { NoOp(info, campaign); }

void DynamicQuestState_Reward::_ChangeCampaign(DynamicQuestInfo& info, CCampaign* campaign) {
    L2_GUARD_FUNCTION();
    info._ChangeCampaign(DynamicQuestInfo::PERIOD_REWARD, campaign);
}

void DynamicQuestState_Reward::_UserTryStartCampaign(DynamicQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    info._TryNoticeRewardPeriod(user);
}

void DynamicQuestState_Reward::_UserTryRequestCampaignHTML(DynamicQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    info._UserTryRequestRewardHTML(user);
}

void DynamicQuestState_Reward::_UserTryAcceptCampaign(DynamicQuestInfo& info, User& user) { NoOp(info, user); }

void DynamicQuestState_Reward::_OnUserAcceptCampaign(DynamicQuestInfo& info, User& user) { NoOp(info, user); }

void DynamicQuestState_Reward::_UserTryReceiveReward(DynamicQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    info._TryReceiveReward(user);
}

void DynamicQuestState_Reward::_TryAddPoint(DynamicQuestInfo& info, const DynamicQuestInfo::PointParam& point) {
    NoOp(info, point);
}

void DynamicQuestState_Reward::_TrySendProgressInfo(DynamicQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    info._TryNoticeRewardPeriod(user);
}

void DynamicQuestState_Reward::_TrySendScoreBoard(DynamicQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    info._TrySendScoreBoard(user);
}

void DynamicQuestState_Failed::_OnStart(DynamicQuestInfo& info) { info._OnCampaignFailed(); }

void DynamicQuestState_Failed::_OnTick(DynamicQuestInfo& info) {
    L2_GUARD_FUNCTION();
    if (GetTimeLeft(info) < 1) {
        info._CampaignFailedFinished();
        return;
    }
    info._MakeSnapshot(DynamicQuestType::FAILED, false);
}

void DynamicQuestState_Failed::_OnEnd(DynamicQuestInfo& info) { NoOp(info); }

bool DynamicQuestState_Failed::CanExecutedByCondition() { return false; }

void DynamicQuestState_Failed::_StartDynamicQuest(DynamicQuestInfo& info) { NoOp(info); }

void DynamicQuestState_Failed::_StopDynamicQuest(DynamicQuestInfo& info) { NoOp(info); }

void DynamicQuestState_Failed::_ContinueCampaign(DynamicQuestInfo& info, CCampaign* campaign) { NoOp(info, campaign); }

void DynamicQuestState_Failed::_ChangeCampaign(DynamicQuestInfo& info, CCampaign* campaign) {
    L2_GUARD_FUNCTION();
    info._ChangeCampaign(DynamicQuestInfo::PERIOD_FAILED, campaign);
}

void DynamicQuestState_Failed::_UserTryStartCampaign(DynamicQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    info._TryNoticeCampaignFailed(user);
}

void DynamicQuestState_Failed::_UserTryRequestCampaignHTML(DynamicQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    info._UserTryRequestFailedHTML(user);
}

void DynamicQuestState_Failed::_UserTryAcceptCampaign(DynamicQuestInfo& info, User& user) { NoOp(info, user); }

void DynamicQuestState_Failed::_OnUserAcceptCampaign(DynamicQuestInfo& info, User& user) { NoOp(info, user); }

void DynamicQuestState_Failed::_UserTryReceiveReward(DynamicQuestInfo& info, User& user) { NoOp(info, user); }

void DynamicQuestState_Failed::_TryAddPoint(DynamicQuestInfo& info, const DynamicQuestInfo::PointParam& point) {
    NoOp(info, point);
}

void DynamicQuestState_Failed::_TrySendProgressInfo(DynamicQuestInfo& info, User& user) {
    L2_GUARD_FUNCTION();
    info._NoticeCampaignFailed(user);
}

void DynamicQuestState_Failed::_TrySendScoreBoard(DynamicQuestInfo& info, User& user) { NoOp(info, user); }

namespace DynamicQuestState {

DynamicQuestStateHandler* Handler[DynamicQuestInfo::PERIOD_MAX]{};

void Init() {
    L2_GUARD_FUNCTION();
    Handler[DynamicQuestInfo::PERIOD_SLEEP] = new DynamicQuestState_Sleep();
    Handler[DynamicQuestInfo::PERIOD_WORKING] = new DynamicQuestState_Working();
    Handler[DynamicQuestInfo::PERIOD_REWARD] = new DynamicQuestState_Reward();
    Handler[DynamicQuestInfo::PERIOD_WAIT] = new DynamicQuestState_Wait();
    Handler[DynamicQuestInfo::PERIOD_FAILED] = new DynamicQuestState_Failed();
}

void Clear() {
    L2_GUARD_FUNCTION();
    for (DynamicQuestStateHandler* const handler : Handler) {
        delete handler;
    }
}

} // namespace DynamicQuestState

// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     L2ServerMemory::kDynamicQuestManagerClassId ==
//     DynamicQuestManager::kMemoryClassId)

L2_DEFINE_OBJECT_CLASS_ID(DynamicQuestManager)

L2_DEFINE_OBJECT_CLASS_NAME(DynamicQuestManager, m_pClassName_DynamicQuestManager)

L2_DEFINE_OBJECT_ALLOCATOR(DynamicQuestManager, __GetClassName())

L2_DEFINE_OBJECT_NEW(DynamicQuestManager)

DynamicQuestManager::DynamicQuestManager() :
    DefaultSetting{},
    m_pDefaultFeedback{},
    m_LoadFinished(0),
    m_SerialId(0),
    m_nLastPickStartTime(0),
    m_alignment194To198{},
    m_nLastPickRandomNo(0.0),
    m_nLastPickPrevProb(0.0) {
    DynamicQuestState::Init();
    m_pDefaultFeedback[0] = nullptr;
}

DynamicQuestManager::~DynamicQuestManager() {
    L2_GUARD_FUNCTION();
    DynamicQuestState::Clear();

    for (auto& entry : m_DynamicQuestMap) {
        delete entry.second;
    }
    m_DynamicQuestMap.clear();
    m_DynamicContentsMap.clear();
    m_ZoneQuestMap.clear();

    for (OnGoingData* const data : m_SleepingDCList) {
        if (data == nullptr) {
            continue;
        }
        if (data->m_pProgressInfo != nullptr) {
            L2_RELEASE(data->m_pProgressInfo, ObjectRefType::CreateDelete);
            data->m_pProgressInfo = nullptr;
        }
        delete data;
    }
    m_SleepingDCList.clear();

    for (OnGoingData* const data : m_OnGoingList) {
        if (data == nullptr) {
            continue;
        }
        if (data->m_pProgressInfo != nullptr) {
            L2_RELEASE(data->m_pProgressInfo, ObjectRefType::CreateDelete);
            data->m_pProgressInfo = nullptr;
        }
        delete data;
    }
    m_OnGoingList.clear();
}

L2_DEFINE_OBJECT_RELEASE(DynamicQuestManager)

DynamicQuestManager* DynamicQuestManager::Inst() {
    static DynamicQuestManager instance;
    return &instance;
}

void DynamicQuestManager::TimerExpired(int) {
    L2_GUARD_FUNCTION();
    CheckExecution(false, 0);
    AddTimer(900000, 0);
}

void DynamicQuestManager::Init() {
    L2_GUARD_FUNCTION();

    if (g_L2ServerType == L2ServerType::WORLDSERVER) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO ERROR] Do not read dynamicquest")
        // L2_LOG_ERROR(Log,
        //     L"[NO ERROR] Do not read dynamicquest")
        L2_LOG_INFO(Log, "Dynamic quests are not initialized in WORLDSERVER mode; script loading is skipped.");
        return;
    }

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\dynamicquest.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/DynamicQuest.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from dynamicquest file failed",
        //     L"DynamicQuest.cpp",
        //     0xf96)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from dynamicquest file failed",
        //     L"DynamicQuest.cpp",
        //     0xf96)
        L2_LOG_ERROR(
            Log, "Cannot read dynamic-quest script '%s'; dynamic-quest initialization stops.", WideToUtf8(kFileName).c_str()
        );
        return;
    }

    CParserForDynamicQuest parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForDynamicQuest.create failed",
        //     L"DynamicQuest.cpp",
        //     3999)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForDynamicQuest.create failed",
        //     L"DynamicQuest.cpp",
        //     3999)
        L2_LOG_ERROR(Log, "Cannot create the DynamicQuest.txt parser; dynamic-quest initialization stops.");
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing dynamicquest file failed after line[%d]",
        //     L"DynamicQuest.cpp",
        //     0xfac,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing dynamicquest file failed after line[%d]",
        //     L"DynamicQuest.cpp",
        //     0xfac,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log,
            "Cannot parse DynamicQuest.txt after lexer line %lld; parsed definitions are retained, but scheduling-list "
            "setup for this load is skipped.",
            static_cast<long long>(parser.m_lexer.line)
        );
        return;
    }

    m_OnGoingList.push_back(new OnGoingData(-1, nullptr));

    for (const auto& entry : m_ZoneQuestMap) {
        m_OnGoingList.push_back(new OnGoingData(entry.first, entry.second));
    }

    for (const auto& entry : m_DynamicContentsMap) {
        const unsigned int index =
            static_cast<unsigned int>(genrand64_real2() * static_cast<double>(m_SleepingDCList.size() + 1));
        InsertToSleepingDCList(new OnGoingData(entry.first, entry.second), index);
    }
}

void DynamicQuestManager::Load() {
    L2_GUARD_FUNCTION();
    db.RequestLoadAllDynamicQuest();
}

void DynamicQuestManager::OnLoaded(const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    int count = 0;
    const unsigned char* cursor = ::Disassemble(packet, "d", &count);
    for (int index = 0; index < count; ++index) {
        int dynamicQuestId = 0;
        int lastExecutedTime = 0;
        cursor = ::Disassemble(cursor, "dd", &dynamicQuestId, &lastExecutedTime);
        DynamicQuestInfo* const info = GetProgressInfo(dynamicQuestId);
        if (info == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid dynamic quest id[%d]",
            //     L"DynamicQuest.cpp",
            //     0x1004,
            //     dynamicQuestId)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid dynamic quest id[%d]",
            //     L"DynamicQuest.cpp",
            //     0x1004,
            //     dynamicQuestId)
            L2_LOG_WARN(
                Log,
                "Dynamic-quest DB row %lld of %d, quest %d: no progress instance exists; last-executed timestamp %d is "
                "ignored and other rows continue loading.",
                static_cast<long long>(index) + 1, count, dynamicQuestId, lastExecutedTime
            );
            continue;
        }
        info->Init(lastExecutedTime);
    }

    runtime::raw_atomic::ExchangeI32(&m_LoadFinished, 1);
    AddTimer(5000, 0);
}

void DynamicQuestManager::InitNPCMaker() {
    L2_GUARD_FUNCTION();
    if (!Usable()) {
        return;
    }
    for (OnGoingData* const data : m_OnGoingList) {
        if (data->m_pProgressInfo == nullptr) {
            continue;
        }
        if (!data->m_pProgressInfo->IsSleep()) {
            data->m_pProgressInfo->InitNPCMaker();
        }
    }
}

void DynamicQuestManager::ShowGoal(int dynamicQuestId, int campaignId, CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        return;
    }

    const auto questPosition = m_DynamicQuestMap.find(dynamicQuestId);
    if (questPosition == m_DynamicQuestMap.end() || questPosition->second == nullptr ||
        questPosition->second->m_pCampaignData == nullptr) {
        return;
    }

    CCampaign* campaign = questPosition->second->m_pCampaignData->m_pFirstCampaign;
    CCampaign* const last = questPosition->second->m_pCampaignData->GetLastCampaign();
    if (campaign == nullptr || last == nullptr) {
        return;
    }

    while (campaign->GetId() != campaignId) {
        if (campaign == last) {
            return;
        }
        campaign = campaign->GetNextCampaignOnSuccess();
        if (campaign == nullptr) {
            return;
        }
    }
    campaign->ShowGoal(socket);
}

bool DynamicQuestManager::UserPacketHandler(CSocket* socket, const unsigned char* packet) {
    // Current L2Server 0x00709904.
    L2_GUARD_FUNCTION();
    DynamicQuestManager* const manager = Inst();
    if (!manager->Usable()) {
        return false;
    }

    CUserSocket* const userSocket = socket != nullptr ? socket->CastUserSocket() : nullptr;
    User* const user = userSocket != nullptr ? userSocket->GetUser() : nullptr;
    if (user == nullptr) {
        return true;
    }

    char requestType = 0;
    std::pair<int, int> ids{};
    ::Disassemble(packet, "cdd", &requestType, &ids.first, &ids.second);
    switch (requestType) {
        case 2:
            manager->OnRequestProgressInfo(*user, ids);
            break;
        case 3:
            manager->OnRequestRanking(*user, ids);
            break;
        case 4:
            manager->OnClientRequestHTML(*user, ids);
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid client request type[%d]. user[%s]",
            //     L"DynamicQuest.cpp",
            //     0x1099,
            //     static_cast<int>(requestType),
            //     user->GetName())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid client request type[%d]. user[%s]",
            //     L"DynamicQuest.cpp",
            //     0x1099,
            //     static_cast<int>(requestType),
            //     user->GetName())
            // L2_LOG_WARN(Log,
            //     "Ignoring dynamic-quest client request type %d (byte 0x%02X) from character '%s' (DBID %u), quest %d,
            //     campaign %d, supported types are 2, 3 and 4.", static_cast<int>(requestType), static_cast<unsigned
            //     int>(static_cast<unsigned char>(requestType)), WideToUtf8(user->GetName()).c_str(), static_cast<unsigned
            //     int>(user->GetDBID()), ids.first, ids.second)
            L2_LOG_WARN(
                Log,
                "Ignoring dynamic-quest client request type %d (byte 0x%02X) from character '%s' (DBID %u), quest %d, "
                "campaign %d; supported types are 2, 3 and 4.",
                static_cast<int>(requestType), static_cast<unsigned int>(static_cast<unsigned char>(requestType)),
                Utf16ToUtf8(user->GetName()).c_str(), static_cast<unsigned int>(user->GetDBID()), ids.first, ids.second
            );
            break;
    }
    return false;
}

bool DynamicQuestManager::DBPacketHandler(CDBSocket*, const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    char jobType = 0;
    const unsigned char* const cursor = ::Disassemble(packet, "c", &jobType);
    if (jobType == 0) {
        Inst()->OnLoaded(cursor);
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] invalid job type[%d]",
        //     L"DynamicQuest.cpp",
        //     0x10b3,
        //     static_cast<int>(jobType))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] invalid job type[%d]",
        //     L"DynamicQuest.cpp",
        //     0x10b3,
        //     static_cast<int>(jobType))
        L2_LOG_ERROR(
            Log, "Ignoring dynamic-quest DB job type %d (byte 0x%02X); only load-result type 0 is supported.",
            static_cast<int>(jobType), static_cast<unsigned int>(static_cast<unsigned char>(jobType))
        );
    }
    return false;
}

DynamicQuestManager::OnGoingData::OnGoingData(int dynamicQuestId, DynamicQuest* dynamicQuest) :
    m_DQuestId(dynamicQuestId),
    m_alignment04To08{},
    m_pProgressInfo(dynamicQuest != nullptr ? dynamicQuest->MakeInfo() : nullptr) {
    L2_GUARD_FUNCTION();
}

bool DynamicQuestManager::AddDynamicQuest(DynamicQuest* quest, DynamicQuestType::Enum type) {
    L2_GUARD_FUNCTION();
    if (quest == nullptr) {
        return false;
    }

    const auto inserted = m_DynamicQuestMap.emplace(quest->GetId(), quest);
    if (!inserted.second) {
        return false;
    }

    DynamicQuestMap& typedMap = type == DynamicQuestType::DYNAMIC_CONTENTS ? m_DynamicContentsMap : m_ZoneQuestMap;
    typedMap.emplace(quest->GetId(), quest);
    return true;
}

int DynamicQuestManager::OnGoingQuestID() {
    L2_GUARD_FUNCTION();
    if (m_OnGoingList.empty() || m_OnGoingList[0] == nullptr) {
        return 0;
    }
    return m_OnGoingList[0]->m_DQuestId;
}

bool DynamicQuestManager::CanExecuteDQuest() {
    L2_GUARD_FUNCTION();
    if (m_OnGoingList.empty() || m_OnGoingList[0] == nullptr || m_OnGoingList[0]->m_pProgressInfo == nullptr) {
        return true;
    }
    return m_OnGoingList[0]->m_pProgressInfo->IsSleep();
}

void DynamicQuestManager::InsertToSleepingDCList(OnGoingData* data, unsigned int index) {
    L2_GUARD_FUNCTION();
    auto position = m_SleepingDCList.begin();
    const auto distance = std::min<std::size_t>(index, m_SleepingDCList.size());
    std::advance(position, static_cast<std::ptrdiff_t>(distance));
    m_SleepingDCList.insert(position, data);
}

void DynamicQuestManager::CheckExecution(bool force, int dynamicQuestId) {
    L2_GUARD_FUNCTION();
    if (m_OnGoingList.empty() || !CanExecuteDQuest()) {
        return;
    }

    OnGoingData* const active = m_OnGoingList[0];
    if (active == nullptr) {
        return;
    }

    if (active->m_DQuestId != -1) {
        OnGoingData* const sleeping = new OnGoingData(*active);
        const unsigned int index =
            static_cast<unsigned int>(genrand64_real2() * static_cast<double>(m_SleepingDCList.size() + 1));
        InsertToSleepingDCList(sleeping, index);
        active->m_DQuestId = -1;
        active->m_pProgressInfo = nullptr;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    for (auto position = m_SleepingDCList.begin(); position != m_SleepingDCList.end(); ++position) {
        OnGoingData* const data = *position;
        if (data == nullptr || data->m_pProgressInfo == nullptr) {
            continue;
        }
        if (!force && !data->m_pProgressInfo->CanExecutedByCondition(now)) {
            continue;
        }
        if (dynamicQuestId > 0 && data->m_DQuestId != dynamicQuestId) {
            continue;
        }

        active->m_DQuestId = data->m_DQuestId;
        active->m_pProgressInfo = data->m_pProgressInfo;
        active->m_pProgressInfo->TryStart();
        m_SleepingDCList.erase(position);
        delete data;
        break;
    }
}

void DynamicQuestManager::StopDynamicQuest() {
    L2_GUARD_FUNCTION();
    if (!m_OnGoingList.empty() && m_OnGoingList[0] != nullptr && m_OnGoingList[0]->m_pProgressInfo != nullptr) {
        m_OnGoingList[0]->m_pProgressInfo->TryStop();
    }
}

int DynamicQuestManager::MakeSerialId() {
    L2_GUARD_FUNCTION();
    return runtime::raw_atomic::IncrementI32(&m_SerialId);
}

void DynamicQuestManager::OnUserEnterWorld(User& user) {
    L2_GUARD_FUNCTION();
    if (!Usable()) {
        return;
    }
    for (OnGoingData* const data : m_OnGoingList) {
        if (data != nullptr && data->m_pProgressInfo != nullptr) {
            data->m_pProgressInfo->OnUserEnterWorld(user);
        }
    }
}

void DynamicQuestManager::OnUserMove(User& user) {
    L2_GUARD_FUNCTION();
    if (!Usable()) {
        return;
    }
    for (OnGoingData* const data : m_OnGoingList) {
        if (data != nullptr && data->m_pProgressInfo != nullptr) {
            data->m_pProgressInfo->OnUserMove(user);
        }
    }
}

void DynamicQuestManager::OnAddPoint(
    User& user, std::pair<int, int>& ids, unsigned int goalId, DynamicQuestType::PointType type, int point
) {
    L2_GUARD_FUNCTION();
    DynamicQuestInfo* const info = GetProgressInfo(ids, false);
    if (info != nullptr) {
        info->TryAddPoint(user, goalId, point, type);
    }
}

void DynamicQuestManager::OnChangeCampaign(int dynamicQuestId, int campaignId) {
    L2_GUARD_FUNCTION();
    if (OnGoingQuestID() != dynamicQuestId) {
        return;
    }
    DynamicQuestInfo* const info = GetProgressInfo(dynamicQuestId);
    if (info != nullptr) {
        info->TryChangeCampaign(campaignId);
    }
}

void DynamicQuestManager::OnAcceptDynamicQuest(User& user, std::pair<int, int>& ids) {
    L2_GUARD_FUNCTION();
    DynamicQuestInfo* const info = GetProgressInfo(ids, false);
    if (info != nullptr) {
        info->TryAcceptCampaign(user);
    }
}

void DynamicQuestManager::OnRequestReward(User& user, std::pair<int, int>& ids) {
    L2_GUARD_FUNCTION();
    DynamicQuestInfo* const info = GetProgressInfo(ids, false);
    if (info != nullptr) {
        info->TryReceiveReward(user);
    }
}

void DynamicQuestManager::OnRequestRanking(User& user, std::pair<int, int>& ids) {
    L2_GUARD_FUNCTION();
    DynamicQuestInfo* const info = GetProgressInfo(ids, false);
    if (info != nullptr) {
        info->TrySendScoreBoard(user);
    }
}

void DynamicQuestManager::OnClientRequestHTML(User& user, std::pair<int, int>& ids) {
    L2_GUARD_FUNCTION();
    DynamicQuestInfo* const info = GetProgressInfo(ids, false);
    if (info != nullptr) {
        info->UserTryRequestCampaignHTML(user);
    }
}

void DynamicQuestManager::OnRequestProgressInfo(User& user, std::pair<int, int>& ids) {
    L2_GUARD_FUNCTION();
    DynamicQuestInfo* const info = GetProgressInfo(ids, false);
    if (info != nullptr) {
        info->TrySendProgressInfo(user);
    }
}

DynamicQuestInfo* DynamicQuestManager::GetProgressInfo(int dynamicQuestId) {
    L2_GUARD_FUNCTION();
    for (OnGoingData* const data : m_OnGoingList) {
        if (data->m_pProgressInfo != nullptr && data->m_DQuestId == dynamicQuestId) {
            return data->m_pProgressInfo;
        }
    }
    for (OnGoingData* const data : m_SleepingDCList) {
        if (data->m_pProgressInfo != nullptr && data->m_DQuestId == dynamicQuestId) {
            return data->m_pProgressInfo;
        }
    }
    return nullptr;
}

DynamicQuestInfo* DynamicQuestManager::GetProgressInfo(std::pair<int, int>& ids, bool ongoingOnly) {
    L2_GUARD_FUNCTION();
    for (OnGoingData* const data : m_OnGoingList) {
        if (data->m_pProgressInfo == nullptr) {
            continue;
        }
        if (!data->m_pProgressInfo->IsSleep() && data->m_DQuestId == ids.first &&
            data->m_pProgressInfo->m_pCampaign->GetId() == ids.second) {
            return data->m_pProgressInfo;
        }
    }
    if (ongoingOnly) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] cannot find progress info[d:%d][c:%d]!",
        //     L"DynamicQuest.cpp",
        //     0x11c2,
        //     ids.first,
        //     ids.second)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] cannot find progress info[d:%d][c:%d]!",
        //     L"DynamicQuest.cpp",
        //     0x11c2,
        //     ids.first,
        //     ids.second)
        L2_LOG_ERROR(
            Log,
            "Cannot find an active progress instance for dynamic quest %d, campaign %d; lookup returns null and campaign "
            "continuation is not scheduled.",
            ids.first, ids.second
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
    }
    return nullptr;
}

int DynamicQuestManager::GetAdjustedGoalRate() {
    L2_GUARD_FUNCTION();
    for (const auto& rate : m_AdjustedGoalRateMap) {
        if (g_nServerUserCount <= rate.first) {
            return rate.second;
        }
    }
    return 0;
}

int DynamicQuestManager::GetAdjustedRewardRate() {
    L2_GUARD_FUNCTION();
    for (const auto& rate : m_AdjustedRewardRateMap) {
        if (g_nServerUserCount <= rate.first) {
            return rate.second;
        }
    }
    return 0;
}

void DynamicQuestManager::SetPickInformation(int startTime, double randomNo, double previousProbability) {
    m_nLastPickStartTime = startTime;
    m_nLastPickRandomNo = randomNo;
    m_nLastPickPrevProb = previousProbability;
}

int DynamicQuestManager::GetGoal(int dynamicQuestId, int goalId) {
    DynamicQuestInfo* const info = GetProgressInfo(dynamicQuestId);
    return info != nullptr ? info->GetGoal(goalId) : 0;
}

int DynamicQuestManager::GetPoint(int dynamicQuestId, int goalId) {
    DynamicQuestInfo* const info = GetProgressInfo(dynamicQuestId);
    return info != nullptr ? info->GetPoint(goalId) : 0;
}

void DynamicQuestManager::OnContinueCampaign(CCampaign* previous, CCampaign* next) {
    L2_GUARD_FUNCTION();
    if (previous == nullptr || next == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] null campaign!",
        //     L"DynamicQuest.cpp",
        //     0x105c)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] null campaign!",
        //     L"DynamicQuest.cpp",
        //     0x105c)
        L2_LOG_ERROR(
            Log,
            "Cannot continue from campaign %d to campaign %d: previous campaign present: %s, next campaign present: %s; no "
            "continuation is scheduled.",
            previous != nullptr ? previous->GetId() : 0, next != nullptr ? next->GetId() : 0,
            previous != nullptr ? "yes" : "no", next != nullptr ? "yes" : "no"
        );
        return;
    }

    DynamicQuest* const quest = previous->GetDynamicQuest();
    std::pair<int, int> ids(quest->GetId(), previous->GetId());
    DynamicQuestInfo* const info = GetProgressInfo(ids, true);
    if (info != nullptr) {
        info->TryContinue(next);
    }
}
