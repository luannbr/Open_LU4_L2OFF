#pragma once

#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

class User;

struct DeferredJobInfo {
    DeferredJobInfo(int nDeferredJob, int nPledgeServerId) {
        this->nDeferredJob = nDeferredJob;
        this->nPledgeServerId = nPledgeServerId;
    }

    int nDeferredJob; // +0x00
    int nPledgeServerId; // +0x04
};

static_assert(std::is_trivially_copyable_v<DeferredJobInfo>);
static_assert(std::is_trivially_destructible_v<DeferredJobInfo>);
static_assert(!std::is_default_constructible_v<DeferredJobInfo>);
static_assert(std::is_constructible_v<DeferredJobInfo, int, int>);


// Exact current DeferredJob.cpp owner. The 2018 image uses the 0x20
// multimap carrier and the debug-capable 0x20 vector carrier. The older
// TotalChecker vector/map extents are not layout-compatible here.
class CDeferredJob final {
public:
    CDeferredJob();
    ~CDeferredJob();

    CDeferredJob(const CDeferredJob&) = delete;
    CDeferredJob& operator=(const CDeferredJob&) = delete;

    void DoEnterWorldJob(User* user);
    void DoTimerJob();
    void ReserveDeferredJob(int userDatabaseId, int deferredJobType, int pledgeServerId);
    void ReserveNextDoer(User* user);

private:
    void DoDeferredJob(User* user, const DeferredJobInfo& job);

    RWLock m_lock; // +0x00
    std::multimap<int, DeferredJobInfo> m_DeferredJobMultiMap; // +0x38
    std::vector<int> m_UserDBIdList; // +0x58
};


extern CDeferredJob g_DeferredJob;
