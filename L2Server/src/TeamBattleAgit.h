#pragma once

#include "Agit.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/ScopedLockGuards.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <utility>

class CDoor;
class CDB;
class CNPC;
class CTeamBattleFlag;
class CUserSocket;
class User;

bool BuilderCmd_show_castle_info(CUserSocket* socket, User* user, char16_t* command);
bool BuilderCmd_set_tb(CUserSocket* socket, User* user, char16_t* command);
bool BuilderCmd_view_tb(CUserSocket* socket, User* user, char16_t* command);

enum TEAM_BATTLE_COLOR : std::int32_t {
    TBC_NOT_DETERMINED = -1,
    TBC_RED = 0,
    TBC_BLUE = 1,
    TBC_GREEN = 2,
    TBC_YELLOW = 3,
    TBC_WHITE = 4,
    TBC_MAX = 5,
};

enum TEAM_BATTLE_SUBSTATUS : std::int32_t {
    TBSS_WAITING = 0,
    TBSS_PREPARE_BATTLE_ROYAL = 1,
    TBSS_BATTLE_ROYAL = 2,
    TBSS_PREPARE_FINAL = 3,
    TBSS_FINAL = 4,
};

// Current TeamBattleInfo is 0x30: the three scalar fields are followed by
// the 0x20-byte current MSVC map envelope at +0x10.
struct TeamBattleInfo {
    std::uint32_t nPledgeDbId = 0; // +0x00
    std::int32_t tPropose = 0; // +0x04
    std::int32_t nNpcType = 0; // +0x08
    std::byte m_alignment0c[0x04]{};
    std::map<std::uint32_t, std::int32_t> mapEntryPledgeMember; // +0x10
};


// Current allocation site 0x00c73a2c passes 0x6b0. The member offsets below
// are fixed by the current constructor and the inline lookup
// bodies at 0x008af9c0/0x008afaa8. The older TotalChecker 0x500 layout is not
// used here.
class CTeamBattleAgit : public CWorldAgit {
public:
    CTeamBattleAgit();
    ~CTeamBattleAgit() override;

    L2_DECLARE_OBJECT_TYPE(CTeamBattleAgit)

    void TimerExpired(int id) override;
    void StartSiege() override;
    void EndSiege() override;
    void PassSiege() override;
    void PauseSiege() override;
    void ResumeSiege() override;
    bool IsRegisteredForSiege(CPledge* pledge) const override;
    void OnStartInBattleField(User* user, FVector& position, bool& started) override;

    bool IsTeamBattleAgit() const override;
    bool IsBattleTypeAgit() const override;
    CTeamBattleAgit* CastTeamBattleAgit() const override;
    void StartTimer() override;
    void OnBossNpcEnterWorld(CNPC* npc) override;
    void OnBossNpcDead(CNPC* npc) override;

