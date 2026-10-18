#include "EventMaker.h"
#include "Shared/src/ScopedLockGuards.h"

#include "CContributeData.h"
#include "Creature.h"
#include "CreatureController.h"
#include "MakerEvent.h"
#include "MultiSell.h"
#include "NPC.h"
#include "NewAction.h"
#include "Pledge.h"
#include "Shared/src/SharedFactory.h"
#include "Skill.h"
#include "User.h"
#include "XmlLoader.h"

#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <string>
#include <string_view>
#include <utility>

namespace {

constexpr wchar_t kEventMakerPath[] = L"..\\Script\\eventmaker.xml";

constexpr std::array<double, 13> kEventDropProbability{
    0.0,
    10.0,
    20.0,
    30.0,
    40.0,
    50.0,
    60.0,
    70.0,
    80.0,
    90.0,
    100.0,
    150.0,
    200.0,
};

bool EqualsIgnoreCase(const std::u16string& left, const char16_t* right) {
    const runtime::CStringLess<std::u16string> less;
    return !less(left, right) && !less(right, left);
}

std::u16string& Attribute(XMLElement* element, const char16_t* name) { return element->m_smapAttr[std::u16string(name)]; }

std::int32_t ToInt32(const std::u16string& value) { return WindowsWtol(Utf16ToUtf8(runtime::StringView(value))); }

std::int64_t ToInt64(const std::u16string& value) {
    const std::string text = Utf16ToUtf8(runtime::StringView(value));
    return static_cast<std::int64_t>(std::strtoll(text.c_str(), nullptr, 10));
}

double ToDouble(const std::u16string& value) {
    const std::string text = Utf16ToUtf8(runtime::StringView(value));
    return std::strtod(text.c_str(), nullptr);
}


// void LogUnknownEventElement(
//     const int line,
//     const std::xwstring& name) {
//     const std::wstring hostName = Utf16ToWide(name)
//     // Log.Add(
//     //     LOG_ERROR,
//     //     L"[%s][%d][EventMaker] unknown element[%s] for EventMaker.",
//     //     kEventMakerSource,
//     //     line,
//     //     hostName.c_str())
//     L2_LOG_ERROR(Log,
//         L"[%s][%d][EventMaker] unknown element[%s] for EventMaker.",
//         kEventMakerSource,
//         line,
//         hostName.c_str())
// }
void LogUnknownEventElement(const int line, const std::u16string& name) {
    // These are kept caller source locations, not XML line numbers.
    const char* section = "EventMaker";
    switch (line) {
        case 0x169:
            section = "Reward/BuffSkills";
            break;
        case 0x1ca:
            section = "Reward/MultisellItems";
            break;
        case 0x1bf:
            section = "Reward/MultisellItems/Item";
            break;
        case 0x12e:
            section = "Reward";
            break;
        default:
            break;
    }
    L2_LOG_ERROR(
        Log, "eventmaker.xml: unexpected element '%s' under %s; this section stops loading.", Utf16ToUtf8(name).c_str(),
        section
    );
}

} // namespace

BuffSkillInfo::BuffSkillInfo(const std::int64_t skillId) : m_nSkillId(skillId) {}

MultisellItem::MultisellItem() : m_nItemClassId(0), m_nAmount(0) {}

MultisellItem::MultisellItem(const int itemClassId, const std::int64_t amount) :
    m_nItemClassId(itemClassId), m_nAmount(amount) {}

MultisellItem& MultisellItem::operator=(const MultisellItem& other) {
    if (this != &other) {
        m_nItemClassId = other.m_nItemClassId;
        m_nAmount = other.m_nAmount;
    }
    return *this;
}

void MultisellItem::Set(const int itemClassId, const std::int64_t amount) {
    m_nItemClassId = itemClassId;
    m_nAmount = amount;
}

MultisellListInfo::MultisellListInfo() {}

MultisellListInfo::MultisellListInfo(MultisellItem outputItem, std::vector<MultisellItem>& priceItems) {
    m_OutputItem = outputItem;
    for (const MultisellItem& priceItem : priceItems) {
        m_vPriceItems.push_back(priceItem);
    }
}

RewardNPCInfo::RewardNPCInfo() {}

RewardNPCInfo::RewardNPCInfo(const int summonTick, std::u16string npcMakerName) : m_nSummonTick(summonTick) {
    m_sNPCMakerName = npcMakerName;
}

RewardBuffSkillsInfo::RewardBuffSkillsInfo() {}

void RewardBuffSkillsInfo::Add(BuffSkillInfo info) { m_vBuffSkills.push_back(info); }

RewardMultisellListInfo::RewardMultisellListInfo() : m_nId(-1) { m_vMultisellList.clear(); }

void RewardMultisellListInfo::Add(MultisellItem outputItem, std::vector<MultisellItem>& priceItems) {
    MultisellListInfo info(outputItem, priceItems);
    m_vMultisellList.push_back(info);
}

CEventMakerRewardInfo::CEventMakerRewardInfo() = default;

CEventMakerRewardInfo::~CEventMakerRewardInfo() = default;

void CEventMakerRewardInfo::SetNPCInfo(RewardNPCInfo info) {
    // Current L2Server 0x0044ae94.
    L2_GUARD_FUNCTION();
    m_NPCInfo.m_nSummonTick = info.m_nSummonTick;
    m_NPCInfo.m_sNPCMakerName = info.m_sNPCMakerName;
}

void CEventMakerRewardInfo::AddBuffSkillsInfo(BuffSkillInfo info) {
    // Current L2Server 0x0044c1c0.
    L2_GUARD_FUNCTION();
    m_BuffSkillsInfo.Add(info);
}

void CEventMakerRewardInfo::AddMultisellListInfo(MultisellListInfo info) {
    // Current L2Server 0x0044df70.
    L2_GUARD_FUNCTION();
    m_MultisellItemsInfo.Add(info.m_OutputItem, info.m_vPriceItems);
}

