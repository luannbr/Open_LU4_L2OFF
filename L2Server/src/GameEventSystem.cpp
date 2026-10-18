#include "GameEventSystem.h"
#include "Shared/src/MemorySub.h"

#include "Area.h"
#include "BalthusEvent.h"
#include "CConfig.h"
#include "CContributeData.h"
#include "DB.h"
#include "Default.h"
#include "HTMLCache.h"
#include "Item.h"
#include "KalieEvent.h"
#include "LuckyGameSystem.h"
#include "MakerEvent.h"
#include "NPC.h"
#include "NpcServer.h"
#include "ObjectDB.h"
#include "Pledge.h"
#include "Recipe.h"
#include "TeleportManager.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"
#include "XmlLoader.h"

#include "Shared/src/Exception.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/PrecompiledHeaderDB.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"
#include "Shared/src/UtilsString.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <iterator>
#include <string>
#include <utility>

namespace {

constexpr wchar_t kEventConfigFilename[] = L"..\\Script\\eventdata.xml";
constexpr wchar_t kCommonDropConfigFilename[] = L"..\\Script\\commondropdata.xml";
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

int CompareHostWideIgnoreCase(const wchar_t* left, const wchar_t* right) noexcept;

bool EqualsIgnoreCase(const std::u16string& left, const char16_t* right) {
    const std::wstring hostLeft = Utf16ToWide(left);
    const std::wstring hostRight = Utf16ToWide(right);
    return CompareHostWideIgnoreCase(hostLeft.c_str(), hostRight.c_str()) == 0;
}

std::int32_t ToInt32(const std::u16string& value) { return WindowsWtol(Utf16ToUtf8(runtime::StringView(value))); }

std::int64_t ToInt64(const std::u16string& value) {
    const std::string narrow = Utf16ToUtf8(runtime::StringView(value));
    return static_cast<std::int64_t>(std::strtoll(narrow.c_str(), nullptr, 10));
}

double ToDouble(const std::u16string& value) {
    const std::string narrow = Utf16ToUtf8(runtime::StringView(value));
    return std::strtod(narrow.c_str(), nullptr);
}


int CompareHostWideIgnoreCase(const wchar_t* left, const wchar_t* right) noexcept {
#if defined(_WIN32)
    return ::_wcsicmp(left, right);
#else
    while (*left != L'\0' && *right != L'\0') {
        const wint_t foldedLeft = std::towlower(*left);
        const wint_t foldedRight = std::towlower(*right);
        if (foldedLeft != foldedRight) {
            return foldedLeft < foldedRight ? -1 : 1;
        }
        ++left;
        ++right;
    }
    if (*left == *right) {
        return 0;
    }
    return *left == L'\0' ? -1 : 1;
#endif
}

std::int32_t AccountEventResetBoundary(const std::int32_t now, const std::int32_t resetInterval) {
    std::tm local{};
    if (!runtime::crt::LocalTime32(now, local)) {
        return 0;
    }
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    std::int32_t boundary = runtime::crt::Mktime32(&local) + resetInterval;
    if (now < boundary) {
        boundary -= 86400;
    }
    return boundary;
}

std::u16string& Attribute(XMLElement* element, const char16_t* name) { return element->m_smapAttr[std::u16string(name)]; }

void UpdateDropCounters(DropItemInfoOnTick& info, CEvent* event) {
    runtime::raw_atomic::ExchangeI32(&info.m_nMaxDropItemAmount, event->GetMaxDropItemAmount());
    runtime::raw_atomic::ExchangeI32(&info.m_nCurDropItemAmount, 0);
    info.m_dUserRatio = event->GetUserRatio();
}

bool IsInPeriod(const TimePeriodInfo& period, std::int32_t now) noexcept {
    return period.m_nStartTime <= now && now <= period.m_nEndTime;
}

bool IsInPeriods(const std::vector<TimePeriodInfo>& periods, std::int32_t now) {
    for (const TimePeriodInfo& period : periods) {
        if (IsInPeriod(period, now)) {
            return true;
        }
    }
    return false;
}

bool HasUnfinishedPeriod(const std::vector<TimePeriodInfo>& periods, std::int32_t now) {
    for (const TimePeriodInfo& period : periods) {
        if (now <= period.m_nEndTime) {
            return true;
        }
    }
    return false;
}

void AppendPeriods(std::vector<TimePeriodInfo>& destination, const std::vector<TimePeriodInfo>& source) {
    for (const TimePeriodInfo& period : source) {
        destination.push_back(period);
    }
}

TimePeriodInfo ParseEventPeriod(XMLElement* element) {
    TimePeriodInfo period;
    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), period.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), period.m_nEndTime);
    return period;
}

bool AttributeIsTrue(XMLElement* element, const char16_t* name) {
    return EqualsIgnoreCase(Attribute(element, name), u"true");
}

template <class... Args>
void SendBuilderLine(User* user, const std::u16string_view format, const Args&... arguments) {
    if (user == nullptr || user->GetSocket() == nullptr) {
        return;
    }

    std::array<char16_t, 512> message{};
    FormatUtf16To(message.data(), message.size(), format, arguments...);

    char16_t context[] = u"SYS";
    user->GetSocket()->SendSystemMessage(context, message.data());
}

std::array<wchar_t, 64> FormatEventTime(std::int32_t value) {
    std::array<wchar_t, 64> result{};
    const std::time_t hostTime = static_cast<std::time_t>(value);
    std::tm calendar{};
#if defined(_WIN32)
    localtime_s(&calendar, &hostTime);
#else
    localtime_r(&hostTime, &calendar);
#endif
    std::wcsftime(result.data(), result.size(), L"%Y-%m-%d %H:%M:%S", &calendar);
    return result;
}

} // namespace

bool CommonDropItemInfoOnTick::IsEqualNPCRace(const RaceType race) {
    // Current L2Server 0x0074d400.
    L2_GUARD_FUNCTION();
    return m_nNPCRace == -2 || m_nNPCRace == static_cast<std::int32_t>(race);
}

CEvent::CEvent() :
    m_EventId(0),
    m_bRegister(false),
    m_sEventName{},
    m_EnterSystemMsg(0),
    m_ManagerNpcId(0),
    m_EventStart(false),
    m_RandomUserCount(0),
    m_EventPeriod{},
    m_nCurrentPeriod(-1),
    m_bToDoListCheck(false),
    m_ToDayPriority(0),
    m_StartTimeHour(0),
    m_EndTimeHour(0) {}

CEvent::~CEvent() = default;

void CEvent::Init() {}

void CEvent::StartGetRandomUser() {
    // Current L2Server 0x0074c7ec.
    L2_GUARD_FUNCTION();
    static_cast<void>(runtime::raw_atomic::ExchangeI32(&m_RandomUserCount, 0));
    m_EventStart = true;
}

void CEvent::TryToBeWinner(User* user) {
    // Current L2Server.exe 0x0074c884.
    L2_GUARD_FUNCTION();
    if (m_EventStart && user != nullptr && runtime::raw_atomic::CompareExchangeI32(&m_RandomUserCount, 1, 0) != 1) {
        g_npcserver.Send("cddd", 0x94, m_ManagerNpcId, user->GetServerId(), user->m_data->index);
        m_EventStart = false;
    }
}

bool CEvent::InEventTime() { return false; }

bool CEvent::InMyTick(int tick) {
    static_cast<void>(tick);
    return false;
}

bool CEvent::InMyDropTick(int tick) {
    static_cast<void>(tick);
    return false;
}

void CEvent::DoTickAction(int tick) { static_cast<void>(tick); }

bool CEvent::IsAllDone() { return false; }

int CEvent::GetMaxDropItemAmount() { return 0; }

void CEvent::InitDropItemInfo(CommonDropItemInfoOnTick& info) { static_cast<void>(info); }

void CEvent::InitDropItemInfo(DropItemInfoOnTick& info) { static_cast<void>(info); }

bool CEvent::ShouldBeRegistered() const { return m_bRegister; }

double CEvent::GetUserRatio() { return 1.0; }

std::size_t CEvent::GetEventPeriod(std::vector<TimePeriodInfo>& periods) {
    L2_GUARD_FUNCTION();
    periods.push_back(m_EventPeriod);
    return periods.size();
}

void CEvent::LoadRootElement(XMLElement* element) {
    L2_GUARD_FUNCTION();

    Utils::CopyStrInLowerCase(Attribute(element, u"Name").c_str(), m_sEventName.data());
    m_bRegister = EqualsIgnoreCase(Attribute(element, u"Register"), u"true");
    m_bToDoListCheck = EqualsIgnoreCase(Attribute(element, u"todolist_check"), u"true");

    const std::u16string& todayPriority = Attribute(element, u"today_priority");
    m_ToDayPriority = todayPriority.empty() ? 0 : ToInt32(todayPriority);

    const std::u16string& enterMessage = Attribute(element, u"EnterMessage");
    m_EnterSystemMsg = enterMessage.empty() ? 0 : ToInt32(enterMessage);
    m_EventId = ToInt32(Attribute(element, u"EventId"));
}

bool CEvent_CoretimeTeleport::HourPeriodInfo::ParseTime(const char16_t* time, std::int32_t& value) {
    // Current L2Server 0x0074cd74.
    L2_GUARD_FUNCTION();
    std::int32_t hour = 0;
    std::int32_t minute = 0;
    std::int32_t second = 0;
    const std::wstring hostTime = Utf16ToWide(time);
    std::swscanf(hostTime.c_str(), L"%d:%d:%d", &hour, &minute, &second);
    if (static_cast<std::uint32_t>(hour) >= 24U || static_cast<std::uint32_t>(minute) >= 60U ||
        static_cast<std::uint32_t>(second) >= 60U) {
        return false;
    }
    value = (hour * 100 + minute) * 100 + second;
    return true;
}

bool CEvent_CoretimeTeleport::CoretimeInfo::InCoretime() const {
    const std::int32_t now = runtime::crt::Time32Now();
    std::tm local{};
    if (!runtime::crt::LocalTime32(now, local)) {
        return false;
    }
    const std::int32_t hour = (local.tm_hour * 100 + local.tm_min) * 100 + local.tm_sec;
    return m_Time.m_nStartHour <= hour && hour <= m_Time.m_nEndHour;
}

bool CEvent_CoretimeTeleport::WeekDayInfo::InWeekDay() const {
    const std::int32_t now = runtime::crt::Time32Now();
    std::tm local{};
    if (!runtime::crt::LocalTime32(now, local)) {
        return false;
    }
    return m_nStartWDay <= local.tm_wday && local.tm_wday < m_nEndWDay;
}

bool CEvent_CoretimeTeleport::Load(XMLElement* element) {
    // Current L2Server 0x0075c2e4.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.size() != 2U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load CoretimeTeleport. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x634)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load CoretimeTeleport. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x634)
        L2_LOG_ERROR(
            Log,
            "Cannot load CoretimeTeleport: the XML node is missing or has %llu children; expected exactly 2. This event is "
            "not loaded.",
            element != nullptr ? static_cast<unsigned long long>(element->m_vChild.size()) : 0ULL
        );
        return false;
    }

    LoadRootElement(element);
    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_EventPeriod.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_EventPeriod.m_nEndTime);
    m_nTick = ToInt32(Attribute(element, u"Tick"));

    for (XMLElement* child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"Coretime")) {
            SetStartTimeHour(
                static_cast<std::int32_t>(
                    HourPeriodInfo::ParseTime(Attribute(child, u"Start").c_str(), m_Coretime.m_Time.m_nStartHour)
                )
            );
            SetEndTimeHour(
                static_cast<std::int32_t>(
                    HourPeriodInfo::ParseTime(Attribute(child, u"End").c_str(), m_Coretime.m_Time.m_nEndHour)
                )
            );
            m_Coretime.m_nPriceRatio = ToInt32(Attribute(child, u"PriceRatio"));
            if (m_Coretime.m_Time.m_nEndHour < m_Coretime.m_Time.m_nStartHour) {
                const std::string start = Utf16ToUtf8(Attribute(child, u"Start"));
                const std::string end = Utf16ToUtf8(Attribute(child, u"End"));
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] CoretimeTeleport > Coretime is invalid. "
                //     L"Start[%s], End[%s]",
                //     L"GameEventSystem.cpp",
                //     0x658,
                //     start.c_str(),
                //     end.c_str())
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] CoretimeTeleport > Coretime is invalid. "
                //     L"Start[%s], End[%s]",
                //     L"GameEventSystem.cpp",
                //     0x658,
                //     start.c_str(),
                //     end.c_str())
                L2_LOG_ERROR(
                    Log, "CoretimeTeleport/Coretime: end '%s' precedes start '%s'; this event is not loaded.",
                    end.c_str(), start.c_str()
                );
                return false;
            }
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"WeekDay")) {
            m_WeekDay.m_nStartWDay = ToInt32(Attribute(child, u"Start"));
            m_WeekDay.m_nEndWDay = ToInt32(Attribute(child, u"End"));
            if (m_WeekDay.m_nEndWDay < m_WeekDay.m_nStartWDay) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] CoretimeTeleport > WeekDay is invalid. "
                //     L"Start[%d], End[%d]",
                //     L"GameEventSystem.cpp",
                //     0x664,
                //     m_WeekDay.m_nStartWDay,
                //     m_WeekDay.m_nEndWDay)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] CoretimeTeleport > WeekDay is invalid. "
                //     L"Start[%d], End[%d]",
                //     L"GameEventSystem.cpp",
                //     0x664,
                //     m_WeekDay.m_nStartWDay,
                //     m_WeekDay.m_nEndWDay)
                L2_LOG_ERROR(
                    Log, "CoretimeTeleport/WeekDay: end day %d precedes start day %d; this event is not loaded.",
                    m_WeekDay.m_nEndWDay, m_WeekDay.m_nStartWDay
                );
                return false;
            }
            continue;
        }

        const std::string childName = Utf16ToUtf8(child->m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] unknown element[%s] for CoretimeTeleport.",
        //     L"GameEventSystem.cpp",
        //     0x66a,
        //     childName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] unknown element[%s] for CoretimeTeleport.",
        //     L"GameEventSystem.cpp",
        //     0x66a,
        //     childName.c_str())
        L2_LOG_WARN(
            Log, "CoretimeTeleport: unexpected XML element '%s'; this element is ignored and loading continues.",
            childName.c_str()
        );
    }

    {
        L2_GUARD(s_mapTelPosListSect);
        for (auto& entry : s_mapTelPosList) {
            for (TelPos& position : *entry.second) {
                position.m_nCoretimePrice = ConvertToCoretimePrice(position.m_nPrice);
            }
        }
    }
    return true;
}

void CEvent_CoretimeTeleport::ShowSetting(User* user) {
    // Current L2Server 0x00752560.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    const auto start = FormatEventTime(m_EventPeriod.m_nStartTime);
    const auto end = FormatEventTime(m_EventPeriod.m_nEndTime);
    SendBuilderLine(
        user,
        u"CoretimeTeleport EventPeriod(%s ~ %s) / "
        u"Coretime(%d:%d ~ %d:%d) / PriceRatio: %d / "
        u"DayOption: %d ~ %d",
        start.data(), end.data(), m_Coretime.m_Time.m_nStartHour / 10000, (m_Coretime.m_Time.m_nStartHour % 10000) / 100,
        m_Coretime.m_Time.m_nEndHour / 10000, (m_Coretime.m_Time.m_nEndHour % 10000) / 100, m_Coretime.m_nPriceRatio,
        m_WeekDay.m_nStartWDay, m_WeekDay.m_nEndWDay
    );
}

bool CEvent_CoretimeTeleport::InEventTime() {
    // Current L2Server 0x0074ce68.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return m_EventPeriod.m_nStartTime <= now && now <= m_EventPeriod.m_nEndTime;
}

bool CEvent_CoretimeTeleport::InMyTick(int tick) {
    // Current L2Server 0x00423078.
    return m_nTick > 0 && m_nTick <= tick && tick % m_nTick == 0;
}

void CEvent_CoretimeTeleport::DoTickAction(int tick) { static_cast<void>(tick); }

bool CEvent_CoretimeTeleport::IsAllDone() {
    // Current L2Server 0x004231e4.
    return m_EventPeriod.m_nEndTime < runtime::crt::Time32Now();
}

GAME_EVENT::EVENT_TYPE CEvent_CoretimeTeleport::GetEventType() const {
    // Current L2Server 0x008660c4.
    return GAME_EVENT::ET_CORETIME_TELEPORT;
}

std::int32_t CEvent_CoretimeTeleport::ConvertToCoretimePrice(std::int32_t price) {
    // Current L2Server 0x0074cf10.
    L2_GUARD_FUNCTION();
    if (price == 0) {
        return 0;
    }
    const double converted = (static_cast<double>(m_Coretime.m_nPriceRatio) / 100.0) * static_cast<double>(price);
    if (static_cast<double>(price) > 100.0) {
        return runtime::Win64CvttDoubleToInt32(converted / 100.0 + 0.5) * 100;
    }
    return runtime::Win64CvttDoubleToInt32(converted + 0.5);
}

bool CEvent_CoretimeTeleport::IsInCoretime() {
    // Current L2Server 0x00ae4aa0. The two nested calls sample
    // the clock independently, matching the original source structure.
    return m_WeekDay.InWeekDay() && m_Coretime.InCoretime();
}

CEvent_PCCafe::CEvent_PCCafe() :
    m_nTick(0),
    m_nSettingSec(0),
    m_PCCafe(),
    m_HomePCCafe(),
    m_DailyPCCafe(),
    m_bGettingPCCafePoint(false),
    m_bUsingPCCafePoint(false),
    m_nSavedDate(GetYYYYMMDDFromTime(runtime::crt::Time32Now())),
    m_nDailyResetTime(0),
    m_sBonusType{L"", L"Exp", L"PVP_Point", L"Item", L"Point", L"Buff_Skill"} {}

CEvent_PCCafe::~CEvent_PCCafe() = default;

bool CEvent_PCCafe::Load(XMLElement* element) {
    L2_GUARD_FUNCTION();
    if (element == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load PCCafeSetting. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x7f4)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load PCCafeSetting. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x7f4)
        L2_LOG_ERROR(Log, "Cannot load PCCafeSetting: the XML node is missing. This event is not loaded.");
        return false;
    }

    LoadRootElement(element);
    m_nTick = ToInt32(Attribute(element, u"Tick"));
    m_nSettingSec = ToInt32(Attribute(element, u"SettingSec"));

    for (XMLElement* child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"UsingPeriods")) {
            for (XMLElement* period : child->m_vChild) {
                if (period != nullptr) {
                    m_vUsingPeriod.push_back(ParseEventPeriod(period));
                }
            }
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"PCCafe")) {
            m_PCCafe.m_nPoint = ToInt32(Attribute(child, u"Point"));
            m_PCCafe.m_nRandomPointProb = ToInt32(Attribute(child, u"RandomPointProb"));
            for (XMLElement* period : child->m_vChild) {
                if (period != nullptr) {
                    m_PCCafe.m_vGettingPeriod.push_back(ParseEventPeriod(period));
                }
            }
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"HomePCCafe")) {
            m_HomePCCafe.m_nPoint = ToInt32(Attribute(child, u"Point"));
            m_HomePCCafe.m_nRandomPointProb = ToInt32(Attribute(child, u"RandomPointProb"));
            m_HomePCCafe.m_bGettingSet = AttributeIsTrue(child, u"GettingSet");
            m_HomePCCafe.m_bGettingMaxSet = AttributeIsTrue(child, u"GettingMaxSet");
            m_HomePCCafe.m_nGettingMaxCount = ToInt32(Attribute(child, u"GettingMaxCount"));
            for (XMLElement* period : child->m_vChild) {
                if (period != nullptr) {
                    m_HomePCCafe.m_vGettingPeriod.push_back(ParseEventPeriod(period));
                }
            }
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"DailyPCCafe")) {
            m_DailyPCCafe.m_nPoint = ToInt32(Attribute(child, u"Point"));
            for (XMLElement* period : child->m_vChild) {
                if (period != nullptr) {
                    m_DailyPCCafe.m_vGettingPeriod.push_back(ParseEventPeriod(period));
                }
            }
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"PCCafeBonus")) {
            PCCafeBonus bonus;
            const std::u16string& type = Attribute(child, u"Type");
            if (EqualsIgnoreCase(type, u"exp")) {
                bonus.m_nBonusType = BT_EXP;
            } else if (EqualsIgnoreCase(type, u"pvp_point")) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d]pvp_point is deprecated",
                //     L"GameEventSystem.cpp",
                //     0x860)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d]pvp_point is deprecated",
                //     L"GameEventSystem.cpp",
                //     0x860)
                L2_LOG_WARN(
                    Log,
                    "PCCafeSetting/PCCafeBonus: deprecated type 'pvp_point' is retained with bonus type 0; loading "
                    "continues."
                );
            } else if (EqualsIgnoreCase(type, u"item")) {
                bonus.m_nBonusType = BT_ITEM;
            } else if (EqualsIgnoreCase(type, u"point")) {
                bonus.m_nBonusType = BT_POINT;
            } else if (EqualsIgnoreCase(type, u"buff_skill")) {
                bonus.m_nBonusType = BT_BUFFSKILL;
            }

            bonus.m_nBonusValue = ToInt32(Attribute(child, u"Value"));
            bonus.m_nBonusPointTime = ToInt32(Attribute(child, u"PointTime"));
            bonus.m_nBonusItemClassID =
                static_cast<std::int32_t>(g_PrecompiledHeaderDB.Search(Attribute(child, u"ItemName").c_str()));
            bonus.m_nBonusLevel = ToInt32(Attribute(child, u"Level"));

            for (XMLElement* period : child->m_vChild) {
                if (period != nullptr && EqualsIgnoreCase(period->m_sName, u"BonusPeriod")) {
                    bonus.m_vGettingPeriod.push_back(ParseEventPeriod(period));
                }
            }

            bool replaced = false;
            if (bonus.m_nBonusType == BT_EXP || bonus.m_nBonusType == BT_PVP_POINT) {
                for (PCCafeBonus& current : m_PCCafeBonuses) {
                    if (current.m_nBonusType == bonus.m_nBonusType) {
                        current = bonus;
                        replaced = true;
                        break;
                    }
                }
            }
            if (!replaced) {
                m_PCCafeBonuses.push_back(bonus);
            }
            continue;
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] unknown element[%s] for PCCafeSetting.",
        //     L"GameEventSystem.cpp",
        //     0x89b,
        //     child->m_sName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] unknown element[%s] for PCCafeSetting.",
        //     L"GameEventSystem.cpp",
        //     0x89b,
        //     child->m_sName.c_str())
        L2_LOG_WARN(
            Log, "PCCafeSetting: unexpected XML element '%s'; this element is ignored and loading continues.",
            Utf16ToUtf8(child->m_sName).c_str()
        );
    }

    m_nDailyResetTime = 0x276;
    db.RequestSetDailyResetTime(u"daily_pccafe_point", u"06:30");
    return true;
}

