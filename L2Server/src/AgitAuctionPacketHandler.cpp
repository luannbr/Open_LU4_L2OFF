#include "AgitAuctionPacketHandler.h"
#include "AgitAuctionLog.h"
#include "AuctionAgit.h"
#include "DB.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Residence.h"
#include "Shared/src/TextRuntime.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Config.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <array>
#include <cstdint>
#include <ctime>

namespace {

// Noise: The source-prefix macro supplies the filename. No active legacy message uses this display constant.
// constexpr wchar_t kSourceFile[] =
//     L"AgitAuctionPacketHandler.cpp".

CAuctionAgit* CastAuctionAgit(CResidence* const residence) {
    return residence != nullptr ? static_cast<CWorldAgit*>(residence)->CastAuctionAgit() : nullptr;
}

int ResidenceId(const CAuctionAgit* const agit) { return agit->m_data != nullptr ? agit->m_data->m_nResidenceID : 0; }

// Shared diagnostic implementation now lives in AgitAuctionLog.h. The original helper is kept below.
// void ReportUnavailableAuctionClan(
//     User& user,
//     const wchar_t* const operation) {
//     const CSharedCreatureData* const data = user.m_data
//     if (data == nullptr) {
//         L2_LOG_ERROR(Log,
//             L"Cannot %ls: character data is unavailable. The request is rejected.",
//             operation)
//         return.
//     }
//     const std::uint32_t clanDatabaseId = data->m_nPledgeID
//     if (clanDatabaseId == 0) {
//         // Noise: No clan membership is an ordinary eligibility refusal.
//         return.
//     }
//     // L2_LOG_WARN(Log,
//     //     L"Cannot %ls: clan lookup returned no object for character '%ls' (clan DB ID %u). The request is rejected.",
//     //     operation, user.GetName(), static_cast<unsigned>(clanDatabaseId))
//     L2_LOG_WARN(Log,
//         "Cannot %s: clan lookup returned no object for character '%s' (clan DB ID %u). The request is rejected.",
//         WideToUtf8(operation).c_str(), WideToUtf8(user.GetName()).c_str(),
//         static_cast<unsigned>(clanDatabaseId))
// }

} // namespace

CAgitAuctionPacketHandler::CAgitAuctionPacketHandler(CUserSocket* const userSocket) : m_pUserSocket(userSocket) {}

CAgitAuctionPacketHandler::~CAgitAuctionPacketHandler() {
    L2_GUARD_FUNCTION();
    m_pUserSocket = nullptr;
}

int CAgitAuctionPacketHandler::GetAgitType(const int residenceId) {
    // Current L2Server 0x00454fa8.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        return 0;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        return 0;
    }

    // The current image calls IsWorldAgit without a null
    // check after GetResidence.
    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence->IsWorldAgit()) {
        CWorldAgit* const worldAgit = residence->CastWorldAgit();
        CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
        if (auctionAgit != nullptr) {
            if (auctionAgit->GetSaleState() == SALE_ACT && auctionAgit->CanDoAuction()) {
                return 3;
            }
            return auctionAgit->IsInAuction(static_cast<int>(pledge->GetDbId())) ? 4 : 1;
        }
    }
    return 0;
}

void CAgitAuctionPacketHandler::AgitAuctionInitInfo() {
    // Current L2Server 0x00455108.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x11a)
        // L2_LOG_ERROR(Log,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x11a)
        L2_LOG_WARN(
            Log, "Cannot load clan-hall auction information: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x11a)
        // Noise: A character without a clan is an expected refusal. Retain diagnostics for missing character data or an
        // unavailable declared clan. L2_LOG_ERROR(Log,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x11a)
        // ReportUnavailableAuctionClan(*user, L"load clan-hall auction information")
        l2server::logging::ReportUnavailableAuctionClan(Log, *user, L"load clan-hall auction information", __FILE__);
        return;
    }
    db.RequestAgitInfo(user, 0, static_cast<int>(pledge->GetDbId()));
}

