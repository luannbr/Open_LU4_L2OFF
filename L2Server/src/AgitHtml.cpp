#include "AgitHtml.h"
#include "AgitAuctionLog.h"
#include "AuctionAgit.h"
#include "DB.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Residence.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Config.h"
#include "Shared/src/FHtml.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string>
#include <string_view>
#include <utility>

namespace {


std::u16string Padded2(const int value) {
    std::array<char16_t, 32> buffer{};
    static_cast<void>(FormatUtf16To(buffer.data(), buffer.size(), u"%02d", value));
    return buffer.data();
}

std::u16string Padded4(const int value) {
    std::array<char16_t, 32> buffer{};
    static_cast<void>(FormatUtf16To(buffer.data(), buffer.size(), u"%04d", value));
    return buffer.data();
}

std::u16string DecimalText(const long long value) { return FormatUtf16(u"%I64d", value); }

std::u16string AgitNameToken(const int residenceId) { return FormatUtf16(u"&%%%d;", residenceId); }

std::u16string AgitLocationToken(const int residenceId) { return u"&^" + DecimalText(residenceId) + u";"; }

std::u16string AgitAuctionLocationToken(const int residenceId) { return u"&*" + DecimalText(residenceId) + u";"; }

} // namespace

CAgitHTML::CAgitHTML(CUserSocket* const socket) : m_pUserSocket(socket) {
    L2_GUARD_FUNCTION();
    if (socket == nullptr) {
        m_pUserSocket = nullptr;
        // Log.Add(LOG_ERROR, L"CAgitHTML invalid socket")
        // L2_LOG_ERROR(Log, L"CAgitHTML invalid socket")
        L2_LOG_ERROR(
            Log, "Cannot initialize clan-hall HTML handling: the connection is missing; the handler has no socket."
        );
    }
}

CAgitHTML::~CAgitHTML() {
    L2_GUARD_FUNCTION();
    m_pUserSocket = nullptr;
}

void CAgitHTML::AgitBidReRequest(const int residenceId, const int alreadyChecked) {
    // Current L2Server 0x004587cc.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x457)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x457)
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
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x457)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x457)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"prepare a replacement clan-hall bid", __FILE__, l2server::logging::AuctionLogContext::Request
        );
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CAuctionAgit* const auctionAgit =
        residence != nullptr ? static_cast<CWorldAgit*>(residence)->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }
    if (alreadyChecked == 0 && !auctionAgit->CanAttend(static_cast<int>(user->m_data->m_nPledgeID), user)) {
        // Log.Add(LOG_ERROR, L"can not attend auction . agitid(%d) %s, %d", residenceId, L"AgitHtml.cpp", 0x466)
        // Noise: The eligibility check already handles an ordinary auction refusal. No bid is submitted or updated here.
        // L2_LOG_ERROR(Log, L"can not attend auction . agitid(%d) %s, %d", residenceId, L"AgitHtml.cpp", 0x466)
        return;
    }

    const int currentResidenceId = auctionAgit->m_data != nullptr ? auctionAgit->m_data->m_nResidenceID : 0;
    db.RequestAgitBidRe(user, currentResidenceId, static_cast<int>(pledge->GetDbId()));
}

void CAgitHTML::AgitBid1Request(const int residenceId, const int alreadyChecked) {
    // Current L2Server 0x004589ec.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x473)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x473)
        L2_LOG_WARN(
            Log, "Cannot prepare a clan-hall bid: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x473)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x473)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"prepare a clan-hall bid", __FILE__, l2server::logging::AuctionLogContext::Request
        );
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CAuctionAgit* const auctionAgit =
        residence != nullptr ? static_cast<CWorldAgit*>(residence)->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }
    if (alreadyChecked == 0 && !auctionAgit->CanAttend(static_cast<int>(user->m_data->m_nPledgeID), user)) {
        // Log.Add(LOG_ERROR, L"can not attend auction . agitid(%d) %s, %d", residenceId, L"AgitHtml.cpp", 0x482)
        // Noise: The eligibility check already handles an ordinary auction refusal. No bid is submitted or updated here.
        // L2_LOG_ERROR(Log, L"can not attend auction . agitid(%d) %s, %d", residenceId, L"AgitHtml.cpp", 0x482)
        return;
    }

    const int currentResidenceId = auctionAgit->m_data != nullptr ? auctionAgit->m_data->m_nResidenceID : 0;
    db.RequestAgitBid1(user, currentResidenceId, static_cast<int>(pledge->GetDbId()));
}

void CAgitHTML::AgitBidRequest(const int residenceId, const std::int64_t price) {
    // Current L2Server 0x00458c0c.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x510)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x510)
        L2_LOG_WARN(
            Log, "Cannot submit a clan-hall bid: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x510)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x510)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"submit a clan-hall bid", __FILE__, l2server::logging::AuctionLogContext::Request
        );
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CAuctionAgit* const auctionAgit =
        residence != nullptr ? static_cast<CWorldAgit*>(residence)->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }
    if (!user->IsPledgeMaster() && !user->HavePledgePower(PP_AGIT_AUCTION)) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_HALL_AUCTION_PARTICIPATION_REQUIREMENTS);
        return;
    }
    if (price < auctionAgit->GetMinBidPrice()) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::BID_MUST_EXCEED_MINIMUM);
        return;
    }

    const int pledgeDatabaseId = static_cast<int>(pledge->GetDbId());
    if (!auctionAgit->CanAttend(pledgeDatabaseId, user)) {
        // Log.Add(LOG_ERROR, L"can not attend auction nId[%d], nPrice[%I64d]", residenceId, price)
        // Noise: The eligibility check already handles an ordinary auction refusal. No bid is submitted or updated here.
        // L2_LOG_ERROR(Log, L"can not attend auction nId[%d], nPrice[%I64d]", residenceId, price)
        return;
    }
    if (!auctionAgit->CheckSendable(3)) {
        return;
    }

    const int currentResidenceId = auctionAgit->m_data != nullptr ? auctionAgit->m_data->m_nResidenceID : 0;
    db.RequestCreateAgitBid(user, currentResidenceId, auctionAgit->m_nAuctionId, pledgeDatabaseId, price);
}

bool CAgitHTML::AgitBidCancelRequest(const int residenceId, const int auctionId, const int pledgeId) {
    // Current L2Server 0x00458e8c.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x5a5)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x5a5)
        L2_LOG_WARN(
            Log, "Cannot cancel a clan-hall bid: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return false;
    }
    if (user->GetPledge() == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x5a5)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x5a5)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"cancel a clan-hall bid", __FILE__, l2server::logging::AuctionLogContext::Request
        );
        return false;
    }
    if (!user->IsPledgeMaster() && !user->HavePledgePower(PP_AGIT_AUCTION)) {
        // Log.Add(LOG_ERROR, L"invalid pledge master. char_db_id(%d), %s, %d", user->GetDBID(), L"AgitHtml.cpp", 0x5ab)
        // Noise: A character without clan-hall auction permission is an expected refusal. The permission check and return
        // remain unchanged. L2_LOG_ERROR(Log, L"invalid pledge master. char_db_id(%d), %s, %d", user->GetDBID(),
        // L"AgitHtml.cpp", 0x5ab)
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CAuctionAgit* const auctionAgit =
        residence != nullptr ? static_cast<CWorldAgit*>(residence)->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return false;
    }
    if (auctionAgit->CheckSendable(4)) {
        db.RequestAgitBidCancelRequest(user, residenceId, auctionId, pledgeId, 10);
    }
    return true;
}

void CAgitHTML::AgitSale1Request() {
    // Current L2Server 0x00459470.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x191)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x191)
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
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x191)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x191)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"prepare a clan-hall auction listing", __FILE__, l2server::logging::AuctionLogContext::Request
        );
        return;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(2, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        return;
    }
    const int residenceId = auctionAgit->m_data != nullptr ? auctionAgit->m_data->m_nResidenceID : 0;
    db.RequestAgitSale1(user, residenceId, static_cast<int>(pledge->GetDbId()));
}