std::u16string CEventMakerRewardInfo::GetNPCMakerName() const { return m_NPCInfo.m_sNPCMakerName; }

std::vector<BuffSkillInfo> CEventMakerRewardInfo::GetBuffSkillsList() { return m_BuffSkillsInfo.m_vBuffSkills; }

void CEventMakerRewardInfo::MakeMultisellListForManager(CMultiSellList** list) {
    // Current L2Server 0x0044c930. The signed size comparison is always
    // true, so even an empty reward creates an empty list owner.
    L2_GUARD_FUNCTION();
    if (static_cast<int>(m_MultisellItemsInfo.m_vMultisellList.size()) >= 0) {
        CMultiSellList* const result = new CMultiSellList;
        result->m_nId = m_MultisellItemsInfo.m_nId;

        for (const MultisellListInfo& source : m_MultisellItemsInfo.m_vMultisellList) {
            CMultiSellInfo* const info = new CMultiSellInfo;

            MultiSellItemStruct* const output = new MultiSellItemStruct;
            output->ItemClassId = source.m_OutputItem.m_nItemClassId;
            output->Count = source.m_OutputItem.m_nAmount;
            output->Probability = 100.0;
            output->nEnchant = -1;
            info->m_vOutputList.push_back(output);

            for (const MultisellItem& priceSource : source.m_vPriceItems) {
                // Current leaves Probability and nEnchant indeterminate for
                // price entries and assigns only the two physical fields.
                MultiSellItemStruct* const price = new MultiSellItemStruct;
                price->ItemClassId = priceSource.m_nItemClassId;
                price->Count = priceSource.m_nAmount;
                info->m_vInputList.push_back(price);
            }
            result->Add(info);
        }
        *list = result;
    }
}

// Noise: Startup/reload needs a reward summary, not per-skill/per-price dumps and paired banners. Retain the complete old
// diagnostic body. void CEventMakerRewardInfo::PrintRewardInfoLog() {
//     // Current L2Server 0x0044c268.
//     L2_GUARD_FUNCTION()
//     // Log.Add(LOG_ERROR, L"[NO_ERROR]  [Buff SKill List Start]")
//     L2_LOG_ERROR(Log, L"[NO_ERROR]  [Buff SKill List Start]")
//     if (m_BuffSkillsInfo.m_vBuffSkills.empty()) {
//         // Log.Add(LOG_ERROR, L"[NO_ERROR]   No Buff Skill Info")
//         L2_LOG_ERROR(Log, L"[NO_ERROR]   No Buff Skill Info")
//     } else {
//         for (const BuffSkillInfo& info :
//              m_BuffSkillsInfo.m_vBuffSkills) {
//             const std::uint64_t packed =
//                 static_cast<std::uint64_t>(info.m_nSkillId)
//             // Log.Add(
//             //     LOG_ERROR,
//             //     L"[NO_ERROR]   Skill %d, %d, %d",
//             //     runtime::Int32FromBits(
//             //         static_cast<std::uint32_t>(packed >> 32)),
//             //     static_cast<int>((packed >> 16) & UINT64_C(0xffff)),
//             //     static_cast<int>(runtime::Int16FromBits(
//             //         static_cast<std::uint16_t>(packed))))
//             L2_LOG_ERROR(Log,
//                 L"[NO_ERROR]   Skill %d, %d, %d",
//                 runtime::Int32FromBits(
//                     static_cast<std::uint32_t>(packed >> 32)),
//                 static_cast<int>((packed >> 16) & UINT64_C(0xffff)),
//                 static_cast<int>(runtime::Int16FromBits(
//                     static_cast<std::uint16_t>(packed))))
//         }
//     }
//     // Log.Add(LOG_ERROR, L"[NO_ERROR]  [Buff SKill List End]")
//     L2_LOG_ERROR(Log, L"[NO_ERROR]  [Buff SKill List End]")
//     // Log.Add(LOG_ERROR, L"[NO_ERROR]  [Multisell Item List Start]")
//     L2_LOG_ERROR(Log, L"[NO_ERROR]  [Multisell Item List Start]")
//     if (m_MultisellItemsInfo.m_vMultisellList.empty()) {
//         // Log.Add(LOG_ERROR, L"[NO_ERROR]   No Multisell Item Info")
//         L2_LOG_ERROR(Log, L"[NO_ERROR]   No Multisell Item Info")
//     } else {
//         for (const MultisellListInfo& info :
//              m_MultisellItemsInfo.m_vMultisellList) {
//             // Log.Add(
//             //     LOG_ERROR,
//             //     L"[NO_ERROR]   Reward Item : %d, %d",
//             //     info.m_OutputItem.m_nItemClassId,
//             //     runtime::Int32FromBits(
//             //         static_cast<std::uint32_t>(
//             //             info.m_OutputItem.m_nAmount)))
//             L2_LOG_ERROR(Log,
//                 L"[NO_ERROR]   Reward Item : %d, %d",
//                 info.m_OutputItem.m_nItemClassId,
//                 runtime::Int32FromBits(
//                     static_cast<std::uint32_t>(
//                         info.m_OutputItem.m_nAmount)))
//             const std::xvector<MultisellItem> priceItems =
//                 info.m_vPriceItems.
//             for (const MultisellItem& price :
//                  priceItems) {
//                 // Log.Add(
//                 //     LOG_ERROR,
//                 //     L"[NO_ERROR]     Price Item : %d, %d",
//                 //     price.m_nItemClassId,
//                 //     runtime::Int32FromBits(
//                 //         static_cast<std::uint32_t>(
//                 //             price.m_nAmount)))
//                 L2_LOG_ERROR(Log,
//                     L"[NO_ERROR]     Price Item : %d, %d",
//                     price.m_nItemClassId,
//                     runtime::Int32FromBits(
//                         static_cast<std::uint32_t>(
//                             price.m_nAmount)))
//             }
//         }
//     }
//     // Log.Add(LOG_ERROR, L"[NO_ERROR]  [Multisell Item List End]")
//     L2_LOG_ERROR(Log, L"[NO_ERROR]  [Multisell Item List End]")
// }
void CEventMakerRewardInfo::PrintRewardInfoLog() {
    L2_GUARD_FUNCTION();
    L2_LOG_INFO(
        Log, "Event rewards: %llu buff skills, %llu exchange offers (multisell list %d).",
        static_cast<unsigned long long>(m_BuffSkillsInfo.m_vBuffSkills.size()),
        static_cast<unsigned long long>(m_MultisellItemsInfo.m_vMultisellList.size()), m_MultisellItemsInfo.m_nId
    );
}

