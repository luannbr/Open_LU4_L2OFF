#pragma once

#include "Agit.h"

#include "Shared/src/ThreadSafeLibrary.h"

#include <cstddef>
#include <cstdint>

class CNpcBattleAgit final : public CWorldAgit {
public:
    CNpcBattleAgit();
    ~CNpcBattleAgit() override;

    L2_DECLARE_OBJECT_TYPE(CNpcBattleAgit)

    void TimerExpired(int id) override;
    void StartTimer() override;
    void StartSiege() override;
    void EndSiege() override;
    void PauseSiege() override;
    void ResumeSiege() override;

    bool IsNpcBattleAgit() const override;
    bool IsBattleTypeAgit() const override;
    CNpcBattleAgit* CastNpcBattleAgit() const override;
    void OnBossNpcEnterWorld(CNPC* npc) override;
    void OnBossNpcDead(CNPC* npc) override;

    CSPointer<CNPC> GetBossNpc();

    // Current ReplyContributionWinnerPledge writes this original public
    // owner directly at +0x498.
    std::int32_t m_nWinnerPledgeId; // +0x498

private:
    [[maybe_unused]] std::byte m_alignment49cTo4a0[0x04];
    CObjectStorage<CNPC> m_osBossNpc; // +0x4a0
};
