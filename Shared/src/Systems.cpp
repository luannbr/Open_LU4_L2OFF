#include "Shared/src/Systems.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/EventHandle.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/StringTokenizer.h"
#include "Shared/src/TextRuntime.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <cwchar>
#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {

constexpr std::uint64_t kTicksPerSecond = UINT64_C(10000000);
constexpr std::uint64_t kTicksPerMicrosecond = UINT64_C(10);
constexpr std::int64_t kMicrosecondsPerSecond = INT64_C(1000000);

std::uint64_t FileTimeBits(const FileTime& fileTime) noexcept {
    return static_cast<std::uint64_t>(fileTime.dwLowDateTime) | (static_cast<std::uint64_t>(fileTime.dwHighDateTime) << 32U);
}

FileTime FileTimeFromBits(std::uint64_t bits) noexcept {
    return {
        static_cast<std::uint32_t>(bits),
        static_cast<std::uint32_t>(bits >> 32U),
    };
}

FileTime CurrentSystemFileTime() noexcept {
#if defined(_WIN32)
    FILETIME native{};
    ::GetSystemTimeAsFileTime(&native);
    return {
        native.dwLowDateTime,
        native.dwHighDateTime,
    };
#else
    const auto sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(sinceEpoch).count();
    const std::int64_t ticksSinceEpoch = microseconds * static_cast<std::int64_t>(kTicksPerMicrosecond);
    const std::uint64_t fileTime = TimeValue::FILETIMEtoTimevalSkew + static_cast<std::uint64_t>(ticksSinceEpoch);
    return FileTimeFromBits(fileTime);
#endif
}

struct LocalCalendar {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int microsecond;
    int weekday;
};

void ToLocalCalendar(const TimeValue& timeValue, LocalCalendar& output) noexcept {
#if defined(_WIN32)
    const FileTime source = static_cast<FileTime>(timeValue);
    const FILETIME utc{
        source.dwLowDateTime,
        source.dwHighDateTime,
    };
    FILETIME local;
    SYSTEMTIME system;
    // Current L2NPC.exe ignores both BOOL results and calls
    // the second conversion even when the first one fails.
    ::FileTimeToLocalFileTime(&utc, &local);
    ::FileTimeToSystemTime(&local, &system);

    output.year = system.wYear;
    output.month = system.wMonth;
    output.day = system.wDay;
    output.hour = system.wHour;
    output.minute = system.wMinute;
    output.second = system.wSecond;
    output.microsecond = static_cast<int>(system.wMilliseconds) * 1000;
    output.weekday = system.wDayOfWeek;
#else
    const std::int64_t totalMicroseconds =
        static_cast<std::int64_t>(timeValue.Sec()) * kMicrosecondsPerSecond + timeValue.Usec();
    std::int64_t seconds = totalMicroseconds / kMicrosecondsPerSecond;
    std::int64_t remainder = totalMicroseconds % kMicrosecondsPerSecond;
    if (remainder < 0) {
        --seconds;
        remainder += kMicrosecondsPerSecond;
    }

    const std::time_t timestamp = static_cast<std::time_t>(seconds);
    std::tm local;
    // localtime_r is the POSIX transport for the two unchecked Win32 calls
    // above. Its result is ignored for the same reason.
    ::localtime_r(&timestamp, &local);

    output.year = local.tm_year + 1900;
    output.month = local.tm_mon + 1;
    output.day = local.tm_mday;
    output.hour = local.tm_hour;
    output.minute = local.tm_min;
    output.second = local.tm_sec;
    output.microsecond = static_cast<int>(remainder / 1000) * 1000;
    output.weekday = local.tm_wday;
#endif
}

} // namespace

Event::~Event() { Remove(); }

bool Event::Signal() { return runtime::event_handle::Set(mHandle); }

bool Event::Remove() {
    const auto invalidHandle = reinterpret_cast<void*>(std::numeric_limits<std::uintptr_t>::max());
    bool removed = true;
    if (mHandle != invalidHandle) {
        removed = runtime::event_handle::Close(mHandle);
        mHandle = invalidHandle;
    }
    return removed;
}

AutoEvent::AutoEvent(bool initialState, const wchar_t* name) {
#if defined(_WIN32)
    mHandle = ::CreateEventW(nullptr, FALSE, initialState ? TRUE : FALSE, name);
#else
    static_cast<void>(name);
    mHandle = runtime::event_handle::Create(false, initialState);
#endif
}

AutoEvent::~AutoEvent() = default;

// Current L2Server.exe CRT roots 0x00db57c4/0x00db57d0 publish these exact
// 0x08 values at 0x20d87040/0x015e70e0 before calling Normalize().
const TimeValue TimeValue::zero(0, 0);
const TimeValue TimeValue::max_time(std::numeric_limits<std::int32_t>::max(), 999999);

void TimeValue::Normalize() noexcept {
    // Current L2Server.exe 0x00adeae8, L2NPC.exe 0x0053e71c and CacheD.exe
    // 0x0059aa64. These loops, including their 32-bit wrapping increments,
    // are kept instead of replacing them with a division shortcut.
    if (mTv.tv_usec < 1000000) {
        while (mTv.tv_usec <= -1000000) {
            mTv.tv_sec = runtime::AddInt32Wrapping(mTv.tv_sec, -1);
            mTv.tv_usec = runtime::AddInt32Wrapping(mTv.tv_usec, 1000000);
        }
    } else {
        do {
            mTv.tv_sec = runtime::AddInt32Wrapping(mTv.tv_sec, 1);
            mTv.tv_usec = runtime::AddInt32Wrapping(mTv.tv_usec, -1000000);
        } while (mTv.tv_usec >= 1000000);
    }

    if (mTv.tv_sec >= 1 && mTv.tv_usec < 0) {
        mTv.tv_sec = runtime::AddInt32Wrapping(mTv.tv_sec, -1);
        mTv.tv_usec = runtime::AddInt32Wrapping(mTv.tv_usec, 1000000);
    } else if (mTv.tv_sec < 0 && mTv.tv_usec > 0) {
        mTv.tv_sec = runtime::AddInt32Wrapping(mTv.tv_sec, 1);
        mTv.tv_usec = runtime::AddInt32Wrapping(mTv.tv_usec, -1000000);
    }
}

