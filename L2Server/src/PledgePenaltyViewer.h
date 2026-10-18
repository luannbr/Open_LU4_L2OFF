#pragma once

#include <cstddef>

class CHtmlGenerator;
class User;

class CPledgePenaltyViewer final {
public:
    explicit CPledgePenaltyViewer(User* user);
    ~CPledgePenaltyViewer();

    void Show();

private:
    bool CanAcceptNewAllianceMember();
    bool CanAcceptNewPledgeMember();
    bool CanCreateAlliance();
    bool CanCreatePledge();
    bool CanJoinAsAllianceMember();
    bool IsWaitingToDismissPledge();

    void RenderCanNotAcceptNewAllianceMember(CHtmlGenerator& generator);
    void RenderCanNotAcceptNewPledgeMemberPenalty(CHtmlGenerator& generator);
    void RenderCanNotCreateAlliance(CHtmlGenerator& generator);
    void RenderCanNotCreatePledgePenalty(CHtmlGenerator& generator);
    void RenderCanNotDismissPledgePenalty(CHtmlGenerator& generator);
    void RenderCanNotJoinAsAllianceMemberPenalty(CHtmlGenerator& generator);
    void RenderCanNotJoinAsPledgeMemberPenalty(CHtmlGenerator& generator);
    void RenderWaitingToDismissPledge(CHtmlGenerator& generator);

    User* m_pUser; // +0x00
};
