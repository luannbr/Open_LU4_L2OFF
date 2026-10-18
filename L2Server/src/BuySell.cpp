#include "BuySell.h"
#include "Shared/src/MemorySub.h"

#include "NPC.h"
#include "Residence.h"
#include "User.h"

#include "Shared/src/ScopedLockGuards.h"
#include "Shared/src/SharedFactory.h"

// Portable registration: the runtime ID is no longer a shipping number.
// static_assert(
//     CBuySell::kMemoryClassId ==
//     L2ServerMemory::kCBuySellClassId)

CBuy::CBuy(const std::int64_t buySellListId, const int residenceId) noexcept :
    m_nResidenceID(residenceId), m_alignment2CTo30{}, m_nBuySellListID(buySellListId), m_pResultPage{} {}

int CBuy::GetTaxRate() const {
    // Current L2Server 0x0057dcdc.
    L2_GUARD_FUNCTION();
    if (m_nResidenceID == 0) {
        return 0;
    }
    CResidence* const residence = g_ResidenceDB.GetResidence(m_nResidenceID);
    if (residence == nullptr) {
        return 0;
    }
    const CResidenceData* const data = residence->m_data;
    return data->m_TaxInfoData[static_cast<std::size_t>(data->m_ResidenceType)].m_nTaxRate;
}

int CBuy::GetBuyTaxRate() const {
    // Current L2Server 0x0057dda8.
    L2_GUARD_FUNCTION();
    if (m_nResidenceID == 0) {
        return 0;
    }
    CResidence* const residence = g_ResidenceDB.GetResidence(m_nResidenceID);
    if (residence == nullptr) {
        return 0;
    }
    const CResidenceData* const data = residence->m_data;
    return data->m_TaxInfoData[static_cast<std::size_t>(data->m_ResidenceType)].m_nBuyTaxRate;
}

int CBuy::ReportShopTax(const std::int64_t amount) {
    // Current L2Server 0x0057de74.
    L2_GUARD_FUNCTION();
    if (m_nResidenceID != 0) {
        CResidence* const residence = g_ResidenceDB.GetResidence(m_nResidenceID);
        if (residence != nullptr) {
            residence->ReportShopTax(amount, static_cast<CastleType::Enum>(residence->m_data->m_ResidenceType), true);
        }
    }
    return 0;
}

void CBuy::SetResultPage(char16_t* resultPage) {
    // Current L2Server 0x0057df44 is the unbounded Win32 lstrcpyW tail call.
    char16_t* destination = m_pResultPage;
    do {
        *destination = *resultPage;
        ++destination;
    } while (*resultPage++ != u'\0');
}

CBuySell::CBuySell(
    const std::int64_t itemNpcSellListId, const std::int64_t itemNpcBuyBanListId, const int residenceId, const double ratio
) noexcept :
    m_nItemNpcSellListID(itemNpcSellListId),
    m_nItemNpcBuyBanListID(itemNpcBuyBanListId),
    m_nResidenceID(residenceId),
    m_alignment3CTo40{},
    m_dRatio(ratio) {}

L2_DEFINE_OBJECT_CLASS_ID(CBuySell)

L2_DEFINE_OBJECT_CLASS_NAME_LITERAL(CBuySell)

L2_DEFINE_OBJECT_ALLOCATOR(CBuySell, __GetClassName())

L2_DEFINE_OBJECT_NEW(CBuySell)

L2_DEFINE_OBJECT_RELEASE(CBuySell)

int CBuySell::GetTaxRate() const {
    // Current L2Server 0x0057df50.
    L2_GUARD_FUNCTION();
    if (m_nResidenceID == 0) {
        return 0;
    }

    CResidence* residence = g_ResidenceDB.GetResidence(m_nResidenceID);
    if (residence == nullptr) {
        return 0;
    }
    if (residence->IsWorldAgit()) {
        residence = g_ResidenceDB.GetResidence(residence->m_data->m_nAgitParentCastleId);
        if (residence == nullptr) {
            return 0;
        }
    }

    const CResidenceData* const data = residence->m_data;
    return data->m_TaxInfoData[static_cast<std::size_t>(data->m_ResidenceType)].m_nTaxRate;
}

int CBuySell::GetBuyTaxRate() const {
    // Current L2Server 0x0057e060 does not redirect world agits.
    L2_GUARD_FUNCTION();
    if (m_nResidenceID == 0) {
        return 0;
    }
    CResidence* const residence = g_ResidenceDB.GetResidence(m_nResidenceID);
    if (residence == nullptr) {
        return 0;
    }
    const CResidenceData* const data = residence->m_data;
    return data->m_TaxInfoData[static_cast<std::size_t>(data->m_ResidenceType)].m_nBuyTaxRate;
}

int CBuySell::ReportShopTax(const std::int64_t amount, const bool isSell) {
    // Current L2Server 0x0057e12c.
    L2_GUARD_FUNCTION();
    if (m_nResidenceID != 0) {
        CResidence* const residence = g_ResidenceDB.GetResidence(m_nResidenceID);
        if (residence != nullptr) {
            residence->ReportShopTax(amount, static_cast<CastleType::Enum>(residence->m_data->m_ResidenceType), isSell);
        }
    }
    return 0;
}

CSPointer<CBuySell> CBuySell::SetBuySell(
    User& user, CNPC& npc, const std::int64_t itemNpcSellListId, const std::int64_t itemNpcBuyBanListId, const double ratio
) {
    // Current L2Server 0x0057e20c.
    const int residenceId = npc.m_data->m_nResidenceID;
    CBuySell* buySell;
    {
        L2_GUARD(*user.m_lock);

        buySell = new CBuySell(itemNpcSellListId, itemNpcBuyBanListId, residenceId, ratio);
        L2_ADD_REF(buySell, ObjectRefType::Anonymous);
        buySell->SetCreatureData(&npc);
        user.ReleaseEconomy();
        user.m_pEconomy = buySell;
    }
    return CSPointer<CBuySell>(buySell, ObjectRefType::FunctionChain);
}