void CEvent_PCCafe::ShowSetting(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    SendBuilderLine(
        user, u"PCCAFE_GETTIME:%d PCCAFE_USETIME:%d POINT:%d RANDOMBONUS:%d SETTING_SEC:%d", m_bGettingPCCafePoint,
        m_bUsingPCCafePoint, m_PCCafe.m_nPoint, m_PCCafe.m_nRandomPointProb, m_nSettingSec
    );

    SendBuilderLine(user, u"GET_TIME_COUNT:%d", static_cast<int>(m_PCCafe.m_vGettingPeriod.size()));
    int index = 0;
    for (const TimePeriodInfo& period : m_PCCafe.m_vGettingPeriod) {
        const auto start = FormatEventTime(period.m_nStartTime);
        const auto end = FormatEventTime(period.m_nEndTime);
        SendBuilderLine(user, u"GET_TIME%d = %s ~ %s", index, start.data(), end.data());
        ++index;
    }

    SendBuilderLine(user, u"USE_TIME_COUNT:%d", static_cast<int>(m_vUsingPeriod.size()));
    index = 0;
    for (const TimePeriodInfo& period : m_vUsingPeriod) {
        const auto start = FormatEventTime(period.m_nStartTime);
        const auto end = FormatEventTime(period.m_nEndTime);
        SendBuilderLine(user, u"USE_TIME%d = %s ~ %s", index, start.data(), end.data());
        ++index;
    }

    SendBuilderLine(user, u"HOME_GETTING_SET: %s", m_HomePCCafe.m_bGettingSet ? L"true" : L"false");
    SendBuilderLine(user, u"HOME_GETTING_MAX_SET: %s", m_HomePCCafe.m_bGettingMaxSet ? L"true" : L"false");
    SendBuilderLine(
        user, u"HOME_MAX_COUNT:%d HOME_POINT:%d HOME_RANDOMBONUS:%d", m_HomePCCafe.m_nGettingMaxCount, m_HomePCCafe.m_nPoint,
        m_HomePCCafe.m_nRandomPointProb
    );
    SendBuilderLine(user, u"HOME_GET_TIME_COUNT:%d", static_cast<int>(m_HomePCCafe.m_vGettingPeriod.size()));
    index = 0;
    for (const TimePeriodInfo& period : m_HomePCCafe.m_vGettingPeriod) {
        const auto start = FormatEventTime(period.m_nStartTime);
        const auto end = FormatEventTime(period.m_nEndTime);
        SendBuilderLine(user, u"HOME_GET_TIME%d = %s ~ %s", index, start.data(), end.data());
        ++index;
    }

    SendBuilderLine(user, u"PCCafeBonus:%d", static_cast<int>(m_PCCafeBonuses.size()));
    int bonusIndex = 0;
    for (const PCCafeBonus& bonus : m_PCCafeBonuses) {
        const TimePeriodInfo& period = bonus.m_vGettingPeriod.front();
        const auto start = FormatEventTime(period.m_nStartTime);
        const auto end = FormatEventTime(period.m_nEndTime);
        SendBuilderLine(
            user, u"PCCafeBonus_GET_TIME%d = %s ~ %s Type = %s PointTime = %d Item = %d Value = %d", bonusIndex,
            start.data(), end.data(), m_sBonusType[static_cast<std::size_t>(bonus.m_nBonusType)], bonus.m_nBonusPointTime,
            bonus.m_nBonusItemClassID, bonus.m_nBonusValue
        );
        ++bonusIndex;
    }
}

bool CEvent_PCCafe::InEventTime() {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return IsInPeriods(m_vUsingPeriod, now) || IsInPeriods(m_PCCafe.m_vGettingPeriod, now) ||
        IsInPeriods(m_HomePCCafe.m_vGettingPeriod, now) || IsInPeriods(m_DailyPCCafe.m_vGettingPeriod, now);
}

bool CEvent_PCCafe::InMyTick(int tick) { return 0 < m_nTick && m_nTick <= tick && tick % m_nTick == 0; }

void CEvent_PCCafe::DoTickAction(int tick) {
    static_cast<void>(tick);
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();

    if (GetUserEventEnabled(ECT_DAILYGIVE)) {
        L2_GUARD(mLock);
        const int date = GetYYYYMMDDFromTime(now);
        if (m_nSavedDate < date) {
            m_nSavedDate = date;
            ResetDailyGiveRecord();
        }
    }

    m_bGettingPCCafePoint = IsInPeriods(m_PCCafe.m_vGettingPeriod, now);

    // Current 0x00756414 scans the using periods, but then stores literal
    // true regardless of the scan result. Preserve that observable behavior.
    static_cast<void>(IsInPeriods(m_vUsingPeriod, now));
    m_bUsingPCCafePoint = true;

    SharedEventInfo::Inst()->m_PCCafePointEvent.m_PcCafePointEventState = static_cast<int>(GetPCCafeEventType());
}

bool CEvent_PCCafe::IsAllDone() {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return !HasUnfinishedPeriod(m_vUsingPeriod, now) && !HasUnfinishedPeriod(m_PCCafe.m_vGettingPeriod, now) &&
        !HasUnfinishedPeriod(m_HomePCCafe.m_vGettingPeriod, now) &&
        !HasUnfinishedPeriod(m_DailyPCCafe.m_vGettingPeriod, now);
}

std::size_t CEvent_PCCafe::GetEventPeriod(std::vector<TimePeriodInfo>& periods) {
    L2_GUARD_FUNCTION();
    AppendPeriods(periods, m_vUsingPeriod);
    AppendPeriods(periods, m_PCCafe.m_vGettingPeriod);
    AppendPeriods(periods, m_HomePCCafe.m_vGettingPeriod);
    AppendPeriods(periods, m_DailyPCCafe.m_vGettingPeriod);
    return periods.size();
}

GAME_EVENT::EVENT_TYPE CEvent_PCCafe::GetEventType() const { return GAME_EVENT::ET_PCCAFE_SETTING; }

CEvent_PCCafe::EventType CEvent_PCCafe::GetPCCafeEventType() {
    L2_GUARD_FUNCTION();
    if (m_bGettingPCCafePoint) {
        return GettingPoint;
    }
    if (m_bUsingPCCafePoint) {
        return UsingPoint;
    }
    return Nothing;
}

int CEvent_PCCafe::GetPCCafeEventRemainHour() {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    const std::vector<TimePeriodInfo>* periods = nullptr;
    if (m_bGettingPCCafePoint) {
        periods = &m_PCCafe.m_vGettingPeriod;
    } else if (m_bUsingPCCafePoint) {
        periods = &m_vUsingPeriod;
    }
    if (periods != nullptr) {
        for (const TimePeriodInfo& period : *periods) {
            if (now <= period.m_nEndTime) {
                return (period.m_nEndTime - now) / 3600;
            }
        }
    }
    return 0;
}

bool CEvent_PCCafe::GetHomeUserEventSet() {
    L2_GUARD_FUNCTION();
    return m_HomePCCafe.m_bGettingSet && (!m_HomePCCafe.m_bGettingMaxSet || 0 < m_HomePCCafe.m_nGettingMaxCount);
}

bool CEvent_PCCafe::GetUserEventEnabled(int eventCase) {
    L2_GUARD_FUNCTION();
    if (eventCase == ECT_HOMEEVENT && !GetHomeUserEventSet()) {
        return false;
    }

    const std::vector<TimePeriodInfo>* periods = nullptr;
    if (eventCase == ECT_HOMEEVENT) {
        periods = &m_HomePCCafe.m_vGettingPeriod;
    } else if (eventCase == ECT_DAILYGIVE) {
        periods = &m_DailyPCCafe.m_vGettingPeriod;
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"No event for pccafe case %d\n",
        //     eventCase)
        // L2_LOG_ERROR(Log,
        //     L"No event for pccafe case %d\n",
        //     eventCase)
        L2_LOG_ERROR(
            Log,
            "Cannot check PC-cafe event case %d: only home-event and daily-give cases are supported; the check returns "
            "disabled.",
            eventCase
        );
        return false;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    for (const TimePeriodInfo& period : *periods) {
        if (0 < period.m_nStartTime && period.m_nStartTime < period.m_nEndTime && IsInPeriod(period, now)) {
            return true;
        }
    }
    return false;
}

int CEvent_PCCafe::GetPCCafePoint(int randomValue) const noexcept {
    return m_PCCafe.m_nPoint + GetPCCafeRandomPoint(randomValue);
}

int CEvent_PCCafe::GetPCCafeRandomPoint(int randomValue) const noexcept {
    return randomValue < m_PCCafe.m_nRandomPointProb ? 1 : 0;
}

bool CEvent_PCCafe::CheckDayPassed(int time) const { return m_nSavedDate < GetYYYYMMDDFromTime(time); }

void CEvent_PCCafe::ResetDailyGiveRecord() {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    for (DailyGiveRecordType& record : m_DailyGiveRecordList) {
        CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, static_cast<int>(record.first)));
        record.second = static_cast<bool>(user);
    }
}

bool CEvent_PCCafe::CanGiveDailyPoint(std::uint32_t userDatabaseId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(mLock);
    for (DailyGiveRecordType& record : m_DailyGiveRecordList) {
        if (record.first == userDatabaseId) {
            const bool alreadyGiven = record.second;
            record.second = true;
            return !alreadyGiven;
        }
    }
    m_DailyGiveRecordList.push_back(DailyGiveRecordType(userDatabaseId, true));
    return true;
}

int CEvent_PCCafe::GetPCCafeExpBonus() {
    if (!InEventTime()) {
        return 0;
    }
    const std::int32_t now = runtime::crt::Time32Now();
    for (const PCCafeBonus& bonus : m_PCCafeBonuses) {
        if (bonus.m_nBonusType == BT_EXP && IsInPeriods(bonus.m_vGettingPeriod, now)) {
            return bonus.m_nBonusValue;
        }
    }
    return 0;
}

int CEvent_PCCafe::GetPCCafePVPPointBonus() {
    if (!InEventTime()) {
        return 0;
    }
    const std::int32_t now = runtime::crt::Time32Now();
    for (const PCCafeBonus& bonus : m_PCCafeBonuses) {
        if (bonus.m_nBonusType == BT_PVP_POINT && IsInPeriods(bonus.m_vGettingPeriod, now)) {
            return bonus.m_nBonusValue;
        }
    }
    return 0;
}

int CEvent_PCCafe::GetPCCafeLevelBonus(int bonusType, int level) {
    for (const PCCafeBonus& bonus : m_PCCafeBonuses) {
        if (static_cast<int>(bonus.m_nBonusType) != bonusType || bonus.m_nBonusLevel != level) {
            continue;
        }
        if (bonusType == BT_ITEM) {
            return 0 < bonus.m_nBonusValue ? bonus.m_nBonusValue : 1;
        }
        if (bonusType == BT_POINT) {
            return bonus.m_nBonusValue;
        }
    }
    return 0;
}

int CEvent_PCCafe::GetPCCafeItemBonus(int bonusType, int level) {
    for (const PCCafeBonus& bonus : m_PCCafeBonuses) {
        if (static_cast<int>(bonus.m_nBonusType) == bonusType && bonus.m_nBonusLevel == level && bonusType == BT_ITEM) {
            return bonus.m_nBonusItemClassID;
        }
    }
    return 0;
}

int CEvent_PCCafe::GetPCCafeBuffSkillBonus(int bonusType) {
    static_cast<void>(bonusType);
    if (!InEventTime()) {
        return 0;
    }
    const std::int32_t now = runtime::crt::Time32Now();
    for (const PCCafeBonus& bonus : m_PCCafeBonuses) {
        if (bonus.m_nBonusType == BT_BUFFSKILL && IsInPeriods(bonus.m_vGettingPeriod, now)) {
            return bonus.m_nBonusValue;
        }
    }
    return 0;
}

int CEvent_PCCafe::CheckPCCafeBonusItem(int point, int level) {
    if (!InEventTime()) {
        return -1;
    }
    const std::int32_t now = runtime::crt::Time32Now();
    for (const PCCafeBonus& bonus : m_PCCafeBonuses) {
        if ((bonus.m_nBonusType == BT_ITEM || bonus.m_nBonusType == BT_POINT) && bonus.m_nBonusLevel == level &&
            IsInPeriods(bonus.m_vGettingPeriod, now) && bonus.m_nBonusPointTime <= point) {
            return static_cast<int>(bonus.m_nBonusType);
        }
    }
    return -1;
}

void CEvent_PCCafe::SetPCCafeBonusTime(int value) { SharedEventInfo::Inst()->m_gameEvent.eventPCCafeBonusTime = value != 0; }

CEvent_XmasSanta::CEvent_XmasSanta() : m_nTick(0) {}

CEvent_XmasSanta::~CEvent_XmasSanta() = default;

bool CEvent_XmasSanta::Load(XMLElement* element) {
    // Current L2Server 0x0075bca8.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load XmasSantaEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x53b)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load XmasSantaEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x53b)
        L2_LOG_ERROR(Log, "Cannot load XmasSantaEvent: the XML node is missing or empty. This event is not loaded.");
        return false;
    }

    LoadRootElement(element);
    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_EventPeriod.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_EventPeriod.m_nEndTime);
    m_nTick = ToInt32(Attribute(element, u"Tick"));

    for (XMLElement* const child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        if (!EqualsIgnoreCase(child->m_sName, u"Buffs")) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] unknown element[%s] for XmasSantaEvent.",
            //     L"GameEventSystem.cpp",
            //     0x558,
            //     child->m_sName.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] unknown element[%s] for XmasSantaEvent.",
            //     L"GameEventSystem.cpp",
            //     0x558,
            //     child->m_sName.c_str())
            L2_LOG_WARN(
                Log, "XmasSantaEvent: unexpected XML element '%s'; this element is ignored and loading continues.",
                Utf16ToUtf8(child->m_sName).c_str()
            );
            continue;
        }
        for (XMLElement* const buff : child->m_vChild) {
            if (buff == nullptr) {
                continue;
            }
            const int buffId = ToInt32(Attribute(buff, u"id"));
            XmasSantaEventInfo info;
            info.m_nBuffID = buffId;
            m_mapBuffs.emplace(buffId, info);
        }
    }
    return true;
}

void CEvent_XmasSanta::ShowSetting(User*) {
    // Current folded no-op body 0x006fe5d4.
}

bool CEvent_XmasSanta::InEventTime() {
    // Current L2Server 0x0074cb7c.
    L2_GUARD_FUNCTION();
    return IsInPeriod(m_EventPeriod, runtime::crt::Time32Now());
}

bool CEvent_XmasSanta::InMyTick(const int tick) {
    // Current L2Server 0x00423078.
    return m_nTick > 0 && tick >= m_nTick && tick % m_nTick == 0;
}

void CEvent_XmasSanta::DoTickAction(int) {
    // Current L2Server 0x00750a5c.
    L2_GUARD_FUNCTION();
    for (auto& [buffId, info] : m_mapBuffs) {
        (void)buffId;
        if (info.m_bIsActive && runtime::raw_atomic::LoadI32Relaxed(&info.m_nOnStartTurky) == 0) {
            runtime::raw_atomic::ExchangeI32(&info.m_nOnStartSanta, 0);
        }
    }
}

bool CEvent_XmasSanta::IsAllDone() {
    // Current L2Server 0x004231e4 uses a strict comparison.
    return m_EventPeriod.m_nEndTime < runtime::crt::Time32Now();
}

GAME_EVENT::EVENT_TYPE CEvent_XmasSanta::GetEventType() const {
    // Current L2Server 0x00426420.
    return GAME_EVENT::ET_XMAS_SANTA;
}

XmasSantaEventInfo* CEvent_XmasSanta::GetEventInfo(const int buffId) {
    // Current L2Server 0x00751284.
    L2_GUARD_FUNCTION();
    const auto position = m_mapBuffs.find(buffId);
    return position == m_mapBuffs.end() ? nullptr : &position->second;
}

CEvent_VariationEvent::CEvent_VariationEvent() : nCancelFeePercentage(100), nGemStoneCountPercentage(100) {}

CEvent_VariationEvent::~CEvent_VariationEvent() = default;

bool CEvent_VariationEvent::Load(XMLElement* element) {
    // Current L2Server 0x0075d3d0.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load CEvent_VariationEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xe96)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load CEvent_VariationEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xe96)
        L2_LOG_ERROR(Log, "Cannot load VariationEvent: the XML node is missing or empty. This event is not loaded.");
        return false;
    }

    LoadRootElement(element);
    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_EventPeriod.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_EventPeriod.m_nEndTime);

    int cancelPercentage = 100;
    int gemStonePercentage = 100;
    for (XMLElement* const child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"CancelFee")) {
            cancelPercentage = ToInt32(Attribute(child, u"Percentage"));
            if (cancelPercentage < 0 || cancelPercentage > 100) {
                const int invalidPercentage = cancelPercentage;
                cancelPercentage = 100;
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Cancel fee percentage is invalid [%d]",
                //     L"GameEventSystem.cpp",
                //     0xeb3,
                //     cancelPercentage)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Cancel fee percentage is invalid [%d]",
                //     L"GameEventSystem.cpp",
                //     0xeb3,
                //     cancelPercentage)
                L2_LOG_WARN(
                    Log, "VariationEvent/CancelFee: percentage %d is outside 0..100; using 100%%.", invalidPercentage
                );
            }
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"GemStoneCount")) {
            gemStonePercentage = ToInt32(Attribute(child, u"Percentage"));
            if (gemStonePercentage < 0 || gemStonePercentage > 100) {
                const int invalidPercentage = gemStonePercentage;
                gemStonePercentage = 100;
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] GemStone count percentage is invalid [%d]",
                //     L"GameEventSystem.cpp",
                //     0xebe,
                //     gemStonePercentage)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] GemStone count percentage is invalid [%d]",
                //     L"GameEventSystem.cpp",
                //     0xebe,
                //     gemStonePercentage)
                L2_LOG_WARN(
                    Log, "VariationEvent/GemStoneCount: percentage %d is outside 0..100; using 100%%.", invalidPercentage
                );
            }
            continue;
        }
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] unknown element[%s] for CEvent_VariationEvent.",
        //     L"GameEventSystem.cpp",
        //     0xec4,
        //     child->m_sName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] unknown element[%s] for CEvent_VariationEvent.",
        //     L"GameEventSystem.cpp",
        //     0xec4,
        //     child->m_sName.c_str())
        L2_LOG_WARN(
            Log, "VariationEvent: unexpected XML element '%s'; this element is ignored and loading continues.",
            Utf16ToUtf8(child->m_sName).c_str()
        );
    }

    nCancelFeePercentage = cancelPercentage;
    nGemStoneCountPercentage = gemStonePercentage;
    return true;
}

void CEvent_VariationEvent::ShowSetting(User* user) {
    // Current L2Server 0x00754358.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    const auto start = FormatEventTime(m_EventPeriod.m_nStartTime);
    const auto end = FormatEventTime(m_EventPeriod.m_nEndTime);
    SendBuilderLine(
        user, u"VariationEvent EventPeriod(%s ~ %s) / GemStonePercentage: %d / CancelPercentage: %d", start.data(),
        end.data(), nGemStoneCountPercentage, nCancelFeePercentage
    );
}

bool CEvent_VariationEvent::InEventTime() {
    // Current L2Server 0x0074dd18 uses inclusive period bounds.
    L2_GUARD_FUNCTION();
    return IsInPeriod(m_EventPeriod, runtime::crt::Time32Now());
}

bool CEvent_VariationEvent::IsAllDone() {
    // Current folded body 0x004231e4 uses a strict end comparison.
    return m_EventPeriod.m_nEndTime < runtime::crt::Time32Now();
}

GAME_EVENT::EVENT_TYPE CEvent_VariationEvent::GetEventType() const {
    // Current folded body 0x0063093c.
    return GAME_EVENT::ET_VARIATION;
}

CEvent_OneTimeEvent::CEvent_OneTimeEvent() :
    m_nEventNumber(0) {}

CEvent_OneTimeEvent::~CEvent_OneTimeEvent() = default;

bool CEvent_OneTimeEvent::Load(XMLElement* element) {
    // Current L2Server 0x0075d698.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load CEvent_OneTimeEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xefb)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load CEvent_OneTimeEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xefb)
        L2_LOG_ERROR(Log, "Cannot load OneTimeEvent: the XML node is missing or empty. This event is not loaded.");
        return false;
    }

    LoadRootElement(element);
    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_EventPeriod.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_EventPeriod.m_nEndTime);

    for (XMLElement* const child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        if (!EqualsIgnoreCase(child->m_sName, u"Item")) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] unknown element[%s] for CEvent_VariationEvent.",
            //     L"GameEventSystem.cpp",
            //     0xf1b,
            //     child->m_sName.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] unknown element[%s] for CEvent_VariationEvent.",
            //     L"GameEventSystem.cpp",
            //     0xf1b,
            //     child->m_sName.c_str())
            L2_LOG_WARN(
                Log, "OneTimeEvent: unexpected XML element '%s'; this element is ignored and loading continues.",
                Utf16ToUtf8(child->m_sName).c_str()
            );
            continue;
        }

        const std::u16string& itemName = Attribute(child, u"Name");
        const int itemClassId = static_cast<int>(g_PrecompiledHeaderDB.Search(itemName.c_str()));
        if (itemClassId == -1) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Item name is invalid[%s]",
            //     L"GameEventSystem.cpp",
            //     0xf11,
            //     itemName.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Item name is invalid[%s]",
            //     L"GameEventSystem.cpp",
            //     0xf11,
            //     itemName.c_str())
            L2_LOG_ERROR(
                Log, "OneTimeEvent/Item: item '%s' is not defined; this entry is omitted.", Utf16ToUtf8(itemName).c_str()
            );
            continue;
        }
        m_mapItemInfo.emplace(itemClassId, ToInt64(Attribute(child, u"Amount")));
    }
    return true;
}

void CEvent_OneTimeEvent::ShowSetting(User* user) {
    // Current L2Server 0x007545bc.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    const auto start = FormatEventTime(m_EventPeriod.m_nStartTime);
    const auto end = FormatEventTime(m_EventPeriod.m_nEndTime);
    SendBuilderLine(user, u"OneTimeEvent EventPeriod(%s ~ %s)", start.data(), end.data());
}

bool CEvent_OneTimeEvent::InEventTime() {
    // Current L2Server 0x0074ddc0.
    L2_GUARD_FUNCTION();
    return IsInPeriod(m_EventPeriod, runtime::crt::Time32Now());
}

bool CEvent_OneTimeEvent::InMyTick(int) {
    // Current folded false body 0x0046c3fc.
    return false;
}

void CEvent_OneTimeEvent::DoTickAction(int) {
    // Current folded no-op body 0x006fe5d4.
}

bool CEvent_OneTimeEvent::IsAllDone() {
    // Current folded body 0x004231e4 uses a strict comparison.
    return m_EventPeriod.m_nEndTime < runtime::crt::Time32Now();
}

GAME_EVENT::EVENT_TYPE CEvent_OneTimeEvent::GetEventType() const {
    // Current folded body 0x006301f4.
    return GAME_EVENT::ET_ONETIME;
}

bool CEvent_OneTimeEvent::SetItemGiveToUser(const int databaseId) {
    // Current L2Server 0x00754808.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_UserInfoLock);
    const auto position = m_mapUserInfo.find(databaseId);
    if (position == m_mapUserInfo.end()) {
        m_mapUserInfo.emplace(databaseId, true);
        return true;
    }
    if (!position->second) {
        position->second = true;
        return false;
    }
    return true;
}

XmasWishCardResult::Enum CEvent_OneTimeEvent::IsUserReceived(const int databaseId) {
    // Current L2Server 0x00751370.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_UserInfoLock);
    const auto position = m_mapUserInfo.find(databaseId);
    return position != m_mapUserInfo.end() && position->second ? XmasWishCardResult::XWCR_ALREADY_RECEIVED
                                                               : XmasWishCardResult::XWCR_UNKNOWN;
}

void CEvent_OneTimeEvent::GiveItem(User& user) {
    // Current L2Server 0x00758db0.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_ItemGiveLock);
    if (m_mapItemInfo.empty()) {
        return;
    }

    L2_WRITE_GUARD(user.m_inv);
    for (const auto& [itemClassId, amount] : m_mapItemInfo) {
        InventoryParams parameters;
        parameters.nItemClassID = itemClassId;
        parameters.nCount = amount;
        user.AddItemToInventory(parameters);
    }
}

