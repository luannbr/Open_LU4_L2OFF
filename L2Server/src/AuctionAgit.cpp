#include "AuctionAgit.h"
#include "DB.h"
#include "InstantAgit.h"
#include "Packet.h"
#include "Pledge.h"
#include "PledgeFunction.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Config.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/TextRuntime.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

constexpr std::size_t kDescriptionCapacity = 255;

void CopyAuctionDescription(char16_t (&destination)[kDescriptionCapacity], const wchar_t* const source) {
    if (source == nullptr) {
        destination[0] = u'\0';
        errno = EINVAL;
        runtime::process::HandleInvalidParameter();
        return;
    }

    std::u16string converted;
    if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
        converted.assign(reinterpret_cast<const char16_t*>(source));
    } else {
        converted = WideToUtf16(std::wstring_view(source));
    }

    if (converted.size() >= kDescriptionCapacity) {
        destination[0] = u'\0';
        errno = ERANGE;
        runtime::process::HandleInvalidParameter();
        return;
    }
    std::copy(converted.begin(), converted.end(), destination);
    destination[converted.size()] = u'\0';
}

wchar_t* HostAuctionDescription(char16_t (&description)[kDescriptionCapacity]) {
    if constexpr (sizeof(wchar_t) == sizeof(char16_t)) {
        return reinterpret_cast<wchar_t*>(description);
    } else {
        thread_local std::wstring hostDescription;
        const std::size_t length = std::char_traits<char16_t>::length(description);
        hostDescription = Utf16ToWide(std::u16string_view(description, length));
        return hostDescription.data();
    }
}

} // namespace

AuctionInfo::AuctionInfo(const int pledgeId, const std::int64_t deposit, const std::int32_t registerTime) noexcept :
    m_nPledgeId(pledgeId), m_nDeposit(deposit), m_tRegister(registerTime) {}

AuctionInfo::AuctionInfo() noexcept : m_nPledgeId(0), m_nDeposit(0), m_tRegister(0) {}

CAuctionAgit::CAuctionAgit() :
    m_tAuctionBegin(0),
    m_tAuctionEnd(0),
    m_tLastCancel(0),
    m_tNextCost(0),
    m_nLastPrice(0),
    m_nBidPrice(0),
    m_nMinBidPrice(0),
    m_nBidDay(0),
    m_sDesc{},
    m_SaleState(SALE_NONE),
    m_nAuctionId(0),
    m_nCheckSendable(0),
    m_bDoAuction(true) {
    // Current L2Server 0x004a0d14.
    L2_GUARD_FUNCTION();
}

CAuctionAgit::~CAuctionAgit() = default;

bool CAuctionAgit::IsAuctionAgit() const { return true; }

CAuctionAgit* CAuctionAgit::CastAuctionAgit() const { return const_cast<CAuctionAgit*>(this); }

bool CAuctionAgit::InCancelPenalty() {
    // Current L2Server 0x0049f804.
    L2_GUARD_FUNCTION();
    return m_tLastCancel > 0 && runtime::crt::Time32Now() < m_tLastCancel + INT32_C(0x93a80);
}

void CAuctionAgit::StartAuctionTimer(const int auctionEnd) {
    // Current L2Server 0x0049f8b4.
    L2_GUARD_FUNCTION();
    if (m_SaleState == SALE_ACT) {
        m_tAuctionEnd = auctionEnd;
    }
}

bool CAuctionAgit::CheckSendable(const int bit) {
    // Current L2Server 0x0049f95c.
    L2_GUARD_FUNCTION();
    bool available;
    {
        L2_GUARD(m_AuctionSect);
        const std::uint32_t mask = UINT32_C(1) << (static_cast<std::uint8_t>(bit) & 0x1fU);
        available = (m_nCheckSendable & mask) == 0;
        if (available) {
            m_nCheckSendable |= mask;
        }
    }
    return available;
}

bool CAuctionAgit::UnCheckSendable(const int bit) {
    // Current L2Server 0x0049fa50.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_AuctionSect);
        const std::uint32_t mask = UINT32_C(1) << (static_cast<std::uint8_t>(bit) & 0x1fU);
        if ((m_nCheckSendable & mask) != 0) {
            m_nCheckSendable &= ~mask;
        }
    }
    return true;
}

