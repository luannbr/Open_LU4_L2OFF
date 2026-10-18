#include "SSQ.h"
#include "CConfig.h"
#include "DB.h"
#include "MainEvent.h"
#include "MakerEvent.h"
#include "NpcServer.h"
#include "Pledge.h"
#include "Protocol/src/SystemMessageIds.h"
#include "Residence.h"
#include "Shared/src/ThreadSafeLibrary.h"
#include "User.h"
#include "UserBroadcaster.h"
#include "UserSocket.h"

#include "Shared/src/DXPacket.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/StandardContainers.h"

#include <chrono>
#include <cstdint>
#include <cstring>

namespace {

#pragma pack(push, 4)

struct SSQStatusPacket : CStackOnly {
    int status;
};


struct SSQPeriodPacket : CStackOnly {
    int status;
    int startTime;
    int endTime;
};


struct SSQChangeSealSelectionCountPacket : CStackOnly {
    int errorCode;
    SSQPart::type part;
    SSQSeal::type seal;
    int count;
};

struct SSQChangeUserPointPacket : CStackOnly {
    int errorCode;
    int userDatabaseId;
    int userServerId;
    SSQPart::type part;
    std::int64_t point;
};

struct SSQChangeSealOwnerPacket : CStackOnly {
    int errorCode;
    SSQSeal::type seal;
    SSQPart::type part;
};

struct SSQChangeWinnerPacket : CStackOnly {
    int errorCode;
    SSQPart::type part;
};

struct SSQAddMemberPacket : CStackOnly {
    int errorCode;
    int userDatabaseId;
    int userServerId;
    int round;
    SSQPart::type part;
    SSQSeal::type seal;
    SSQPosition::type position;
    std::int64_t initialPoint;
    int joinType;
};

struct SSQDismissMemberPacket : CStackOnly {
    int errorCode;
    int userDatabaseId;
    int userServerId;
    SSQPart::type fromPart;
    std::int64_t point;
    SSQSeal::type seal;
};

struct SSQStartQuickCyclePacket : CStackOnly {
    int errorCode;
    SSQStatus::type status;
    int eventStartTime;
    int eventEndTime;
    int sealEffectTime;
    int sealEffectEndTime;
};

struct SSQChangeCycleModePacket : CStackOnly {
    int errorCode;
    int cycleModeFlag;
};

#pragma pack(pop)


using NpcPosEventNameMapType = std::map<std::pair<int, int>, std::u16string>;

NpcPosEventNameMapType NpcPosEventNameMap;

[[maybe_unused]] bool IsSet_1_0 = !(NpcPosEventNameMap[{1, 0}] = std::u16string(u"ssq_seal1_none")).empty();
[[maybe_unused]] bool IsSet_1_1 = !(NpcPosEventNameMap[{1, 1}] = std::u16string(u"ssq_seal1_twilight")).empty();
[[maybe_unused]] bool IsSet_1_2 = !(NpcPosEventNameMap[{1, 2}] = std::u16string(u"ssq_seal1_dawn")).empty();
[[maybe_unused]] bool IsSet_2_0 = !(NpcPosEventNameMap[{2, 0}] = std::u16string(u"ssq_seal2_none")).empty();
[[maybe_unused]] bool IsSet_2_1 = !(NpcPosEventNameMap[{2, 1}] = std::u16string(u"ssq_seal2_twilight")).empty();
[[maybe_unused]] bool IsSet_2_2 = !(NpcPosEventNameMap[{2, 2}] = std::u16string(u"ssq_seal2_dawn")).empty();
[[maybe_unused]] bool IsSet_3_0 = !(NpcPosEventNameMap[{3, 0}] = std::u16string(u"ssq_seal3_none")).empty();
[[maybe_unused]] bool IsSet_3_1 = !(NpcPosEventNameMap[{3, 1}] = std::u16string(u"ssq_seal3_twilight")).empty();
[[maybe_unused]] bool IsSet_3_2 = !(NpcPosEventNameMap[{3, 2}] = std::u16string(u"ssq_seal3_dawn")).empty();

void DisableAllNpcPosEvent(NpcPosEventNameMapType::value_type& entry) { DisableNpcPosEvent(entry.second.c_str()); }

std::int32_t CurrentUnixSeconds() noexcept {
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::uint32_t bits = static_cast<std::uint32_t>(seconds);
    std::int32_t value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

TimeValue CurrentTimeValue() noexcept {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now);
    const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(now - seconds);
    return TimeValue(static_cast<std::int32_t>(seconds.count()), static_cast<std::int32_t>(microseconds.count()));
}

void PushSSQSkillToUser(User* const user, const int skillId, const int level) {
    // Current L2Server 0x00ad42f4.
    L2_GUARD_FUNCTION();
    user->AcquireSkill(skillId, level, 0, true, false);
    user->ValidateChanged(VB_ITEMSKILL | VB_SKILLLIST | VB_SKILLMOD | VB_HPMPCP);
    user->SendUserHPInfo(4, 0, false);
}

} // namespace

CastlePledgeSnapshot::CastlePledgeSnapshot(const int roundNumber) :
    mRoundNumber(roundNumber),
    mPadding01c{},
    mPledgeIndex(0),
    mUserIndex(0) {}

CastlePledgeSnapshot::~CastlePledgeSnapshot() = default;

void CastlePledgeSnapshot::AddPledgeInfo(CPledge* const pledge) {
    // Current L2Server 0x00ad810c.
    L2_GUARD_FUNCTION();
    if (pledge == nullptr) {
        return;
    }

    mPledgeDBIDSet.insert(runtime::Int32FromBits(pledge->GetDbId()));
    std::vector<std::uint32_t> memberIds;
    pledge->GetAllMemberId(memberIds);
    for (const std::uint32_t memberId : memberIds) {
        mUserIDSet.insert(runtime::Int32FromBits(memberId));
    }
}

void CastlePledgeSnapshot::Start() {
    // Current L2Server 0x00ad82b0 does not clear either list.
    L2_GUARD_FUNCTION();
    mPledgeDBIDList.reserve(mPledgeDBIDSet.size());
    mUserIDList.reserve(mUserIDSet.size());
    for (const int pledgeId : mPledgeDBIDSet) {
        mPledgeDBIDList.push_back(pledgeId);
    }
    mPledgeIndex = 0;
    for (const int userId : mUserIDSet) {
        mUserIDList.push_back(userId);
    }
    mUserIndex = 0;
    AddTimer(200, 0);
}

void CastlePledgeSnapshot::TimerExpired(int) {
    // Current L2Server 0x00ad729c.
    L2_GUARD_FUNCTION();

    for (int count = 0; static_cast<std::size_t>(mPledgeIndex) < mPledgeDBIDList.size() && count < 3; ++count) {
        db.SetDawnRoundToPledge(mPledgeDBIDList[static_cast<std::size_t>(mPledgeIndex)], mRoundNumber);
        ++mPledgeIndex;
    }

    for (int count = 0; static_cast<std::size_t>(mUserIndex) < mUserIDList.size() && count < 100; ++count) {
        CCreature* const creature = g_CreatureSmartIdManager.GetObjectW(
            static_cast<std::uint32_t>(mUserIDList[static_cast<std::size_t>(mUserIndex)])
        );
        User* const user = creature != nullptr && creature->IsPC() ? creature->CastUser() : nullptr;
        if (user != nullptr) {
            {
                L2_GUARD(*user->m_lock);
                user->m_data->m_nSSQRoundNumber = mRoundNumber;
            }
        }
        ++mUserIndex;
    }

    if (static_cast<std::int64_t>(mPledgeIndex) == static_cast<std::int64_t>(mPledgeDBIDList.size()) &&
        static_cast<std::int64_t>(mUserIndex) == static_cast<std::int64_t>(mUserIDList.size())) {
        L2_RELEASE(this, ObjectRefType::CreateDelete);
    } else {
        AddTimer(200, 0);
    }
}

