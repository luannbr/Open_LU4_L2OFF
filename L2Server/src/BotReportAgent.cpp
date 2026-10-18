#include "BotReportAgent.h"
#include "Shared/src/MemorySub.h"

#include "CConfig.h"
#include "DB.h"
#include "User.h"

#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>

namespace {


constexpr std::array<int, 3> kBotRestrainCount = {
    1,
    1,
    1,
};

constexpr std::array<bool, 3> kBotCanBeNull = {
    true,
    false,
    false,
};

} // namespace

static_assert(
    //     L2ServerMemory::kCBotReportAgentClassId == 72)
    static_cast<std::int32_t>(ShippingMemoryClassId::CBotReportAgent) == 72
);

PointInfo::PointInfo(
    const std::uint32_t accountId, const std::int32_t point, const std::int32_t reportedTime, const bool ignoreDelay
) noexcept :
    m_nAccountID(accountId),
    m_nPoint(point),
    m_tReportedTime(reportedTime),
    m_bIgnoreDelay(ignoreDelay),
    m_alignment000dTo0010{} {}

CBotReportAgent::CBotReportAgent() :
    m_nDate(0), m_nSentReduceDate(0) {
    L2_GUARD_FUNCTION();
}

CBotReportAgent::~CBotReportAgent() {
    // Current L2Server 0x004c4860. Member and base teardown follows this
    // empty guarded body through ordinary generated destruction.
    L2_GUARD_FUNCTION();
}

L2_DEFINE_OBJECT_CLASS_ID(CBotReportAgent)

L2_DEFINE_OBJECT_CLASS_NAME(CBotReportAgent, m_pClassName_CBotReportAgent)

L2_DEFINE_OBJECT_ALLOCATOR(CBotReportAgent, m_pClassName_CBotReportAgent)

L2_DEFINE_OBJECT_NEW(CBotReportAgent)

L2_DEFINE_OBJECT_RELEASE(CBotReportAgent)

CBotReportAgent* CBotReportAgent::Inst() {
    static CBotReportAgent instance;
    return &instance;
}

void CBotReportAgent::Init() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_RestrainLock);
        m_lstRestrainInfo.clear();
    }
    const std::int32_t now = runtime::crt::Time32Now();
    m_nDate = GetYYYYMMDDFromTime(now);
    m_nSentReduceDate = m_nDate;
    AddTimer(60000, 0);
}

int CBotReportAgent::CheckRestrainAndUpdate(
    const std::uint32_t accountId,
    const std::uint32_t address,
    const std::int32_t pledgeId,
    const std::int32_t botCharacterDatabaseId
) {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();

    {
        L2_GUARD(m_PointLock);
        for (const PointInfo& point : m_lstPointInfo) {
            if (point.m_nAccountID != accountId) {
                continue;
            }
            if (point.m_nPoint > 6) {
                return BCRT_FAILED_BY_NOT_ENOUGH_POINT;
            }
            if (now - g_CaptchaSetting.reportReuseDelaySeconds < point.m_tReportedTime && !point.m_bIgnoreDelay) {
                return BCRT_FAILED_BY_REUSE_DELAY;
            }
        }
    }

    const std::array<std::uint32_t, 3> keys = {
        static_cast<std::uint32_t>(pledgeId),
        accountId,
        address,
    };
    int result = BCRT_OK;
    {
        L2_GUARD(m_RestrainLock);
        auto found = std::find_if(
            m_lstRestrainInfo.begin(), m_lstRestrainInfo.end(), [botCharacterDatabaseId](const RestrainInfo& info) {
                return info.m_nBotCharDBID == static_cast<std::uint32_t>(botCharacterDatabaseId);
            }
        );
        if (found == m_lstRestrainInfo.end()) {
            RestrainInfo info;
            info.m_nBotCharDBID = static_cast<std::uint32_t>(botCharacterDatabaseId);
            for (std::size_t index = 0; index < keys.size(); ++index) {
                if (!kBotCanBeNull[index] || keys[index] != 0) {
                    info.m_lstRestrain[index].push_back({keys[index], 1});
                }
            }
            m_lstRestrainInfo.push_back(info);
        } else {
            for (std::size_t index = 0; index < keys.size(); ++index) {
                if (kBotCanBeNull[index] && keys[index] == 0) {
                    continue;
                }
                auto& restraint = found->m_lstRestrain[index];
                auto entry = std::find_if(
                    restraint.begin(), restraint.end(),
                    [key = keys[index]](const RestrainInfo::RestrainEntry& candidate) { return candidate.first == key; }
                );
                if (entry == restraint.end()) {
                    restraint.push_back({keys[index], 1});
                } else if (entry->second < kBotRestrainCount[index]) {
                    ++entry->second;
                } else {
                    result = static_cast<int>(index) + 1;
                }
            }
        }
    }

    if (result != BCRT_OK) {
        return result;
    }

    {
        L2_GUARD(m_PointLock);
        bool found = false;
        for (PointInfo& point : m_lstPointInfo) {
            if (point.m_nAccountID == accountId) {
                found = true;
                ++point.m_nPoint;
                point.m_tReportedTime = now;
            }
        }
        if (!found) {
            m_lstPointInfo.emplace_back(accountId, 1, now, false);
        }
    }
    return BCRT_OK;
}

