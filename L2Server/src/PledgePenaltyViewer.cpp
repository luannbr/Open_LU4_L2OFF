#include "PledgePenaltyViewer.h"

#include "Alliance.h"
#include "HtmlGenerator.h"
#include "ParseHtml.h"
#include "Pledge.h"
#include "User.h"

#include "Shared/src/SharedFactory.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

std::int32_t AddPenaltyTime(const std::uint32_t base, const std::int32_t seconds) noexcept {
    const std::uint32_t bits = base + static_cast<std::uint32_t>(seconds);
    std::int32_t result = 0;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

} // namespace

CPledgePenaltyViewer::CPledgePenaltyViewer(User* const user) : m_pUser(user) {
    // Current L2Server 0x009dc968.
}

CPledgePenaltyViewer::~CPledgePenaltyViewer() {
    // Current L2Server 0x009dc970.
    m_pUser = nullptr;
}

bool CPledgePenaltyViewer::CanAcceptNewPledgeMember() {
    // Current L2Server 0x009dc978.
    CPledge* const pledge = m_pUser->GetPledge();
    return pledge == nullptr || pledge->CanAcceptNewMember();
}

bool CPledgePenaltyViewer::CanCreatePledge() {
    // Current L2Server 0x009dc9a0.
    return m_pUser->GetPledge() != nullptr || m_pUser->CanCreatePledge();
}

bool CPledgePenaltyViewer::CanJoinAsAllianceMember() {
    // Current L2Server 0x009dc9d0.
    CPledge* const pledge = m_pUser->GetPledge();
    return pledge == nullptr || pledge->CanJoinAlliance();
}

bool CPledgePenaltyViewer::CanAcceptNewAllianceMember() {
    // Current L2Server 0x009dc9f8.
    CAlliance* const alliance = m_pUser->GetAlliance();
    return alliance == nullptr || alliance->CanAcceptNewMemberPledge();
}

bool CPledgePenaltyViewer::CanCreateAlliance() {
    // Current L2Server 0x009dca1c.
    CPledge* const pledge = m_pUser->GetPledge();
    return pledge == nullptr || pledge->CanCreateAlliance();
}

bool CPledgePenaltyViewer::IsWaitingToDismissPledge() {
    // Current L2Server 0x009dca44.
    CPledge* const pledge = m_pUser->GetPledge();
    return pledge != nullptr && pledge->IsWaitingToDismiss();
}

void CPledgePenaltyViewer::RenderCanNotJoinAsPledgeMemberPenalty(CHtmlGenerator& generator) {
    // Current L2Server 0x009dca6c.
    L2_GUARD_FUNCTION();
    CLocalHtmlGenerator local(generator.GetTemplet(1));
    const std::u16string formatted = CParseHTML::MakeFormatedDateLocal(
        AddPenaltyTime(m_pUser->GetPledgeOutTime(), BP::_PLEDGE_OUSTED_PENALTY_TIMEOUT), 6
    );
    const std::u16string hostFormatted(formatted);
    std::u16string dueDate = hostFormatted;
    local.AddValue(u"due_date", dueDate);
    generator.AddValue(u"UNABLE_TO_JOIN_PLEDGE", local);
}

void CPledgePenaltyViewer::RenderCanNotAcceptNewPledgeMemberPenalty(CHtmlGenerator& generator) {
    // Current L2Server 0x009dcc00.
    L2_GUARD_FUNCTION();
    std::uint32_t penaltyStart = 0;
    CPledge* const pledge = m_pUser->GetPledge();
    if (pledge != nullptr) {
        penaltyStart = pledge->GetOustTime();
    }
    CLocalHtmlGenerator local(generator.GetTemplet(2));
    const std::u16string formatted =
        CParseHTML::MakeFormatedDateLocal(AddPenaltyTime(penaltyStart, BP::_PLEDGE_OUST_PENALTY_TIMEOUT), 6);
    const std::u16string hostFormatted(formatted);
    std::u16string dueDate = hostFormatted;
    local.AddValue(u"due_date", dueDate);
    generator.AddValue(u"UNABLE_TO_ACCEPT_NEW_PLEDGE_MEMBER", local);
}