bool CAgitHTML::SellActionRequest(const std::int64_t minimumBid, const int bidDay, const char16_t* const description) {
    // Current L2Server 0x00459610.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x21e)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x21e)
        L2_LOG_WARN(
            Log, "Cannot submit a clan-hall auction listing: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return false;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x21e)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x21e)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"submit a clan-hall auction listing", __FILE__, l2server::logging::AuctionLogContext::Request
        );
        return false;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(2, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        return false;
    }
    if (static_cast<std::uint64_t>(minimumBid) - 1ULL >= 99900000000ULL) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::BID_MUST_EXCEED_MINIMUM);
        return false;
    }
    if (!auctionAgit->CheckSendable(1)) {
        return false;
    }

    CResidenceData* const data = auctionAgit->m_data;
    const int residenceId = data != nullptr ? data->m_nResidenceID : 0;
    db.RequestCreateAgitAuction(
        user, residenceId, static_cast<int>(pledge->GetDbId()), description, minimumBid, bidDay, data->m_nAgitDeposit, 10
    );
    return true;
}

bool CAgitHTML::SaleCancelRequest() {
    // Current L2Server 0x00459838.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x28a)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x28a)
        L2_LOG_WARN(
            Log, "Cannot cancel a clan-hall auction listing: connection %u has no bound character; the request is rejected.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return false;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x28a)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x28a)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"cancel a clan-hall auction listing", __FILE__, l2server::logging::AuctionLogContext::Request
        );
        return false;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(2, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr || !auctionAgit->CheckSendable(2)) {
        return false;
    }
    if (auctionAgit->GetSaleState() != SALE_ACT) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_HALL_NOT_BEING_AUCTIONED);
        return false;
    }

    const int residenceId = auctionAgit->m_data != nullptr ? auctionAgit->m_data->m_nResidenceID : 0;
    db.RequestCancelAgitAuction(user, residenceId, static_cast<int>(pledge->GetDbId()));
    return true;
}

int CAgitHTML::SendHTML(std::u16string html) {
    L2_GUARD_FUNCTION();
    if (m_pUserSocket == nullptr) {
        return 0;
    }

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        return 0;
    }

    if (html.size() < 0x1ce9) {
        const std::u16string hostHtml(html);
        user->ShowHTML(nullptr, hostHtml.c_str(), 0, HTML::LAUNCH, true);
        return 1;
    }

    // Current 0x004591b8 supplies no value for the literal's `%d` slot.
    // Log.Add(LOG_ERROR, L"CAgitHTML Too large packet (size:%d)")
    // L2_LOG_ERROR(Log, L"CAgitHTML Too large packet (size:%d)")
    L2_LOG_ERROR(
        Log,
        "Cannot send clan-hall HTML on connection %u: %llu UTF-16 code units exceed the limit of 7400; the page is not "
        "sent.",
        static_cast<unsigned>(m_pUserSocket->GetServerId()), static_cast<unsigned long long>(html.size())
    );
    return 0;
}

CWorldAgit* CAgitHTML::GetPledgeWorldAgit(const int flags, const bool report) {
    // Current L2Server 0x0045922c.
    L2_GUARD_FUNCTION();

    if ((flags & 4) != 0) {
        for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
            CResidence* const residence = entry.second;
            if (residence != nullptr && residence->IsWorldAgit()) {
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

    CWorldAgit* const pledgeAgit = pledge->GetWorldAgit();
    CAuctionAgit* const auctionAgit = pledgeAgit != nullptr ? pledgeAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        if (report) {
            m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        }
        return nullptr;
    }

    if ((flags & 2) == 0) {
        return auctionAgit;
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
    return auctionAgit;
}

void CAgitHTML::AddStringMap(std::u16string_view name, std::u16string_view value) {
    // Current L2Server 0x0045a660. TrimLR recognizes only
    // U+0020, matching the target helper.
    L2_GUARD_FUNCTION();

    std::u16string targetName = TrimLR(std::u16string(name));
    std::u16string targetValue = TrimLR(std::u16string(value));
    if (!targetName.empty() && !targetValue.empty()) {
        m_mapParam[targetName] = targetValue;
    }
}

void CAgitHTML::AgitMaid() {
    // Current L2Server 0x00459a28.
    L2_GUARD_FUNCTION();
    char16_t sTmp[512];
    User* pUser = m_pUserSocket->GetUser();
    if (pUser == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x12d)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x12d)
        L2_LOG_WARN(
            Log, "Cannot show the clan-hall maid menu: connection %u has no bound character; the page is not sent.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }

    CPledge* pPledge = pUser->GetPledge();
    if (pPledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", pUser->GetName(), L"AgitHtml.cpp", 0x12d)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", pUser->GetName(), L"AgitHtml.cpp", 0x12d)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *pUser, L"show the clan-hall maid menu", __FILE__, l2server::logging::AuctionLogContext::HtmlPage
        );
        return;
    }

    std::u16string dst(u"<html><body>&nbsp;<br>&nbsp;<br><center>&$794;<br><hr>");
    std::fill_n(sTmp, 512, u'\0');
    static_cast<void>(FormatUtf16To(sTmp, 512, u"<a action=\"bypass %s\"> &$795;</a>", u"_agitminfo"));
    dst += sTmp;
    dst += u"<br><hr>";
    dst += u"<br><hr></center></body></html>";
    SendHTML(dst);
}

void CAgitHTML::AgitDealer() {
    // Current L2Server 0x00459c94.
    L2_GUARD_FUNCTION();
    char16_t sTmp[512];
    User* pUser = m_pUserSocket->GetUser();
    if (pUser == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x2cd)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x2cd)
        L2_LOG_WARN(
            Log, "Cannot show the clan-hall auctioneer menu: connection %u has no bound character; the page is not sent.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }

    std::u16string dst(u"<html><body>&nbsp;<br>&nbsp;<br><center>&$608;<br><hr>");
    std::fill_n(sTmp, 512, u'\0');
    static_cast<void>(FormatUtf16To(sTmp, 512, u"<a action=\"bypass %s\"> &$609;</a>", u"_agitauclist"));
    dst += sTmp;
    dst += u"<br><hr>";

    static_cast<void>(FormatUtf16To(sTmp, 512, u"<a action=\"bypass %s\"> &$610;</a>", u"_agitaucmy"));
    dst += sTmp;
    dst += u"<br><hr>";
    dst += u"<a action=\"bypass -h menu_select?ask=-201&reply=1\"> &$1194;</a>";
    dst += u"<br><hr></center></body></html>";
    SendHTML(dst);
}