void CAgitAuctionPacketHandler::AgitAuctionBidStep1(const int residenceId) {
    // Current L2Server 0x00455268.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x2d7)
        // L2_LOG_ERROR(Log,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x2d7)
        L2_LOG_WARN(
            Log, "Cannot prepare a clan-hall bid: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x2d7)
        // Noise: A character without a clan is an expected refusal. Retain diagnostics for missing character data or an
        // unavailable declared clan. L2_LOG_ERROR(Log,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x2d7)
        // ReportUnavailableAuctionClan(*user, L"prepare a clan-hall bid")
        l2server::logging::ReportUnavailableAuctionClan(Log, *user, L"prepare a clan-hall bid", __FILE__);
        return;
    }

    CAuctionAgit* const auctionAgit = CastAuctionAgit(g_ResidenceDB.GetResidence(residenceId));
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }
    if (!auctionAgit->CanAttend(static_cast<int>(user->m_data->m_nPledgeID), user)) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"can not attend auction . agitid(%d) %s, %d",
        //     residenceId,
        //     kSourceFile,
        //     0x2e6)
        // Noise: Eligibility refusal is a normal bid outcome. Preserve CanAttend's existing feedback and the caller's return
        // policy. L2_LOG_ERROR(Log,
        //     L"can not attend auction . agitid(%d) %s, %d",
        //     residenceId,
        //     kSourceFile,
        //     0x2e6)
        return;
    }
    db.RequestAgitBid1(user, ResidenceId(auctionAgit), static_cast<int>(pledge->GetDbId()));
}

void CAgitAuctionPacketHandler::AgitAuctionBidStep2(const int residenceId, const std::int64_t price) {
    // Current L2Server 0x0045548c.
    L2_GUARD_FUNCTION();

    CAuctionAgit* const auctionAgit = CastAuctionAgit(g_ResidenceDB.GetResidence(residenceId));
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }

    const std::int64_t tenPercent = runtime::Win64CvttDoubleToInt64(
        static_cast<double>(runtime::MultiplyInt64Wrapping(price, INT64_C(10))) / 100.0
    );
    m_pUserSocket->Send("chddQQ", 0xfe, 0xd2, 0x0e, ResidenceId(auctionAgit), price, tenPercent);
}

void CAgitAuctionPacketHandler::AgitAuctionBidStep3(const int residenceId, const std::int64_t price) {
    // Current L2Server 0x004555d4.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x32a)
        // L2_LOG_ERROR(Log,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x32a)
        L2_LOG_WARN(
            Log, "Cannot submit a clan-hall bid: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x32a)
        // Noise: A character without a clan is an expected refusal. Retain diagnostics for missing character data or an
        // unavailable declared clan. L2_LOG_ERROR(Log,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x32a)
        // ReportUnavailableAuctionClan(*user, L"submit a clan-hall bid")
        l2server::logging::ReportUnavailableAuctionClan(Log, *user, L"submit a clan-hall bid", __FILE__);
        return;
    }

    CAuctionAgit* const auctionAgit = CastAuctionAgit(g_ResidenceDB.GetResidence(residenceId));
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }
    if (!user->IsPledgeMaster() && !user->HavePledgePower(PP_AGIT_AUCTION)) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_HALL_AUCTION_PARTICIPATION_REQUIREMENTS);
        return;
    }
    if (price < auctionAgit->m_nMinBidPrice) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::BID_MUST_EXCEED_MINIMUM);
        return;
    }

    const int pledgeId = static_cast<int>(pledge->GetDbId());
    if (!auctionAgit->CanAttend(pledgeId, user)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"can not attend auction nId[%d], nPrice[%I64d]",
        //     residenceId,
        //     price)
        // Noise: Eligibility refusal is a normal bid outcome. Preserve CanAttend's existing feedback and the caller's return
        // policy. L2_LOG_ERROR(Log,
        //     L"can not attend auction nId[%d], nPrice[%I64d]",
        //     residenceId,
        //     price)
        return;
    }
    if (!auctionAgit->CheckSendable(3)) {
        return;
    }
    db.RequestCreateAgitBid(user, ResidenceId(auctionAgit), auctionAgit->m_nAuctionId, pledgeId, price);
}