CEvent_PCCafeCoupon::CEvent_PCCafeCoupon() :
    m_nTick(0),
    m_nOrdinal(-1),
    m_EventState(NOT_DEFINED),
    m_EventPeriod{} {}

CEvent_PCCafeCoupon::~CEvent_PCCafeCoupon() = default;

bool CEvent_PCCafeCoupon::Load(XMLElement* element) {
    // Current L2Server 0x0075c79c.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.size() != 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load PCCafeCouponEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x6c8)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load PCCafeCouponEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x6c8)
        L2_LOG_ERROR(
            Log,
            "Cannot load PCCafeCouponEvent: the XML node is missing or has %llu children; expected exactly 2. This event is "
            "not loaded.",
            element != nullptr ? static_cast<unsigned long long>(element->m_vChild.size()) : 0ULL
        );
        return false;
    }

    LoadRootElement(element);
    m_nTick = ToInt32(Attribute(element, u"Tick"));
    m_nOrdinal = ToInt32(Attribute(element, u"Ordinal"));

    for (XMLElement* child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"EventPeriod")) {
            for (XMLElement* period : child->m_vChild) {
                if (period == nullptr) {
                    continue;
                }
                if (EqualsIgnoreCase(period->m_sName, u"Event")) {
                    TimePeriodInfo::ParseTime(Attribute(period, u"Start").c_str(), m_EventPeriod[0].m_nStartTime);
                    TimePeriodInfo::ParseTime(Attribute(period, u"End").c_str(), m_EventPeriod[0].m_nEndTime);
                } else if (EqualsIgnoreCase(period->m_sName, u"AddedEvent")) {
                    TimePeriodInfo::ParseTime(Attribute(period, u"Start").c_str(), m_EventPeriod[1].m_nStartTime);
                    TimePeriodInfo::ParseTime(Attribute(period, u"End").c_str(), m_EventPeriod[1].m_nEndTime);
                } else {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] unknown element[%s] for PCCafeCouponEvent > EventPeriod.",
                    //     L"GameEventSystem.cpp",
                    //     0x6f0,
                    //     period->m_sName.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] unknown element[%s] for PCCafeCouponEvent > EventPeriod.",
                    //     L"GameEventSystem.cpp",
                    //     0x6f0,
                    //     period->m_sName.c_str())
                    L2_LOG_WARN(
                        Log,
                        "PCCafeCouponEvent/EventPeriod: unexpected XML element '%s'; this element is ignored and loading "
                        "continues.",
                        Utf16ToUtf8(period->m_sName).c_str()
                    );
                }
            }
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"CouponItems")) {
            for (XMLElement* item : child->m_vChild) {
                if (item == nullptr) {
                    continue;
                }
                CouponItem couponItem;
                couponItem.m_nLevel = ToInt32(Attribute(item, u"Level"));
                couponItem.m_nItemClassID = g_PrecompiledHeaderDB.Search(Attribute(item, u"ItemName").c_str());
                if (couponItem.m_nItemClassID == -1) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Item name is invalid[%s]",
                    //     L"GameEventSystem.cpp",
                    //     0x702,
                    //     Attribute(item, u"ItemName").c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] Item name is invalid[%s]",
                    //     L"GameEventSystem.cpp",
                    //     0x702,
                    //     Attribute(item, u"ItemName").c_str())
                    L2_LOG_ERROR(
                        Log, "PCCafeCouponEvent/CouponItems: item '%s' is not defined; this entry is omitted.",
                        Utf16ToUtf8(Attribute(item, u"ItemName")).c_str()
                    );
                } else {
                    m_vCouponItems.push_back(couponItem);
                }
            }
            continue;
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] unknown element[%s] for PCCafeCouponEvent.",
        //     L"GameEventSystem.cpp",
        //     0x70b,
        //     child->m_sName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] unknown element[%s] for PCCafeCouponEvent.",
        //     L"GameEventSystem.cpp",
        //     0x70b,
        //     child->m_sName.c_str())
        L2_LOG_WARN(
            Log, "PCCafeCouponEvent: unexpected XML element '%s'; this element is ignored and loading continues.",
            Utf16ToUtf8(child->m_sName).c_str()
        );
    }

    CalcEventState();
    return true;
}

void CEvent_PCCafeCoupon::ShowSetting(User* user) {
    // Current L2Server 0x00752864.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    SendBuilderLine(user, u"PCCafeCouponEventState:%d EventOrdinal:%d", static_cast<int>(m_EventState), m_nOrdinal);

    const auto start = FormatEventTime(m_EventPeriod[0].m_nStartTime);
    const auto end = FormatEventTime(m_EventPeriod[0].m_nEndTime);
    SendBuilderLine(user, u"TimePeriod = %s ~ %s", start.data(), end.data());

    const auto addedStart = FormatEventTime(m_EventPeriod[1].m_nStartTime);
    const auto addedEnd = FormatEventTime(m_EventPeriod[1].m_nEndTime);
    SendBuilderLine(user, u"TimeExchangePeriod = %s ~ %s", addedStart.data(), addedEnd.data());
}

bool CEvent_PCCafeCoupon::InEventTime() {
    // Current L2Server 0x0074cffc.
    L2_GUARD_FUNCTION();
    if (m_nOrdinal == -1) {
        return false;
    }
    const std::int32_t now = runtime::crt::Time32Now();
    return IsInPeriod(m_EventPeriod[0], now) || IsInPeriod(m_EventPeriod[1], now);
}

bool CEvent_PCCafeCoupon::InMyTick(const int tick) {
    // Current folded body 0x00423078.
    return 0 < m_nTick && m_nTick <= tick && tick % m_nTick == 0;
}

void CEvent_PCCafeCoupon::DoTickAction(int) {
    // Current L2Server 0x0074ea60.
    L2_GUARD_FUNCTION();
    if (m_EventState != NOT_DEFINED) {
        CalcEventState();
    }
}

bool CEvent_PCCafeCoupon::IsAllDone() {
    // Current L2Server 0x0074d0bc. The number check and the OR between
    // the two end times are the literal target behavior.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return !(m_nOrdinal == -1 && (now < m_EventPeriod[0].m_nEndTime || now < m_EventPeriod[1].m_nEndTime));
}

std::size_t CEvent_PCCafeCoupon::GetEventPeriod(std::vector<TimePeriodInfo>& periods) {
    // Current L2Server 0x007576f4.
    L2_GUARD_FUNCTION();
    periods.push_back(m_EventPeriod[0]);
    periods.push_back(m_EventPeriod[1]);
    return periods.size();
}

GAME_EVENT::EVENT_TYPE CEvent_PCCafeCoupon::GetEventType() const {
    // Current folded body 0x005a7f00.
    return GAME_EVENT::ET_PCCAFE_COUPON_EVENT;
}

CCouponKey CEvent_PCCafeCoupon::Reversed(const char16_t* value) const {
    // Current L2Server 0x0074d16c.
    return CCouponKey::Reversed(value, static_cast<std::uint32_t>(m_nOrdinal));
}

bool CEvent_PCCafeCoupon::Find(const CCouponKey& key) const {
    // Current L2Server 0x0074d198.
    return m_CouponKeyMgr.Find(&key);
}

bool CEvent_PCCafeCoupon::IsValidUser(const int databaseId, int& remainingMinutes) {
    // Current L2Server 0x00757544.
    L2_GUARD_FUNCTION();
    CheckData& data = m_BlockedUsers[databaseId];
    if (data.m_nCount < 5) {
        return true;
    }

    const std::int32_t now = runtime::crt::Time32Now();
    remainingMinutes = 240 - (now - data.m_nBlockedTime) / 60;
    if (remainingMinutes > 0) {
        return false;
    }
    data.m_nCount = 0;
    data.m_nBlockedTime = runtime::crt::Time32Now();
    return true;
}

int CEvent_PCCafeCoupon::FailedInputCoupon(const int databaseId) {
    // Current L2Server 0x00757640.
    L2_GUARD_FUNCTION();
    CheckData& data = m_BlockedUsers[databaseId];
    ++data.m_nCount;
    if (4 < data.m_nCount) {
        data.m_nBlockedTime = runtime::crt::Time32Now();
    }
    return data.m_nCount;
}

int CEvent_PCCafeCoupon::GetCouponItemId(const int level) const {
    // Current L2Server 0x00750140 indexes element zero when no
    // threshold matches, including the target's unchecked empty-vector case.
    L2_GUARD_FUNCTION();
    for (const CouponItem& item : m_vCouponItems) {
        if (level <= item.m_nLevel) {
            return item.m_nItemClassID;
        }
    }
    return runtime::CheckedVectorAt(m_vCouponItems, 0).m_nItemClassID;
}

void CEvent_PCCafeCoupon::CalcEventState() {
    // Current L2Server 0x0074d1a4.
    L2_GUARD_FUNCTION();
    if (m_nOrdinal == -1) {
        m_EventState = NOT_IN_EVENT;
        return;
    }

    const EventState previous = m_EventState;
    const std::int32_t now = runtime::crt::Time32Now();
    if (IsInPeriod(m_EventPeriod[0], now)) {
        m_EventState = IN_EVENT;
    } else if (IsInPeriod(m_EventPeriod[1], now)) {
        m_EventState = IN_EXCHANGE;
    } else {
        m_EventState = NOT_IN_EVENT;
    }
    if (previous != m_EventState) {
        SharedEventInfo::Inst()->m_PCCafeCoupon.couponEventState = static_cast<int>(m_EventState);
    }
}

CEvent_RecipeSuccessRate::CEvent_RecipeSuccessRate() :
    m_EventState(-1), m_nDefaultSuccessRate(-1) {}

CEvent_RecipeSuccessRate::~CEvent_RecipeSuccessRate() = default;

bool CEvent_RecipeSuccessRate::Load(XMLElement* element) {
    // Current L2Server 0x0075dafc.
    L2_GUARD_FUNCTION();
    if (element == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load CEvent_RecipeSuccessRate. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xfe7)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load CEvent_RecipeSuccessRate. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xfe7)
        L2_LOG_ERROR(Log, "Cannot load RecipeSuccessRate: the XML node is missing. This event is not loaded.");
        return false;
    }

    LoadRootElement(element);
    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_EventPeriod.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_EventPeriod.m_nEndTime);
    m_nDefaultSuccessRate = ToInt32(Attribute(element, u"GetDefaultSuccessRate"));

    for (XMLElement* const child : element->m_vChild) {
        if (child == nullptr || !EqualsIgnoreCase(child->m_sName, u"Recipes")) {
            continue;
        }
        for (XMLElement* const recipe : child->m_vChild) {
            if (recipe == nullptr) {
                continue;
            }
            const int recipeId = ToInt32(Attribute(recipe, u"Id"));
            if (g_RecipeDB.find(recipeId) == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Recipe Id is invalid[%d]",
                //     L"GameEventSystem.cpp",
                //     0x1004,
                //     recipeId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Recipe Id is invalid[%d]",
                //     L"GameEventSystem.cpp",
                //     0x1004,
                //     recipeId)
                L2_LOG_ERROR(
                    Log, "RecipeSuccessRate/Recipes: recipe ID %d is not defined; this entry is omitted.", recipeId
                );
                continue;
            }
            if (FindRecipeMap(recipeId) != -1) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Recipe Id is Aready Exist[%d]",
                //     L"GameEventSystem.cpp",
                //     0x100a,
                //     recipeId)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Recipe Id is Aready Exist[%d]",
                //     L"GameEventSystem.cpp",
                //     0x100a,
                //     recipeId)
                L2_LOG_WARN(
                    Log,
                    "RecipeSuccessRate/Recipes: recipe ID %d already has a configured rate; this entry replaces that rate.",
                    recipeId
                );
            }
            m_mapRecipeSuccessRate[recipeId] = ToInt32(Attribute(recipe, u"SuccessRate"));
        }
    }
    m_EventState = -1;
    return true;
}

void CEvent_RecipeSuccessRate::ShowSetting(User* user) {
    // Current L2Server 0x00754ca4.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    const auto start = FormatEventTime(m_EventPeriod.m_nStartTime);
    const auto end = FormatEventTime(m_EventPeriod.m_nEndTime);
    SendBuilderLine(
        user, u"[RecipeSuccessRate Event] \n (%s ~ %s) \n DefaultRate = %d \n", start.data(), end.data(),
        m_nDefaultSuccessRate
    );
    for (const auto& [recipeId, rate] : m_mapRecipeSuccessRate) {
        SendBuilderLine(user, u"[Recipe id = %d SuccessRate = %d] \n", recipeId, rate);
    }
}

bool CEvent_RecipeSuccessRate::InEventTime() {
    // Current L2Server 0x0074de68.
    L2_GUARD_FUNCTION();
    return IsInPeriod(m_EventPeriod, runtime::crt::Time32Now());
}

bool CEvent_RecipeSuccessRate::InMyTick(int) {
    // Current folded body: always true.
    return true;
}

void CEvent_RecipeSuccessRate::DoTickAction(int) {
    // Current folded body: no operation.
}

bool CEvent_RecipeSuccessRate::IsAllDone() {
    // Current folded body shared with EconomyControlEvent.
    return m_EventPeriod.m_nEndTime <= runtime::crt::Time32Now();
}

GAME_EVENT::EVENT_TYPE CEvent_RecipeSuccessRate::GetEventType() const {
    // Current L2Server 0x00425b08.
    return GAME_EVENT::ET_RECIPE_SUCCESS_RATE;
}

int CEvent_RecipeSuccessRate::FindRecipeMap(const int recipeId) {
    // Current L2Server 0x007514b0.
    L2_GUARD_FUNCTION();
    const auto position = m_mapRecipeSuccessRate.find(recipeId);
    return position == m_mapRecipeSuccessRate.end() ? -1 : position->second;
}

int CEvent_RecipeSuccessRate::GetRecipeSuccessRate(const int recipeId) {
    // Current L2Server 0x00751598.
    L2_GUARD_FUNCTION();
    if (!InEventTime()) {
        return -1;
    }
    const int rate = FindRecipeMap(recipeId);
    return rate == -1 ? m_nDefaultSuccessRate : rate;
}

CEvent_EconomyControlEvent::CEvent_EconomyControlEvent() {
    // Current L2Server 0x00425bbc initializes only levels 1..120 even though
    // each physical table contains 121 doubles.
    std::fill_n(m_arrAdenaDropRatePerLevel.begin(), 120, 1.0);
    std::fill_n(m_arrItemDropRatePerLevel.begin(), 120, 1.0);
    std::fill_n(m_arrSpoilDropRatPerLevel.begin(), 120, 1.0);
    std::fill_n(m_arrExpSpGetRatePerLevel.begin(), 120, 1.0);
}

CEvent_EconomyControlEvent::~CEvent_EconomyControlEvent() {
    // Current L2Server 0x00425e08 performs the same observable reset before
    // the four vector owners are destroyed.
    m_vAdenaDropRateControl.clear();
    m_vItemDropRateControl.clear();
    m_vSpoilDropRateControl.clear();
    m_vExpSpGetRateControl.clear();
    std::fill_n(m_arrAdenaDropRatePerLevel.begin(), 120, 1.0);
    std::fill_n(m_arrItemDropRatePerLevel.begin(), 120, 1.0);
    std::fill_n(m_arrSpoilDropRatPerLevel.begin(), 120, 1.0);
    std::fill_n(m_arrExpSpGetRatePerLevel.begin(), 120, 1.0);
}

GAME_EVENT::EVENT_TYPE CEvent_EconomyControlEvent::GetEventType() const {
    // Current L2Server 0x00425de0.
    return GAME_EVENT::ET_ECONOMY_CONTROL;
}

bool CEvent_EconomyControlEvent::IsAllDone() {
    // Current L2Server 0x00425de8 uses <=, not a strict comparison.
    return m_EventPeriod.m_nEndTime <= runtime::crt::Time32Now();
}

bool CEvent_EconomyControlEvent::InEventTime() {
    // Current L2Server 0x0074df10.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return m_EventPeriod.m_nStartTime <= now && now <= m_EventPeriod.m_nEndTime;
}

double CEvent_EconomyControlEvent::GetAdenaDropRate(int level) {
    // Current L2Server 0x0074dfb8 has no upper-bound check.
    L2_GUARD_FUNCTION();
    return level < 1 ? 1.0 : m_arrAdenaDropRatePerLevel[static_cast<std::size_t>(level - 1)];
}

double CEvent_EconomyControlEvent::GetItemDropRate(int level) {
    // Current L2Server 0x0074e06c.
    L2_GUARD_FUNCTION();
    return level < 1 ? 1.0 : m_arrItemDropRatePerLevel[static_cast<std::size_t>(level - 1)];
}

double CEvent_EconomyControlEvent::GetSpoilDropRate(int level) {
    // Current L2Server 0x0074e120.
    L2_GUARD_FUNCTION();
    return level < 1 ? 1.0 : m_arrSpoilDropRatPerLevel[static_cast<std::size_t>(level - 1)];
}

double CEvent_EconomyControlEvent::GetExpSpGetRate(int level) {
    // Current L2Server 0x0074e1d4.
    L2_GUARD_FUNCTION();
    return level < 1 ? 1.0 : m_arrExpSpGetRatePerLevel[static_cast<std::size_t>(level - 1)];
}

bool CEvent_EconomyControlEvent::Load(XMLElement* element) {
    // Current L2Server 0x0075de24.
    L2_GUARD_FUNCTION();
    if (element == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load CEvent_EconomyControlEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x1099)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load CEvent_EconomyControlEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x1099)
        L2_LOG_ERROR(Log, "Cannot load EconomyControlEvent: the XML node is missing. This event is not loaded.");
        return false;
    }

    LoadRootElement(element);
    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_EventPeriod.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_EventPeriod.m_nEndTime);

    const auto loadControl = [](XMLElement* child, std::vector<EconomyControl>& controls, std::array<double, 121>& rates) {
        EconomyControl control;
        control.m_nMinLevel = ToInt32(Attribute(child, u"MinLev"));
        control.m_nMaxLevel = ToInt32(Attribute(child, u"MaxLev"));
        control.m_nRate = ToInt32(Attribute(child, u"Rate"));
        control.m_dRate = static_cast<double>(control.m_nRate) / 100.0;
        controls.push_back(control);
        for (int index = control.m_nMinLevel - 1; index < control.m_nMaxLevel; ++index) {
            rates[static_cast<std::size_t>(index)] = control.m_dRate;
        }
    };

    for (XMLElement* child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"AdenaDropRate")) {
            loadControl(child, m_vAdenaDropRateControl, m_arrAdenaDropRatePerLevel);
        } else if (EqualsIgnoreCase(child->m_sName, u"ItemDropRate")) {
            loadControl(child, m_vItemDropRateControl, m_arrItemDropRatePerLevel);
        } else if (EqualsIgnoreCase(child->m_sName, u"SpoilDropRate")) {
            loadControl(child, m_vSpoilDropRateControl, m_arrSpoilDropRatPerLevel);
        } else if (EqualsIgnoreCase(child->m_sName, u"ExpSpGetRate")) {
            loadControl(child, m_vExpSpGetRateControl, m_arrExpSpGetRatePerLevel);
        }
    }
    return true;
}

void CEvent_EconomyControlEvent::ShowSetting(User* user) {
    // Current L2Server 0x00754f9c.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const auto start = FormatEventTime(m_EventPeriod.m_nStartTime);
    const auto end = FormatEventTime(m_EventPeriod.m_nEndTime);
    SendBuilderLine(user, u"[Economy Control Event] \n (%ls ~ %ls) \n", start.data(), end.data());

    const auto showControls = [user](const char16_t* title, const std::vector<EconomyControl>& controls) {
        if (controls.empty()) {
            return;
        }
        SendBuilderLine(user, title);
        for (const EconomyControl& control : controls) {
            SendBuilderLine(user, u"%d ~ %d : %d\n", control.m_nMinLevel, control.m_nMaxLevel, control.m_nRate);
        }
    };

    showControls(u"[AdenaDropRate]\n", m_vAdenaDropRateControl);
    showControls(u"[ItemDropRate]\n", m_vItemDropRateControl);
    showControls(u"[SpoilDropRate]\n", m_vSpoilDropRateControl);
    showControls(u"[ExpSpGetRate]\n", m_vExpSpGetRateControl);
}

CEvent_AccountEvent::CEvent_AccountEvent() :
    m_nEventTick(0),
    m_InitTime{},
    m_nMaxStamp(0),
    m_nResetInterval(0),
    m_bPremiumOnly(false),
    m_Type(101),
    m_nAttendMinLevel(0) {}

CEvent_AccountEvent::~CEvent_AccountEvent() = default;

GAME_EVENT::EVENT_TYPE CEvent_AccountEvent::GetEventType() const { return GAME_EVENT::ET_ACCOUNT_EVENT; }

bool CEvent_AccountEvent::InEventTime() {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return m_EventPeriod.m_nStartTime <= now && now <= m_EventPeriod.m_nEndTime;
}

bool CEvent_AccountEvent::IsAllDone() { return m_EventPeriod.m_nEndTime < runtime::crt::Time32Now(); }

void CEvent_AccountEvent::ShowSetting(User*) { L2_GUARD_FUNCTION(); }

void CEvent_AccountEvent::LoadAttandanceRewardItem(XMLElement* element) {
    L2_GUARD_FUNCTION();
    if (element == nullptr) {
        return;
    }
    for (XMLElement* child : element->m_vChild) {
        if (child == nullptr || !EqualsIgnoreCase(child->m_sName, u"Item")) {
            continue;
        }
        ItemRewardInfo reward;
        reward.nItemClassId = ToInt32(Attribute(child, u"Id"));
        reward.nAmount = ToInt64(Attribute(child, u"Amount"));
        const int when = ToInt32(Attribute(child, u"When"));
        if (when <= m_nMaxStamp) {
            m_mapAttendReward.emplace(when, reward);
        }
    }
}

void CEvent_AccountEvent::LoadStamp(XMLElement* element) {
    L2_GUARD_FUNCTION();
    if (element == nullptr || !EqualsIgnoreCase(element->m_sName, u"Stamp")) {
        return;
    }

    std::u16string& maximum = Attribute(element, u"Max");
    if (!maximum.empty()) {
        m_nMaxStamp = ToInt32(maximum);
    }

    std::u16string& dailyReset = Attribute(element, u"DailyReset");
    if (!dailyReset.empty()) {
        int hour = 0;
        int minute = 0;
        int second = 0;
        const std::string resetText = Utf16ToUtf8(runtime::StringView(dailyReset));
        std::sscanf(resetText.c_str(), "%d:%d:%d", &hour, &minute, &second);
        m_nResetInterval = (hour * 60 + minute) * 60 + second;
    }

    std::u16string& type = Attribute(element, u"Type");
    if (type.empty() || EqualsIgnoreCase(type, u"Attendance")) {
        m_Type = 0;
        LoadAttandanceRewardItem(element);
    }

    std::u16string& minimumLevel = Attribute(element, u"MinLevel");
    if (!minimumLevel.empty()) {
        m_nAttendMinLevel = ToInt32(minimumLevel);
    }

    std::u16string& premiumOnly = Attribute(element, u"PremiumOnly");
    if (!premiumOnly.empty() && EqualsIgnoreCase(premiumOnly, u"true")) {
        m_bPremiumOnly = true;
    }
}