SSQSystem::SSQSystem() :
    mCacheDWaiter(false, nullptr),
    mTimerEnabled(true),
    mPadding039{},
    mSharedInfo(SharedSSQInfo::GetInfo()),
    mCollectPointPortion{},
    mMainEventPointPortion{},
    mTotalPointPortion{},
    mCycleMode(0),
    mHalfCycleInterval(0x93a80),
    mAccountInterval(900),
    mPadding09c(0) {}

SSQSystem::~SSQSystem() = default;

SSQSystem& SSQSystem::inst() {
    static SSQSystem instance;
    return instance;
}

void SSQSystem::Notify() { mCacheDWaiter.Signal(); }

std::int64_t SSQSystem::GetCollectedPoint(const SSQPart::type part) const {
    // Current L2Server 0x004d3498.
    L2_GUARD_FUNCTION();
    if (part == SSQPart::DAWN || part == SSQPart::TWILIGHT) {
        return mSharedInfo->mCollectedPoint[part];
    }
    return 0;
}

std::int64_t SSQSystem::GetMainEventPoint(const SSQPart::type part) const {
    // Current L2Server 0x004d3548.
    L2_GUARD_FUNCTION();
    if (part == SSQPart::DAWN || part == SSQPart::TWILIGHT) {
        return mSharedInfo->mMainEventPoint[part];
    }
    return 0;
}

int SSQSystem::GetMemberCount(const SSQPart::type part) const {
    // Current L2Server 0x004d35f8.
    L2_GUARD_FUNCTION();
    if (part == SSQPart::DAWN || part == SSQPart::TWILIGHT) {
        return mSharedInfo->mMemberCount[part];
    }
    return 0;
}

int SSQSystem::GetSealSelectionCount(const SSQPart::type part, const SSQSeal::type seal) const {
    // Current L2Server 0x004d36ac performs no range checks.
    L2_GUARD_FUNCTION();
    return mSharedInfo->mSealSelectionCount[part][seal];
}

bool SSQSystem::SetCollectedPoint(const SSQPart::type part, const std::int64_t point) {
    // Current L2Server 0x006d44a4 uses an unsigned upper-bound comparison.
    // negative values therefore fail the same gate.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint64_t>(point) > UINT64_C(999999999999)) {
        return false;
    }
    mSharedInfo->mCollectedPoint[part] = point;
    ValidateCollectPointPortion();
    return true;
}

void SSQSystem::ValidateTotalPortion() {
    L2_GUARD_FUNCTION();
    mTotalPointPortion[SSQPart::TWILIGHT] =
        mMainEventPointPortion[SSQPart::TWILIGHT] + mCollectPointPortion[SSQPart::TWILIGHT];
    mTotalPointPortion[SSQPart::DAWN] = mMainEventPointPortion[SSQPart::DAWN] + mCollectPointPortion[SSQPart::DAWN];
}

void SSQSystem::ValidateCollectPointPortion() {
    L2_GUARD_FUNCTION();

    double twilightPercent = 0.0;
    double dawnPercent = 0.0;

    const std::int64_t twilightPoint = mSharedInfo->mCollectedPoint[SSQPart::TWILIGHT];
    if (twilightPoint != 0) {
        const std::int64_t totalPoint =
            runtime::AddInt64Wrapping(mSharedInfo->mCollectedPoint[SSQPart::DAWN], twilightPoint);
        twilightPercent = static_cast<double>(twilightPoint) * 100.0 / static_cast<double>(totalPoint);
    }

    const std::int64_t dawnPoint = mSharedInfo->mCollectedPoint[SSQPart::DAWN];
    if (dawnPoint != 0) {
        const std::int64_t totalPoint =
            runtime::AddInt64Wrapping(dawnPoint, mSharedInfo->mCollectedPoint[SSQPart::TWILIGHT]);
        dawnPercent = static_cast<double>(dawnPoint) * 100.0 / static_cast<double>(totalPoint);
    }

    mCollectPointPortion[SSQPart::TWILIGHT] = (twilightPercent / 100.0) * 50.0 * 10.0;
    mCollectPointPortion[SSQPart::DAWN] = (dawnPercent / 100.0) * 50.0 * 10.0;
    ValidateTotalPortion();
}

void SSQSystem::ValidateMainEventPortion() {
    L2_GUARD_FUNCTION();
    mMainEventPointPortion[SSQPart::TWILIGHT] =
        static_cast<double>(TimeAttackBoard::inst().GetTotalPoint(SSQPart::TWILIGHT));
    mMainEventPointPortion[SSQPart::DAWN] = static_cast<double>(TimeAttackBoard::inst().GetTotalPoint(SSQPart::DAWN));
    ValidateTotalPortion();
}

void SSQSystem::SetCycleModeFlag(const int cycleModeFlag) {
    L2_GUARD_FUNCTION();

    const std::uint32_t bits = static_cast<std::uint32_t>(cycleModeFlag);
    const std::uint32_t mode = (bits >> 16) & 0x0f;
    const std::uint32_t value = bits & 0xffff;
    if (mode == 0) {
        mCycleMode = 0;
        if (value == 0) {
            mHalfCycleInterval = 0x93a80;
        } else {
            mHalfCycleInterval = runtime::MultiplyInt32Wrapping(runtime::Int32FromBits(value), 0x93a80) / 2;
        }
        mAccountInterval = 900;
    } else if (mode == 1) {
        mCycleMode = 1;
        mHalfCycleInterval = static_cast<int>(value);
        mAccountInterval = 60;
    } else if (mode == 2) {
        mCycleMode = 2;
    }
}

const unsigned char* SSQSystem::SetAll(const unsigned char* packet) {
    L2_GUARD_FUNCTION();

    packet = ::Disassemble(packet, "d", &mSharedInfo->mRoundNumber);
    packet = ::Disassemble(packet, "d", &mSharedInfo->mCurrentStatus);
    packet = ::Disassemble(packet, "d", &mSharedInfo->mWinner);
    for (int seal = SSQSeal::AVARICE; seal <= SSQSeal::AMAGEDON; ++seal) {
        packet = ::Disassemble(packet, "d", &mSharedInfo->mSealOwner[seal]);
    }

    packet = ::Disassemble(
        packet, "dddd", &mSharedInfo->mEventStartTime, &mSharedInfo->mEventEndTime, &mSharedInfo->mSealEffectTime,
        &mSharedInfo->mSealEffectEndTime
    );

    for (int part = SSQPart::TWILIGHT; part <= SSQPart::DAWN; ++part) {
        packet = ::Disassemble(
            packet, "QQQd", &mSharedInfo->mTotalPoint[part], &mSharedInfo->mCollectedPoint[part],
            &mSharedInfo->mMainEventPoint[part], &mSharedInfo->mMemberCount[part]
        );
        for (int seal = SSQSeal::AVARICE; seal <= SSQSeal::AMAGEDON; ++seal) {
            packet = ::Disassemble(packet, "d", &mSharedInfo->mSealSelectionCount[part][seal]);
        }
    }

    packet = ::Disassemble(packet, "d", &mSharedInfo->m_nCanDropGuard);
    int cycleModeFlag = 0;
    packet = ::Disassemble(packet, "d", &cycleModeFlag);
    SetCycleModeFlag(cycleModeFlag);
    packet = ::Disassemble(packet, "d", &mSharedInfo->mPrevWinner);
    ValidateCollectPointPortion();
    return packet;
}