void CAgitHTML::BidderList(const int residenceId, int page, const int backMode) {
    // Current L2Server 0x0045a894.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0xc9)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0xc9)
        L2_LOG_WARN(
            Log, "Cannot show clan-hall bidders: connection %u has no bound character; the page is not sent.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CAuctionAgit* const auctionAgit =
        residence != nullptr ? static_cast<CWorldAgit*>(residence)->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }

    m_mapParam.clear();
    std::u16string list;
    std::vector<AuctionInfo> auctions;
    auctionAgit->GetAuction(auctions);
    if (page < 1) {
        page = 1;
    }

    int index = 0;
    for (const AuctionInfo& auction : auctions) {
        if (index >= page * 8) {
            break;
        }
        if (index >= page * 8 - 8) {
            runtime::crt::LocalTime32(auction.m_tRegister);
            CSPointer<CPledge> pledge = CPledge::FindPledge(g_nWorldId, auction.m_nPledgeId);
            if (pledge) {
                list += u"<img src=\"L2UI.SquareGray\" width=270 height=1>  "
                        u"<table width=270 border=0>  <tr>  "
                        u"<td width=100><font color=\"aaaaff\">";
                const int currentResidenceId = auctionAgit->m_data != nullptr ? auctionAgit->m_data->m_nResidenceID : 0;
                list += AgitNameToken(currentResidenceId);
                list += u"</font></td> ";
                list += u"<td width=100> <font color=\"ffffaa\">  ";
                list += pledge->GetPledgeName();
                list += u" </font></td>";
                list += u" <td width=70>";
                std::tm* const registerTime = runtime::crt::LocalTime32(auction.m_tRegister);
                list += Padded2(registerTime->tm_year - 100);
                list += u"/";
                list += Padded2(registerTime->tm_mon + 1);
                list += u"/";
                list += Padded2(registerTime->tm_mday);
                list += u" </td>  </tr>  </table>";
            }
        }
        ++index;
    }

    list += u"<table border=0><tr>";
    if (page > 1) {
        list += u"<td > <a action=\"bypass _agitbidlist_" + DecimalText(residenceId) + u"_" + DecimalText(page - 1) + u"_" +
            DecimalText(backMode) + u"\"> &lt; </a> </td><td > | </td>";
    }
    list += u"<td > <a action=\"bypass _agitbidlist_" + DecimalText(residenceId) + u"_" + DecimalText(page + 1) + u"_" +
        DecimalText(backMode) + u"\"> &gt; </a> </td> ";
    list += u"</tr></table>&nbsp;";

    AddStringMap(u"AGIT_LIST", list);
    if (backMode == 3) {
        AddStringMap(u"AGIT_LINK_BACK", u"bypass _agitminfo");
    } else if (backMode == 4) {
        AddStringMap(u"AGIT_LINK_BACK", u"bypass _agitaucinfo_" + DecimalText(residenceId));
    }
    SendHTML(CFHTML::MakeHTMLFromStrMap(u"agitbidderlist.htm", m_mapParam, user->m_data->m_CountryCode));
}

void CAgitHTML::AgitInfo(const int mode) {
    // Current L2Server 0x0045af90. The incoming mode is not consumed.
    L2_GUARD_FUNCTION();
    static_cast<void>(mode);

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x15c)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x15c)
        L2_LOG_WARN(
            Log, "Cannot show clan-hall information: connection %u has no bound character; the page is not sent.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }

    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x15c)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x15c)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"show clan-hall information", __FILE__, l2server::logging::AuctionLogContext::HtmlPage
        );
        return;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(1, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }
    if (auctionAgit->GetSaleState() == SALE_ACT) {
        return;
    }

    auctionAgit->SetSaleState(SALE_STEP1);
    m_mapParam.clear();
    CResidenceData* const data = auctionAgit->m_data;
    const int residenceId = data != nullptr ? data->m_nResidenceID : 0;
    AddStringMap(u"AGIT_NAME", AgitNameToken(residenceId));
    AddStringMap(u"AGIT_OWNER_PLEDGE_NAME", std::u16string(pledge->GetPledgeName()));
    AddStringMap(u"OWNER_PLEDGE_MASTER", std::u16string(pledge->GetMasterName()));
    AddStringMap(u"AGIT_SIZE", std::u16string(DecimalText(data->m_nAgitSize).c_str()));
    AddStringMap(u"AGIT_LEASE", std::u16string(DecimalText(data->m_nAgitLease).c_str()));
    AddStringMap(u"AGIT_LOCATION", std::u16string(AgitLocationToken(residenceId).c_str()));
    AddStringMap(u"AGIT_LINK_BACK", u"bypass _agitdealer");
    AddStringMap(u"AGIT_LINK_SALE1", u"bypass _agitsale1");
    SendHTML(CFHTML::MakeHTMLFromStrMap(u"agitinfo.htm", m_mapParam, user->m_data->m_CountryCode));
}

void CAgitHTML::AgitSale1Reply(const int residenceId, const int pledgeId, const std::int64_t pledgeAdena) {
    // Current L2Server 0x0045b3c8. The first two reply fields are ignored.
    L2_GUARD_FUNCTION();
    static_cast<void>(residenceId);
    static_cast<void>(pledgeId);

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x1a3)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x1a3)
        L2_LOG_WARN(
            Log,
            "Cannot apply clan-hall listing preparation: connection %u has no bound character; the database reply is not "
            "applied.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    if (user->GetPledge() == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x1a3)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x1a3)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"apply clan-hall listing preparation", __FILE__, l2server::logging::AuctionLogContext::DatabaseReply
        );
        return;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(2, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        return;
    }

    auctionAgit->SetSaleState(SALE_STEP1);
    m_mapParam.clear();
    CResidenceData* const data = auctionAgit->m_data;
    const int agitId = data != nullptr ? data->m_nResidenceID : 0;
    AddStringMap(u"AGIT_NAME", AgitNameToken(agitId));
    AddStringMap(u"AGIT_LINK_BACK", u"bypass _agitinfo_2");
    AddStringMap(
        u"AGIT_DEPOSIT",
        std::u16string(DecimalText(runtime::Int32FromBits(static_cast<std::uint32_t>(data->m_nAgitDeposit))).c_str())
    );
    AddStringMap(u"AGIT_PLEDGE_ADENA", std::u16string(DecimalText(pledgeAdena).c_str()));
    AddStringMap(u"AGIT_LINK_SALE2", u"bypass _agitsale2");
    SendHTML(CFHTML::MakeHTMLFromStrMap(u"agitsale1.htm", m_mapParam, user->m_data->m_CountryCode));
}

void CAgitHTML::AgitSale2() {
    // Current L2Server 0x0045b75c.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x1c1)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x1c1)
        L2_LOG_WARN(
            Log, "Cannot show clan-hall listing terms: connection %u has no bound character; the page is not sent.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    if (user->GetPledge() == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x1c1)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x1c1)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"show clan-hall listing terms", __FILE__, l2server::logging::AuctionLogContext::HtmlPage
        );
        return;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(2, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        return;
    }

    auctionAgit->SetSaleState(SALE_STEP2);
    std::array<char16_t, 256> value{};
    m_mapParam.clear();
    CResidenceData* const data = auctionAgit->m_data;
    value.fill(L'\0');
    static_cast<void>(FormatUtf16To(value.data(), value.size(), u"&%%%d;", data->m_nResidenceID));
    AddStringMap(u"AGIT_NAME", std::u16string(value.data()));
    value.fill(L'\0');
    static_cast<void>(FormatUtf16To(value.data(), value.size(), u"%I64d", static_cast<long long>(data->m_nAgitAuctionMin)));
    AddStringMap(u"AGIT_AUCTION_MIN", std::u16string(value.data()));
    value.fill(L'\0');
    static_cast<void>(
        FormatUtf16To(value.data(), value.size(), u"%I64d", static_cast<long long>(auctionAgit->GetLastPrice()))
    );
    AddStringMap(u"AGIT_LAST_PRICE", std::u16string(value.data()));
    value.fill(L'\0');
    static_cast<void>(FormatUtf16To(value.data(), value.size(), u"bypass %s", u"_agitsale1"));
    AddStringMap(u"AGIT_LINK_BACK", std::u16string(value.data()));
    value.fill(L'\0');
    static_cast<void>(FormatUtf16To(value.data(), value.size(), u"Write %d 0 0 minbid day comment", 6));
    AddStringMap(u"AGIT_LINK_SALE3", std::u16string(value.data()));
    SendHTML(CFHTML::MakeHTMLFromStrMap(u"agitsale2.htm", m_mapParam, user->m_data->m_CountryCode));
}