void CAgitAuctionPacketHandler::AgitAuctionReBidStep1(const int residenceId) {
    // Current L2Server 0x00455854.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x398)
        // L2_LOG_ERROR(Log,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x398)
        L2_LOG_WARN(
            Log,
            "Cannot prepare a replacement clan-hall bid: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x398)
        // Noise: A character without a clan is an expected refusal. Retain diagnostics for missing character data or an
        // unavailable declared clan. L2_LOG_ERROR(Log,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x398)
        // ReportUnavailableAuctionClan(*user, L"prepare a replacement clan-hall bid")
        l2server::logging::ReportUnavailableAuctionClan(Log, *user, L"prepare a replacement clan-hall bid", __FILE__);
        return;
    }

    CAuctionAgit* const auctionAgit = CastAuctionAgit(g_ResidenceDB.GetResidence(residenceId));
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }
    if (!auctionAgit->CanAttend(static_cast<int>(user->m_data->m_nPledgeID), user)) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"can not attend auction . agitid(%d) %s, %d",
        //     residenceId,
        //     kSourceFile,
        //     0x3a7)
        // Noise: Eligibility refusal is a normal bid outcome. Preserve CanAttend's existing feedback and the caller's return
        // policy. L2_LOG_ERROR(Log,
        //     L"can not attend auction . agitid(%d) %s, %d",
        //     residenceId,
        //     kSourceFile,
        //     0x3a7)
        return;
    }
    db.RequestAgitBidRe(user, ResidenceId(auctionAgit), static_cast<int>(pledge->GetDbId()));
}

void CAgitAuctionPacketHandler::AgitAuctionBidCancelConfirm(const int residenceId) {
    // Current L2Server 0x00455a78.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x420)
        // L2_LOG_ERROR(Log,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x420)
        L2_LOG_WARN(
            Log,
            "Cannot show clan-hall bid cancellation details: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x420)
        // Noise: A character without a clan is an expected refusal. Retain diagnostics for missing character data or an
        // unavailable declared clan. L2_LOG_ERROR(Log,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x420)
        // ReportUnavailableAuctionClan(*user, L"show clan-hall bid cancellation details")
        l2server::logging::ReportUnavailableAuctionClan(Log, *user, L"show clan-hall bid cancellation details", __FILE__);
        return;
    }

    CAuctionAgit* const auctionAgit = CastAuctionAgit(g_ResidenceDB.GetResidence(residenceId));
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }
    const std::int64_t auctionPrice = auctionAgit->GetAuctionPrice(static_cast<int>(pledge->GetDbId()));
    CResidenceData* const data = auctionAgit->m_data;
    const int currentResidenceId = data != nullptr ? data->m_nResidenceID : 0;
    m_pUserSocket->Send("chddQQ", 0xfe, 0xd2, 8, currentResidenceId, auctionPrice, data->m_nAgitDeposit);
}