void CAuctionAgit::SetSaleState(const SaleState state) {
    // Current L2Server 0x0049fb44.
    L2_GUARD_FUNCTION();
    m_SaleState = state;
}

SaleState CAuctionAgit::GetSaleState() {
    // Current L2Server 0x0049fbe4.
    L2_GUARD_FUNCTION();
    return m_SaleState;
}

bool CAuctionAgit::CanDoAuction() {
    // Current L2Server 0x0049fc74.
    L2_GUARD_FUNCTION();
    return m_bDoAuction;
}

void CAuctionAgit::EnterDoAuction() {
    // Current L2Server 0x0049fd04.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_AuctionSect);
        m_bDoAuction = false;
    }
}

void CAuctionAgit::LeaveDoAuction() {
    // Current L2Server 0x0049fdd8.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_AuctionSect);
        m_bDoAuction = true;
    }
}

void CAuctionAgit::SetAuctionDesc(wchar_t* const description) { CopyAuctionDescription(m_sDesc, description); }

wchar_t* CAuctionAgit::GetDesc() { return HostAuctionDescription(m_sDesc); }

void CAuctionAgit::SetAuctionData(
    const int auctionId, const std::int64_t minimumBidPrice, wchar_t* const description, const int auctionEnd
) {
    // Current L2Server 0x0049feac.
    L2_GUARD_FUNCTION();
    m_nAuctionId = auctionId;
    m_nMinBidPrice = minimumBidPrice;
    CopyAuctionDescription(m_sDesc, description);
    m_tAuctionEnd = auctionEnd;
    SetSaleState(SALE_ACT);
}

int CAuctionAgit::GetAuctionCount() {
    // Current L2Server 0x0049ff90 does not lock the vector.
    L2_GUARD_FUNCTION();
    return static_cast<int>(m_Auctions.size());
}

void CAuctionAgit::TimerExpired(const int id) {
    // Current L2Server 0x004a0044.
    L2_GUARD_FUNCTION();
    if (GetSaleState() == SALE_ACT && CanDoAuction()) {
        const std::int32_t currentTime = runtime::crt::Time32Now();
        if (m_tAuctionEnd <= currentTime) {
            EnterDoAuction();
            db.RequestSetAgitAuction(m_data->m_nResidenceID, m_nAuctionId);
        }
    }

    if (m_data->m_nResidenceOwnerID != 0 && m_data->m_nAgitLease > 0 &&
        (GetSaleState() == SALE_OWNED || GetSaleState() == SALE_STEP1)) {
        if (m_tNextCost == 0) {
            m_tNextCost = -1;
            db.RequestAgitCost(
                *this, static_cast<int>(m_data->m_nResidenceOwnerID), m_data->m_nAgitLease, -1, runtime::crt::Time32Now()
            );
        } else if (m_tNextCost > 0 && m_tNextCost <= runtime::crt::Time32Now()) {
            db.RequestAgitCost(
                *this, static_cast<int>(m_data->m_nResidenceOwnerID), m_data->m_nAgitLease, m_data->m_nCostFail, m_tNextCost
            );
            m_tNextCost = -1;
        }
    }

    if (m_data->m_nResidenceOwnerID == 0 && GetSaleState() == SALE_AUTOSELL) {
        SetSaleState(SALE_REQSELL);
        CopyAuctionDescription(m_sDesc, L"");
        db.RequestAgitAutoSell(*this, m_data->m_nAgitAuctionMin, 10, 7, 0);
    }

    if (m_data->m_nResidenceOwnerID == 0 && m_data->m_nAgitAuction == 1) {
        const std::int32_t currentTime = runtime::crt::Time32Now();
        if (m_data->m_nAgitNextAuction <= currentTime && GetSaleState() == SALE_NOTOWNED) {
            SetSaleState(SALE_REQSELL);
            CopyAuctionDescription(m_sDesc, L"");
            db.RequestAgitAutoSell(*this, m_data->m_nAgitAuctionMin, 10, 7, 0);
        }
    }
    CWorldAgit::TimerExpired(id);
}

