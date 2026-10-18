#pragma once

#include "Agit.h"
#include "Castle.h"
#include "Shared/src/TemplateHelper.h"

#include "Shared/src/StandardContainers.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class CNPC;
class CDB;
class CPledge;
class CUserSocket;
class User;


// Current allocation site 0x00c73b34 passes 0xa510. In particular, the
// attacker map and lock are fixed by IsAttackerPledge@0x00a60488.
class CSiegeAgit : public CWorldAgit {
public:
    CSiegeAgit();
    ~CSiegeAgit() override;

    L2_DECLARE_OBJECT_TYPE(CSiegeAgit)

    void TimerExpired(int id) override;
    void StartSiege() override;
    void EndSiege() override;
    void PassSiege() override;
    void PauseSiege() override;
    void ResumeSiege() override;
    bool IsRegisteredForSiege(CPledge* pledge) const override;
    void OnStartInBattleField(User* user, FVector& position, bool& started) override;

    bool IsSiegeAgit() const override;
    bool IsBattleTypeAgit() const override;
    CSiegeAgit* CastSiegeAgit() const override;
    void StartTimer() override;
    void OnBossNpcEnterWorld(CNPC* npc) override;
    void OnBossNpcDead(CNPC* npc) override;

    bool IsAttackerPledge(CPledge* pledge);
    int GetAttackerCount();
    void RegisterAttackerPledge(const std::uint32_t pledgeDatabaseId, const std::int32_t score) {
        // Current compiled header body 0x006c3148.
        {
            L2_GUARD(m_SiegeAgitSect);
            m_mapAttackerPledge[pledgeDatabaseId] = score;
            m_bAttackerListCached = false;
        }
    }
    void UnregisterAttackerPledge(const std::uint32_t pledgeDatabaseId) {
        // Current compiled header body 0x006c32b0.
        {
            L2_GUARD(m_SiegeAgitSect);
            m_mapAttackerPledge.erase(pledgeDatabaseId);
            m_bAttackerListCached = false;
        }
    }
    void SendCastleSiegeInfo(CUserSocket* socket, User* user);
    void SendSiegeAttackerList(CUserSocket* socket, User* user);
    void SendSiegeDefenderList(CUserSocket* socket, User* user);
    CSPointer<CNPC> GetBossNpc();

private:
    friend class CDB;

    void FreeSiegePledgeList();
    void SendElapsedTimeMessage(std::int32_t now, std::int32_t endTime);
    void GetAttackerPledge(std::vector<CPledge*>& pledges, std::vector<int>& scores);

    std::map<std::uint32_t, std::int32_t> m_mapAttackerPledge; // +0x498
    CYieldLock m_SiegeAgitSect; // +0x4b8

public:
    // Current ReplyContributionWinnerPledge writes this original public
    // owner directly at +0x4c8.
    std::int32_t m_nWinnerPledgeId; // +0x4c8

private:
    std::byte m_alignment4cc[0x04];
    CObjectStorage<CNPC> m_osBossNpc; // +0x4d0
    bool m_bAttackerListCached; // +0x4d8
    std::byte m_alignment4d9[0x03];
    std::int32_t m_tAttackerListLastCached; // +0x4dc
    std::int32_t m_nAttackerListTotalPage; // +0x4e0
    BufForCaching m_bufForAttackerListCaching[5]; // +0x4e4
    std::byte m_alignmenta50c[0x04];
};
