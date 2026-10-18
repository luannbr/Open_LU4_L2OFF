#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"

#include <cstddef>
#include <cstdint>

struct ChangeBodyStruct {
    ChangeBodyStruct();

    std::int32_t TransformId;
    std::int32_t Prob;
};


// Current PledgeRaidInfo.cpp value records. The target uses the 0x20-byte
// MSVC map owner in both records. Rank has three integer fields and the
// natural four-byte padding before the map. Tier has four integer fields.
struct PledgeRaidRankRewardInfo {
    PledgeRaidRankRewardInfo();

    std::int32_t nRank_; // +0x00
    std::int32_t nNameValue_; // +0x04
    std::int32_t nBonusPercentage_; // +0x08
    std::map<int, int> mItemMap_; // +0x10
};

struct PledgeRaidTierRewardInfo {
    PledgeRaidTierRewardInfo();

    std::int32_t nTier_; // +0x00
    std::int32_t nInPercentage_; // +0x04
    std::int32_t nNameValue_; // +0x08
    std::int32_t nBonusPercentage_; // +0x0c
    std::map<int, int> mItemMap_; // +0x10
};

struct PledgeRaidMasteryRewardInfo {
    std::int32_t nMasteryId_;
    std::int32_t nPoint_;
};

// Exact current 0x88 Singleton owner. Setting.cpp is the original producer
// of these public configuration carriers, so their target names and offsets
// stay visible instead of being hidden behind a recovery-only interface.
class PledgeRaidDB : public Singleton<PledgeRaidDB> {
public:

    PledgeRaidDB();
    ~PledgeRaidDB() override;

    std::map<int, PledgeRaidRankRewardInfo> mRankRewardInfo_; // +0x08
    std::map<int, PledgeRaidTierRewardInfo> mTierRewardInfo_; // +0x28
    std::vector<PledgeRaidMasteryRewardInfo> vMasteryRewardInfo_; // +0x48
    std::u16string strHuntNpcDbName_; // +0x68
};