bool CEvent_AccountEvent::Load(XMLElement* element) {
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load CEvent_AccountEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x127a)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load CEvent_AccountEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x127a)
        L2_LOG_ERROR(Log, "Cannot load AccountEvent: the XML node is missing or empty. This event is not loaded.");
        return false;
    }

    LoadRootElement(element);
    for (XMLElement* child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"Period")) {
            TimePeriodInfo::ParseTime(Attribute(child, u"Start").c_str(), m_EventPeriod.m_nStartTime);
            TimePeriodInfo::ParseTime(Attribute(child, u"End").c_str(), m_EventPeriod.m_nEndTime);
        } else if (EqualsIgnoreCase(child->m_sName, u"Stamp")) {
            LoadStamp(child);
        }
    }
    return true;
}

bool CEvent_AccountEvent::CanSendMsg(const int accountId) {
    L2_GUARD_FUNCTION();
    const int bucket = accountId % 16;
    L2_GUARD(m_lock[static_cast<std::size_t>(bucket)]);
    SendMessageTimeMap& messages = m_mapTimeTable[static_cast<std::size_t>(bucket)];
    const auto found = messages.find(accountId);
    if (found == messages.end()) {
        return true;
    }
    const std::int32_t now = runtime::crt::Time32Now();
    return found->second < AccountEventResetBoundary(now, m_nResetInterval);
}

void CEvent_AccountEvent::SetSendMsg(const int accountId, const bool sent) {
    L2_GUARD_FUNCTION();
    const int bucket = accountId % 16;
    L2_GUARD(m_lock[static_cast<std::size_t>(bucket)]);
    SendMessageTimeMap& messages = m_mapTimeTable[static_cast<std::size_t>(bucket)];
    const auto found = messages.find(accountId);
    if (found == messages.end()) {
        if (!sent) {
            messages.emplace(accountId, runtime::crt::Time32Now());
        }
        return;
    }

    if (sent) {
        messages.erase(found);
        return;
    }
    const std::int32_t now = runtime::crt::Time32Now();
    found->second = AccountEventResetBoundary(now, m_nResetInterval);
}

bool CEvent_DormantBuffEvent::Load(XMLElement* element) {
    // Current L2Server.exe 0x0075ed9c.
    L2_GUARD_FUNCTION();
    if (element == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load CEvent_DormantClover. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x1663)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load CEvent_DormantClover. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x1663)
        L2_LOG_ERROR(Log, "Cannot load DormantBuff: the XML node is missing. This event is not loaded.");
        return false;
    }

    LoadRootElement(element);
    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_EventPeriod.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_EventPeriod.m_nEndTime);
    if (m_EventPeriod.m_nStartTime != 0 && m_EventPeriod.m_nEndTime != 0 &&
        m_EventPeriod.m_nStartTime <= m_EventPeriod.m_nEndTime) {
        std::wstring dormantName = Utf16ToWide(Attribute(element, u"DormantBuff"));
        m_dormantSkill = g_SkillDB.GetSkillKeyByName(dormantName.data());
        std::wstring newbieName = Utf16ToWide(Attribute(element, u"NewbieBuff"));
        m_newbieSkill = g_SkillDB.GetSkillKeyByName(newbieName.data());
        return true;
    }

    // The current target supplies only these two integer varargs despite the
    // four conversion fields in the embedded format string.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] fail to load CEvent_DormantClover invalid period start[%d] end[%d]",
    //     m_EventPeriod.m_nStartTime,
    //     m_EventPeriod.m_nEndTime)
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] fail to load CEvent_DormantClover invalid period start[%d] end[%d]",
    //     m_EventPeriod.m_nStartTime,
    //     m_EventPeriod.m_nEndTime)
    L2_LOG_ERROR(
        Log, "Cannot load DormantBuff: start/end timestamps %d..%d must both be nonzero and start must not exceed end.",
        m_EventPeriod.m_nStartTime, m_EventPeriod.m_nEndTime
    );
    return false;
}

void CEvent_DormantBuffEvent::ShowSetting(User* user) {
    // Current L2Server.exe 0x00755ce0.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    const auto start = FormatEventTime(m_EventPeriod.m_nStartTime);
    const auto end = FormatEventTime(m_EventPeriod.m_nEndTime);
    SendBuilderLine(user, u"DormantClover EventPeriod(%s ~ %s) ", start.data(), end.data());
}

bool CEvent_DormantBuffEvent::InEventTime() {
    // Current L2Server.exe 0x0074e9b8.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return m_EventPeriod.m_nStartTime <= now && now <= m_EventPeriod.m_nEndTime;
}

CEvent_Balthus::CEvent_Balthus() :
    m_nNpcTick(0),
    m_nNpcStartTime(0),
    m_nNpcEndTime(0),
    m_arrWinningProbability{},
    m_nStartTime(0),
    m_nMinimumLevel(0),
    m_nMinimumChangedClass(0),
    m_nMinimumUserCount(100),
    m_fMinimumUserPenalty(0.2F),
    m_nCurrentRewardSet(-1),
    m_nCurrentRewardIndex(-1) {
    // Current L2Server 0x00428a8c -> Clear@0x007615a4.
    Clear();
}

CEvent_Balthus::~CEvent_Balthus() = default;

void CEvent_Balthus::Clear() {
    // Current L2Server 0x007615a4 leaves m_vNpcInfo intact.
    L2_GUARD_FUNCTION();
    m_arrWinningProbability.fill(10);
    m_vRewardSet.clear();
    m_nCurrentRewardSet = -1;
    m_nCurrentRewardIndex = -1;
    m_nStartTime = 0;
    m_nMinimumLevel = 0;
    m_nMinimumChangedClass = 0;
    m_nMinimumUserCount = 100;
    m_fMinimumUserPenalty = 0.2F;
}

bool CEvent_Balthus::InEventTime() {
    // Current L2Server 0x0074e6a0.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return m_EventPeriod.m_nStartTime <= now && now <= m_EventPeriod.m_nEndTime;
}

bool CEvent_Balthus::IsAllDone() {
    // Current L2Server 0x004231e4.
    return m_EventPeriod.m_nEndTime < runtime::crt::Time32Now();
}

GAME_EVENT::EVENT_TYPE CEvent_Balthus::GetEventType() const {
    // Current L2Server 0x00428b50.
    return GAME_EVENT::ET_BALTHUS;
}

int CEvent_Balthus::GetCurrentState() const {
    // Current L2Server 0x0074e748.
    L2_GUARD_FUNCTION();
    const std::tm* const local = runtime::crt::LocalTime32(runtime::crt::Time32Now());
    const int difference = local->tm_hour - m_nStartTime / 3600;
    return difference + (difference < 0 ? 25 : 1);
}

int CEvent_Balthus::GetWinningProbability(const int progress) const {
    // Current L2Server 0x0074e818 literally accepts index 5 even though the
    // declared array has five elements. Index 5 aliases m_nStartTime.
    L2_GUARD_FUNCTION();
    if (static_cast<unsigned int>(progress) >= 6U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid Balthus Event Progress",
        //     L"GameEventSystem.cpp",
        //     0x15f5)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid Balthus Event Progress",
        //     L"GameEventSystem.cpp",
        //     0x15f5)
        L2_LOG_WARN(Log, "BalthusEvent: progress index %d is outside 0..5; using winning probability 10.", progress);
        return 10;
    }

    const int value = progress == 5 ? m_nStartTime : m_arrWinningProbability[static_cast<std::size_t>(progress)];
    if (g_nPlayingUserCount < m_nMinimumUserCount) {
        return static_cast<int>(static_cast<float>(value) * m_fMinimumUserPenalty);
    }
    return value;
}

void CEvent_Balthus::ShuffleRewards() {
    // Current L2Server 0x00751b9c uses the shipping random_shuffle body.
    L2_GUARD_FUNCTION();
    for (BalthusSet& rewardSet : m_vRewardSet) {
        for (std::size_t remaining = rewardSet.m_vRewards.size(); remaining > 1; --remaining) {
            const std::size_t selected = static_cast<std::size_t>(LegacyCrtRand()) % remaining;
            std::swap(
                runtime::CheckedVectorAt(rewardSet.m_vRewards, remaining - 1), runtime::CheckedVectorAt(rewardSet.m_vRewards, selected)
            );
        }
    }
}

void CEvent_Balthus::UpdateRewardSetInfo(const int state) {
    // Current L2Server 0x00751c94.
    L2_GUARD_FUNCTION();
    const int startHour = m_nStartTime / 3600;
    int seconds = (state - 1 + startHour) * 3600;
    if (seconds >= 86400) {
        seconds -= 86400;
    }

    for (std::size_t index = 0; index < m_vRewardSet.size(); ++index) {
        BalthusSet& rewardSet = runtime::CheckedVectorAt(m_vRewardSet, index);
        if (rewardSet.m_nStartTime <= seconds && seconds <= rewardSet.m_nEndTime) {
            if (m_nCurrentRewardSet != static_cast<int>(index)) {
                m_nCurrentRewardSet = static_cast<int>(index);
                ShuffleRewards();
            }
            m_nCurrentRewardIndex = state - 1 - rewardSet.m_nStartTime / 3600 + startHour;
            if (m_nCurrentRewardIndex > 23) {
                m_nCurrentRewardIndex -= 24;
            }
            break;
        }
    }

    if (m_nCurrentRewardSet < 0 || static_cast<std::size_t>(m_nCurrentRewardSet) >= m_vRewardSet.size()) {
        const int invalidRewardSet = m_nCurrentRewardSet;
        const int invalidRewardIndex = m_nCurrentRewardIndex;
        m_nCurrentRewardSet = 0;
        m_nCurrentRewardIndex = 0;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid Reward Set",
        //     L"GameEventSystem.cpp",
        //     0x15c6)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid Reward Set",
        //     L"GameEventSystem.cpp",
        //     0x15c6)
        L2_LOG_ERROR(
            Log,
            "BalthusEvent state %d: reward-set index %d is outside %llu sets (reward index %d); both indexes are reset to "
            "0.",
            state, invalidRewardSet, static_cast<unsigned long long>(m_vRewardSet.size()), invalidRewardIndex
        );
        return;
    }

    const BalthusSet& rewardSet = runtime::CheckedVectorAt(m_vRewardSet, static_cast<std::size_t>(m_nCurrentRewardSet));
    if (m_nCurrentRewardIndex < 0 || static_cast<std::size_t>(m_nCurrentRewardIndex) >= rewardSet.m_vRewards.size()) {
        const int invalidRewardIndex = m_nCurrentRewardIndex;
        m_nCurrentRewardIndex = 0;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid Reward Index",
        //     L"GameEventSystem.cpp",
        //     0x15d0)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid Reward Index",
        //     L"GameEventSystem.cpp",
        //     0x15d0)
        L2_LOG_ERROR(
            Log, "BalthusEvent state %d, reward set %d: reward index %d is outside %llu rewards; the index is reset to 0.",
            state, m_nCurrentRewardSet, invalidRewardIndex, static_cast<unsigned long long>(rewardSet.m_vRewards.size())
        );
    }
}

int CEvent_Balthus::BuilderCmd_GetRewardItem(const int state) {
    // Current L2Server 0x00751ecc performs unchecked indexing after update.
    L2_GUARD_FUNCTION();
    UpdateRewardSetInfo(state);
    return runtime::CheckedVectorAt(
        runtime::CheckedVectorAt(m_vRewardSet, static_cast<std::size_t>(m_nCurrentRewardSet)).m_vRewards,
        static_cast<std::size_t>(m_nCurrentRewardIndex)
    )
        .m_nItemId;
}

int CEvent_Balthus::BuilderCmd_GetTokenCount(const int state) {
    // Current L2Server 0x00751f90 performs unchecked indexing after update.
    L2_GUARD_FUNCTION();
    UpdateRewardSetInfo(state);
    return runtime::CheckedVectorAt(
        runtime::CheckedVectorAt(m_vRewardSet, static_cast<std::size_t>(m_nCurrentRewardSet)).m_vRewards,
        static_cast<std::size_t>(m_nCurrentRewardIndex)
    )
        .m_nTokenCount;
}

int CEvent_Balthus::GetCurrentRewardItem() {
    // Current L2Server 0x007558fc has its own guard and does not route through
    // the separately guarded builder-command function.
    L2_GUARD_FUNCTION();
    UpdateRewardSetInfo(GetCurrentState());
    return runtime::CheckedVectorAt(
        runtime::CheckedVectorAt(m_vRewardSet, static_cast<std::size_t>(m_nCurrentRewardSet)).m_vRewards,
        static_cast<std::size_t>(m_nCurrentRewardIndex)
    )
        .m_nItemId;
}

int CEvent_Balthus::GetCurrentTokenCount() {
    // Current L2Server 0x007559c8 has the matching independent body.
    L2_GUARD_FUNCTION();
    UpdateRewardSetInfo(GetCurrentState());
    return runtime::CheckedVectorAt(
        runtime::CheckedVectorAt(m_vRewardSet, static_cast<std::size_t>(m_nCurrentRewardSet)).m_vRewards,
        static_cast<std::size_t>(m_nCurrentRewardIndex)
    )
        .m_nTokenCount;
}

bool CEvent_Balthus::ValidateRewards() const {
    // Current L2Server 0x0075031c.
    L2_GUARD_FUNCTION();
    int total = 0;
    for (const BalthusSet& rewardSet : m_vRewardSet) {
        const int expected = (rewardSet.m_nEndTime - rewardSet.m_nStartTime + 60) / 3600;
        if (static_cast<int>(rewardSet.m_vRewards.size()) != expected) {
            return false;
        }
        total += expected;
    }
    return total == 24;
}

bool CEvent_Balthus::LoadNPC(XMLElement* element) {
    // Current L2Server 0x007616b0.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load Balthus Event NPC. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x152a)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load Balthus Event NPC. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x152a)
        L2_LOG_ERROR(Log, "Cannot load BalthusEvent/NPC: the XML node is missing or empty. Fatal configuration error.");
        ForceCrash(CrashInfo::PARSING_ERROR_EVENTDATA_FILE);
    }

    const bool validStart = TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_nNpcStartTime);
    const bool validEnd = TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_nNpcEndTime);
    if (!validStart || !validEnd) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid NPC Spawn Time StartTime[%d] EndTime[%d]",
        //     L"GameEventSystem.cpp",
        //     0x1533,
        //     static_cast<int>(validStart),
        //     static_cast<int>(validEnd))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid NPC Spawn Time StartTime[%d] EndTime[%d]",
        //     L"GameEventSystem.cpp",
        //     0x1533,
        //     static_cast<int>(validStart),
        //     static_cast<int>(validEnd))
        L2_LOG_ERROR(
            Log, "BalthusEvent/NPC: invalid spawn period (Start parsed: %s, End parsed: %s); fatal configuration error.",
            validStart ? "yes" : "no", validEnd ? "yes" : "no"
        );
        ForceCrash(CrashInfo::PARSING_ERROR_EVENTDATA_FILE);
    }
    m_nNpcTick = ToInt32(Attribute(element, u"Tick"));

    for (XMLElement* child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        BalthusNpcInfo info;
        info.m_sMakerEventName = Attribute(child, u"MakerEventName");
        for (XMLElement* periodElement : child->m_vChild) {
            if (periodElement == nullptr) {
                continue;
            }
            TimePeriodInfo period;
            TimePeriodInfo::ParseTime(Attribute(periodElement, u"Start").c_str(), period.m_nStartTime);
            TimePeriodInfo::ParseTime(Attribute(periodElement, u"End").c_str(), period.m_nEndTime);
            info.m_vPeriod.push_back(period);
        }
        m_vNpcInfo.push_back(std::move(info));
    }
    return true;
}

bool CEvent_Balthus::Load(XMLElement* element) {
    // Current L2Server 0x00763014.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load CEvent_Balthus. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x1411)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load CEvent_Balthus. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x1411)
        L2_LOG_ERROR(Log, "Cannot load BalthusEvent: the XML node is missing or empty. Fatal configuration error.");
        ForceCrash(CrashInfo::PARSING_ERROR_EVENTDATA_FILE);
    }

    LoadRootElement(element);
    if (!TimePeriodInfo::ParseTime(Attribute(element, u"StartSeason").c_str(), m_EventPeriod.m_nStartTime)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid Start Season",
        //     L"GameEventSystem.cpp",
        //     0x1419)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid Start Season",
        //     L"GameEventSystem.cpp",
        //     0x1419)
        L2_LOG_ERROR(Log, "BalthusEvent: cannot parse StartSeason; fatal configuration error.");
        ForceCrash(CrashInfo::PARSING_ERROR_EVENTDATA_FILE);
    }
    if (!TimePeriodInfo::ParseTime(Attribute(element, u"EndSeason").c_str(), m_EventPeriod.m_nEndTime)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid End Season",
        //     L"GameEventSystem.cpp",
        //     0x141f)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid End Season",
        //     L"GameEventSystem.cpp",
        //     0x141f)
        L2_LOG_ERROR(Log, "BalthusEvent: cannot parse EndSeason; fatal configuration error.");
        ForceCrash(CrashInfo::PARSING_ERROR_EVENTDATA_FILE);
    }
    if (!TimePeriodInfo::ParseHourMin(Attribute(element, u"StartTime").c_str(), m_nStartTime)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid Start Time",
        //     L"GameEventSystem.cpp",
        //     0x1425)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid Start Time",
        //     L"GameEventSystem.cpp",
        //     0x1425)
        L2_LOG_ERROR(Log, "BalthusEvent: cannot parse StartTime; fatal configuration error.");
        ForceCrash(CrashInfo::PARSING_ERROR_EVENTDATA_FILE);
    }

    bool hasConstraint = false;
    bool hasNpc = false;
    bool hasProbabilities = false;

    for (XMLElement* child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"RewardSet")) {
            BalthusSet rewardSet;
            rewardSet.m_nStartTime = 0;
            rewardSet.m_nEndTime = 86400;
            std::u16string& start = Attribute(child, u"StartTime");
            std::u16string& end = Attribute(child, u"EndTime");
            const bool validStart = TimePeriodInfo::ParseHourMin(start.c_str(), rewardSet.m_nStartTime);
            const bool validEnd = TimePeriodInfo::ParseHourMin(end.c_str(), rewardSet.m_nEndTime);
            if (!validStart || !validEnd || rewardSet.m_nEndTime < rewardSet.m_nStartTime) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Invalid RewardSet Time. StartTime[%d] EndTime[%d]",
                //     L"GameEventSystem.cpp",
                //     0x1442,
                //     rewardSet.m_nStartTime,
                //     rewardSet.m_nEndTime)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Invalid RewardSet Time. StartTime[%d] EndTime[%d]",
                //     L"GameEventSystem.cpp",
                //     0x1442,
                //     rewardSet.m_nStartTime,
                //     rewardSet.m_nEndTime)
                L2_LOG_ERROR(
                    Log,
                    "BalthusEvent/RewardSet: invalid time range '%s'..'%s' (Start parsed: %s, End parsed: %s; seconds "
                    "%d..%d); fatal configuration error.",
                    Utf16ToUtf8(start).c_str(), Utf16ToUtf8(end).c_str(), validStart ? "yes" : "no", validEnd ? "yes" : "no",
                    rewardSet.m_nStartTime, rewardSet.m_nEndTime
                );
                ForceCrash(CrashInfo::PARSING_ERROR_EVENTDATA_FILE);
                return false;
            }
            for (XMLElement* rewardElement : child->m_vChild) {
                if (rewardElement == nullptr) {
                    continue;
                }
                BalthusReward reward;
                const std::u16string& itemName = Attribute(rewardElement, u"ItemName");
                reward.m_nItemId = static_cast<int>(g_PrecompiledHeaderDB.Search(itemName.c_str()));
                reward.m_nTokenCount = ToInt32(Attribute(rewardElement, u"Token"));
                if (reward.m_nItemId == -1) {
                    const std::string hostItemName = Utf16ToUtf8(runtime::StringView(itemName));
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Item name is invalid[%s]",
                    //     L"GameEventSystem.cpp",
                    //     0x1457,
                    //     hostItemName.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] Item name is invalid[%s]",
                    //     L"GameEventSystem.cpp",
                    //     0x1457,
                    //     hostItemName.c_str())
                    L2_LOG_ERROR(
                        Log, "BalthusEvent/RewardSet: item '%s' is not defined; fatal configuration error.",
                        hostItemName.c_str()
                    );
                    ForceCrash(CrashInfo::PARSING_ERROR_EVENTDATA_FILE);
                }
                rewardSet.m_vRewards.push_back(reward);
            }
            m_vRewardSet.push_back(std::move(rewardSet));
        } else if (EqualsIgnoreCase(child->m_sName, u"Constraint")) {
            m_nMinimumLevel = ToInt32(Attribute(child, u"MinLevel"));
            m_nMinimumChangedClass = ToInt32(Attribute(child, u"MinChangedClass"));
            if (m_nMinimumLevel < 0 || m_nMinimumLevel > 120) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Invalid Reward Level[%d]",
                //     L"GameEventSystem.cpp",
                //     0x1467,
                //     m_nMinimumLevel)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Invalid Reward Level[%d]",
                //     L"GameEventSystem.cpp",
                //     0x1467,
                //     m_nMinimumLevel)
                L2_LOG_ERROR(
                    Log, "BalthusEvent/Constraint: MinLevel %d is outside 0..120; fatal configuration error.",
                    m_nMinimumLevel
                );
                ForceCrash(CrashInfo::PARSING_ERROR_EVENTDATA_FILE);
            }
            hasConstraint = true;
        } else if (EqualsIgnoreCase(child->m_sName, u"NPC")) {
            if (!LoadNPC(child)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Invalid NPC Data",
                //     L"GameEventSystem.cpp",
                //     0x1477)
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Invalid NPC Data",
                //     L"GameEventSystem.cpp",
                //     0x1477)
                L2_LOG_ERROR(Log, "BalthusEvent: loading the NPC section failed; fatal configuration error.");
                ForceCrash(CrashInfo::PARSING_ERROR_EVENTDATA_FILE);
            }
            hasNpc = true;
        } else if (EqualsIgnoreCase(child->m_sName, u"Probs")) {
            m_nMinimumUserCount = ToInt32(Attribute(child, u"MinimumUser"));
            m_fMinimumUserPenalty = static_cast<float>(ToDouble(Attribute(child, u"Penalty")));
            std::size_t index = 0;
            for (XMLElement* probability : child->m_vChild) {
                if (probability == nullptr) {
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Invalid Winning Prob",
                    //     L"GameEventSystem.cpp",
                    //     0x1487)
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] Invalid Winning Prob",
                    //     L"GameEventSystem.cpp",
                    //     0x1487)
                    L2_LOG_ERROR(
                        Log, "BalthusEvent/Probs: probability entry %llu is null; fatal configuration error.",
                        static_cast<unsigned long long>(index)
                    );
                    ForceCrash(CrashInfo::PARSING_ERROR_EVENTDATA_FILE);
                }
                m_arrWinningProbability[index] = ToInt32(Attribute(probability, u"Prob"));
                ++index;
            }
            hasProbabilities = true;
        } else {
            const std::string childName = Utf16ToUtf8(runtime::StringView(child->m_sName));
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] unknown element[%s] for CEvent_Balthus.",
            //     L"GameEventSystem.cpp",
            //     0x1498,
            //     childName.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] unknown element[%s] for CEvent_Balthus.",
            //     L"GameEventSystem.cpp",
            //     0x1498,
            //     childName.c_str())
            L2_LOG_ERROR(
                Log, "BalthusEvent: unexpected XML element '%s'; fatal configuration error.", childName.c_str()
            );
            ForceCrash(CrashInfo::PARSING_ERROR_EVENTDATA_FILE);
        }
    }

    const bool validRewards = ValidateRewards();
    if (!validRewards) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load CEvent_Balthus. invalid rewards",
        //     L"GameEventSystem.cpp",
        //     0x14a3)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load CEvent_Balthus. invalid rewards",
        //     L"GameEventSystem.cpp",
        //     0x14a3)
        L2_LOG_ERROR(
            Log,
            "BalthusEvent: reward counts do not match each set's duration or the total is not 24 hourly slots; fatal "
            "configuration error."
        );
        ForceCrash(CrashInfo::PARSING_ERROR_EVENTDATA_FILE);
    }
    if (!hasNpc || !hasConstraint || !hasProbabilities || !validRewards) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Missing Element. NPC[%d] Constraints[%d] Reward[%d] Prob[%d]",
        //     L"GameEventSystem.cpp",
        //     0x14ae,
        //     static_cast<int>(hasNpc),
        //     static_cast<int>(hasConstraint),
        //     static_cast<int>(validRewards),
        //     static_cast<int>(hasProbabilities))
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Missing Element. NPC[%d] Constraints[%d] Reward[%d] Prob[%d]",
        //     L"GameEventSystem.cpp",
        //     0x14ae,
        //     static_cast<int>(hasNpc),
        //     static_cast<int>(hasConstraint),
        //     static_cast<int>(validRewards),
        //     static_cast<int>(hasProbabilities))
        L2_LOG_ERROR(
            Log,
            "BalthusEvent is incomplete (NPC loaded: %s, Constraint loaded: %s, rewards valid: %s, Probs loaded: %s); fatal "
            "configuration error.",
            hasNpc ? "yes" : "no", hasConstraint ? "yes" : "no", validRewards ? "yes" : "no", hasProbabilities ? "yes" : "no"
        );
        ForceCrash(CrashInfo::PARSING_ERROR_EVENTDATA_FILE);
    }
    return true;
}

