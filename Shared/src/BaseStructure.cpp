#include "Shared/src/BaseStructure.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Runtime/LogStorage.h"
#include "Shared/src/Runtime/ResourceDirectories.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/IOCPacket.h"
#include "L2Core/src/inc/Guard.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace {

std::uint32_t RepeatedBindingDayMask(const int bindingDayMask) {
    const std::uint32_t mask = static_cast<std::uint32_t>(bindingDayMask);
    return (mask << 7U) | mask;
}

bool HasPositiveBindingDayBit(const std::uint32_t repeatedMask, const std::int32_t dayIndex) {
    const std::uint32_t bit = std::uint32_t{1} << (static_cast<std::uint32_t>(dayIndex) & 0x1fU);
    return runtime::Int32FromBits(repeatedMask & bit) > 0;
}

int BindingDayMinuteResult(const std::int32_t dayCount, const int minuend, const int subtrahend) {
    const std::uint32_t bits = static_cast<std::uint32_t>(dayCount) * 1440U + static_cast<std::uint32_t>(minuend) -
        static_cast<std::uint32_t>(subtrahend);
    return runtime::Int32FromBits(bits);
}

[[noreturn]] void CrashForMissingSharedFile(const wchar_t* fileName) {
    // Current L2Server.exe 0x004af74a-0x004af7b6 and current Cached.exe
    // 0x00450fae-0x0045101f. The line and reason are direct machine facts.
    // the same source macro is visible in the VS2015 Shared donor.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s]file is not EXIST!",
    //     fileName)
    // Noise: The caller now reports the attempted path and exact failure
    // before entering fatal handling. Repeating a generic error adds no context.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[scripts] Cannot read metadata for required file \"%ls\". Terminating the server.",
    //     fileName ? fileName : L"(not specified)")
    ForceCrash(CrashInfo::CACHEDSCRIPT_IS_NOT_UPDATED);
    std::abort();
}

#if !defined(_WIN32)

std::string HostPathFromTargetPath(const wchar_t* fileName) {
    std::string path = WideToUtf8(fileName == nullptr ? std::wstring_view{} : std::wstring_view(fileName));
    std::replace(path.begin(), path.end(), '\\', '/');
    return runtime::ResolveResourcePath(path).string();
}

void SetTargetFileTime(FileTime& destination, std::int64_t unixSeconds, long nanoseconds) {
    constexpr std::int64_t kSecondsFrom1601To1970 = INT64_C(11644473600);
    constexpr std::uint64_t kTicksPerSecond = UINT64_C(10000000);

    if (unixSeconds < -kSecondsFrom1601To1970) {
        destination = {};
        return;
    }
    const std::uint64_t ticks = static_cast<std::uint64_t>(unixSeconds + kSecondsFrom1601To1970) * kTicksPerSecond +
        static_cast<std::uint64_t>(nanoseconds < 0 ? 0 : nanoseconds) / 100U;
    destination.dwLowDateTime = static_cast<std::uint32_t>(ticks);
    destination.dwHighDateTime = static_cast<std::uint32_t>(ticks >> 32U);
}

#endif

} // namespace

bool FAngle::IsBetween(FAngle begin, FAngle end) {
    Yaw = static_cast<std::uint16_t>(Yaw);
    begin.Yaw = static_cast<std::uint16_t>(begin.Yaw);
    end.Yaw = static_cast<std::uint16_t>(end.Yaw);

    if (end.Yaw < begin.Yaw) {
        end.Yaw += 0x10000;
    }
    if (Yaw < begin.Yaw) {
        Yaw += 0x10000;
    }
    return begin.Yaw <= Yaw && Yaw <= end.Yaw;
}