SSQPart::type SSQSystem::GetSealOwner(const SSQSeal::type seal) const {
    L2_GUARD_FUNCTION();
    return mSharedInfo->mSealOwner[seal];
}

int SSQSystem::GetMaxTaxRateOfCastle() const {
    // Current L2Server 0x00ad5314.
    L2_GUARD_FUNCTION();
    switch (GetSealOwner(SSQSeal::STRIFE)) {
        case SSQPart::TWILIGHT:
            return 5;
        case SSQPart::DAWN:
            return 25;
        default:
            return 15;
    }
}

bool SSQSystem::SetDropGuardStatus(const bool status) {
    // Current L2Server 0x00ad4a68.
    L2_GUARD_FUNCTION();
    mSharedInfo->m_nCanDropGuard = status != false;
    return true;
}

bool SSQSystem::ChangeDropGuardStatus(const bool status) {
    // Current L2Server 0x00ad6664.
    L2_GUARD_FUNCTION();
    const bool changed = (mSharedInfo->m_nCanDropGuard != 0) != status;
    if (changed) {
        SetDropGuardStatus(status);
        db.ChangeSSQDropGuardStatus(status);
    }
    return changed;
}

bool SSQSystem::ChangePeriod(const int status, const int startTime, const int endTime) {
    // Current L2Server 0x00ad6958.
    L2_GUARD_FUNCTION();
    SSQPeriodPacket packet{};
    packet.status = status;
    packet.startTime = startTime;
    packet.endTime = endTime;
    db.RequestTPacketProcess(g_nWorldId, 5, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool SSQSystem::ChangeSealSelectionCount(const SSQPart::type part, const SSQSeal::type seal, const int count) {
    // Current L2Server 0x00ad4de0.
    L2_GUARD_FUNCTION();
    if (mSharedInfo->mCurrentStatus != SSQStatus::EVENT || static_cast<std::uint32_t>(part) - 1U >= 2U ||
        static_cast<std::uint32_t>(seal) - 1U >= 3U) {
        return false;
    }

    SSQChangeSealSelectionCountPacket packet{};
    packet.errorCode = 0;
    packet.part = part;
    packet.seal = seal;
    packet.count = count;
    db.RequestTPacketProcess(g_nWorldId, 0x0f, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool SSQSystem::ChangeUserPoint(User* const user, const int point) {
    // Current L2Server 0x00ad4ee8 has no null-user gate.
    L2_GUARD_FUNCTION();
    if (user->m_data->m_nSSQRoundNumber != mSharedInfo->mRoundNumber) {
        return false;
    }
    if (point < 0 || static_cast<std::uint32_t>(point) >= UINT32_C(0x80000000)) {
        return false;
    }

    const std::uint32_t result = [&]() -> std::uint32_t {
        L2_GUARD(mLock);
        const std::int32_t collected =
            runtime::Int32FromBits(static_cast<std::uint32_t>(GetCollectedPoint(user->GetSSQPart())));
        const std::int32_t mainEvent =
            runtime::Int32FromBits(static_cast<std::uint32_t>(GetMainEventPoint(user->GetSSQPart())));
        const std::uint32_t result = static_cast<std::uint32_t>(collected) + static_cast<std::uint32_t>(point) +
            static_cast<std::uint32_t>(mainEvent);
        return result;
    }();

    if (runtime::Int32FromBits(result) < 0 || result >= UINT32_C(0x80000000)) {
        return false;
    }

    SSQChangeUserPointPacket packet{};
    const SSQPart::type packetPart = user->GetSSQPart();
    const int packetServerId = runtime::Int32FromBits(user->GetServerId());
    const int packetDatabaseId = runtime::Int32FromBits(user->GetDBID());
    packet.errorCode = 0;
    packet.userDatabaseId = packetDatabaseId;
    packet.userServerId = packetServerId;
    packet.part = packetPart;
    packet.point = static_cast<std::int64_t>(point);
    db.RequestTPacketProcess(user->m_data->m_nWorldId, 0x10, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool SSQSystem::ChangeSealOwner(const SSQSeal::type seal, const SSQPart::type part) {
    // Current L2Server 0x00ad5098.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(seal) - 1U >= 3U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid seal number=[%d]",
        //     L"SSQ.cpp",
        //     0x2a7,
        //     seal)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid seal number=[%d]",
        //     L"SSQ.cpp",
        //     0x2a7,
        //     seal)
        L2_LOG_ERROR(Log, "Cannot change Seven Signs seal owner: seal %d is outside 1..3.", static_cast<int>(seal));
        return false;
    }
    if (static_cast<std::uint32_t>(part) >= 3U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid seal owner=[%d]",
        //     L"SSQ.cpp",
        //     0x2ad,
        //     part)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid seal owner=[%d]",
        //     L"SSQ.cpp",
        //     0x2ad,
        //     part)
        L2_LOG_ERROR(
            Log, "Cannot change Seven Signs seal %d owner: faction %d is outside 0..2.", static_cast<int>(seal),
            static_cast<int>(part)
        );
        return false;
    }

    {
        L2_GUARD(mLock);
        mSharedInfo->mSealOwner[seal] = part;
    }

    SSQChangeSealOwnerPacket packet{};
    packet.errorCode = 0;
    packet.seal = seal;
    packet.part = part;
    db.RequestTPacketProcess(g_nWorldId, 0x15, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool SSQSystem::ChangeWinner(const SSQPart::type part) {
    // Current L2Server 0x00ad5210.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(part) >= 3U) {
        return false;
    }

    {
        L2_GUARD(mLock);
        mSharedInfo->mWinner = part;
    }

    SSQChangeWinnerPacket packet{};
    packet.errorCode = 0;
    packet.part = part;
    db.RequestTPacketProcess(g_nWorldId, 0x16, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool SSQSystem::AddMember(
    User* const user,
    const SSQPart::type part,
    const SSQSeal::type seal,
    const SSQPosition::type position,
    const std::int64_t initialPoint,
    const int joinType
) {
    // Current L2Server 0x00ad5a80.
    L2_GUARD_FUNCTION();
    if (user == nullptr || static_cast<std::uint32_t>(part) - 1U >= 2U || static_cast<std::uint32_t>(seal) - 1U >= 3U ||
        mSharedInfo->mCurrentStatus != SSQStatus::EVENT) {
        return false;
    }
    if (user->GetSSQPart() != SSQPart::NONE) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] AddSSQMember, Already member[%d], nRound[%d]",
        //     L"SSQ.cpp",
        //     0x78b,
        //     user->GetDBID(),
        //     mSharedInfo->mRoundNumber)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] AddSSQMember, Already member[%d], nRound[%d]",
        //     L"SSQ.cpp",
        //     0x78b,
        //     user->GetDBID(),
        //     mSharedInfo->mRoundNumber)
        L2_LOG_WARN(
            Log, "Seven Signs registration rejected for character %u in round %d: already a member.",
            static_cast<unsigned>(user->GetDBID()), mSharedInfo->mRoundNumber
        );
        return false;
    }
    if (static_cast<std::uint64_t>(initialPoint) > UINT64_C(999999999999)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_PROCEED_CONTRIBUTION_LIMIT_EXCEEDED);
        return false;
    }

    const std::uint64_t result = [&]() -> std::uint64_t {
        L2_GUARD(mLock);
        const std::uint64_t result = static_cast<std::uint64_t>(initialPoint) +
            static_cast<std::uint64_t>(GetMainEventPoint(SSQPart::DAWN)) +
            static_cast<std::uint64_t>(GetCollectedPoint(SSQPart::TWILIGHT));
        return result;
    }();

    if (result >= UINT64_C(1000000000000)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_PROCEED_CONTRIBUTION_LIMIT_EXCEEDED);
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SSQSystem::AddMember() : Too large system point, result[%d], delta[%d]",
        //     L"SSQ.cpp",
        //     0x7a1,
        //     result,
        //     initialPoint)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] SSQSystem::AddMember() : Too large system point, result[%d], delta[%d]",
        //     L"SSQ.cpp",
        //     0x7a1,
        //     result,
        //     initialPoint)
        L2_LOG_WARN(
            Log,
            "Seven Signs registration rejected for character %u: system point result %llu exceeds 999999999999 (requested "
            "contribution %lld).",
            static_cast<unsigned>(user->GetDBID()), static_cast<unsigned long long>(result),
            static_cast<long long>(initialPoint)
        );
        return false;
    }

    SSQAddMemberPacket packet{};
    const int packetRound = mSharedInfo->mRoundNumber;
    const int packetServerId = runtime::Int32FromBits(user->GetServerId());
    const int packetDatabaseId = runtime::Int32FromBits(user->GetDBID());
    packet.errorCode = 0;
    packet.userDatabaseId = packetDatabaseId;
    packet.userServerId = packetServerId;
    packet.round = packetRound;
    packet.part = part;
    packet.seal = seal;
    packet.position = position;
    packet.initialPoint = initialPoint;
    packet.joinType = joinType;
    db.RequestTPacketProcess(user->m_data->m_nWorldId, 0x0c, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool SSQSystem::DismissMember(User* const user) {
    // Current L2Server 0x00ad5d2c.
    L2_GUARD_FUNCTION();
    if (user == nullptr || mSharedInfo->mCurrentStatus != SSQStatus::EVENT || user->GetSSQPart() == SSQPart::NONE) {
        return false;
    }

    const SSQSeal::type packetSeal = user->GetSealSelectionNo();
    const std::int64_t packetPoint = user->GetSSQPoint();
    const SSQPart::type packetPart = user->GetSSQPart();
    const int packetServerId = runtime::Int32FromBits(user->GetServerId());
    const int packetDatabaseId = runtime::Int32FromBits(user->GetDBID());
    SSQDismissMemberPacket packet{};
    packet.errorCode = 0;
    packet.userDatabaseId = packetDatabaseId;
    packet.userServerId = packetServerId;
    packet.fromPart = packetPart;
    packet.point = packetPoint;
    packet.seal = packetSeal;
    db.RequestTPacketProcess(user->m_data->m_nWorldId, 0x17, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool SSQSystem::StartQuickCycle(const int seconds) {
    // Current L2Server 0x00ad5e90.
    L2_GUARD_FUNCTION();
    if (seconds <= 0) {
        return false;
    }

    TimeValue eventStart = CurrentTimeValue();
    eventStart += TimeValue(seconds, 0);
    TimeValue eventEnd = eventStart;
    eventEnd += TimeValue(mHalfCycleInterval, 0);
    TimeValue sealStart = eventEnd;
    sealStart += TimeValue(mAccountInterval, 0);
    TimeValue sealEnd = sealStart;
    sealEnd += TimeValue(mHalfCycleInterval, 0);
    sealEnd -= TimeValue(mAccountInterval, 0);

    SSQStartQuickCyclePacket packet{};
    packet.errorCode = 0;
    packet.status = SSQStatus::NONE;
    packet.eventStartTime = eventStart.Sec();
    packet.eventEndTime = eventEnd.Sec();
    packet.sealEffectTime = sealStart.Sec();
    packet.sealEffectEndTime = sealEnd.Sec();
    db.RequestTPacketProcess(g_nWorldId, 0x11, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool SSQSystem::DeleteDepositedItem(
    User* const user, const SSQPart::type part, const SSQItem::type itemType, const std::int64_t count
) {
    // Current L2Server 0x00ad601c.
    L2_GUARD_FUNCTION();
    if (count == 0 || static_cast<std::uint32_t>(part) - 1U >= 2U || static_cast<std::uint32_t>(itemType) > 2U ||
        user == nullptr) {
        return false;
    }

    const int userServerId = runtime::Int32FromBits(user->GetServerId());
    const int userDatabaseId = runtime::Int32FromBits(user->GetDBID());
    DxDeleteDepositedSevenSignQuestItem packet(userDatabaseId, userServerId, part, itemType, count);
    db.RequestTPacketProcess(user->m_data->m_nWorldId, 0x0d, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool SSQSystem::DepositItemEx(
    User* const user, const SSQPart::type part, std::int64_t countList[], const std::int64_t point
) {
    // Current L2Server 0x00ad6158.
    L2_GUARD_FUNCTION();
    constexpr std::int64_t kMaximumPoint = INT64_C(999999999999);

    if (static_cast<std::uint32_t>(part) - 1U >= 2U) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s][%d] SSQSystem::DepositItemEx() : Invalid part[%d]",
        //     L"SSQ.cpp",
        //     0x838,
        //     part)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] SSQSystem::DepositItemEx() : Invalid part[%d]",
        //     L"SSQ.cpp",
        //     0x838,
        //     part)
        L2_LOG_ERROR(Log, "Seven Signs deposit rejected: faction %d is outside 1..2.", static_cast<int>(part));
        return false;
    }
    if (user == nullptr) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s][%d] SSQSystem::DepositItemEx() : Invalid user",
        //     L"SSQ.cpp",
        //     0x83f)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] SSQSystem::DepositItemEx() : Invalid user",
        //     L"SSQ.cpp",
        //     0x83f)
        L2_LOG_ERROR(Log, "Seven Signs deposit rejected: player is missing.");
        return false;
    }
    if (user->GetSSQPart() != part) {
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s][%d] SSQSystem::DepositItemEx() : Invalid user part[%d], part[%d]",
        //     L"SSQ.cpp",
        //     0x845,
        //     user->GetSSQPart(),
        //     part)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] SSQSystem::DepositItemEx() : Invalid user part[%d], part[%d]",
        //     L"SSQ.cpp",
        //     0x845,
        //     user->GetSSQPart(),
        //     part)
        L2_LOG_WARN(
            Log, "Seven Signs deposit rejected for character %u: player faction %d differs from requested faction %d.",
            static_cast<unsigned>(user->GetDBID()), static_cast<int>(user->GetSSQPart()), static_cast<int>(part)
        );
        return false;
    }

    std::int64_t totalCount = 0;
    for (int index = 0; index < 3; ++index) {
        const std::int64_t count = countList[index];
        totalCount = runtime::AddInt64Wrapping(totalCount, count);
        if (count < 0 || count > kMaximumPoint) {
            user->SendSystemMessage(SystemMessageId::CANNOT_PROCEED_CONTRIBUTION_LIMIT_EXCEEDED);
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d] SSQSystem::DepositItemEx() : Too large count[%d]",
            //     L"SSQ.cpp",
            //     0x858,
            //     countList[index])
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] SSQSystem::DepositItemEx() : Too large count[%d]",
            //     L"SSQ.cpp",
            //     0x858,
            //     countList[index])
            L2_LOG_WARN(
                Log,
                "Seven Signs deposit rejected for character %u: item category %d count %lld is outside 0..999999999999.",
                static_cast<unsigned>(user->GetDBID()), index, static_cast<long long>(countList[index])
            );
            return false;
        }

        const std::int64_t depositedCount = user->GetDepositedSSQItemCount(part, index);
        const std::int64_t result = runtime::AddInt64Wrapping(depositedCount, count);
        if (static_cast<std::uint64_t>(result) > static_cast<std::uint64_t>(kMaximumPoint)) {
            user->SendSystemMessage(SystemMessageId::CANNOT_PROCEED_CONTRIBUTION_LIMIT_EXCEEDED);
            // Log.AddT(
            //     LOG_ERROR,
            //     L"[%s][%d] SSQSystem::DepositItemEx() : Too large count[%d]",
            //     L"SSQ.cpp",
            //     0x863,
            //     result)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] SSQSystem::DepositItemEx() : Too large count[%d]",
            //     L"SSQ.cpp",
            //     0x863,
            //     result)
            L2_LOG_WARN(
                Log,
                "Seven Signs deposit rejected for character %u: category %d total %lld is outside 0..999999999999 (previous "
                "%lld, added %lld).",
                static_cast<unsigned>(user->GetDBID()), index, static_cast<long long>(result),
                static_cast<long long>(depositedCount), static_cast<long long>(count)
            );
            return false;
        }
    }

    if (static_cast<std::uint64_t>(totalCount) - UINT64_C(1) >= static_cast<std::uint64_t>(kMaximumPoint)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_PROCEED_CONTRIBUTION_LIMIT_EXCEEDED);
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s][%d] SSQSystem::DepositItemEx() : Invalid count[%d]",
        //     L"SSQ.cpp",
        //     0x86c,
        //     totalCount)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] SSQSystem::DepositItemEx() : Invalid count[%d]",
        //     L"SSQ.cpp",
        //     0x86c,
        //     totalCount)
        L2_LOG_WARN(
            Log, "Seven Signs deposit rejected for character %u: combined item count %lld is outside 1..999999999999.",
            static_cast<unsigned>(user->GetDBID()), static_cast<long long>(totalCount)
        );
        return false;
    }

    const std::int64_t userPoint = user->GetSSQPoint();
    const std::int64_t userPointResult = runtime::AddInt64Wrapping(point, userPoint);
    if (static_cast<std::uint64_t>(userPointResult) - UINT64_C(1) >= static_cast<std::uint64_t>(kMaximumPoint)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_PROCEED_CONTRIBUTION_LIMIT_EXCEEDED);
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s][%d] SSQSystem::AddPoint() : Too large user point, result=[%d], delta=[%d]",
        //     L"SSQ.cpp",
        //     0x879,
        //     userPointResult,
        //     point)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] SSQSystem::AddPoint() : Too large user point, result=[%d], delta=[%d]",
        //     L"SSQ.cpp",
        //     0x879,
        //     userPointResult,
        //     point)
        L2_LOG_WARN(
            Log,
            "Seven Signs deposit rejected for character %u: player point result %lld is outside 1..999999999999 (previous "
            "%lld, added %lld).",
            static_cast<unsigned>(user->GetDBID()), static_cast<long long>(userPointResult),
            static_cast<long long>(userPoint), static_cast<long long>(point)
        );
        return false;
    }

    const std::int64_t systemPointResult = [&]() -> std::int64_t {
        L2_GUARD(mLock);
        const std::int64_t dawnPoint = GetCollectedPoint(SSQPart::DAWN);
        const std::int64_t twilightPoint = GetCollectedPoint(SSQPart::TWILIGHT);
        const std::int64_t systemPointResult =
            runtime::AddInt64Wrapping(runtime::AddInt64Wrapping(point, dawnPoint), twilightPoint);
        return systemPointResult;
    }();

    if (static_cast<std::uint64_t>(systemPointResult) - UINT64_C(1) >= static_cast<std::uint64_t>(kMaximumPoint)) {
        user->SendSystemMessage(SystemMessageId::CANNOT_PROCEED_CONTRIBUTION_LIMIT_EXCEEDED);
        // Log.AddT(
        //     LOG_ERROR,
        //     L"[%s][%d] SSQSystem::AddPoint() : Too large system point, result[%d], delta[%d]",
        //     L"SSQ.cpp",
        //     0x887,
        //     systemPointResult,
        //     point)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] SSQSystem::AddPoint() : Too large system point, result[%d], delta[%d]",
        //     L"SSQ.cpp",
        //     0x887,
        //     systemPointResult,
        //     point)
        L2_LOG_WARN(
            Log,
            "Seven Signs deposit rejected for character %u: system point result %lld is outside 1..999999999999 (added "
            "%lld).",
            static_cast<unsigned>(user->GetDBID()), static_cast<long long>(systemPointResult), static_cast<long long>(point)
        );
        return false;
    }

    const int userServerId = runtime::Int32FromBits(user->GetServerId());
    const int userDatabaseId = runtime::Int32FromBits(user->GetDBID());
    DxDepositSevenSignQuestItemEx packet(userDatabaseId, userServerId, part, countList, point);
    db.RequestTPacketProcess(user->m_data->m_nWorldId, 0x19, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool SSQSystem::ChangeCurrentStatus(const SSQStatus::type status) {
    // Current L2Server 0x00ad6590 does not update shared state directly.
    L2_GUARD_FUNCTION();
    const bool changed = mSharedInfo->mCurrentStatus != status;
    if (changed) {
        SSQStatusPacket packet{};
        packet.status = status;
        db.RequestTPacketProcess(g_nWorldId, 8, static_cast<int>(sizeof(packet)), packet);
    }
    return changed;
}

bool SSQSystem::ChangeCycleMode(const int mode, const int halfCycleInterval) {
    // Current L2Server 0x00ad6a34.
    L2_GUARD_FUNCTION();
    SSQChangeCycleModePacket packet{};
    packet.errorCode = 0;
    packet.cycleModeFlag = runtime::Int32FromBits(
        (static_cast<std::uint32_t>(mode) & 0x0fU) << 16U | (static_cast<std::uint32_t>(halfCycleInterval) & 0xffffU)
    );
    db.RequestTPacketProcess(g_nWorldId, 0x18, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool SSQSystem::SetNextEventPeriod() {
    // Current L2Server 0x00ad6e54.
    L2_GUARD_FUNCTION();
    if (mCycleMode == 0) {
        DateTime previous(TimeValue(mSharedInfo->mEventStartTime, 0));
        if (mSharedInfo->mEventStartTime == 0) {
            previous.Minute(0);
            previous.Weekday(1);
            previous.Hour(18);
        }

        TimeValue eventStart = CurrentTimeValue();
        DateTime next(eventStart);
        int dayDistance = (previous.Weekday() - next.Weekday() + 7) % 7;
        if (dayDistance == 0) {
            dayDistance = 7;
        }
        eventStart += TimeValue(dayDistance * 86400, 0);
        next.Update(eventStart);
        next.Second(0);
        next.Hour(previous.Hour());
        next.Minute(previous.Minute());
        eventStart = DateTimeToTimeValue(&next);

        const int eventEnd = runtime::AddInt32Wrapping(eventStart.Sec(), mHalfCycleInterval);
        ChangePeriod(SSQStatus::EVENT, eventStart.Sec(), eventEnd);
        return true;
    }
    if (mCycleMode == 1) {
        const TimeValue eventStart(mSharedInfo->mSealEffectEndTime, 0);
        const int eventEnd = runtime::AddInt32Wrapping(eventStart.Sec(), mHalfCycleInterval);
        ChangePeriod(SSQStatus::EVENT, eventStart.Sec(), eventEnd);
        return true;
    }
    return false;
}

bool SSQSystem::SetNextSealEffectPeriod() {
    // Current L2Server 0x00ad6c90.
    L2_GUARD_FUNCTION();
    if (mCycleMode == 0) {
        TimeValue remaining(runtime::MultiplyInt32Wrapping(mHalfCycleInterval, 2), 0);
        remaining -= TimeValue(mSharedInfo->mEventEndTime, 0);
        remaining += TimeValue(mSharedInfo->mEventStartTime, 0);

        TimeValue start(mSharedInfo->mEventEndTime, 0);
        start += TimeValue(mAccountInterval, 0);
        TimeValue end = remaining + start;
        end -= TimeValue(mAccountInterval, 0);
        end -= TimeValue(900, 0);
        ChangePeriod(3, start.Sec(), end.Sec());
        return true;
    }
    if (mCycleMode == 1) {
        TimeValue start(mSharedInfo->mEventEndTime, 0);
        start += TimeValue(mAccountInterval, 0);
        TimeValue end = start + TimeValue(mHalfCycleInterval, 0);
        end -= TimeValue(mAccountInterval, 0);
        ChangePeriod(3, start.Sec(), end.Sec());
        return true;
    }
    return false;
}

bool SSQSystem::SetRoundNumberToAllCastleMember(const int roundNumber) {
    // Current L2Server 0x00ad8cc0.
    L2_GUARD_FUNCTION();
    std::vector<int> pledgeIds;
    pledgeIds.reserve(10);
    g_ResidenceDB.GetCastlePledgeList(pledgeIds);

    CastlePledgeSnapshot* const snapshot = new CastlePledgeSnapshot(roundNumber);
    for (const int pledgeId : pledgeIds) {
        CPledge* const pledge = CPledge::GetObjectW(static_cast<std::uint32_t>(pledgeId));
        if (pledge != nullptr) {
            snapshot->AddPledgeInfo(pledge);
        }
    }
    snapshot->Start();
    return true;
}

bool SSQSystem::OnEventEnd() {
    // Current L2Server 0x00ad4220.
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_NORMAL, L"+ SSQ : Event finished.")
    // L2_LOG_INFO(Log, L"+ SSQ : Event finished.")
    L2_LOG_INFO(Log, "Ending the Seven Signs competition period; requesting NPC event shutdown.");
    DisableNpcPosEvent(u"ssq_event");
    g_npcserver.Send("cdd", 0x4a, 1, 0);
    return true;
}

bool SSQSystem::DoAccount() {
    // Current L2Server 0x00ad7088.
    L2_GUARD_FUNCTION();
    ValidateCollectPointPortion();
    ValidateMainEventPortion();

    SSQPart::type winner = SSQPart::NONE;
    SSQPart::type loser = SSQPart::NONE;
    if (mTotalPointPortion[SSQPart::TWILIGHT] <= mTotalPointPortion[SSQPart::DAWN]) {
        if (mTotalPointPortion[SSQPart::DAWN] <= mTotalPointPortion[SSQPart::TWILIGHT]) {
            for (int seal = SSQSeal::AVARICE; seal <= SSQSeal::STRIFE; ++seal) {
                ChangeSealOwner(static_cast<SSQSeal::type>(seal), SSQPart::NONE);
            }
            ChangeWinner(SSQPart::NONE);
            return true;
        }
        winner = SSQPart::DAWN;
        loser = SSQPart::TWILIGHT;
    } else {
        winner = SSQPart::TWILIGHT;
        loser = SSQPart::DAWN;
    }

    const int totalSelections = GetTotalSealSelectionCount(winner);
    for (int seal = SSQSeal::AVARICE; seal <= SSQSeal::STRIFE; ++seal) {
        const SSQSeal::type sealType = static_cast<SSQSeal::type>(seal);
        const double portion = totalSelections == 0 ? 0.0
                                                    : static_cast<double>(mSharedInfo->mSealSelectionCount[winner][seal]) *
                100.0 / static_cast<double>(totalSelections);
        const SSQPart::type previousOwner = mSharedInfo->mSealOwner[seal];

        bool change = false;
        double requiredPortion = 0.0;
        if (previousOwner == SSQPart::NONE || previousOwner == loser) {
            change = true;
            requiredPortion = 35.0;
        } else if (previousOwner == winner) {
            change = true;
            requiredPortion = 10.0;
        }
        if (!change) {
            continue;
        }
        if (portion < requiredPortion) {
            ChangeSealOwner(sealType, SSQPart::NONE);
        } else {
            ChangeSealOwner(sealType, winner);
            Log.Add(
                LOG_AUDIT, u"%I64d,,,,,,,,,,,%I64d,%I64d,,,,,,,,,,,,,", static_cast<std::int64_t>(0x594),
                static_cast<std::int64_t>(seal), static_cast<std::int64_t>(winner)
            );
        }
    }
    ChangeWinner(winner);
    TimeAttackBoard::inst().DistributeFee();
    return true;
}

bool SSQSystem::OnAccountStart() {
    // Current L2Server 0x00ad74ac.
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_NORMAL, L"+ SSQ : Account started.")
    // L2_LOG_INFO(Log, L"+ SSQ : Account started.")
    L2_LOG_INFO(Log, "Starting Seven Signs score settlement.");
    DoAccount();
    g_npcserver.Send("cdd", 0x4a, 2, 1);
    return true;
}

bool SSQSystem::OnClose() {
    // Current L2Server 0x00ad758c.
    L2_GUARD_FUNCTION();
    ChangeDropGuardStatus(false);
    return true;
}

void SSQSystem::DispelSealEffect() {
    // Current L2Server 0x00ad7934.
    L2_GUARD_FUNCTION();
    const SSQPart::type owner = GetSealOwner(SSQSeal::STRIFE);
    if (owner != SSQPart::TWILIGHT && owner != SSQPart::DAWN) {
        return;
    }
    FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
        user->DeleteAcquireSkill(0x13d2);
        user->DeleteAcquireSkill(0x13d3);
        user->ValidateChanged(VB_ITEMSKILL | VB_SKILLLIST | VB_SKILLMOD | VB_HPMPCP);
        {
            L2_GUARD(*user->m_lock);
            user->m_data->ValidateCP(0.0);
        }
        user->SendUserHPInfo(4, 0, false);
    });
}

void SSQSystem::CastSpellSealEffect() {
    // Current L2Server 0x00ad7be8.
    L2_GUARD_FUNCTION();
    const SSQPart::type owner = GetSealOwner(SSQSeal::STRIFE);
    if (owner == SSQPart::DAWN) {
        FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
            const SSQPart::type part = user->GetSSQPart();
            if (part == SSQPart::TWILIGHT) {
                PushSSQSkillToUser(user, 0x13d3, 1);
            } else if (part == SSQPart::DAWN) {
                PushSSQSkillToUser(user, 0x13d2, 1);
            }
        });
    } else if (owner == SSQPart::TWILIGHT) {
        FOR_ALL_OBJECT_IN_OBJECT_MAP(g_UserMap, user, {
            const SSQPart::type part = user->GetSSQPart();
            if (part == SSQPart::TWILIGHT) {
                PushSSQSkillToUser(user, 0x13d2, 1);
            } else if (part == SSQPart::DAWN) {
                PushSSQSkillToUser(user, 0x13d3, 1);
            }
        });
    }
}