int CEventMakerDropInfo::GetMaxDropItemAmount() {
    // Current L2Server 0x0044a620.
    L2_GUARD_FUNCTION();
    if (m_vTimeVariables.empty()) {
        return m_nTotalItemAmount;
    }

    std::tm calendar{};
    runtime::crt::LocalTime32(runtime::crt::Time32Now(), calendar);
    for (const TimeVariableInfo& info : m_vTimeVariables) {
        if (info.m_nStartHour <= calendar.tm_hour && calendar.tm_hour < info.m_nEndHour) {
            return info.m_nVariable * m_nTotalItemAmount;
        }
    }
    return 0;
}

void CEventMakerDropInfo::AddDropItemInfo(EventMakerDropItem info) {
    // Current L2Server 0x0044bc30.
    L2_GUARD_FUNCTION();
    m_nTotalItemAmount += info.m_nAmount;
    m_vDropItems.push_back(info);
}

void CEventMakerDropInfo::AddTimeVariableInfo(TimeVariableInfo info) {
    // Current L2Server 0x0044c4bc.
    L2_GUARD_FUNCTION();
    m_nMaxVariables = std::max(m_nMaxVariables, info.m_nVariable);
    m_vTimeVariables.push_back(info);
}

void CEventMakerDropInfo::MakeDropItemInfoOnTickForManager(EventMakerDropItemInfoOnTick& info) {
    // Current L2Server 0x0044bce0.
    L2_GUARD_FUNCTION();
    info.m_vDropItems.reserve(
        m_vDropItems.size() * static_cast<std::size_t>(m_nTotalItemAmount) * static_cast<std::size_t>(m_nMaxVariables)
    );
    info.m_dProbability = m_dProbability;

    for (int variable = 0; variable < m_nMaxVariables; ++variable) {
        for (const EventMakerDropItem& source : m_vDropItems) {
            for (int amount = 0; amount < source.m_nAmount; ++amount) {
                EventMakerDropItem item;
                item.m_nItemClassId = source.m_nItemClassId;
                item.m_nAmount = 1;
                item.m_dProbability = source.m_dProbability;
                info.m_vDropItems.push_back(item);
            }
        }
    }
    info.m_nMaxDropItemAmount = GetMaxDropItemAmount();
}

CEventMaker::CEventMaker(const int eventId) : m_nEventId(eventId) {
    // Current L2Server 0x0044de6c.
    std::fill(std::begin(m_sEventName), std::end(m_sEventName), u'\0');
    m_bActive = false;
    m_bIsRunning = 0;
    m_DropInfo.m_nTick = 1;
    m_DropInfo.m_nTotalItemAmount = 0;
    m_DropInfo.m_nMaxVariables = 0;
    m_DropInfo.m_dProbability = 100.0;
}

CEventMaker::~CEventMaker() = default;

bool CEventMaker::InDropTick(const int tick) {
    // Current L2Server 0x00449fb0.
    L2_GUARD_FUNCTION();
    return m_DropInfo.m_nTick > 0 && m_DropInfo.m_nTick <= tick && tick % m_DropInfo.m_nTick == 0;
}

bool CEventMaker::InSummonNPCTick(const int tick) {
    // Current L2Server 0x0044a06c.
    L2_GUARD_FUNCTION();
    return m_RewardInfo.GetSummonNPCTick() > 0 && m_RewardInfo.GetSummonNPCTick() <= tick &&
        tick % m_RewardInfo.GetSummonNPCTick() == 0;
}

int CEventMaker::GetMaxDropItemAmount() {
    // Current L2Server 0x0044a744.
    L2_GUARD_FUNCTION();
    return m_DropInfo.GetMaxDropItemAmount();
}

std::vector<BuffSkillInfo> CEventMaker::GetBuffSkillsList() { return m_RewardInfo.GetBuffSkillsList(); }

void CEventMaker::MakeDropItemInfoOnTickForManager(EventMakerDropItemInfoOnTick& info) {
    // Current L2Server 0x0044be48.
    L2_GUARD_FUNCTION();
    m_DropInfo.MakeDropItemInfoOnTickForManager(info);
}

void CEventMaker::MakeMultisellListForManager(CMultiSellList** list) {
    // Current L2Server 0x0044caf8.
    L2_GUARD_FUNCTION();
    m_RewardInfo.MakeMultisellListForManager(list);
}

void CEventMaker::Run() {
    // Current L2Server 0x0044af7c.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(&m_bIsRunning, 1);
    if (g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20)) {
        const std::u16string makerName = m_RewardInfo.GetNPCMakerName();
        EnableNpcPosEvent(makerName.c_str());
    }
}

void CEventMaker::Stop() {
    // Current L2Server 0x0044b07c.
    L2_GUARD_FUNCTION();
    runtime::raw_atomic::ExchangeI32(&m_bIsRunning, 0);
    if (g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20)) {
        const std::u16string makerName = m_RewardInfo.GetNPCMakerName();
        DisableNpcPosEvent(makerName.c_str());
    }
}

