#pragma once

#include "Pledge.h"

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

class CSocket;

class CUser;
template <std::size_t Capacity>
class CPacketHelperEx;

using CPledgeKey = std::CDualKey<int, int>;
using CPledgeMap = CObjectMap<CPledge, 128, 5, RWLock, CPledgeKey>;


// Shipping owner: CacheD/PledgeDb.{h,cpp}. The kept layout is already
// fixed here, but no global instance is constructed until CPledge itself has
// its exact current layout and lifetime.
class CPledgeDb {
public:
    CPledgeDb();
    virtual ~CPledgeDb();

    static void TransferPledgeMasterBatch();
    static void ChangeNameByMergeBatch();

    int CreatePledge(wchar_t* pledgeName, int rulerId);
    bool ModifyPledgeName(std::uint32_t pledgeId, wchar_t* newName);
    bool AcquirePledgeSkill(std::uint32_t pledgeId, std::uint32_t skillId, int level, std::int32_t expirationTime);
    bool SetPowerGrade(std::uint32_t pledgeId, int gradeId, std::uint32_t power);
    bool UpgradeSubPledgeMemberCount(std::uint32_t pledgeId, PledgeType pledgeType, int memberCount);
    bool SetUserPledgeInfo(std::uint32_t characterId, std::uint32_t fieldSelector, std::uint32_t newValue);
    bool SetPledgeInfo(std::uint32_t pledgeId, std::uint32_t fieldSelector, int newValue);
    bool SigninPledge(std::uint32_t pledgeId, std::uint32_t characterId, PledgeType pledgeType);
    bool JoinPledgeMember(std::uint32_t pledgeId, std::uint32_t characterId, PledgeType pledgeType);
    bool WithdrawPledgeMember(std::uint32_t pledgeId, std::uint32_t characterId, PledgeType pledgeType);
    std::uint32_t OustPledgeMember(std::uint32_t pledgeId, const wchar_t* characterName);
    bool GraduateAcademyMember(std::uint32_t pledgeId, std::uint32_t memberId);
    bool DeletePledge(std::uint32_t pledgeId);
    bool UpdateSubPledgeMaster(std::uint32_t pledgeId, PledgeType pledgeType, std::uint32_t masterId);
    int RequestPledgeMasterTransfer(std::uint32_t pledgeId, wchar_t* newMasterName);
    int RequestPledgeMasterTransferCancel(std::uint32_t pledgeId);
    int RenameSubpledge(std::uint32_t pledgeId, PledgeType pledgeType, wchar_t* newName);

    CPledgeSP GetPledge(wchar_t* pledgeName, bool suppressLoadError);
    CPledgeSP GetPledge(int pledgeId, bool suppressLoadError, int worldId);
    void ItemCheckOnAllLoadedPledge();

private:

    CPledgeSP LoadPledge(wchar_t* pledgeName);
    CPledgeSP LoadPledge(int pledgeId);
    CPledgeSP LoadPledgeForWorld(int pledgeId, int worldId);
    CPledgeSP LoadPledgeInternal(CPledgeSP pledge);

    bool LoadPledgePower(CPledge* pledge);
    bool LoadAcademy(CPledge* pledge);
    bool LoadAcademyMember(CPledge* pledge);
    bool LoadGraduateAcademyCount(CPledge* pledge);
    bool LoadPledgeSkill(CPledge* pledge);
    bool LoadPledgeMember(CPledge* pledge);

    void InsertIntoMap(std::uint32_t pledgeId, wchar_t* pledgeName, CPledge* pledge);
    void DeleteFromMap(CPledge* pledge);

    CPledgeMap m_pledgesByWorldAndId; // +0x0008
    CObjectByIntKeyStringMap<CPledge> m_pledgesByName; // +0x2c10
};



extern CPledgeDb g_pledgeDb;

namespace PledgeDBUtil {

bool WritePledgeWaitingUser(
    int worldId,
    int characterId,
    const wchar_t* characterName,
    int classId,
    int level,
    int pledgeId,
    const wchar_t* selfIntroduction
);

} // namespace PledgeDBUtil

