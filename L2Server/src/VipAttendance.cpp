#include "VipAttendance.h"
#include "Auth.h"
#include "CConfig.h"
#include "Item.h"
#include "ObjectDB.h"
#include "Protocol/src/SystemMessageIds.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/EtcMani.h"
#include "Shared/src/ItemMani.h"
#include "Shared/src/Log.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Runtime/RawAtomic.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <ctime>
#include <stdexcept>

namespace {

constexpr std::size_t kAttendanceBitCount = 28;

} // namespace

VipAttendanceDB g_VipAttendanceDB;

VipAttendanceDB::VipAttendanceDB() : m_nResetHour(6), m_nResetMin(30) {}

VipAttendanceDB::~VipAttendanceDB() = default;

const VipAttendanceItem* VipAttendanceDB::GetAttendanceNormalReward(int day) {
    L2_GUARD_FUNCTION();
    const VipAttendanceItem* const reward = [&] {
        L2_GUARD(m_lock);
        const auto found = m_mapNormalReward.find(day);
        const VipAttendanceItem* const reward = found == m_mapNormalReward.end() ? nullptr : found->second;
        return reward;
    }();
    return reward;
}

const VipAttendanceItem* VipAttendanceDB::GetAttendanceNewbieReward(int day) {
    L2_GUARD_FUNCTION();
    const VipAttendanceItem* const reward = [&] {
        L2_GUARD(m_lock);
        const auto found = m_mapNewbieReward.find(day);
        const VipAttendanceItem* const reward = found == m_mapNewbieReward.end() ? nullptr : found->second;
        return reward;
    }();
    return reward;
}

const VipAttendanceItem* VipAttendanceDB::GetAttendanceDormantReward(int day) {
    L2_GUARD_FUNCTION();
    const VipAttendanceItem* const reward = [&] {
        L2_GUARD(m_lock);
        const auto found = m_mapDormantReward.find(day);
        const VipAttendanceItem* const reward = found == m_mapDormantReward.end() ? nullptr : found->second;
        return reward;
    }();
    return reward;
}

const VipAttendanceItem* VipAttendanceDB::GetAttendanceVipReward(int day) {
    L2_GUARD_FUNCTION();
    const VipAttendanceItem* const reward = [&] {
        L2_GUARD(m_lock);
        const auto found = m_mapVipReward.find(day);
        const VipAttendanceItem* const reward = found == m_mapVipReward.end() ? nullptr : found->second;
        return reward;
    }();
    return reward;
}

int VipAttendanceDB::GetNormalMaxCnt() const { return static_cast<int>(m_mapNormalReward.size()); }

int VipAttendanceDB::GetNewbieMaxCnt() const { return static_cast<int>(m_mapNewbieReward.size()); }

int VipAttendanceDB::GetDormantMaxCnt() const { return static_cast<int>(m_mapDormantReward.size()); }

void VipAttendanceDB::SetResetTime(int hour, int minute) {
    m_nResetHour = hour;
    m_nResetMin = minute;
}

void VipAttendanceDB::GetResetTime(int& hour, int& minute) {
    L2_GUARD_FUNCTION();
    hour = m_nResetHour;
    minute = m_nResetMin;
}

std::int32_t VipAttendanceDB::GetResetTime() {
    L2_GUARD_FUNCTION();
    const std::int32_t now = runtime::crt::Time32Now();
    std::tm current{};
    std::tm reset{};
    if (!runtime::crt::LocalTime32(now, current) || !runtime::crt::LocalTime32(now, reset)) {
        return -1;
    }
    reset.tm_hour = m_nResetHour;
    reset.tm_min = m_nResetMin;
    reset.tm_sec = 0;
    std::int32_t result = runtime::crt::Mktime32(&reset);
    if (current.tm_hour < m_nResetHour ||
        (current.tm_hour == m_nResetHour && current.tm_min <= (m_nResetMin > 1 ? m_nResetMin - 1 : 0))) {
        result -= 86400;
    }
    return result;
}

bool VipAttendanceDB::MakeVipRewardPacket(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    if (static_cast<int>(m_mapVipReward.size()) >= 0) {
        packet.Assemble("c", static_cast<int>(static_cast<char>(m_mapVipReward.size())));
        for (const auto& entry : m_mapVipReward) {
            const VipAttendanceItem* const item = entry.second;
            if (item == nullptr || item->nAmount <= 0 || g_ObjectDB.GetObjectW(item->nItemType, false) == nullptr) {
                continue;
            }
            if (packet.Assemble(
                    "cddcc", static_cast<int>(static_cast<char>(entry.first)), item->nItemType,
                    static_cast<int>(item->nAmount), static_cast<int>(static_cast<char>(item->nVIPLevel)),
                    static_cast<int>(item->bHighLight)
                ) < 1) {
                return false;
            }
        }
    }
    return true;
}

bool VipAttendanceDB::MakeNormalRewardPacket(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    if (static_cast<int>(m_mapNormalReward.size()) < 1) {
        return false;
    }
    packet.Assemble("c", static_cast<int>(static_cast<char>(m_mapNormalReward.size())));
    {
        L2_GUARD(m_lock);
        for (const auto& entry : m_mapNormalReward) {
            const VipAttendanceItem* const item = entry.second;
            if (item == nullptr || item->nAmount <= 0 || g_ObjectDB.GetObjectW(item->nItemType, false) == nullptr) {
                continue;
            }
            if (packet.Assemble(
                    "dQcc", item->nItemType, static_cast<long long>(item->nAmount),
                    static_cast<int>(static_cast<char>(item->nPcCafeMultiple)), static_cast<int>(item->bHighLight)
                ) < 1) {
                return false;
            }
        }
    }
    return true;
}

bool VipAttendanceDB::MakeNewbieRewardPacket(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    if (static_cast<int>(m_mapNewbieReward.size()) < 1) {
        return false;
    }
    {
        L2_GUARD(m_lock);
        packet.Assemble("c", static_cast<int>(static_cast<char>(m_mapNewbieReward.size())));
        for (const auto& entry : m_mapNewbieReward) {
            const VipAttendanceItem* const item = entry.second;
            if (item == nullptr || item->nAmount <= 0 || g_ObjectDB.GetObjectW(item->nItemType, false) == nullptr) {
                continue;
            }
            if (packet.Assemble(
                    "dQcc", item->nItemType, static_cast<long long>(item->nAmount),
                    static_cast<int>(static_cast<char>(item->nPcCafeMultiple)), static_cast<int>(item->bHighLight)
                ) < 1) {
                return false;
            }
        }
    }
    return true;
}