void TimeValue::Set(const FileTime& fileTime) noexcept {
    // Current L2NPC.exe 0x0053e824 uses unsigned 64-bit subtraction and
    // division. Dates preceding the Unix epoch therefore wrap
    // through the same low 32-bit result as the Windows target.
    const std::uint64_t delta = FileTimeBits(fileTime) - FILETIMEtoTimevalSkew;
    mTv.tv_sec = runtime::Int32FromBits(static_cast<std::uint32_t>(delta / kTicksPerSecond));
    mTv.tv_usec = runtime::Int32FromBits(static_cast<std::uint32_t>((delta % kTicksPerSecond) / kTicksPerMicrosecond));
    Normalize();
}

TimeValue::operator FileTime() const noexcept {
    // The current Update implementations inline this exact conversion:
    // sign-extend seconds/useconds, offset to 1601, then scale to 100 ns.
    const std::int64_t secondsSince1601 = static_cast<std::int64_t>(mTv.tv_sec) + INT64_C(11644473600);
    const std::int64_t ticks = secondsSince1601 * static_cast<std::int64_t>(kTicksPerSecond) +
        static_cast<std::int64_t>(mTv.tv_usec) * static_cast<std::int64_t>(kTicksPerMicrosecond);
    return FileTimeFromBits(static_cast<std::uint64_t>(ticks));
}

DateTime::DateTime() { Update(); }

DateTime::DateTime(const char16_t* const date, const char16_t* const time) :
    mYear(0), mMonth(0), mDay(0), mHour(0), mMinute(0), mSecond(0), mMicrosec(0), mWday(0) {
    // Current L2Server 0x00adeebc. StringTokenizer skips
    // repeated delimiters in CountTokens while NextToken can still return an
    // empty leading token. Retain that original asymmetry. The target also
    // length-scans each input without a null check.
    L2_GUARD_FUNCTION();

    StringTokenizer dateTokens(std::u16string(date), std::u16string(u"/"));
    if (dateTokens.CountTokens() == 3) {
        mYear = WindowsWtol(dateTokens.NextToken());
        mMonth = WindowsWtol(dateTokens.NextToken());
        mDay = WindowsWtol(dateTokens.NextToken());
    }

    StringTokenizer timeTokens(std::u16string(time), std::u16string(u":"));
    if (timeTokens.CountTokens() == 3) {
        mHour = WindowsWtol(timeTokens.NextToken());
        mMinute = WindowsWtol(timeTokens.NextToken());
        mSecond = WindowsWtol(timeTokens.NextToken());
    }
}

void DateTime::Update() {
    const TimeValue now(CurrentSystemFileTime());
    Update(now);
}

void DateTime::Update(const TimeValue& timeValue) {
    // Current L2NPC.exe 0x0053e77c and CacheD.exe 0x0059aac4.
    LocalCalendar local;
    ToLocalCalendar(timeValue, local);

    mMonth = local.month;
    mDay = local.day;
    mYear = local.year;
    mHour = local.hour;
    mMinute = local.minute;
    mSecond = local.second;
    mMicrosec = local.microsecond;
    mWday = local.weekday;
}

DateTime TimeValueToDateTime(const TimeValue& timeValue) {
    // Current L2NPC.exe 0x0053e87c duplicates DateTime::Update's conversion
    // inside this guarded free function.
    L2_GUARD_FUNCTION();
    return DateTime(timeValue);
}

TimeValue DateTimeToTimeValue(DateTime* const dateTime) {
    // Current L2Server 0x00adedf8. The target validates these intentionally
    // broad zero-based bounds, then supplies only the six calendar lanes to
    // _mktime32. Tm_wday/tm_yday/tm_isdst keep stack state.
    if (dateTime->Year() >= 1970 && dateTime->Year() <= 2050 && dateTime->Month() >= 0 && dateTime->Month() <= 12 &&
        dateTime->Day() >= 0 && dateTime->Day() <= 31 && dateTime->Hour() >= 0 && dateTime->Hour() <= 24 &&
        dateTime->Minute() >= 0 && dateTime->Minute() <= 60 && dateTime->Second() >= 0 && dateTime->Second() <= 60) {
        std::tm localTime;
        localTime.tm_year = dateTime->Year() - 1900;
        localTime.tm_mon = dateTime->Month() - 1;
        localTime.tm_mday = dateTime->Day();
        localTime.tm_hour = dateTime->Hour();
        localTime.tm_min = dateTime->Minute();
        localTime.tm_sec = dateTime->Second();
        return TimeValue(runtime::crt::Mktime32(&localTime), dateTime->Microsec());
    }
    return TimeValue::zero;
}

std::wstring DateTime::ToString(std::wstring format) const {
    // Current L2NPC.exe 0x0053e9a4.
    L2_GUARD_FUNCTION();

    if (format.empty()) {
        format = L"%d/%02d/%02d %02d:%02d:%02d";
    }

    wchar_t buffer[256]{};
    const int length = std::swprintf(buffer, 256, format.c_str(), mYear, mMonth, mDay, mHour, mMinute, mSecond);
    if (length < 1) {
        return {};
    }
    return buffer;
}
