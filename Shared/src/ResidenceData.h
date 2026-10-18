#pragma once

#include "Shared/src/BaseStructure.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CSharedRWLock;

namespace CastleType {

enum Enum : std::int32_t {
    CASTLE_NOTHING = 0,
    CASTLE_GOOD = 1,
    CASTLE_BAD = 2,
    CASTLE_MAX = 3,
};

} // namespace CastleType


// Shared/residencedata.h target declarations.

extern CSharedRWLock g_ResidenceLock;

enum ResidenceState : std::int32_t {
    RS_ANNOUNCING = 0,
    RS_REGISTRATION = 1,
    RS_STANDBY = 2,
    RS_SIEGE = 3,
    RS_MID_VICTORY = 4,
    RS_DOMINION_SIEGE = 5,
    RS_PASSED_CASTLE_SIEGE = 6,
    RS_TYPE_CHANGE = 7,
    RS_MAX = 8,
};

struct PledgeInfo {
    int m_nPledgeID = 0;
    int m_nWorldID = 0;
};

struct TaxInfo {
    std::uint32_t m_nTaxRate = 0;
    std::uint32_t m_nTaxTaxRate = 0;
    std::uint32_t m_nTaxForResidenceRate = 0;
    std::uint32_t m_nBuyTaxRate = 0;
    std::uint32_t m_nBuyTaxTaxRate = 0;
    std::uint32_t m_nBuyTaxForResidenceRate = 0;
};

struct Deco {
    int nLevel = 0;
    int nId = 0;
    std::uint32_t nExpire = 0;
};

struct DecoNpcSlot {
    FVector pos;
    int nYaw = 0;
};

struct DecoNpc {
    int nCID = 0;
    int nNpcSID = 0;
    std::int32_t tExpire = 0;
};


// Current L2NPC.exe layout, corroborated field-by-field by its accesses and
// the 0x1450 CResidenceData::Inst stride. The adjacent PDB is older here, so
// it supplies member/source names but cannot override these current offsets.
struct CResidenceData {
    static CResidenceData& Inst(int residenceId);

    void InitializeData();
    bool IsDefendingPledge(int pledgeId, int worldId) const;
    bool IsOffendingPledge(int pledgeId, int worldId) const;
    std::int64_t GetTaxIncome() const;
    int GetAvailableOwnMinutes() const;

    std::array<char16_t, 40> m_sResidenceName{}; // +0x0000
    int m_nResidenceID = 0; // +0x0050
    std::uint32_t m_nResidenceOwnerID = 0; // +0x0054
    std::uint32_t m_nResidenceOwnerIndex = 0; // +0x0058
    int m_nResidenceOwnerWorldId = 0; // +0x005c
    std::int32_t m_NextSiege = 0; // +0x0060
    std::int32_t m_nSiegeTerm = 0; // +0x0064
    ResidenceState m_ResidenceState = RS_ANNOUNCING; // +0x0068
    int m_nLifeControlLevel = 0; // +0x006c
    int m_DefendingPledgeListSize = 0; // +0x0070
    std::array<PledgeInfo, 256> m_stDefendingPledgeList{}; // +0x0074
    int m_OffendingPledgeListSize = 0; // +0x0874
    std::array<PledgeInfo, 256> m_stOffendingPledgeList{}; // +0x0878
    int m_ResidenceType = 0; // +0x1078
    int m_nSuccessCampaign = 0; // +0x107c
    std::array<TaxInfo, 3> m_TaxInfoData{}; // +0x1080
    std::int64_t m_nTaxIncome = 0; // +0x10c8
    int m_nParentResidenceId = 0; // +0x10d0
    int m_nTaxRateToChange = 0; // +0x10d4
    int m_nBuyTaxRateToChange = 0; // +0x10d8
    int m_nTaxForResidenceRateToChange = 0; // +0x10dc
    std::int64_t m_nTaxIncomeTemp = 0; // +0x10e0
    std::array<int, 5> m_ManorIdList{}; // +0x10e8
    int m_nDomainFortressContractStatus = 0; // +0x10fc
    std::array<int, 5> m_nRelatedFortress{}; // +0x1100
    int m_nHpStove = 0; // +0x1114
    int m_nMpFlame = 0; // +0x1118
    int m_nHatcher = 0; // +0x111c
    int m_nTeleportLevel = 0; // +0x1120
    int m_nHpStoveExpire = 0; // +0x1124
    int m_nMpFlameExpire = 0; // +0x1128
    int m_nTeleportExpire = 0; // +0x112c
    std::array<Deco, 32> m_aDeco{}; // +0x1130
    int m_nResidenceGrade = 0; // +0x12b0
    int m_nResidenceDomainCount = 0; // +0x12b4
    std::array<int, 10> m_aResidenceDomains{}; // +0x12b8
    int m_nDecoNpcSlotCount = 0; // +0x12e0
    std::array<DecoNpcSlot, 5> m_aDecoNpcSlots{}; // +0x12e8
    std::array<DecoNpc, 5> m_aDecoNpc{}; // +0x1388
    int m_nBasicRate = 0; // +0x13c4
    int m_nAgitAuction = 0; // +0x13c8
    int m_nAgitNextAuction = 0; // +0x13cc
    std::int64_t m_nAgitAuctionMin = 0; // +0x13d0
    std::int64_t m_nAgitDeposit = 0; // +0x13d8
    std::int64_t m_nAgitLease = 0; // +0x13e0
    int m_nAgitSize = 0; // +0x13e8
    int m_nAgitParentCastleId = 0; // +0x13ec
    int m_nCostFail = 0; // +0x13f0
    int m_nMiniGameAgitPrepareTerm = 0; // +0x13f4
    int m_nMiniGameAgitProgressTerm = 0; // +0x13f8
    int m_nMiniGameAgitCloseTerm = 0; // +0x13fc
    int m_nCompeteAgitSiegeDay = 0; // +0x1400
    int m_nCompeteAgitSiegehour = 0; // +0x1404
    int m_nCompeteAgitSiegeMin = 0; // +0x1408
    int m_nCompeteAgitSiegeProgress = 0; // +0x140c
    int m_nFortressSiegeStatus = 0; // +0x1410
    bool m_bFortressIsInBoundary = false; // +0x1414
    int m_nFortressContractStatus = 0; // +0x1418
    int m_nFortressParentCastleId = 0; // +0x141c
    int m_nFortressCastleTreasureLevel = 0; // +0x1420
    int m_nFortressOwnerRewardCycleCount = 0; // +0x1424
    std::array<int, 5> m_nFacilityLevel{}; // +0x1428
    std::int32_t m_tLastOwnerChangedTime = 0; // +0x143c
    std::int64_t m_nFortressRent = 0; // +0x1440
    std::int32_t m_tLastRewardGivenTime = 0; // +0x1448
    int m_nDominionStatus = 0; // +0x144c
};


int GetMaxFortressOwnSeconds();
int GetMaxFortressRewardSeconds();