void CAgitHTML::AgitSale3(const std::int64_t minimumBid, const int bidDay, wchar_t* const description) {
    // Current L2Server 0x0045baf0.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x1e1)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x1e1)
        L2_LOG_WARN(
            Log, "Cannot show clan-hall listing confirmation: connection %u has no bound character; the page is not sent.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    if (user->GetPledge() == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x1e1)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x1e1)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"show clan-hall listing confirmation", __FILE__, l2server::logging::AuctionLogContext::HtmlPage
        );
        return;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(2, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        return;
    }

    constexpr std::uint64_t kMaximumBid = 99900000000ULL;
    const std::uint64_t bidBits = static_cast<std::uint64_t>(minimumBid);
    if (bidBits - 1ULL >= kMaximumBid) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::BID_MUST_EXCEED_MINIMUM);
        return;
    }

    std::u16string escaped = WideToUtf16(std::wstring_view(description));
    escaped = ReplaceStr(std::move(escaped), u"<br/>", u"\n");
    escaped = ReplaceStr(std::move(escaped), u"&#10;", u" ");
    escaped = ReplaceStr(std::move(escaped), u"&#160;", u" ");
    escaped = ReplaceStr(std::move(escaped), u"&", u"&amp;");
    escaped = ReplaceStr(std::move(escaped), u"<", u"&lt;");
    escaped = ReplaceStr(std::move(escaped), u">", u"&gt;");
    escaped = ReplaceStr(std::move(escaped), u"\n", u"<br>");
    escaped = ReplaceStr(std::move(escaped), u"'", u"&apos;");
    escaped = ReplaceStr(std::move(escaped), u"\x7f", u"&apos;");

    std::array<char16_t, 256> escapedBuffer{};
    constexpr std::size_t kCopyCapacity = 0xff;
    if (escaped.size() >= kCopyCapacity) {
        escapedBuffer[0] = u'\0';
        errno = ERANGE;
        runtime::process::HandleInvalidParameter();
    } else {
        std::memcpy(escapedBuffer.data(), escaped.data(), (escaped.size() + 1U) * sizeof(char16_t));
    }
    const std::u16string escapedDescriptionUtf16 = SanitizeUtf16(escapedBuffer.data());
    const std::wstring escapedDescription = Utf16ToWide(escapedDescriptionUtf16);

    auctionAgit->SetSaleState(SALE_STEP3);
    m_mapParam.clear();
    CResidenceData* const data = auctionAgit->m_data;
    AddStringMap(u"AGIT_NAME", AgitNameToken(data->m_nResidenceID));
    AddStringMap(u"AGIT_AUCTION_MIN", std::u16string(DecimalText(data->m_nAgitAuctionMin).c_str()));
    AddStringMap(u"AGIT_AUCTION_MINBID", std::u16string(DecimalText(minimumBid).c_str()));
    AddStringMap(u"AGIT_AUCTION_DURATION", std::u16string(DecimalText(bidDay).c_str()));

    const std::uint32_t endBits =
        static_cast<std::uint32_t>(runtime::crt::Time32Now()) + static_cast<std::uint32_t>(bidDay) * 86400U;
    std::tm* const endTime = runtime::crt::LocalTime32(runtime::Int32FromBits(endBits));
    AddStringMap(u"AGIT_AUCTION_END_YY", std::u16string(Padded4(endTime->tm_year + 1900).c_str()));
    AddStringMap(u"AGIT_AUCTION_END_MM", std::u16string(Padded2(endTime->tm_mon + 1).c_str()));
    AddStringMap(u"AGIT_AUCTION_END_DD", std::u16string(Padded2(endTime->tm_mday).c_str()));
    AddStringMap(u"AGIT_AUCTION_END_HH", std::u16string(Padded2(endTime->tm_hour).c_str()));
    AddStringMap(u"AGIT_AUCTION_DESC", escapedDescriptionUtf16);
    AddStringMap(u"AGIT_LINK_BACK", u"bypass _agitsale1");
    std::array<char16_t, 256> action{};
    static_cast<void>(FormatUtf16To(
        action.data(), action.size(), u"bypass %s_%I64d_%d_%s", u"_agitsellact", minimumBid, bidDay,
        escapedDescription.c_str()
    ));
    AddStringMap(u"AGIT_LINK_ACT", std::u16string(action.data()));
    SendHTML(CFHTML::MakeHTMLFromStrMap(u"agitsale3.htm", m_mapParam, 0));
}

void CAgitHTML::SaleCancel() {
    // Current L2Server 0x0045c53c.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x272)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x272)
        L2_LOG_WARN(
            Log, "Cannot show clan-hall listing cancellation: connection %u has no bound character; the page is not sent.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    if (user->GetPledge() == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x272)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x272)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"show clan-hall listing cancellation", __FILE__, l2server::logging::AuctionLogContext::HtmlPage
        );
        return;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(2, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        return;
    }

    m_mapParam.clear();
    AddStringMap(
        u"AGIT_DEPOSIT",
        std::u16string(
            DecimalText(runtime::Int32FromBits(static_cast<std::uint32_t>(auctionAgit->m_data->m_nAgitDeposit))).c_str()
        )
    );
    AddStringMap(u"AGIT_LINK_BACK", u"bypass _agitminfo");
    AddStringMap(u"AGIT_LINK_DOCANCEL", u"bypass _agitcanact");
    SendHTML(CFHTML::MakeHTMLFromStrMap(u"agitsalecancel.htm", m_mapParam, user->m_data->m_CountryCode));
}

int CAgitHTML::AuctionList(int page) {
    // Current L2Server 0x0045c85c.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x2f1)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x2f1)
        L2_LOG_WARN(
            Log, "Cannot show the clan-hall auction list: connection %u has no bound character; the page is not sent.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return 0;
    }

    m_mapParam.clear();
    std::u16string list;
    if (page < 1) {
        page = 1;
    }
    list += u"<img src=\"L2UI.SquareGray\" width=270 height=1>  "
            u"<table width=270 border=0> ";

    int count = 0;
    for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
        if (count >= page * 8) {
            break;
        }
        CResidence* const residence = entry.second;
        if (residence == nullptr || !residence->IsWorldAgit()) {
            continue;
        }
        CWorldAgit* const worldAgit = residence->CastWorldAgit();
        CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
        if (auctionAgit == nullptr || auctionAgit->GetSaleState() != SALE_ACT) {
            continue;
        }

        if (count >= page * 8 - 8) {
            CResidenceData* const data = auctionAgit->m_data;
            const int residenceId = data != nullptr ? data->m_nResidenceID : 0;
            list += u"<tr><td width=50><font color=\"aaaaff\">";
            list += AgitLocationToken(residenceId);
            list += u"</font></td>";
            list +=
                u"<td width=100><a action=\"bypass _agitaucinfo_" + DecimalText(residenceId) + u"\"><font color=\"ffffaa\">";
            list += AgitNameToken(residenceId);
            list += u"[";
            list += DecimalText(auctionAgit->GetAuctionCount());
            list += u"]</font></a></td>";
            list += u"<td width=50>";
            std::tm* const endTime = runtime::crt::LocalTime32(auctionAgit->m_tAuctionEnd);
            list += Padded2(endTime->tm_year - 100);
            list += u"/";
            list += Padded2(endTime->tm_mon + 1);
            list += u"/";
            list += Padded2(endTime->tm_mday);
            list += u"</td><td width=70 align=right>"
                    u"<font color=\"aaffff\">";
            list += DecimalText(auctionAgit->GetMinBidPrice());
            list += u"</font></td> </tr>";
        }
        ++count;
    }

    list += u"</table>";
    list += u"<table border=0><tr>";
    if (count > 0) {
        if (page > 1) {
            list += u"<td ><button value=\"&$1037;\" "
                    u"action=\"bypass _agitauclist_" +
                DecimalText(page - 1) +
                u"\" back=\"L2ui_ch3.smallbutton2_down\" "
                u"width=\"65\" height=\"20\" "
                u"fore=\"L2ui_ch3.smallbutton2\"></td>";
        }
        list += u"<td ><button value=\"&$1038;\" "
                u"action=\"bypass _agitauclist_" +
            DecimalText(page + 1) +
            u"\" back=\"L2ui_ch3.smallbutton2_down\" "
            u"width=\"65\" height=\"20\" "
            u"fore=\"L2ui_ch3.smallbutton2\"></td>";
    }
    list += u"</tr></table>&nbsp;";

    AddStringMap(u"AGIT_LIST", list);
    AddStringMap(u"AGIT_LINK_BACK", u"bypass _agitdealer");
    SendHTML(CFHTML::MakeHTMLFromStrMap(u"agitauctionlist.htm", m_mapParam, user->m_data->m_CountryCode));
    return count;
}