bool VipAttendanceDB::MakeDormantRewardPacket(CPacketHelperEx<20480>& packet) {
    L2_GUARD_FUNCTION();
    if (static_cast<int>(m_mapDormantReward.size()) < 1) {
        return false;
    }
    {
        L2_GUARD(m_lock);
        packet.Assemble("c", static_cast<int>(static_cast<char>(m_mapDormantReward.size())));
        for (const auto& entry : m_mapDormantReward) {
            const VipAttendanceItem* const item = entry.second;
            if (item == nullptr || item->nAmount <= 0 || g_ObjectDB.GetObjectW(item->nItemType, false) == nullptr) {
                continue;
            }
            if (packet.Assemble(
                    "dQcc", item->nItemType, static_cast<long long>(item->nAmount),
                    static_cast<int>(static_cast<char>(item->nPcCafeMultiple)), static_cast<int>(item->bHighLight)
                ) < 1) {
                return false;
            }
        }
    }
    return true;
}

UserAttendance::UserAttendance() :
    m_cCheckType(0),
    m_nCheckNormal(0),
    m_CheckPCCafeBits(0),
    m_CheckVIPBits(0),
    m_LastCheckTime(0),
    m_nSetVIPAttendanceFlag(0) {
    L2_GUARD_FUNCTION();
    // Current L2Server 0x00c49390 repeats both bitset resets in the body
    // after their value-initialization and after entering the guard.
    m_CheckPCCafeBits = 0;
    m_CheckVIPBits = 0;
}

void UserAttendance::SetNormalCheck(int normalCheck) { m_nCheckNormal = normalCheck; }

bool UserAttendance::AcquireFlag() {
    return runtime::raw_atomic::CompareExchangeI32(&m_nSetVIPAttendanceFlag, 1, 0) == 0;
}

void UserAttendance::ReleaseFlag() { runtime::raw_atomic::ExchangeI32(&m_nSetVIPAttendanceFlag, 0); }

bool UserAttendance::IsAcquiredFlag() const { return runtime::raw_atomic::LoadI32Relaxed(&m_nSetVIPAttendanceFlag) == 1; }

void UserAttendance::ResetVipAttendance(int lastCheckTime) {
    m_nCheckNormal = 0;
    m_CheckPCCafeBits = 0;
    m_CheckVIPBits = 0;
    m_LastCheckTime = lastCheckTime;
}

void UserAttendance::Copy(UserAttendance& destination) {
    L2_GUARD_FUNCTION();
    destination.m_cCheckType = m_cCheckType;
    destination.m_nCheckNormal = m_nCheckNormal;
    destination.m_CheckPCCafeBits = m_CheckPCCafeBits;
    destination.m_CheckVIPBits = m_CheckVIPBits;
    destination.m_LastCheckTime = m_LastCheckTime;
}

void UserAttendance::SetVipAttendance(unsigned char normalCheck, int pcCafeChecks, int vipChecks, int lastCheckTime) {
    L2_GUARD_FUNCTION();
    m_nCheckNormal = normalCheck;

    std::uint32_t pcCafeBits = 0;
    std::uint32_t pcCafeValue = static_cast<std::uint32_t>(pcCafeChecks);
    for (std::size_t position = 0; pcCafeValue != 0 && position < kAttendanceBitCount; ++position) {
        if ((pcCafeValue & 1U) != 0) {
            if (position >= kAttendanceBitCount) {
                throw std::out_of_range("invalid bitset<N> position");
            }
            pcCafeBits |= std::uint32_t{1} << static_cast<unsigned>(position);
        }
        pcCafeValue >>= 1U;
    }
    m_CheckPCCafeBits = pcCafeBits;

    std::uint32_t vipBits = 0;
    std::uint32_t vipValue = static_cast<std::uint32_t>(vipChecks);
    for (std::size_t position = 0; vipValue != 0 && position < kAttendanceBitCount; ++position) {
        if ((vipValue & 1U) != 0) {
            if (position >= kAttendanceBitCount) {
                throw std::out_of_range("invalid bitset<N> position");
            }
            vipBits |= std::uint32_t{1} << static_cast<unsigned>(position);
        }
        vipValue >>= 1U;
    }
    m_CheckVIPBits = vipBits;
    m_LastCheckTime = lastCheckTime;
}

void UserAttendance::ResetTimeVipAttendanceForBuilder() {
    L2_GUARD_FUNCTION();
    m_LastCheckTime = 0;
}

bool UserAttendance::BitTestPCCafe(int day) const {
    L2_GUARD_FUNCTION();
    if (day >= 29) {
        return false;
    }
    const int position = runtime::SubtractInt32Wrapping(day, 1);
    if (position < 0) {
        return false;
    }
    if (static_cast<std::uint64_t>(position) >= kAttendanceBitCount) {
        throw std::out_of_range("invalid bitset<N> position");
    }
    return (m_CheckPCCafeBits & (std::uint32_t{1} << static_cast<unsigned>(position))) == 0;
}

bool UserAttendance::BitTestVIP(int day) const {
    L2_GUARD_FUNCTION();
    if (day >= 28) {
        return false;
    }
    const int position = runtime::SubtractInt32Wrapping(day, 1);
    if (position < 0) {
        return false;
    }
    if (static_cast<std::uint64_t>(position) >= kAttendanceBitCount) {
        throw std::out_of_range("invalid bitset<N> position");
    }
    return (m_CheckVIPBits & (std::uint32_t{1} << static_cast<unsigned>(position))) == 0;
}

bool UserAttendance::GetConfirmNormalAttendance() const {
    L2_GUARD_FUNCTION();
    return m_LastCheckTime < g_VipAttendanceDB.GetResetTime();
}

int UserAttendance::GetDay() const {
    L2_GUARD_FUNCTION();
    return m_nCheckNormal + (GetConfirmNormalAttendance() ? 1 : 0);
}

bool UserAttendance::GetConfirmPCCafeAttendance(User* user) const {
    L2_GUARD_FUNCTION();
    return g_bUsePCCafeAttendance && user->m_data->m_bPCCafeUser && BitTestPCCafe(GetDay());
}

int UserAttendance::GetConfirmVIPAttendance(User*) const {
    L2_GUARD_FUNCTION();
    if (!g_bUseVipAttendance) {
        return -6;
    }
    const VipAttendanceItem* const reward = g_VipAttendanceDB.GetAttendanceVipReward(m_nCheckNormal);
    if (reward == nullptr) {
        return -6;
    }
    if (BitTestVIP(GetDay())) {
        return 0;
    }
    return reward->nVIPLevel < 1 ? 1 : -1;
}