void CAgitAuctionPacketHandler::AgitAuctionBidCancelProc(const int residenceId) {
    // Current L2Server 0x00455c70.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x43c)
        // L2_LOG_ERROR(Log,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x43c)
        L2_LOG_WARN(
            Log, "Cannot cancel a clan-hall bid: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x43c)
        // Noise: A character without a clan is an expected refusal. Retain diagnostics for missing character data or an
        // unavailable declared clan. L2_LOG_ERROR(Log,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x43c)
        // ReportUnavailableAuctionClan(*user, L"cancel a clan-hall bid")
        l2server::logging::ReportUnavailableAuctionClan(Log, *user, L"cancel a clan-hall bid", __FILE__);
        return;
    }
    if (!user->IsPledgeMaster() && !user->HavePledgePower(PP_AGIT_AUCTION)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid pledge master. char_db_id(%d), %s, %d",
        //     user->GetDBID(),
        //     kSourceFile,
        //     0x440)
        // Noise: Missing auction permission is an expected command refusal. The cancellation returns without changing bids.
        // L2_LOG_ERROR(Log,
        //     L"invalid pledge master. char_db_id(%d), %s, %d",
        //     user->GetDBID(),
        //     kSourceFile,
        //     0x440)
        return;
    }

    CAuctionAgit* const auctionAgit = CastAuctionAgit(g_ResidenceDB.GetResidence(residenceId));
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }
    if (auctionAgit->CheckSendable(4)) {
        db.RequestAgitBidCancelRequest(
            user, residenceId, auctionAgit->m_nAuctionId, static_cast<int>(pledge->GetDbId()), 10
        );
    }
}

void CAgitAuctionPacketHandler::AgitDetailInfo(const int residenceId) {
    // Current L2Server 0x00455ed4.
    L2_GUARD_FUNCTION();

    if (m_pUserSocket->GetUser() == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x1fb)
        // L2_LOG_ERROR(Log,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x1fb)
        L2_LOG_WARN(
            Log, "Cannot show clan-hall details: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    if (residence != nullptr && residence->IsWorldAgit()) {
        CWorldAgit* const worldAgit = residence->CastWorldAgit();
        CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
        if (auctionAgit != nullptr) {
            CResidenceData* const data = auctionAgit->m_data;
            CSPointer<CPledge> owner = CPledge::FindPledge(g_nWorldId, static_cast<int>(data->m_nResidenceOwnerID));
            if (owner) {
                m_pUserSocket->Send(
                    "chdcdSSQQQd", 0xfe, 0xd2, 2, GetAgitType(residenceId), residenceId, owner->GetPledgeName(),
                    owner->GetMasterName(), data->m_nAgitLease, auctionAgit->m_nLastPrice, auctionAgit->m_nMinBidPrice,
                    auctionAgit->m_tAuctionEnd
                );
            }
            return;
        }
    }
    m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
}

CWorldAgit* CAgitAuctionPacketHandler::GetPledgeWorldAgit(const int flags, const bool report) {
    // Current L2Server 0x0045613c.
    L2_GUARD_FUNCTION();

    if ((flags & 4) != 0) {
        for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
            CResidence* const residence = entry.second;
            if (residence->IsWorldAgit()) {
                CWorldAgit* const worldAgit = residence->CastWorldAgit();
                if (worldAgit != nullptr) {
                    return worldAgit;
                }
            }
        }
    }

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return nullptr;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        return nullptr;
    }
    if (!user->HasWorldAgit()) {
        if (report) {
            m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        }
        return nullptr;
    }

    CWorldAgit* const worldAgit = pledge->GetWorldAgit();
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        if (report) {
            m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        }
        return nullptr;
    }
    if ((flags & 2) == 0) {
        return worldAgit;
    }
    if (!user->IsPledgeMaster() && !user->HavePledgePower(PP_AGIT_AUCTION)) {
        if (report) {
            m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_HALL_AUCTION_PARTICIPATION_REQUIREMENTS);
        }
        return nullptr;
    }
    if (auctionAgit->InCancelPenalty()) {
        if (report) {
            m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_AUCTION_DURING_POST_CANCELLATION_RESTRICTION);
        }
        return nullptr;
    }
    return worldAgit;
}

