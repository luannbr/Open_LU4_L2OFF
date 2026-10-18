#pragma once

#include "Shared/src/PledgeMasteryID.h"
#include "Shared/src/RWLock.h"

#include <cstddef>
#include <cstdint>


class PledgeMasteryInCache {
public:
    explicit PledgeMasteryInCache(std::int32_t pledgeId);
    ~PledgeMasteryInCache();

    bool LoadFromDB();
    bool SetMasteryPoint(PledgeMasteryID::Type masteryId, std::int32_t point);
    bool ResetMasteryPoint();
    std::int32_t GetMasteryPoint(PledgeMasteryID::Type masteryId) const;
    void GetPoints(std::int32_t (&points)[PledgeMasteryID::MAX_COUNT]) const;

private:

    // Semantic name for the unnamed current helper at 0x005202b8.
    static bool IsValidMasteryId(PledgeMasteryID::Type masteryId) noexcept;

    mutable RWLock m_lock; // +0x00
    std::int32_t m_pledgeId; // +0x38
    std::int32_t m_points[PledgeMasteryID::MAX_COUNT]; // +0x3c
};
