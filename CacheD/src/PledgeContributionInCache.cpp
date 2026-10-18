#include "Shared/src/Log.h"

#include "PledgeContributionInCache.h"

#include "PledgeDb.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/proto/DBPacket.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <array>

namespace {

bool RetrieveUserContribution(int pledgeId, int characterId, protocol::RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO& response) {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&response.Info.nTotal);
    connection.Bind(&response.Info.nPeriod);
    connection.Bind(&response.nRewardedStep);
    connection.Bind(&response.nPeriodReward);
    constexpr const wchar_t* statement = L"{Call lin_PledgeContributionMemberGetInfo (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&characterId);
    return connection.ExecutePrepared(statement) && connection.Fetch();
}

bool ResetUserContribution(int pledgeId, int characterId, int periodPoint, int totalPoint) {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_PledgeContributionMemberResetInfo (?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&characterId);
    connection.BindParameter(&periodPoint);
    connection.BindParameter(&totalPoint);
    return connection.ExecutePrepared(statement);
}

bool RetrievePeriodContributors(int pledgeId, protocol::RP_EX_PLEDGE_CONTRIBUTION_RANK& response) {
    // Current Cached.exe 0x0050c480.
    L2_GUARD_FUNCTION();

    protocol::PkPledgeContributionMemberInfo row{};
    std::array<char16_t, 25> name{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(name.data(), 0x32);
    connection.Bind(&row.nPledgeType);
    connection.Bind(&row.nPeriod);
    connection.Bind(&row.nTotal);
    constexpr const wchar_t* statement = L"{Call lin_PledgeContributionMemberGetPeriodRankers (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    if (!connection.ExecutePrepared(statement))
        return false;
    while (connection.Fetch()) {
        row.nRank = 0;
        row.szName.fill(u'\0');
        const auto text = FixedUtf16View(name);
        std::copy(text.begin(), text.end(), row.szName.begin());
        response.vMembers.push_back(row);
    }
    return true;
}

bool RetrievePrevPeriodContributors(int pledgeId, protocol::RP_EX_PLEDGE_CONTRIBUTION_RANK& response) {
    // Current Cached.exe 0x0050c65c.
    L2_GUARD_FUNCTION();

    protocol::PkPledgeContributionMemberInfo row{};
    std::array<char16_t, 25> name{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&row.nRank);
    connection.Bind(name.data(), 0x32);
    connection.Bind(&row.nPledgeType);
    connection.Bind(&row.nPeriod);
    connection.Bind(&row.nTotal);
    constexpr const wchar_t* statement = L"{Call lin_PledgeContributionMemberGetPrevPeriodRankers (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    if (!connection.ExecutePrepared(statement))
        return false;
    while (connection.Fetch()) {
        row.szName.fill(u'\0');
        const auto text = FixedUtf16View(name);
        std::copy(text.begin(), text.end(), row.szName.begin());
        response.vMembers.push_back(row);
    }
    return true;
}

} // namespace

bool PledgeContributionInCache::AddInfo(int pledgeId, int characterId) {
    L2_GUARD_FUNCTION();
    // Current 0x0050c840 tail-keeps ResetUserContribution's AL result.
    return ResetUserContribution(pledgeId, characterId, 0, 0);
}

bool PledgeContributionInCache::AddPoint(int pledgeId, int characterId, int point) {
    // Current Cached.exe 0x0050cf24 validates the pledge through the live
    // CPledgeDb before touching persistence.
    L2_GUARD_FUNCTION();
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge)
        return false;

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_PledgeContributionMemberAddPoint (?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&characterId);
    connection.BindParameter(&point);
    return connection.ExecutePrepared(statement);
}

bool PledgeContributionInCache::DeleteInfo(int characterId) {
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_PledgeContributionMemberDeleteInfo (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&characterId);
    return connection.ExecutePrepared(statement);
}

bool PledgeContributionInCache::DeleteAllInfo(int pledgeId) {
    // Current Cached.exe 0x0050ca00 has no guard-stack entry.
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_PledgeContributionMemberDeleteAllInfo (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    return connection.ExecutePrepared(statement);
}

bool PledgeContributionInCache::RetrieveInfo(
    int pledgeId, int characterId, protocol::RP_EX_PLEDGE_CONTRIBUTION_MEMBER_INFO& response
) {
    // Current Cached.exe 0x0050caac. The pledge lookup is a required live
    // ownership check. A missing DB row is repaired to the target's all-zero
    // contribution state before the request is reported successful.
    L2_GUARD_FUNCTION();
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge)
        return false;

    if (!RetrieveUserContribution(pledgeId, characterId, response)) {
        response.Info.nPeriod = 0;
        response.Info.nTotal = 0;
        response.nRewardedStep = 0;
        if (!ResetUserContribution(pledgeId, characterId, 0, 0)) {
            return false;
        }
    }
    return true;
}

bool PledgeContributionInCache::RetrieveRankInfo(int pledgeId, protocol::RP_EX_PLEDGE_CONTRIBUTION_RANK& response) {
    // Current Cached.exe 0x0050cc0c.
    L2_GUARD_FUNCTION();
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        // Log.Add(
        //     LOG_ERROR, "[PLEDGE][%s][%d]No Pledge: %d", "PledgeContributionInCache.cpp", 0xaf, pledgeId
        // )
        // Log.Add(LOG_ERROR,
        //     "[clan contribution] Cannot load current-period rankings: clan %d is unavailable in database %d.",
        //     pledgeId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[clan contribution] Cannot load current-period rankings: clan %d is unavailable in database %d.",
            pledgeId, runtime::process::CurrentDatabaseId());
        return false;
    }
    return RetrievePeriodContributors(pledgeId, response);
}

bool PledgeContributionInCache::RetrievePrevRankInfo(int pledgeId, protocol::RP_EX_PLEDGE_CONTRIBUTION_RANK& response) {
    // Current Cached.exe 0x0050cd98.
    L2_GUARD_FUNCTION();
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        // Log.Add(
        //     LOG_ERROR, "[PLEDGE][%s][%d]No Pledge: %d", "PledgeContributionInCache.cpp", 0xc6, pledgeId
        // )
        // Log.Add(LOG_ERROR,
        //     "[clan contribution] Cannot load previous-period rankings: clan %d is unavailable in database %d.",
        //     pledgeId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[clan contribution] Cannot load previous-period rankings: clan %d is unavailable in database %d.",
            pledgeId, runtime::process::CurrentDatabaseId());
        return false;
    }
    return RetrievePrevPeriodContributors(pledgeId, response);
}

bool PledgeContributionInCache::TakePeriodReward(
    int pledgeId, int characterId, int& result, int& rank, int& ratio, int& activeMember
) {
    // Current Cached.exe 0x0050d0d4 returns true only when the procedure
    // executes, yields one row and its first result column is nonzero.
    L2_GUARD_FUNCTION();
    DBConn connection(runtime::process::CurrentDatabaseId());
    int accepted = 0;
    connection.Bind(&accepted);
    connection.Bind(&result);
    connection.Bind(&rank);
    connection.Bind(&ratio);
    connection.Bind(&activeMember);
    constexpr const wchar_t* statement = L"{Call lin_PledgeContributionMemberTakePeriodReward (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement))
        return false;
    return connection.Fetch() && accepted != 0;
}
