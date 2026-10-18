#include "Shared/src/DateTime.h"

#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string_view>

namespace {

constexpr std::array<std::int16_t, 13> kDaysInMonth{
    0,
    31,
    28,
    31,
    30,
    31,
    30,
    31,
    31,
    30,
    31,
    30,
    31,
};

constexpr char16_t kWeekDays[7][4]{
    u"SUN",
    u"MON",
    u"TUE",
    u"WED",
    u"THU",
    u"FRI",
    u"SAT",
};

constexpr char16_t FoldAsciiCase(char16_t value) noexcept {
    return value >= u'a' && value <= u'z' ? static_cast<char16_t>(value - (u'a' - u'A')) : value;
}

bool EqualsIgnoreAsciiCase(const char16_t* left, const char16_t* right) noexcept {
    for (;;) {
        const char16_t leftValue = FoldAsciiCase(*left);
        const char16_t rightValue = FoldAsciiCase(*right);
        if (leftValue != rightValue) {
            return false;
        }
        if (leftValue == u'\0') {
            return true;
        }
        ++left;
        ++right;
    }
}


bool GetLocalTime(std::int32_t time, std::tm& result) noexcept {
    // The current body calls the 32-bit MSVC local-time routine. That target
    // routine rejects pre-epoch values instead of accepting them as POSIX
    // localtime_r does.
    if (time < 0) {
        return false;
    }
    const std::time_t hostTime = static_cast<std::time_t>(time);
#if defined(_WIN32)
    return localtime_s(&result, &hostTime) == 0;
#else
    return localtime_r(&hostTime, &result) != nullptr;
#endif
}

int DaysInMonth(int year, int month) noexcept {
    if (month == 2) {
        const bool leapYear = year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
        return 28 + static_cast<int>(leapYear);
    }
    return kDaysInMonth[static_cast<std::size_t>(month)];
}

int ParseTargetDecimal(const std::u16string& value, std::size_t offset, std::size_t count) {
    wchar_t buffer[5]{};
    for (std::size_t index = 0; index < count; ++index) {
        buffer[index] = static_cast<wchar_t>(value[offset + index]);
    }
    return static_cast<int>(std::wcstol(buffer, nullptr, 10));
}

} // namespace


L2DateTime::L2DateTime() :
    m_tSecond(0), m_nYear(0), m_nMonth(0), m_nDay(0), m_eDayOfWeek(NONE), m_nHour(0), m_nMin(0), m_nSec(0) {}

L2DateTime L2DateTime::GetCurrent() { return FromTime_t(static_cast<std::int32_t>(std::time(nullptr))); }

L2DateTime L2DateTime::FromTime_t(std::int32_t time) {
    L2DateTime result;
    std::tm localTime{};
    if (!GetLocalTime(time, localTime)) {
        return result;
    }

    result.m_tSecond = time;
    result.m_nYear = localTime.tm_year + 1900;
    result.m_nMonth = localTime.tm_mon + 1;
    result.m_nDay = localTime.tm_mday;
    result.m_eDayOfWeek = static_cast<DayOfWeek>(localTime.tm_wday);
    result.m_nHour = localTime.tm_hour;
    result.m_nMin = localTime.tm_min;
    result.m_nSec = localTime.tm_sec;
    return result;
}

L2DateTime::DayOfWeek L2DateTime::ConvertDayOfWeek(const std::u16string& dayOfWeek) {
    L2_GUARD_FUNCTION();
    for (int value = SUN; value <= SAT; ++value) {
        if (EqualsIgnoreAsciiCase(dayOfWeek.c_str(), kWeekDays[value])) {
            return static_cast<DayOfWeek>(value);
        }
    }
    return NONE;
}

std::int32_t L2DateTime::StringToSec(std::u16string value) {
    if (value.size() != 16) {
        return 0;
    }

    // The original function left tm_isdst uninitialized.
    // Let the time library infer DST from the local date.
    std::tm localTime{};
    localTime.tm_isdst = -1;
    localTime.tm_year = ParseTargetDecimal(value, 0, 4) - 1900;
    localTime.tm_mon = ParseTargetDecimal(value, 5, 2) - 1;
    localTime.tm_mday = ParseTargetDecimal(value, 8, 2);
    localTime.tm_hour = ParseTargetDecimal(value, 11, 2);
    localTime.tm_min = ParseTargetDecimal(value, 14, 2);
    localTime.tm_sec = 0;

    const std::time_t result = std::mktime(&localTime);
    return runtime::Int32FromBits(static_cast<std::int64_t>(result));
}

std::int32_t L2DateTime::GetStartOfTheMonth() const {
    const std::int64_t result = static_cast<std::int64_t>(m_tSecond) +
        (((24 - static_cast<std::int64_t>(m_nDay) * 24) - m_nHour) * 60 - m_nMin) * 60 - m_nSec;
    return runtime::Int32FromBits(result);
}

