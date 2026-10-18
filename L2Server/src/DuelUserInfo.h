#pragma once

#include "Duel.h"

#include <cstddef>
#include <cstdint>

class CDuel;
class CDuelPlayerInfo;
class User;

struct FVector;

class CDuelUserInfo {
public:
    struct DuelRecord {
        struct Record {
            Record() : m_nWinCount(0), m_nLoseCount(0), m_nDrawCount(0) {}

            void SetWin() { ++m_nWinCount; }

            void SetLose() { ++m_nLoseCount; }

            void SetDraw() { ++m_nDrawCount; }

            void Reset();
            void Update(int winCount, int loseCount, int drawCount);

            int m_nWinCount; // +0x00
            int m_nLoseCount; // +0x04
            int m_nDrawCount; // +0x08
        };

        DuelRecord() = default;

        void Update(
            int individualWinCount,
            int individualLoseCount,
            int individualDrawCount,
            int partyWinCount,
            int partyLoseCount,
            int partyDrawCount
        );

        Record m_IndividualRecord; // +0x00
        Record m_PartyRecord; // +0x0c
    };

    CDuelUserInfo();

    void SetDuelInfo(int duelSid, DuelTeamType team, CDuelPlayerInfo* duelPlayerInfo);
    void ResetDuelInfo();
    int GetDuelSid() { return m_nDuelSid; }
    CDuel* GetDuel() const;
    void SetDuelPlayerInfo(CDuelPlayerInfo* duelPlayerInfo);
    CDuelPlayerInfo* GetPartyDuelPlayerInfo();
    bool IsInDuel() const { return m_nDuelSid > 0; }
    bool IsInIndividualDuel() const;
    bool IsInPartyDuel() const;
    bool IsInFightStatus() const;
    bool IsDuelEffectOn(User* user) const;
    bool IsDuelDead(User* user);
    bool IsInPartyDuelReadyStatus() const;
    void SetTeam(DuelTeamType team);
    DuelTeamType GetTeam() const { return m_eDuelTeam; }
    DuelTeamType GetOpponentTeam() const;
    std::uint8_t GetTeamForEffect(User* user) const;
    bool IsEnemy(User* user);
    bool IsAlly(User* user);
    User* GetIndividualDuelEnemy();
    const FVector GetReturnPos();
    void GetOriginalStatus(double& hp, double& mp, double& cp);
    void GetSummonOriginalStatus(int summonId, double& hp, double& mp);
    void WinIndividual() { m_DuelRecord.m_IndividualRecord.SetWin(); }

    void LoseIndividual() { m_DuelRecord.m_IndividualRecord.SetLose(); }

    void DrawIndividual() { m_DuelRecord.m_IndividualRecord.SetDraw(); }

    void WinParty() { m_DuelRecord.m_PartyRecord.SetWin(); }

    void LoseParty() { m_DuelRecord.m_PartyRecord.SetLose(); }

    void DrawParty() { m_DuelRecord.m_PartyRecord.SetDraw(); }

    void UpdateDuelRecord(
        int individualWinCount,
        int individualLoseCount,
        int individualDrawCount,
        int partyWinCount,
        int partyLoseCount,
        int partyDrawCount
    );
    void SaveDuelRecord(User* user);

private:
    int m_nDuelSid; // +0x00
    DuelTeamType m_eDuelTeam; // +0x04
    CDuelPlayerInfo* m_pDuelPlayerInfo; // +0x08
    DuelRecord m_DuelRecord; // +0x10
};