FAngle::Enum FAngle::GetAngleDiff(const FAngle other) {
    const std::uint32_t yaw = static_cast<std::uint16_t>(Yaw);
    const std::uint32_t otherYaw = static_cast<std::uint16_t>(other.Yaw);
    const std::uint32_t difference = yaw < otherYaw ? otherYaw - yaw : yaw - otherYaw;

    if ((0x2000U <= difference && difference <= 0x6000U) || (0xa000U <= difference && difference <= 0xe000U)) {
        return Side;
    }
    if (difference < 0x2000U || 0xe000U < difference) {
        return Back;
    }
    return Front;
}

ByHandleFileInformation GetFileInfo(const wchar_t* const fileName) {
    L2_GUARD_FUNCTION();

    ByHandleFileInformation result{};

#if defined(_WIN32)
    // const HANDLE file = ::CreateFileW(
    //     runtime::ResolveResourcePath(fileName).wstring().c_str(),
    //     GENERIC_READ,
    //     FILE_SHARE_READ,
    //     nullptr,
    //     OPEN_EXISTING,
    //     FILE_ATTRIBUTE_NORMAL,
    //     nullptr)
    const std::wstring path = runtime::ResolveResourcePath(fileName).wstring();
    const HANDLE file =
        ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        {
            const runtime::logging::StorageErrorState failure;
            // Log.Add(
            //     LOG_ERROR,
            //     L"[scripts] Cannot open required file \"%ls\" for metadata (Windows error %lu). Terminating the server.",
            //     path.c_str(),
            //     static_cast<unsigned long>(failure.savedWindowsError))
            L2_LOG_ERROR(
                Log,
                u"[scripts] Cannot open required file \"%ls\" for metadata (Windows error %lu); terminating the server.",
                path.c_str(), static_cast<unsigned long>(failure.savedWindowsError)
            );
        }
        CrashForMissingSharedFile(fileName);
    }

    BY_HANDLE_FILE_INFORMATION native{};
    // ::GetFileInformationByHandle(
    //     file,
    //     &native)
    const BOOL metadataRead = ::GetFileInformationByHandle(file, &native);
    if (!metadataRead) {
        const runtime::logging::StorageErrorState failure;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[scripts] Cannot read metadata for \"%ls\" (Windows error %lu). "
        //     L"The metadata buffer will still be returned despite this failure.",
        //     path.c_str(),
        //     static_cast<unsigned long>(failure.savedWindowsError))
        L2_LOG_ERROR(
            Log,
            u"[scripts] Cannot read metadata for \"%ls\" (Windows error %lu). "
            u"The metadata buffer will still be returned despite this failure.",
            path.c_str(), static_cast<unsigned long>(failure.savedWindowsError)
        );
    }
    // ::CloseHandle(file)
    const BOOL fileClosed = ::CloseHandle(file);
    if (!fileClosed) {
        const runtime::logging::StorageErrorState failure;
        // Log.Add(
        //     LOG_ERROR,
        //     L"[scripts] Cannot close metadata input \"%ls\" (Windows error %lu). Continuing without retrying the close.",
        //     path.c_str(),
        //     static_cast<unsigned long>(failure.savedWindowsError))
        L2_LOG_ERROR(
            Log,
            u"[scripts] Cannot close metadata input \"%ls\" (Windows error %lu). Continuing without retrying the close.",
            path.c_str(), static_cast<unsigned long>(failure.savedWindowsError)
        );
    }

    result.dwFileAttributes = native.dwFileAttributes;
    result.ftCreationTime = {
        native.ftCreationTime.dwLowDateTime,
        native.ftCreationTime.dwHighDateTime,
    };
    result.ftLastAccessTime = {
        native.ftLastAccessTime.dwLowDateTime,
        native.ftLastAccessTime.dwHighDateTime,
    };
    result.ftLastWriteTime = {
        native.ftLastWriteTime.dwLowDateTime,
        native.ftLastWriteTime.dwHighDateTime,
    };
    result.dwVolumeSerialNumber = native.dwVolumeSerialNumber;
    result.nFileSizeHigh = native.nFileSizeHigh;
    result.nFileSizeLow = native.nFileSizeLow;
    result.nNumberOfLinks = native.nNumberOfLinks;
    result.nFileIndexHigh = native.nFileIndexHigh;
    result.nFileIndexLow = native.nFileIndexLow;