std::int32_t L2DateTime::GetStartOfWeek() const {
    const std::int64_t elapsed = ((static_cast<std::int64_t>(m_eDayOfWeek) * 24 + m_nHour) * 60 + m_nMin) * 60 + m_nSec;
    return runtime::Int32FromBits(static_cast<std::int64_t>(m_tSecond) - elapsed);
}

int L2DateTime::GetDaysInThisMonth() const { return DaysInMonth(m_nYear, m_nMonth); }

namespace L2DateTimeUtil {

bool IsBetweenInMinute(int now, int start, int end) {
    if (runtime::Int32FromBits(static_cast<std::int64_t>(end) - start) < 0) {
        return (0 <= now && now < end) || (start <= now && now <= 1440);
    }
    return start <= now && now < end;
}

int CalculateRemainSecond(std::int32_t now, std::int32_t previous, int remainSecond) {
    const std::int32_t result = runtime::Int32FromBits(static_cast<std::int64_t>(previous) - now + remainSecond);
    return result > 0 ? result : 0;
}

std::int32_t WeekDayToSecond(std::int32_t time, L2DateTime::DayOfWeek dayOfWeek) {
    const L2DateTime base = L2DateTime::FromTime_t(time);
    const std::int64_t result =
        static_cast<std::int64_t>(base.GetStartOfWeek()) + static_cast<std::int64_t>(dayOfWeek) * 86400;
    return runtime::Int32FromBits(result);
}

std::u16string ToString(const L2DateTime& time) {
    char buffer[1024]{};
    const int length = std::snprintf(
        buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", time.m_nYear, time.m_nMonth, time.m_nDay, time.m_nHour,
        time.m_nMin, time.m_nSec
    );

    std::u16string result;
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(buffer)) {
        return result;
    }

    char16_t targetBuffer[1024]{};
    for (int index = 0; index < length; ++index) {
        targetBuffer[index] = static_cast<unsigned char>(buffer[index]);
    }
    result.assign(std::u16string_view(targetBuffer, static_cast<std::size_t>(length)));
    return result;
}

} // namespace L2DateTimeUtil


L2WeeklyTimeCalculator::L2WeeklyTimeCalculator() : nWeek(0), hour(-1, -1) {}

std::vector<std::int32_t, std::allocator<std::int32_t>> L2WeeklyTimeCalculator::MakeCandidates(std::int32_t baseTime) const {
    std::vector<std::int32_t, std::allocator<std::int32_t>> candidates;

    for (const L2DateTime::DayOfWeek weekDay : wdayFilter) {
        std::int32_t candidate = L2DateTimeUtil::WeekDayToSecond(baseTime, weekDay);
        std::int32_t candidateHour = hour.first;
        if (candidateHour <= hour.second) {
            candidate = runtime::Int32FromBits(
                static_cast<std::int64_t>(candidate) + static_cast<std::int64_t>(candidateHour) * 3600
            );
            do {
                candidates.push_back(candidate);
                candidateHour = runtime::Int32FromBits(static_cast<std::int64_t>(candidateHour) + 1);
                candidate = runtime::Int32FromBits(static_cast<std::int64_t>(candidate) + 3600);
            } while (candidateHour <= hour.second);
        }
    }
    return candidates;
}

std::vector<std::int32_t, std::allocator<std::int32_t>> L2WeeklyTimeCalculator::MakeCandidatesOnlyFuture(
    std::int32_t baseTime
) const {
    auto candidates = MakeCandidates(baseTime);
    candidates.erase(
        std::remove_if(
            candidates.begin(), candidates.end(), [baseTime](std::int32_t candidate) { return candidate < baseTime; }
        ),
        candidates.end()
    );
    return candidates;
}

std::int32_t L2WeeklyTimeCalculator::CalculateRandomeTime(std::int32_t baseTime) const {
    const std::int32_t weeksLater = runtime::Int32FromBits(static_cast<std::int64_t>(nWeek) * 604800 + baseTime);
    const auto candidates = MakeCandidates(weeksLater);
    return Random(candidates);
}

std::int32_t L2WeeklyTimeCalculator::CalculateNextTime(std::int32_t now) const {
    auto candidates = MakeCandidatesOnlyFuture(now);
    std::sort(candidates.begin(), candidates.end());
    const auto next =
        std::find_if(candidates.begin(), candidates.end(), [now](std::int32_t candidate) { return now < candidate; });
    return next == candidates.end() ? now : *next;
}

std::int32_t L2WeeklyTimeCalculator::CalculateTime(std::int32_t baseTime, std::int32_t now) const {
    if (nWeek < 1 || wdayFilter.empty()) {
        return 0;
    }

    const std::int32_t stepChangeTime = CalculateRandomeTime(baseTime);
    return stepChangeTime <= now ? CalculateNextTime(now) : stepChangeTime;
}
