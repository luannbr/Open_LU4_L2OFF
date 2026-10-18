#pragma once

#include "PVPMatch.h"

#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class User;

class CCCRecordBoardData {
public:
    CCCRecordBoardData(std::int32_t userId, const char16_t* userName);
    ~CCCRecordBoardData();

    void Init(std::uint32_t userId, const char16_t* userName);
    void SetUserDieTime(std::uint32_t dieTime);
    std::uint32_t GetUserDieTime() const;
    void IncKillCount();
    void IncKillPoint(int point);
    void SetRank(int rank);

    std::uint32_t m_nUserId;
    std::uint32_t m_targetPadding4 = 0;
    std::u16string m_strUserName;
    std::int32_t m_nKillCount;
    std::int32_t m_nKillPoint;
    std::int32_t m_nRank;
    std::uint32_t m_dwRegTime;
    std::uint32_t m_dwDieTime;
    std::uint32_t m_targetPadding44 = 0;
};

class CCCRecordBoard {
public:
    using CCRecordBoardDataVec = std::vector<CCCRecordBoardData*>;

    struct GreaterKillPoints {
        bool operator()(const CCCRecordBoardData* left, const CCCRecordBoardData* right) const;
    };

    CCCRecordBoard();
    ~CCCRecordBoard();

    void Init();
    void AddNewRecordData(User* user);
    bool DeleteRecordData(User* user);
    int AddKillPoint(User* user, int point);
    CCCRecordBoardData* GetRecordData(User* user);
    void Summation();
    void SendCurrentRecordForSystemMessage(User* user);
    void SendCurrentRecordForUI(User* user, int type);
    int MakeCurrentRecordPacket(char* packet, int packetSize, int type);
    CCRecordBoardDataVec GetRecordBoardDataSorted();
    void SortRecordBoardData(CCRecordBoardDataVec& records);
    void _SortRecordBoardData(CCRecordBoardDataVec& records);

    CYieldLock m_Lock;
    std::int32_t m_nTotalKillPoint;
    std::uint32_t m_targetPadding14 = 0;
    CCRecordBoardDataVec m_vecUserRecordBoardData;
};

class CCrataeCube : public CPVPMatch2 {
public:
    using UserMapById = std::map<int, int>;

    static const int MAX_USER_COUNT;
    static const int PLAYING_DISTANCE;
    static const int PLAYING_TIME;
    static const int WAITING_TIME;

    explicit CCrataeCube(PVP_MATCH_TYPE type);
    ~CCrataeCube() override;

    bool IsCC() const override;
    CCrataeCube* CastCC() override;
    PVPMatchType::Enum GetPVPMatchType() const override;

    bool ReadyMatch() override;
    bool StartMatch() override;
    void TriggerEndMatch() override;
    void EndMatchByTimeOut() override;
    virtual void EndMatch(int reason);
    void OnNpcServerClosed() override;
    void AddKillPoint(User* user, int point) override;
    void UserDie(User* user, User* enemy) override;
    bool CanResurrectedByMatch(User* user) override;
    void TimerExpired(int id) override;

    virtual CCCRecordBoardData* GetRecordData(User* user);
    void SendCurrentRecordForSystemMessage(User* user) override;
    virtual void SendCurrentRecordForUIToAllUser(int type);
    void SendCurrentRecordForUIToOneUser(std::uint32_t userId, int type) override;
    void SendCurrentRecordForUI(User* user, int type) override;
    virtual bool RegisterResurrectionNPC(std::uint32_t npcId);
    void PostEndMatch() override;

    bool IsRegisteredUser(std::uint32_t userId) override;
    bool _IsRegisteredUser(std::uint32_t userId);
    bool IsCrataeCubeUser(User& user) override;
    int RegisterUser(std::uint32_t userId) override;
    bool UnregisterUser(std::uint32_t userId, int reason) override;
    bool _UnregisterUserById(std::uint32_t userId, int reason);
    int CheckRegisterUser(std::uint32_t userId) override;
    bool CheckRegisterLevel(int level) override;
    int GetRankUserPVPMatch(std::uint32_t userId) override;

    virtual bool _UnregisterUser(UserMapById::iterator position, int reason);

    UserMapById m_applicantListByUserId;
    UserMapById m_participantListByUserId;
    std::int32_t m_nStatus;

private:
    void RegisterAllUsersToRecordBoard();
    void _RegisterUserToRecordBoard(User* user);
    void IterationJob(bool combat);
    void ResurrectUser(User* user, FVector position);
    void RefreshUser(User* user);
    void SendEndMatchPacket();

    std::uint32_t m_nResurrectionNpcId;
    CCCRecordBoard m_RecordBoard;
};
