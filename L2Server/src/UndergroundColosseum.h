#pragma once

#include "PVPMatch.h"

#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <functional>

class CParty;
class User;

enum UCMaxParty : std::int32_t {
    UCM_BY_BUILDER = 2,
    UCM_BY_NPC = 4,
};

class CUCRecordBoardData {
public:
    CUCRecordBoardData(std::int32_t userId, const char16_t* userName);
    ~CUCRecordBoardData();

    void Init(std::uint32_t userId, const char16_t* userName);
    void SetUserDieTime(std::uint32_t dieTime);
    std::uint32_t GetUserDieTime() const;
    void IncDeathCount();
    void IncKillCount();

    std::uint32_t m_nUserId;
    std::uint32_t m_targetPadding4 = 0;
    std::u16string m_strUserName;
    std::int32_t m_nKillCount;
    std::int32_t m_nDeathCount;
    std::uint32_t m_dwDieTime;
    std::uint32_t m_targetPadding3c = 0;
};

class CUCRecordBoard {
public:
    using UCRecordBoardDataVec = std::vector<CUCRecordBoardData*>;

    CUCRecordBoard();
    ~CUCRecordBoard();

    void Init();
    void AddNewRecordData(int team, User* user);
    void UserDie(User* user, User* enemy);
    void OnHandOverPartyMaster(int team, User* user);
    CUCRecordBoardData* GetRecordData(User* user);
    void Summation();
    int GetWinnerTeamIndex();
    void SendCurrentRecordForSystemMessage(User* user);
    void SendCurrentRecordForUI(User* user, int type);
    int MakeCurrentRecordPacket(char* packet, int packetSize, int type, int first, int second);

    UCRecordBoardDataVec m_vecBlueTeam;
    UCRecordBoardDataVec m_vecRedTeam;
    std::int32_t m_nBlueTeamTotalKillCount;
    std::int32_t m_nRedTeamTotalKillCount;
    std::int32_t m_nBlueTeamTotalDeathCount;
    std::int32_t m_nRedTeamTotalDeathCount;
};

class CUndergroundColosseum : public CPVPMatch2 {
public:
    using ObserverSetType = std::set<std::uint32_t, std::less<std::uint32_t>>;

    explicit CUndergroundColosseum(PVP_MATCH_TYPE type);
    ~CUndergroundColosseum() override;

    bool IsUC() const override;
    CUndergroundColosseum* CastUC() override;
    PVPMatchType::Enum GetPVPMatchType() const override;

    virtual PVPMatchResult CheckRegisterParty(std::uint32_t partyId);
    virtual bool StartMatchWithParty(std::uint32_t bluePartyId, std::uint32_t redPartyId);
    bool StartMatchByBuilder();
    void EndMatchByTimeOut() override;
    virtual void EndMatch(std::uint32_t bluePartyId, std::uint32_t redPartyId, int reason);

    int GetRegisteredPartiesPacket(char* packet, int packetSize);
    bool IsRegisteredParty(std::uint32_t partyId);
    int GetRegisteredPartyIndex(std::uint32_t partyId);
    PVPMatchResult RegisterParty(UCMaxParty maxParty, std::uint32_t partyId);
    void UnregisterParty(std::uint32_t partyId, int reason);
    void OnDismissParty(CParty* party);
    void OnHandOverPartyMaster(CParty* party, User* newMaster);
    void ValidateParties();

    void UserDie(User* user, User* enemy) override;
    bool CanResurrectedByMatch(User* user) override;
    void TimerExpired(int id) override;

    virtual CUCRecordBoardData* GetRecordData(User* user);
    void SendCurrentRecordForSystemMessage(User* user) override;
    virtual void SendSimpleRecordToAllUser();
    virtual void SendCurrentRecordForUIToAllUser(int type, std::uint32_t bluePartyId, std::uint32_t redPartyId);
    void SendCurrentRecordForUI(User* user, int type) override;
    bool IsPVPMatchRelateNpc(CNPC* npc) override;
    int GetPVPMatchTeamIndex(CNPC* npc) override;

    virtual bool RegisterResurrectionTower(std::uint32_t team, std::uint32_t npcId);
    virtual FVector GetResPoint(int team);

    static bool IsPartyPVPPlayer(User& user);
    void UnregisterAll();
    void RegisterObserve(User& user);
    void UnregisterObserve(User& user);
    bool IsForEventMatch();

protected:
    void PostEndMatch() override;
    bool CheckRegisterLevel(int level) override;
    bool CheckDistance(FVector& first, FVector& second, int distance) override;
    virtual bool CheckBannedUser(User* user);
    void GivePVPPointToWinnerParty(std::uint32_t partyId) override;

private:
    void RegisterTeamToRecordBoad(int team);
    void CheckResurrection(int team);
    void ResurrectUser(User* user, FVector position);
    void RefreshUser(User* user);
    bool CheckAllMemberDied(int team);
    void SendEndMatchPacket();

    std::uint32_t m_nBlueTeamResurrectionTowerId;
    std::uint32_t m_nRedTeamResurrectionTowerId;
    CUCRecordBoard m_RecordBoard;
    ObserverSetType m_ObserverList;
};