void CEventMaker::DoTickAction(const int tick) {
    // Current L2Server 0x0044b178.
    L2_GUARD_FUNCTION();
    if (InSummonNPCTick(tick) && g_PledgeTimer.IsNpcServerAlive(CPledgeTimer::NSAT_20)) {
        const std::u16string makerName = m_RewardInfo.GetNPCMakerName();
        EnableNpcPosEvent(makerName.c_str());
    }
}

void CEventMaker::PrintEventInfoLog() {
    // Current L2Server 0x0044c570.
    L2_GUARD_FUNCTION();
    // const std::wstring eventName =
    //     Utf16ToWide(m_sEventName)
    const std::string eventName = Utf16ToUtf8(m_sEventName);
    // Log.Add(LOG_ERROR, L"[NO_ERROR] [[[Event Maker Info Start]]]")
    // Noise: The adjacent event summary already identifies this report. A separate begin/end banner adds no information.
    // L2_LOG_ERROR(Log, L"[NO_ERROR] [[[Event Maker Info Start]]]")
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] Event Id = %d, %s",
    //     m_nEventId,
    //     eventName.c_str())
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR] Event Id = %d, %s",
    //     m_nEventId,
    //     eventName.c_str())
    L2_LOG_INFO(Log, "Selected event %d: '%s'.", m_nEventId, eventName.c_str());
    m_RewardInfo.PrintRewardInfoLog();
    // Log.Add(LOG_ERROR, L"[NO_ERROR] [[[Event Maker Info End]]]")
    // Noise: The adjacent event summary already identifies this report. A separate begin/end banner adds no information.
    // L2_LOG_ERROR(Log, L"[NO_ERROR] [[[Event Maker Info End]]]")
}

bool CEventMaker::LoadReward_NPC(XMLElement* element) {
    // Current L2Server 0x0044d254.
    L2_GUARD_FUNCTION();
    if (element == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][EventMaker] fail to load EventMaker > Reward > NPC node.",
        //     kEventMakerSource,
        //     0x13f)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][EventMaker] fail to load EventMaker > Reward > NPC node.",
        //     kEventMakerSource,
        //     0x13f)
        L2_LOG_ERROR(
            Log, "eventmaker.xml event %d: Reward/NPC is missing; NPC reward configuration is skipped.", m_nEventId
        );
        return false;
    }

    std::u16string makerName = Attribute(element, u"makername");
    const int summonTick = ToInt32(Attribute(element, u"tick"));
    RewardNPCInfo info(summonTick, std::move(makerName));
    m_RewardInfo.SetNPCInfo(info);
    return true;
}

bool CEventMaker::LoadReward_BuffSkills(XMLElement* element) {
    // Current L2Server 0x0044d3fc.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][EventMaker] fail to load EventMaker > Reward > BuffSkills node.",
        //     kEventMakerSource,
        //     0x152)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][EventMaker] fail to load EventMaker > Reward > BuffSkills node.",
        //     kEventMakerSource,
        //     0x152)
        L2_LOG_ERROR(
            Log, "eventmaker.xml event %d: Reward/BuffSkills is missing or empty; buff reward configuration is skipped.",
            m_nEventId
        );
        return false;
    }

    for (XMLElement* child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        if (!EqualsIgnoreCase(child->m_sName, u"Skill")) {
            LogUnknownEventElement(0x169, child->m_sName);
            return false;
        }

        const std::int64_t encoded = g_PrecompiledHeaderDB.Search(Attribute(child, u"name").c_str());
        BuffSkillInfo info(encoded);
        m_RewardInfo.AddBuffSkillsInfo(info);
    }
    return true;
}

bool CEventMaker::LoadDrop(XMLElement* element) {
    // Current L2Server 0x0044d5b8.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty() || element->m_vChild.size() > 4U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][EventMaker] fail to load MakerEvent > Drop. invalid child nodes",
        //     kEventMakerSource,
        //     0x1db)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][EventMaker] fail to load MakerEvent > Drop. invalid child nodes",
        //     kEventMakerSource,
        //     0x1db)
        L2_LOG_ERROR(
            Log,
            "eventmaker.xml event %d: Drop is missing or its child count %llu is outside 1..4; drop configuration is "
            "skipped.",
            m_nEventId, element != nullptr ? static_cast<unsigned long long>(element->m_vChild.size()) : 0ULL
        );
        return false;
    }

    m_DropInfo.m_nTick = ToInt32(Attribute(element, u"tick"));
    for (XMLElement* child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"Items")) {
            m_DropInfo.m_dProbability = ToDouble(Attribute(child, u"dropprob"));
            for (XMLElement* itemElement : child->m_vChild) {
                if (itemElement == nullptr) {
                    continue;
                }
                const int classId = static_cast<int>(g_PrecompiledHeaderDB.Search(Attribute(itemElement, u"name").c_str()));
                if (classId == -1) {
                    continue;
                }
                EventMakerDropItem info;
                info.m_nItemClassId = classId;
                info.m_nAmount = ToInt32(Attribute(itemElement, u"amount"));
                info.m_dProbability = ToDouble(Attribute(itemElement, u"prob"));
                m_DropInfo.AddDropItemInfo(info);
            }
        } else if (EqualsIgnoreCase(child->m_sName, u"TimeVariables")) {
            for (XMLElement* variableElement : child->m_vChild) {
                if (variableElement == nullptr) {
                    continue;
                }
                TimeVariableInfo info;
                info.m_nStartHour = ToInt32(Attribute(variableElement, u"start"));
                info.m_nEndHour = ToInt32(Attribute(variableElement, u"end"));
                info.m_nVariable = ToInt32(Attribute(variableElement, u"variable"));
                m_DropInfo.AddTimeVariableInfo(info);
            }
        } else {
            // const std::wstring name =
            //     Utf16ToWide(child->m_sName)
            const std::string name = Utf16ToUtf8(child->m_sName);
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] unknown element[%s] for MakerEvent > Drop.",
            //     kEventMakerSource,
            //     0x222,
            //     name.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] unknown element[%s] for MakerEvent > Drop.",
            //     kEventMakerSource,
            //     0x222,
            //     name.c_str())
            L2_LOG_WARN(
                Log,
                "eventmaker.xml event %d: unexpected element '%s' under Drop; this element is ignored and loading "
                "continues.",
                m_nEventId, name.c_str()
            );
        }
    }
    return true;
}