void CAgitAuctionPacketHandler::AgitAuctionList(int page) {
    // The original server writes each entry at offset zero and sends a zero-length blob.
    L2_GUARD_FUNCTION();

    std::array<char, 0x5000> buffer{};
    if (m_pUserSocket->GetUser() == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x157)
        // L2_LOG_ERROR(Log,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x157)
        L2_LOG_WARN(
            Log, "Cannot list clan-hall auctions: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    if (page < 1) {
        page = 1;
    }

    const int limit = runtime::MultiplyInt32Wrapping(page, 8);
    const int firstOnPage = runtime::SubtractInt32Wrapping(limit, 8);
    int count = 0;
    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        if (count >= limit) {
            break;
        }
        CResidence* const residence = entry.second;
        if (residence->IsWorldAgit()) {
            CWorldAgit* const worldAgit = residence->CastWorldAgit();
            CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
            if (auctionAgit != nullptr && auctionAgit->GetSaleState() == SALE_ACT) {
                if (count >= firstOnPage) {
                    static_cast<void>(::Assemble(
                        buffer.data(), 0x5000, "ddQc", ResidenceId(auctionAgit), auctionAgit->m_tAuctionEnd,
                        auctionAgit->m_nMinBidPrice, 1
                    ));
                }
                ++count;
            }
        }
    }
    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        if (count >= limit) {
            break;
        }
        CResidence* const residence = entry.second;
        if (residence->IsWorldAgit()) {
            CWorldAgit* const worldAgit = residence->CastWorldAgit();
            CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
            if (auctionAgit != nullptr && auctionAgit->GetSaleState() != SALE_ACT) {
                if (count >= firstOnPage) {
                    static_cast<void>(::Assemble(buffer.data(), 0x5000, "ddQc", ResidenceId(auctionAgit), 0, INT64_C(0), 0));
                }
                ++count;
            }
        }
    }
    m_pUserSocket->Send("chddb", 0xfe, 0xd2, 0x14, count, 0, buffer.data());
}

void CAgitAuctionPacketHandler::MyAgitList() {
    // Current L2Server 0x004566f8.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x1a5)
        // L2_LOG_ERROR(Log,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x1a5)
        L2_LOG_WARN(
            Log, "Cannot list the character's clan halls: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    if (user->GetPledge() == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x1a5)
        // Noise: A character without a clan is an expected refusal. Retain diagnostics for missing character data or an
        // unavailable declared clan. L2_LOG_ERROR(Log,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x1a5)
        // ReportUnavailableAuctionClan(*user, L"list the character's clan halls")
        l2server::logging::ReportUnavailableAuctionClan(Log, *user, L"list the character's clan halls", __FILE__);
        return;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(1, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }

    std::array<char, 0x5000> buffer{};
    const int residenceId = ResidenceId(auctionAgit);
    const int firstSize = ::Assemble(buffer.data(), 0x5000, "cd", 1, residenceId);
    int packetSize = firstSize > 0 ? firstSize : 0;
    int count = 1;
    if (auctionAgit->GetSaleState() == SALE_ACT && auctionAgit->CanDoAuction()) {
        const int written = ::Assemble(buffer.data() + packetSize, 0x5000 - packetSize, "cd", 3, residenceId);
        count = 2;
        if (written > 0) {
            packetSize += written;
        }
    }
    m_pUserSocket->Send("chddb", 0xfe, 0xd2, 3, count, packetSize, buffer.data());
}

void CAgitAuctionPacketHandler::AgitAuctionRegisterStep1() {
    // Current L2Server 0x00456988.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x239)
        // L2_LOG_ERROR(Log,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x239)
        L2_LOG_WARN(
            Log,
            "Cannot prepare a clan-hall auction listing: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x239)
        // Noise: A character without a clan is an expected refusal. Retain diagnostics for missing character data or an
        // unavailable declared clan. L2_LOG_ERROR(Log,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x239)
        // ReportUnavailableAuctionClan(*user, L"prepare a clan-hall auction listing")
        l2server::logging::ReportUnavailableAuctionClan(Log, *user, L"prepare a clan-hall auction listing", __FILE__);
        return;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(2, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit != nullptr) {
        db.RequestAgitSale1(user, ResidenceId(auctionAgit), static_cast<int>(pledge->GetDbId()));
    }
}

void CAgitAuctionPacketHandler::AgitAuctionRegisterStep2(
    const int residenceId, const std::int64_t price, const int periodDays
) {
    // Current L2Server 0x00456b28. The decoded residence id is unused.
    L2_GUARD_FUNCTION();
    static_cast<void>(residenceId);

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x264)
        // L2_LOG_ERROR(Log,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x264)
        L2_LOG_WARN(
            Log,
            "Cannot show clan-hall auction listing confirmation: connection %u has no bound character; the request is "
            "rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    if (user->GetPledge() == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x264)
        // Noise: A character without a clan is an expected refusal. Retain diagnostics for missing character data or an
        // unavailable declared clan. L2_LOG_ERROR(Log,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x264)
        // ReportUnavailableAuctionClan(*user, L"show clan-hall auction listing confirmation")
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"show clan-hall auction listing confirmation", __FILE__
        );
        return;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(2, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        return;
    }
    if (static_cast<std::uint64_t>(price) - UINT64_C(1) >= UINT64_C(99900000000)) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::BID_MUST_EXCEED_MINIMUM);
        return;
    }

    const std::int32_t endBits =
        runtime::AddInt32Wrapping(runtime::crt::Time32Now(), runtime::MultiplyInt32Wrapping(periodDays, 86400));
    std::tm* const endTime = runtime::crt::LocalTime32(endBits);
    endTime->tm_sec = 0;
    endTime->tm_min = 0;
    const std::int32_t auctionEnd = runtime::crt::Mktime32(endTime);

    auctionAgit->SetSaleState(SALE_STEP2);
    CResidenceData* const data = auctionAgit->m_data;
    const int currentResidenceId = data != nullptr ? data->m_nResidenceID : 0;
    m_pUserSocket->Send(
        "chddQQdd", 0xfe, 0xd2, 5, currentResidenceId, data->m_nAgitAuctionMin, data->m_nAgitDeposit, periodDays, auctionEnd
    );
}