// Current CacheD keeps the pledge-recruitment state in this second class in
// PledgeDb.{h,cpp}. Private member spellings are not present in the usable PDB
// tail. The semantic names below are derived from each map's current callers.
class CPledgeJoinDB {
public:
    CPledgeJoinDB();
    ~CPledgeJoinDB();

    // The current image uses one lazily initialized BSS object. The accessor's
    // source name is absent, so Instance is the portable name for that exact
    // lifetime instead of a second global owner.
    static CPledgeJoinDB& Instance();

    void Init();
    void InitPledgeJoinSystemTables(CSocket* socket, int initializationPeriod);
    void LoadPledge();
    void InitPledgeNotify();
    int SearchPledgeRecruitBoard(
        CPacketHelperEx<20480>& pledgeIds,
        CPacketHelperEx<20480>& pledgeRows,
        CSPointer<CUser>& user,
        wchar_t* pledgeName,
        int pledgeLevel,
        int trend,
        wchar_t* pledgeMasterName,
        int sortCondition,
        int orderCondition,
        int recruitType,
        int& totalPages,
        int& page
    );
    int RequestPledgeWaitingUserList(CPacketHelperEx<20480>& packet, CSPointer<CUser> user, int pledgeId);
    void RequestPledgeDelete(int pledgeId);

    int GetPledgeWaitUserCount(int pledgeId);
    void DecresePledgeWaitUserCount(int pledgeId);
    bool CanRegistratePledge(int pledgeId);

    void UnRegisterPledge(int worldId, int pledgeId);
    void UnRegisterDraftUser(int worldId, int characterId);
    void UnRegisterWaitingUser(int worldId, int characterId);
    void SetPledgeWaitUserCount(int pledgeId, int userCount);
    void RegisterPledge(int worldId, int pledgeId);
    void RegisterDraftUser(int worldId, int characterId);
    void RegisterWaitingUser(int worldId, int characterId, int pledgeId);

    bool IsWaitingUserRegistered(int worldId, int characterId);
    bool IsDraftUserRegistered(int worldId, int characterId);
    bool IsPledgeRegistered(int worldId, int pledgeId);
    void ModifyUserInfoOnPledgeJoinDB(int characterId, int level, int classId);

    int GetTimeOnPledge(int pledgeId);
    int GetTimeOnWait(int characterOrPledgeId);
    int GetTimeOnDraft(int characterId);

private:
    using RegistrationKey = std::CDualKey<int, int>;
    using RegistrationMap = std::map<RegistrationKey, bool>;
    using TimeMap = std::map<int, int>;

    void RegisterObject(int worldId, int objectId, RegistrationMap& objects, RWLock& lock);
    void UnRegisterObject(int worldId, int objectId, RegistrationMap& objects, RWLock& lock);
    void SetTimeOnPledgeJoinSystem(int objectId, TimeMap& times, RWLock& lock);
    bool IsObjectExist(int worldId, int objectId, const wchar_t* statement, RegistrationMap& objects, RWLock& lock);
    int GetTimeOnPledgeJoinSystem(int objectId, const wchar_t* statement, TimeMap& times, RWLock& lock);

    RegistrationMap m_draftUsers; // target +0x000
    RegistrationMap m_waitingUsers; // target +0x020
    RegistrationMap m_registeredPledges; // target +0x040
    TimeMap m_pledgeWaitUserCounts; // target +0x060
    TimeMap m_pledgeRegistrationTimes; // target +0x080
    TimeMap m_waitingRegistrationTimes; // target +0x0a0
    TimeMap m_draftRegistrationTimes; // target +0x0c0

    RWLock m_registeredPledgesLock; // target +0x0e0
    RWLock m_draftUsersLock; // target +0x118
    RWLock m_waitingUsersLock; // target +0x150
    RWLock m_pledgeWaitUserCountsLock; // target +0x188
    RWLock m_pledgeRegistrationTimesLock; // target +0x1c0
    RWLock m_waitingRegistrationTimesLock; // target +0x1f8
    RWLock m_draftRegistrationTimesLock; // target +0x230

    // The target stores these at +0x16b8 and +0x1b6bc. Unknown intervening
    // request scratch regions are not presented as source
    // fields in the host object. Their wire owners are recovered separately.
    std::int32_t m_initPledgeNotifyTime = 0;
    int m_pageSize = 12;
};