int UserAttendance::GetNextPCCafeCheck(int day) const {
    L2_GUARD_FUNCTION();
    const int position = runtime::SubtractInt32Wrapping(day, 1);
    if (static_cast<std::uint64_t>(static_cast<std::int64_t>(position)) >= kAttendanceBitCount) {
        throw std::out_of_range("invalid bitset<N> position");
    }
    return static_cast<int>(m_CheckPCCafeBits | (std::uint32_t{1} << static_cast<unsigned>(position)));
}

char UserAttendance::GetNextVIPCheck(int day) const {
    L2_GUARD_FUNCTION();
    const int position = runtime::SubtractInt32Wrapping(day, 1);
    if (static_cast<std::uint64_t>(static_cast<std::int64_t>(position)) >= kAttendanceBitCount) {
        throw std::out_of_range("invalid bitset<N> position");
    }
    return static_cast<char>(m_CheckVIPBits | (std::uint32_t{1} << static_cast<unsigned>(position)));
}

bool UserAttendance::VerifyAttendanceData() const {
    L2_GUARD_FUNCTION();
    return static_cast<unsigned char>(m_cCheckType) < 4 && m_nCheckNormal >= 0 && m_nCheckNormal < 29;
}

char VipAttendanceSystem::GetNextAttendanceType(CUserSocket* socket) {
    L2_GUARD_FUNCTION();
    if (socket != nullptr) {
        if (socket->IsDormantUser()) {
            return 3;
        }
        if (socket->IsNewbieUser()) {
            return 2;
        }
    }
    return 1;
}

bool VipAttendanceSystem::AcquireVIPAttendanceFlag(unsigned int accountId) {
    L2_GUARD_FUNCTION();
    bool acquired = false;
    {
        L2_GUARD(UserAttendanceMapLock);
        const auto attendance = m_mapUserAttendance.find(accountId);
        if (attendance != m_mapUserAttendance.end() && attendance->second != nullptr) {
            acquired = attendance->second->AcquireFlag();
        }
    }
    return acquired;
}

int VipAttendanceSystem::GetMaxDay(char attendanceType) {
    L2_GUARD_FUNCTION();
    if (attendanceType == 1) {
        return g_VipAttendanceDB.GetNormalMaxCnt();
    }
    if (attendanceType == 2) {
        return g_VipAttendanceDB.GetNewbieMaxCnt();
    }
    if (attendanceType == 3) {
        return g_VipAttendanceDB.GetDormantMaxCnt();
    }
    return 0;
}

void VipAttendanceSystem::ReleaseVIPAttendanceFlag(unsigned int accountId) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(UserAttendanceMapLock);
        const auto attendance = m_mapUserAttendance.find(accountId);
        if (attendance != m_mapUserAttendance.end() && attendance->second != nullptr) {
            runtime::raw_atomic::ExchangeI32(&attendance->second->m_nSetVIPAttendanceFlag, 0);
        }
    }
}

bool VipAttendanceSystem::IsAcquiredVIPAttendanceFlag(unsigned int accountId) {
    L2_GUARD_FUNCTION();
    bool acquired = false;
    {
        L2_GUARD(UserAttendanceMapLock);
        const auto attendance = m_mapUserAttendance.find(accountId);
        if (attendance != m_mapUserAttendance.end() && attendance->second != nullptr) {
            acquired = runtime::raw_atomic::LoadI32Relaxed(&attendance->second->m_nSetVIPAttendanceFlag) == 1;
        }
    }
    return acquired;
}

bool VipAttendanceSystem::GetUserAttendance(unsigned int accountId, UserAttendance& attendance) {
    L2_GUARD_FUNCTION();
    bool found = false;
    {
        L2_GUARD(UserAttendanceMapLock);
        const auto stored = m_mapUserAttendance.find(accountId);
        if (stored != m_mapUserAttendance.end() && stored->second != nullptr) {
            stored->second->Copy(attendance);
            found = true;
        }
    }
    return found;
}

void VipAttendanceSystem::ResetTimeUserAttendanceForBuilder(unsigned int accountId) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(UserAttendanceMapLock);
        const auto attendance = m_mapUserAttendance.find(accountId);
        if (attendance != m_mapUserAttendance.end() && attendance->second != nullptr) {
            attendance->second->ResetTimeVipAttendanceForBuilder();
        }
    }
}

void VipAttendanceSystem::AddUserAttendanceInfo(CUserSocket* socket, AuthUserDataInfo info) {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(UserAttendanceMapLock);

        const unsigned int accountId = socket->GetAccountID();
        const auto stored = m_mapUserAttendance.find(accountId);
        if (stored != m_mapUserAttendance.end()) {
            delete stored->second;
            m_mapUserAttendance.erase(stored);
        }

        auto* const attendance = new UserAttendance;
        if (info.eAttendanceType == NEW_AUTH_USER_DATA::AT_FIRST_ATTENDANCE) {
            attendance->m_cCheckType = GetNextAttendanceType(socket);
            attendance->SetVipAttendance(0, 0, 0, 0);
        } else {
            attendance->m_cCheckType = static_cast<char>(info.eAttendanceType);
            attendance->SetVipAttendance(
                info.nAttendanceDays, info.nPCCafeAttendanceFlag, info.nVIPAttendanceFlag, info.tLastAttendanceCheckTime
            );
        }
        m_mapUserAttendance.emplace(accountId, attendance);
    }
}

void VipAttendanceSystem::ReleseUserAttendanceInfo(unsigned int accountId) {
    L2_GUARD_FUNCTION();

    {
        L2_GUARD(UserAttendanceMapLock);
        const auto attendance = m_mapUserAttendance.find(accountId);
        if (attendance != m_mapUserAttendance.end()) {
            delete attendance->second;
            m_mapUserAttendance.erase(attendance);
        }
    }
}

bool VipAttendanceSystem::IsReceptableReward(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }
    UserAttendance attendance;
    const unsigned int accountId = user->GetAccountID();
    if (!GetUserAttendance(accountId, attendance)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FILE[%s] LINE[%d] VipAttendance no data AccountId[%d]",
        //     L"VipAttendance.cpp",
        //     0x39a,
        //     accountId)
        // Log.Add(
        //     LOG_WARN,
        //     "[attendance] Cannot check reward eligibility for account %u: no local attendance record.",
        //     static_cast<unsigned>(accountId))
        L2_LOG_WARN(
            Log, "[attendance] Cannot check reward eligibility for account %u: no local attendance record.",
            static_cast<unsigned>(accountId)
        );
        return false;
    }
    if (!attendance.VerifyAttendanceData()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FILE[%s] LINE[%d] VipAttendance Verify Fail "
        //     L"AccountId[%d] Type[%d] days[%d]",
        //     L"VipAttendance.cpp",
        //     0x3a5,
        //     accountId,
        //     static_cast<int>(attendance.m_cCheckType),
        //     attendance.m_nCheckNormal)
        // Log.Add(
        //     LOG_ERROR,
        //     "[attendance] Cannot check rewards for account %u: invalid attendance type %d or day count %d (expected type
        //     0..3, days 0..28).", static_cast<unsigned>(accountId), static_cast<int>(attendance.m_cCheckType),
        //     attendance.m_nCheckNormal)
        L2_LOG_ERROR(
            Log,
            "[attendance] Cannot check rewards for account %u: invalid attendance type %d or day count %d (expected type "
            "0..3, days 0..28).",
            static_cast<unsigned>(accountId), static_cast<int>(attendance.m_cCheckType), attendance.m_nCheckNormal
        );
        return false;
    }
    return attendance.GetConfirmNormalAttendance() || attendance.GetConfirmPCCafeAttendance(user) ||
        attendance.GetConfirmVIPAttendance(user) == 1;
}