void CEvent_Balthus::Init() {
    // Current L2Server 0x00755848.
    L2_GUARD_FUNCTION();
    ShuffleRewards();
    UpdateRewardSetInfo(GetCurrentState());
}

void CEvent_Balthus::DoTickAction(const int tick) {
    // Current L2Server 0x0074f7e8.
    L2_GUARD_FUNCTION();
    const int now = runtime::crt::Time32Now();
    if (now < m_nNpcStartTime || now > m_nNpcEndTime || m_nNpcTick <= 0 || tick < m_nNpcTick || tick % m_nNpcTick != 0) {
        return;
    }

    for (const BalthusNpcInfo& npc : m_vNpcInfo) {
        for (const TimePeriodInfo& period : npc.m_vPeriod) {
            if (!g_npcserver.Connected()) {
                continue;
            }
            NpcPosEventMap::inst().TriggerNpcPosEvent(
                npc.m_sMakerEventName.c_str(), period.m_nStartTime <= now && now <= period.m_nEndTime ? 1 : 0
            );
        }
    }
}

void CEvent_Balthus::ShowSetting(User* user) {
    // Current L2Server 0x0075e898.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const DateTime startDate = TimeValueToDateTime(TimeValue(m_EventPeriod.m_nStartTime, 0));
    const DateTime endDate = TimeValueToDateTime(TimeValue(m_EventPeriod.m_nEndTime, 0));
    const int startHour = m_nStartTime / 3600;
    const int rewardSet = m_nCurrentRewardSet;
    const int rewardIndex = m_nCurrentRewardIndex;
    const int currentState = BalthusEvent::inst().GetCurrentState();
    const int currentProgress = BalthusEvent::inst().GetCurrentProgress();
    const int winningProbability = GetWinningProbability(currentProgress);
    const int rewardItem = BalthusEvent::inst().GetCurrentRewardItem();
    const int tokenCount = BalthusEvent::inst().GetTokenCount();
    const bool eventRunning = BalthusEvent::inst().IsEventRunning();
    const int minimumLevel = m_nMinimumLevel;
    const int minimumChangedClass = m_nMinimumChangedClass;

    const std::wstring endText = endDate.ToString();
    const std::wstring startText = startDate.ToString();
    const int drawMinute4 = BalthusEvent::inst().GetProgressInterval(4);
    const int drawMinute3 = BalthusEvent::inst().GetProgressInterval(3);
    const int drawMinute2 = BalthusEvent::inst().GetProgressInterval(2);
    const int drawMinute1 = BalthusEvent::inst().GetProgressInterval(1);
    const int drawMinute0 = BalthusEvent::inst().GetProgressInterval(0);

    constexpr char16_t kFormat[] = u"Balthus EventPeriod(%s ~ %s) \n StartTime : %d(H) \n "
                                   u"Set: %d\t ServerIndex: %d\t State: %d \tProgress: %d \n "
                                   u"DrawTime: %d %d %d %d %d \n Reward Item: %d \t\t "
                                   u"Token Count: %d \n Winning Prob : %d%% \t Event Running : %d "
                                   u"\n Min Level : %d \t Min Changed Class : %d";
    std::array<char16_t, 512> message;
    FormatUtf16To(
        message.data(), message.size(), kFormat,
        startText.c_str(), endText.c_str(), startHour, rewardSet + 1, rewardIndex, currentState, currentProgress + 1,
        drawMinute0, drawMinute1, drawMinute2, drawMinute3, drawMinute4, rewardItem, tokenCount, winningProbability,
        static_cast<int>(eventRunning), minimumLevel, minimumChangedClass
    );

    user->GetSocket()->SendSystemMessage(u"SYS", message.data());
}

CEvent_InvitationEvent::CEvent_InvitationEvent() : m_nTickInkEnergy(0), m_nMaxInkEnergy(0), m_nTick(0) {}

CEvent_InvitationEvent::~CEvent_InvitationEvent() = default;

GAME_EVENT::EVENT_TYPE CEvent_InvitationEvent::GetEventType() const {
    // Current L2Server 0x00423188.
    return GAME_EVENT::ET_INVITATION;
}

bool CEvent_InvitationEvent::Load(XMLElement* element) {
    // Current L2Server 0x0075d940.
    L2_GUARD_FUNCTION();
    if (element == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load CEvent_Invitation. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xfa7)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load CEvent_Invitation. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xfa7)
        L2_LOG_ERROR(Log, "Cannot load InvitationEvent: the XML node is missing. This event is not loaded.");
        return false;
    }

    LoadRootElement(element);
    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_EventPeriod.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_EventPeriod.m_nEndTime);
    m_nTick = std::max(ToInt32(Attribute(element, u"Tick")), 90);
    m_nTickInkEnergy = ToInt32(Attribute(element, u"GetTickInkEnergy"));
    m_nMaxInkEnergy = ToInt32(Attribute(element, u"GetMaxInkEnergy"));
    return true;
}

void CEvent_InvitationEvent::ShowSetting(User* user) {
    // Current L2Server 0x007549b8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const auto start = FormatEventTime(m_EventPeriod.m_nStartTime);
    const auto end = FormatEventTime(m_EventPeriod.m_nEndTime);
    float inkPercent = 0.0F;
    if (user->GetInkEnergy() != 0) {
        inkPercent = (static_cast<float>(user->GetInkEnergy()) / static_cast<float>(m_nMaxInkEnergy)) * 100.0F - 0.005F;
    }
    SendBuilderLine(
        user, u"[Invitation Event] \n (%ls ~ %ls) \n Tick %d sec per %d \n InkPoint( %.2f %% %d/%d(MAX)) ", start.data(),
        end.data(), static_cast<int>(static_cast<double>(m_nTick) / 1.5), m_nTickInkEnergy, static_cast<double>(inkPercent),
        user->GetInkEnergy(), m_nMaxInkEnergy
    );
}

bool CEvent_InvitationEvent::InMyTick(const int tick) {
    // Current L2Server 0x00423190.
    return m_nTick > 0 && m_nTick <= tick && tick % m_nTick == 0;
}

void CEvent_InvitationEvent::DoTickAction(const int tick) {
    // Current vtable slot is the shared folded no-op at 0x006fe5d4.
    static_cast<void>(tick);
}

bool CEvent_InvitationEvent::IsAllDone() {
    // Current shared leaf 0x004231e4.
    return m_EventPeriod.m_nEndTime < runtime::crt::Time32Now();
}

CEvent_Kalie::CEvent_Kalie() : m_nRewardMinLevel(40), m_nRewardMinChangedClass(2) {}

CEvent_Kalie::~CEvent_Kalie() = default;

GAME_EVENT::EVENT_TYPE CEvent_Kalie::GetEventType() const {
    // Current folded leaf 0x006301ec.
    return GAME_EVENT::ET_KALIE;
}

int CEvent_Kalie::GetKalieInfoSize() {
    // Current L2Server 0x00428a28 returns the physical record extent.
    return static_cast<int>(sizeof(KalieInfo));
}

bool CEvent_Kalie::LoadNPC(XMLElement* element) {
    // Current L2Server 0x00760ff4.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load DropEvent > NPC. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xe20)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load DropEvent > NPC. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xe20)
        L2_LOG_ERROR(
            Log,
            "Cannot load KalieEvent/NPC: the XML node is missing or empty. NPC settings are skipped; event loading "
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
        for (XMLElement* const periodElement : spawnElement->m_vChild) {
            if (periodElement == nullptr) {
                continue;
            }
            TimePeriodInfo period;
            TimePeriodInfo::ParseTime(Attribute(periodElement, u"Start").c_str(), period.m_nStartTime);
            TimePeriodInfo::ParseTime(Attribute(periodElement, u"End").c_str(), period.m_nEndTime);
            spawn.m_vSpawnTimes.push_back(period);
        }
        m_NPCInfo.m_vSpawnEvents.push_back(spawn);
    }
    return true;
}

bool CEvent_Kalie::Load(XMLElement* element) {
    // Current L2Server 0x00762864.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load CEvent_Kalie. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xdb7)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load CEvent_Kalie. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xdb7)
        L2_LOG_ERROR(Log, "Cannot load KalieEvent: the XML node is missing or empty. This event is not loaded.");
        return false;
    }

    LoadRootElement(element);
    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_EventPeriod.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_EventPeriod.m_nEndTime);

    for (XMLElement* const child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"GiveItems")) {
            for (XMLElement* const itemElement : child->m_vChild) {
                if (itemElement == nullptr) {
                    continue;
                }
                const std::int32_t state = ToInt32(Attribute(itemElement, u"State"));
                const std::u16string& itemName = Attribute(itemElement, u"ItemName");
                const std::int32_t itemClassId = static_cast<std::int32_t>(g_PrecompiledHeaderDB.Search(itemName.c_str()));
                const float constantA = static_cast<float>(ToDouble(Attribute(itemElement, u"ConstantA")));
                const std::int32_t constantB = ToInt32(Attribute(itemElement, u"ConstantB"));
                const std::int32_t token = ToInt32(Attribute(itemElement, u"Token"));
                if (itemClassId == -1) {
                    const std::string hostItemName = Utf16ToUtf8(runtime::StringView(itemName));
                    // Log.Add(
                    //     LOG_ERROR,
                    //     L"[%s][%d] Item name is invalid[%s]",
                    //     L"GameEventSystem.cpp",
                    //     0xde1,
                    //     hostItemName.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] Item name is invalid[%s]",
                    //     L"GameEventSystem.cpp",
                    //     0xde1,
                    //     hostItemName.c_str())
                    L2_LOG_ERROR(
                        Log, "KalieEvent/GiveItems: state %d references undefined item '%s'; this state entry is omitted.",
                        state, hostItemName.c_str()
                    );
                    continue;
                }
                m_mapKalieInfo.emplace(state, KalieInfo{state, itemClassId, constantA, constantB, token});
            }
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"Reward")) {
            for (XMLElement* const rewardElement : child->m_vChild) {
                if (rewardElement == nullptr) {
                    continue;
                }
                m_nRewardMinLevel = ToInt32(Attribute(rewardElement, u"MinLevel"));
                m_nRewardMinChangedClass = ToInt32(Attribute(rewardElement, u"MinChangedClass"));
            }
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"NPC")) {
            LoadNPC(child);
            continue;
        }
        const std::string childName = Utf16ToUtf8(runtime::StringView(child->m_sName));
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] unknown element[%s] for CEvent_Kalie.",
        //     L"GameEventSystem.cpp",
        //     0xe10,
        //     childName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] unknown element[%s] for CEvent_Kalie.",
        //     L"GameEventSystem.cpp",
        //     0xe10,
        //     childName.c_str())
        L2_LOG_WARN(
            Log, "KalieEvent: unexpected XML element '%s'; this element is ignored and loading continues.",
            childName.c_str()
        );
    }
    return true;
}

void CEvent_Kalie::ShowSetting(User* user) {
    // Current L2Server 0x007540a8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const auto start = FormatEventTime(m_EventPeriod.m_nStartTime);
    const auto end = FormatEventTime(m_EventPeriod.m_nEndTime);
    const std::int32_t state = KalieEvent::Inst().m_nCurrentState;
    const std::int32_t progress = KalieEvent::Inst().m_nProgress;
    const int rewardItem = RewardItem(state);
    const int tokenCount = TheNumberOfToken(state);
    const std::int32_t goalCount = KalieEvent::Inst().m_nGoalCnt;
    SendBuilderLine(
        user,
        u"Kalie EventPeriod(%s ~ %s) / State: %d / Progress: %d / Reward Item: %d / The Num of Token: %d / Goal Count: %d",
        start.data(), end.data(), state, progress, rewardItem, tokenCount, goalCount
    );
}

bool CEvent_Kalie::InEventTime() {
    // Current L2Server 0x0074dc70.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return m_EventPeriod.m_nStartTime <= now && now <= m_EventPeriod.m_nEndTime;
}

bool CEvent_Kalie::InMyTick(const int tick) {
    // Current vtable slot is the shared always-true leaf at 0x004c0008.
    static_cast<void>(tick);
    return true;
}

void CEvent_Kalie::DoTickAction(const int tick) {
    // Current L2Server 0x0074f3a8.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    if (now < m_NPCInfo.m_SpawnPeriod.m_nStartTime || m_NPCInfo.m_SpawnPeriod.m_nEndTime < now || m_NPCInfo.m_nTick <= 0 ||
        tick < m_NPCInfo.m_nTick || tick % m_NPCInfo.m_nTick != 0) {
        return;
    }

    const std::int32_t actionTime = runtime::crt::Time32Now();
    for (const SpawnTimes& spawn : m_NPCInfo.m_vSpawnEvents) {
        for (const TimePeriodInfo& period : spawn.m_vSpawnTimes) {
            if (!g_npcserver.Connected()) {
                continue;
            }
            NpcPosEventMap::inst().TriggerNpcPosEvent(
                spawn.m_sMakerEventName.c_str(), period.m_nStartTime <= actionTime && actionTime <= period.m_nEndTime ? 1 : 0
            );
        }
    }
}

bool CEvent_Kalie::IsAllDone() {
    // Current shared leaf 0x004231e4.
    return m_EventPeriod.m_nEndTime < runtime::crt::Time32Now();
}

CEvent_DynamicPvPZone::CEvent_DynamicPvPZone() :
    m_tpEventSeason{},
    m_bEventWeeks{},
    m_nEventStartTime(0),
    m_nEventEndTime(0),
    m_isAreaActive(false),
    m_isNPCServerActive(false) {}

CEvent_DynamicPvPZone::~CEvent_DynamicPvPZone() {
    // Current L2Server 0x00426cf4 clears the active lanes and
    // both deques before their ordinary member destruction.
    m_bEventWeeks.fill(false);
    m_nEventStartTime = 0;
    m_nEventEndTime = 0;
    m_mapNPCMaker.clear();
    m_mapArea.clear();
    m_isAreaActive = false;
    m_isNPCServerActive = false;
}

GAME_EVENT::EVENT_TYPE CEvent_DynamicPvPZone::GetEventType() const {
    // Current L2Server 0x00426cec.
    return GAME_EVENT::ET_DYNAMIC_PVPZONE;
}

bool CEvent_DynamicPvPZone::Load(XMLElement* element) {
    // Current L2Server 0x0075e4d0.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to parse 1, %s",
        //     L"GameEventSystem.cpp",
        //     0x1355,
        //     L"CEvent_DynamicPvPZone::Load")
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to parse 1, %s",
        //     L"GameEventSystem.cpp",
        //     0x1355,
        //     L"CEvent_DynamicPvPZone::Load")
        L2_LOG_ERROR(Log, "Cannot load DynamicPvPZoneEvent: the XML node is missing or empty. This event is not loaded.");
        return false;
    }

    LoadRootElement(element);
    const bool validStartSeason =
        TimePeriodInfo::ParseTime(Attribute(element, u"StartSeason").c_str(), m_tpEventSeason.m_nStartTime);
    const bool validEndSeason =
        TimePeriodInfo::ParseTime(Attribute(element, u"EndSeason").c_str(), m_tpEventSeason.m_nEndTime);
    const bool validWeeks =
        TimePeriodInfo::ParseWeek(Attribute(element, u"Weekdays").c_str(), m_bEventWeeks.data(), DAY_OF_THE_WEEK);
    const bool validStartTime = TimePeriodInfo::ParseHourMin(Attribute(element, u"StartTime").c_str(), m_nEventStartTime);
    const bool validEndTime = TimePeriodInfo::ParseHourMin(Attribute(element, u"EndTime").c_str(), m_nEventEndTime);

    m_tpEventSeason.m_nStartTime = runtime::SubtractInt32Wrapping(m_tpEventSeason.m_nStartTime, 10);
    m_tpEventSeason.m_nEndTime = runtime::AddInt32Wrapping(m_tpEventSeason.m_nEndTime, 10);
    if (!validStartSeason || !validEndSeason || !validWeeks || !validStartTime || !validEndTime ||
        m_nEventStartTime >= m_nEventEndTime) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to parse 2, %s",
        //     L"GameEventSystem.cpp",
        //     0x1369,
        //     L"CEvent_DynamicPvPZone::Load")
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to parse 2, %s",
        //     L"GameEventSystem.cpp",
        //     0x1369,
        //     L"CEvent_DynamicPvPZone::Load")
        L2_LOG_ERROR(
            Log,
            "Cannot load DynamicPvPZoneEvent: invalid schedule (StartSeason parsed: %s, EndSeason parsed: %s, Weekdays "
            "parsed: %s, StartTime parsed: %s, EndTime parsed: %s; start/end seconds %d..%d must be increasing).",
            validStartSeason ? "yes" : "no", validEndSeason ? "yes" : "no", validWeeks ? "yes" : "no",
            validStartTime ? "yes" : "no", validEndTime ? "yes" : "no", m_nEventStartTime, m_nEventEndTime
        );
        return false;
    }

    for (XMLElement* const child : element->m_vChild) {
        if (child == nullptr || child->m_sName.empty()) {
            continue;
        }
        const std::u16string& name = Attribute(child, u"Name");
        if (name.empty()) {
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"NPCMaker")) {
            m_mapNPCMaker.push_back(name);
        } else if (EqualsIgnoreCase(child->m_sName, u"Area")) {
            m_mapArea.push_back(name);
        }
    }
    return !m_mapNPCMaker.empty() && !m_mapArea.empty();
}

void CEvent_DynamicPvPZone::ShowSetting(User* user) {
    // Current vtable slot is the shared folded no-op.
    static_cast<void>(user);
}

bool CEvent_DynamicPvPZone::InEventTime() {
    // Current L2Server 0x0074e4d4.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    if (now < m_tpEventSeason.m_nStartTime || m_tpEventSeason.m_nEndTime < now) {
        return false;
    }
    std::tm local{};
    if (!runtime::crt::LocalTime32(now, local)) {
        return false;
    }
    return m_bEventWeeks[static_cast<std::size_t>(local.tm_wday)];
}

bool CEvent_DynamicPvPZone::InMyTick(const int tick) {
    // Current L2Server 0x0074e454 keeps its guard around this true leaf.
    L2_GUARD_FUNCTION();
    static_cast<void>(tick);
    return true;
}

bool CEvent_DynamicPvPZone::IsInTime() {
    // Current L2Server 0x0074e5b8.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    std::tm local{};
    if (!runtime::crt::LocalTime32(now, local)) {
        return false;
    }
    const std::int32_t seconds = (local.tm_hour * 60 + local.tm_min) * 60 + local.tm_sec;
    return m_nEventStartTime <= seconds && seconds < m_nEventEndTime;
}

void CEvent_DynamicPvPZone::DoTickAction(const int tick) {
    // Current L2Server 0x00751814.
    L2_GUARD_FUNCTION();
    static_cast<void>(tick);

    const bool previousAreaActive = m_isAreaActive;
    m_isAreaActive = IsInTime();
    const bool previousNpcServerActive = m_isNPCServerActive;
    m_isNPCServerActive = g_PledgeTimer.IsAllNpcLoaded();

    if (previousAreaActive != m_isAreaActive) {
        for (const std::u16string& areaName : m_mapArea) {
            CArea* const area = CAreaDB::inst().FindArea(areaName.c_str());
            if (area == nullptr || area->m_InstantZoneKey.typeId != 0) {
                continue;
            }
            area->SetOnOff(m_isAreaActive);
            const std::string hostAreaName = Utf16ToUtf8(runtime::StringView(areaName));
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR] area[%s] activated: %d",
            //     hostAreaName.c_str(),
            //     m_isAreaActive)
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR] area[%s] activated: %d",
            //     hostAreaName.c_str(),
            //     m_isAreaActive)
            L2_LOG_INFO(
                Log, "Dynamic PvP area '%s' is now %s.", hostAreaName.c_str(),
                m_isAreaActive ? "enabled" : "disabled"
            );
        }
    }

    if (previousAreaActive == m_isAreaActive && previousNpcServerActive == m_isNPCServerActive) {
        return;
    }
    if (!m_isNPCServerActive) {
        return;
    }
    for (const std::u16string& makerName : m_mapNPCMaker) {
        if (m_isAreaActive) {
            EnableNpcPosEvent(makerName.c_str());
        } else {
            DisableNpcPosEvent(makerName.c_str());
        }
        // Noise: This display-only conversion served the duplicate NPC activation message.
        // const std::wstring hostMakerName =
        //     Utf16ToWide(makerName.view())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] npc[%s] activated: %d",
        //     hostMakerName.c_str(),
        //     m_isAreaActive)
        // Noise: Enable/DisableNpcPosEvent already reports a dispatched maker request. This duplicate does not prove that
        // NPCs have spawned. L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] npc[%s] activated: %d",
        //     hostMakerName.c_str(),
        //     m_isAreaActive)
    }
}

bool CEvent_DynamicPvPZone::IsAllDone() {
    // Current folded leaf 0x0046c3fc.
    return false;
}

bool CEvent_DynamicPvPZone::ShouldBeRegistered() const {
    // Current folded leaf 0x004c0008.
    return true;
}

CEvent_ToDoListName::CEvent_ToDoListName() = default;

CEvent_ToDoListName::~CEvent_ToDoListName() = default;

GAME_EVENT::EVENT_TYPE CEvent_ToDoListName::GetEventType() const {
    // Current L2Server 0x004231dc.
    return GAME_EVENT::ET_TODOLIST_NAME;
}

bool CEvent_ToDoListName::Load(XMLElement* element) {
    // Current L2Server 0x0075ec44.
    L2_GUARD_FUNCTION();
    if (element == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load ToDoListNameEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x1625)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load ToDoListNameEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x1625)
        L2_LOG_ERROR(Log, "Cannot load ToDoListNameEvent: the XML node is missing. This event is not loaded.");
        return false;
    }

    LoadRootElement(element);
    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_EventPeriod.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_EventPeriod.m_nEndTime);
    m_nTick = ToInt32(Attribute(element, u"Tick"));
    return true;
}