void CPledgePenaltyViewer::RenderCanNotCreatePledgePenalty(CHtmlGenerator& generator) {
    // Current L2Server 0x009dcdb0.
    L2_GUARD_FUNCTION();
    CLocalHtmlGenerator local(generator.GetTemplet(3));
    const std::u16string formatted = CParseHTML::MakeFormatedDateLocal(
        AddPenaltyTime(static_cast<std::uint32_t>(m_pUser->m_nPledgeDismissTime), BP::_PLEDGE_DISMISS_PENALTY_TIMEOUT), 6
    );
    const std::u16string hostFormatted(formatted);
    std::u16string dueDate = hostFormatted;
    local.AddValue(u"due_date", dueDate);
    generator.AddValue(u"UNABLE_TO_CREATE_PLEDGE", local);
}

void CPledgePenaltyViewer::RenderCanNotJoinAsAllianceMemberPenalty(CHtmlGenerator& generator) {
    // Current L2Server 0x009dcf48.
    L2_GUARD_FUNCTION();
    std::uint32_t penaltyStart = 0;
    CPledge* const pledge = m_pUser->GetPledge();
    if (pledge != nullptr) {
        penaltyStart = pledge->GetAllianceOutTime();
    }
    CLocalHtmlGenerator local(generator.GetTemplet(4));
    const std::u16string formatted =
        CParseHTML::MakeFormatedDateLocal(AddPenaltyTime(penaltyStart, BP::_ALLIANCE_OUSTED_PENALTY_TIMEOUT), 6);
    const std::u16string hostFormatted(formatted);
    std::u16string dueDate = hostFormatted;
    local.AddValue(u"due_date", dueDate);
    generator.AddValue(u"UNABLE_TO_JOIN_ALLIANCE", local);
}

void CPledgePenaltyViewer::RenderCanNotAcceptNewAllianceMember(CHtmlGenerator& generator) {
    // Current L2Server 0x009dd0f8.
    L2_GUARD_FUNCTION();
    std::uint32_t penaltyStart = 0;
    CAlliance* const alliance = m_pUser->GetAlliance();
    if (alliance != nullptr) {
        penaltyStart = alliance->m_data->m_nOustTime;
    }
    CLocalHtmlGenerator local(generator.GetTemplet(5));
    const std::u16string formatted =
        CParseHTML::MakeFormatedDateLocal(AddPenaltyTime(penaltyStart, BP::_ALLIANCE_OUST_PENALTY_TIMEOUT), 6);
    const std::u16string hostFormatted(formatted);
    std::u16string dueDate = hostFormatted;
    local.AddValue(u"due_date", dueDate);
    generator.AddValue(u"UNABLE_TO_ACCEPT_NEW_ALLIANCE_MEMBER", local);
}

void CPledgePenaltyViewer::RenderCanNotCreateAlliance(CHtmlGenerator& generator) {
    // Current L2Server 0x009dd2a0.
    L2_GUARD_FUNCTION();
    std::uint32_t penaltyStart = 0;
    CPledge* const pledge = m_pUser->GetPledge();
    if (pledge != nullptr) {
        penaltyStart = pledge->GetAllianceDismissTime();
    }
    CLocalHtmlGenerator local(generator.GetTemplet(6));
    const std::u16string formatted =
        CParseHTML::MakeFormatedDateLocal(AddPenaltyTime(penaltyStart, BP::_ALLIANCE_DISMISS_PENALTY_TIMEOUT), 6);
    const std::u16string hostFormatted(formatted);
    std::u16string dueDate = hostFormatted;
    local.AddValue(u"due_date", dueDate);
    generator.AddValue(u"UNABLE_TO_CREATE_ALLIANCE", local);
}