void CBotReportAgent::Report(
    User* reporter,
    User* target,
    char16_t* reporterAddress,
    char16_t* reporterPledge,
    char16_t* targetAddress,
    char16_t* targetPledge
) {
    L2_GUARD_FUNCTION();
    if (reporter == nullptr || target == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Bot or User doesn't exist.")
        // L2_LOG_ERROR(Log,
        //     L"Bot or User doesn't exist.")
        L2_LOG_WARN(Log, "Cannot record bot report: reporter or target is unavailable; the report is skipped.");
        return;
    }

    CLogFormatter formatter;
    formatter.WriteLog1(
        0x1f67, reporter, target, reporterAddress, reporterPledge, nullptr, GetAvailablePoint(reporter->GetAccountID()),
        reporter->m_nBotReportPoint, 0, 0, 0, 0, 0
    );
    formatter.WriteLog1(
        0x1f68, target, reporter, targetAddress, targetPledge, nullptr, GetAvailablePoint(target->GetAccountID()),
        target->m_nBotReportPoint, 0, 0, 0, 0, 0
    );
    db.RequestSaveBotReport(target);
    ++target->m_nBotReportPoint;
}

void CBotReportAgent::ResetAll() {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_RestrainLock);
    m_lstRestrainInfo.clear();
}

void CBotReportAgent::SetAvailablePointAndReuseDelay(const std::uint32_t accountId, const int amount) {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    L2_GUARD(m_PointLock);
    bool found = false;
    for (PointInfo& point : m_lstPointInfo) {
        if (point.m_nAccountID == accountId) {
            point.m_bIgnoreDelay = true;
            point.m_nPoint -= amount;
            found = true;
        }
    }
    if (!found) {
        m_lstPointInfo.emplace_back(accountId, 1 - amount, now, true);
    }
}

int CBotReportAgent::GetAvailablePoint(const std::uint32_t accountId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_PointLock);
    for (const PointInfo& point : m_lstPointInfo) {
        if (point.m_nAccountID == accountId) {
            return 7 - point.m_nPoint;
        }
    }
    return 7;
}

int CBotReportAgent::GetRemainDelay(const std::uint32_t accountId) {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    int remain = g_CaptchaSetting.reportReuseDelaySeconds;
    L2_GUARD(m_PointLock);
    for (const PointInfo& point : m_lstPointInfo) {
        if (point.m_nAccountID == accountId) {
            remain += point.m_tReportedTime - now;
            break;
        }
    }
    return std::max(remain, 0);
}

void CBotReportAgent::SetStopBotReport(const std::uint32_t accountId, const int seconds) {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    const std::int32_t reportedTime = now - g_CaptchaSetting.reportReuseDelaySeconds + seconds;
    L2_GUARD(m_PointLock);
    for (PointInfo& point : m_lstPointInfo) {
        if (point.m_nAccountID == accountId) {
            point.m_tReportedTime = reportedTime;
            return;
        }
    }
    m_lstPointInfo.emplace_back(accountId, 0, reportedTime, false);
}

void CBotReportAgent::TimerExpired(int id [[maybe_unused]]) {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    const int date = GetYYYYMMDDFromTime(now);
    {
        L2_GUARD(m_RestrainLock);
        L2_GUARD(m_PointLock);
        if (m_nDate < date) {
            m_nDate = date;
            m_lstRestrainInfo.clear();
            m_lstPointInfo.clear();
        }
    }
    AddTimer(60000, 0);
}