void CEvent_ToDoListName::ShowSetting(User* user) {
    // Current L2Server 0x00755a94.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    const auto start = FormatEventTime(m_EventPeriod.m_nStartTime);
    const auto end = FormatEventTime(m_EventPeriod.m_nEndTime);
    SendBuilderLine(user, u"ToDoListNameEvent EventPeriod(%s ~ %s) ", start.data(), end.data());
}

bool CEvent_ToDoListName::InEventTime() {
    // Current L2Server 0x0074e910.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return m_EventPeriod.m_nStartTime <= now && now <= m_EventPeriod.m_nEndTime;
}

bool CEvent_ToDoListName::InMyTick(const int tick) {
    // Current L2Server 0x00423078.
    return m_nTick > 0 && m_nTick <= tick && tick % m_nTick == 0;
}

void CEvent_ToDoListName::DoTickAction(const int tick) {
    // Current vtable slot is the shared folded no-op at 0x006fe5d4.
    static_cast<void>(tick);
}

bool CEvent_ToDoListName::IsAllDone() {
    // Current shared leaf 0x004231e4.
    return m_EventPeriod.m_nEndTime < runtime::crt::Time32Now();
}

CEvent_LuckyGameEvent::CEvent_LuckyGameEvent() : m_nCommonIndex(0), m_nPremiumIndex(0) {}

CEvent_LuckyGameEvent::~CEvent_LuckyGameEvent() = default;

GAME_EVENT::EVENT_TYPE CEvent_LuckyGameEvent::GetEventType() const { return GAME_EVENT::ET_LUCKY_GAME; }

bool CEvent_LuckyGameEvent::InEventTime() {
    // Current L2Server 0x0074e288.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return m_EventPeriod.m_nStartTime <= now && now <= m_EventPeriod.m_nEndTime;
}

bool CEvent_LuckyGameEvent::InMyTick(int tick) {
    // Current vtable slot is folded with the shared always-true leaf.
    static_cast<void>(tick);
    return true;
}

bool CEvent_LuckyGameEvent::IsAllDone() {
    // Current vtable slot is folded with the shared always-false leaf.
    return false;
}

bool CEvent_LuckyGameEvent::ShouldBeRegistered() const {
    // Current vtable slot is folded with the shared always-true leaf.
    return true;
}

double CEvent_LuckyGameEvent::GetUserRatio() { return 1.0; }

void CEvent_LuckyGameEvent::DoTickAction(const int tick) {
    // Current L2Server 0x0074f5c8.
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    if (now < m_NPCInfo.m_SpawnPeriod.m_nStartTime || m_NPCInfo.m_SpawnPeriod.m_nEndTime < now || m_NPCInfo.m_nTick <= 0 ||
        tick < m_NPCInfo.m_nTick || tick % m_NPCInfo.m_nTick != 0) {
        return;
    }

    const std::int32_t actionTime = runtime::crt::Time32Now();
    for (const SpawnTimes& spawn : m_NPCInfo.m_vSpawnEvents) {
        for (const TimePeriodInfo& period : spawn.m_vSpawnTimes) {
            if (!g_npcserver.Connected()) {
                continue;
            }
            NpcPosEventMap::inst().TriggerNpcPosEvent(
                spawn.m_sMakerEventName.c_str(), period.m_nStartTime <= actionTime && actionTime <= period.m_nEndTime ? 1 : 0
            );
        }
    }
}

bool CEvent_LuckyGameEvent::LoadNPC(XMLElement* element) {
    // Current L2Server 0x007612cc.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load DropEvent > NPC. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x11ce)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load DropEvent > NPC. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x11ce)
        L2_LOG_ERROR(
            Log,
            "Cannot load LuckyGameEvent/NPC: the XML node is missing or empty. NPC settings are skipped; event loading "
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
        for (XMLElement* const periodElement : spawnElement->m_vChild) {
            if (periodElement == nullptr) {
                continue;
            }
            TimePeriodInfo period;
            TimePeriodInfo::ParseTime(Attribute(periodElement, u"Start").c_str(), period.m_nStartTime);
            TimePeriodInfo::ParseTime(Attribute(periodElement, u"End").c_str(), period.m_nEndTime);
            spawn.m_vSpawnTimes.push_back(period);
        }
        m_NPCInfo.m_vSpawnEvents.push_back(spawn);
    }
    return true;
}

void CEvent_LuckyGameEvent::SetCommonIndex(const int index) {
    // Current L2Server 0x007599fc updates only the manager.
    L2_GUARD_FUNCTION();
    CLuckyGameSystem::Inst()->SetCommonGameRewardIndex(index);
}

void CEvent_LuckyGameEvent::SetPremiumIndex(const int index) {
    // Current L2Server 0x00759ac8 updates only the manager.
    L2_GUARD_FUNCTION();
    CLuckyGameSystem::Inst()->SetPremiumGameRewardIndex(index);
}

bool CEvent_LuckyGameEvent::Load(XMLElement* element) {
    // Current L2Server 0x00762d00.
    L2_GUARD_FUNCTION();
    if (element == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load CEvent_LuckyGameEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x119a)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load CEvent_LuckyGameEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x119a)
        L2_LOG_ERROR(Log, "Cannot load LuckyGameEvent: the XML node is missing. This event is not loaded.");
        return false;
    }

    LoadRootElement(element);
    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_EventPeriod.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_EventPeriod.m_nEndTime);
    m_nCommonIndex = ToInt32(Attribute(element, u"CommonIndex"));
    m_nPremiumIndex = ToInt32(Attribute(element, u"PremiumIndex"));

    SetCommonIndex(m_nCommonIndex);
    SetPremiumIndex(m_nPremiumIndex);
    db.RequestGetEventScore(0x14, -1, 0, u"");
    db.RequestGetEventScore(0x14, -2, 0, u"");
    db.RequestGetEventScore(0x14, -3, 0, u"");
    db.RequestGetEventScore(0x14, -4, 0, u"");

    for (XMLElement* const child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"NPC")) {
            LoadNPC(child);
            continue;
        }
        const std::string name = Utf16ToUtf8(runtime::StringView(child->m_sName));
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] unknown element[%s] for LuckyGameEvent.",
        //     L"GameEventSystem.cpp",
        //     0x11bf,
        //     name.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] unknown element[%s] for LuckyGameEvent.",
        //     L"GameEventSystem.cpp",
        //     0x11bf,
        //     name.c_str())
        L2_LOG_WARN(
            Log, "LuckyGameEvent: unexpected XML element '%s'; this element is ignored and loading continues.",
            name.c_str()
        );
    }
    return true;
}

void CEvent_LuckyGameEvent::ShowSetting(User* user) {
    // Current L2Server 0x007595a0.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    CLuckyGameSystem* const system = CLuckyGameSystem::Inst();
    const int commonIndex = system->GetCommonGameRewardIndex();
    const int premiumIndex = system->GetPremiumGameRewardIndex();
    const auto start = FormatEventTime(m_EventPeriod.m_nStartTime);
    const auto end = FormatEventTime(m_EventPeriod.m_nEndTime);
    SendBuilderLine(
        user,
        u"LuckyGameEvent EventPeriod(%s ~ %s) /\n CommonIndex[%d] / \t\t\t\t\t\t\n PremiumIndex[%d]/ \t\t\t\t\t\t\n "
        u"WorldTurn Common[%d] Premium [%d]/ \t\t\t\t\t\t\n Modify Point Common[%d] Premium[%d]/ \t\t\t\t\t\t\n RevCount "
        u"Common[%d] Premium[%d]",
        start.data(), end.data(), commonIndex, premiumIndex, system->GetCurrentTurnCnt(LuckyGameSystem::BT_COMMON),
        system->GetCurrentTurnCnt(LuckyGameSystem::BT_PREMIUM), system->GetPersonalPoint(user, commonIndex),
        system->GetPersonalPoint(user, premiumIndex), system->GetRevCount(LuckyGameSystem::BT_COMMON),
        system->GetRevCount(LuckyGameSystem::BT_PREMIUM)
    );
}

CCommonDropInfo::CCommonDropInfo() : m_nNPCRace(-2), m_nPCStatus(0) {}

CCommonDropInfo::~CCommonDropInfo() = default;

void CCommonDropInfo::InitCommonDropItemInfo(CommonDropItemInfoOnTick& info) {
    // Current L2Server 0x00757990.
    L2_GUARD_FUNCTION();
    m_baseDropInfo.InitDropItemInfo(info.m_baseDropInfoOnTick);
    info.m_nNPCRace = m_nNPCRace;
    info.m_nPCStatus = m_nPCStatus;
}

CEvent_CommonDropSystem::CEvent_CommonDropSystem() {}

CEvent_CommonDropSystem::~CEvent_CommonDropSystem() = default;

GAME_EVENT::EVENT_TYPE CEvent_CommonDropSystem::GetEventType() const { return GAME_EVENT::ET_COMMONDROP_SYSTEM; }

bool CEvent_CommonDropSystem::InEventTime() { return m_DropInfo.m_baseDropInfo.InPeriod(); }

bool CEvent_CommonDropSystem::InMyTick(const int tick) {
    const int eventTick = m_DropInfo.m_baseDropInfo.m_nTick;
    return eventTick > 0 && eventTick <= tick && tick % eventTick == 0;
}

bool CEvent_CommonDropSystem::InMyDropTick(const int tick) {
    const int eventTick = m_DropInfo.m_baseDropInfo.m_nTick;
    return eventTick > 0 && eventTick <= tick && tick % eventTick == 0;
}

void CEvent_CommonDropSystem::DoTickAction(const int tick) {
    // Current L2Server 0x0074f2dc.
    L2_GUARD_FUNCTION();
    if (InMyDropTick(tick)) {
        SetEventDropTime(m_DropInfo.m_baseDropInfo.InPeriod());
    }
}

bool CEvent_CommonDropSystem::IsAllDone() {
    // Current L2Server 0x0074d4a8.
    L2_GUARD_FUNCTION();
    return m_DropInfo.m_baseDropInfo.m_TotalPeriod.m_nEndTime < runtime::crt::Time32Now();
}

int CEvent_CommonDropSystem::GetMaxDropItemAmount() { return m_DropInfo.m_baseDropInfo.GetMaxDropItemAmount(); }

double CEvent_CommonDropSystem::GetUserRatio() { return m_DropInfo.m_baseDropInfo.m_dUserRatio; }

std::size_t CEvent_CommonDropSystem::GetEventPeriod(std::vector<TimePeriodInfo>& periods) {
    // Current L2Server 0x00757bd0.
    L2_GUARD_FUNCTION();
    for (const TimePeriodInfo& period : m_DropInfo.m_baseDropInfo.m_vDropPeriods) {
        periods.push_back(period);
    }
    return periods.size();
}

void CEvent_CommonDropSystem::InitCommonDropItemInfo(CommonDropItemInfoOnTick& info) {
    m_DropInfo.InitCommonDropItemInfo(info);
}

void CEvent_CommonDropSystem::SetEventDropTime(const bool enabled) {
    // Current L2Server 0x0074d5e0 is a guarded no-op.
    L2_GUARD_FUNCTION();
    static_cast<void>(enabled);
}

void CEvent_CommonDropSystem::SetDropProb(const double probability) {
    // Current L2Server 0x0074d540.
    L2_GUARD_FUNCTION();
    m_DropInfo.m_baseDropInfo.m_dDropProb = probability;
}

void CEvent_CommonDropSystem::SetDropProb(std::u16string probability) {
    // Current L2Server 0x0074fd30.
    L2_GUARD_FUNCTION();
    SetDropProb(ToDouble(probability));
}

void CEvent_CommonDropSystem::SetDropSequence(std::u16string sequence) {
    // Current L2Server 0x0074fbd0.
    L2_GUARD_FUNCTION();
    if (EqualsIgnoreCase(sequence, u"random")) {
        m_DropInfo.m_baseDropInfo.m_eDropSequence = GAME_EVENT::DT_RANDOM;
        return;
    }
    if (EqualsIgnoreCase(sequence, u"fix")) {
        m_DropInfo.m_baseDropInfo.m_eDropSequence = GAME_EVENT::DT_FIX;
        return;
    }
    const std::string hostSequence = Utf16ToUtf8(sequence);
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] CommonDropSequence is invalid[%s]",
    //     L"GameEventSystem.cpp",
    //     0xc2e,
    //     hostSequence.c_str())
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] CommonDropSequence is invalid[%s]",
    //     L"GameEventSystem.cpp",
    //     0xc2e,
    //     hostSequence.c_str())
    L2_LOG_WARN(
        Log, "CommonDrop: unknown DropSequence '%s'; expected 'random' or 'fix'. The previous sequence is retained.",
        hostSequence.c_str()
    );
}

int CEvent_CommonDropSystem::FindPCStatus(const wchar_t* status) const {
    // Current L2Server 0x0074d65c.
    L2_GUARD_FUNCTION();
    if (status != nullptr) {
        if (CompareHostWideIgnoreCase(status, L"normal") == 0) {
            return 0;
        }
        if (CompareHostWideIgnoreCase(status, L"chaotic") == 0) {
            return 1;
        }
    }
    return -2;
}

int CEvent_CommonDropSystem::FindRaceType(const wchar_t* race) const {
    // Current L2Server 0x0074d72c.
    L2_GUARD_FUNCTION();
    if (race == nullptr) {
        return -1;
    }
    struct RaceName {
        const wchar_t* name;
        int type;
    };
    static constexpr std::array<RaceName, 30> kRaces{{
        {L"all", -2},
        {L"human", 0},
        {L"elf", 1},
        {L"darkelf", 2},
        {L"orc", 3},
        {L"dwarf", 4},
        {L"kamael", 5},
        {L"ertheia", 6},
        {L"humanoid", 7},
        {L"animal", 8},
        {L"monster", 9},
        {L"creature", 10},
        {L"undead", 11},
        {L"elemental", 12},
        {L"devil", 13},
        {L"dragon", 14},
        {L"angel", 15},
        {L"construct", 16},
        {L"beast", 17},
        {L"plant", 18},
        {L"divine", 19},
        {L"demonic", 20},
        {L"giant", 21},
        {L"bug", 22},
        {L"fairy", 23},
        {L"siege_weapon", 24},
        {L"mercenary", 25},
        {L"castle_guard", 26},
        {L"etc", 28},
        {L"friend", 29},
    }};
    for (const RaceName& candidate : kRaces) {
        if (CompareHostWideIgnoreCase(race, candidate.name) == 0) {
            return candidate.type;
        }
    }
    return -1;
}

const wchar_t* CEvent_CommonDropSystem::FindNPCRaceString(const int race) const {
    // Current L2Server 0x0074dad0.
    switch (race) {
        case 0:
            return L"human";
        case 1:
            return L"elf";
        case 2:
            return L"darkelf";
        case 3:
            return L"orc";
        case 4:
            return L"dwarf";
        case 5:
            return L"kamael";
        case 6:
            return L"ertheia";
        case 7:
            return L"humanoid";
        case 8:
            return L"animal";
        case 9:
            return L"monster";
        case 10:
            return L"creature";
        case 11:
            return L"undead";
        case 12:
            return L"elemental";
        case 13:
            return L"devil";
        case 14:
            return L"dragon";
        case 15:
            return L"angel";
        case 16:
            return L"construct";
        case 17:
            return L"beast";
        case 18:
            return L"plant";
        case 19:
            return L"divine";
        case 20:
            return L"demonic";
        case 21:
            return L"giant";
        case 22:
            return L"bug";
        case 23:
            return L"fairy";
        case 24:
            return L"siegeweapon";
        case 25:
            return L"mercenary";
        case 26:
            return L"castleguard";
        case 27:
            return L"monrace";
        case 28:
            return L"etcetera";
        case 29:
            return L"friend";
        case -1:
            return L"none";
        default:
            return L"all";
    }
}

bool CEvent_CommonDropSystem::LoadPC(XMLElement* element) {
    // Current L2Server 0x0075cbf4.
    L2_GUARD_FUNCTION();
    if (element == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load PC Condtion. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xbb3)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load PC Condtion. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xbb3)
        L2_LOG_ERROR(
            Log,
            "Cannot load CommonDrop/PC: the XML node is missing. PC conditions are retained and event loading continues."
        );
        return false;
    }
    const std::wstring status = Utf16ToWide(Attribute(element, u"Status"));
    m_DropInfo.m_nPCStatus = FindPCStatus(status.c_str());
    return true;
}

bool CEvent_CommonDropSystem::LoadDrop(XMLElement* element) {
    // Current L2Server 0x0075cd14.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty() || element->m_vChild.size() > 3) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load DropEvent > Drop. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xbc5)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load DropEvent > Drop. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xbc5)
        L2_LOG_ERROR(
            Log,
            "Cannot load CommonDrop/Drop: the XML node is missing or has %llu children; expected 1..3. This section stops "
            "loading; event loading continues.",
            element != nullptr ? static_cast<unsigned long long>(element->m_vChild.size()) : 0ULL
        );
        return false;
    }

    CDropInfo& drop = m_DropInfo.m_baseDropInfo;
    drop.m_nTick = ToInt32(Attribute(element, u"Tick"));
    drop.m_nMinimumPlayingUserCount = std::max(1, ToInt32(Attribute(element, u"MinPlayingUserCount")));
    drop.m_nBasePlayingUserCount = std::max(1, ToInt32(Attribute(element, u"BasePlayingUserCount")));

    for (XMLElement* const child : element->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        if (EqualsIgnoreCase(child->m_sName, u"DropTimes")) {
            for (XMLElement* const timeElement : child->m_vChild) {
                if (timeElement == nullptr) {
                    continue;
                }
                TimePeriodInfo period;
                TimePeriodInfo::ParseTime(Attribute(timeElement, u"Start").c_str(), period.m_nStartTime);
                TimePeriodInfo::ParseTime(Attribute(timeElement, u"End").c_str(), period.m_nEndTime);
                drop.m_TotalPeriod.m_nStartTime = std::min(drop.m_TotalPeriod.m_nStartTime, period.m_nStartTime);
                drop.m_TotalPeriod.m_nEndTime = std::max(drop.m_TotalPeriod.m_nEndTime, period.m_nEndTime);
                drop.m_vDropPeriods.push_back(period);
            }
            continue;
        }

        if (EqualsIgnoreCase(child->m_sName, u"Items")) {
            SetDropSequence(Attribute(child, u"DropSequence"));
            SetDropProb(Attribute(child, u"DropProb"));
            drop.m_nNPCMinLevel = ToInt32(Attribute(child, u"NPCMinLev"));
            drop.m_nNPCMaxLevel = ToInt32(Attribute(child, u"NPCMaxLev"));
            const std::wstring race = Utf16ToWide(Attribute(child, u"NPCRace"));
            m_DropInfo.m_nNPCRace = FindRaceType(race.c_str());

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
                    //     L"GameEventSystem.cpp",
                    //     0xbfc,
                    //     name.c_str())
                    // L2_LOG_ERROR(Log,
                    //     L"[%s][%d] Item name is invalid[%s]",
                    //     L"GameEventSystem.cpp",
                    //     0xbfc,
                    //     name.c_str())
                    L2_LOG_ERROR(
                        Log, "CommonDrop/Drop/Items: item '%s' is not defined; this entry is omitted.",
                        name.c_str()
                    );
                    continue;
                }
                DropItemInfo info{};
                info.m_nAmountPerServer = 0;
                info.m_nItemClassID = classId;
                info.m_nAmount = ToInt32(Attribute(itemElement, u"Amount"));
                info.m_nProb = ToDouble(Attribute(itemElement, u"Prob"));
                drop.Add(info);
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
                drop.Add(variable);
            }
            continue;
        }

        const std::string name = Utf16ToUtf8(child->m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] unknown element[%s] for CommonDropEvent > CommonDrop.",
        //     L"GameEventSystem.cpp",
        //     0xc17,
        //     name.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] unknown element[%s] for CommonDropEvent > CommonDrop.",
        //     L"GameEventSystem.cpp",
        //     0xc17,
        //     name.c_str())
        L2_LOG_WARN(
            Log, "CommonDrop/Drop: unexpected XML element '%s'; this element is ignored and loading continues.",
            name.c_str()
        );
    }
    return true;
}

bool CEvent_CommonDropSystem::Load(XMLElement* element) {
    // Current L2Server 0x0075f5b8.
    L2_GUARD_FUNCTION();
    if (element == nullptr || element->m_vChild.empty() || element->m_vChild.size() > 2) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load CommonDropSystem. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xb5d)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load CommonDropSystem. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0xb5d)
        L2_LOG_ERROR(
            Log,
            "Cannot load CommonDrop: the XML node is missing or has %llu children; expected 1..2. This event is not loaded.",
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
        if (EqualsIgnoreCase(child->m_sName, u"PC")) {
            LoadPC(child);
            continue;
        }
        const std::string name = Utf16ToUtf8(child->m_sName);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] unknown element[%s] for CommonDropEvent.",
        //     L"GameEventSystem.cpp",
        //     0xb75,
        //     name.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] unknown element[%s] for CommonDropEvent.",
        //     L"GameEventSystem.cpp",
        //     0xb75,
        //     name.c_str())
        L2_LOG_WARN(
            Log, "CommonDrop: unexpected XML element '%s'; this element is ignored and loading continues.",
            name.c_str()
        );
    }
    return true;
}

void CEvent_CommonDropSystem::ShowSetting(User* user) {
    // Current L2Server 0x007539e8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    CDropInfo& drop = m_DropInfo.m_baseDropInfo;
    const std::wstring eventName = Utf16ToWide(GetName());
    SendBuilderLine(
        user, u"EVENT_NAME:%s EVENT_DOING:%d FLACSetting::inst()_TIME:%d", eventName.c_str(), InEventTime(), drop.m_nTick
    );
    SendBuilderLine(
        user, u"NPCMINLEV:%d NPCMAXLEV:%d MINPLAYINGUSERCOUNT:%d BASEPLAYINGUSERCOUNT:%d", drop.m_nNPCMinLevel,
        drop.m_nNPCMaxLevel, drop.m_nMinimumPlayingUserCount, drop.m_nBasePlayingUserCount
    );

    const wchar_t* pcStatus = L"all";
    if (m_DropInfo.m_nPCStatus == 0) {
        pcStatus = L"normal";
    } else if (m_DropInfo.m_nPCStatus == 1) {
        pcStatus = L"chaotic";
    }
    SendBuilderLine(user, u"PCSTATUS:%s NPCRACE:%s", pcStatus, FindNPCRaceString(m_DropInfo.m_nNPCRace));

    SendBuilderLine(
        user, u"DROP_ITEM_COUNT:%d DROP_PROB:%.1f", static_cast<int>(drop.m_vDropItemList.size()), drop.m_dDropProb
    );
    int index = 0;
    for (const DropItemInfo& item : drop.m_vDropItemList) {
        SendBuilderLine(
            user, u"DROP_ITEM%d: itemname=%s, itemid=%d, amount=%d, prob=%.1f", index++,
            Utf16ToWide(g_ObjectDB.GetName(item.m_nItemClassID)).c_str(), item.m_nItemClassID, item.m_nAmount, item.m_nProb
        );
    }

    SendBuilderLine(user, u"DROP_TIME_COUNT:%d", static_cast<int>(drop.m_vDropPeriods.size()));
    index = 0;
    for (const TimePeriodInfo& period : drop.m_vDropPeriods) {
        const auto start = FormatEventTime(period.m_nStartTime);
        const auto end = FormatEventTime(period.m_nEndTime);
        SendBuilderLine(user, u"DROP_TIME%d = %s ~ %s", index++, start.data(), end.data());
    }

    SendBuilderLine(user, u"TIME_VARIABLE_COUNT:%d", static_cast<int>(drop.m_vTimeVariables.size()));
    index = 0;
    for (const TimeVariableInfo& variable : drop.m_vTimeVariables) {
        SendBuilderLine(
            user, u"TIME_VARIABLE_COUNT%d: %d ~ %d : %d", index++, variable.m_nStartHour, variable.m_nEndHour,
            variable.m_nVariable
        );
    }
}

