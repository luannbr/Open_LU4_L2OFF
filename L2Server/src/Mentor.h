#pragma once

#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CUserSocket;
class CDuelPlayerInfo;
class User;
class CPledgeGameUser;
template <std::size_t Capacity>
class CPacketHelperEx;

namespace MentorState {

enum LoginStatus : std::int32_t {
    LOGOUT = 0,
    LOGIN = 1,
};

} // namespace MentorState


struct MentorInfo {
    MentorInfo() : nCharDBId(0), nLevel(0), nClass(0), nLogin(MentorState::LOGOUT) { sCharName[0] = u'\0'; }

    std::int32_t nCharDBId;
    char16_t sCharName[25];
    std::int16_t nLevel;
    std::int16_t nClass;
    MentorState::LoginStatus nLogin;
};


class CMenteeWaitingListFilter {
public:
    CMenteeWaitingListFilter(User* user, int page, int levelMin, int levelMax) :
        m_pUser(user), m_nPage(page), m_nLevelMin(levelMin), m_nLevelMax(levelMax) {}

    User* GetUser() const { return m_pUser; }
    int GetCountPerPage() const { return 64; }
    int GetPage() const { return m_nPage; }
    bool IsBetweenLevel(User* user);
    bool IsInPage(int index);

private:
    User* m_pUser; // +0x00
    int m_nPage; // +0x08
    int m_nLevelMin; // +0x0c
    int m_nLevelMax; // +0x10
};


class CMenteeWaitingList {
public:
    CMenteeWaitingList() = default;
    ~CMenteeWaitingList() = default;

    bool CanAddWaitingList(User& user, bool unknown);
    bool AddWaitingList(User* user, bool unknown);
    bool IsInWaitingList(User* user);
    void DeleteWaitingList(User* user);
    void SendWaitingList(CMenteeWaitingListFilter& filter);
    void MakeWaitingListForBuilder();

private:
    ::CYieldLock m_lock; // +0x00
    ::std::vector<int> m_waitingSidList; // +0x10
};


extern CMenteeWaitingList g_MenteeWaitingList;

class CMentee {
public:
    CMentee() {
        for (char16_t& codeUnit : m_Mentor.sCharName) {
            codeUnit = u'\0';
        }
    }

    ~CMentee() = default;

    void Init(std::uint32_t userSId);
    int GetMentorDbId();
    wchar_t* GetMentorName();
    void SetMentor(MentorInfo& mentorInfo);
    void ShowMentorInfo(CUserSocket* socket);
    void SetLoginStateToMentor(MentorState::LoginStatus loginStatus);
    void SetMentorLoginStatus(MentorState::LoginStatus loginStatus);
    bool ResetMentor(bool notify);
    void UpdateMenteeStatus();
    void UpdateMentorStatus(User& user);
    void SendByLevelUp(bool graduate, int originalLevel);
    void SendGiftByMentee(int giftType, int originalLevel);
    void MakeMentorList(CPacketHelperEx<20480>& packet);
    bool IsMentorLogIn();
    void ActivateMenteeBuff();
    void ActivateMenteeBuffWithoutMentor();
    void DeleteMenteeBuff();
    void SubjobChange();
    void SomethingChange() { runtime::raw_atomic::IncrementI32(&m_nSomethingChanged); }
    bool CanBeBuffed();
    void OnTimerExpired();
    void SetMenteeStartTime(std::uint32_t time);
    std::uint32_t GetMenteeStartTime();
    void SetMentorLogoutTime(std::uint32_t time);
    std::uint32_t GetMentorLogoutTime();
    void SetMenteeBuffDeleted();

private:
    friend class CCreature;
    friend class CDuelPlayerInfo;
    friend class User;

    ::CYieldLock m_lockMentee; // +0x00
    MentorInfo m_Mentor; // +0x10
    volatile std::int32_t m_nInfectedSkillByMentor; // +0x50
    volatile std::int32_t m_nSomethingChanged; // +0x54
    std::uint32_t m_nMenteeSId; // +0x58
    std::uint32_t m_MenteeStartTime; // +0x5c
    std::uint32_t m_MentorLogoutTime; // +0x60
};


class CMentor {
public:
    using MenteeMap = ::std::map<std::uint32_t, MentorInfo>;

    CMentor();
    ~CMentor();

    void Init(std::uint32_t mentorSId);
    int GetPenaltyEndTimeLeft();
    int GetActiveMenteeCount();
    int GetPlayingMenteeCount();
    int _GetPlayingMenteeCount();
    bool CanAddMoreMentee();
    bool AddMentee(std::uint32_t menteeSId, MentorInfo& mentorInfo);
    bool DeleteMentee(std::uint32_t menteeSId, bool unknown);
    void ShowMenteeList(CUserSocket* socket);
    void SetLoginStateToMentee(MentorState::LoginStatus loginStatus);
    void SetMenteeLoginStatus(int menteeSId, MentorState::LoginStatus loginStatus);
    void UpdateMentorStatus();
    void UpdateMenteeStatus(User& user);
    int GetMenteeDBId(wchar_t* name);
    void MakeMenteeList(CPacketHelperEx<20480>& packet);
    const char16_t* GetMenteeName(int menteeSId);
    void SetPenaltyEnd(std::int32_t penaltyEnd);
    void SomethingChange() { runtime::raw_atomic::IncrementI32(&m_nSomethingChanged); }
    void SetMentorStartTime(std::uint32_t mentorStartTime);
    std::uint32_t GetMentorStartTime();
    void ActivateMentorBuff(int skillId);
    void DeleteMentorBuff();
    int CanGetBuff();
    void OnTimerExpired();
    bool IsMyMentee(int menteeSId);
    void SetMentorBuffDeleted();

private:
    friend class CDuelPlayerInfo;
    friend class User;
    friend class CPledgeGameUser;
    friend class WOlympiadUser;

    ::CYieldLock m_lockMentor;
    std::uint32_t m_nMentorSId;
    volatile std::int32_t m_nSomethingChanged;
    std::int32_t m_nCanGetBuff;
    volatile std::int32_t m_nInfectedSkillByMentee;
    MenteeMap m_mapMentees;
    std::int32_t m_tPenaltyEnd;
    std::uint32_t m_mentorStartTime;
};