    int GetPledgeRegisterStatusForNpcServer(int pledgeDatabaseId);
    TEAM_BATTLE_COLOR GetColor(int pledgeDatabaseId, bool& alreadyRegistered);
    FVector GetChallengerRestartPoint(int pledgeDatabaseId);
    FVector GetBanishPoint(User* user, bool useOwnerRestart, std::set<std::uint32_t>& masterEntries);
    void GetMasterEntry(std::set<std::uint32_t>& entries);
    void GetTeamBattleEntry(std::set<std::uint32_t>& entries, bool challengersOnly);
    bool IsBattleRoyalEntryMember(std::uint32_t pledgeDatabaseId, std::uint32_t userDatabaseId);
    bool IsMasterEntryMember(std::uint32_t userDatabaseId);
    int GetBattleRoyalEntryMemberCount(std::uint32_t pledgeDatabaseId) {
        // Current compiled header body 0x0089ce20.
        int count = -1;
        {
            L2_GUARD(m_TeamBattleSect);
            for (const TeamBattleInfo& entry : m_TeamBattleEntryInfo) {
                if (entry.nPledgeDbId == pledgeDatabaseId) {
                    count = static_cast<int>(entry.mapEntryPledgeMember.size());
                    break;
                }
            }
        }
        return count;
    }
    int TestRegisterTeamBattleEntryMember(std::uint32_t pledgeDatabaseId, std::uint32_t userDatabaseId) {
        // Current compiled header body 0x008afb48.
        if (pledgeDatabaseId == 0) {
            return -3;
        }

        bool foundPledge = false;
        bool foundMember = false;
        bool memberLimitReached = false;
        {
            L2_GUARD(m_TeamBattleSect);
            if (m_TeamBattleMasterInfo.nPledgeDbId == pledgeDatabaseId) {
                foundPledge = true;
                foundMember = m_TeamBattleMasterInfo.mapEntryPledgeMember.find(userDatabaseId) !=
                    m_TeamBattleMasterInfo.mapEntryPledgeMember.end();
                memberLimitReached = m_TeamBattleMasterInfo.mapEntryPledgeMember.size() >= 0x12;
            }
            if (!foundPledge) {
                for (const TeamBattleInfo& entry : m_TeamBattleEntryInfo) {
                    if (entry.nPledgeDbId == pledgeDatabaseId) {
                        foundPledge = true;
                        foundMember = entry.mapEntryPledgeMember.find(userDatabaseId) != entry.mapEntryPledgeMember.end();
                        memberLimitReached = entry.mapEntryPledgeMember.size() >= 0x12;
                        break;
                    }
                }
            }
        }

        if (!foundPledge) {
            return -3;
        }
        if (foundMember) {
            return -6;
        }
        return memberLimitReached ? -2 : 0;
    }
    int GetNpcType(std::uint32_t pledgeDatabaseId) {
        // Current compiled header body 0x0089a170.
        if (pledgeDatabaseId == 0) {
            return -1;
        }

        int npcType = -1;
        {
            L2_GUARD(m_TeamBattleSect);
            if (m_TeamBattleMasterInfo.nPledgeDbId == pledgeDatabaseId) {
                npcType = m_TeamBattleMasterInfo.nNpcType;
            }
            if (npcType == -1) {
                for (const TeamBattleInfo& entry : m_TeamBattleEntryInfo) {
                    if (entry.nPledgeDbId == pledgeDatabaseId) {
                        npcType = entry.nNpcType;
                        break;
                    }
                }
            }
        }
        return npcType;
    }
    int SetNpcType(std::uint32_t pledgeDatabaseId, int npcType) {
        // Current compiled header body 0x0089a220.
        if (pledgeDatabaseId == 0 || npcType < 0) {
            return -1;
        }

        int result = -1;
        {
            L2_GUARD(m_TeamBattleSect);
            if (m_TeamBattleMasterInfo.nPledgeDbId == pledgeDatabaseId) {
                m_TeamBattleMasterInfo.nNpcType = npcType;
                result = npcType;
            }
            if (result == -1) {
                for (TeamBattleInfo& entry : m_TeamBattleEntryInfo) {
                    if (entry.nPledgeDbId == pledgeDatabaseId) {
                        entry.nNpcType = npcType;
                        result = npcType;
                        break;
                    }
                }
            }
        }
        return result;
    }
    int GetMasterPledgeDbId() const { return static_cast<int>(m_TeamBattleMasterInfo.nPledgeDbId); }
    int GetEntryPledgeDbId(TEAM_BATTLE_COLOR color) const {
        return static_cast<int>(m_TeamBattleEntryInfo[static_cast<int>(color)].nPledgeDbId);
    }
    void SetSubStatus(const TEAM_BATTLE_SUBSTATUS status) { m_SubStatus = status; }
    int GetBattleRoyalEntryCount() {
        // Current compiled header body 0x006a2e10.
        const int count = [&]() -> int {
            L2_GUARD(m_TeamBattleSect);
            int count = 0;
            for (const TeamBattleInfo& entry : m_TeamBattleEntryInfo) {
                if (entry.nPledgeDbId != 0) {
                    ++count;
                }
            }
            return count;
        }();
        return count;
    }
    int AddEntryPledge(std::uint32_t pledgeDatabaseId, std::int32_t proposeTime, TEAM_BATTLE_COLOR color, int npcType);
    bool RemoveEntryPledge(std::uint32_t pledgeDatabaseId);
    bool RemoveEntryPledgeMember(std::uint32_t pledgeDatabaseId, std::uint32_t userDatabaseId);
    int AddEntryPledgeMember(std::uint32_t userDatabaseId, std::uint32_t pledgeDatabaseId, std::int32_t proposeTime);
    void SendWaitTimeMessage(std::int32_t now);
    void SendElapsedTimeMessage(std::int32_t now, std::int32_t endTime);
    void WaitBattleRoyal();
    void StartBattleRoyal();
    void WaitFinal();
    void StartFinal();

    std::vector<CTeamBattleFlag*> m_lstTeamBattleObject; // +0x498
    std::vector<std::pair<FVector, FAngle>> m_FinalFlagList; // +0x4b8
    std::vector<std::pair<FVector, FAngle>> m_BattleRoyalFlagList; // +0x4d8
    std::set<CDoor*> m_setFinalOpenDoor; // +0x4f8

private:
    friend class CDB;
    friend bool BuilderCmd_show_castle_info(CUserSocket* socket, User* user, char16_t* command);
    friend bool BuilderCmd_set_tb(CUserSocket* socket, User* user, char16_t* command);
    friend bool BuilderCmd_view_tb(CUserSocket* socket, User* user, char16_t* command);

    bool IsBattleRoyalEntryPledge(std::uint32_t pledgeDatabaseId) const;
    void BanBattleRoyalLoser(std::uint32_t pledgeDatabaseId);
    void BanExceptTeamBattleEntry(bool battleRoyal);

    CYieldLock m_TeamBattleSect; // +0x518
    TEAM_BATTLE_COLOR m_Color[TBC_MAX]; // +0x528
    std::byte m_alignment53c[0x04]{};
    std::set<CNPC*> m_setNpcInBattle; // +0x540
    TEAM_BATTLE_SUBSTATUS m_SubStatus = TBSS_WAITING; // +0x560
    std::byte m_alignment564[0x04]{};
    TeamBattleInfo m_TeamBattleMasterInfo; // +0x568
    TeamBattleInfo m_TeamBattleEntryInfo[TBC_MAX]; // +0x598
    std::int32_t m_nWinnerPledgeId = 0; // +0x688
    std::int32_t m_nBattleRoyalTimeout = 0; // +0x68c
    std::int32_t m_nWaitingTimeout = 0; // +0x690
    std::int32_t m_nBattleRoyalBeginTime = 0; // +0x694
    std::int32_t m_nWaitingBattleRoyalBeginTime = 0; // +0x698
    std::int32_t m_nWaitingFinalBeginTime = 0; // +0x69c
    std::int32_t m_nFinalTimeout = 0; // +0x6a0
    std::int32_t m_nFinalBeginTime = 0; // +0x6a4
    std::int32_t m_tPaused = 0; // +0x6a8
    std::byte m_alignment6ac[0x04]{};
};