bool SSQSystem::OnSealEffectEnd() {
    // Current L2Server 0x00ad7ff4.
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_NORMAL, L"+ SSQ : Seal effect finished.")
    // L2_LOG_INFO(Log, L"+ SSQ : Seal effect finished.")
    L2_LOG_INFO(Log, "Ending the Seven Signs seal-effect period; removing events and effects.");
    for (auto& entry : NpcPosEventNameMap) {
        DisableAllNpcPosEvent(entry);
    }
    g_npcserver.Send("cdd", 0x4a, 3, 0);
    CUserBroadcaster::inst().BroadcastToAllUserEx("ccc", 0x73, 0, 0);
    DispelSealEffect();
    return true;
}

bool SSQSystem::OnSealEffectStart() {
    // Current L2Server 0x00ad944c.
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_NORMAL, L"+ SSQ : Seal effect started.")
    // L2_LOG_INFO(Log, L"+ SSQ : Seal effect started.")
    L2_LOG_INFO(Log, "Starting the Seven Signs seal-effect period; applying events and effects.");
    for (int seal = SSQSeal::AVARICE; seal <= SSQSeal::STRIFE; ++seal) {
        EnableNpcPosEvent(NpcPosEventNameMap[{seal, static_cast<int>(mSharedInfo->mSealOwner[seal])}].c_str());
    }
    g_npcserver.Send("cdd", 0x4a, 3, 1);
    ChangeDropGuardStatus(true);
    CUserBroadcaster::inst().BroadcastToAllUserEx("ccc", 0x73, mSharedInfo->mWinner, 0);
    CastSpellSealEffect();

    const std::int32_t eventStartTime = mSharedInfo->mEventStartTime;
    if (eventStartTime == 0 || CurrentTimeValue() > TimeValue(eventStartTime, 0)) {
        SetNextEventPeriod();
    }
    g_ResidenceDB.VerifyMaxRateOfCastle();

    for (int roomNumber = 1; roomNumber < 6; ++roomNumber) {
        for (int recordType = TimeAttackRecordType::TOP_OF_TWILIGHT; recordType <= TimeAttackRecordType::TOP_OF_DAWN;
            ++recordType) {
            TimeAttackInfo* const info = TimeAttackBoard::inst().GetInfo(roomNumber, recordType);
            if (info == nullptr || info->mSharedInfo == nullptr || info->mSharedInfo->mMemberCount <= 0) {
                continue;
            }
            for (int index = 0; index < info->mSharedInfo->mMemberCount; ++index) {
                CSPointer<User> user =
                    g_UserDBIDMap.FindObject(std::CDualKey<int, int>(g_nWorldId, info->mSharedInfo->mMemberDBIDList[index]));
                if (user) {
                    user->GetPledge();
                }
            }
        }
    }
    return true;
}

