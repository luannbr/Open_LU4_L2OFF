#include "GameEventSystem.h"

#include "CConfig.h"
#include "Cubic.h"
#include "DB.h"
#include "Default.h"
#include "MakerEvent.h"
#include "ObjectDB.h"
#include "Party.h"
#include "Pledge.h"
#include "Skill.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "XmlLoader.h"

#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string>
#include <string_view>

namespace {


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

double ToDouble(const std::u16string_view value) {
    const std::string text = Utf16ToUtf8(value);
    return std::strtod(text.c_str(), nullptr);
}

TimePeriodInfo ParsePeriod(XMLElement* element) {
    TimePeriodInfo period{};
    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), period.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), period.m_nEndTime);
    return period;
}

std::array<char, 64> FormatEventTime(const std::int32_t value) {
    std::array<char, 64> result{};
    std::tm calendar{};
    if (!runtime::crt::LocalTime32(value, calendar)) {
        return result;
    }
    std::strftime(result.data(), result.size(), "%Y-%m-%d %H:%M:%S", &calendar);
    return result;
}

template <typename... Args>
void SendSetting(User* user, const std::u16string_view format, const Args&... args) {
    std::array<char16_t, 512> buffer{};
    FormatUtf16To(buffer.data(), buffer.size(), format, args...);
    user->SendSystemMessage(u"SYS", buffer.data());
}


bool NextCsvToken(const std::u16string_view source, std::size_t& cursor, std::u16string_view& token) {
    while (cursor < source.size() && source[cursor] == u',') {
        ++cursor;
    }
    if (cursor >= source.size()) {
        token = {};
        return false;
    }
    const std::size_t end = source.find(u',', cursor);
    if (end == std::u16string_view::npos) {
        token = source.substr(cursor);
        cursor = source.size();
    } else {
        token = source.substr(cursor, end - cursor);
        cursor = end + 1;
    }
    return true;
}

bool ParseConditionProbabilities(const std::u16string& source, std::array<double, 4>& probabilities) {
    std::size_t cursor = 0;
    std::u16string_view token;
    double first = 0.0;
    if (NextCsvToken(runtime::StringView(source), cursor, token)) {
        first = ToDouble(token);
    }
    probabilities[0] = first;

    if (NextCsvToken(runtime::StringView(source), cursor, token)) {
        probabilities[1] = ToDouble(token);
    } else {
        probabilities[1] = first;
    }

    if (NextCsvToken(runtime::StringView(source), cursor, token)) {
        probabilities[2] = ToDouble(token);
    } else {
        // Literal current fallback: slot two falls back to the first value,
        // not to slot one.
        probabilities[2] = first;
    }

    if (!NextCsvToken(runtime::StringView(source), cursor, token)) {
        // The current loader publishes the item only after reading a fourth
        // token, even though it filled the preceding fallback slots.
        return false;
    }
    probabilities[3] = ToDouble(token);
    return true;
}

} // namespace

bool DropItemInfoOnTick::IsInNPCLevel(const int level) {
    // Current L2Server 0x00765be4.
    L2_GUARD_FUNCTION();
    return m_nNPCMinLevel == 0 || m_nNPCMaxLevel == 0 || (m_nNPCMinLevel <= level && level <= m_nNPCMaxLevel);
}

bool DropItemInfoOnTick::CheckUserCondition(User* user) {
    // Current L2Server 0x00766290.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    if (m_UserDropCond.m_nMinLev != UserDropCond::INVALID_VALUE && m_UserDropCond.m_nMaxLev != UserDropCond::INVALID_VALUE) {
        if (user->Level() < m_UserDropCond.m_nMinLev || m_UserDropCond.m_nMaxLev < user->Level()) {
            return false;
        }
    }

    if (m_UserDropCond.m_nOwnItemClassId != UserDropCond::INVALID_VALUE &&
        user->HasItem(static_cast<unsigned int>(m_UserDropCond.m_nOwnItemClassId)) < m_UserDropCond.m_nOwnItemAmount) {
        return false;
    }

    if (m_UserDropCond.m_nSkillNameId == UserDropCond::INVALID_VALUE) {
        return true;
    }

    const std::uint64_t wanted = static_cast<std::uint64_t>(m_UserDropCond.m_nSkillNameId);
    L2_GUARD(user->m_InfectedSkillsSect);
    for (const InfectedSkillType& infected : user->m_InfectedSkills) {
        const CSkillInfo* skill = infected.pSkillInfo;
        if (skill == nullptr) {
            break;
        }

        // Literal current packing: ((level | (skillId << 16)) << 16)
        // | subLevel. Converting each signed source through int64_t first
        // keeps the x64 sign-extension and bit pattern without relying
        // on undefined signed left shifts.
        const std::uint64_t skillId = static_cast<std::uint64_t>(static_cast<std::int64_t>(skill->m_nSkillId));
        const std::uint64_t level = static_cast<std::uint64_t>(static_cast<std::int64_t>(skill->m_nLevel));
        const std::uint64_t subLevel = static_cast<std::uint64_t>(static_cast<std::int64_t>(skill->m_nSubLevel));
        const std::uint64_t packed = ((level | (skillId << 16U)) << 16U) | subLevel;
        if (packed == wanted) {
            return true;
        }
    }
    return false;
}

CDropInfo::CDropInfo() :
    m_nTick(0),
    m_TotalPeriod{},
    m_eDropSequence(GAME_EVENT::DT_RANDOM),
    m_dDropProb(100.0),
    m_dPremiumUserDropProb(0.0),
    m_dFreeUserDropProb(0.0),
    m_UserDropCond(),
    m_nMaxVariables(0),
    m_nTotalItemAmount(0),
    m_nNPCMinLevel(0),
    m_nNPCMaxLevel(0),
    m_nMinimumPlayingUserCount(1),
    m_nBasePlayingUserCount(1),
    m_dUserRatio(1.0) {}

CDropInfo::~CDropInfo() = default;

void CDropInfo::Add(DropItemInfo info) {
    // Current L2Server 0x00768914.
    L2_GUARD_FUNCTION();
    m_nTotalItemAmount = runtime::Int32FromBits(
        static_cast<std::uint32_t>(m_nTotalItemAmount) + static_cast<std::uint32_t>(info.m_nAmount)
    );
    m_vDropItemList.push_back(info);
}

void CDropInfo::Add(TimeVariableInfo info) {
    // Current L2Server 0x00768cfc.
    L2_GUARD_FUNCTION();
    m_nMaxVariables = std::max(m_nMaxVariables, info.m_nVariable);
    m_vTimeVariables.push_back(info);
}

bool CDropInfo::InPeriod() {
    // Current L2Server 0x00766458.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    for (const TimePeriodInfo& period : m_vDropPeriods) {
        if (period.m_nStartTime <= now && now <= period.m_nEndTime) {
            return true;
        }
    }
    return false;
}