bool CAuctionAgit::CanAttend(const int pledgeId, User* const user) {
    // Current L2Server 0x004a02dc.
    L2_GUARD_FUNCTION();
    if (user->m_data->GetWorldId() != g_nWorldId) {
        return false;
    }
    if (pledgeId < 1 || GetSaleState() != SALE_ACT) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return false;
    }
    const int ownerPledgeId = m_data != nullptr ? static_cast<int>(m_data->m_nResidenceOwnerID) : 0;
    if (pledgeId == ownerPledgeId) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CLAN_HALL_AUCTION_PARTICIPATION_REQUIREMENTS);
        return false;
    }

    CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, pledgeId);
    if (!pledge || pledge->GetWorldAgit() != nullptr) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return false;
    }
    if (!pledge->GetFunction()->IsEnable(PledgeFunctionType::AUCTION_AGIT)) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CLAN_HALL_AUCTION_PARTICIPATION_REQUIREMENTS);
        return false;
    }
    if (pledge->GetStatus() != 0) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return false;
    }

    int residenceId = 0;
    int auctionId = 0;
    int flags = 0;
    IsInAllAuctions(pledgeId, residenceId, auctionId, flags);
    if (flags == 0) {
        return true;
    }
    if ((flags & 1) != 0) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_BID_IN_ANOTHER_AUCTION);
    } else if ((flags & 2) != 0) {
        user->GetSocket()->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
    }
    return false;
}

void CAuctionAgit::SetNextCost(const int nextCost, const int costFail) {
    // Current L2Server 0x004a06a4.
    L2_GUARD_FUNCTION();
    if (costFail < 8) {
        m_tNextCost = nextCost;
        m_data->m_nCostFail = costFail;
        return;
    }

    m_tNextCost = 0;
    m_data->m_nCostFail = 0;
    const int ownerPledgeId = static_cast<int>(m_data->m_nResidenceOwnerID);
    const char16_t* pledgeName = u"";
    CSPointer<CPledge> pledge;
    if (ownerPledgeId != 0) {
        pledge = CPledge::FindPledge(g_nWorldId, ownerPledgeId);
        if (pledge) {
            pledgeName = pledge->GetPledgeName();
        }
    }
    ChangeOwner(nullptr, 0, true);
    if (ownerPledgeId != 0) {
        Log.Add(
            LOG_AUDIT, u"%I64d,,,,,,,,%s,,,,,,,%I64d,%I64d,,,,,,,,,", INT64_C(0x595), pledgeName,
            static_cast<std::int64_t>(ownerPledgeId), static_cast<std::int64_t>(m_data->m_nResidenceID)
        );
    }
    SetSaleState(SALE_AUTOSELL);
}

bool CAuctionAgit::IsInAuction(const int pledgeId) {
    // Current L2Server 0x004a083c.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_AuctionSect);
        for (const AuctionInfo& auction : m_Auctions) {
            if (auction.m_nPledgeId == pledgeId) {
                return true;
            }
        }
    }
    return false;
}

std::int64_t CAuctionAgit::GetAuctionPrice(const int pledgeId) {
    // Current L2Server 0x004a0944.
    L2_GUARD_FUNCTION();
    std::int64_t result = 0;
    {
        L2_GUARD(m_AuctionSect);
        for (const AuctionInfo& auction : m_Auctions) {
            if (auction.m_nPledgeId == pledgeId) {
                result = auction.m_nDeposit;
                break;
            }
        }
    }
    return result;
}

void CAuctionAgit::DelAuction(const int pledgeId) {
    // Current L2Server 0x004a0bf8.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_AuctionSect);
        for (auto position = m_Auctions.begin(); position != m_Auctions.end(); ++position) {
            if (position->m_nPledgeId == pledgeId) {
                m_Auctions.erase(position);
                break;
            }
        }
    }
}

void CAuctionAgit::AddAuction(const int pledgeId, const std::int64_t deposit) {
    // Current L2Server 0x004a0eac.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_AuctionSect);
        for (AuctionInfo& auction : m_Auctions) {
            if (auction.m_nPledgeId == pledgeId) {
                auction.m_nDeposit = deposit;
                auction.m_tRegister = runtime::crt::Time32Now();
                return;
            }
        }
        m_Auctions.emplace_back(pledgeId, deposit, runtime::crt::Time32Now());
    }
}