bool SSQSystem::OnEventStart() {
    // Current L2Server 0x00ad8fdc.
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_NORMAL, L"+ SSQ : Event started.")
    // L2_LOG_INFO(Log, L"+ SSQ : Event started.")
    L2_LOG_INFO(Log, "Starting the Seven Signs competition period.");

    const std::int32_t sealEffectTime = mSharedInfo->mSealEffectTime;
    if (sealEffectTime == 0 || mSharedInfo->mEventStartTime > sealEffectTime ||
        CurrentTimeValue() > TimeValue(sealEffectTime, 0)) {
        SetNextSealEffectPeriod();
    }
    ChangeDropGuardStatus(false);
    SetRoundNumberToAllCastleMember(mSharedInfo->mRoundNumber);
    EnableNpcPosEvent(u"ssq_event");
    g_npcserver.Send("cdd", 0x4a, 1, 1);
    return true;
}

bool SSQSystem::OnCastleSiegeStart(CCastle*) {
    // Current L2Server 0x00ad7634 discards the change result.
    L2_GUARD_FUNCTION();
    SSQSystem::inst().ChangeDropGuardStatus(false);
    return false;
}

bool SSQSystem::OnCastleSiegeEnd(CCastle*) {
    // Current L2Server 0x00ad76d4. Equality with the scheduled second does
    // not start the event. A positive sub-second lane is strictly later.
    L2_GUARD_FUNCTION();
    const std::int32_t eventStart = mSharedInfo->mEventStartTime;
    if (eventStart == 0 || CurrentTimeValue() > TimeValue(eventStart, 0)) {
        SetNextEventPeriod();
    }
    return true;
}