bool CEventMaker::LoadReward_MultisellItems(XMLElement* element) {
    // Current L2Server 0x0044e084.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][EventMaker] fail to load EventMaker > Reward > MultisellItems node.",
        //     kEventMakerSource,
        //     0x17a)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][EventMaker] fail to load EventMaker > Reward > MultisellItems node.",
        //     kEventMakerSource,
        //     0x17a)
        L2_LOG_ERROR(
            Log,
            "eventmaker.xml event %d: Reward/MultisellItems is missing or empty; exchange reward configuration is skipped.",
            m_nEventId
        );
        return false;
    }

    m_RewardInfo.SetMultisellId(runtime::NegateInt32Wrapping(ToInt32(Attribute(element, u"id"))));

    for (XMLElement* itemElement : element->m_vChild) {
        if (itemElement == nullptr) {
            continue;
        }
        if (!EqualsIgnoreCase(itemElement->m_sName, u"Item")) {
            LogUnknownEventElement(0x1ca, itemElement->m_sName);
            return false;
        }

        const int outputClassId = static_cast<int>(g_PrecompiledHeaderDB.Search(Attribute(itemElement, u"name").c_str()));
        const std::int64_t outputAmount = ToInt64(Attribute(itemElement, u"amount"));
        if (outputClassId == -1 || outputAmount == 0) {
            continue;
        }

        // Current tests the parent MultisellItems count here instead of the
        // Item's price count. The outer precondition makes this branch dead.
        if (element->m_vChild.empty()) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d][EventMaker] fail to load EventMaker > Reward > MultisellItems > Item node.",
            //     kEventMakerSource,
            //     0x19e)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d][EventMaker] fail to load EventMaker > Reward > MultisellItems > Item node.",
            //     kEventMakerSource,
            //     0x19e)
            L2_LOG_ERROR(
                Log,
                "eventmaker.xml event %d: the MultisellItems parent has no children while loading Item class %d; this "
                "reward list stops loading.",
                m_nEventId, outputClassId
            );
            return false;
        }

        std::vector<MultisellItem> prices;
        for (XMLElement* priceElement : itemElement->m_vChild) {
            if (priceElement == nullptr) {
                continue;
            }
            if (!EqualsIgnoreCase(priceElement->m_sName, u"Price")) {
                LogUnknownEventElement(0x1bf, priceElement->m_sName);
                return false;
            }
            const int priceClassId =
                static_cast<int>(g_PrecompiledHeaderDB.Search(Attribute(priceElement, u"name").c_str()));
            const std::int64_t priceAmount = ToInt64(Attribute(priceElement, u"amount"));
            if (priceClassId == -1 || priceAmount == 0) {
                continue;
            }
            prices.push_back(MultisellItem(priceClassId, priceAmount));
        }

        MultisellItem output(outputClassId, outputAmount);
        MultisellListInfo info(output, prices);
        m_RewardInfo.AddMultisellListInfo(info);
    }
    return true;
}

bool CEventMaker::LoadReward(XMLElement* element) {
    // Current L2Server 0x0044e4fc.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty() || element->m_vChild.size() > 3U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][EventMaker] fail to load EventMaker > Reward node. invalid child nodes.",
        //     kEventMakerSource,
        //     0x113)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][EventMaker] fail to load EventMaker > Reward node. invalid child nodes.",
        //     kEventMakerSource,
        //     0x113)
        L2_LOG_ERROR(
            Log,
            "eventmaker.xml event %d: Reward is missing or its child count %llu is outside 1..3; reward configuration is "
            "skipped.",
            m_nEventId, element != nullptr ? static_cast<unsigned long long>(element->m_vChild.size()) : 0ULL
        );
        return false;
    }

    for (XMLElement* child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"NPC")) {
            static_cast<void>(LoadReward_NPC(child));
        } else if (EqualsIgnoreCase(child->m_sName, u"BuffSkills")) {
            static_cast<void>(LoadReward_BuffSkills(child));
        } else if (EqualsIgnoreCase(child->m_sName, u"MultisellItems")) {
            static_cast<void>(LoadReward_MultisellItems(child));
        } else {
            LogUnknownEventElement(0x12e, child->m_sName);
            return false;
        }
    }
    return true;
}

bool CEventMaker::Load(XMLElement* element) {
    // Current L2Server 0x0044e6f4.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty() || element->m_vChild.size() > 2U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][EventMaker] fail to load EventMaker root. invalid child nodes.",
        //     kEventMakerSource,
        //     0xe6)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][EventMaker] fail to load EventMaker root. invalid child nodes.",
        //     kEventMakerSource,
        //     0xe6)
        L2_LOG_ERROR(
            Log,
            "eventmaker.xml event %d: root is missing or its child count %llu is outside 1..2; this event is not "
            "registered.",
            m_nEventId, element != nullptr ? static_cast<unsigned long long>(element->m_vChild.size()) : 0ULL
        );
        return false;
    }

    const std::u16string& eventName = Attribute(element, u"name");
    std::char_traits<char16_t>::copy(m_sEventName, eventName.c_str(), eventName.size() + 1);
    m_bActive = EqualsIgnoreCase(Attribute(element, u"activate"), u"true");

    for (XMLElement* child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"Reward")) {
            static_cast<void>(LoadReward(child));
        } else if (EqualsIgnoreCase(child->m_sName, u"Drop")) {
            static_cast<void>(LoadDrop(child));
        } else {
            LogUnknownEventElement(0x102, child->m_sName);
            return false;
        }
    }
    return true;
}