void CAuctionAgit::AddAuction2(const int pledgeId, const std::int64_t deposit, const int registerTime) {
    // Current L2Server 0x004a0ff4.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_AuctionSect);
        m_Auctions.emplace_back(pledgeId, deposit, registerTime);
    }
}

void CAuctionAgit::GetAuction(std::vector<AuctionInfo>& auctions) {
    // Current L2Server 0x004a10f4.
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_AuctionSect);
        auctions.clear();
        for (const AuctionInfo& auction : m_Auctions) {
            auctions.push_back(auction);
        }
    }
}

void CAuctionAgit::ResetAuction() {
    // Current L2Server 0x004a1238 leaves m_tNextCost,
    // m_nBidPrice and m_nCheckSendable unchanged.
    L2_GUARD_FUNCTION();
    m_tAuctionBegin = 0;
    m_tAuctionEnd = 0;
    m_tLastCancel = 0;
    std::fill(std::begin(m_sDesc), std::end(m_sDesc), u'\0');
    m_Auctions.clear();
    m_nLastPrice = 0;
    m_nMinBidPrice = 0;
    m_nBidDay = 0;
    m_SaleState = SALE_NONE;
    m_nAuctionId = 0;
    m_bDoAuction = true;
}

void CAuctionAgit::ReplyDoAuction(
    const int result, const int auctionId, const std::int64_t price, const int winnerPledgeId
) {
    // Current L2Server 0x004a134c. The first two reply lanes are parsed but
    // unused by the owner.
    L2_GUARD_FUNCTION();
    static_cast<void>(result);
    static_cast<void>(auctionId);

    CSPointer<CPledge> winner = CPledge::FindPledge(g_nWorldId, winnerPledgeId);
    if (!winner) {
        SetSaleState(m_data->m_nResidenceOwnerID != 0 ? SALE_OWNED : SALE_AUTOSELL);
        LeaveDoAuction();
    } else {
        SetSaleState(SALE_OWNED);
        ChangeOwner(winner.get(), 0, false);
        LeaveDoAuction();
        m_nLastPrice = price;

        CInstantAgitManager& manager = CInstantAgitManager::inst();
        if (manager.GetInstantAgit(static_cast<int>(winner->GetDbId())) != nullptr) {
            winner->SystemMessage(SystemMessageId::PROVISIONAL_CLAN_HALL_FORFEITED_AFTER_ACQUIRING_HIGHER_GRADE_HALL);
            User* const master = winner->GetMaster();
            if (master == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"Undefined Pledge Master PledgeDbId[%d] %s %d",
                //     winner->GetDbId(),
                //     L"AuctionAgit.cpp",
                //     0x1a1)
                // L2_LOG_ERROR(Log,
                //     L"Undefined Pledge Master PledgeDbId[%d] %s %d",
                //     winner->GetDbId(),
                //     L"AuctionAgit.cpp",
                //     0x1a1)
                L2_LOG_INFO(
                    Log,
                    "Clan %u has no available online leader after the hall auction; immediate expulsion is skipped and "
                    "returning the provisional hall is still attempted.",
                    static_cast<unsigned>(winner->GetDbId())
                );
            } else {
                manager.ExpelInstantAgit(*master);
            }
            manager.TryReturnInstantAgit(static_cast<int>(winner->GetDbId()));
        }
        ExpelNonPledgeMember();
    }

    std::vector<AuctionInfo> auctions;
    GetAuction(auctions);
    for (const AuctionInfo& auction : auctions) {
        CSPointer<CPledge> participant = CPledge::FindPledge(g_nWorldId, auction.m_nPledgeId);
        if (!participant) {
            continue;
        }
        if (!winner) {
            participant->SystemMessage(SystemMessageId::CLAN_HALL_AUCTION_UNSOLD);
        } else {
            participant->SystemMessage(
                SystemMessageId::CLAN_HALL_AUCTION_WON_BY_CLAN, EST_STRING, std::u16string(winner->GetPledgeName()).c_str()
            );
        }
    }
    m_Auctions.clear();
}
