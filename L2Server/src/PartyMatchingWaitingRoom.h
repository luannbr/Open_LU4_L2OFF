#pragma once

#include "Shared/src/Runtime/TargetWideChar.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class User;

enum PartyMatchingRoomType : std::int32_t {
    PARTY_ROOM = 0,
    WAITING_ROOM = 1,
};


// Exact current L2Server PartyMatchingWaitingRoom.cpp filter. The current
// four-word xvector moves the donor's 0x68-byte class to 0x70 bytes while
// retaining every named field and its original access boundary.
class CPartyMatchingWaitingRoomListFilter {

public:
    CPartyMatchingWaitingRoomListFilter(
        User* user,
        int page,
        int lowerLevelLimit,
        int upperLevelLimit,
        PartyMatchingRoomType roomType,
        std::vector<int>& classList,
        const SharedUtilsWideChar* characterName
    );
    ~CPartyMatchingWaitingRoomListFilter() = default;

    bool IsBetweenLevelLimit(User* user);
    bool IsInPage(int index);
    User* GetUser();
    PartyMatchingRoomType GetRoomType();
    bool IsInClass(User* user);
    bool HasCharacterName(User* user);
    int GetCountPerPage();

private:
    void InitLevelLimits(int lowerLevelLimit, int upperLevelLimit);
    void SetLowerLevelLimit(int lowerLevelLimit);
    void SetUpperLevelLimit(int upperLevelLimit);
    void CheckLevelLimits();
    void SetClass(std::vector<int>& classList);
    void SetCharName(const SharedUtilsWideChar* characterName);

    User* m_pUser;
    std::int32_t m_nPage;
    std::int32_t m_nLowerLevelLimit;
    std::int32_t m_nUpperLevelLimit;
    PartyMatchingRoomType m_eRoomType;
    std::vector<int> m_lstClass;
    SharedUtilsWideChar m_sCharName[25];
};


// Exact current L2Server waiting-room owner. The target vectors have the
// four-word xvector envelope, so the current object is 0x58 bytes rather
// than the 0x50-byte TotalChecker2015 donor class.
class CPartyMatchingWaitingRoom {
public:
    CPartyMatchingWaitingRoom() = default;
    virtual ~CPartyMatchingWaitingRoom();

    bool EnterWaitingRoom(User* user);
    bool LeaveWaitingRoom(User* user);
    bool IsInWaitingRoom(User* user);
    bool SetNonInvitable(User* user);
    bool SetInvitable(User* user);
    bool IsInvitable(User* user);
    bool _IsInvitable(User* user);
    bool ListWaitingRoom(CPartyMatchingWaitingRoomListFilter& filter);

    CYieldLock m_lock;

private:
    std::vector<std::int32_t> m_waitingUserSidList;
    std::vector<std::int32_t> m_nonInvitableUserSidList;
};

extern CPartyMatchingWaitingRoom g_PartyMatchingWaitingRoom;
