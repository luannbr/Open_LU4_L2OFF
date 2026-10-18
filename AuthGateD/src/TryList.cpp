#include "TryList.h"

#include "Shared/src/ScopedLockGuards.h"

#include "AuthGateConfig.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

namespace {

constexpr std::size_t kMaximumEntries = 40000;
constexpr std::int32_t kRecordLifetimeSeconds = 3600;

std::int32_t& TryPoint(TryRecord& record, std::int32_t position) {
    // The exact PDB layout contains tryPoint[63] immediately followed by
    // blockTime, while the machine code uses a 0x3f ring mask. Position 63
    // therefore aliases blockTime in the shipping x86 object.
    if (position == 63) {
        return record.blockTime;
    }
    return record.tryPoint[static_cast<std::size_t>(position)];
}




} // namespace

// AuthGateD.exe 0x00405390, TryList.cpp.
TryList::TryList()
    : m_lock(u"TryList::m_lock", false) {}

// AuthGateD.exe 0x00404140, TryList.cpp.
bool TryList::IsOverTried(
    [[maybe_unused]] const char* userId,
    TryRecord* record) {
    const std::int32_t firstNow = runtime::crt::Time32Now();
    if (record->blockTime <= firstNow) {
        std::int32_t comparisonPosition =
            record->lastPos - config.tryCount + 2;
        if (comparisonPosition < 0) {
            comparisonPosition += 64;
        }

        record->lastPos = (record->lastPos + 1) & 0x3f;
        const std::int32_t attemptTime = runtime::crt::Time32Now();
        TryPoint(*record, record->lastPos) = attemptTime;
        if (TryPoint(*record, comparisonPosition) <=
            runtime::SubtractInt32Wrapping(attemptTime, config.tryInterval)) {
            return false;
        }

        record->blockTime = runtime::AddInt32Wrapping(
            runtime::crt::Time32Now(),
            config.tryBlockInterval);
    }
    return true;
}

// AuthGateD.exe 0x004050E0, TryList.cpp.
bool TryList::IsBlocked(const char* userId) {
    if (config.tryCount < 2) {
        return false;
    }

    // The target makes this first time() call before entering its lock and
    // discards the result.
    [[maybe_unused]] const std::int32_t discardedTime = runtime::crt::Time32Now();
    bool blocked = false;
    {
        L2_GUARD(m_lock);

        if (m_table.size() > kMaximumEntries) {
            m_table.clear();
        }

        const auto found = m_table.find(userId);
        if (found == m_table.end()) {
            TryRecord record;
            record.lastPos = 1;
            record.tryPoint[1] = runtime::crt::Time32Now();
            m_table.emplace(userId, record);
        } else {
            TryRecord& record = found->second;
            const std::int32_t now = runtime::crt::Time32Now();
            if (runtime::SubtractInt32Wrapping(
                    now,
                    TryPoint(record, record.lastPos)) <=
                kRecordLifetimeSeconds) {
                blocked = IsOverTried(userId, &record);
            } else {
                m_table.erase(found);
            }
        }

    }
    return blocked;
}
