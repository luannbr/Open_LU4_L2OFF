#include "L2Core/src/inc/GuardProfile.h"

#include "L2Core/src/inc/FileUtils.h"

#include <chrono>
#include <mutex>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace {

std::mutex g_guardRegistrationLock;

std::int64_t CurrentProfileClock() noexcept {
    // The active macro body is absent from both shipping binaries available
    // here. A monotonic nanosecond clock is the portable replacement for the
    // target's declared 64-bit clock carrier.
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::int64_t AtomicLoad(const std::int64_t* value) noexcept {
#if defined(_MSC_VER)
    return static_cast<std::int64_t>(
        _InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(const_cast<std::int64_t*>(value)), 0, 0)
    );
#else
    return __atomic_load_n(value, __ATOMIC_RELAXED);
#endif
}

void AtomicStore(std::int64_t* destination, std::int64_t value) noexcept {
#if defined(_MSC_VER)
    _InterlockedExchange64(reinterpret_cast<volatile long long*>(destination), value);
#else
    __atomic_store_n(destination, value, __ATOMIC_RELAXED);
#endif
}

void AtomicAdd(std::int64_t* destination, std::int64_t value) noexcept {
#if defined(_MSC_VER)
    _InterlockedExchangeAdd64(reinterpret_cast<volatile long long*>(destination), value);
#else
    __atomic_fetch_add(destination, value, __ATOMIC_RELAXED);
#endif
}

std::int32_t AtomicLoad(const std::int32_t* value) noexcept {
#if defined(_MSC_VER)
    return static_cast<std::int32_t>(
        _InterlockedCompareExchange(reinterpret_cast<volatile long*>(const_cast<std::int32_t*>(value)), 0, 0)
    );
#else
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
#endif
}

void AtomicStore(std::int32_t* destination, std::int32_t value) noexcept {
#if defined(_MSC_VER)
    _InterlockedExchange(reinterpret_cast<volatile long*>(destination), value);
#else
    __atomic_store_n(destination, value, __ATOMIC_RELEASE);
#endif
}

} // namespace

GuardProfileInfo GuardProfileHelper::_guardProfile[GuardProfileHelper::kProfileCapacity]{};
std::int32_t GuardProfileHelper::_guardIndexSeed = 0;

thread_local std::int64_t GuardProfileHelper::_tlGuardProfileClockStorage[GuardProfileHelper::kMaximumDepth]{};
thread_local std::int64_t* GuardProfileHelper::_tlGuardProfileClock = GuardProfileHelper::_tlGuardProfileClockStorage;
thread_local std::int32_t GuardProfileHelper::_callStackDepth = 0;

void GuardProfileInfo::Reset() noexcept {
    AtomicStore(&nTotalCallCount, 0);
    AtomicStore(&nTotalClock, 0);
    AtomicStore(&nAccTotalClock, 0);
}

void GuardProfileInfo::ReportByMail(void* file) const {
    const std::int64_t callCount = AtomicLoad(&nTotalCallCount);
    if (callCount <= 0) {
        return;
    }
    const std::int64_t totalClock = AtomicLoad(&nTotalClock);
    const std::int64_t accumulatedClock = AtomicLoad(&nAccTotalClock);
    hprintf(
        file, L"\t %I64d \t %I64d \t %I64d \t %I64d \t %s\r\n", totalClock, callCount, totalClock / callCount,
        accumulatedClock, pszGuardName ? pszGuardName : L""
    );
}

GuardProfileHelper::GuardProfileHelper(std::int32_t guardIndex, const wchar_t*, bool) noexcept : _guardIndex(guardIndex) {
    if (_guardIndex < 0 || _guardIndex >= RegisteredProfileCount() || _callStackDepth < 0 ||
        _callStackDepth >= static_cast<std::int32_t>(kMaximumDepth)) {
        _guardIndex = -1;
        return;
    }

    _startTime = CurrentProfileClock();
    _tlGuardProfileClock[_callStackDepth] = _startTime;
    ++_callStackDepth;
}

GuardProfileHelper::~GuardProfileHelper() noexcept {
    if (_guardIndex < 0 || _callStackDepth <= 0) {
        return;
    }

    const std::int64_t endTime = CurrentProfileClock();
    --_callStackDepth;
    const std::int64_t accumulatedStart = _tlGuardProfileClock[_callStackDepth];
    const std::int64_t inclusiveClock = endTime >= _startTime ? endTime - _startTime : 0;
    const std::int64_t exclusiveClock = endTime >= accumulatedStart ? endTime - accumulatedStart : 0;

    if (_callStackDepth > 0) {
        // Moving the parent's effective start forward by the complete child
        // duration leaves its final nTotalClock exclusive of lower guards.
        _tlGuardProfileClock[_callStackDepth - 1] += inclusiveClock;
    }

    GuardProfileInfo& profile = _guardProfile[_guardIndex];
    AtomicAdd(&profile.nTotalCallCount, 1);
    AtomicAdd(&profile.nTotalClock, exclusiveClock);
    AtomicAdd(&profile.nAccTotalClock, inclusiveClock);
}

std::int32_t GuardProfileHelper::RegisterGuard(const wchar_t* guardName, bool macro) noexcept {
    std::lock_guard<std::mutex> lock(g_guardRegistrationLock);
    const std::int32_t index = AtomicLoad(&_guardIndexSeed);
    if (index < 0 || index >= static_cast<std::int32_t>(kProfileCapacity)) {
        return -1;
    }

    GuardProfileInfo& profile = _guardProfile[index];
    profile.Reset();
    profile.pszGuardName = guardName;
    profile.index = index;
    profile.bMacro = macro;
    AtomicStore(&_guardIndexSeed, index + 1);
    return index;
}

void GuardProfileHelper::InitializeGuardProfilePerThread() {
    _callStackDepth = 0;
    _tlGuardProfileClock = _tlGuardProfileClockStorage;
}

void GuardProfileHelper::ResetGuardProfile() {
    const std::int32_t count = RegisteredProfileCount();
    for (std::int32_t index = 0; index < count; ++index) {
        _guardProfile[index].Reset();
    }
}

void GuardProfileHelper::ReportGuardProfileByMail(void* file) {
    hprintf(
        file,
        L"\t Only in guard \t Count \t only guard / count "
        L"\t with lower guard \t GuardName\r\n"
    );
    const std::int32_t count = RegisteredProfileCount();
    for (std::int32_t index = 0; index < count; ++index) {
        Snapshot(index).ReportByMail(file);
    }
}

std::int32_t GuardProfileHelper::RegisteredProfileCount() noexcept { return AtomicLoad(&_guardIndexSeed); }

GuardProfileInfo GuardProfileHelper::Snapshot(std::int32_t index) noexcept {
    GuardProfileInfo result;
    if (index < 0 || index >= RegisteredProfileCount()) {
        return result;
    }

    const GuardProfileInfo& profile = _guardProfile[index];
    result.nTotalCallCount = AtomicLoad(&profile.nTotalCallCount);
    result.nTotalClock = AtomicLoad(&profile.nTotalClock);
    result.nAccTotalClock = AtomicLoad(&profile.nAccTotalClock);
    result.pszGuardName = profile.pszGuardName;
    result.index = profile.index;
    result.bMacro = profile.bMacro;
    return result;
}