bool VipAttendanceSystem::CheckDaysAndReset(User* user) {
    // Current L2Server 0x00c4c724.
    L2_GUARD_FUNCTION();

    UserAttendance attendance;
    const unsigned int accountId = user->GetAccountID();
    if (!VipAttendanceSystem::Inst().GetUserAttendance(accountId, attendance)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FILE[%s] LINE[%d] VipAttendance no data AccountId[%d]",
        //     L"VipAttendance.cpp",
        //     0x2bb,
        //     user->GetAccountID())
        // Log.Add(
        //     LOG_WARN,
        //     "[attendance] Cannot check the attendance-cycle reset for account %u: no local attendance record.",
        //     static_cast<unsigned>(accountId))
        L2_LOG_WARN(
            Log, "[attendance] Cannot check the attendance-cycle reset for account %u: no local attendance record.",
            static_cast<unsigned>(accountId)
        );
        return false;
    }

    const int day = attendance.GetDay();
    const int maxDay = GetMaxDay(attendance.m_cCheckType);
    if (!attendance.GetConfirmNormalAttendance() || day <= maxDay) {
        return true;
    }

    CUserSocket* const socket = user->GetSocket();
    const char nextAttendanceType = GetNextAttendanceType(socket);
    const unsigned int resetAccountId = user->GetAccountID();
    SendToAuthResetAttendance(resetAccountId, nextAttendanceType, const_cast<wchar_t*>(L"max_day_reset"));
    return false;
}

int VipAttendanceSystem::GetCheckableVipAttendance(User* const user) {
    // Current L2Server 0x00c4c8ec.
    L2_GUARD_FUNCTION();
    const unsigned int accountId = user->GetAccountID();
    UserAttendance attendance;
    if (!VipAttendanceSystem::Inst().GetUserAttendance(accountId, attendance)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FILE[%s] LINE[%d] VipAttendance no data AccountId[%d]",
        //     L"VipAttendance.cpp",
        //     0x2ec,
        //     accountId)
        // Log.Add(
        //     LOG_WARN,
        //     "[attendance] Cannot determine available attendance rewards for account %u: no local attendance record.",
        //     static_cast<unsigned>(accountId))
        L2_LOG_WARN(
            Log, "[attendance] Cannot determine available attendance rewards for account %u: no local attendance record.",
            static_cast<unsigned>(accountId)
        );
        return 0;
    }
    if (!CheckDaysAndReset(user)) {
        return -1;
    }

    const int day = attendance.GetDay();
    int result = 3;
    if (attendance.GetConfirmNormalAttendance()) {
        user->SendSystemMessage_d(SystemMessageId::DAILY_ATTENDANCE_REWARD_AVAILABLE, day);
        result = 2;
    }
    if (attendance.GetConfirmPCCafeAttendance(user)) {
        user->SendSystemMessage_d(SystemMessageId::DAILY_PC_CAFE_ATTENDANCE_REWARD_AVAILABLE, day);
        result = 2;
    }
    if (attendance.GetConfirmVIPAttendance(user) == 1) {
        user->SendSystemMessage_d(SystemMessageId::DAILY_VIP_ATTENDANCE_REWARD_AVAILABLE, day);
        result = 2;
    }
    return result;
}

void VipAttendanceSystem::SendVipAttendanceItemList(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return;
    }
    UserAttendance attendance;
    const unsigned int accountId = user->GetAccountID();
    if (!GetUserAttendance(accountId, attendance)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FILE[%s] LINE[%d] VipAttendance no data AccountId[%d]",
        //     L"VipAttendance.cpp",
        //     0x43b,
        //     accountId)
        // Log.Add(
        //     LOG_WARN,
        //     "[attendance] Cannot send the attendance item list for account %u: no local attendance record.",
        //     static_cast<unsigned>(accountId))
        L2_LOG_WARN(
            Log, "[attendance] Cannot send the attendance item list for account %u: no local attendance record.",
            static_cast<unsigned>(accountId)
        );
        return;
    }

    CPacketHelperEx<20480> packet;
    bool assembled = false;
    if (attendance.m_cCheckType == 1) {
        assembled = g_VipAttendanceDB.MakeNormalRewardPacket(packet);
    } else if (attendance.m_cCheckType == 2) {
        assembled = g_VipAttendanceDB.MakeNewbieRewardPacket(packet);
    } else if (attendance.m_cCheckType == 3) {
        assembled = g_VipAttendanceDB.MakeDormantRewardPacket(packet);
    }
    if (g_bUseVipAttendance) {
        // Current 0x00c4ae35 does not short-circuit on the first packet.
        // The VIP packet builder is called unconditionally and its result
        // becomes the observable success value for the combined packet.
        assembled = g_VipAttendanceDB.MakeVipRewardPacket(packet);
    } else {
        packet.Assemble("c", 0);
    }
    if (!assembled) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FILE[%s] LINE[%d] VipAttendance no Reward Item data Type[%d]",
        //     L"VipAttendance.cpp",
        //     0x47e,
        //     static_cast<int>(attendance.m_cCheckType))
        // Log.Add(
        //     LOG_ERROR,
        //     "[attendance] Cannot build the attendance item list for account %u (type %d): the final reward-list builder
        //     reported failure.", static_cast<unsigned>(accountId), static_cast<int>(attendance.m_cCheckType))
        L2_LOG_ERROR(
            Log,
            "[attendance] Cannot build the attendance item list for account %u (type %d): the final reward-list builder "
            "reported failure.",
            static_cast<unsigned>(accountId), static_cast<int>(attendance.m_cCheckType)
        );
        return;
    }

    const int day = attendance.GetDay();
    const int level = user->m_data != nullptr ? user->m_data->m_nLevel : 0;
    int vipResult = -6;
    if (level < g_nAttendanceMinimumLev) {
        user->SendSystemMessage(SystemMessageId::ATTENDANCE_REWARD_LEVEL_REQUIREMENTS_NOT_MET);
    } else {
        vipResult = attendance.GetConfirmVIPAttendance(user);
    }
    const bool checkable = attendance.GetConfirmNormalAttendance() || attendance.GetConfirmPCCafeAttendance(user);
    CUserSocket* const socket = user->GetSocket();
    if (socket != nullptr) {
        socket->Send(
            "chccddcccbd", 0xfe, 0x17e, static_cast<int>(static_cast<char>(day)),
            static_cast<int>(static_cast<char>(attendance.m_nCheckNormal)), attendance.m_CheckPCCafeBits,
            attendance.m_CheckVIPBits, static_cast<int>(attendance.m_cCheckType), static_cast<int>(checkable),
            static_cast<int>(static_cast<char>(vipResult)), packet.GetLength(), packet.GetPacket(), g_nAttendanceMinimumLev
        );
    }
}