void CAgitHTML::AgitAuctionInfo(const int residenceId) {
    // Current L2Server 0x0045cee8.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x35d)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x35d)
        L2_LOG_WARN(
            Log, "Cannot show clan-hall auction information: connection %u has no bound character; the page is not sent.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CAuctionAgit* const auctionAgit =
        residence != nullptr && residence->IsWorldAgit() ? static_cast<CWorldAgit*>(residence)->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }

    m_mapParam.clear();
    CResidenceData* const data = auctionAgit->m_data;
    CSPointer<CPledge> owner = CPledge::FindPledge(g_nWorldId, static_cast<int>(data->m_nResidenceOwnerID));
    AddStringMap(u"AGIT_NAME", AgitNameToken(data->m_nResidenceID));
    if (owner) {
        AddStringMap(u"OWNER_PLEDGE_NAME", std::u16string(owner->GetPledgeName()));
        AddStringMap(u"OWNER_PLEDGE_MASTER", std::u16string(owner->GetMasterName()));
    }
    AddStringMap(u"AGIT_SIZE", std::u16string(DecimalText(data->m_nAgitSize).c_str()));
    AddStringMap(u"AGIT_LEASE", std::u16string(DecimalText(data->m_nAgitLease).c_str()));
    AddStringMap(u"AGIT_LOCATION", std::u16string(AgitAuctionLocationToken(data->m_nResidenceID).c_str()));

    std::tm* const endTime = runtime::crt::LocalTime32(auctionAgit->m_tAuctionEnd);
    AddStringMap(u"AGIT_AUCTION_END_YY", std::u16string(Padded4(endTime->tm_year + 1900).c_str()));
    AddStringMap(u"AGIT_AUCTION_END_MM", std::u16string(Padded2(endTime->tm_mon + 1).c_str()));
    AddStringMap(u"AGIT_AUCTION_END_DD", std::u16string(Padded2(endTime->tm_mday).c_str()));
    AddStringMap(u"AGIT_AUCTION_END_HH", std::u16string(Padded2(endTime->tm_hour).c_str()));

    const std::int32_t remaining =
        runtime::SubtractInt32Wrapping(auctionAgit->m_tAuctionEnd, runtime::crt::Time32Now());
    AddStringMap(u"AGIT_AUCTION_REMAIN_HOUR", std::u16string(Padded2(remaining / 3600).c_str()));
    AddStringMap(u"AGIT_AUCTION_REMAIN_MINUTE", std::u16string(Padded2((remaining % 3600) / 60).c_str()));
    AddStringMap(u"AGIT_AUCTION_MINBID", std::u16string(DecimalText(auctionAgit->GetMinBidPrice()).c_str()));
    AddStringMap(u"AGIT_AUCTION_COUNT", std::u16string(DecimalText(auctionAgit->GetAuctionCount()).c_str()));
    AddStringMap(u"AGIT_AUCTION_DESC", WideToUtf16(auctionAgit->GetDesc()));
    AddStringMap(u"AGIT_LINK_BACK", u"bypass _agitauclist_1");
    AddStringMap(u"AGIT_LINK_BIDLIST", u"bypass _agitbidlist_" + DecimalText(residenceId) + u"_1_4");
    AddStringMap(u"AGIT_LINK_BID", u"bypass _agitbid1_" + DecimalText(residenceId) + u"_0");
    SendHTML(CFHTML::MakeHTMLFromStrMap(u"agitauctioninfo.htm", m_mapParam, user->m_data->m_CountryCode));
}

void CAgitHTML::AgitSaleInfo(const int residenceId, const int backMode) {
    // Current L2Server 0x0045d5f0.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x3d8)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x3d8)
        L2_LOG_WARN(
            Log, "Cannot show clan-hall sale information: connection %u has no bound character; the page is not sent.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x3d8)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x3d8)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"show clan-hall sale information", __FILE__, l2server::logging::AuctionLogContext::HtmlPage
        );
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CAuctionAgit* const auctionAgit =
        residence != nullptr ? static_cast<CWorldAgit*>(residence)->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }

    m_mapParam.clear();
    CResidenceData* const data = auctionAgit->m_data;
    AddStringMap(u"AGIT_NAME", AgitNameToken(data->m_nResidenceID));
    AddStringMap(u"AGIT_OWNER_PLEDGE_NAME", std::u16string(pledge->GetPledgeName()));
    AddStringMap(u"OWNER_PLEDGE_MASTER", std::u16string(pledge->GetMasterName()));
    AddStringMap(u"AGIT_SIZE", std::u16string(DecimalText(data->m_nAgitSize).c_str()));
    AddStringMap(u"AGIT_LEASE", std::u16string(DecimalText(data->m_nAgitLease).c_str()));
    AddStringMap(u"AGIT_LOCATION", std::u16string(AgitLocationToken(data->m_nResidenceID).c_str()));

    std::tm* const endTime = runtime::crt::LocalTime32(auctionAgit->m_tAuctionEnd);
    AddStringMap(u"AGIT_AUCTION_END_YY", std::u16string(Padded4(endTime->tm_year + 1900).c_str()));
    AddStringMap(u"AGIT_AUCTION_END_MM", std::u16string(Padded2(endTime->tm_mon + 1).c_str()));
    AddStringMap(u"AGIT_AUCTION_END_DD", std::u16string(Padded2(endTime->tm_mday).c_str()));
    AddStringMap(u"AGIT_AUCTION_END_HH", std::u16string(Padded2(endTime->tm_hour).c_str()));

    const std::int32_t remaining =
        runtime::SubtractInt32Wrapping(auctionAgit->m_tAuctionEnd, runtime::crt::Time32Now());
    AddStringMap(u"AGIT_AUCTION_REMAIN_HOUR", std::u16string(Padded2(remaining / 3600).c_str()));
    AddStringMap(u"AGIT_AUCTION_REMAIN_MINUTE", std::u16string(Padded2((remaining % 3600) / 60).c_str()));
    AddStringMap(u"AGIT_AUCTION_MINBID", std::u16string(DecimalText(auctionAgit->GetMinBidPrice()).c_str()));
    AddStringMap(u"AGIT_AUCTION_BIDCOUNT", std::u16string(DecimalText(auctionAgit->GetAuctionCount()).c_str()));
    AddStringMap(u"AGIT_AUCTION_DESC", WideToUtf16(auctionAgit->GetDesc()));

    std::wstring back;
    if (backMode == 1) {
        back = L"bypass _agitdealer";
    } else if (backMode == 2) {
        back = L"bypass _agitminfo";
    }
    AddStringMap(u"AGIT_LINK_BACK", WideToUtf16(back.c_str()));
    AddStringMap(u"AGIT_LINK_BIDLIST", u"bypass _agitbidlist_" + DecimalText(residenceId) + u"_1_3");
    AddStringMap(u"AGIT_LINK_CANCELAUCTION", u"bypass _agitsalecan");
    SendHTML(CFHTML::MakeHTMLFromStrMap(u"agitsaleinfo.htm", m_mapParam, user->m_data->m_CountryCode));
}