CEventMakerManager::CEventMakerManager() {
    // Current L2Server 0x004918ec.
    m_nTickCount = 0;
    m_pActiveEvent = nullptr;
    m_DropItemInfoOnTick.m_nMaxDropItemAmount = 0;
    m_DropItemInfoOnTick.m_nCurrentDropItemAmount = 0;
    m_DropItemInfoOnTick.m_dProbability = 100.0;
    m_pMultisellList = nullptr;
    m_vEvents.clear();
}

CEventMakerManager::~CEventMakerManager() {
    // Current L2Server 0x00491afc deletes only event owners. The current
    // shutdown path does not delete m_pMultisellList.
    for (CEventMaker* event : m_vEvents) {
        delete event;
    }
    m_vEvents.clear();
}

double CEventMakerManager::CalcLevelDiffDropProb(CNPC* npc, User* user) {
    // Current L2Server 0x0044a128.
    L2_GUARD_FUNCTION();
    if (npc == nullptr || user == nullptr) {
        return -1.0;
    }

    const int contributorLevel = npc->m_pContribute->GetPersonalGreatContributorLevel();
    const int userLevel = user->m_data->m_nLevel;
    if (npc->m_data->m_nLevel >= 85) {
        return 100.0;
    }
    if (User::IsInFifthClassGroup(user->m_data->m_nClass) || User::IsInSixthClassGroup(user->m_data->m_nClass)) {
        return -1.0;
    }
    return userLevel < 86 && userLevel - contributorLevel >= -8 ? 100.0 : -1.0;
}

CMultiSellList* CEventMakerManager::GetMultisellListPointer(const int listId) {
    // Current L2Server 0x0044a25c.
    L2_GUARD_FUNCTION();
    if (m_pActiveEvent == nullptr || m_pActiveEvent->m_bIsRunning != 1 || m_pMultisellList == nullptr ||
        listId != m_pMultisellList->m_nId) {
        return nullptr;
    }
    return m_pMultisellList;
}

bool CEventMakerManager::CanDropEventMakerItem(CNPC* npc, const bool summon) {
    // Current L2Server 0x0044a7d8.
    L2_GUARD_FUNCTION();
    if (npc == nullptr) {
        return false;
    }

    const bool movable = npc->m_data->m_moveType != CMT_FLY && !npc->m_AreaMod.m_bIsUnderWater;
    const std::uint32_t type = static_cast<std::uint32_t>(npc->getEventDropPorbType());
    const double eventRoll = ::genrand64_real1();
    bool typeAccepted = false;
    if (type <= 12U && eventRoll * 100.0 + 0.0 <= kEventDropProbability[type]) {
        typeAccepted = true;
        if (summon) {
            static_cast<void>(::genrand64_real1());
        }
    }

    const double hpRate = npc->m_pClassConst->m_nHpRate;
    if (hpRate <= 1.0 && hpRate != 1.0) {
        static_cast<void>(::genrand64_real1());
        return false;
    }
    return movable && typeAccepted;
}

bool CEventMakerManager::CheckAndDropItem(CNPC* npc, CCreature* creature, const bool summon) {
    // Current L2Server 0x0044a978.
    L2_GUARD_FUNCTION();
    if (m_pActiveEvent == nullptr || npc == nullptr || creature == nullptr) {
        return false;
    }
    User* const user = creature->GetUserOrMaster();
    if (user == nullptr) {
        return false;
    }

    const double levelProbability = CalcLevelDiffDropProb(npc, user);
    L2_GUARD(m_Lock);
    if (::genrand64_real1() * 100.0 + 0.0 > levelProbability ||
        m_DropItemInfoOnTick.m_nCurrentDropItemAmount >= m_DropItemInfoOnTick.m_nMaxDropItemAmount ||
        !CanDropEventMakerItem(npc, summon) || ::genrand64_real1() * 100.0 + 0.0 > m_DropItemInfoOnTick.m_dProbability) {
        return false;
    }

    const int index = runtime::raw_atomic::AddI32(&m_DropItemInfoOnTick.m_nCurrentDropItemAmount, 1);
    if (index <= 0 || index > m_DropItemInfoOnTick.m_nMaxDropItemAmount) {
        return false;
    }
    if (index < 0 || index >= static_cast<int>(m_DropItemInfoOnTick.m_vDropItems.size())) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][EventMaker] Drop Event ItemList Range Over!! ret[%d], DropItemListSize[%d]",
        //     kEventMakerSource,
        //     0x47e,
        //     index,
        //     static_cast<int>(
        //         m_DropItemInfoOnTick.m_vDropItems.size()))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][EventMaker] Drop Event ItemList Range Over!! ret[%d], DropItemListSize[%d]",
        //     kEventMakerSource,
        //     0x47e,
        //     index,
        //     static_cast<int>(
        //         m_DropItemInfoOnTick.m_vDropItems.size()))
        L2_LOG_ERROR(
            Log,
            "Event drop index %d is outside %llu prepared entries (per-tick limit %d); no item is added and the drop "
            "counter remains incremented.",
            index, static_cast<unsigned long long>(m_DropItemInfoOnTick.m_vDropItems.size()),
            m_DropItemInfoOnTick.m_nMaxDropItemAmount
        );
        return false;
    }

    const EventMakerDropItem& item = runtime::CheckedVectorAt(m_DropItemInfoOnTick.m_vDropItems, static_cast<std::size_t>(index));
    if (item.m_dProbability < ::genrand64_real1() * 100.0 + 0.0) {
        runtime::raw_atomic::AddI32(&m_DropItemInfoOnTick.m_nCurrentDropItemAmount, -1);
        return false;
    }

    const double eventProbability = kEventDropProbability[static_cast<std::size_t>(npc->getEventDropPorbType())];
    int amount = static_cast<int>(std::max(1.0, eventProbability / 100.0));
    if (::genrand64_real1() * 100.0 + 0.0 <= eventProbability - static_cast<double>(amount) * 100.0) {
        ++amount;
    }

    const ItemOptionKey optionKey{};
    const BaseAttribute attribute;
    npc->AddItemToInventory(item.m_nItemClassId, amount, ItemGetReason::Etc, 0, 0, 0, optionKey, 0, attribute);
    return true;
}