int VipAttendanceSystem::GetVipAttendanceReward(User* user, ItemAmountDataStruct& rewards) {
    // Current L2Server 0x00c4a6b4, original owner VipAttendance.cpp.
    L2_GUARD_FUNCTION();

    const unsigned int accountId = user->GetAccountID();
    UserAttendance attendance;
    if (!GetUserAttendance(accountId, attendance)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FILE[%s] LINE[%d] VipAttendance no data AccountId[%d]",
        //     L"VipAttendance.cpp",
        //     0x31d,
        //     accountId)
        // Log.Add(
        //     LOG_WARN,
        //     "[attendance] Cannot prepare attendance rewards for account %u: no local attendance record (result -8).",
        //     static_cast<unsigned>(accountId))
        L2_LOG_WARN(
            Log, "[attendance] Cannot prepare attendance rewards for account %u: no local attendance record (result -8).",
            static_cast<unsigned>(accountId)
        );
        return -8;
    }

    const int day = attendance.GetDay();
    const bool normal = attendance.GetConfirmNormalAttendance();
    const bool pcCafe = attendance.GetConfirmPCCafeAttendance(user);
    const int vipResult = attendance.GetConfirmVIPAttendance(user);
    if (!normal && !pcCafe) {
        return vipResult == -1 ? -1 : 0;
    }

    const VipAttendanceItem* const vipReward = g_VipAttendanceDB.GetAttendanceVipReward(day);
    if (vipResult == 1 && vipReward != nullptr && vipReward->nAmount > 0) {
        // CObject* const object =
        //     g_ObjectDB.GetObjectW(vipReward->nItemType, false)
        // Reuse the observed lookup ID in diagnostics without rereading the reward.
        const int vipItemId = vipReward->nItemType;
        CObject* const object = g_ObjectDB.GetObjectW(vipItemId, false);
        if (object == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[attendance] Cannot prepare the VIP attendance reward for account %u on day %d: item definition %d is
            //     missing (result -3).", static_cast<unsigned>(accountId), day, vipItemId)
            L2_LOG_ERROR(
                Log,
                "[attendance] Cannot prepare the VIP attendance reward for account %u on day %d: item definition %d is "
                "missing (result -3).",
                static_cast<unsigned>(accountId), day, vipItemId
            );
            return -3;
        }
        CItem* const item = static_cast<CItem*>(object);
        if (CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType)) {
            rewards.Push(vipReward->nItemType, vipReward->nAmount);
        } else {
            for (std::int32_t index = 0; static_cast<std::int64_t>(index) < vipReward->nAmount;
                index = runtime::AddInt32Wrapping(index, 1)) {
                rewards.Push(vipReward->nItemType, 1);
            }
        }
    }

    const VipAttendanceItem* reward = nullptr;
    if (attendance.m_cCheckType == 1) {
        reward = g_VipAttendanceDB.GetAttendanceNormalReward(day);
    } else if (attendance.m_cCheckType == 2) {
        reward = g_VipAttendanceDB.GetAttendanceNewbieReward(day);
    } else if (attendance.m_cCheckType == 3) {
        reward = g_VipAttendanceDB.GetAttendanceDormantReward(day);
    }
    if (reward == nullptr || reward->nAmount < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[attendance] Cannot prepare attendance rewards for account %u (type %d, day %d): %s (result -3).",
        //     static_cast<unsigned>(accountId), static_cast<int>(attendance.m_cCheckType), day,
        //     reward == nullptr ? "reward definition missing" : "negative reward amount")
        L2_LOG_ERROR(
            Log, "[attendance] Cannot prepare attendance rewards for account %u (type %d, day %d): %s (result -3).",
            static_cast<unsigned>(accountId), static_cast<int>(attendance.m_cCheckType), day,
            reward == nullptr ? "reward definition missing" : "negative reward amount"
        );
        return -3;
    }
    // CObject* const object =
    //     g_ObjectDB.GetObjectW(reward->nItemType, false)
    // Reuse the observed lookup ID in diagnostics without rereading the reward.
    const int rewardItemId = reward->nItemType;
    CObject* const object = g_ObjectDB.GetObjectW(rewardItemId, false);
    if (object == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[attendance] Cannot prepare attendance rewards for account %u (type %d, day %d): item definition %d is
        //     missing (result -3).", static_cast<unsigned>(accountId), static_cast<int>(attendance.m_cCheckType), day,
        //     rewardItemId)
        L2_LOG_ERROR(
            Log,
            "[attendance] Cannot prepare attendance rewards for account %u (type %d, day %d): item definition %d is missing "
            "(result -3).",
            static_cast<unsigned>(accountId), static_cast<int>(attendance.m_cCheckType), day, rewardItemId
        );
        return -3;
    }
    CItem* const item = static_cast<CItem*>(object);
    const bool stackable = CSharedItemData::IsStackable(item->m_data->m_ItemConsumeType);
    if (normal) {
        if (stackable) {
            rewards.Push(reward->nItemType, reward->nAmount);
        } else {
            for (std::int32_t index = 0; static_cast<std::int64_t>(index) < reward->nAmount;
                index = runtime::AddInt32Wrapping(index, 1)) {
                rewards.Push(reward->nItemType, 1);
            }
        }
    }
    if (pcCafe) {
        const std::int64_t amount =
            runtime::MultiplyInt64Wrapping(static_cast<std::int64_t>(reward->nPcCafeMultiple), reward->nAmount);
        if (stackable) {
            rewards.Push(reward->nItemType, amount);
        } else {
            for (std::int32_t index = 0; static_cast<std::int64_t>(index) < amount;
                index = runtime::AddInt32Wrapping(index, 1)) {
                rewards.Push(reward->nItemType, 1);
            }
        }
    }
    return 1;
}