void CDropInfo::SetUserRatio() {
    // Current L2Server 0x00765c9c.
    L2_GUARD_FUNCTION();
    if (m_nMinimumPlayingUserCount == 1 || m_nBasePlayingUserCount == 1) {
        m_dUserRatio = 1.0;
        return;
    }
    const int playingUserCount = g_nPlayingUserCount;
    m_dUserRatio = static_cast<double>(std::max(m_nMinimumPlayingUserCount, playingUserCount)) /
        static_cast<double>(m_nBasePlayingUserCount);
}

int CDropInfo::GetRealMaxDropItemAmount(const int variable) {
    // Current L2Server 0x00766524.
    L2_GUARD_FUNCTION();
    SetUserRatio();
    if (m_eDropSequence != GAME_EVENT::DT_FIX) {
        return variable;
    }
    const std::int32_t product =
        runtime::Int32FromBits(static_cast<std::uint32_t>(m_nTotalItemAmount) * static_cast<std::uint32_t>(variable));
    return runtime::Win64CvttDoubleToInt32(std::ceil(static_cast<double>(product) * m_dUserRatio));
}

int CDropInfo::GetMaxDropItemAmount() {
    // Current L2Server 0x007668d4.
    L2_GUARD_FUNCTION();
    if (!InPeriod()) {
        return 0;
    }
    if (m_vTimeVariables.empty()) {
        return GetRealMaxDropItemAmount(1);
    }

    std::tm local{};
    if (!runtime::crt::LocalTime32(runtime::crt::Time32Now(), local)) {
        return 0;
    }
    for (const TimeVariableInfo& variable : m_vTimeVariables) {
        if (variable.m_nStartHour <= local.tm_hour && local.tm_hour < variable.m_nEndHour) {
            return GetRealMaxDropItemAmount(variable.m_nVariable);
        }
    }
    return 0;
}

void CDropInfo::InitDropItemInfo(DropItemInfoOnTick& info) {
    // Current L2Server 0x007689c4.
    L2_GUARD_FUNCTION();
    const std::int32_t wrappedReserve = runtime::Int32FromBits(
        static_cast<std::uint32_t>(m_vDropItemList.size()) * static_cast<std::uint32_t>(m_nTotalItemAmount) *
        static_cast<std::uint32_t>(m_nMaxVariables)
    );
    const std::size_t reserveCount = static_cast<std::size_t>(static_cast<std::int64_t>(wrappedReserve));
    info.m_vDropItemList.reserve(reserveCount);
    info.m_eDropSequence = m_eDropSequence;
    info.m_dDropProb = m_dDropProb;
    info.m_nNPCMinLevel = m_nNPCMinLevel;
    info.m_nNPCMaxLevel = m_nNPCMaxLevel;
    info.m_dPremiumUserDropProb = m_dPremiumUserDropProb;
    info.m_dFreeUserDropProb = m_dFreeUserDropProb;
    info.m_UserDropCond = m_UserDropCond;

    for (int variable = 0; variable < m_nMaxVariables; ++variable) {
        for (const DropItemInfo& source : m_vDropItemList) {
            for (int amount = 0; amount < source.m_nAmount; ++amount) {
                DropItemInfo item{};
                item.m_nAmountPerServer = 0;
                item.m_nItemClassID = source.m_nItemClassID;
                item.m_nAmount = 1;
                item.m_nProb = source.m_nProb;
                item.m_nConditionProb = source.m_nConditionProb;
                info.m_vDropItemList.push_back(item);
            }
        }
    }
    info.m_nMaxDropItemAmount = InPeriod() ? GetMaxDropItemAmount() : 0;
}

CNPCInfo::CNPCInfo() : m_SpawnPeriod{} {}

CNPCInfo::~CNPCInfo() = default;

CEvent_Drop::CEvent_Drop() {}

CEvent_Drop::~CEvent_Drop() = default;

GAME_EVENT::EVENT_TYPE CEvent_Drop::GetEventType() const { return GAME_EVENT::ET_DROP; }

bool CEvent_Drop::InEventTime() {
    if (m_DropInfo.InPeriod()) {
        return true;
    }
    const std::int32_t now = runtime::crt::Time32Now();
    return m_NPCInfo.m_SpawnPeriod.m_nStartTime <= now && now <= m_NPCInfo.m_SpawnPeriod.m_nEndTime;
}

bool CEvent_Drop::InMyTick(const int tick) {
    const bool dropTick = m_DropInfo.m_nTick > 0 && m_DropInfo.m_nTick <= tick && tick % m_DropInfo.m_nTick == 0;
    const bool npcTick = m_NPCInfo.m_nTick > 0 && m_NPCInfo.m_nTick <= tick && tick % m_NPCInfo.m_nTick == 0;
    return dropTick || npcTick;
}

bool CEvent_Drop::InMyDropTick(const int tick) {
    return m_DropInfo.m_nTick > 0 && m_DropInfo.m_nTick <= tick && tick % m_DropInfo.m_nTick == 0;
}

bool CEvent_Drop::IsAllDone() {
    // Current L2Server 0x00765d70.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return m_DropInfo.m_TotalPeriod.m_nEndTime < now && m_NPCInfo.m_SpawnPeriod.m_nEndTime < now;
}

int CEvent_Drop::GetMaxDropItemAmount() { return m_DropInfo.GetMaxDropItemAmount(); }

void CEvent_Drop::InitDropItemInfo(DropItemInfoOnTick& info) { m_DropInfo.InitDropItemInfo(info); }

double CEvent_Drop::GetUserRatio() { return m_DropInfo.m_dUserRatio; }

std::size_t CEvent_Drop::GetEventPeriod(std::vector<TimePeriodInfo>& periods) {
    // Current L2Server 0x00768db4.
    L2_GUARD_FUNCTION();
    for (const TimePeriodInfo& period : m_DropInfo.m_vDropPeriods) {
        periods.push_back(period);
    }
    periods.push_back(m_NPCInfo.m_SpawnPeriod);
    return periods.size();
}

void CEvent_Drop::SetEventDropTime(const bool enabled) {
    // Current L2Server 0x00765eb8.
    L2_GUARD_FUNCTION();
    SharedEventInfo::Inst()->m_gameEvent.eventDropTime = enabled;
}

void CEvent_Drop::SetDropProb(const double probability) {
    // Current L2Server 0x00765f28.
    L2_GUARD_FUNCTION();
    m_DropInfo.m_dDropProb = probability;
}

void CEvent_Drop::SetPremiumUserDropProb(const double probability) {
    // Current L2Server 0x00765f5c.
    L2_GUARD_FUNCTION();
    m_DropInfo.m_dPremiumUserDropProb = probability;
}

void CEvent_Drop::SetFreeUserDropProb(const double probability) {
    // Current L2Server 0x00765f90.
    L2_GUARD_FUNCTION();
    m_DropInfo.m_dFreeUserDropProb = probability;
}

