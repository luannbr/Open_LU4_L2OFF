#pragma once

#include "Portable/ProcessRuntime.h"
#include "Shared/src/IOC.h"
#include "Shared/src/SSQ.h"
#include "Shared/src/SharedType.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/Systems.h"
#include "Shared/src/YieldLock.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct SharedSSQInfo;
class User;
class CCreature;
class CCastle;
class CPledge;


class CastlePledgeSnapshot final : public CIOObject {
public:

    explicit CastlePledgeSnapshot(int roundNumber);
    ~CastlePledgeSnapshot() override;

    void AddPledgeInfo(CPledge* pledge);
    void Start();
    void TimerExpired(int id) override;

private:
    int mRoundNumber; // +0x18
    [[maybe_unused]] std::array<std::byte, 0x04> mPadding01c;
    std::set<int> mPledgeDBIDSet; // +0x20
    std::set<int> mUserIDSet; // +0x40
    std::vector<int> mPledgeDBIDList; // +0x60
    std::vector<int> mUserIDList; // +0x80
    int mPledgeIndex; // +0xa0
    int mUserIndex; // +0xa4
};


class SSQSystem final : public CIOObject {
public:

    SSQSystem();
    ~SSQSystem() override;

    static SSQSystem& inst();

    int GetRoundNumber() const { return mSharedInfo->mRoundNumber; }

    SSQStatus::type GetCurrentStatus() const { return mSharedInfo->mCurrentStatus; }

    SSQPart::type GetWinner() const { return mSharedInfo->mWinner; }

    bool SetCurrentStatus(const SSQStatus::type status) {
        // Current L2Server 0x006f26c4.
        L2_GUARD_FUNCTION();
        mSharedInfo->mCurrentStatus = status;
        return true;
    }

    std::int32_t GetEventStartTime() const { return mSharedInfo->mEventStartTime; }

    bool SetEventStartTime(const std::int32_t time) {
        // Current L2Server 0x006f2768.
        L2_GUARD_FUNCTION();
        mSharedInfo->mEventStartTime = time;
        return true;
    }

    std::int32_t GetEventEndTime() const { return mSharedInfo->mEventEndTime; }

    bool SetEventEndTime(const std::int32_t time) {
        // Current L2Server 0x006f280c.
        L2_GUARD_FUNCTION();
        mSharedInfo->mEventEndTime = time;
        return true;
    }

    std::int32_t GetSealEffectTime() const { return mSharedInfo->mSealEffectTime; }

    bool SetSealEffectTime(const std::int32_t time) {
        // Current L2Server 0x006f28b0.
        L2_GUARD_FUNCTION();
        mSharedInfo->mSealEffectTime = time;
        return true;
    }

    std::int32_t GetSealEffectEndTime() const { return mSharedInfo->mSealEffectEndTime; }

    bool SetSealEffectEndTime(const std::int32_t time) {
        // Current L2Server 0x006f2954.
        L2_GUARD_FUNCTION();
        mSharedInfo->mSealEffectEndTime = time;
        return true;
    }

    bool SetWinner(const SSQPart::type part) {
        // Current L2Server 0x006f29f8.
        L2_GUARD_FUNCTION();
        mSharedInfo->mWinner = part;
        return true;
    }

    bool SetSealOwner(const int seal, const SSQPart::type part) {
        // Current L2Server 0x006f2a9c.
        L2_GUARD_FUNCTION();
        mSharedInfo->mSealOwner[seal] = part;
        return true;
    }

    bool SetMainEventPoint(const SSQPart::type part, const std::int64_t point) {
        // Current L2Server 0x006f2b40.
        L2_GUARD_FUNCTION();
        mSharedInfo->mMainEventPoint[part] = point;
        return true;
    }

    bool SetMemberCount(const SSQPart::type part, const int count) {
        // Current L2Server 0x006f2be4.
        L2_GUARD_FUNCTION();
        mSharedInfo->mMemberCount[part] = count;
        return true;
    }

    bool SetSealSelectionCount(const SSQPart::type part, const SSQSeal::type seal, const int count) {
        // Current L2Server 0x006f2c8c.
        L2_GUARD_FUNCTION();
        mSharedInfo->mSealSelectionCount[part][seal] = count;
        return true;
    }