int SSQSystem::GetTotalSealSelectionCount(const SSQPart::type part) {
    // Current L2Server 0x00ad43e8, original owner SSQ.cpp.
    L2_GUARD_FUNCTION();
    int total = 0;
    for (int seal = SSQSeal::AVARICE; seal <= SSQSeal::STRIFE; ++seal) {
        total = runtime::AddInt32Wrapping(total, mSharedInfo->mSealSelectionCount[part][seal]);
    }
    return total;
}

bool SSQSystem::SendSealInfoTo(User* const user) {
    // Current L2Server 0x00ad44a8, original owner SSQ.cpp.
    L2_GUARD_FUNCTION();
    std::array<char, 0x5000> buffer{};
    int length = ::Assemble(buffer.data(), static_cast<int>(buffer.size()), "ccc", 10, 0x23, 3);
    if (length < 1) {
        return false;
    }

    for (int seal = SSQSeal::AVARICE; seal <= SSQSeal::STRIFE; ++seal) {
        int twilightPercent = 0;
        const int twilightTotal = GetTotalSealSelectionCount(SSQPart::TWILIGHT);
        if (twilightTotal > 0) {
            twilightPercent =
                runtime::MultiplyInt32Wrapping(mSharedInfo->mSealSelectionCount[SSQPart::TWILIGHT][seal], 100) /
                twilightTotal;
        }

        int dawnPercent = 0;
        const int dawnTotal = GetTotalSealSelectionCount(SSQPart::DAWN);
        if (dawnTotal > 0) {
            dawnPercent =
                runtime::MultiplyInt32Wrapping(mSharedInfo->mSealSelectionCount[SSQPart::DAWN][seal], 100) / dawnTotal;
        }

        const int assembled = ::Assemble(
            buffer.data() + length, static_cast<int>(buffer.size()) - length, "cccc", seal,
            static_cast<int>(mSharedInfo->mSealOwner[seal]), twilightPercent, dawnPercent
        );
        if (assembled < 1) {
            return false;
        }
        length = runtime::AddInt32Wrapping(length, assembled);
    }

    user->GetSocket()->Send(
        "cccb", 0xfb, 3, static_cast<int>(static_cast<std::int8_t>(mSharedInfo->mCurrentStatus)), length, buffer.data()
    );
    return true;
}