void CEvent_Drop::SetDropProb(std::u16string probability) {
    // Current L2Server 0x00766b80.
    L2_GUARD_FUNCTION();
    SetDropProb(ToDouble(probability));
}

void CEvent_Drop::SetPremiumUserDropProb(std::u16string probability) {
    // Current L2Server 0x00766c6c.
    L2_GUARD_FUNCTION();
    SetPremiumUserDropProb(ToDouble(probability));
}

void CEvent_Drop::SetFreeUserDropProb(std::u16string probability) {
    // Current L2Server 0x00766d58.
    L2_GUARD_FUNCTION();
    SetFreeUserDropProb(ToDouble(probability));
}

void CEvent_Drop::SetDropSequence(std::u16string sequence) {
    // Current L2Server 0x00766a20.
    L2_GUARD_FUNCTION();
    if (EqualsIgnoreCase(sequence, u"random")) {
        m_DropInfo.m_eDropSequence = GAME_EVENT::DT_RANDOM;
        return;
    }
    if (EqualsIgnoreCase(sequence, u"fix")) {
        m_DropInfo.m_eDropSequence = GAME_EVENT::DT_FIX;
        return;
    }
    const std::string hostSequence = Utf16ToUtf8(sequence);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] DropSequence is invalid[%s]",
    //     L"GameEventSystem_DropEvent.cpp",
    //     0x2e0,
    //     hostSequence.c_str())
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] DropSequence is invalid[%s]",
    //     L"GameEventSystem_DropEvent.cpp",
    //     0x2e0,
    //     hostSequence.c_str())
    L2_LOG_WARN(
        Log, "DropEvent: unknown DropSequence '%s'; expected 'random' or 'fix'. The previous sequence is retained.",
        hostSequence.c_str()
    );
}

void CEvent_Drop::DiableAllNpcPosEvent() {
    // Current L2Server 0x007665ec. The original repeats the disable for
    // every configured period instead of once per maker event.
    L2_GUARD_FUNCTION();
    for (const SpawnTimes& spawn : m_NPCInfo.m_vSpawnEvents) {
        for (const TimePeriodInfo& period : spawn.m_vSpawnTimes) {
            static_cast<void>(period);
            DisableNpcPosEvent(spawn.m_sMakerEventName.c_str());
        }
    }
}