bool CEvent_YearCatch::Load(XMLElement* element) {
    L2_GUARD_FUNCTION();
    if (element == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load YearCatchEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x5b2)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load YearCatchEvent. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x5b2)
        L2_LOG_ERROR(Log, "Cannot load YearCatchEvent: the XML node is missing. This event is not loaded.");
        return false;
    }

    LoadRootElement(element);
    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_EventPeriod.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_EventPeriod.m_nEndTime);
    m_nTick = ToInt32(Attribute(element, u"Tick"));
    return true;
}

bool CEvent_YearCatch::InEventTime() {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    return m_EventPeriod.m_nStartTime <= now && now <= m_EventPeriod.m_nEndTime;
}

void CEvent_YearCatch::DoTickAction(int) {
    L2_GUARD_FUNCTION();
    if (runtime::raw_atomic::LoadI32Relaxed(&m_nOnStart) == 1 &&
        runtime::raw_atomic::LoadI32Relaxed(&m_nOnSpawn) != 1) {
        runtime::raw_atomic::ExchangeI32(&m_nOnSpawn, 1);
    }
}

bool CEvent_VoteSystem::Load(XMLElement* element) {
    // Current L2Server 0x0075c0c8.
    L2_GUARD_FUNCTION();
    if (element == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] fail to load VoteSystem. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x5e2)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] fail to load VoteSystem. invalid child nodes",
        //     L"GameEventSystem.cpp",
        //     0x5e2)
        L2_LOG_ERROR(Log, "Cannot load VoteSystem: the XML node is missing. This event is not loaded.");
        return false;
    }

    LoadRootElement(element);
    TimePeriodInfo::ParseTime(Attribute(element, u"Start").c_str(), m_EventPeriod.m_nStartTime);
    TimePeriodInfo::ParseTime(Attribute(element, u"End").c_str(), m_EventPeriod.m_nEndTime);
    m_nTick = ToInt32(Attribute(element, u"Tick"));
    m_nVoteCount = ToInt32(Attribute(element, u"GetVoteCount"));
    m_nBonusTimeLimit = ToInt32(Attribute(element, u"BonusTimeLimit"));
    m_nBonusRate = ToInt32(Attribute(element, u"BonusRate"));
    m_nPlayTime = ToInt32(Attribute(element, u"PlayTime"));
    m_nValue = ToInt32(Attribute(element, u"Value"));
    return true;
}

void CEvent_VoteSystem::ShowSetting(User* user) {
    // Current L2Server 0x007522e8.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }

    const auto start = FormatEventTime(m_EventPeriod.m_nStartTime);
    const auto end = FormatEventTime(m_EventPeriod.m_nEndTime);
    SendBuilderLine(
        user,
        u"VoteSystem EventPeriod(%s ~ %s) / VoteCount: %d / "
        u"BonusTimeLimit: %d / BonusRate: %d / PlayTime: %d / "
        u"Value: %d",
        start.data(), end.data(), m_nVoteCount, m_nBonusTimeLimit, m_nBonusRate, m_nPlayTime, m_nValue
    );
}

namespace {

using RegisteredEventFactory = GenericFactory<CEvent, std::u16string, CEvent* (*)()>;

#define L2_REGISTER_EVENT(ClassName, Key)                                                                                   \
    L2_DEFINE_AND_REGISTER_FACTORY_TYPE(                                                                                    \
        CEvent, ClassName, Create_##ClassName, isReg_##ClassName, RegisteredEventFactory, std::u16string(Key)               \
    )

L2_REGISTER_EVENT(CEvent_YearCatch, u"YearCatchEvent");
L2_REGISTER_EVENT(CEvent_VoteSystem, u"VoteSystem");
L2_REGISTER_EVENT(CEvent_CoretimeTeleport, u"CoretimeTeleport");
L2_REGISTER_EVENT(CEvent_CommonDropSystem, u"CommonDrop");
L2_REGISTER_EVENT(CEvent_PCCafe, u"PCCafeSetting");
L2_REGISTER_EVENT(CEvent_World, u"WorldEvent");
L2_REGISTER_EVENT(CEvent_Buff, u"BuffEvent");

// The current initializer order places this creator before XmasSanta while
// registering its key after OneTimeEvent. Preserve that split explicitly.
L2_DEFINE_FACTORY_CREATOR(CEvent, Create_CEvent_PCCafeCoupon, CEvent_PCCafeCoupon)

L2_REGISTER_EVENT(CEvent_XmasSanta, u"XmasSantaEvent");
L2_REGISTER_EVENT(CEvent_Kalie, u"KalieEvent");
L2_REGISTER_EVENT(CEvent_VariationEvent, u"VariationEvent");
L2_REGISTER_EVENT(CEvent_OneTimeEvent, u"OneTimeEvent");
L2_REGISTER_FACTORY_CREATOR(
    isReg_CEvent_PCCafeCoupon, RegisteredEventFactory, std::u16string(u"PCCafeCouponEvent"), Create_CEvent_PCCafeCoupon
);
L2_REGISTER_EVENT(CEvent_InvitationEvent, u"InvitationEvent");
L2_REGISTER_EVENT(CEvent_RecipeSuccessRate, u"RecipeSuccessRate");
L2_REGISTER_EVENT(CEvent_EconomyControlEvent, u"EconomyControlEvent");
L2_REGISTER_EVENT(CEvent_AccountEvent, u"AccountEvent");
L2_REGISTER_EVENT(CEvent_DormantBuffEvent, u"DormantBuff");
L2_REGISTER_EVENT(CEvent_LuckyGameEvent, u"LuckyGameEvent");
L2_REGISTER_EVENT(CEvent_Balthus, u"BalthusEvent");
L2_REGISTER_EVENT(CEvent_DynamicPvPZone, u"DynamicPvPZoneEvent");
L2_REGISTER_EVENT(CEvent_ToDoListName, u"ToDoListNameEvent");

#undef L2_REGISTER_EVENT

} // namespace

CEventManager::CEventManager() = default;

CEventManager::~CEventManager() = default;

L2_DEFINE_OBJECT_CLASS_ID(CEventManager)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CEventManager)

L2_DEFINE_OBJECT_ALLOCATOR(CEventManager, __GetClassName())

L2_DEFINE_OBJECT_NEW(CEventManager)

void CEventManager::Init(std::u16string filename) {
    // Current L2Server 0x00763c58, original owner GameEventSystem.cpp.
    L2_GUARD_FUNCTION();
    m_nTickCount = 0;

    XMLElement* root = nullptr;
    if (g_bScriptSecureEnabled) {
        root = Singleton<CXmlLoader>::inst().LoadSecure(kEventConfigFilename);
    } else if (g_bLoadEventDataFromDB) {
        const std::wstring hostFilename = Utf16ToWide(filename);
        root = Singleton<CXmlLoader>::inst().Load(hostFilename.c_str());
    } else {
        root = Singleton<CXmlLoader>::inst().Load(kEventConfigFilename);
    }

    m_nPickItemId = 0;
    m_nPickItemMsg = 0;
    if (root == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Fail to load eventdata.xml",
        //     L"GameEventSystem.cpp",
        //     0xd3)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Fail to load eventdata.xml",
        //     L"GameEventSystem.cpp",
        //     0xd3)
        L2_LOG_ERROR(
            Log, "Cannot load event definitions from %s; event initialization stops before its timer is scheduled.",
            !g_bScriptSecureEnabled && g_bLoadEventDataFromDB ? Utf16ToUtf8(filename).c_str() : "eventdata.xml"
        );
        return;
    }

    using EventFactory = GenericFactory<CEvent, std::u16string, CEvent* (*)()>;
    for (XMLElement* const child : root->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        CEvent* const event = Singleton<EventFactory>::inst().CreateObject(child->m_sName);
        if (event != nullptr && event->Load(child)) {
            const std::string hostEventName = Utf16ToUtf8(event->GetName());
            // Log.Add(
            //     LOG_ERROR,
            //     L"[NO_ERROR] [%s] event is loaded.",
            //     hostEventName.c_str())
            // L2_LOG_ERROR(Log,
            //     L"[NO_ERROR] [%s] event is loaded.",
            //     hostEventName.c_str())
            L2_LOG_INFO(Log, "Loaded definition for event '%s'.", hostEventName.c_str());
            L2_GUARD(m_EventManagerLock);
            _RegisterEvent(event);
        }
    }

    CommonDropSystemInit();
    InitDropItem();
    {
        L2_GUARD(m_EventManagerLock);
        _ResetEventTypeMap();
    }
    InitEvent();
    root->ReleaseChild();
    delete root;
    {
        L2_GUARD(m_EventManagerLock);
        _LoadDropItemAmountPerServer();
    }
    AddTimer(1000, 0);
}

void CEventManager::CommonDropSystemInit() {
    // Current L2Server 0x0075f39c, original owner GameEventSystem.cpp.
    L2_GUARD_FUNCTION();
    XMLElement* const root = g_bScriptSecureEnabled ? Singleton<CXmlLoader>::inst().LoadSecure(kCommonDropConfigFilename)
                                                    : Singleton<CXmlLoader>::inst().Load(kCommonDropConfigFilename);
    if (root == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Fail to load commondropdata.xml",
        //     L"GameEventSystem.cpp",
        //     0x127)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Fail to load commondropdata.xml",
        //     L"GameEventSystem.cpp",
        //     0x127)
        L2_LOG_WARN(
            Log,
            "[events] CommonDrop definitions were not loaded from commondropdata.xml; "
            "see the XML/file error above. Other event initialization continues."
        );
        return;
    }

    using EventFactory = GenericFactory<CEvent, std::u16string, CEvent* (*)()>;
    for (XMLElement* const child : root->m_vChild) {
        if (child == nullptr) {
            continue;
        }
        CEvent* const event = Singleton<EventFactory>::inst().CreateObject(child->m_sName);
        // The current owner assumes every common-drop element name has a
        // registered factory and calls Load without a null check.
        if (event->Load(child)) {
            L2_GUARD(m_EventManagerLock);
            _RegisterEvent(event);
        }
    }
    root->ReleaseChild();
    delete root;
}

void CEventManager::InitDropItem() {
    // Current L2Server 0x007623f8, original owner GameEventSystem.cpp.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EventManagerLock);

    m_smapDropItemList.clear();
    m_smapAgathionDropItemList.clear();
    for (const auto& entry : m_smapRegisteredEvent) {
        CEvent* const event = entry.second;
        if (event->GetEventType() == GAME_EVENT::ET_COMMONDROP_SYSTEM) {
            CommonDropItemInfoOnTick information;
            if (auto* const common = dynamic_cast<CEvent_CommonDropSystem*>(event)) {
                common->InitCommonDropItemInfo(information);
            }
            m_smapCommonDropItemList[event->GetName()] = information;
        } else if (event->GetEventType() == GAME_EVENT::ET_AGATHION) {
            DropItemInfoOnTick information;
            event->InitDropItemInfo(information);
            m_smapAgathionDropItemList[event->GetName()] = information;
        } else if (event->GetEventType() == GAME_EVENT::ET_DROP) {
            DropItemInfoOnTick information;
            event->InitDropItemInfo(information);
            m_smapDropItemList[event->GetName()] = information;
        }
    }
}

void CEventManager::InitEvent() {
    // Current L2Server 0x00750794, original owner GameEventSystem.cpp.
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_smapUnregisteredEvent) {
        if (entry.second != nullptr) {
            entry.second->Init();
        }
    }
    for (const auto& entry : m_smapRegisteredEvent) {
        if (entry.second != nullptr) {
            entry.second->Init();
        }
    }
}

L2_DEFINE_OBJECT_ADD_REF(CEventManager)

L2_DEFINE_OBJECT_TYPED_RELEASE(CEventManager)

void CEventManager::TimerExpired(int id) {
    static_cast<void>(id);
    L2_GUARD_FUNCTION();

    ++m_nTickCount;
    {
        L2_GUARD(m_EventManagerLock);

        auto unregistered = m_smapUnregisteredEvent.begin();
        while (unregistered != m_smapUnregisteredEvent.end()) {
            CEvent* const event = unregistered->second;
            if (!event->InEventTime() || !event->ShouldBeRegistered()) {
                ++unregistered;
                continue;
            }

            const std::u16string eventName(event->GetName());
            unregistered = m_smapUnregisteredEvent.erase(unregistered);
            m_smapRegisteredEvent.emplace(eventName, event);
        }

        for (const auto& entry : m_smapRegisteredEvent) {
            CEvent* const event = entry.second;
            if (event->GetEventType() == GAME_EVENT::ET_WORLD) {
                static_cast<CEvent_World*>(event)->CheckEventState();
            }

            if (!event->InEventTime() || !event->InMyTick(m_nTickCount)) {
                continue;
            }

            event->DoTickAction(m_nTickCount);
            if (event->GetEventType() == GAME_EVENT::ET_COMMONDROP_SYSTEM) {
                if (!event->InMyDropTick(m_nTickCount)) {
                    continue;
                }
                auto found = m_smapCommonDropItemList.find(event->GetName());
                if (found != m_smapCommonDropItemList.end()) {
                    UpdateDropCounters(found->second.m_baseDropInfoOnTick, event);
                }
                continue;
            }

            if (!event->InMyDropTick(m_nTickCount)) {
                continue;
            }
            auto found = m_smapDropItemList.find(event->GetName());
            if (found != m_smapDropItemList.end()) {
                UpdateDropCounters(found->second, event);
            }
        }
    }

    AddTimer(1000, 0);
}

bool CEventManager::CanDropEventItem(const int probabilityType, const bool summonedKiller) {
    // Current L2Server 0x0074c980.
    L2_GUARD_FUNCTION();
    const double probabilityRoll = ::genrand64_real1() * 100.0 + 0.0;
    if (static_cast<std::uint32_t>(probabilityType) > 12U ||
        kEventDropProbability[static_cast<std::size_t>(probabilityType)] < probabilityRoll) {
        return false;
    }
    if (!summonedKiller) {
        return true;
    }
    return ::genrand64_real1() * 1.0 + 0.0 <= 0.33;
}

bool CEventManager::CheckNpcProperties(CNPC* const npc) {
    // Current L2Server 0x0074ca90.
    L2_GUARD_FUNCTION();
    const double hpRate = npc->m_pClassConst->m_nHpRate;
    if (hpRate <= 1.0 && hpRate != 1.0) {
        const double roll = ::genrand64_real1() * 1.0 + 0.0;
        if (hpRate <= roll && roll != hpRate) {
            return false;
        }
    }
    return true;
}

bool CEventManager::CheckAndDropItem(CNPC* const npc, CCreature* const killer, const bool attackedByRange) {
    // Current L2Server 0x0075a0f0, original owner GameEventSystem.cpp.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EventManagerLock);
    if (killer == nullptr) {
        return false;
    }

    const int npcLevel = npc->m_data->m_nLevel;
    const int levelDifference =
        runtime::SubtractInt32Wrapping(npcLevel, npc->m_pContribute->GetPersonalGreatContributorLevel());
    User* const user = killer->GetUserOrMaster();
    if (user == nullptr) {
        return false;
    }

    double levelProbability = 100.0;
    if (npcLevel < 85) {
        if (User::IsInFifthClassGroup(static_cast<ClassType>(user->GetClass())) ||
            User::IsInSixthClassGroup(static_cast<ClassType>(user->GetClass())) || user->m_data->m_nLevel >= 86 ||
            levelDifference <= -9) {
            levelProbability = -1.0;
        }
    } else if (levelDifference >= -10) {
        levelProbability =
            100.0 / std::pow(2.0, static_cast<double>(std::max(runtime::SubtractInt32Wrapping(-5, levelDifference), 0)));
    } else {
        levelProbability = -1.0;
    }

    bool dropped = false;
    const int probabilityType = npc->getEventDropPorbType();

    for (auto& entry : m_smapAgathionDropItemList) {
        DropItemInfoOnTick& information = entry.second;
        CEvent_AgathionDrop* const event = static_cast<CEvent_AgathionDrop*>(m_smapRegisteredEvent[entry.first]);
        if (::genrand64_real1() * 100.0 + 0.0 > levelProbability ||
            information.m_nCurDropItemAmount >= information.m_nMaxDropItemAmount || !information.IsInNPCLevel(npcLevel) ||
            static_cast<std::int32_t>(npc->m_data->m_moveType) == CMT_FLY || npc->m_AreaMod.m_bIsUnderWater ||
            !CanDropEventItem(probabilityType, attackedByRange) || !CheckNpcProperties(npc) ||
            ::genrand64_real1() * 100.0 + 0.0 > information.m_dDropProb) {
            continue;
        }

        const int current = runtime::raw_atomic::AddI32(&information.m_nCurDropItemAmount, 1);
        if (current > 0 && current <= information.m_nMaxDropItemAmount) {
            if (information.m_dUserRatio == 0.0) {
                return dropped;
            }
            const int itemIndex =
                runtime::Win64CvttDoubleToInt32(static_cast<double>(current) / information.m_dUserRatio - 1.0);
            const std::size_t itemCount = information.m_vDropItemList.size();
            if (itemIndex < 0 || itemCount <= static_cast<std::size_t>(itemIndex)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Drop Event ItemList Range Over!! "
                //     L"ret[%d], m_dUserRator[%f], "
                //     L"DropItemListSize[%d]",
                //     L"GameEventSystem.cpp",
                //     0x191,
                //     current,
                //     information.m_dUserRatio,
                //     static_cast<int>(itemCount))
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Drop Event ItemList Range Over!! "
                //     L"ret[%d], m_dUserRator[%f], "
                //     L"DropItemListSize[%d]",
                //     L"GameEventSystem.cpp",
                //     0x191,
                //     current,
                //     information.m_dUserRatio,
                //     static_cast<int>(itemCount))
                L2_LOG_ERROR(
                    Log,
                    "Agathion drop event '%s': item index %d is outside %llu prepared entries (counter %d, user ratio "
                    "%.17g); this drop pass stops with the counter still incremented.",
                    Utf16ToUtf8(entry.first).c_str(), itemIndex, static_cast<unsigned long long>(itemCount), current,
                    information.m_dUserRatio
                );
                return dropped;
            }

            DropItemInfo* item = &runtime::CheckedVectorAt(information.m_vDropItemList, static_cast<std::size_t>(itemIndex));
            if (information.m_eDropSequence == GAME_EVENT::DT_RANDOM) {
                const int randomIndex = runtime::Win64CvttDoubleToInt32(
                    ::genrand64_real2() * static_cast<double>(static_cast<int>(itemCount)) + 0.0
                );
                item = &runtime::CheckedVectorAt(information.m_vDropItemList, static_cast<std::size_t>(randomIndex));
            }

            if (item->m_nAmountPerServer < 1 ||
                GetDropItemAmountPerServer(item->m_nItemClassID) < item->m_nAmountPerServer) {
                const double probability = event->GetProb(static_cast<User*>(killer), item);
                if (::genrand64_real1() * 100.0 + 0.0 <= probability) {
                    const double eventProbability = kEventDropProbability[static_cast<std::size_t>(probabilityType)];
                    const double amountProbability = eventProbability / 100.0;
                    int amount = runtime::Win64CvttDoubleToInt32(amountProbability >= 1.0 ? amountProbability : 1.0);
                    if (::genrand64_real1() * 100.0 + 0.0 <= eventProbability - static_cast<double>(amount) * 100.0) {
                        ++amount;
                    }
                    npc->AddItemToInventory(
                        item->m_nItemClassID, amount, ItemGetReason::Etc, 0, 0, 0, ItemOptionKey{}, 0, BaseAttribute{}
                    );
                    dropped = true;
                    if (item->m_nAmountPerServer > 0) {
                        const std::int32_t now = runtime::crt::Time32Now();
                        SetDropItemAmountPerServer(
                            item->m_nItemClassID, GetDropItemAmountPerServer(item->m_nItemClassID) + 1, now
                        );
                        db.RequestSaveDropItemAmountPerServer(
                            item->m_nItemClassID, GetDropItemAmountPerServer(item->m_nItemClassID)
                        );
                    }
                    continue;
                }
            }
        }
        static_cast<void>(runtime::raw_atomic::AddI32(&information.m_nCurDropItemAmount, -1));
    }

    for (auto& entry : m_smapDropItemList) {
        DropItemInfoOnTick& information = entry.second;
        if (::genrand64_real1() * 100.0 + 0.0 > levelProbability ||
            information.m_nCurDropItemAmount >= information.m_nMaxDropItemAmount || !information.IsInNPCLevel(npcLevel) ||
            static_cast<std::int32_t>(npc->m_data->m_moveType) == CMT_FLY || npc->m_AreaMod.m_bIsUnderWater ||
            !CanDropEventItem(probabilityType, attackedByRange) || !CheckNpcProperties(npc)) {
            continue;
        }

        const double userProbability =
            user->IsInPremiumGroup() ? information.m_dPremiumUserDropProb : information.m_dFreeUserDropProb;
        if (::genrand64_real1() * 100.0 + 0.0 > userProbability * information.m_dDropProb ||
            !information.CheckUserCondition(user)) {
            continue;
        }

        const int current = runtime::raw_atomic::AddI32(&information.m_nCurDropItemAmount, 1);
        if (current > 0 && current <= information.m_nMaxDropItemAmount) {
            if (information.m_dUserRatio == 0.0) {
                return dropped;
            }
            const int itemIndex =
                runtime::Win64CvttDoubleToInt32(static_cast<double>(current) / information.m_dUserRatio - 1.0);
            const std::size_t itemCount = information.m_vDropItemList.size();
            if (itemIndex < 0 || itemCount <= static_cast<std::size_t>(itemIndex)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Drop Event ItemList Range Over!! "
                //     L"ret[%d], m_dUserRator[%f], "
                //     L"DropItemListSize[%d]",
                //     L"GameEventSystem.cpp",
                //     0x1e1,
                //     current,
                //     information.m_dUserRatio,
                //     static_cast<int>(itemCount))
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Drop Event ItemList Range Over!! "
                //     L"ret[%d], m_dUserRator[%f], "
                //     L"DropItemListSize[%d]",
                //     L"GameEventSystem.cpp",
                //     0x1e1,
                //     current,
                //     information.m_dUserRatio,
                //     static_cast<int>(itemCount))
                L2_LOG_ERROR(
                    Log,
                    "Drop event '%s': item index %d is outside %llu prepared entries (counter %d, user ratio %.17g); this "
                    "drop pass stops with the counter still incremented.",
                    Utf16ToUtf8(entry.first).c_str(), itemIndex, static_cast<unsigned long long>(itemCount), current,
                    information.m_dUserRatio
                );
                return dropped;
            }

            DropItemInfo* item = &runtime::CheckedVectorAt(information.m_vDropItemList, static_cast<std::size_t>(itemIndex));
            if (information.m_eDropSequence == GAME_EVENT::DT_RANDOM) {
                const int randomIndex = runtime::Win64CvttDoubleToInt32(
                    ::genrand64_real2() * static_cast<double>(static_cast<int>(itemCount)) + 0.0
                );
                item = &runtime::CheckedVectorAt(information.m_vDropItemList, static_cast<std::size_t>(randomIndex));
            }

            if ((item->m_nAmountPerServer < 1 ||
                    GetDropItemAmountPerServer(item->m_nItemClassID) < item->m_nAmountPerServer) &&
                ::genrand64_real1() * 100.0 + 0.0 <= item->m_nProb) {
                const double eventProbability = kEventDropProbability[static_cast<std::size_t>(probabilityType)];
                const double amountProbability = eventProbability / 100.0;
                int amount = runtime::Win64CvttDoubleToInt32(amountProbability >= 1.0 ? amountProbability : 1.0);
                if (::genrand64_real1() * 100.0 + 0.0 <= eventProbability - static_cast<double>(amount) * 100.0) {
                    ++amount;
                }
                npc->AddItemToInventory(
                    item->m_nItemClassID, amount, ItemGetReason::Etc, 0, 0, 0, ItemOptionKey{}, 0, BaseAttribute{}
                );
                dropped = true;
                if (item->m_nAmountPerServer > 0) {
                    const std::int32_t now = runtime::crt::Time32Now();
                    SetDropItemAmountPerServer(
                        item->m_nItemClassID, GetDropItemAmountPerServer(item->m_nItemClassID) + 1, now
                    );
                    db.RequestSaveDropItemAmountPerServer(
                        item->m_nItemClassID, GetDropItemAmountPerServer(item->m_nItemClassID)
                    );
                }
                continue;
            }
        }
        static_cast<void>(runtime::raw_atomic::AddI32(&information.m_nCurDropItemAmount, -1));
    }

    for (auto& entry : m_smapCommonDropItemList) {
        CommonDropItemInfoOnTick& common = entry.second;
        DropItemInfoOnTick& information = common.m_baseDropInfoOnTick;
        if (::genrand64_real1() * 100.0 + 0.0 > levelProbability ||
            information.m_nCurDropItemAmount >= information.m_nMaxDropItemAmount) {
            continue;
        }

        if (common.m_nPCStatus != 0) {
            bool statusAccepted = false;
            if (common.m_nPCStatus == 1) {
                statusAccepted = killer->m_data->m_nKarma < 0;
            }
            if (!statusAccepted) {
                continue;
            }
        }

        if (!information.IsInNPCLevel(npcLevel) || !common.IsEqualNPCRace(npc->m_data->m_Race) ||
            static_cast<std::int32_t>(npc->m_data->m_moveType) == CMT_FLY || npc->m_AreaMod.m_bIsUnderWater ||
            !CheckNpcProperties(npc) || ::genrand64_real1() * 100.0 + 0.0 > information.m_dDropProb) {
            continue;
        }

        const int current = runtime::raw_atomic::AddI32(&information.m_nCurDropItemAmount, 1);
        if (current > 0 && current <= information.m_nMaxDropItemAmount) {
            const int itemIndex =
                runtime::Win64CvttDoubleToInt32(static_cast<double>(current) / information.m_dUserRatio - 1.0);
            const std::size_t itemCount = information.m_vDropItemList.size();
            // The strict '<' is the current executable's literal check.
            // unlike the other two routes, index == size is not rejected.
            if (itemIndex < 0 || itemCount < static_cast<std::size_t>(itemIndex)) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Drop Event ItemList Range Over!! "
                //     L"ret[%d], m_dUserRator[%f], "
                //     L"DropItemListSize[%d]",
                //     L"GameEventSystem.cpp",
                //     0x22c,
                //     current,
                //     information.m_dUserRatio,
                //     static_cast<int>(itemCount))
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Drop Event ItemList Range Over!! "
                //     L"ret[%d], m_dUserRator[%f], "
                //     L"DropItemListSize[%d]",
                //     L"GameEventSystem.cpp",
                //     0x22c,
                //     current,
                //     information.m_dUserRatio,
                //     static_cast<int>(itemCount))
                L2_LOG_ERROR(
                    Log,
                    "Common drop event '%s': item index %d is outside %llu prepared entries (counter %d, user ratio %.17g); "
                    "this drop pass stops with the counter still incremented.",
                    Utf16ToUtf8(entry.first).c_str(), itemIndex, static_cast<unsigned long long>(itemCount), current,
                    information.m_dUserRatio
                );
                return dropped;
            }

            DropItemInfo* item = &runtime::CheckedVectorAt(information.m_vDropItemList, static_cast<std::size_t>(itemIndex));
            if (information.m_eDropSequence == GAME_EVENT::DT_RANDOM) {
                const int randomIndex = runtime::Win64CvttDoubleToInt32(
                    ::genrand64_real2() * static_cast<double>(static_cast<int>(itemCount)) + 0.0
                );
                item = &runtime::CheckedVectorAt(information.m_vDropItemList, static_cast<std::size_t>(randomIndex));
            }

            if (::genrand64_real1() * 100.0 + 0.0 <= item->m_nProb) {
                npc->AddItemToInventory(
                    item->m_nItemClassID, 1, ItemGetReason::Etc, 0, 0, 0, ItemOptionKey{}, 0, BaseAttribute{}
                );
                dropped = true;
                continue;
            }
        }
        static_cast<void>(runtime::raw_atomic::AddI32(&information.m_nCurDropItemAmount, -1));
    }
    return dropped;
}