bool CEventMakerManager::CastBuffSkillsToUser(CNPC* npc, CCreature* creature) {
    // Current L2Server 0x0044c664.
    L2_GUARD_FUNCTION();
    if (npc == nullptr || creature == nullptr || m_pActiveEvent == nullptr || m_pActiveEvent->m_bIsRunning != 1) {
        return false;
    }

    L2_GUARD(m_Lock);
    const std::vector<BuffSkillInfo> skills = m_pActiveEvent->GetBuffSkillsList();
    for (const BuffSkillInfo& source : skills) {
        const std::uint64_t packed = static_cast<std::uint64_t>(source.m_nSkillId);
        const CSkillKey key(
            runtime::Int32FromBits(static_cast<std::uint32_t>(packed >> 32)),
            static_cast<int>((packed >> 16) & UINT64_C(0xffff)), static_cast<int>(packed & UINT64_C(0xffff))
        );
        const CSkillInfo* const skill = g_SkillDB.GetSkillInfo(key, npc);
        if (skill == nullptr) {
            return false;
        }

        CSkillAction2 action;
        SkillUseInfo useInfo;
        useInfo.m_skillID = skill->m_nSkillId;
        useInfo.m_skillLevel = skill->m_nLevel;
        useInfo.m_skillSubLevel = skill->m_nSubLevel;
        useInfo.m_castRange = skill->m_nCastRange;
        useInfo.m_type = 0;
        useInfo.m_param1 = 0;

        CWorldObject* const target = creature->CastWorldObject();
        npc->GetController()->ActionBroadcastSkillUse(
            *target, const_cast<CSkillInfo*>(skill), 0, 0, &action, nullptr, useInfo
        );
        skill->ActivateSkill(npc, target, 0.0, nullptr, CSkillInfo::EAT_SUCCESS, false);
    }
    creature->ValidateParameters(VB_SKILLMOD, false);
    return true;
}

void CEventMakerManager::ShowMultisell(const int npcIndex, User* user) {
    // Current L2Server 0x0044cfc8.
    L2_GUARD_FUNCTION();
    if (m_pActiveEvent == nullptr || m_pMultisellList == nullptr || user == nullptr) {
        return;
    }

    CNPC* npc = nullptr;
    if (!m_pMultisellList->IsWithoutNPC()) {
        CSPointer<CCreature> creature = CCreatureFactory::Inst().FindObjectSP(npcIndex);
        if (!creature || !creature->IsNPC()) {
            return;
        }
        npc = creature->CastNPC();
        if (npc == nullptr || !user->IsTalkableDistance(npc)) {
            return;
        }
        TalkChannelCache::inst().mChannels[1].Cache(user->Id(), npc->Id(), 0);
    }
    m_pMultisellList->SendList(user, npc, false);
}

void CEventMakerManager::TimerExpired(int) {
    // Current L2Server 0x0044b27c.
    L2_GUARD_FUNCTION();
    ++m_nTickCount;
    L2_GUARD(m_Lock);
    if (m_pActiveEvent != nullptr) {
        m_pActiveEvent->DoTickAction(m_nTickCount);
        if (m_pActiveEvent->InDropTick(m_nTickCount)) {
            runtime::raw_atomic::ExchangeI32(
                &m_DropItemInfoOnTick.m_nMaxDropItemAmount, m_pActiveEvent->GetMaxDropItemAmount()
            );
            runtime::raw_atomic::ExchangeI32(&m_DropItemInfoOnTick.m_nCurrentDropItemAmount, 0);
        }
    }
    AddTimer(1000, 0);
}

void CEventMakerManager::InitActiveEvent() {
    // Current L2Server 0x0044b38c.
    L2_GUARD_FUNCTION();
    if (m_pActiveEvent != nullptr) {
        m_pActiveEvent->Run();
    }
}

void CEventMakerManager::MakeDropItemInfoOnTick() {
    // Current L2Server 0x0044bef4.
    L2_GUARD_FUNCTION();
    if (m_pActiveEvent != nullptr) {
        L2_GUARD(m_Lock);
        m_pActiveEvent->MakeDropItemInfoOnTickForManager(m_DropItemInfoOnTick);
    }
}

void CEventMakerManager::_MakeDropItemInfoOnTick() {
    // Current L2Server 0x0044bfcc.
    L2_GUARD_FUNCTION();
    if (m_pActiveEvent != nullptr) {
        m_pActiveEvent->MakeDropItemInfoOnTickForManager(m_DropItemInfoOnTick);
    }
}

void CEventMakerManager::MakeMultisellList() {
    // Current L2Server 0x0044ccb4.
    L2_GUARD_FUNCTION();
    if (m_pActiveEvent != nullptr) {
        L2_GUARD(m_Lock);
        if (m_pMultisellList != nullptr) {
            delete m_pMultisellList;
            m_pMultisellList = nullptr;
        }
        m_pActiveEvent->MakeMultisellListForManager(&m_pMultisellList);
    }
}

void CEventMakerManager::_MakeMultisellList() {
    // Current L2Server 0x0044cdc4.
    L2_GUARD_FUNCTION();
    if (m_pActiveEvent != nullptr) {
        if (m_pMultisellList != nullptr) {
            delete m_pMultisellList;
            m_pMultisellList = nullptr;
        }
        m_pActiveEvent->MakeMultisellListForManager(&m_pMultisellList);
    }
}