bool CEvent_Drop::LoadDrop(XMLElement* element) {
    // Current L2Server 0x00768f64.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty() || element->m_vChild.size() > 5) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load DropEvent > Drop. invalid child nodes",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x1e0)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load DropEvent > Drop. invalid child nodes",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x1e0)
        L2_LOG_ERROR(
            Log,
            "Cannot load DropEvent/Drop: the XML node is missing or has %llu children; expected 1..5. This section stops "
            "loading; event loading continues.",
            element != nullptr ? static_cast<unsigned long long>(element->m_vChild.size()) : 0ULL
        );
        return false;
    }

    m_DropInfo.m_nTick = ToInt32(Attribute(element, u"Tick"));
    m_DropInfo.m_nNPCMinLevel = ToInt32(Attribute(element, u"NPCMinLev"));
    m_DropInfo.m_nNPCMaxLevel = ToInt32(Attribute(element, u"NPCMaxLev"));
    m_DropInfo.m_nMinimumPlayingUserCount = std::max(1, ToInt32(Attribute(element, u"MinPlayingUserCount")));
    m_DropInfo.m_nBasePlayingUserCount = std::max(1, ToInt32(Attribute(element, u"BasePlayingUserCount")));

    for (XMLElement* const child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"DropTimes")) {
            std::size_t index = 0;
            for (XMLElement* const timeElement : child->m_vChild) {
                if (timeElement == nullptr) {
                    ++index;
                    continue;
                }
                const TimePeriodInfo period = ParsePeriod(timeElement);
                m_DropInfo.m_TotalPeriod.m_nStartTime = std::min(m_DropInfo.m_TotalPeriod.m_nStartTime, period.m_nStartTime);
                m_DropInfo.m_TotalPeriod.m_nEndTime = std::max(m_DropInfo.m_TotalPeriod.m_nEndTime, period.m_nEndTime);
                m_DropInfo.m_vDropPeriods.push_back(period);
                if (index == 0) {
                    SharedEventInfo::Inst()->m_gameEvent.m_FirstDropTime = period.m_nStartTime;
                }
                ++index;
            }
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"Items")) {
            SetDropSequence(Attribute(child, u"DropSequence"));
            SetDropProb(Attribute(child, u"DropProb"));
            SetPremiumUserDropProb(Attribute(child, u"PremiumProb"));
            SetFreeUserDropProb(Attribute(child, u"FreeProb"));

            for (XMLElement* const itemElement : child->m_vChild) {
                if (itemElement == nullptr) {
                    continue;
                }
                const int classId = static_cast<int>(g_PrecompiledHeaderDB.Search(Attribute(itemElement, u"Name").c_str()));
                if (classId == -1) {
                    const std::string name = Utf16ToUtf8(Attribute(itemElement, u"Name"));
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Item name is invalid[%s]",
                    //     L"GameEventSystem_DropEvent.cpp",
                    //     0x220,
                    //     name.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] Item name is invalid[%s]",
                    //     L"GameEventSystem_DropEvent.cpp",
                    //     0x220,
                    //     name.c_str())
                    L2_LOG_ERROR(
                        Log, "DropEvent/Drop/Items: item '%s' is not defined; this entry is omitted.",
                        name.c_str()
                    );
                    continue;
                }

                DropItemInfo info{};
                info.m_nAmountPerServer = ToInt32(Attribute(itemElement, u"AmountPerServer"));
                info.m_nItemClassID = classId;
                info.m_nAmount = ToInt32(Attribute(itemElement, u"Amount"));
                info.m_nProb = ToDouble(Attribute(itemElement, u"Prob"));
                // Unlike AgathionDrop, the current DropEvent leaves all
                // four conditional probabilities at zero.
                m_DropInfo.Add(info);
            }
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"TimeVariables")) {
            for (XMLElement* const variableElement : child->m_vChild) {
                if (variableElement == nullptr) {
                    continue;
                }
                TimeVariableInfo variable{};
                variable.m_nStartHour = ToInt32(Attribute(variableElement, u"Start"));
                variable.m_nEndHour = ToInt32(Attribute(variableElement, u"End"));
                variable.m_nVariable = ToInt32(Attribute(variableElement, u"Variable"));
                m_DropInfo.Add(variable);
            }
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"TicksPerPlayer")) {
            for (XMLElement* const tickElement : child->m_vChild) {
                if (tickElement == nullptr) {
                    continue;
                }
                TickVariableInfo variable{};
                variable.m_nMinimumUserCount = ToInt32(Attribute(tickElement, u"MinPlayer"));
                variable.m_nMaximumUserCount = ToInt32(Attribute(tickElement, u"MaxPlayer"));
                variable.m_nVariable = ToInt32(Attribute(tickElement, u"Variable"));
                m_DropInfo.m_vTickSettingVariable.push_back(variable);
            }
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"PC")) {
            int minimumLevel = UserDropCond::INVALID_VALUE;
            int maximumLevel = UserDropCond::INVALID_VALUE;
            int itemClassId = UserDropCond::INVALID_VALUE;
            std::int64_t skillNameId = UserDropCond::INVALID_VALUE;
            std::int64_t itemAmount = UserDropCond::INVALID_VALUE;

            for (XMLElement* const condition : child->m_vChild) {
                if (condition == nullptr) {
                    continue;
                }
                if (EqualsIgnoreCase(condition->m_sName, u"SkillCheck")) {
                    const std::int64_t candidate = g_PrecompiledHeaderDB.Search(Attribute(condition, u"Name").c_str());
                    if (candidate > 0) {
                        const std::uint64_t bits = static_cast<std::uint64_t>(candidate);
                        const CSkillKey skillKey(
                            static_cast<int>(bits >> 32U), static_cast<int>((bits >> 16U) & 0xffffU),
                            static_cast<int>(bits & 0xffffU)
                        );
                        if (g_SkillDB.GetSkillInfo(skillKey, nullptr) != nullptr) {
                            skillNameId = candidate;
                        }
                    }
                    continue;
                }

                if (EqualsIgnoreCase(condition->m_sName, u"Level")) {
                    minimumLevel = std::max(0, ToInt32(Attribute(condition, u"MinLevel")));
                    maximumLevel = std::min(120, ToInt32(Attribute(condition, u"MaxLevel")));
                    if (minimumLevel < 1 || maximumLevel < 1 || maximumLevel < minimumLevel) {
                        minimumLevel = UserDropCond::INVALID_VALUE;
                        maximumLevel = UserDropCond::INVALID_VALUE;
                    }
                    continue;
                }

                if (EqualsIgnoreCase(condition->m_sName, u"Item")) {
                    const int candidateClassId =
                        static_cast<int>(g_PrecompiledHeaderDB.Search(Attribute(condition, u"Name").c_str()));
                    const std::int64_t candidateAmount = ToInt64(Attribute(condition, u"Amount"));
                    if (candidateClassId < 1 || candidateAmount < 1) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d]invalid item info id[%d] amount[%I64d]",
                        //     L"GameEventSystem_DropEvent.cpp",
                        //     0x28b,
                        //     candidateClassId,
                        //     candidateAmount)
                        // L2_LOG_ERROR(Log,
                        //     L"[%s][%d]invalid item info id[%d] amount[%I64d]",
                        //     L"GameEventSystem_DropEvent.cpp",
                        //     0x28b,
                        //     candidateClassId,
                        //     candidateAmount)
                        L2_LOG_ERROR(
                            Log,
                            "DropEvent/Drop/PC/Item: class ID %d and amount %lld must both be positive; this condition "
                            "entry is ignored and the previously collected item condition is retained.",
                            candidateClassId, static_cast<long long>(candidateAmount)
                        );
                    } else {
                        itemClassId = candidateClassId;
                        itemAmount = candidateAmount;
                    }
                    continue;
                }

                const std::string name = Utf16ToUtf8(condition->m_sName);
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]invalid xml node [%s]",
                //     L"GameEventSystem_DropEvent.cpp",
                //     0x290,
                //     name.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d]invalid xml node [%s]",
                //     L"GameEventSystem_DropEvent.cpp",
                //     0x290,
                //     name.c_str())
                L2_LOG_WARN(
                    Log, "DropEvent/Drop/PC: unexpected XML element '%s'; this element is ignored and loading continues.",
                    name.c_str()
                );
            }

            m_DropInfo.m_UserDropCond.m_nMinLev = minimumLevel;
            m_DropInfo.m_UserDropCond.m_nMaxLev = maximumLevel;
            m_DropInfo.m_UserDropCond.m_nOwnItemClassId = itemClassId;
            m_DropInfo.m_UserDropCond.m_nSkillNameId = skillNameId;
            m_DropInfo.m_UserDropCond.m_nOwnItemAmount = itemAmount;
            continue;
        }

        const std::string name = Utf16ToUtf8(child->m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] unknown element[%s] for DropEvent > Drop.",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x29a,
        //     name.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] unknown element[%s] for DropEvent > Drop.",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x29a,
        //     name.c_str())
        L2_LOG_WARN(
            Log, "DropEvent/Drop: unexpected XML element '%s'; this element is ignored and loading continues.",
            name.c_str()
        );
    }
    return true;
}

bool CEvent_Drop::LoadNPC(XMLElement* element) {
    // Current L2Server 0x0076a42c.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load DropEvent > NPC. invalid child nodes",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x2a9)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load DropEvent > NPC. invalid child nodes",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x2a9)
        L2_LOG_ERROR(
            Log,
            "Cannot load DropEvent/NPC: the XML node is missing or empty. NPC settings are skipped; event loading continues."
        );
        return false;
    }

    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_NPCInfo.m_SpawnPeriod.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_NPCInfo.m_SpawnPeriod.m_nEndTime);
    m_NPCInfo.m_nTick = ToInt32(Attribute(element, u"Tick"));

    for (XMLElement* const spawnElement : element->m_vChild) {
        if (spawnElement == nullptr) {
            continue;
        }
        SpawnTimes spawn;
        spawn.m_sMakerEventName = Attribute(spawnElement, u"MakerEventName");
        for (XMLElement* const timeElement : spawnElement->m_vChild) {
            if (timeElement != nullptr) {
                spawn.m_vSpawnTimes.push_back(ParsePeriod(timeElement));
            }
        }
        m_NPCInfo.m_vSpawnEvents.push_back(spawn);
    }
    return true;
}

bool CEvent_Drop::Load(XMLElement* element) {
    // Current L2Server 0x0076aa10.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty() || element->m_vChild.size() > 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load DropEvent. invalid child nodes",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x104)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load DropEvent. invalid child nodes",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x104)
        L2_LOG_ERROR(
            Log,
            "Cannot load DropEvent: the XML node is missing or has %llu children; expected 1..2. This event is not loaded.",
            element != nullptr ? static_cast<unsigned long long>(element->m_vChild.size()) : 0ULL
        );
        return false;
    }

    LoadRootElement(element);
    for (XMLElement* const child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"Drop")) {
            LoadDrop(child);
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"NPC")) {
            LoadNPC(child);
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"BroadcastPickItem")) {
            Singleton<CEventManager>::inst().SetBroadcastPickItemMsg(
                g_ObjectDB.GetClassIdFromName(Attribute(child, u"Name").c_str(), false), ToInt32(Attribute(child, u"Msg"))
            );
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"EventData")) {
            db.RequestSetEventDataPeriod(ToInt32(Attribute(child, u"Period")));
            continue;
        }

        const std::string name = Utf16ToUtf8(child->m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] unknown element[%s] for DropEvent.",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x129,
        //     name.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] unknown element[%s] for DropEvent.",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x129,
        //     name.c_str())
        L2_LOG_WARN(
            Log, "DropEvent: unexpected XML element '%s'; this element is ignored and loading continues.",
            name.c_str()
        );
    }
    return true;
}