void CPledgePenaltyViewer::RenderWaitingToDismissPledge(CHtmlGenerator& generator) {
    // Current L2Server 0x009dd450.
    L2_GUARD_FUNCTION();
    std::uint32_t dismissTime = static_cast<std::uint32_t>(m_pUser->m_nPledgeDismissTime);
    CPledge* const pledge = m_pUser->GetPledge();
    if (pledge != nullptr) {
        dismissTime = pledge->GetDismissReservedTime();
    }
    CLocalHtmlGenerator local(generator.GetTemplet(7));
    const std::u16string formatted = CParseHTML::MakeFormatedDateLocal(static_cast<std::int32_t>(dismissTime), 6);
    const std::u16string hostFormatted(formatted);
    std::u16string dueDate = hostFormatted;
    local.AddValue(u"due_date", dueDate);
    generator.AddValue(u"WAITING_TO_DISMISS_PLEDGE", local);
}

void CPledgePenaltyViewer::RenderCanNotDismissPledgePenalty(CHtmlGenerator& generator) {
    // Current L2Server 0x009dd5f8.
    L2_GUARD_FUNCTION();
    std::uint32_t penaltyStart = static_cast<std::uint32_t>(m_pUser->GetLastDismissProposeTime());
    CPledge* const pledge = m_pUser->GetPledge();
    if (pledge != nullptr) {
        penaltyStart = pledge->GetLastDismissProposeTime();
    }
    CLocalHtmlGenerator local(generator.GetTemplet(8));
    const std::u16string formatted =
        CParseHTML::MakeFormatedDateLocal(AddPenaltyTime(penaltyStart, BP::_PLEDGE_DISMISS_PROPOSE_PENALTY_TIMEOUT), 6);
    const std::u16string hostFormatted(formatted);
    std::u16string dueDate = hostFormatted;
    local.AddValue(u"due_date", dueDate);
    generator.AddValue(u"UNABLE_TO_DISMISS_PLEDGE", local);
}

void CPledgePenaltyViewer::Show() {
    // Current L2Server 0x009dd7ac.
    L2_GUARD_FUNCTION();
    CHtmlGenerator generator(u"pledge_penalty.htm");

    bool hasPenalty = false;
    if (!m_pUser->CanJoinPledge()) {
        RenderCanNotJoinAsPledgeMemberPenalty(generator);
        hasPenalty = true;
    }
    if (!CanAcceptNewPledgeMember()) {
        RenderCanNotAcceptNewPledgeMemberPenalty(generator);
        hasPenalty = true;
    }
    if (!CanCreatePledge() && !IsWaitingToDismissPledge() && m_pUser->CanDismissPledge()) {
        hasPenalty = true;
        RenderCanNotCreatePledgePenalty(generator);
    }
    if (!CanJoinAsAllianceMember()) {
        hasPenalty = true;
        RenderCanNotJoinAsAllianceMemberPenalty(generator);
    }
    if (!CanAcceptNewAllianceMember()) {
        hasPenalty = true;
        RenderCanNotAcceptNewAllianceMember(generator);
    }
    if (!CanCreateAlliance()) {
        hasPenalty = true;
        RenderCanNotCreateAlliance(generator);
    }
    if (IsWaitingToDismissPledge()) {
        hasPenalty = true;
        RenderWaitingToDismissPledge(generator);
    }
    if (!m_pUser->CanDismissPledge() && !IsWaitingToDismissPledge()) {
        hasPenalty = true;
        RenderCanNotDismissPledgePenalty(generator);
    }

    if (!hasPenalty) {
        CLocalHtmlGenerator local(generator.GetTemplet(9));
        generator.AddValue(u"NO_PENALTY", local);
    }

    const std::u16string html = generator.Generate();
    const std::u16string hostHtml(html);
    m_pUser->ShowHTML(u"", hostHtml.c_str(), 0, HTML::LAUNCH, false);
}