#else
    const std::string path = HostPathFromTargetPath(fileName);
    struct stat native{};
    if (path.empty() || ::stat(path.c_str(), &native) != 0) {
        {
            const runtime::logging::StorageErrorState failure;
            if (path.empty()) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[scripts] Required file \"%ls\" resolves to an empty path. Terminating the server without calling
                //     stat.", fileName ? fileName : L"(not specified)")
                L2_LOG_ERROR(
                    Log,
                    u"[scripts] Required file \"%ls\" resolves to an empty path; terminating the server without calling "
                    u"stat.",
                    fileName ? fileName : L"(not specified)"
                );
            } else {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[scripts] Cannot read metadata for required file \"%ls\" (errno %d: %ls). Terminating the server.",
                //     Utf8ToWide(path).c_str(),
                //     failure.savedErrno,
                //     failure.savedErrno ? Utf8ToWide(std::strerror(failure.savedErrno)).c_str() : L"not reported")
                L2_LOG_ERROR(
                    Log,
                    u"[scripts] Cannot read metadata for required file \"%ls\" (errno %d: %ls); terminating the server.",
                    Utf8ToWide(path).c_str(), failure.savedErrno,
                    failure.savedErrno ? Utf8ToWide(std::strerror(failure.savedErrno)).c_str() : L"not reported"
                );
            }
        }
        CrashForMissingSharedFile(fileName);
    }

    constexpr std::uint32_t kFileAttributeDirectory = 0x10;
    constexpr std::uint32_t kFileAttributeNormal = 0x80;
    result.dwFileAttributes = S_ISDIR(native.st_mode) ? kFileAttributeDirectory : kFileAttributeNormal;

#if defined(__APPLE__)
    SetTargetFileTime(result.ftCreationTime, native.st_birthtimespec.tv_sec, native.st_birthtimespec.tv_nsec);
    SetTargetFileTime(result.ftLastAccessTime, native.st_atimespec.tv_sec, native.st_atimespec.tv_nsec);
    SetTargetFileTime(result.ftLastWriteTime, native.st_mtimespec.tv_sec, native.st_mtimespec.tv_nsec);
#else
    SetTargetFileTime(result.ftCreationTime, native.st_ctim.tv_sec, native.st_ctim.tv_nsec);
    SetTargetFileTime(result.ftLastAccessTime, native.st_atim.tv_sec, native.st_atim.tv_nsec);
    SetTargetFileTime(result.ftLastWriteTime, native.st_mtim.tv_sec, native.st_mtim.tv_nsec);
#endif

    const std::uint64_t fileSize = native.st_size < 0 ? 0 : static_cast<std::uint64_t>(native.st_size);
    const std::uint64_t fileIndex = static_cast<std::uint64_t>(native.st_ino);
    result.dwVolumeSerialNumber = static_cast<std::uint32_t>(native.st_dev);
    result.nFileSizeHigh = static_cast<std::uint32_t>(fileSize >> 32U);
    result.nFileSizeLow = static_cast<std::uint32_t>(fileSize);
    result.nNumberOfLinks = static_cast<std::uint32_t>(native.st_nlink);
    result.nFileIndexHigh = static_cast<std::uint32_t>(fileIndex >> 32U);
    result.nFileIndexLow = static_cast<std::uint32_t>(fileIndex);
#endif

    return result;
}