void CEvent_Drop::ShowSetting(User* user) {
    // Current L2Server 0x007670cc.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    SendSetting(
        user, u"EVENT_NAME:%s EVENT_DOING:%d FLACSetting::inst()_TIME:%d", GetName(), InEventTime(), m_DropInfo.m_nTick
    );

    for (const TickVariableInfo& variable : m_DropInfo.m_vTickSettingVariable) {
        SendSetting(
            user, u"TICKS_PER_PLAYER:%d MIN_PLAYER:%d MAX_PLAYER:%d", variable.m_nVariable, variable.m_nMinimumUserCount,
            variable.m_nMaximumUserCount
        );
    }

    SendSetting(
        user, u"NPCMINLEV:%d NPCMAXLEV:%d MINPLAYINGUSERCOUNT:%d BASEPLAYINGUSERCOUNT:%d", m_DropInfo.m_nNPCMinLevel,
        m_DropInfo.m_nNPCMaxLevel, m_DropInfo.m_nMinimumPlayingUserCount, m_DropInfo.m_nBasePlayingUserCount
    );
    SendSetting(
        user, u"DROP_ITEM_COUNT:%d DROP_PROB:%.1f", static_cast<int>(m_DropInfo.m_vDropItemList.size()),
        m_DropInfo.m_dDropProb
    );

    int index = 0;
    for (const DropItemInfo& item : m_DropInfo.m_vDropItemList) {
        const char16_t* const name = g_ObjectDB.GetName(item.m_nItemClassID);
        SendSetting(
            user, u"DROP_ITEM%d: itemname=%s, itemid=%d, amount=%d, prob=%.1f, amountPerServer=%d", index++, name,
            item.m_nItemClassID, item.m_nAmount, item.m_nProb, item.m_nAmountPerServer
        );
    }

    SendSetting(user, u"DROP_TIME_COUNT:%d", static_cast<int>(m_DropInfo.m_vDropPeriods.size()));
    index = 0;
    for (const TimePeriodInfo& period : m_DropInfo.m_vDropPeriods) {
        const auto start = FormatEventTime(period.m_nStartTime);
        const auto end = FormatEventTime(period.m_nEndTime);
        SendSetting(user, u"DROP_TIME%d = %s ~ %s", index++, start.data(), end.data());
    }

    SendSetting(user, u"TIME_VARIABLE_COUNT:%d", static_cast<int>(m_DropInfo.m_vTimeVariables.size()));
    index = 0;
    for (const TimeVariableInfo& variable : m_DropInfo.m_vTimeVariables) {
        SendSetting(
            user, u"TIME_VARIABLE_COUNT%d: %d ~ %d : %d", index++, variable.m_nStartHour, variable.m_nEndHour,
            variable.m_nVariable
        );
    }

    SendSetting(user, u"NPC_SETTING_COUNT:%d", static_cast<int>(m_NPCInfo.m_vSpawnEvents.size()));
    index = 0;
    for (const SpawnTimes& spawn : m_NPCInfo.m_vSpawnEvents) {
        const std::wstring makerEventName = Utf16ToWide(spawn.m_sMakerEventName);
        SendSetting(user, u"NPC_EVENT_NAME%d = %s", index++, makerEventName.c_str());

        int timeIndex = 0;
        for (const TimePeriodInfo& period : spawn.m_vSpawnTimes) {
            const auto start = FormatEventTime(period.m_nStartTime);
            const auto end = FormatEventTime(period.m_nEndTime);
            SendSetting(user, u"SPAWN_TIME%d = %s ~ %s", timeIndex++, start.data(), end.data());
        }
    }
}

void CEvent_Drop::DoTickAction(const int tick) {
    // Current L2Server 0x0076acd8.
    L2_GUARD_FUNCTION();
    const int period = m_DropInfo.GetCurrentPeriod();
    if (m_nCurrentPeriod != -1 && m_nCurrentPeriod != period) {
        Singleton<CEventManager>::inst().ResetDropItemAmountPerServer();
    }
    m_nCurrentPeriod = period;

    if (InMyDropTick(tick)) {
        SetEventDropTime(m_DropInfo.InPeriod());
    }

    const std::int32_t now = runtime::crt::Time32Now();
    if (m_NPCInfo.m_SpawnPeriod.m_nStartTime <= now && now <= m_NPCInfo.m_SpawnPeriod.m_nEndTime && m_NPCInfo.m_nTick > 0 &&
        m_NPCInfo.m_nTick <= tick && tick % m_NPCInfo.m_nTick == 0) {
        for (const SpawnTimes& spawn : m_NPCInfo.m_vSpawnEvents) {
            for (const TimePeriodInfo& spawnPeriod : spawn.m_vSpawnTimes) {
                if (spawnPeriod.m_nStartTime <= now && now <= spawnPeriod.m_nEndTime) {
                    if (g_PledgeTimer.IsAllNpcRelatedLoaded()) {
                        EnableNpcPosEvent(spawn.m_sMakerEventName.c_str());
                    }
                } else if (g_PledgeTimer.IsAllNpcRelatedLoaded()) {
                    DisableNpcPosEvent(spawn.m_sMakerEventName.c_str());
                }
            }
        }
    }
}

CEvent_AgathionDrop::CEvent_AgathionDrop() : m_ForceReachGoal(false) {}

CEvent_AgathionDrop::~CEvent_AgathionDrop() = default;

GAME_EVENT::EVENT_TYPE CEvent_AgathionDrop::GetEventType() const { return GAME_EVENT::ET_AGATHION; }

bool CEvent_AgathionDrop::InEventTime() {
    if (m_DropInfo.InPeriod()) {
        return true;
    }
    const std::int32_t now = runtime::crt::Time32Now();
    return m_NPCInfo.m_SpawnPeriod.m_nStartTime <= now && now <= m_NPCInfo.m_SpawnPeriod.m_nEndTime;
}

bool CEvent_AgathionDrop::InMyTick(const int tick) {
    const bool dropTick = m_DropInfo.m_nTick > 0 && m_DropInfo.m_nTick <= tick && tick % m_DropInfo.m_nTick == 0;
    const bool npcTick = m_NPCInfo.m_nTick > 0 && m_NPCInfo.m_nTick <= tick && tick % m_NPCInfo.m_nTick == 0;
    return dropTick || npcTick;
}

