#include "PledgeRaidInfo.h"

ChangeBodyStruct::ChangeBodyStruct() : TransformId(0), Prob(0) {}

PledgeRaidRankRewardInfo::PledgeRaidRankRewardInfo() : nRank_(0), nNameValue_(0), nBonusPercentage_(0) {}

PledgeRaidTierRewardInfo::PledgeRaidTierRewardInfo() :
    nTier_(0),
    // Current 009c5e38 leaves this parser-owned lane untouched.
    nNameValue_(0),
    nBonusPercentage_(0) {}

PledgeRaidDB::PledgeRaidDB() {}

PledgeRaidDB::~PledgeRaidDB() = default;