void CAgitHTML::AgitBidInfo(const int residenceId) {
    // Current L2Server 0x0045dc94.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x413)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x413)
        L2_LOG_WARN(
            Log, "Cannot show clan-hall bid information: connection %u has no bound character; the page is not sent.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x413)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x413)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"show clan-hall bid information", __FILE__, l2server::logging::AuctionLogContext::HtmlPage
        );
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CAuctionAgit* const auctionAgit =
        residence != nullptr ? static_cast<CWorldAgit*>(residence)->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }

    m_mapParam.clear();
    CResidenceData* const data = auctionAgit->m_data;
    AddStringMap(u"AGIT_NAME", AgitNameToken(data->m_nResidenceID));
    CSPointer<CPledge> owner = CPledge::FindPledge(g_nWorldId, static_cast<int>(data->m_nResidenceOwnerID));
    if (owner) {
        AddStringMap(u"OWNER_PLEDGE_NAME", std::u16string(owner->GetPledgeName()));
        AddStringMap(u"OWNER_PLEDGE_MASTER", std::u16string(owner->GetMasterName()));
    }
    const int pledgeDatabaseId = static_cast<int>(pledge->GetDbId());
    AddStringMap(u"AGIT_AUCTION_MYBID", std::u16string(DecimalText(auctionAgit->GetAuctionPrice(pledgeDatabaseId)).c_str()));
    AddStringMap(u"AGIT_SIZE", std::u16string(DecimalText(data->m_nAgitSize).c_str()));
    AddStringMap(u"AGIT_LEASE", std::u16string(DecimalText(data->m_nAgitLease).c_str()));
    AddStringMap(u"AGIT_LOCATION", std::u16string(AgitLocationToken(data->m_nResidenceID).c_str()));

    std::tm* const endTime = runtime::crt::LocalTime32(auctionAgit->m_tAuctionEnd);
    AddStringMap(u"AGIT_AUCTION_END_YY", std::u16string(Padded4(endTime->tm_year + 1900).c_str()));
    AddStringMap(u"AGIT_AUCTION_END_MM", std::u16string(Padded2(endTime->tm_mon + 1).c_str()));
    AddStringMap(u"AGIT_AUCTION_END_DD", std::u16string(Padded2(endTime->tm_mday).c_str()));
    AddStringMap(u"AGIT_AUCTION_END_HH", std::u16string(Padded2(endTime->tm_hour).c_str()));

    const std::int32_t remaining =
        runtime::SubtractInt32Wrapping(auctionAgit->m_tAuctionEnd, runtime::crt::Time32Now());
    AddStringMap(u"AGIT_AUCTION_REMAIN_HOUR", std::u16string(Padded2(remaining / 3600).c_str()));
    AddStringMap(u"AGIT_AUCTION_REMAIN_MINUTE", std::u16string(Padded2((remaining % 3600) / 60).c_str()));
    AddStringMap(u"AGIT_AUCTION_MINBID", std::u16string(DecimalText(auctionAgit->GetMinBidPrice()).c_str()));
    AddStringMap(u"AGIT_AUCTION_DESC", WideToUtf16(auctionAgit->GetDesc()));
    AddStringMap(u"AGIT_LINK_BIDLIST", u"bypass _agitbidlist_" + DecimalText(residenceId) + u"_1_4");
    AddStringMap(u"AGIT_LINK_CANCEL", u"bypass _agitbidcancel_" + DecimalText(residenceId));
    AddStringMap(u"AGIT_LINK_BACK", u"bypass _agitdealer");
    AddStringMap(u"AGIT_LINK_RE", u"bypass _agitbidre_" + DecimalText(residenceId) + u"_" + DecimalText(pledgeDatabaseId));
    SendHTML(CFHTML::MakeHTMLFromStrMap(u"agitbidinfo.htm", m_mapParam, user->m_data->m_CountryCode));
}

void CAgitHTML::AgitBid1ReReply(const int residenceId, const int alreadyChecked, const std::int64_t pledgeAdena) {
    // Current L2Server 0x0045e414.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x490)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x490)
        L2_LOG_WARN(
            Log,
            "Cannot apply replacement clan-hall bid preparation: connection %u has no bound character; the database reply "
            "is not applied.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x490)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x490)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"apply replacement clan-hall bid preparation", __FILE__,
            l2server::logging::AuctionLogContext::DatabaseReply
        );
        return;
    }
    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CAuctionAgit* const auctionAgit =
        residence != nullptr ? static_cast<CWorldAgit*>(residence)->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }
    if (alreadyChecked == 0 && !auctionAgit->CanAttend(static_cast<int>(pledge->GetDbId()), user)) {
        // Log.Add(LOG_ERROR, L"can not attend auction  agitid(%d) %s, %d", residenceId, L"AgitHtml.cpp", 0x49d)
        // Noise: The eligibility check already handles an ordinary auction refusal. No bid is submitted or updated here.
        // L2_LOG_ERROR(Log, L"can not attend auction  agitid(%d) %s, %d", residenceId, L"AgitHtml.cpp", 0x49d)
        return;
    }

    std::array<char16_t, 256> value{};
    m_mapParam.clear();
    value.fill(L'\0');
    static_cast<void>(FormatUtf16To(value.data(), value.size(), u"%I64d", static_cast<long long>(pledgeAdena)));
    AddStringMap(u"PLEDGE_ADENA", std::u16string(value.data()));
    value.fill(L'\0');
    static_cast<void>(
        FormatUtf16To(value.data(), value.size(), u"%I64d", static_cast<long long>(auctionAgit->GetMinBidPrice()))
    );
    AddStringMap(u"AGIT_AUCTION_MINBID", std::u16string(value.data()));
    value.fill(L'\0');
    static_cast<void>(FormatUtf16To(value.data(), value.size(), u"bypass %s", u"_agitaucmy"));
    AddStringMap(u"AGIT_LINK_BACK", std::u16string(value.data()));
    value.fill(L'\0');
    static_cast<void>(FormatUtf16To(value.data(), value.size(), u"Write %d %d 0 bidprice '' ''", 7, residenceId));
    AddStringMap(u"AGIT_LINK_NEXT", std::u16string(value.data()));
    SendHTML(CFHTML::MakeHTMLFromStrMap(u"agitbid1.htm", m_mapParam, user->m_data->m_CountryCode));
}

void CAgitHTML::AgitBid1Reply(const int residenceId, const int alreadyChecked, const std::int64_t pledgeAdena) {
    // Current L2Server 0x0045e7c8.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x4b4)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x4b4)
        L2_LOG_WARN(
            Log,
            "Cannot apply clan-hall bid preparation: connection %u has no bound character; the database reply is not "
            "applied.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x4b4)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x4b4)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"apply clan-hall bid preparation", __FILE__, l2server::logging::AuctionLogContext::DatabaseReply
        );
        return;
    }
    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CAuctionAgit* const auctionAgit =
        residence != nullptr ? static_cast<CWorldAgit*>(residence)->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }
    if (alreadyChecked == 0 && !auctionAgit->CanAttend(static_cast<int>(pledge->GetDbId()), user)) {
        // Log.Add(LOG_ERROR, L"can not attend auction  agitid(%d) %s, %d", residenceId, L"AgitHtml.cpp", 0x4c1)
        // Noise: The eligibility check already handles an ordinary auction refusal. No bid is submitted or updated here.
        // L2_LOG_ERROR(Log, L"can not attend auction  agitid(%d) %s, %d", residenceId, L"AgitHtml.cpp", 0x4c1)
        return;
    }

    std::array<char16_t, 256> value{};
    m_mapParam.clear();
    value.fill(L'\0');
    static_cast<void>(FormatUtf16To(value.data(), value.size(), u"%I64d", static_cast<long long>(pledgeAdena)));
    AddStringMap(u"PLEDGE_ADENA", std::u16string(value.data()));
    value.fill(L'\0');
    static_cast<void>(
        FormatUtf16To(value.data(), value.size(), u"%I64d", static_cast<long long>(auctionAgit->GetMinBidPrice()))
    );
    AddStringMap(u"AGIT_AUCTION_MINBID", std::u16string(value.data()));
    value.fill(L'\0');
    static_cast<void>(FormatUtf16To(value.data(), value.size(), u"bypass %s_%d", u"_agitaucinfo", residenceId));
    AddStringMap(u"AGIT_LINK_BACK", std::u16string(value.data()));
    value.fill(L'\0');
    static_cast<void>(FormatUtf16To(value.data(), value.size(), u"Write %d %d 0 bidprice '' ''", 7, residenceId));
    AddStringMap(u"AGIT_LINK_NEXT", std::u16string(value.data()));
    SendHTML(CFHTML::MakeHTMLFromStrMap(u"agitbid1.htm", m_mapParam, user->m_data->m_CountryCode));
}

