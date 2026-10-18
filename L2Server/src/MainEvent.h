#pragma once

#include "Shared/src/SSQ.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <utility>


struct SharedTimeAttackInfo;
class ItemManip;
class User;


// MainEvent.h. The current L2Server creates this source-level wrapper
// with a real 0x10 CYieldLock, unlike the CIODummyLock specialization visible
// in the older TotalChecker donor.
class TimeAttackInfo {
public:
    TimeAttackInfo(int roomNumber, int recordType);

    const unsigned char* Set(const unsigned char* packet);

    void ReadLock(const wchar_t* callerFile, int callerLine) { mLock.Enter(callerFile, callerLine); }

    void ReadUnlock() { mLock.Leave(nullptr, 0); }

    void WriteLock(const wchar_t* callerFile, int callerLine) { mLock.Enter(callerFile, callerLine); }

    void WriteUnlock() { mLock.Leave(nullptr, 0); }

    ::SharedTimeAttackInfo* mSharedInfo;

private:
    ::CYieldLock mLock;
};


class TimeAttackBoard {
public:
    using RecordKey = std::pair<int, int>;
    using RecordMap = std::map<RecordKey, TimeAttackInfo*>;

    static TimeAttackBoard& inst();

    TimeAttackBoard();
    virtual ~TimeAttackBoard();

    TimeAttackInfo* GetInfo(int roomNumber, int recordType) const;
    void OnStart();
    void Notify();
    int GetWinnerRoomCount(SSQPart::type partType);
    std::int64_t GetFee(int roomNumber);
    bool AddFee(int roomNumber, std::int64_t fee);
    bool GiveReward(User* user, int roomNumber, ItemManip& request);
    std::int64_t GetTotalPoint(SSQPart::type partType);
    int MakeTimeAttackInfo(char* output, int outputSize);
    void DistributeFee();
    bool AddRecord(
        int roomNumber, SSQPart::type partType, int partyServerId, std::int64_t point, int recordTime, int elapsedTime
    );

private:
    RecordMap mRecordMap;
};