void CEventMakerManager::Init(std::u16string filename) {
    // Current L2Server 0x0044e920 ignores the incoming filename and always
    // owns the shipping eventmaker.xml path.
    L2_GUARD_FUNCTION();
    static_cast<void>(filename);
    m_nTickCount = 0;
    XMLElement* const root = CXmlLoader::inst().Load(kEventMakerPath);
    if (root == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][EventMaker] Fail to load eventmaker.xml",
        //     kEventMakerSource,
        //     0x2a3)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][EventMaker] Fail to load eventmaker.xml",
        //     kEventMakerSource,
        //     0x2a3)
        L2_LOG_ERROR(
            Log,
            "[events] EventMaker initialization stopped: eventmaker.xml could not be loaded "
            "(see the XML/file error above). Its event timer was not scheduled; server startup continues."
        );
        return;
    }

    int eventId = 0;
    for (XMLElement* child : root->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        CEventMaker* const event = new CEventMaker(eventId);
        if (event->Load(child)) {
            m_vEvents.push_back(event);
            if (m_pActiveEvent == nullptr && event->m_bActive) {
                m_pActiveEvent = event;
            }
            ++eventId;
        }
        // Current leaks an EventMaker whose Load returned
        // false. No delete belongs on this branch.
    }

    MakeDropItemInfoOnTick();
    MakeMultisellList();
    InitActiveEvent();
    root->ReleaseChild();
    delete root;
    AddTimer(1000, 0);

    if (m_pActiveEvent == nullptr) {
        // Log.Add(LOG_ERROR, L"[NO_ERROR] [[[Event Maker Info Start]]]")
        // Noise: The adjacent event summary already identifies this report. A separate begin/end banner adds no information.
        // L2_LOG_ERROR(Log, L"[NO_ERROR] [[[Event Maker Info Start]]]")
        // Log.Add(LOG_ERROR, L"[NO_ERROR] No Active Event")
        // L2_LOG_ERROR(Log, L"[NO_ERROR] No Active Event")
        L2_LOG_INFO(
            Log, "Event configuration loaded: %llu registered definitions; none is active.",
            static_cast<unsigned long long>(m_vEvents.size())
        );
        // Log.Add(LOG_ERROR, L"[NO_ERROR] [[[Event Maker Info End]]]")
        // Noise: The adjacent event summary already identifies this report. A separate begin/end banner adds no information.
        // L2_LOG_ERROR(Log, L"[NO_ERROR] [[[Event Maker Info End]]]")
    } else {
        m_pActiveEvent->PrintEventInfoLog();
    }
}

void CEventMakerManager::_Init(std::u16string filename) {
    // Current L2Server 0x0044eb90: Reload already owns m_Lock, so the two
    // publishing helpers omit their inner lock acquisition.
    L2_GUARD_FUNCTION();
    static_cast<void>(filename);
    m_nTickCount = 0;
    XMLElement* const root = CXmlLoader::inst().Load(kEventMakerPath);
    if (root == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d][EventMaker] Fail to load eventmaker.xml",
        //     kEventMakerSource,
        //     0x2f0)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d][EventMaker] Fail to load eventmaker.xml",
        //     kEventMakerSource,
        //     0x2f0)
        L2_LOG_ERROR(
            Log, "Cannot reload eventmaker.xml; previous events were cleared and no replacement event is initialized."
        );
        return;
    }

    int eventId = 0;
    for (XMLElement* child : root->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        CEventMaker* const event = new CEventMaker(eventId);
        if (event->Load(child)) {
            m_vEvents.push_back(event);
            if (m_pActiveEvent == nullptr && event->m_bActive) {
                m_pActiveEvent = event;
            }
            ++eventId;
        }
    }

    _MakeDropItemInfoOnTick();
    _MakeMultisellList();
    InitActiveEvent();
    root->ReleaseChild();
    delete root;
    AddTimer(1000, 0);

    if (m_pActiveEvent == nullptr) {
        // Log.Add(LOG_ERROR, L"[NO_ERROR] [[[Event Maker Info Start]]]")
        // Noise: The adjacent event summary already identifies this report. A separate begin/end banner adds no information.
        // L2_LOG_ERROR(Log, L"[NO_ERROR] [[[Event Maker Info Start]]]")
        // Log.Add(LOG_ERROR, L"[NO_ERROR] No Active Event")
        // L2_LOG_ERROR(Log, L"[NO_ERROR] No Active Event")
        L2_LOG_INFO(
            Log, "Event configuration loaded: %llu registered definitions; none is active.",
            static_cast<unsigned long long>(m_vEvents.size())
        );
        // Log.Add(LOG_ERROR, L"[NO_ERROR] [[[Event Maker Info End]]]")
        // Noise: The adjacent event summary already identifies this report. A separate begin/end banner adds no information.
        // L2_LOG_ERROR(Log, L"[NO_ERROR] [[[Event Maker Info End]]]")
    } else {
        m_pActiveEvent->PrintEventInfoLog();
    }
}

void CEventMakerManager::Reload() {
    // Current L2Server 0x0044ee00.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_Lock);
    if (m_pActiveEvent != nullptr) {
        m_pActiveEvent->Stop();
        m_pActiveEvent = nullptr;
    }
    for (CEventMaker* event : m_vEvents) {
        delete event;
    }
    m_vEvents.clear();

    runtime::raw_atomic::ExchangeI32(&m_DropItemInfoOnTick.m_nMaxDropItemAmount, 0);
    runtime::raw_atomic::ExchangeI32(&m_DropItemInfoOnTick.m_nCurrentDropItemAmount, 0);
    m_DropItemInfoOnTick.m_dProbability = 100.0;
    m_DropItemInfoOnTick.m_vDropItems.clear();
    if (m_pMultisellList != nullptr) {
        delete m_pMultisellList;
    }
    m_pMultisellList = nullptr;
    m_nTickCount = 0;
    _Init(std::u16string(u"..\\Script\\eventmaker.xml"));
}