void CAgitAuctionPacketHandler::AgitAuctionRegisterStep3(
    const int residenceId, const std::int64_t price, const int periodDays
) {
    // Current L2Server 0x00456d7c. The decoded residence id is unused.
    L2_GUARD_FUNCTION();
    static_cast<void>(residenceId);

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x28b)
        // L2_LOG_ERROR(Log,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x28b)
        L2_LOG_WARN(
            Log, "Cannot submit a clan-hall auction listing: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x28b)
        // Noise: A character without a clan is an expected refusal. Retain diagnostics for missing character data or an
        // unavailable declared clan. L2_LOG_ERROR(Log,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x28b)
        // ReportUnavailableAuctionClan(*user, L"submit a clan-hall auction listing")
        l2server::logging::ReportUnavailableAuctionClan(Log, *user, L"submit a clan-hall auction listing", __FILE__);
        return;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(2, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        return;
    }
    if (static_cast<std::uint64_t>(price) - UINT64_C(1) >= UINT64_C(99900000000)) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::BID_MUST_EXCEED_MINIMUM);
        return;
    }
    if (!auctionAgit->CheckSendable(1)) {
        return;
    }

    CResidenceData* const data = auctionAgit->m_data;
    const int currentResidenceId = data != nullptr ? data->m_nResidenceID : 0;
    db.RequestCreateAgitAuction(
        user, currentResidenceId, static_cast<int>(pledge->GetDbId()), u"", price, periodDays,
        data->m_nAgitDeposit, 10
    );
}

void CAgitAuctionPacketHandler::AgitAuctionRegisterCancelConfirm() {
    // Current L2Server 0x00456fa0.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x3d5)
        // L2_LOG_ERROR(Log,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     0x3d5)
        L2_LOG_WARN(
            Log,
            "Cannot show clan-hall listing cancellation details: connection %u has no bound character; the request is "
            "rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    if (user->GetPledge() == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x3d5)
        // Noise: A character without a clan is an expected refusal. Retain diagnostics for missing character data or an
        // unavailable declared clan. L2_LOG_ERROR(Log,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     0x3d5)
        // ReportUnavailableAuctionClan(*user, L"show clan-hall listing cancellation details")
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"show clan-hall listing cancellation details", __FILE__
        );
        return;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(2, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit != nullptr) {
        m_pUserSocket->Send("chdQ", 0xfe, 0xd2, 7, auctionAgit->m_data->m_nAgitDeposit);
    }
}