bool ScriptFileUtil::CacheToServer::CheckFileInfo(const unsigned char* packet) {
    // Current L2Server.exe 0x004af844 (457 bytes). Keep Itemdata last: the
    // paired current CacheD producer emits the same five size pairs.
    L2_GUARD_FUNCTION();
    constexpr std::array<const wchar_t*, 5> files{
        L"..\\Script\\npc_pch.txt",
        L"..\\Script\\manual_pch.txt",
        L"..\\Script\\category_pch.txt",
        L"..\\Script\\categorydata.txt",
        L"..\\Script\\Itemdata.txt",
    };

    for (const wchar_t* const file : files) {
        const ByHandleFileInformation info = GetFileInfo(file);
        std::uint32_t fileSizeHigh = 0;
        std::uint32_t fileSizeLow = 0;
        packet = ::Disassemble(packet, "dd", &fileSizeHigh, &fileSizeLow);
        if (fileSizeHigh != info.nFileSizeHigh || fileSizeLow != info.nFileSizeLow) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s] is not valid!!",
            //     file)
            // Log.Add(
            //     LOG_ERROR,
            //     L"[scripts] Size mismatch for \"%ls\": L2Server has %llu bytes, CacheD reports %llu bytes. "
            //     L"Script compatibility check failed. Use matching script files on both servers.",
            //     file,
            //     (static_cast<unsigned long long>(info.nFileSizeHigh) << 32U) | info.nFileSizeLow,
            //     (static_cast<unsigned long long>(fileSizeHigh) << 32U) | fileSizeLow)
            L2_LOG_ERROR(
                Log,
                u"[scripts] Size mismatch for \"%ls\": L2Server has %llu bytes, CacheD reports %llu bytes. "
                u"Script compatibility check failed; use matching script files on both servers.",
                file, (static_cast<unsigned long long>(info.nFileSizeHigh) << 32U) | info.nFileSizeLow,
                (static_cast<unsigned long long>(fileSizeHigh) << 32U) | fileSizeLow
            );
            return false;
        }
    }
    return true;
}

int ResetBindingDayHelper::FindLatestResetBindingDay(
    int bindingDayMask, int resetMinuteOfDay, int currentWeekday, int currentMinuteOfDay
) {
    L2_GUARD_FUNCTION();

    const bool afterReset = currentMinuteOfDay > resetMinuteOfDay;
    std::int32_t dayIndex =
        afterReset ? static_cast<std::int32_t>(currentWeekday) : runtime::SubtractInt32Wrapping(currentWeekday, 1);
    dayIndex = runtime::AddInt32Wrapping(dayIndex, 7);

    const std::uint32_t repeatedMask = RepeatedBindingDayMask(bindingDayMask);
    std::int32_t elapsedDays = 0;
    while (dayIndex >= 0 && !HasPositiveBindingDayBit(repeatedMask, dayIndex)) {
        elapsedDays = runtime::AddInt32Wrapping(elapsedDays, 1);
        dayIndex = runtime::SubtractInt32Wrapping(dayIndex, 1);
    }

    const std::int32_t dayCount = afterReset ? elapsedDays : runtime::AddInt32Wrapping(elapsedDays, 1);
    return BindingDayMinuteResult(dayCount, currentMinuteOfDay, resetMinuteOfDay);
}

int ResetBindingDayHelper::FindNextResetBindingDay(
    int bindingDayMask, int resetMinuteOfDay, int currentWeekday, int currentMinuteOfDay
) {
    L2_GUARD_FUNCTION();

    const bool afterReset = currentMinuteOfDay > resetMinuteOfDay;
    std::int32_t dayIndex = static_cast<std::int32_t>(currentWeekday);
    if (afterReset) {
        dayIndex = runtime::AddInt32Wrapping(dayIndex, 1);
    }

    const std::uint32_t repeatedMask = RepeatedBindingDayMask(bindingDayMask);
    std::int32_t elapsedDays = 0;
    while (dayIndex < 14 && !HasPositiveBindingDayBit(repeatedMask, dayIndex)) {
        elapsedDays = runtime::AddInt32Wrapping(elapsedDays, 1);
        dayIndex = runtime::AddInt32Wrapping(dayIndex, 1);
    }

    const std::int32_t dayCount = afterReset ? runtime::AddInt32Wrapping(elapsedDays, 1) : elapsedDays;
    return BindingDayMinuteResult(dayCount, resetMinuteOfDay, currentMinuteOfDay);
}


