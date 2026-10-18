#pragma once

#include <cstddef>
#include <cstdint>

// L2Core/inc/guardprofile.h. The current server executables contain this
// 10,000-entry carrier. Current L2NPC.exe keeps the reporting/reset
// infrastructure but was built as the "No guard Profile version": exact
// xrefs to its _guardIndexSeed at 0x0293e640 are the four reads in
// ReportGuardProfile/ResetGuardProfile, with no registration write, and the
// profile array at 0x028dcbc0 is reached only by those two owners. Preserve
// that disabled shipping configuration by default. TotalChecker2015.pdb does,
// however, keep the active build's constructor/destructor, mail-report,
// TLS-stack and field declarations. L2_ENABLE_GUARD_PROFILING opts into a
// portable implementation of that declared configuration. It is an explicit
// extension and is OFF unless requested at configure time.
struct GuardProfileInfo {
    std::int64_t nTotalCallCount = 0;
    std::int64_t nTotalClock = 0;
    std::int64_t nAccTotalClock = 0;
    const wchar_t* pszGuardName = nullptr;
    std::int32_t index = 0;
    bool bMacro = false;
    std::uint8_t reserved[3]{};

    void Reset() noexcept;
    void ReportByMail(void* file) const;
};


class GuardProfileHelper {
public:
    static constexpr std::size_t kProfileCapacity = 10000;
    static constexpr std::size_t kMaximumDepth = 1000;

    GuardProfileHelper(
        std::int32_t guardIndex,
        const wchar_t* guardName,
        bool macro) noexcept;
    ~GuardProfileHelper() noexcept;

    GuardProfileHelper(const GuardProfileHelper&) = delete;
    GuardProfileHelper& operator=(const GuardProfileHelper&) = delete;

    static std::int32_t RegisterGuard(
        const wchar_t* guardName,
        bool macro) noexcept;

    static void InitializeGuardProfilePerThread();
    static void ResetGuardProfile();
    static void ReportGuardProfileByMail(void* file);
    static std::int32_t RegisteredProfileCount() noexcept;
    static GuardProfileInfo Snapshot(std::int32_t index) noexcept;

    static GuardProfileInfo _guardProfile[kProfileCapacity];
    static std::int32_t _guardIndexSeed;

protected:
    // Exact donor-declared instance layout. The active constructor is used
    // only by an profiling-enabled portable build.
    std::int32_t _guardIndex = 0;
    std::int32_t reserved_ = 0;
    std::int64_t _startTime = 0;

    static thread_local std::int64_t* _tlGuardProfileClock;
    static thread_local std::int32_t _callStackDepth;

private:
    static thread_local std::int64_t
        _tlGuardProfileClockStorage[kMaximumDepth];
};