void CAgitAuctionPacketHandler::AgitAuctionRegisterCancelProc() {
    // Current L2Server 0x00457144.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     1000)
        // L2_LOG_ERROR(Log,
        //     L"invalid user. %s, %d",
        //     kSourceFile,
        //     1000)
        L2_LOG_WARN(
            Log, "Cannot cancel a clan-hall auction listing: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(
        //     LOG_ERROR,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     1000)
        // Noise: A character without a clan is an expected refusal. Retain diagnostics for missing character data or an
        // unavailable declared clan. L2_LOG_ERROR(Log,
        //     L"invalid user[%s]'s pledge. %s, %d",
        //     user->GetName(),
        //     kSourceFile,
        //     1000)
        // ReportUnavailableAuctionClan(*user, L"cancel a clan-hall auction listing")
        l2server::logging::ReportUnavailableAuctionClan(Log, *user, L"cancel a clan-hall auction listing", __FILE__);
        return;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(2, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr || !auctionAgit->CheckSendable(2)) {
        return;
    }
    if (auctionAgit->GetSaleState() == SALE_ACT) {
        db.RequestCancelAgitAuction(user, ResidenceId(auctionAgit), static_cast<int>(pledge->GetDbId()));
    } else {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_HALL_NOT_BEING_AUCTIONED);
    }
}

bool CAgitAuctionPacketHandler::Process(const int command, const std::uint8_t* const packet) {
    // Current L2Server 0x00457324.
    L2_GUARD_FUNCTION();

    int residenceId = 0;
    std::int64_t price = 0;
    int periodDays = 0;
    switch (command) {
        case 1:
            AgitAuctionInitInfo();
            break;
        case 2:
            ::Disassemble(packet, "d", &residenceId);
            AgitDetailInfo(residenceId);
            break;
        case 3:
            MyAgitList();
            break;
        case 4:
            AgitAuctionRegisterStep1();
            break;
        case 5:
            ::Disassemble(packet, "dQd", &residenceId, &price, &periodDays);
            AgitAuctionRegisterStep2(residenceId, price, periodDays);
            break;
        case 6:
            ::Disassemble(packet, "dQd", &residenceId, &price, &periodDays);
            AgitAuctionRegisterStep3(residenceId, price, periodDays);
            break;
        case 7:
            AgitAuctionRegisterCancelConfirm();
            break;
        case 8:
            AgitAuctionRegisterCancelProc();
            break;
        case 0x0b:
            ::Disassemble(packet, "d", &residenceId);
            AgitAuctionBidCancelConfirm(residenceId);
            break;
        case 0x0c:
            ::Disassemble(packet, "d", &residenceId);
            AgitAuctionBidCancelProc(residenceId);
            break;
        case 0x0d:
            ::Disassemble(packet, "d", &residenceId);
            AgitAuctionBidStep1(residenceId);
            break;
        case 0x0e:
            ::Disassemble(packet, "dQ", &residenceId, &price);
            AgitAuctionBidStep2(residenceId, price);
            break;
        case 0x0f:
            ::Disassemble(packet, "dQ", &residenceId, &price);
            AgitAuctionBidStep3(residenceId, price);
            break;
        case 0x10:
            ::Disassemble(packet, "d", &residenceId);
            AgitAuctionReBidStep1(residenceId);
            break;
        case 0x14:
            ::Disassemble(packet, "d", &residenceId);
            AgitAuctionList(residenceId);
            break;
        default:
            break;
    }
    return false;
}