void CAgitHTML::AgitBid2(const int residenceId, const std::int64_t price, const int minimumPrice) {
    // Current L2Server 0x0045eb80.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x4d7)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x4d7)
        L2_LOG_WARN(
            Log, "Cannot show clan-hall bid confirmation: connection %u has no bound character; the page is not sent.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    if (user->GetPledge() == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x4d7)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x4d7)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"show clan-hall bid confirmation", __FILE__, l2server::logging::AuctionLogContext::HtmlPage
        );
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CAuctionAgit* const auctionAgit =
        residence != nullptr ? static_cast<CWorldAgit*>(residence)->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }
    if (!user->IsPledgeMaster() && !user->HavePledgePower(PP_AGIT_AUCTION)) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_HALL_AUCTION_PARTICIPATION_REQUIREMENTS);
        return;
    }
    if (price < auctionAgit->GetMinBidPrice()) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::BID_MUST_EXCEED_MINIMUM);
        return;
    }
    if (minimumPrice > 0 && price < static_cast<std::int64_t>(minimumPrice)) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::BID_MUST_EXCEED_MINIMUM);
        return;
    }

    m_mapParam.clear();
    AddStringMap(u"AGIT_AUCTION_MINBID", std::u16string(DecimalText(auctionAgit->GetMinBidPrice()).c_str()));
    AddStringMap(u"AGIT_AUCTION_BID", std::u16string(DecimalText(price).c_str()));
    std::tm* const endTime = runtime::crt::LocalTime32(auctionAgit->m_tAuctionEnd);
    AddStringMap(u"AGIT_AUCTION_END_YY", std::u16string(Padded4(endTime->tm_year + 1900).c_str()));
    AddStringMap(u"AGIT_AUCTION_END_MM", std::u16string(Padded2(endTime->tm_mon + 1).c_str()));
    AddStringMap(u"AGIT_AUCTION_END_DD", std::u16string(Padded2(endTime->tm_mday).c_str()));
    AddStringMap(u"AGIT_AUCTION_END_HH", std::u16string(Padded2(endTime->tm_hour).c_str()));
    AddStringMap(u"AGIT_LINK_BACK", u"bypass _agitbid1_" + DecimalText(residenceId) + u"_0");
    std::array<char16_t, 256> doBidAction{};
    static_cast<void>(FormatUtf16To(
        doBidAction.data(), doBidAction.size(), u"bypass %s_%d_%I64d", u"_agitattact", residenceId,
        static_cast<long long>(price)
    ));
    AddStringMap(u"AGIT_LINK_DOBID", std::u16string(doBidAction.data()));
    SendHTML(CFHTML::MakeHTMLFromStrMap(u"agitbid2.htm", m_mapParam, user->m_data->m_CountryCode));
}

void CAgitHTML::AgitBidCancel(const int residenceId) {
    // Current L2Server 0x0045f064.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x582)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x582)
        L2_LOG_WARN(
            Log, "Cannot show clan-hall bid cancellation: connection %u has no bound character; the page is not sent.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x582)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x582)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"show clan-hall bid cancellation", __FILE__, l2server::logging::AuctionLogContext::HtmlPage
        );
        return;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CAuctionAgit* const auctionAgit =
        residence != nullptr ? static_cast<CWorldAgit*>(residence)->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return;
    }

    m_mapParam.clear();
    std::array<char16_t, 256> value{};
    const int pledgeDatabaseId = static_cast<int>(pledge->GetDbId());
    const std::int64_t bid = auctionAgit->GetAuctionPrice(pledgeDatabaseId);
    value.fill(L'\0');
    static_cast<void>(FormatUtf16To(value.data(), value.size(), u"%I64d", static_cast<long long>(bid)));
    AddStringMap(u"AGIT_BID", std::u16string(value.data()));
    value.fill(L'\0');
    const std::int64_t remainingBid = runtime::Win64CvttDoubleToInt64(static_cast<double>(bid) * 0.9);
    static_cast<void>(FormatUtf16To(value.data(), value.size(), u"%I64d", static_cast<long long>(remainingBid)));
    AddStringMap(u"AGIT_BID_REMAIN", std::u16string(value.data()));
    value.fill(L'\0');
    static_cast<void>(FormatUtf16To(value.data(), value.size(), u"bypass %s", u"_agitaucmy"));
    AddStringMap(u"AGIT_LINK_BACK", std::u16string(value.data()));
    value.fill(L'\0');
    const int cancelPledgeDatabaseId = static_cast<int>(pledge->GetDbId());
    const int auctionId = auctionAgit->m_nAuctionId;
    static_cast<void>(FormatUtf16To(
        value.data(), value.size(), u"bypass -h %s_%d_%d_%d", u"_agitbidcanact", residenceId, cancelPledgeDatabaseId,
        auctionId
    ));
    AddStringMap(u"AGIT_LINK_CANCEL", std::u16string(value.data()));
    SendHTML(CFHTML::MakeHTMLFromStrMap(u"agitbidcancel.htm", m_mapParam, user->m_data->m_CountryCode));
}

void CAgitHTML::AgitCommand(
    const int residenceId, const int context, const std::int64_t pledgeAdena, const int command, const int ignored
) {
    // Current L2Server 0x0045f408.
    L2_GUARD_FUNCTION();
    static_cast<void>(ignored);

    if (command == 1) {
        AgitSale1Reply(residenceId, context, pledgeAdena);
    } else if (command == 2) {
        AgitBid1Reply(residenceId, context, pledgeAdena);
    } else if (command == 3) {
        AgitBid1ReReply(residenceId, context, pledgeAdena);
    }
}

void CAgitHTML::AgitMaidInfo() {
    // Current L2Server 0x0045f508.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x147)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x147)
        L2_LOG_WARN(
            Log, "Cannot show the clan-hall management page: connection %u has no bound character; the page is not sent.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    if (user->GetPledge() == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x147)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x147)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"show the clan-hall management page", __FILE__, l2server::logging::AuctionLogContext::HtmlPage
        );
        return;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(1, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        return;
    }
    if (auctionAgit->GetSaleState() == SALE_ACT && auctionAgit->CanDoAuction()) {
        AgitSaleInfo(auctionAgit->m_data != nullptr ? auctionAgit->m_data->m_nResidenceID : 0, 1);
    } else {
        AgitInfo(2);
    }
}

bool CAgitHTML::SellActionReply(
    const int auctionId,
    const std::int64_t minimumBid,
    const int bidDay,
    wchar_t* const description,
    const int auctionEnd,
    const int success
) {
    // Current L2Server 0x0045f6c4.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x243)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x243)
        L2_LOG_WARN(
            Log,
            "Cannot apply the clan-hall listing result: connection %u has no bound character; the database reply is not "
            "applied.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return false;
    }
    if (user->GetPledge() == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x243)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x243)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"apply the clan-hall listing result", __FILE__, l2server::logging::AuctionLogContext::DatabaseReply
        );
        return false;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(2, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        return false;
    }
    auctionAgit->UnCheckSendable(1);
    if (success == 0) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::NOT_ENOUGH_ADENA_IN_CLAN_WAREHOUSE);
        return false;
    }

    auctionAgit->m_nAuctionId = auctionId;
    auctionAgit->SetSaleState(SALE_ACT);
    auctionAgit->StartAuctionTimer(auctionEnd);
    auctionAgit->SetAuctionDesc(description);
    auctionAgit->SetMinBidPrice(minimumBid);
    auctionAgit->SetBidDay(bidDay);
    AgitSaleInfo(auctionAgit->m_data != nullptr ? auctionAgit->m_data->m_nResidenceID : 0, 2);
    m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_HALL_AUCTION_REGISTERED);
    return true;
}