namespace {


int Mktime64Low(std::tm& value) {
    const std::time_t converted = std::mktime(&value);
    const std::uint32_t lowBits = static_cast<std::uint32_t>(static_cast<std::int64_t>(converted));
    return runtime::Int32FromBits(lowBits);
}

std::size_t TargetStringLength(const char16_t* value) {
    const char16_t* end = value;
    while (*end != u'\0') {
        ++end;
    }
    return static_cast<std::size_t>(end - value);
}

} // namespace

AgitDecoEffect::AgitDecoEffect() {
    // Current L2Server.exe 0x004af5c8, 26 bytes.
    dExpRestore = 0.0;
    dHpBonus = 1.0;
    dMpBonus = 1.0;
}

void AgitDecoEffect::Init() {
    // Current L2Server.exe 0x004aef94, 23 bytes.
    dExpRestore = 0.0;
    dHpBonus = 1.0;
    dMpBonus = 1.0;
}

bool FVector::operator==(const FVector& vector) const {
    // Current L2NPC.exe 0x0046a52c. The constant is the target's promoted
    // 1.0e-5f value, and every component comparison is strict.
    constexpr double epsilon = 9.999999747378752e-06;
    return std::fabs(x - vector.x) < epsilon && std::fabs(y - vector.y) < epsilon && std::fabs(z - vector.z) < epsilon;
}

