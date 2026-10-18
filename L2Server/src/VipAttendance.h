#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>

class CUserSocket;
class User;
struct AuthUserDataInfo;
struct ItemAmountDataStruct;
template <std::size_t Capacity>
class CPacketHelperEx;

namespace BuilderCmd {
bool VipAttendance(CUserSocket*, User*, char16_t*);
}

struct VipAttendanceItem {
    VipAttendanceItem() = default;

    std::int32_t nItemType = 0;
    std::int64_t nAmount = 0;
    std::int32_t nPcCafeMultiple = 0;
    std::int32_t nVIPLevel = 0;
    bool bHighLight = false;
};


class VipAttendanceDB {
public:
    VipAttendanceDB();
    ~VipAttendanceDB();

    bool MakeVipRewardPacket(CPacketHelperEx<20480>& packet);
    bool MakeNormalRewardPacket(CPacketHelperEx<20480>& packet);
    bool MakeNewbieRewardPacket(CPacketHelperEx<20480>& packet);
    bool MakeDormantRewardPacket(CPacketHelperEx<20480>& packet);

    const VipAttendanceItem* GetAttendanceNormalReward(int day);
    const VipAttendanceItem* GetAttendanceNewbieReward(int day);
    const VipAttendanceItem* GetAttendanceDormantReward(int day);
    const VipAttendanceItem* GetAttendanceVipReward(int day);

    int GetNormalMaxCnt() const;
    int GetNewbieMaxCnt() const;
    int GetDormantMaxCnt() const;
    void SetResetTime(int hour, int minute);
    std::int32_t GetResetTime();
    void GetResetTime(int& hour, int& minute);

private:
    friend bool BuilderCmd::VipAttendance(CUserSocket*, User*, char16_t*);

    using RewardMap = std::map<int, VipAttendanceItem*>;

    RewardMap m_mapNormalReward;
    RewardMap m_mapNewbieReward;
    RewardMap m_mapDormantReward;
    RewardMap m_mapVipReward;
    CYieldLock m_lock;
    std::int32_t m_nResetHour;
    std::int32_t m_nResetMin;
};


extern VipAttendanceDB g_VipAttendanceDB;

class UserAttendance {
public:
    UserAttendance();

    void Copy(UserAttendance& destination);
    void SetNormalCheck(int normalCheck);
    bool AcquireFlag();
    void ReleaseFlag();
    bool IsAcquiredFlag() const;
    void ResetVipAttendance(int lastCheckTime);
    void SetVipAttendance(unsigned char normalCheck, int pcCafeChecks, int vipChecks, int lastCheckTime);
    void ResetTimeVipAttendanceForBuilder();
    bool BitTestPCCafe(int day) const;
    bool BitTestVIP(int day) const;
    bool GetConfirmNormalAttendance() const;
    bool GetConfirmPCCafeAttendance(User* user) const;
    int GetConfirmVIPAttendance(User* user) const;
    int GetDay() const;
    int GetNextPCCafeCheck(int day) const;
    char GetNextVIPCheck(int day) const;
    bool VerifyAttendanceData() const;

    char m_cCheckType;
    int m_nCheckNormal;
    // Current Win64 std::bitset<28> occupies one dword. Fixed carriers keep
    // that physical width on hosts whose standard-library bitset uses an
    // eight-byte word. VipAttendance.cpp keeps the compiled operations.
    std::uint32_t m_CheckPCCafeBits;
    std::uint32_t m_CheckVIPBits;
    std::int32_t m_LastCheckTime;
    volatile std::int32_t m_nSetVIPAttendanceFlag;
};


class VipAttendanceSystem {
public:
    static VipAttendanceSystem& Inst() {
        static VipAttendanceSystem instance;
        return instance;
    }

    bool AcquireVIPAttendanceFlag(unsigned int accountId);
    void ReleaseVIPAttendanceFlag(unsigned int accountId);
    bool IsAcquiredVIPAttendanceFlag(unsigned int accountId);
    int GetMaxDay(char attendanceType);
    bool GetUserAttendance(unsigned int accountId, UserAttendance& attendance);
    void AddUserAttendanceInfo(CUserSocket* socket, AuthUserDataInfo info);
    void ResetTimeUserAttendanceForBuilder(unsigned int accountId);
    void ReleseUserAttendanceInfo(unsigned int accountId);
    bool IsReceptableReward(User* user);
    bool CheckDaysAndReset(User* user);
    int GetCheckableVipAttendance(User* user);
    void SendVipAttendanceItemList(User* user);
    bool SendVipAttendanceResult(User* user);
    int GetVipAttendanceReward(User* user, ItemAmountDataStruct& rewards);
    void AddRewardLog(unsigned int accountId, int* itemClassIds, std::int64_t* itemAmounts);
    int CheckAndRequestVipAttendanceReward(User* user);
    bool SendToAuthUpdateVipAttendance(
        unsigned int accountId,
        char attendanceType,
        int attendanceDay,
        int pcCafeAttendanceFlag,
        int vipAttendanceFlag,
        int lastAttendanceCheckTime,
        bool builder
    );
    bool ProgressCheckAttendanceData(User* user);
    void SendToAuthResetAttendance(unsigned int accountId, char attendanceType, wchar_t* reason);
    void SendToAuthRollbackVipAttendance(unsigned int accountId);
    void ReceiveToAuthResetVipAttendance(unsigned int accountId, char result, AuthUserDataInfo info);

private:
    char GetNextAttendanceType(CUserSocket* socket);

    CYieldLock UserAttendanceMapLock;
    std::map<unsigned int, UserAttendance*> m_mapUserAttendance;
};
