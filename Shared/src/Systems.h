#pragma once

#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>


// Shared/systems.cpp current Event envelope. The target object is one
// virtual-destructor slot followed by the native handle at +0x08. Keep the
// handle opaque here so the public layout is identical on Win32 and POSIX.
class Event {
public:
    virtual ~Event();

    bool Signal();
    bool Remove();

protected:
    Event() noexcept = default;

    void* mHandle; // +0x08
};

class AutoEvent final : public Event {
public:
    explicit AutoEvent(bool initialState, const wchar_t* name = nullptr);
    ~AutoEvent() override;
};


// Cross-platform spelling of the Win32 _FILETIME payload used by the
// original TimeValue API. The two DWORDs and their order are ABI-significant.
struct FileTime {
    std::uint32_t dwLowDateTime;
    std::uint32_t dwHighDateTime;
};

// The target was built with WinSock's 32-bit-long timeval. A host timeval
// cannot be embedded here because time_t/long are 64-bit on most LP64 hosts.
struct Timeval32 {
    std::int32_t tv_sec;
    std::int32_t tv_usec;
};

class TimeValue {
public:
    static const TimeValue zero;
    static const TimeValue max_time;

    // Number of 100 ns intervals between 1601-01-01 and 1970-01-01.
    static constexpr std::uint64_t FILETIMEtoTimevalSkew = UINT64_C(0x019db1ded53e8000);

    TimeValue() noexcept { Set(0, 0); }

    TimeValue(const TimeValue&) noexcept = default;

    explicit TimeValue(const FileTime& fileTime) noexcept { Set(fileTime); }

    explicit TimeValue(const Timeval32& timeValue) noexcept { Set(timeValue); }

    TimeValue(std::int32_t seconds, std::int32_t microseconds) noexcept { Set(seconds, microseconds); }

    void Set(const FileTime& fileTime) noexcept;

    void Set(const Timeval32& timeValue) noexcept { Set(timeValue.tv_sec, timeValue.tv_usec); }

    void Set(std::int32_t seconds, std::int32_t microseconds) noexcept {
        mTv.tv_sec = seconds;
        mTv.tv_usec = microseconds;
        Normalize();
    }

    operator Timeval32() const noexcept { return mTv; }

    operator const Timeval32*() const noexcept { return &mTv; }

    operator FileTime() const noexcept;

    void Sec(std::int32_t seconds) noexcept { mTv.tv_sec = seconds; }

    std::int32_t Sec() const noexcept { return mTv.tv_sec; }

    void Usec(std::int32_t microseconds) noexcept {
        // This is only a field mutator. The original inline
        // method did not call Normalize.
        mTv.tv_usec = microseconds;
    }

    std::int32_t Usec() const noexcept { return mTv.tv_usec; }

    std::int32_t GetMilliSecond() const noexcept {
        const std::uint32_t milliseconds =
            static_cast<std::uint32_t>(mTv.tv_sec) * UINT32_C(1000) + static_cast<std::uint32_t>(mTv.tv_usec / 1000);
        return runtime::Int32FromBits(milliseconds);
    }

    TimeValue& operator+=(const TimeValue& other) noexcept {
        mTv.tv_sec = runtime::AddInt32Wrapping(mTv.tv_sec, other.mTv.tv_sec);
        mTv.tv_usec = runtime::AddInt32Wrapping(mTv.tv_usec, other.mTv.tv_usec);
        Normalize();
        return *this;
    }

    TimeValue& operator-=(const TimeValue& other) noexcept {
        mTv.tv_sec = runtime::SubtractInt32Wrapping(mTv.tv_sec, other.mTv.tv_sec);
        mTv.tv_usec = runtime::SubtractInt32Wrapping(mTv.tv_usec, other.mTv.tv_usec);
        Normalize();
        return *this;
    }

    TimeValue& operator=(const TimeValue& other) noexcept {
        mTv.tv_sec = other.mTv.tv_sec;
        mTv.tv_usec = other.mTv.tv_usec;
        return *this;
    }

    friend TimeValue operator+(TimeValue left, const TimeValue& right) noexcept {
        left += right;
        return left;
    }

    friend TimeValue operator-(TimeValue left, const TimeValue& right) noexcept {
        left -= right;
        return left;
    }

    friend bool operator>(const TimeValue& left, const TimeValue& right) noexcept {
        return left.mTv.tv_sec > right.mTv.tv_sec ||
            (left.mTv.tv_sec == right.mTv.tv_sec && left.mTv.tv_usec > right.mTv.tv_usec);
    }

    friend bool operator>=(const TimeValue& left, const TimeValue& right) noexcept {
        return left.mTv.tv_sec > right.mTv.tv_sec ||
            (left.mTv.tv_sec == right.mTv.tv_sec && left.mTv.tv_usec >= right.mTv.tv_usec);
    }

    friend bool operator<(const TimeValue& left, const TimeValue& right) noexcept { return right > left; }

    friend bool operator<=(const TimeValue& left, const TimeValue& right) noexcept { return right >= left; }

    friend bool operator==(const TimeValue& left, const TimeValue& right) noexcept {
        return left.mTv.tv_sec == right.mTv.tv_sec && left.mTv.tv_usec == right.mTv.tv_usec;
    }

    friend bool operator!=(const TimeValue& left, const TimeValue& right) noexcept { return !(left == right); }

private:
    void Normalize() noexcept;

    Timeval32 mTv;
};

class DateTime {
public:
    DateTime();

    DateTime(const char16_t* date, const char16_t* time);

    explicit DateTime(const TimeValue& timeValue) { Update(timeValue); }

    DateTime(int year, int month, int day, int hour, int minute, int second, int microsecond, int weekday) noexcept :
        mYear(year),
        mMonth(month),
        mDay(day),
        mHour(hour),
        mMinute(minute),
        mSecond(second),
        mMicrosec(microsecond),
        mWday(weekday) {}

    void Update(const TimeValue& timeValue);
    void Update();

    void Day(int value) noexcept { mDay = value; }
    int Day() const noexcept { return mDay; }

    void Month(int value) noexcept { mMonth = value; }
    int Month() const noexcept { return mMonth; }

    void Year(int value) noexcept { mYear = value; }
    int Year() const noexcept { return mYear; }

    void Hour(int value) noexcept { mHour = value; }
    int Hour() const noexcept { return mHour; }

    void Minute(int value) noexcept { mMinute = value; }
    int Minute() const noexcept { return mMinute; }

    void Second(int value) noexcept { mSecond = value; }
    int Second() const noexcept { return mSecond; }

    void Microsec(int value) noexcept { mMicrosec = value; }
    int Microsec() const noexcept { return mMicrosec; }

    void Weekday(int value) noexcept { mWday = value; }
    int Weekday() const noexcept { return mWday; }

    DateTime& operator=(const DateTime&) noexcept = default;

    std::wstring ToString(std::wstring format = {}) const;

private:
    int mYear;
    int mMonth;
    int mDay;
    int mHour;
    int mMinute;
    int mSecond;
    int mMicrosec;
    int mWday;
};

DateTime TimeValueToDateTime(const TimeValue& timeValue);

TimeValue DateTimeToTimeValue(DateTime* dateTime);
