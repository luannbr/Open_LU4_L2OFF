#include "DeferredJob.h"
#include "Pledge.h"
#include "Shared/src/UserConst.h"
#include "User.h"

#include "Shared/src/Config.h"
#include "Shared/src/Runtime/RawAtomic.h"

#include <cstdint>
#include <utility>

CDeferredJob g_DeferredJob;

CDeferredJob::CDeferredJob() {}

CDeferredJob::~CDeferredJob() = default;

void CDeferredJob::DoDeferredJob(User* user, const DeferredJobInfo& job) {
    L2_GUARD_FUNCTION();

    if (job.nDeferredJob != 0) {
        return;
    }

    CPledge* const pledge = CPledge::GetObjectW(static_cast<std::uint32_t>(job.nPledgeServerId));
    if (pledge == nullptr) {
        return;
    }

    pledge->RemovePledgeSkillsFrom(user);
    pledge->DeleteDbMember(user->GetDBID());
    pledge->DeleteMember(user);
    pledge->CloseMemberWindowPacket(user);
    pledge->ValidateTotalLevel();

    if (pledge->IsSubMaster(user->GetDBID())) {
        pledge->SendSubPledgeUpdatedPacket(pledge->GetSubMasterPledgeType(user->GetDBID()));
    }

    if (user->m_data->m_nPledgeID == pledge->GetDbId()) {
        user->SetPledge(nullptr);
    }
    user->ParamChanged(PCB_PLEDGE | PCB_USER);
    runtime::raw_atomic::CompareExchangeI32(&user->m_nRelationChanged, 1, 0);
    user->CloseAllPledgeWindowPacket();
}

void CDeferredJob::DoEnterWorldJob(User* user) {
    L2_GUARD_FUNCTION();

    const int userDatabaseId = static_cast<int>(user->GetDBID());
    {
        L2_WRITE_GUARD(m_lock);
        const auto range = m_DeferredJobMultiMap.equal_range(userDatabaseId);
        for (auto position = range.first; position != range.second; ++position) {
            DoDeferredJob(user, position->second);
        }
        m_DeferredJobMultiMap.erase(range.first, range.second);
    }
}

void CDeferredJob::DoTimerJob() {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_lock);
        for (const int userDatabaseId : m_UserDBIdList) {
            CSPointer<User> user = g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, userDatabaseId));
            if (!user) {
                continue;
            }

            const auto range = m_DeferredJobMultiMap.equal_range(userDatabaseId);
            for (auto position = range.first; position != range.second; ++position) {
                DoDeferredJob(user.get(), position->second);
            }
            m_DeferredJobMultiMap.erase(range.first, range.second);
        }
        m_UserDBIdList.clear();
    }
}

void CDeferredJob::ReserveDeferredJob(const int userDatabaseId, const int deferredJobType, const int pledgeServerId) {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_lock);
        m_DeferredJobMultiMap.emplace(userDatabaseId, DeferredJobInfo(deferredJobType, pledgeServerId));
    }
}

void CDeferredJob::ReserveNextDoer(User* user) {
    L2_GUARD_FUNCTION();

    {
        L2_WRITE_GUARD(m_lock);
        m_UserDBIdList.push_back(static_cast<int>(user->GetDBID()));
    }
}