bool CEvent_AgathionDrop::InMyDropTick(const int tick) {
    return m_DropInfo.m_nTick > 0 && m_DropInfo.m_nTick <= tick && tick % m_DropInfo.m_nTick == 0;
}

bool CEvent_AgathionDrop::IsAllDone() {
    // Current L2Server 0x0076609c.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return m_DropInfo.m_TotalPeriod.m_nEndTime < now && m_NPCInfo.m_SpawnPeriod.m_nEndTime < now;
}

int CEvent_AgathionDrop::GetMaxDropItemAmount() { return m_DropInfo.GetMaxDropItemAmount(); }

void CEvent_AgathionDrop::InitDropItemInfo(DropItemInfoOnTick& info) { m_DropInfo.InitDropItemInfo(info); }

double CEvent_AgathionDrop::GetUserRatio() { return m_DropInfo.m_dUserRatio; }

std::size_t CEvent_AgathionDrop::GetEventPeriod(std::vector<TimePeriodInfo>& periods) {
    // Current L2Server 0x00768e8c.
    L2_GUARD_FUNCTION();
    for (const TimePeriodInfo& period : m_DropInfo.m_vDropPeriods) {
        periods.push_back(period);
    }
    periods.push_back(m_NPCInfo.m_SpawnPeriod);
    return periods.size();
}

void CEvent_AgathionDrop::SetEventDropTime(const bool enabled) {
    // Current L2Server 0x00766144.
    L2_GUARD_FUNCTION();
    SharedEventInfo::Inst()->m_gameEvent.eventDropTime = enabled;
}

void CEvent_AgathionDrop::SetDropProb(const double probability) {
    // Current L2Server 0x007661e8.
    L2_GUARD_FUNCTION();
    m_DropInfo.m_dDropProb = probability;
}

void CEvent_AgathionDrop::SetDropProb(std::u16string probability) {
    // Current L2Server 0x00766fa4.
    L2_GUARD_FUNCTION();
    SetDropProb(ToDouble(probability));
}

void CEvent_AgathionDrop::SetDropSequence(std::u16string sequence) {
    // Current L2Server 0x00766e44.
    L2_GUARD_FUNCTION();
    if (EqualsIgnoreCase(sequence, u"random")) {
        m_DropInfo.m_eDropSequence = GAME_EVENT::DT_RANDOM;
        return;
    }
    if (EqualsIgnoreCase(sequence, u"fix")) {
        m_DropInfo.m_eDropSequence = GAME_EVENT::DT_FIX;
        return;
    }
    const std::string hostSequence = Utf16ToUtf8(sequence);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] DropSequence is invalid[%s]",
    //     L"GameEventSystem_DropEvent.cpp",
    //     0x508,
    //     hostSequence.c_str())
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] DropSequence is invalid[%s]",
    //     L"GameEventSystem_DropEvent.cpp",
    //     0x508,
    //     hostSequence.c_str())
    L2_LOG_WARN(
        Log, "AgathionDropEvent: unknown DropSequence '%s'; expected 'random' or 'fix'. The previous sequence is retained.",
        hostSequence.c_str()
    );
}

void CEvent_AgathionDrop::DiableAllNpcPosEvent() {
    // Current L2Server 0x00766738. The repeated disable per period is
    // observable and therefore kept.
    L2_GUARD_FUNCTION();
    for (const SpawnTimes& spawn : m_NPCInfo.m_vSpawnEvents) {
        for (const TimePeriodInfo& period : spawn.m_vSpawnTimes) {
            static_cast<void>(period);
            DisableNpcPosEvent(spawn.m_sMakerEventName.c_str());
        }
    }
}

bool CEvent_AgathionDrop::IsConquested() {
    if (!m_ForceReachGoal) {
        const GameEvent& info = SharedEventInfo::Inst()->m_gameEvent;
        if (info.m_AgathionCount < info.m_Goal) {
            return false;
        }
    }
    return true;
}

bool CEvent_AgathionDrop::IsForceReachGoal() noexcept { return m_ForceReachGoal; }

bool CEvent_AgathionDrop::IsReachGoal() noexcept { return m_ReachGoal; }

void CEvent_AgathionDrop::SetForceReachGoal(const bool force) {
    m_ForceReachGoal = force;
    m_ReachGoal = IsConquested();
}

int CEvent_AgathionDrop::GetAgathionID() const noexcept { return m_AgathionID; }

CDropInfo& CEvent_AgathionDrop::GetDropInfo() noexcept { return m_DropInfo; }

CNPCInfo& CEvent_AgathionDrop::GetNPCInfo() noexcept { return m_NPCInfo; }

double CEvent_AgathionDrop::GetProb(User* user, DropItemInfo* info) {
    // Current L2Server 0x00768554.
    bool hasAgathion = user->HaveAgathion(m_AgathionID);
    const bool conquered = IsConquested();
    double partyBonus = 0.0;
    if (CParty* const party = user->GetParty()) {
        const int partyIndex = conquered ? 3 : 1;
        std::vector<User*> members;
        party->GetAllMember(members);
        for (User* const member : members) {
            if (member->HaveAgathion(m_AgathionID)) {
                partyBonus += info->m_nConditionProb[partyIndex] / 7.0;
                hasAgathion = true;
            }
        }
    }

    const int index = conquered ? (hasAgathion ? 3 : 2) : (hasAgathion ? 1 : 0);
    return partyBonus + info->m_nConditionProb[index];
}

