#pragma once

#include "Residence.h"

#include <cstddef>
#include <cstdint>

namespace NameValueForAgit {

extern int SUCCESS_FROM_NPC;
extern int SUCCESS_FROM_PC;
extern int SUCCESS_DEFENCE;
extern int FAILED_DEFENCE;

} // namespace NameValueForAgit

// Current L2Server CWorldAgit extends the exact CResidence envelope by the
// parent-castle pointer at +0x490. Its introduced virtual range is
// +0x140..+0x1c0.
class CWorldAgit : public CResidence {
public:
    static void InitWorldAgit();

    CWorldAgit();
    ~CWorldAgit() override;

    L2_DECLARE_OBJECT_TYPE(CWorldAgit)

    bool IsWorldAgit() const override;
    CWorldAgit* CastWorldAgit() const override;

    virtual bool IsAuctionAgit() const;
    virtual bool IsTeamBattleAgit() const;
    virtual bool IsNpcBattleAgit() const;
    virtual bool IsSiegeAgit() const;
    virtual bool IsMiniGameAgit() const;
    virtual bool IsCompetitionAgit() const;
    virtual bool IsBaseCampAgit() const;
    virtual bool IsBattleTypeAgit() const;
    virtual CAuctionAgit* CastAuctionAgit() const;
    virtual CTeamBattleAgit* CastTeamBattleAgit() const;
    virtual CNpcBattleAgit* CastNpcBattleAgit() const;
    virtual CSiegeAgit* CastSiegeAgit() const;
    virtual CMiniGameAgit* CastMiniGameAgit() const;
    virtual CCompeteAgit* CastCompeteAgit() const;
    virtual CBaseCampAgit* CastBaseCampAgit() const;
    virtual void OnBossNpcEnterWorld(CNPC* npc);
    virtual void OnBossNpcDead(CNPC* npc);

    void Set(int ownerPledgeId, int residenceState, int nextSiege);
    void TimerExpired(int id) override;
    void OnReplyLoadAgitPacket();
    std::int32_t GetAgitFirstSiegeDate();
    void StartTimer() override;
    void StartSiege() override;
    void EndSiege() override;
    void PassSiege() override;
    void ChangeOwner(CPledge* pledge, int reason, bool announce) override;
    void PauseSiege() override;
    void ResumeSiege() override;
    void ExpelNonPledgeMember() override;

    void UpdatePledgeNameValue(int previousOwnerPledgeId, CPledge* pledge);
    void OpenAllDoor();
    void CloseAllDoor();
    CCastle* GetParentCastle() const;
    void SetParentCastle();

private:
    CCastle* m_pParentCastle; // +0x490
};