bool VipAttendanceSystem::SendVipAttendanceResult(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    const unsigned int accountId = user->GetAccountID();
    UserAttendance attendance;
    if (!GetUserAttendance(accountId, attendance)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] VipAttendance no data AccountId[%d]",
        //     L"VipAttendance.cpp",
        //     0x48d,
        //     accountId)
        // Log.Add(
        //     LOG_WARN,
        //     "[attendance] Cannot send the attendance result for account %u: no local attendance record.",
        //     static_cast<unsigned>(accountId))
        L2_LOG_WARN(
            Log, "[attendance] Cannot send the attendance result for account %u: no local attendance record.",
            static_cast<unsigned>(accountId)
        );
        return false;
    }

    const int day = attendance.GetDay();
    if (attendance.GetConfirmNormalAttendance()) {
        user->SendSystemMessage_d(SystemMessageId::DAILY_ATTENDANCE_REWARD_RECEIVED, day);
    }
    if (attendance.GetConfirmPCCafeAttendance(user)) {
        user->SendSystemMessage_d(SystemMessageId::DAILY_PC_CAFE_ATTENDANCE_REWARD_RECEIVED, day);
    }
    if (attendance.GetConfirmVIPAttendance(user) == 1) {
        user->SendSystemMessage_d(SystemMessageId::DAILY_VIP_ATTENDANCE_REWARD_RECEIVED, day);
    }

    AuthUserDataInfo info;
    if (!authd.GetAuthUserDataInfo(static_cast<int>(accountId), info)) {
        // Noise: GetAuthUserDataInfo already reports the account and precise missing/not-loaded reason.
        // Log.Add(
        //     LOG_ERROR,
        //     L"FILE[%s] LINE[%d] VipAttendance AuthInfo get fail ToAuth AccountId[%d]",
        //     L"VipAttendance.cpp",
        //     0x4bd,
        //     accountId)
        return false;
    }

    CUserSocket* const socket = user->GetSocket();
    if (socket == nullptr) {
        return false;
    }

    CLogFormatter formatter;
    formatter.WriteLog4(
        0x1fce, user, nullptr, u"AfterAuth", nullptr, nullptr, static_cast<int>(info.eAttendanceType), info.nAttendanceDays,
        info.nPCCafeAttendanceFlag, info.nVIPAttendanceFlag, info.tLastAttendanceCheckTime, socket->GetDormantType(),
        g_nWorldId, 0, 0, 0, 0
    );

    AddUserAttendanceInfo(socket, info);
    UserAttendance updated;
    if (!GetUserAttendance(accountId, updated)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] VipAttendance AudInfo UpdateFail AccountId[%d]",
        //     L"VipAttendance.cpp",
        //     0x4c7,
        //     accountId)
        // Log.Add(
        //     LOG_ERROR,
        //     "[attendance] Cannot reload attendance for account %u after applying AuthD data. The result reply is
        //     skipped.", static_cast<unsigned>(accountId))
        L2_LOG_ERROR(
            Log,
            "[attendance] Cannot reload attendance for account %u after applying AuthD data; the result reply is skipped.",
            static_cast<unsigned>(accountId)
        );
        return false;
    }
    if (!updated.VerifyAttendanceData()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FILE[%s] LINE[%d] VipAttendance Verify Fail AccountId[%d] Type[%d] days[%d]",
        //     L"VipAttendance.cpp",
        //     0x4cf,
        //     accountId,
        //     static_cast<int>(updated.m_cCheckType),
        //     updated.m_nCheckNormal)
        // Log.Add(
        //     LOG_ERROR,
        //     "[attendance] Loaded attendance data for account %u is invalid (type %d, day count %d. Expected type 0..3,
        //     days 0..28), continuing with the result reply.", static_cast<unsigned>(accountId),
        //     static_cast<int>(updated.m_cCheckType), updated.m_nCheckNormal)
        L2_LOG_ERROR(
            Log,
            "[attendance] Loaded attendance data for account %u is invalid (type %d, day count %d; expected type 0..3, days "
            "0..28); continuing with the result reply.",
            static_cast<unsigned>(accountId), static_cast<int>(updated.m_cCheckType), updated.m_nCheckNormal
        );
    }

    socket->Send(
        "chccdd", 0xfe, 0x17f, 1, static_cast<int>(static_cast<char>(updated.m_nCheckNormal)), updated.m_CheckPCCafeBits,
        updated.m_CheckVIPBits
    );
    runtime::raw_atomic::ExchangeI32(&socket->m_VipAttendnaceCheck, 1);
    return true;
}

void VipAttendanceSystem::AddRewardLog(const unsigned int accountId, int* itemClassIds, std::int64_t* itemAmounts) {
    L2_GUARD_FUNCTION();
    if (itemClassIds == nullptr || itemAmounts == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] VipAttendance no reward data AccountId[%d]",
        //     L"VipAttendance.cpp",
        //     0x616,
        //     accountId)
        // Log.Add(
        //     LOG_ERROR,
        //     "[attendance] Cannot write reward accounting for account %u: item IDs %s, item amounts %s.",
        //     static_cast<unsigned>(accountId), itemClassIds ? "present" : "missing", itemAmounts ? "present" : "missing")
        L2_LOG_ERROR(
            Log, "[attendance] Cannot write reward accounting for account %u: item IDs %s, item amounts %s.",
            static_cast<unsigned>(accountId), itemClassIds ? "present" : "missing", itemAmounts ? "present" : "missing"
        );
        return;
    }

    int attendanceType = 0;
    int nextNormalDay = 0;
    int normalDay = 0;
    int pcCafeBits = 0;
    int vipBits = 0;
    UserAttendance attendance;
    if (GetUserAttendance(accountId, attendance)) {
        attendanceType = static_cast<int>(attendance.m_cCheckType);
        nextNormalDay = attendance.GetDay();
        normalDay = attendance.m_nCheckNormal;
        pcCafeBits = static_cast<int>(attendance.m_CheckPCCafeBits);
        vipBits = static_cast<int>(attendance.m_CheckVIPBits);
    }

    CLogFormatter formatter;
    CSPointer<User> user = g_UserAccountDBIDMap.FindObject(accountId);
    if (!user) {
        formatter.WriteLog(
            0x2340, 0, accountId, 0, 0, 0, 0, 0, nullptr, nullptr, nullptr, itemClassIds[0], itemAmounts[0], itemClassIds[1],
            itemAmounts[1], itemClassIds[2], itemAmounts[2], nextNormalDay, normalDay, pcCafeBits, vipBits, nullptr, nullptr,
            nullptr, nullptr, attendanceType
        );
        return;
    }

    formatter.WriteLog4(
        0x2340, user.get(), nullptr, nullptr, nullptr, nullptr, itemClassIds[0], itemAmounts[0], itemClassIds[1],
        itemAmounts[1], itemClassIds[2], itemAmounts[2], nextNormalDay, normalDay, pcCafeBits, vipBits, attendanceType
    );
}