bool SSQSystem::SendStatusInfoTo(User* const user) {
    // Current L2Server 0x00ad53c8, original owner SSQ.cpp.
    L2_GUARD_FUNCTION();
    if (user == nullptr) {
        return false;
    }

    const SSQStatus::type status = mSharedInfo->mCurrentStatus;
    const int round = mSharedInfo->mRoundNumber;

    int statusMessage = 0;
    int timeMessage = 0;
    switch (status) {
        case SSQStatus::NONE:
            statusMessage = 0x49f;
            break;
        case SSQStatus::EVENT:
            statusMessage = 0x498;
            break;
        case SSQStatus::ACCOUNTING:
            statusMessage = 0x4a0;
            break;
        case SSQStatus::SEAL_EFFECT:
            statusMessage = 0x499;
            break;
        default:
            break;
    }

    if (status == SSQStatus::EVENT || status == SSQStatus::SEAL_EFFECT) {
        const DateTime now;
        timeMessage = now.Weekday() == 1 && now.Hour() < 18 ? 0x507 : 0x506;
    }

    const SSQPart::type part = user->GetSSQPart();
    const SSQSeal::type seal = user->GetSealSelectionNo();
    const int partValue = static_cast<int>(part);

    const std::int64_t item2 = user->GetDepositedSSQItemCount(partValue, 2);
    const std::int64_t item1 = user->GetDepositedSSQItemCount(partValue, 1);
    const std::int64_t item0 = user->GetDepositedSSQItemCount(partValue, 0);

    std::int64_t collectDawn = 0;
    std::int64_t mainDawn = 0;
    std::int64_t totalDawn = 0;
    std::int64_t collectTwilight = 0;
    std::int64_t mainTwilight = 0;
    std::int64_t totalTwilight = 0;
    int dawnPercent = 0;
    int twilightPercent = 0;
    if (status != SSQStatus::NONE) {
        collectDawn = static_cast<int>(mCollectPointPortion[1]);
        mainDawn = static_cast<int>(mMainEventPointPortion[1]);
        totalDawn = static_cast<int>(mTotalPointPortion[1]);
        collectTwilight = static_cast<int>(mCollectPointPortion[2]);
        mainTwilight = static_cast<int>(mMainEventPointPortion[2]);
        totalTwilight = static_cast<int>(mTotalPointPortion[2]);

        const double total = mTotalPointPortion[1] + mTotalPointPortion[2];
        if (mTotalPointPortion[1] != 0.0) {
            dawnPercent = static_cast<int>(mTotalPointPortion[1] * 100.0 / total);
        }
        if (mTotalPointPortion[2] != 0.0) {
            twilightPercent = static_cast<int>(mTotalPointPortion[2] * 100.0 / total);
        }
    }

    user->GetSocket()->Send(
        "cccdddccQQQQQcQQQc", 0xfb, 1, static_cast<int>(status), round, statusMessage, timeMessage, partValue,
        static_cast<int>(seal), item0 + item1 + item2 * 10, (item1 + item2 * 2) * 5 + item0 * 3, collectDawn, mainDawn,
        totalDawn, dawnPercent, collectTwilight, mainTwilight, totalTwilight, twilightPercent
    );
    return true;
}

