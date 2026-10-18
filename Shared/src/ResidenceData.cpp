#include "Shared/src/ResidenceData.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/CenteredSharedRWLock.h"
#include "Shared/src/SharedType.h"

#include <cstring>
#include <ctime>

CSharedRWLock g_ResidenceLock(L"L2ResidenceSMLock", 0, nullptr, 4000);

int GetMaxFortressOwnSeconds() { return 0x93a80; }

int GetMaxFortressRewardSeconds() {
    // Current L2Server 0x00a39a3c.
    return 0x91050;
}

CResidenceData& CResidenceData::Inst(int residenceId) {
    // Current L2NPC.exe 0x0050bf00 and the PDB both place g_ResidenceData
    // here as a function-local static.
    static CSharedVector<CResidenceData, 16, 2, 8> g_ResidenceData(L"L2ResidenceSMData");
    return g_ResidenceData[residenceId];
}

void CResidenceData::InitializeData() {
    // Current L2Server 0x00a39a44 resets only this subset.
    m_ManorIdList.fill(0);
    m_nRelatedFortress.fill(-1);
    m_nFortressParentCastleId = -1;
    m_nFortressCastleTreasureLevel = -1;
    m_nFortressOwnerRewardCycleCount = -1;
    m_nFortressRent = 125000;
}

bool CResidenceData::IsDefendingPledge(int pledgeId, int worldId) const {
    L2_GUARD_FUNCTION();
    for (int index = 0; index < m_DefendingPledgeListSize; ++index) {
        const PledgeInfo& pledge = m_stDefendingPledgeList[static_cast<std::size_t>(index)];
        if (pledge.m_nPledgeID == pledgeId && pledge.m_nWorldID == worldId) {
            return true;
        }
    }
    return false;
}

bool CResidenceData::IsOffendingPledge(int pledgeId, int worldId) const {
    L2_GUARD_FUNCTION();
    for (int index = 0; index < m_OffendingPledgeListSize; ++index) {
        const PledgeInfo& pledge = m_stOffendingPledgeList[static_cast<std::size_t>(index)];
        if (pledge.m_nPledgeID == pledgeId && pledge.m_nWorldID == worldId) {
            return true;
        }
    }
    return false;
}

std::int64_t CResidenceData::GetTaxIncome() const {
    L2_GUARD_FUNCTION();
    return m_nTaxIncome;
}

int CResidenceData::GetAvailableOwnMinutes() const {
    const std::int32_t elapsed = runtime::Int32FromBits(
        static_cast<std::uint32_t>(runtime::crt::Time32Now()) - static_cast<std::uint32_t>(m_tLastOwnerChangedTime)
    );
    const std::int32_t remaining = runtime::Int32FromBits(
        static_cast<std::uint32_t>(GetMaxFortressOwnSeconds()) - static_cast<std::uint32_t>(elapsed)
    );
    return (remaining > 0 ? remaining : 0) / 60;
}