int VipAttendanceSystem::CheckAndRequestVipAttendanceReward(User* user) {
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return -8;
    }
    UserAttendance attendance;
    const unsigned int accountId = user->GetAccountID();
    if (!GetUserAttendance(accountId, attendance)) {
        return -8;
    }

    const int day = attendance.GetDay();
    const VipAttendanceItem* reward = nullptr;
    if (attendance.m_cCheckType == 1) {
        reward = g_VipAttendanceDB.GetAttendanceNormalReward(day);
    } else if (attendance.m_cCheckType == 2) {
        reward = g_VipAttendanceDB.GetAttendanceNewbieReward(day);
    } else if (attendance.m_cCheckType == 3) {
        reward = g_VipAttendanceDB.GetAttendanceDormantReward(day);
    }

    std::array<ItemManip, 100> requests{};
    std::array<int, 3> classIds{};
    std::array<std::int64_t, 3> amounts{};
    std::array<int, 3> enchants{};
    if (reward == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] VipAttendance Invalid Item Type[%d] Day[%d] "
        //     L"AccountId[%d]",
        //     L"VipAttendance.cpp",
        //     0x529,
        //     static_cast<int>(attendance.m_cCheckType),
        //     day,
        //     accountId)
        // Log.Add(
        //     LOG_ERROR,
        //     "[attendance] Cannot request attendance rewards for account %u: no reward definition for type %d, day %d
        //     (result -3).", static_cast<unsigned>(accountId), static_cast<int>(attendance.m_cCheckType), day)
        L2_LOG_ERROR(
            Log,
            "[attendance] Cannot request attendance rewards for account %u: no reward definition for type %d, day %d "
            "(result -3).",
            static_cast<unsigned>(accountId), static_cast<int>(attendance.m_cCheckType), day
        );
        return -3;
    }

    int requestCount = 0;
    if (attendance.GetConfirmNormalAttendance()) {
        classIds[0] = reward->nItemType;
        amounts[0] = reward->nAmount;
        requests[requestCount++] = ItemManip(0, reward->nItemType, reward->nAmount, 0, 0, 0);
    }
    if (g_bUsePCCafeAttendance && attendance.GetConfirmPCCafeAttendance(user)) {
        const std::int64_t amount = static_cast<std::int64_t>(reward->nPcCafeMultiple) * reward->nAmount;
        classIds[1] = reward->nItemType;
        amounts[1] = amount;
        requests[requestCount++] = ItemManip(0, reward->nItemType, amount, 0, 0, 0);
    }
    const VipAttendanceItem* const vipReward = g_VipAttendanceDB.GetAttendanceVipReward(day);
    if (vipReward != nullptr && g_bUseVipAttendance && attendance.GetConfirmVIPAttendance(user) == 1) {
        classIds[2] = vipReward->nItemType;
        amounts[2] = vipReward->nAmount;
        requests[requestCount++] = ItemManip(0, vipReward->nItemType, vipReward->nAmount, 0, 0, 0);
    }

    EtcManiGiveItemOnAccount manipulation(
        user->GetDBID(), 0, 0, 0, 3, classIds.data(), amounts.data(), enchants.data(), 2, accountId
    );
    CPacketHelperEx<20480> packet;
    manipulation.Assemble(packet);
    User::etcMsg message = user->SaveEtcMsg(false, false, 0, 0, 0, std::u16string(), std::u16string(), 0);
    User::ManipulateQueue queue = user->SaveManipulateQueue(
        ManipulateType::MANI_VIP_ATTENDANCE, 0, requestCount, requests.data(), nullptr, 1, packet, false, ItemGetReason::Etc,
        message
    );

    const User::ManiItemResultType result = user->_SetManiItemJob(queue);
    if (result == User::MRT_NOJOB) {
        return user->_HandleItemManipulation(queue) ? 1 : -5;
    }
    if (result == User::MRT_JOBMAP) {
        user->SendSystemMessage(SystemMessageId::PREVIOUS_REQUEST_NOT_COMPLETED);
        return -5;
    }
    if (result == User::MRT_JOBNOTMAP) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[NO_ERROR][%s][%d] ManiItemQueue Insert",
        //     L"VipAttendance.cpp",
        //     0x56a)
        // Log.Add(
        //     LOG_NORMAL,
        //     "[attendance] Attendance reward operation queued for account %u (day %d, %d item requests).",
        //     static_cast<unsigned>(accountId), day, requestCount)
        L2_LOG_INFO(
            Log, "[attendance] Attendance reward operation queued for account %u (day %d, %d item requests).",
            static_cast<unsigned>(accountId), day, requestCount
        );
        return 1;
    }
    // Noise: the current _SetManiItemJob returns only the three results already handled above.
    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR][%s][%d] ManiItemQueue ERROR",
    //     L"VipAttendance.cpp",
    //     0x56f)
    return -5;
}

void VipAttendanceSystem::SendToAuthResetAttendance(
    const unsigned int accountId, const char attendanceType, wchar_t* const reasonText
) {
    // Current L2Server 0x00c4bd9c.
    L2_GUARD_FUNCTION();

    CSPointer<User> user = g_UserAccountDBIDMap.FindObject(accountId);
    if (!user || user->GetSocket() == nullptr) {
        return;
    }

    UserAttendance attendance;
    if (VipAttendanceSystem::Inst().GetUserAttendance(accountId, attendance)) {
        std::array<char16_t, 50> reason{};
        if (reasonText != nullptr) {
            std::size_t length = 0;
            while (length < reason.size() && reasonText[length] != L'\0') {
                ++length;
            }
            if (length >= reason.size()) {
                reason[0] = L'\0';
                runtime::process::HandleInvalidParameter(L"wcscpy_s");
            } else {
                std::copy_n(reasonText, length + 1, reason.data());
            }
        }

        const int day = attendance.GetDay();
        CLogFormatter formatter;
        formatter.WriteLog4(
            0x1fcc, user.get(), nullptr, nullptr, nullptr, reason.data(), attendanceType, day, attendance.m_nCheckNormal,
            static_cast<int>(attendance.m_CheckPCCafeBits), static_cast<int>(attendance.m_CheckVIPBits), 0, 0, 0, 0, 0, 0
        );
    }

    authd.AddAuthUserDataAttendanceInfo(
        static_cast<int>(accountId), static_cast<NEW_AUTH_USER_DATA::ATTENDANCE_TYPE>(attendanceType), 0, 0, 0, 0,
        NEW_AUTH_USER_DATA::AJ_RESET
    );
    authd.AuthRequestGetUserData(
        static_cast<int>(accountId), NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_WRITE,
        NEW_AUTH_USER_DATA_FOR_SERVER::REASON_FOR_ATTENDANCE
    );
}