bool SSQSystem::SendPreResultTo(User* const user) {
    // Current L2Server 0x00ad5740, original owner SSQ.cpp.
    L2_GUARD_FUNCTION();
    if (user == nullptr || mSharedInfo->mCurrentStatus != SSQStatus::EVENT) {
        return false;
    }

    SSQPart::type winner = SSQPart::NONE;
    if (mTotalPointPortion[SSQPart::TWILIGHT] <= mTotalPointPortion[SSQPart::DAWN]) {
        if (mTotalPointPortion[SSQPart::DAWN] <= mTotalPointPortion[SSQPart::TWILIGHT]) {
            winner = SSQPart::NONE;
        } else {
            winner = SSQPart::DAWN;
        }
    } else {
        winner = SSQPart::TWILIGHT;
    }

    std::array<char, 0x5000> buffer{};
    int length = ::Assemble(buffer.data(), static_cast<int>(buffer.size()), "cc", static_cast<int>(winner), 3);
    if (length < 1) {
        return false;
    }

    if (winner == SSQPart::NONE) {
        for (int seal = SSQSeal::AVARICE; seal <= SSQSeal::STRIFE; ++seal) {
            const int assembled = ::Assemble(
                buffer.data() + length, static_cast<int>(buffer.size()) - length, "ccd", seal,
                static_cast<int>(SSQPart::NONE), 0x50e
            );
            if (assembled < 1) {
                return false;
            }
            length = runtime::AddInt32Wrapping(length, assembled);
        }
        user->GetSocket()->Send(
            "cccb", 0xfb, 4, static_cast<int>(static_cast<std::int8_t>(mSharedInfo->mCurrentStatus)), length, buffer.data()
        );
        return true;
    }

    const SSQPart::type loser = winner == SSQPart::TWILIGHT ? SSQPart::DAWN : SSQPart::TWILIGHT;
    const int totalSelection = GetTotalSealSelectionCount(winner);
    for (int seal = SSQSeal::AVARICE; seal <= SSQSeal::STRIFE; ++seal) {
        double selectionPercent = 0.0;
        if (totalSelection != 0) {
            selectionPercent = static_cast<double>(mSharedInfo->mSealSelectionCount[winner][seal]) * 100.0 /
                static_cast<double>(totalSelection);
        }

        SSQPart::type resultOwner = SSQPart::NONE;
        int messageId = 0;
        const SSQPart::type owner = mSharedInfo->mSealOwner[seal];
        if (owner == SSQPart::NONE || owner == loser) {
            if (selectionPercent < 35.0) {
                messageId = 0x50c;
            } else {
                resultOwner = winner;
                messageId = 0x50a;
            }
        } else if (owner == winner) {
            if (selectionPercent < 10.0) {
                messageId = 0x50b;
            } else {
                resultOwner = winner;
                messageId = 0x509;
            }
        }

        const int assembled = ::Assemble(
            buffer.data() + length, static_cast<int>(buffer.size()) - length, "ccd", seal, static_cast<int>(resultOwner),
            messageId
        );
        if (assembled < 1) {
            return false;
        }
        length = runtime::AddInt32Wrapping(length, assembled);
    }

    user->GetSocket()->Send(
        "cccb", 0xfb, 4, static_cast<int>(static_cast<std::int8_t>(mSharedInfo->mCurrentStatus)), length, buffer.data()
    );
    return true;
}

bool SSQSystem::StartEvent() {
    L2_GUARD_FUNCTION();
    const int round = mSharedInfo->mRoundNumber + 1;
    db.StartSSQEvent(round);
    db.InitTimeAttackRecord(round);
    return true;
}

bool SSQSystem::StartAccount() {
    L2_GUARD_FUNCTION();
    SSQStatusPacket packet{};
    packet.status = 2;
    db.RequestTPacketProcess(g_nWorldId, 6, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool SSQSystem::StartSealEffect() {
    L2_GUARD_FUNCTION();
    SSQStatusPacket packet{};
    packet.status = 3;
    db.RequestTPacketProcess(g_nWorldId, 7, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool SSQSystem::Close() {
    L2_GUARD_FUNCTION();
    SSQStatusPacket packet{};
    packet.status = 0;
    db.RequestTPacketProcess(g_nWorldId, 0x0b, static_cast<int>(sizeof(packet)), packet);
    return true;
}

bool SSQSystem::Reset() {
    // Current L2Server 0x00ad7834. The current body always returns false
    // after disabling the nine seal-position events and the main SSQ event.
    L2_GUARD_FUNCTION();
    if (mCycleMode == 0) {
        SetNextEventPeriod();
    } else {
        ChangePeriod(SSQStatus::EVENT, 0, 0);
    }
    ChangePeriod(SSQStatus::SEAL_EFFECT, 0, 0);
    ChangeCurrentStatus(SSQStatus::NONE);
    for (auto& entry : NpcPosEventNameMap) {
        DisableAllNpcPosEvent(entry);
    }
    DisableNpcPosEvent(u"ssq_event");
    return false;
}

void SSQSystem::TimerExpired(int) {
    L2_GUARD_FUNCTION();

    const SSQStatus::type status = mSharedInfo->mCurrentStatus;
    const std::int32_t now = CurrentUnixSeconds();

    if (mTimerEnabled) {
        if (status == SSQStatus::NONE) {
            const std::int32_t transition = mSharedInfo->mEventStartTime;
            if (transition > 0 && transition < now) {
                mTimerEnabled = false;
                if (mSharedInfo->mRoundNumber == 0) {
                    DisableNpcPosEvent(u"ssq_seal1_none");
                    DisableNpcPosEvent(u"ssq_seal2_none");
                }
                StartEvent();
            }
        } else if (status == SSQStatus::EVENT) {
            const std::int32_t transition = mSharedInfo->mEventEndTime;
            if (transition > 0 && transition < now) {
                mTimerEnabled = false;
                StartAccount();
            }
        } else if (status == SSQStatus::ACCOUNTING) {
            const std::int32_t transition = mSharedInfo->mSealEffectTime;
            if (transition > 0 && transition < now) {
                mTimerEnabled = false;
                StartSealEffect();
            }
        } else if (status == SSQStatus::SEAL_EFFECT) {
            const std::int32_t transition = mSharedInfo->mSealEffectEndTime;
            if (transition > 0 && transition < now) {
                mTimerEnabled = false;
                Close();
            }
        }
    }
    AddTimer(5000, 0);
}
