#pragma once

namespace protocol {
class RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO;
class RP_EX_PLEDGE_CONTRIBUTION_RANK;
} // namespace protocol

// The current symbols and PledgeContributionInCache.cpp literals identify a
// namespace owner: every recovered entry is a free __cdecl function and no
// object construction or `this` argument exists in Cached.exe.
namespace PledgeContributionInCache {

bool AddInfo(int pledgeId, int characterId);
bool AddPoint(int pledgeId, int characterId, int point);
bool DeleteInfo(int characterId);

// Semantic recovery name: current 0x0050ca00 has no surviving guard or PDB
// identifier. Its namespace owner and one-pledge delete-all contract are
// fixed by adjacent functions and the exact SQL call below.
bool DeleteAllInfo(int pledgeId);

bool RetrieveInfo(int pledgeId, int characterId, protocol::RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO& response);

bool RetrieveRankInfo(int pledgeId, protocol::RP_EX_PLEDGE_CONTRIBUTION_RANK& response);
bool RetrievePrevRankInfo(int pledgeId, protocol::RP_EX_PLEDGE_CONTRIBUTION_RANK& response);

bool TakePeriodReward(int pledgeId, int characterId, int& result, int& rank, int& ratio, int& activeMember);

} // namespace PledgeContributionInCache
