#pragma once

#include "Shared/src/IOC.h"
#include "Shared/src/PledgeType.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CPledge;
class User;

// Current PledgeJoinSystem.cpp process-lifetime owner. The seven target
// maps are the current 0x20 MSVC carriers (instead of the older donor's
// 0x10 trees), followed by seven 0x38 Shared RWLocks and m_nInitTime.
class CPledgeJoinSystem : public CIOObject {
public:
    using TimeMap = ::std::map<int, std::int32_t>;

    L2_DECLARE_OBJECT_REGISTRATION(CPledgeJoinSystem)

    CPledgeJoinSystem();
    ~CPledgeJoinSystem() override;

    CPledgeJoinSystem(const CPledgeJoinSystem&) = delete;
    CPledgeJoinSystem& operator=(const CPledgeJoinSystem&) = delete;

    static CPledgeJoinSystem* Inst();

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void Init();
    void SetDelTime(int days);
    void InitPledgeInfoPerDay();
    void RequestModifyPledgeInfo(CPledge* pledge);
    void RequestSearchPledgeRecruitBoard(
        User* user,
        const char16_t* pledgeName,
        int minimumLevel,
        int maximumLevel,
        const char16_t* masterName,
        int pledgeType,
        int sortCondition,
        int orderCondition,
        int searchCondition
    );
    void RequestPledgeOnPledgeRecruitBoard(User* user, int pledgeId);
    void RequestPledgeWaitingUserList(User* user, int pledgeId);
    void RequestUserOnPledgeWaitingList(User* user, int pledgeId, int characterId);
    void RequestPledgeDraftUserList(
        User* user, int minimumLevel, int maximumLevel, int classGroup, const char16_t* name, int sortCondition, int orderCondition
    );
    void RequestModifyPledgeRecruitBoard(
        User* user, int recruitType, const char16_t* title, const char16_t* description, int sortCondition, PledgeType pledgeType
    );
    void RequestWritePledgeRecruitBoard(
        User* user, int recruitType, const char16_t* title, const char16_t* description, int sortCondition, PledgeType pledgeType
    );
    void RequestDeletePledgeRecruitBoard(User* user, bool fromDatabase);
    void RequestDeletePledgeRecruitBoard(CPledge* pledge, bool fromDatabase);
    void RequestWritePledgeWaitingUserList(User* user, int pledgeId, const char16_t* comment);
    void RequestDeletePledgeWaitingUserList(User* user, bool fromDatabase);
    void RequestDeletePledgeWaitingUserList(const char16_t* userName, bool fromDatabase);
    void RequestWriteDraftUserList(User* user, int value);
    void RequestDeleteDraftUserList(User* user);
    void RequestPledgeJoinRemainTime(User* user);
    void RequestRejectPledgeWiatingUser(User* user, int worldId, int characterId);
    void SetNotifyTable(int pledgeDatabaseId, std::int32_t notifyTime);
    std::int32_t GetNotifyTable(int pledgeDatabaseId);
    void SendRemainTimeOnPledge(User* user, int remainingTime);
    void SendRemainTimeOnUser(User* user, int type, int remainingTime);
    void ClearPledgeRecruitTimePenalty(int key);
    void ClearDraftTimePenalty(int key);
    void ClearWaitTimePenalty(int key);
    void ClearUserTimePenalty(int userDatabaseId, int pledgeDatabaseId);
    void ClearUserListByPledgeJoin(User* user);

private:
    void ClearTimeTable(int key, TimeMap* table, RWLock& lock);
    bool ConfirmTimePenalty(int key, TimeMap* table);
    std::int32_t GetRemainTime(int key, TimeMap* table);
    void SetTimeTable(int key, TimeMap* table, RWLock& lock);

    TimeMap m_mapDraftUser; // +0x018
    TimeMap m_mapPledgeBoard; // +0x038
    TimeMap m_mapWaitingUser; // +0x058
    TimeMap m_mapPledgeNotifyTable; // +0x078
    TimeMap m_mapPledgeDel; // +0x098
    TimeMap m_mapWaitDel; // +0x0b8
    TimeMap m_mapDraftDel; // +0x0d8
    RWLock m_draftLock; // +0x0f8
    RWLock m_pledgeLock; // +0x130
    RWLock m_waitLock; // +0x168
    RWLock m_notifyLock; // +0x1a0
    RWLock m_pledgeDelLock; // +0x1d8
    RWLock m_draftDelLock; // +0x210
    RWLock m_waitDelLock; // +0x248
    std::int32_t m_nInitTime{}; // +0x280
    [[maybe_unused]] std::uint32_t m_alignment284{}; // +0x284
};