bool CAgitHTML::SaleCancelReply(const int ignored, const int lastCancel) {
    // Current L2Server 0x0045f8f8. The first reply dword is ignored.
    L2_GUARD_FUNCTION();
    static_cast<void>(ignored);

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x2a6)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x2a6)
        L2_LOG_WARN(
            Log,
            "Cannot apply clan-hall listing cancellation: connection %u has no bound character; the database reply is not "
            "applied.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return false;
    }
    if (user->GetPledge() == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x2a6)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x2a6)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"apply clan-hall listing cancellation", __FILE__,
            l2server::logging::AuctionLogContext::DatabaseReply
        );
        return false;
    }

    CWorldAgit* const worldAgit = GetPledgeWorldAgit(2, true);
    CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        return false;
    }
    auctionAgit->UnCheckSendable(2);
    if (auctionAgit->GetSaleState() != SALE_ACT) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_HALL_NOT_BEING_AUCTIONED);
        return false;
    }
    auctionAgit->ResetAuction();
    auctionAgit->SetAuctionLastCancel(lastCancel);
    AgitMaidInfo();
    return true;
}

void CAgitHTML::AuctionMine(const bool suppressNotFound) {
    // Current L2Server 0x0045fabc.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x39d)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x39d)
        L2_LOG_WARN(
            Log, "Cannot show the clan's halls and bids: connection %u has no bound character; the page is not sent.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_DOES_NOT_OWN_HALL);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x39d)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x39d)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"show the clan's halls and bids", __FILE__, l2server::logging::AuctionLogContext::HtmlPage
        );
        return;
    }

    CWorldAgit* const ownWorldAgit = GetPledgeWorldAgit(1, false);
    CAuctionAgit* const ownAuctionAgit = ownWorldAgit != nullptr ? ownWorldAgit->CastAuctionAgit() : nullptr;
    if (ownAuctionAgit == nullptr) {
        const int pledgeDatabaseId = static_cast<int>(pledge->GetDbId());
        for (const auto& entry : g_ResidenceDB.m_ResidenceIDMap) {
            CResidence* const residence = entry.second;
            if (residence == nullptr || !residence->IsWorldAgit()) {
                continue;
            }
            CWorldAgit* const worldAgit = residence->CastWorldAgit();
            CAuctionAgit* const auctionAgit = worldAgit != nullptr ? worldAgit->CastAuctionAgit() : nullptr;
            if (auctionAgit != nullptr && auctionAgit->IsInAuction(pledgeDatabaseId)) {
                AgitBidInfo(auctionAgit->m_data != nullptr ? auctionAgit->m_data->m_nResidenceID : 0);
                return;
            }
        }
        if (!suppressNotFound) {
            m_pUserSocket->SendSystemMessage(SystemMessageId::NO_OWNED_OR_BID_AUCTION_LOTS);
        }
        return;
    }

    if (ownAuctionAgit->GetSaleState() == SALE_ACT && ownAuctionAgit->CanDoAuction()) {
        AgitSaleInfo(ownAuctionAgit->m_data != nullptr ? ownAuctionAgit->m_data->m_nResidenceID : 0, 1);
    } else {
        AgitInfo(1);
    }
}

bool CAgitHTML::AgitBidReply(const int residenceId, const std::int64_t price, const int success, const int resultFlags) {
    // Current L2Server 0x0045fd68.
    L2_GUARD_FUNCTION();

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x53d)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x53d)
        L2_LOG_WARN(
            Log,
            "Cannot apply the clan-hall bid result: connection %u has no bound character; the database reply is not "
            "applied.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return false;
    }
    CPledge* const pledge = user->GetPledge();
    if (pledge == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x53d)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x53d)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"apply the clan-hall bid result", __FILE__, l2server::logging::AuctionLogContext::DatabaseReply
        );
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CAuctionAgit* const auctionAgit =
        residence != nullptr ? static_cast<CWorldAgit*>(residence)->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return false;
    }
    auctionAgit->UnCheckSendable(3);
    if (!user->IsPledgeMaster() && !user->HavePledgePower(PP_AGIT_AUCTION)) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_HALL_AUCTION_PARTICIPATION_REQUIREMENTS);
        return false;
    }
    if (price < auctionAgit->GetMinBidPrice()) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::BID_MUST_EXCEED_MINIMUM);
        return false;
    }

    const int pledgeDatabaseId = static_cast<int>(pledge->GetDbId());
    if (!auctionAgit->CanAttend(pledgeDatabaseId, user)) {
        // Log.Add(LOG_ERROR, L"can not attend auction  nId[%d], nPrice[%I64d], nSuccess[%d]", residenceId, price, success)
        // L2_LOG_ERROR(Log, L"can not attend auction  nId[%d], nPrice[%I64d], nSuccess[%d]", residenceId, price, success)
        // Noise: An unsuccessful bid reply followed by an eligibility refusal needs no duplicate warning. Keep a
        // reported-success/local-state mismatch visible.
        if (success != 0) {
            L2_LOG_WARN(
                Log,
                "Clan hall %d: database bid reply reports success %d for clan %u, amount %lld adena, but the current "
                "eligibility check failed; the reply is not applied to the local auction list.",
                residenceId, success, static_cast<unsigned>(pledgeDatabaseId), static_cast<long long>(price)
            );
        }
        return false;
    }

    int messageId = 0;
    if (success == 0) {
        messageId = (resultFlags & 1) != 0 ? SystemMessageId::NEW_BID_MUST_EXCEED_PREVIOUS_BID
                                           : SystemMessageId::NOT_ENOUGH_ADENA_IN_CLAN_WAREHOUSE;
    } else {
        auctionAgit->AddAuction(pledgeDatabaseId, price);
        messageId = SystemMessageId::CLAN_HALL_AUCTION_BID_PLACED;
    }
    m_pUserSocket->SendSystemMessage(messageId);
    AuctionMine(false);
    return true;
}

bool CAgitHTML::AgitBidCancelReply(const int residenceId, const int pledgeId, const std::int64_t ignored) {
    // Current L2Server 0x00460000. The incoming Q lane is unused.
    L2_GUARD_FUNCTION();
    static_cast<void>(ignored);

    User* const user = m_pUserSocket->GetUser();
    if (user == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x5c9)
        // L2_LOG_ERROR(Log, L"invalid user. %s, %d", L"AgitHtml.cpp", 0x5c9)
        L2_LOG_WARN(
            Log,
            "Cannot apply clan-hall bid cancellation: connection %u has no bound character; the database reply is not "
            "applied.",
            static_cast<unsigned>(m_pUserSocket->GetServerId())
        );
        return false;
    }
    if (user->GetPledge() == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        // Log.Add(LOG_ERROR, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x5c9)
        // Noise: Ordinary clanless page/request refusals are quiet. Unavailable clan data and unapplied database replies
        // remain visible. L2_LOG_ERROR(Log, L"invalid user[%s]'s pledge. %s, %d", user->GetName(), L"AgitHtml.cpp", 0x5c9)
        l2server::logging::ReportUnavailableAuctionClan(
            Log, *user, L"apply clan-hall bid cancellation", __FILE__, l2server::logging::AuctionLogContext::DatabaseReply
        );
        return false;
    }

    CResidence* const residence = g_ResidenceDB.GetResidence(residenceId);
    CAuctionAgit* const auctionAgit =
        residence != nullptr ? static_cast<CWorldAgit*>(residence)->CastAuctionAgit() : nullptr;
    if (auctionAgit == nullptr) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CANNOT_PARTICIPATE_IN_AUCTION);
        return false;
    }
    auctionAgit->UnCheckSendable(4);
    if (!user->IsPledgeMaster() && !user->HavePledgePower(PP_AGIT_AUCTION)) {
        m_pUserSocket->SendSystemMessage(SystemMessageId::CLAN_HALL_AUCTION_PARTICIPATION_REQUIREMENTS);
        return false;
    }
    auctionAgit->DelAuction(pledgeId);
    m_pUserSocket->SendSystemMessage(SystemMessageId::AUCTION_BID_CANCELLED);
    AuctionMine(true);
    return true;
}