bool CEvent_AgathionDrop::LoadDrop(XMLElement* element) {
    // Current L2Server 0x00769b40.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty() || element->m_vChild.size() > 4) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load DropEvent > Drop. invalid child nodes",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x431)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load DropEvent > Drop. invalid child nodes",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x431)
        L2_LOG_ERROR(
            Log,
            "Cannot load AgathionDropEvent/Drop: the XML node is missing or has %llu children; expected 1..4. This section "
            "stops loading; event loading continues.",
            element != nullptr ? static_cast<unsigned long long>(element->m_vChild.size()) : 0ULL
        );
        return false;
    }

    m_DropInfo.m_nTick = ToInt32(Attribute(element, u"Tick"));
    m_DropInfo.m_nNPCMinLevel = ToInt32(Attribute(element, u"NPCMinLev"));
    m_DropInfo.m_nNPCMaxLevel = ToInt32(Attribute(element, u"NPCMaxLev"));
    m_DropInfo.m_nMinimumPlayingUserCount = std::max(1, ToInt32(Attribute(element, u"MinPlayingUserCount")));
    m_DropInfo.m_nBasePlayingUserCount = std::max(1, ToInt32(Attribute(element, u"BasePlayingUserCount")));

    for (XMLElement* const child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"DropTimes")) {
            std::size_t index = 0;
            for (XMLElement* const timeElement : child->m_vChild) {
                if (timeElement == nullptr) {
                    ++index;
                    continue;
                }
                const TimePeriodInfo period = ParsePeriod(timeElement);
                m_DropInfo.m_TotalPeriod.m_nStartTime = std::min(m_DropInfo.m_TotalPeriod.m_nStartTime, period.m_nStartTime);
                m_DropInfo.m_TotalPeriod.m_nEndTime = std::max(m_DropInfo.m_TotalPeriod.m_nEndTime, period.m_nEndTime);
                m_DropInfo.m_vDropPeriods.push_back(period);
                if (index == 0) {
                    SharedEventInfo::Inst()->m_gameEvent.m_FirstDropTime = period.m_nStartTime;
                }
                ++index;
            }
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"Items")) {
            SetDropSequence(Attribute(child, u"DropSequence"));
            SetDropProb(Attribute(child, u"DropProb"));
            for (XMLElement* const itemElement : child->m_vChild) {
                if (itemElement == nullptr) {
                    continue;
                }
                const int classId = static_cast<int>(g_PrecompiledHeaderDB.Search(Attribute(itemElement, u"Name").c_str()));
                if (classId == -1) {
                    const std::string name = Utf16ToUtf8(Attribute(itemElement, u"Name"));
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Item name is invalid[%s]",
                    //     L"GameEventSystem_DropEvent.cpp",
                    //     0x470,
                    //     name.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] Item name is invalid[%s]",
                    //     L"GameEventSystem_DropEvent.cpp",
                    //     0x470,
                    //     name.c_str())
                    L2_LOG_ERROR(
                        Log, "AgathionDropEvent/Drop/Items: item '%s' is not defined; this entry is omitted.",
                        name.c_str()
                    );
                    continue;
                }

                DropItemInfo info{};
                info.m_nAmountPerServer = ToInt32(Attribute(itemElement, u"AmountPerServer"));
                info.m_nItemClassID = classId;
                info.m_nAmount = ToInt32(Attribute(itemElement, u"Amount"));
                info.m_nProb = ToDouble(Attribute(itemElement, u"Prob"));
                if (ParseConditionProbabilities(Attribute(itemElement, u"CondProb"), info.m_nConditionProb)) {
                    m_DropInfo.Add(info);
                }
            }
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"TimeVariables")) {
            for (XMLElement* const variableElement : child->m_vChild) {
                if (variableElement == nullptr) {
                    continue;
                }
                TimeVariableInfo variable{};
                variable.m_nStartHour = ToInt32(Attribute(variableElement, u"Start"));
                variable.m_nEndHour = ToInt32(Attribute(variableElement, u"End"));
                variable.m_nVariable = ToInt32(Attribute(variableElement, u"Variable"));
                m_DropInfo.Add(variable);
            }
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"TicksPerPlayer")) {
            for (XMLElement* const tickElement : child->m_vChild) {
                if (tickElement == nullptr) {
                    continue;
                }
                TickVariableInfo variable{};
                variable.m_nMinimumUserCount = ToInt32(Attribute(tickElement, u"MinPlayer"));
                variable.m_nMaximumUserCount = ToInt32(Attribute(tickElement, u"MaxPlayer"));
                variable.m_nVariable = ToInt32(Attribute(tickElement, u"Variable"));
                m_DropInfo.m_vTickSettingVariable.push_back(variable);
            }
            continue;
        }

        const std::string name = Utf16ToUtf8(child->m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] unknown element[%s] for DropEvent > Drop.",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x4c2,
        //     name.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] unknown element[%s] for DropEvent > Drop.",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x4c2,
        //     name.c_str())
        L2_LOG_WARN(
            Log, "AgathionDropEvent/Drop: unexpected XML element '%s'; this element is ignored and loading continues.",
            name.c_str()
        );
    }
    return true;
}

bool CEvent_AgathionDrop::LoadNPC(XMLElement* element) {
    // Current L2Server 0x0076a704.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load DropEvent > NPC. invalid child nodes",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x4d1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load DropEvent > NPC. invalid child nodes",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x4d1)
        L2_LOG_ERROR(
            Log,
            "Cannot load AgathionDropEvent/NPC: the XML node is missing or empty. NPC settings are skipped; event loading "
            "continues."
        );
        return false;
    }

    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_NPCInfo.m_SpawnPeriod.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_NPCInfo.m_SpawnPeriod.m_nEndTime);
    m_NPCInfo.m_nTick = ToInt32(Attribute(element, u"Tick"));

    for (XMLElement* const spawnElement : element->m_vChild) {
        if (spawnElement == nullptr) {
            continue;
        }
        SpawnTimes spawn;
        spawn.m_sMakerEventName = Attribute(spawnElement, u"MakerEventName");
        for (XMLElement* const timeElement : spawnElement->m_vChild) {
            if (timeElement != nullptr) {
                spawn.m_vSpawnTimes.push_back(ParsePeriod(timeElement));
            }
        }
        m_NPCInfo.m_vSpawnEvents.push_back(spawn);
    }
    return true;
}

bool CEvent_AgathionDrop::Load(XMLElement* element) {
    // Current L2Server 0x0076af58.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty() || element->m_vChild.size() > 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load DropEvent. invalid child nodes",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x345)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load DropEvent. invalid child nodes",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x345)
        L2_LOG_ERROR(
            Log,
            "Cannot load AgathionDropEvent: the XML node is missing or has %llu children; expected 1..2. This event is not "
            "loaded.",
            element != nullptr ? static_cast<unsigned long long>(element->m_vChild.size()) : 0ULL
        );
        return false;
    }

    LoadRootElement(element);
    SharedEventInfo::Inst()->m_gameEvent.m_Goal = runtime::Win64CvttDoubleToInt32(
        static_cast<double>(ToInt32(Attribute(element, u"NeedAgathionCount"))) * g_dEventAdjustGoalRate
    );

    const int agathionLevel = ToInt32(Attribute(element, u"AgathionLevel"));
    const int agathionId = ToInt32(Attribute(element, u"AgathionId"));
    m_AgathionID =
        SpriteDB::Inst().GetInfo(SPRITE_AGATHION, agathionId, agathionLevel)->CastAgathion()->m_npcClassID - 1000000;

    for (XMLElement* const child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"Drop")) {
            LoadDrop(child);
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"NPC")) {
            LoadNPC(child);
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"BroadcastPickItem")) {
            Singleton<CEventManager>::inst().SetBroadcastPickItemMsg(
                g_ObjectDB.GetClassIdFromName(Attribute(child, u"Name").c_str(), false), ToInt32(Attribute(child, u"Msg"))
            );
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"EventData")) {
            db.RequestSetEventDataPeriod(ToInt32(Attribute(child, u"Period")));
            continue;
        }

        const std::string name = Utf16ToUtf8(child->m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] unknown element[%s] for DropEvent.",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x36d,
        //     name.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] unknown element[%s] for DropEvent.",
        //     L"GameEventSystem_DropEvent.cpp",
        //     0x36d,
        //     name.c_str())
        L2_LOG_WARN(
            Log, "AgathionDropEvent: unexpected XML element '%s'; this element is ignored and loading continues.",
            name.c_str()
        );
    }
    return true;
}