CEvent* CEventManager::GetEvent(GAME_EVENT::EVENT_TYPE type) {
    L2_GUARD_FUNCTION();
    if (type == GAME_EVENT::ET_DROP || type == GAME_EVENT::ET_COMMONDROP_SYSTEM) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] multiple event cannot get by type",
        //     L"GameEventSystem.cpp",
        //     0x2da)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] multiple event cannot get by type",
        //     L"GameEventSystem.cpp",
        //     0x2da)
        L2_LOG_ERROR(
            Log, "Cannot select one event by type %d: this type supports multiple events; the lookup returns null.",
            static_cast<int>(type)
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        return nullptr;
    }

    L2_GUARD(m_EventManagerLock);
    const auto found = m_mmapEventByType.find(type);
    return found == m_mmapEventByType.end() ? nullptr : found->second;
}

CEvent* CEventManager::GetEvent(GAME_EVENT::EVENT_TYPE type, int eventId) {
    // Current L2Server 0x00750f58.
    L2_GUARD_FUNCTION();
    if (type == GAME_EVENT::ET_DROP || type == GAME_EVENT::ET_COMMONDROP_SYSTEM) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] multiple event cannot get by type",
        //     L"GameEventSystem.cpp",
        //     0x2f4)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] multiple event cannot get by type",
        //     L"GameEventSystem.cpp",
        //     0x2f4)
        L2_LOG_ERROR(
            Log,
            "Cannot select one event by type %d and ID %d: this type supports multiple events; the lookup returns null.",
            static_cast<int>(type), eventId
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        return nullptr;
    }

    L2_GUARD(m_EventManagerLock);
    const auto found = m_mmapEventByType.find(type);
    if (found == m_mmapEventByType.end() || found->second->m_EventId != eventId) {
        return nullptr;
    }
    return found->second;
}

CEvent* CEventManager::GetEvent(std::u16string name) {
    // Current L2Server 0x007510f0.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EventManagerLock);
    const auto registered = m_smapRegisteredEvent.find(name);
    if (registered != m_smapRegisteredEvent.end()) {
        return registered->second;
    }
    const auto unregistered = m_smapUnregisteredEvent.find(name);
    return unregistered == m_smapUnregisteredEvent.end() ? nullptr : unregistered->second;
}

void CEventManager::SendEnterSystemMsg(User* const user) {
    // Current L2Server 0x007508f0.
    L2_GUARD_FUNCTION();
    if (user == nullptr || m_smapRegisteredEvent.empty()) {
        return;
    }

    L2_GUARD(m_EventManagerLock);
    for (const auto& registered : m_smapRegisteredEvent) {
        CEvent* const event = registered.second;
        if (event != nullptr && event->m_EnterSystemMsg != 0 && event->InEventTime()) {
            user->GetSocket()->SendSystemMessage(event->m_EnterSystemMsg);
        }
    }
}

void CEventManager::GetToDoListInfo(ToDoListRecommandInfoGroupMap& information, bool entering, User* user) {
    // Current L2Server 0x00764a9c.
    L2_GUARD_FUNCTION();
    static_cast<void>(entering);
    if (user == nullptr) {
        return;
    }

    L2_GUARD(m_EventManagerLock);
    for (const auto& registered : m_smapRegisteredEvent) {
        CEvent* const event = registered.second;
        if (event == nullptr || !event->m_bToDoListCheck) {
            continue;
        }

        ToDoListRecommandInfo info;
        info.m_ToDoListType = E_TODOLIST_TYPE_EVENT;
        info.m_ToDoListID.assign(event->m_sEventName.data());
        g_HTMLCache.GetToDoListNameFromHTML(
            info.m_Name, info.m_ToDoListType, info.m_ToDoListID, user->m_data->m_CountryCode
        );
        info.m_TimeStart = static_cast<std::int16_t>(event->m_StartTimeHour);
        info.m_TimeEnd = static_cast<std::int16_t>(event->m_EndTimeHour);
        info.m_LevelMin = 0;
        info.m_LevelMax = 0;
        info.m_ProgressStateType = event->InEventTime() ? E_TODOLIST_PROGRESS_TYPE_PROGRESS : E_TODOLIST_PROGRESS_TYPE_BEFOR;
        info.m_Priority = event->m_ToDayPriority;

        int group = E_TODOLIST_PROGRESS_TYPE_BEFOR;
        if (info.m_ProgressStateType == E_TODOLIST_PROGRESS_TYPE_FINISH) {
            group = E_TODOLIST_PROGRESS_TYPE_FINISH;
        }
        const auto found = information.find(group);
        if (found == information.end()) {
            ToDoListRecommandInfoMap groupInformation;
            groupInformation.emplace(static_cast<int>(info.m_TimeStart), info);
            information.emplace(group, groupInformation);
        } else {
            found->second.emplace(static_cast<int>(info.m_TimeStart), info);
        }
    }
}

void CEventManager::SendEventInfoToNPC() {
    // Current L2Server 0x00756ef4.
    L2_GUARD_FUNCTION();
    if (m_smapRegisteredEvent.empty() && m_smapUnregisteredEvent.empty()) {
        return;
    }

    std::array<char, 0x5000> packet;
    for (const auto& entry : m_smapUnregisteredEvent) {
        CEvent* const event = entry.second;
        if (event == nullptr) {
            continue;
        }
        std::vector<TimePeriodInfo> periods;
        const std::size_t periodCount = event->GetEventPeriod(periods);
        int packetSize = ::Assemble(
            packet.data(), static_cast<int>(packet.size()), "Sd", event->GetName(), static_cast<int>(periodCount)
        );
        for (const TimePeriodInfo& period : periods) {
            packetSize += ::Assemble(
                packet.data() + packetSize, static_cast<int>(packet.size()) - packetSize, "dd", period.m_nStartTime,
                period.m_nEndTime
            );
        }
        g_npcserver.Send("cb", 0x9d, packetSize, packet.data());
    }

    for (const auto& entry : m_smapRegisteredEvent) {
        CEvent* const event = entry.second;
        if (event == nullptr) {
            continue;
        }
        std::vector<TimePeriodInfo> periods;
        const std::size_t periodCount = event->GetEventPeriod(periods);
        int packetSize = ::Assemble(
            packet.data(), static_cast<int>(packet.size()), "Sd", event->GetName(), static_cast<int>(periodCount)
        );
        for (const TimePeriodInfo& period : periods) {
            packetSize += ::Assemble(
                packet.data() + packetSize, static_cast<int>(packet.size()) - packetSize, "dd", period.m_nStartTime,
                period.m_nEndTime
            );
        }
        g_npcserver.Send("cb", 0x9d, packetSize, packet.data());
    }
}

void CEvent_World::SendScheduledTime() {
    // Current L2Server 0x00750204.
    L2_GUARD_FUNCTION();
    if (m_ScheduledTimeList.empty()) {
        return;
    }

    std::array<char, 0x800> packet;
    int packetSize = 0;
    int scheduledCount = 0;
    for (const std::int32_t scheduledTime : m_ScheduledTimeList) {
        packetSize +=
            ::Assemble(packet.data() + packetSize, static_cast<int>(packet.size()) - packetSize, "d", scheduledTime);
        ++scheduledCount;
    }
    g_npcserver.Send("cddb", 0x9b, m_EventStateInfo.eventId, scheduledCount, packetSize, packet.data());
}

void CEventManager::GetEvent(GAME_EVENT::EVENT_TYPE type, std::vector<CEvent*>& events) {
    // Current L2Server 0x00758984.
    L2_GUARD_FUNCTION();
    events.clear();
    L2_GUARD(m_EventManagerLock);
    const auto range = m_mmapEventByType.equal_range(type);
    for (auto current = range.first; current != range.second; ++current) {
        events.push_back(current->second);
    }
}

void CEventManager::_GetEvent(GAME_EVENT::EVENT_TYPE type, std::vector<CEvent*>& events) {
    // Current L2Server 0x00758b2c. The caller owns
    // m_EventManagerLock. Unlike GetEvent this helper must not lock.
    L2_GUARD_FUNCTION();
    events.clear();
    const auto range = m_mmapEventByType.equal_range(type);
    for (auto current = range.first; current != range.second; ++current) {
        events.push_back(current->second);
    }
}

bool CEventManager::_RemoveEvent(std::u16string name) {
    // Current L2Server 0x00757214.
    L2_GUARD_FUNCTION();
    auto registered = m_smapRegisteredEvent.find(name);
    if (registered != m_smapRegisteredEvent.end()) {
        delete registered->second;
        m_smapRegisteredEvent.erase(registered);
        return true;
    }

    auto unregistered = m_smapUnregisteredEvent.find(name);
    if (unregistered == m_smapUnregisteredEvent.end()) {
        return false;
    }
    delete unregistered->second;
    m_smapUnregisteredEvent.erase(unregistered);
    return true;
}

void CEventManager::_ResetEventTypeMap() {
    // Current L2Server 0x00757394.
    L2_GUARD_FUNCTION();
    m_mmapEventByType.clear();
    for (const auto& entry : m_smapRegisteredEvent) {
        CEvent* const event = entry.second;
        m_mmapEventByType.emplace(event->GetEventType(), event);
    }
    for (const auto& entry : m_smapUnregisteredEvent) {
        CEvent* const event = entry.second;
        m_mmapEventByType.emplace(event->GetEventType(), event);
    }
}

void CEventManager::_RegisterEvent(CEvent* event) {
    // Current L2Server 0x00759408.
    L2_GUARD_FUNCTION();
    EventSMap& destination =
        event->ShouldBeRegistered() && !event->IsAllDone() ? m_smapRegisteredEvent : m_smapUnregisteredEvent;
    destination[std::u16string(event->GetName())] = event;
}

std::u16string CEventManager::GetRelativeEventData(const std::u16string& filename) {
    // Current L2Server 0x00756d98. The input names the DB-side payload but
    // the current inline owner always resolves it to the standard script
    // pathname.
    static_cast<void>(filename);
    return std::u16string(u"..\\Script\\eventdata.xml");
}

bool CEventManager::_Reload(std::u16string name) {
    // Current L2Server 0x00763f48.
    L2_GUARD_FUNCTION();

    XMLElement* root = nullptr;
    if (g_bScriptSecureEnabled) {
        root = Singleton<CXmlLoader>::inst().LoadSecure(kEventConfigFilename);
    } else if (g_bLoadEventDataFromDB) {
        const std::u16string relative = GetRelativeEventData(m_szFileName);
        const std::wstring hostPath = Utf16ToWide(relative);
        root = Singleton<CXmlLoader>::inst().Load(hostPath.c_str());
    } else {
        root = Singleton<CXmlLoader>::inst().Load(kEventConfigFilename);
    }

    if (root == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Fail to load eventdata.xml",
        //     L"GameEventSystem.cpp",
        //     0x40d)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Fail to load eventdata.xml",
        //     L"GameEventSystem.cpp",
        //     0x40d)
        L2_LOG_ERROR(
            Log, "Cannot reload event '%s': eventdata.xml could not be loaded; the existing event is retained.",
            Utf16ToUtf8(name).c_str()
        );
        return false;
    }

    using EventFactory = GenericFactory<CEvent, std::u16string, CEvent* (*)()>;

    bool reloaded = false;
    for (XMLElement* const child : root->m_vChild) {
        if (child == nullptr || !EqualsIgnoreCase(Attribute(child, u"Name"), name.c_str())) {
            continue;
        }

        CEvent* const event = Singleton<EventFactory>::inst().CreateObject(child->m_sName);
        // The current owner assumes every XML element name was registered by
        // its static event factory and immediately calls Load.
        if (!event->Load(child)) {
            break;
        }

        _RemoveEvent(name);
        const std::string hostEventName = Utf16ToUtf8(std::u16string(event->GetName()));
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR] [%s] event is reloaded.",
        //     hostEventName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[NO_ERROR] [%s] event is reloaded.",
        //     hostEventName.c_str())
        L2_LOG_INFO(Log, "Loaded replacement definition for event '%s'.", hostEventName.c_str());
        _RegisterEvent(event);

        if (event->GetEventType() == GAME_EVENT::ET_DROP) {
            DropItemInfoOnTick info;
            event->InitDropItemInfo(info);
            m_smapDropItemList[std::u16string(event->GetName())] = info;
            static_cast<CEvent_Drop*>(event)->DiableAllNpcPosEvent();
        } else if (event->GetEventType() == GAME_EVENT::ET_AGATHION) {
            DropItemInfoOnTick info;
            event->InitDropItemInfo(info);
            m_smapAgathionDropItemList[std::u16string(event->GetName())] = info;
            static_cast<CEvent_AgathionDrop*>(event)->DiableAllNpcPosEvent();
        }
        reloaded = true;
    }

    _ResetEventTypeMap();
    root->ReleaseChild();
    delete root;
    _LoadDropItemAmountPerServer();

    if (!reloaded) {
        return _ReloadCommonDropSystem(std::move(name));
    }
    return true;
}

bool CEventManager::_ReloadCommonDropSystem(std::u16string name) {
    // Current L2Server 0x00762060.
    L2_GUARD_FUNCTION();

    XMLElement* const root = g_bScriptSecureEnabled ? Singleton<CXmlLoader>::inst().LoadSecure(kCommonDropConfigFilename)
                                                    : Singleton<CXmlLoader>::inst().Load(kCommonDropConfigFilename);
    if (root == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Fail to load commondropdata.xml",
        //     L"GameEventSystem.cpp",
        //     0x462)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Fail to load commondropdata.xml",
        //     L"GameEventSystem.cpp",
        //     0x462)
        L2_LOG_ERROR(
            Log,
            "Cannot reload common-drop event '%s': commondropdata.xml could not be loaded; this fallback returns failure.",
            Utf16ToUtf8(name).c_str()
        );
        return false;
    }

    using EventFactory = GenericFactory<CEvent, std::u16string, CEvent* (*)()>;

    bool reloaded = false;
    for (XMLElement* const child : root->m_vChild) {
        if (child == nullptr || !EqualsIgnoreCase(Attribute(child, u"Name"), name.c_str())) {
            continue;
        }

        CEvent* const event = Singleton<EventFactory>::inst().CreateObject(child->m_sName);
        if (!event->Load(child)) {
            break;
        }

        _RemoveEvent(name);
        _RegisterEvent(event);

        CommonDropItemInfoOnTick info;
        if (auto* const common = dynamic_cast<CEvent_CommonDropSystem*>(event)) {
            common->InitCommonDropItemInfo(info);
        }
        m_smapCommonDropItemList[std::u16string(event->GetName())] = info;
        reloaded = true;
    }

    root->ReleaseChild();
    delete root;
    return reloaded;
}

void CEventManager::_LoadDropItemAmountPerServer() {
    // Current L2Server 0x004c9460. The caller owns m_EventManagerLock, so
    // this uses the non-locking event enumerator.
    L2_GUARD_FUNCTION();

    std::vector<CEvent*> events;
    _GetEvent(GAME_EVENT::ET_DROP, events);

    std::vector<std::int32_t> classIds;
    for (CEvent* const baseEvent : events) {
        const auto* const event = static_cast<const CEvent_Drop*>(baseEvent);
        for (const DropItemInfo& item : event->m_DropInfo.m_vDropItemList) {
            if (item.m_nAmountPerServer <= 0 ||
                std::find(classIds.begin(), classIds.end(), item.m_nItemClassID) != classIds.end()) {
                continue;
            }
            classIds.push_back(item.m_nItemClassID);
        }
    }

    if (classIds.empty()) {
        return;
    }

    CPacketHelperEx<20480> packet;
    for (const std::int32_t classId : classIds) {
        packet.Assemble("d", classId);
    }
    db.RequestLoadItemAmountPerServer(static_cast<int>(classIds.size()), &packet);
}

int CEventManager::Reload(GAME_EVENT::EVENT_TYPE type) {
    // Current L2Server 0x007644b8.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EventManagerLock);

    if (m_mmapEventByType.find(type) == m_mmapEventByType.end()) {
        return -1;
    }

    std::vector<CEvent*> events;
    _GetEvent(type, events);
    int failed = 0;
    for (CEvent* const event : events) {
        if (event != nullptr && !_Reload(std::u16string(event->GetName()))) {
            ++failed;
        }
    }
    _ResetEventTypeMap();
    return failed;
}

int CEventManager::ReloadAll() {
    // Current L2Server 0x00764960.
    L2_GUARD_FUNCTION();
    std::uint32_t failedTypes = 0;
    for (std::int32_t type = 0; type < GAME_EVENT::ET_MAX; ++type) {
        if (Reload(static_cast<GAME_EVENT::EVENT_TYPE>(type)) != 0) {
            failedTypes += std::uint32_t{1} << (static_cast<std::uint32_t>(type) & 0x1f);
        }
    }
    return static_cast<std::int32_t>(failedTypes);
}

bool CEventManager::_SetEventEnable(std::u16string name, bool enabled) {
    // Current L2Server 0x007592f8.
    L2_GUARD_FUNCTION();
    EventSMap& source = enabled ? m_smapUnregisteredEvent : m_smapRegisteredEvent;
    EventSMap& destination = enabled ? m_smapRegisteredEvent : m_smapUnregisteredEvent;
    const auto found = source.find(name);
    if (found == source.end()) {
        return false;
    }
    CEvent* const event = found->second;
    source.erase(found);
    destination[name] = event;
    return true;
}

bool CEventManager::SetEventEnable(std::u16string name, bool enabled) {
    // Current L2Server 0x0075b090.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EventManagerLock);
    return _SetEventEnable(std::move(name), enabled);
}

void CEventManager::OnPickUp(User* const user, CItem* const item) {
    // Current L2Server 0x004c8cc0, 377 bytes.
    L2_GUARD_FUNCTION();
    if (user == nullptr || item == nullptr || item->m_data->m_nClassID != m_nPickItemId) {
        return;
    }

    CUserBroadcaster::inst().BroadcastToAllUserEx(
        "chccS", 0x62, static_cast<std::int16_t>(m_nPickItemMsg), 1, 0, user->GetName()
    );
    m_nPickItemId = 0;
}

bool CEventManager::SetEventEnable(GAME_EVENT::EVENT_TYPE type, bool enabled) {
    // Current L2Server 0x0075ae90.
    L2_GUARD_FUNCTION();
    L2_GUARD(m_EventManagerLock);
    bool changed = false;
    const auto range = m_mmapEventByType.equal_range(type);
    for (auto current = range.first; current != range.second; ++current) {
        CEvent* const event = current->second;
        if (event != nullptr && _SetEventEnable(std::u16string(event->GetName()), enabled)) {
            changed = true;
        }
    }
    return changed;
}