    int GetCycleMode() const { return mCycleMode; }

    int GetHalfCycleInterval() const { return mHalfCycleInterval; }

    void TimerExpired(int id) override;

    const unsigned char* SetAll(const unsigned char* packet);
    void Notify();
    void ValidateTotalPortion();
    void ValidateCollectPointPortion();
    void ValidateMainEventPortion();
    void SetCycleModeFlag(int cycleModeFlag);
    std::int64_t GetCollectedPoint(SSQPart::type part) const;
    std::int64_t GetMainEventPoint(SSQPart::type part) const;
    int GetMemberCount(SSQPart::type part) const;
    int GetSealSelectionCount(SSQPart::type part, SSQSeal::type seal) const;
    bool SetCollectedPoint(SSQPart::type part, std::int64_t point);

    void ReadLock(const wchar_t* callerFile, int callerLine) { mLock.Enter(callerFile, callerLine); }

    void ReadUnlock() { mLock.Leave(nullptr, 0); }

    void WriteLock(const wchar_t* callerFile, int callerLine) { mLock.Enter(callerFile, callerLine); }

    void WriteUnlock() { mLock.Leave(nullptr, 0); }

    SSQPart::type GetSealOwner(SSQSeal::type seal) const;
    int GetMaxTaxRateOfCastle() const;
    bool SetDropGuardStatus(bool status);
    bool ChangeDropGuardStatus(bool status);
    bool ChangePeriod(int status, int startTime, int endTime);
    bool ChangeSealSelectionCount(SSQPart::type part, SSQSeal::type seal, int count);
    bool ChangeUserPoint(User* user, int point);
    bool ChangeSealOwner(SSQSeal::type seal, SSQPart::type part);
    bool ChangeWinner(SSQPart::type part);
    bool AddMember(
        User* user,
        SSQPart::type part,
        SSQSeal::type seal,
        SSQPosition::type position,
        std::int64_t initialPoint,
        int joinType
    );
    bool DismissMember(User* user);
    bool StartQuickCycle(int seconds);
    bool DeleteDepositedItem(User* user, SSQPart::type part, SSQItem::type itemType, std::int64_t count);
    bool DepositItemEx(User* user, SSQPart::type part, std::int64_t countList[], std::int64_t point);
    bool ChangeCurrentStatus(SSQStatus::type status);
    bool ChangeCycleMode(int mode, int halfCycleInterval);
    bool SetNextEventPeriod();
    bool SetNextSealEffectPeriod();
    bool SetRoundNumberToAllCastleMember(int roundNumber);
    bool OnEventEnd();
    bool DoAccount();
    bool OnAccountStart();
    bool OnClose();
    bool OnSealEffectEnd();
    bool OnSealEffectStart();
    bool OnEventStart();
    bool OnCastleSiegeStart(CCastle* castle);
    bool OnCastleSiegeEnd(CCastle* castle);

    void EnableTimer() { mTimerEnabled = true; }

    int GetTotalSealSelectionCount(SSQPart::type part);
    bool SendSealInfoTo(User* user);
    bool SendStatusInfoTo(User* user);
    bool SendPreResultTo(User* user);

    bool StartEvent();
    bool StartAccount();
    bool StartSealEffect();
    bool Close();
    bool Reset();

private:
    friend class CCreature;
    friend class User;

    void CastSpellSealEffect();
    void DispelSealEffect();

    CYieldLock mLock; // +0x18
    AutoEvent mCacheDWaiter; // +0x28
    bool mTimerEnabled; // +0x38
    std::array<std::byte, 0x07> mPadding039;
    SharedSSQInfo* mSharedInfo; // +0x40
    std::array<double, 3> mCollectPointPortion; // +0x48
    std::array<double, 3> mMainEventPointPortion; // +0x60
    std::array<double, 3> mTotalPointPortion; // +0x78
    int mCycleMode; // +0x90
    int mHalfCycleInterval; // +0x94
    int mAccountInterval; // +0x98
    std::uint32_t mPadding09c;
};