void CEvent_AgathionDrop::ShowSetting(User* user) {
    // Current L2Server 0x00767b10.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    SendSetting(
        user, u"EVENT_NAME:%s EVENT_DOING:%d FLACSetting::inst()_TIME:%d", GetName(), InEventTime(), m_DropInfo.m_nTick
    );

    for (const TickVariableInfo& variable : m_DropInfo.m_vTickSettingVariable) {
        SendSetting(
            user, u"TICKS_PER_PLAYER:%d MIN_PLAYER:%d MAX_PLAYER:%d", variable.m_nVariable, variable.m_nMinimumUserCount,
            variable.m_nMaximumUserCount
        );
    }

    SendSetting(
        user, u"NPCMINLEV:%d NPCMAXLEV:%d MINPLAYINGUSERCOUNT:%d BASEPLAYINGUSERCOUNT:%d", m_DropInfo.m_nNPCMinLevel,
        m_DropInfo.m_nNPCMaxLevel, m_DropInfo.m_nMinimumPlayingUserCount, m_DropInfo.m_nBasePlayingUserCount
    );
    SendSetting(
        user, u"DROP_ITEM_COUNT:%d DROP_PROB:%.1f", static_cast<int>(m_DropInfo.m_vDropItemList.size()),
        m_DropInfo.m_dDropProb
    );

    int index = 0;
    for (const DropItemInfo& item : m_DropInfo.m_vDropItemList) {
        const char16_t* const name = g_ObjectDB.GetName(item.m_nItemClassID);
        SendSetting(
            user, u"DROP_ITEM%d: itemname=%s, itemid=%d, amount=%d, prob=%.1f, amountPerServer=%d", index++, name,
            item.m_nItemClassID, item.m_nAmount, item.m_nProb, item.m_nAmountPerServer
        );
    }

    SendSetting(user, u"DROP_TIME_COUNT:%d", static_cast<int>(m_DropInfo.m_vDropPeriods.size()));
    index = 0;
    for (const TimePeriodInfo& period : m_DropInfo.m_vDropPeriods) {
        const auto start = FormatEventTime(period.m_nStartTime);
        const auto end = FormatEventTime(period.m_nEndTime);
        SendSetting(user, u"DROP_TIME%d = %s ~ %s", index++, start.data(), end.data());
    }

    SendSetting(user, u"TIME_VARIABLE_COUNT:%d", static_cast<int>(m_DropInfo.m_vTimeVariables.size()));
    index = 0;
    for (const TimeVariableInfo& variable : m_DropInfo.m_vTimeVariables) {
        SendSetting(
            user, u"TIME_VARIABLE_COUNT%d: %d ~ %d : %d", index++, variable.m_nStartHour, variable.m_nEndHour,
            variable.m_nVariable
        );
    }

    SendSetting(user, u"NPC_SETTING_COUNT:%d", static_cast<int>(m_NPCInfo.m_vSpawnEvents.size()));
    index = 0;
    for (const SpawnTimes& spawn : m_NPCInfo.m_vSpawnEvents) {
        const std::wstring makerEventName = Utf16ToWide(spawn.m_sMakerEventName);
        SendSetting(user, u"NPC_EVENT_NAME%d = %s", index++, makerEventName.c_str());

        int timeIndex = 0;
        for (const TimePeriodInfo& period : spawn.m_vSpawnTimes) {
            const auto start = FormatEventTime(period.m_nStartTime);
            const auto end = FormatEventTime(period.m_nEndTime);
            SendSetting(user, u"SPAWN_TIME%d = %s ~ %s", timeIndex++, start.data(), end.data());
        }
    }
}

void CEvent_AgathionDrop::DoTickAction(const int tick) {
    // Current L2Server 0x0076b2ec.
    L2_GUARD_FUNCTION();
    const int period = m_DropInfo.GetCurrentPeriod();
    if (m_nCurrentPeriod != -1 && m_nCurrentPeriod != period) {
        Singleton<CEventManager>::inst().ResetDropItemAmountPerServer();
    }
    m_nCurrentPeriod = period;

    if (InMyDropTick(tick)) {
        SetEventDropTime(m_DropInfo.InPeriod());
    }

    const std::int32_t now = runtime::crt::Time32Now();
    if (m_NPCInfo.m_SpawnPeriod.m_nStartTime <= now && now <= m_NPCInfo.m_SpawnPeriod.m_nEndTime && m_NPCInfo.m_nTick > 0 &&
        m_NPCInfo.m_nTick <= tick && tick % m_NPCInfo.m_nTick == 0) {
        for (const SpawnTimes& spawn : m_NPCInfo.m_vSpawnEvents) {
            for (const TimePeriodInfo& spawnPeriod : spawn.m_vSpawnTimes) {
                if (spawnPeriod.m_nStartTime <= now && now <= spawnPeriod.m_nEndTime) {
                    if (g_PledgeTimer.IsAllNpcRelatedLoaded()) {
                        EnableNpcPosEvent(spawn.m_sMakerEventName.c_str());
                    }
                } else if (g_PledgeTimer.IsAllNpcRelatedLoaded()) {
                    DisableNpcPosEvent(spawn.m_sMakerEventName.c_str());
                }
            }
        }
    }

    if (tick % 600 == 0) {
        const GameEvent& eventInfo = SharedEventInfo::Inst()->m_gameEvent;
        CLogFormatter formatter;
        formatter.WriteLog(
            0x148, 0, 0, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, eventInfo.m_Goal, eventInfo.m_AgathionCount, 0, 0, 0, 0,
            0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr, 0
        );
    }

    if (tick % 3600 == 0) {
        CUserBroadcaster::inst().BroadcastToAllUser(
            "chdddddddddddSSSSS", 0xfe, 39, 0, IsConquested() ? 0xe83 : 0xe82, 2, 0, 0, 0, 1, 0, 5000, 0, -1, u"", u"", u"",
            u"", u""
        );
    }
}

namespace {

using RegisteredEventFactory = GenericFactory<CEvent, std::u16string, CEvent* (*)()>;

L2_DEFINE_AND_REGISTER_FACTORY_TYPE(
    CEvent, CEvent_Drop, Create_CEvent_Drop, isReg_CEvent_Drop, RegisteredEventFactory, std::u16string(u"DropEvent")
);
L2_DEFINE_AND_REGISTER_FACTORY_TYPE(
    CEvent,
    CEvent_AgathionDrop,
    Create_CEvent_AgathionDrop,
    isReg_CEvent_AgathionDrop,
    RegisteredEventFactory,
    std::u16string(u"AgathionDropEvent")
);

} // namespace