bool TimePeriodInfo::ParseTime(const char16_t* time, int& value) {
    // Current L2Server.exe 0x004af07c, 359 bytes.
    L2_GUARD_FUNCTION();
    if (!time || *time == u'\0') {
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    const std::wstring hostTime = target_utf16::ToHostCodeUnits(time);
    std::swscanf(hostTime.c_str(), L"%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second);

    if (year < 1900 || year > 3950 || month < 0 || month >= 13 || day < 0 || day >= 32 || hour < 0 || hour >= 24 ||
        minute < 0 || minute >= 60 || second < 0 || second >= 60) {
        return false;
    }

    // The current body writes only these six target tm dwords before its
    // _mktime64 call. The remaining standard fields keep stack state.
    std::tm temporary;
    temporary.tm_year = year - 1900;
    temporary.tm_mon = month - 1;
    temporary.tm_mday = day;
    temporary.tm_hour = hour;
    temporary.tm_min = minute;
    temporary.tm_sec = second;
    value = Mktime64Low(temporary);
    return true;
}

bool TimePeriodInfo::ParseWeek(const char16_t* week, bool* days, int count) {
    // Current L2Server.exe 0x004af1e4, 270 bytes.
    L2_GUARD_FUNCTION();
    if (!week || *week == u'\0' || !days || count < 7) {
        return false;
    }

    const char16_t* const end = week + TargetStringLength(week);
    bool acceptDigit = true;
    bool parsed = false;
    while (week < end && count > 0) {
        const char16_t current = *week++;
        if (!acceptDigit || current < u'0' || current > u'6') {
            acceptDigit = true;
            continue;
        }

        --count;
        parsed = true;
        days[current - u'0'] = true;
        acceptDigit = false;
    }
    return parsed;
}

bool TimePeriodInfo::ParseHourMin(const char16_t* time, int& value) {
    // Current L2Server.exe 0x004af2f4, 229 bytes.
    L2_GUARD_FUNCTION();
    if (!time || *time == u'\0') {
        return false;
    }

    value = 0;
    int hour = 0;
    int minute = 0;
    const std::wstring hostTime = target_utf16::ToHostCodeUnits(time);
    std::swscanf(hostTime.c_str(), L"%d:%d", &hour, &minute);
    if (static_cast<std::uint32_t>(hour) > 23U || static_cast<std::uint32_t>(minute) > 59U) {
        return false;
    }

    value = (hour * 60 + minute) * 60;
    return true;
}

// NASC64_2015.exe BSS: 0x00ac7de0 and 0x00ac7e60. The definitions originate
// in Shared/BaseStructure.obj. Population belongs to L2Server's
// CParserForFaction, not to the L2NPC process.
std::int64_t* FactionByLevel[16]{};
int FactionMaxLevel[16]{};


bool HeroType_IsHero(const int heroType) {
    // Current L2Server.exe 0x004aefac, 19 bytes.
    // return heroType == 2 || heroType == 4
    return heroType == HERO_ACTIVE || heroType == LEGENDARY_ACTIVE;
}

int LeftTurn(const FVector& first, const FVector& second, const FVector& third) {
    // Current L2Server.exe 0x004aeedc, 94 bytes. Keep the target's strict
    // negative test: zero and unordered (NaN) determinants return -1.
    const double determinant = second.x * third.y + first.x * second.y + third.x * first.y - first.y * second.x -
        third.x * second.y - third.y * first.x;
    return determinant < 0.0 ? 1 : -1;
}

void SwapFVector(FVector& first, FVector& second) {
    // Current L2Server.exe 0x004af804, 62 bytes. The target uses its
    // existing copy constructor and two copy assignments, not move/swap.
    const FVector temporary(first);
    first = second;
    second = temporary;
}

bool IsIntersectionNew(const FVector& first, const FVector& second, const FVector& third, const FVector& fourth) {
    // Current L2Server.exe 0x00c6bea4, 710 bytes. Keep the target's
    // separately evaluated strict determinants: replacing the reversed
    // forms with negation would change rounding at the zero boundary.
    return (((first.x * second.y + second.x * third.y + third.x * first.y - first.y * second.x - third.x * second.y -
                 first.x * third.y) < 0.0 &&
                (fourth.y * first.x + first.y * second.x + fourth.x * second.y - second.y * first.x - fourth.x * first.y -
                    fourth.y * second.x) < 0.0) ||
               ((third.y * first.x + first.y * second.x + second.y * third.x - second.y * first.x - first.y * third.x -
                    third.y * second.x) < 0.0 &&
                   (second.y * first.x + fourth.y * second.x + first.y * fourth.x - first.y * second.x -
                       second.y * fourth.x - fourth.y * first.x) < 0.0)) &&
        (((first.y * fourth.x + fourth.y * third.x + third.y * first.x - third.y * fourth.x - fourth.y * first.x -
              first.y * third.x) < 0.0 &&
             (fourth.x * third.y + third.x * second.y + fourth.y * second.x - fourth.y * third.x - second.x * third.y -
                 fourth.x * second.y) < 0.0) ||
            ((fourth.x * third.y + third.x * first.y + fourth.y * first.x - fourth.y * third.x - first.x * third.y -
                 fourth.x * first.y) < 0.0 &&
                (fourth.x * second.y + fourth.y * third.x + third.y * second.x - third.y * fourth.x - fourth.y * second.x -
                    second.y * third.x) < 0.0));
}

FVector FindIntersectionPoint(FVector first, FVector second, FVector third, FVector fourth) {
    // Current L2Server.exe 0x00c6bd90, 276 bytes.
    first.z = 1.0;
    second.z = 1.0;
    third.z = 1.0;
    fourth.z = 1.0;

    const double fourthThirdX = fourth.z * third.y - fourth.y * third.z;
    const double secondFirstX = second.z * first.y - second.y * first.z;
    const double fourthThirdY = fourth.x * third.z - third.x * fourth.z;
    const double fourthThirdZ = third.x * fourth.y - fourth.x * third.y;
    const double secondFirstY = second.x * first.z - first.x * second.z;
    const double secondFirstZ = first.x * second.y - second.x * first.y;

    FVector result;
    result.x = secondFirstY * fourthThirdZ - secondFirstZ * fourthThirdY;
    result.z = secondFirstX * fourthThirdY - secondFirstY * fourthThirdX;
    result.y = secondFirstZ * fourthThirdX - secondFirstX * fourthThirdZ;
    return result;
}