bool VipAttendanceSystem::SendToAuthUpdateVipAttendance(
    const unsigned int accountId,
    const char attendanceType,
    const int attendanceDay,
    const int pcCafeAttendanceFlag,
    const int vipAttendanceFlag,
    const int lastAttendanceCheckTime,
    const bool builder
) {
    // Current L2Server 0x00c4bb94, original owner VipAttendance.cpp.
    L2_GUARD_FUNCTION();

    UserAttendance attendance;
    if (!GetUserAttendance(accountId, attendance)) {
        // Log.Add(
        //     LOG_WARN,
        //     "[attendance] Cannot request an attendance update for account %u: no local attendance record.",
        //     static_cast<unsigned>(accountId))
        L2_LOG_WARN(
            Log, "[attendance] Cannot request an attendance update for account %u: no local attendance record.",
            static_cast<unsigned>(accountId)
        );
        return false;
    }
    authd.AddAuthUserDataAttendanceInfo(
        static_cast<int>(accountId), static_cast<NEW_AUTH_USER_DATA::ATTENDANCE_TYPE>(attendanceType),
        static_cast<unsigned char>(attendanceDay), lastAttendanceCheckTime, pcCafeAttendanceFlag, vipAttendanceFlag,
        builder ? NEW_AUTH_USER_DATA::AJ_SET_BUILDER : NEW_AUTH_USER_DATA::AJ_UPDATE
    );
    if (authd.AuthRequestGetUserData(
            static_cast<int>(accountId), NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_WRITE,
            NEW_AUTH_USER_DATA_FOR_SERVER::REASON_FOR_ATTENDANCE
        )) {
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%S][%d] Save To Auth Fail!!",
    //     "VipAttendance.cpp",
    //     0x5a5)
    // Log.Add(
    //     LOG_WARN,
    //     "[attendance] Attendance update for account %u was not submitted to AuthD. Restoring the previous values in the
    //     local AuthD user-data cache.", static_cast<unsigned>(accountId))
    L2_LOG_WARN(
        Log,
        "[attendance] Attendance update for account %u was not submitted to AuthD; restoring the previous values in the "
        "local AuthD user-data cache.",
        static_cast<unsigned>(accountId)
    );
    authd.AddAuthUserDataAttendanceInfo(
        static_cast<int>(accountId), static_cast<NEW_AUTH_USER_DATA::ATTENDANCE_TYPE>(attendance.m_cCheckType),
        static_cast<unsigned char>(attendance.m_nCheckNormal), attendance.m_LastCheckTime,
        static_cast<int>(attendance.m_CheckPCCafeBits), static_cast<int>(attendance.m_CheckVIPBits),
        NEW_AUTH_USER_DATA::AJ_ROLLBACK
    );
    return false;
}

bool VipAttendanceSystem::ProgressCheckAttendanceData(User* user) {
    // Current L2Server 0x00c4cadc, original owner VipAttendance.cpp.
    L2_GUARD_FUNCTION();

    UserAttendance attendance;
    const unsigned int accountId = user->GetAccountID();
    if (!GetUserAttendance(accountId, attendance)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"FILE[%s] LINE[%d] VipAttendance no data AccountId[%d]",
        //     L"VipAttendance.cpp",
        //     0x413,
        //     accountId)
        // Log.Add(
        //     LOG_WARN,
        //     "[attendance] Cannot advance attendance for account %u: no local attendance record.",
        //     static_cast<unsigned>(accountId))
        L2_LOG_WARN(
            Log, "[attendance] Cannot advance attendance for account %u: no local attendance record.",
            static_cast<unsigned>(accountId)
        );
        return false;
    }

    const int day = attendance.GetDay();
    static_cast<void>(attendance.GetConfirmNormalAttendance());
    const bool pcCafe = attendance.GetConfirmPCCafeAttendance(user);
    const int vipResult = attendance.GetConfirmVIPAttendance(user);
    int pcCafeAttendanceFlag = static_cast<int>(attendance.m_CheckPCCafeBits);
    int vipAttendanceFlag = static_cast<int>(attendance.m_CheckVIPBits);
    if (pcCafe) {
        pcCafeAttendanceFlag = attendance.GetNextPCCafeCheck(day);
    }
    if (vipResult == 1) {
        vipAttendanceFlag = static_cast<int>(attendance.GetNextVIPCheck(day));
    }
    return SendToAuthUpdateVipAttendance(
        accountId, attendance.m_cCheckType, day, pcCafeAttendanceFlag, vipAttendanceFlag, runtime::crt::Time32Now(), false
    );
}

void VipAttendanceSystem::SendToAuthRollbackVipAttendance(unsigned int accountId) {
    L2_GUARD_FUNCTION();
    UserAttendance attendance;
    if (!GetUserAttendance(accountId, attendance)) {
        return;
    }
    authd.AddAuthUserDataAttendanceInfo(
        static_cast<int>(accountId), static_cast<NEW_AUTH_USER_DATA::ATTENDANCE_TYPE>(attendance.m_cCheckType),
        static_cast<unsigned char>(attendance.m_nCheckNormal), attendance.m_LastCheckTime,
        static_cast<int>(attendance.m_CheckPCCafeBits), static_cast<int>(attendance.m_CheckVIPBits),
        NEW_AUTH_USER_DATA::AJ_ROLLBACK
    );
    authd.AuthRequestGetUserData(
        static_cast<int>(accountId), NEW_AUTH_USER_DATA_FOR_SERVER::TYPE_WRITE,
        NEW_AUTH_USER_DATA_FOR_SERVER::REASON_FOR_ATTENDANCE
    );
}

void VipAttendanceSystem::ReceiveToAuthResetVipAttendance(unsigned int accountId, char result, AuthUserDataInfo info) {
    L2_GUARD_FUNCTION();
    CSPointer<User> user = g_UserAccountDBIDMap.FindObject(accountId);
    if (!user || user->GetSocket() == nullptr || result < 1) {
        if (result < 1) {
            // Log.Add(
            //     LOG_WARN,
            //     "[attendance] AuthD did not confirm the attendance reset for account %u (result %d). Local attendance is
            //     unchanged.", static_cast<unsigned>(accountId), static_cast<int>(result))
            L2_LOG_WARN(
                Log,
                "[attendance] AuthD did not confirm the attendance reset for account %u (result %d); local attendance is "
                "unchanged.",
                static_cast<unsigned>(accountId), static_cast<int>(result)
            );
        }
        return;
    }
    AddUserAttendanceInfo(user->GetSocket(), info);
    SendVipAttendanceItemList(user.get());
}
