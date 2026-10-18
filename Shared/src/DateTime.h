#pragma once

#include "Shared/src/StandardContainers.h"

#include <cstdint>
#include <utility>
#include <vector>

class L2DateTime;
class CFieldCycle;
class PledgeRaidPacketHandlerAccess;
class PledgeRaidSystem;
class CDX_WOLYMPIAD_LOAD;
class WOlympiadParam;
class WOlympiadServiceProxy;
class WOlympiadService;

namespace L2DateTimeUtil {

std::u16string ToString(const L2DateTime& time);

} // namespace L2DateTimeUtil

// Current L2Server.exe stores this value object as eight consecutive 32-bit
// fields. std::int32_t is the portable spelling of the target's Win64 long.
class L2DateTime {
public:
    enum DayOfWeek : std::int32_t {
        NONE = -1,
        SUN = 0,
        MON = 1,
        TUE = 2,
        WED = 3,
        THU = 4,
        FRI = 5,
        SAT = 6,
    };

    L2DateTime();

    static L2DateTime GetCurrent();
    static L2DateTime FromTime_t(std::int32_t time);
    static DayOfWeek ConvertDayOfWeek(const std::u16string& dayOfWeek);
    static std::int32_t StringToSec(std::u16string value);

    // These five inline accessors are compiled as direct field loads by the
    // current reservation-announcement reply at 0x006a08d8. Their original
    // names/access are kept by the VS2015 Shared PDB.
    int Year() const noexcept { return m_nYear; }
    int Month() const noexcept { return m_nMonth; }
    int Day() const noexcept { return m_nDay; }
    int Hour() const noexcept { return m_nHour; }
    int Minute() const noexcept { return m_nMin; }

    std::int32_t GetStartOfTheMonth() const;
    std::int32_t GetStartOfWeek() const;
    int GetDaysInThisMonth() const;

private:
    friend class CFieldCycle;
    friend class PledgeRaidPacketHandlerAccess;
    friend class PledgeRaidSystem;
    friend class CDX_WOLYMPIAD_LOAD;
    friend class WOlympiadParam;
    friend class WOlympiadServiceProxy;
    friend class WOlympiadService;
    friend std::u16string L2DateTimeUtil::ToString(const L2DateTime& time);

    std::int32_t m_tSecond;
    std::int32_t m_nYear;
    std::int32_t m_nMonth;
    std::int32_t m_nDay;
    DayOfWeek m_eDayOfWeek;
    std::int32_t m_nHour;
    std::int32_t m_nMin;
    std::int32_t m_nSec;
};

namespace L2DateTimeUtil {

bool IsBetweenInMinute(int now, int start, int end);
int CalculateRemainSecond(std::int32_t now, std::int32_t previous, int remainSecond);
std::int32_t WeekDayToSecond(std::int32_t time, L2DateTime::DayOfWeek dayOfWeek);

} // namespace L2DateTimeUtil


// Current L2Server's next DateTime.cpp owner layer. The ordinary target
// vector is the three-pointer 0x18 materialization, not std::xvector.
struct L2WeeklyTimeCalculator {
    L2WeeklyTimeCalculator();

    std::int32_t CalculateTime(std::int32_t baseTime, std::int32_t now) const;
    std::int32_t CalculateRandomeTime(std::int32_t baseTime) const;
    std::int32_t CalculateNextTime(std::int32_t now) const;
    std::vector<std::int32_t, std::allocator<std::int32_t>> MakeCandidates(std::int32_t baseTime) const;
    std::vector<std::int32_t, std::allocator<std::int32_t>> MakeCandidatesOnlyFuture(std::int32_t baseTime) const;

    int nWeek;
    std::pair<int, int> hour;
    std::vector<L2DateTime::DayOfWeek, std::allocator<L2DateTime::DayOfWeek>> wdayFilter;
};
