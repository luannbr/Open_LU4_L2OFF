#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/RWLock.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

class DxAddTimeAttackFee;
class DxAddTimeAttackRecord;
class DxSetTimeAttackRewardFlag;
class TimeAttackDB;


// Shipping owner: CacheD/TimeAttackDB.{h,cpp}. This is not the
// SharedTimeAttackInfo transport object: current Cached.exe keeps time_t as a
// signed 64-bit value at +0x18 and therefore moves elapsed/member-count to
// +0x20/+0x24 before serializing the compact Shared wire representation.
class TimeAttackRecord {
public:
    TimeAttackRecord(int roomNo, int recordType, int round);
    ~TimeAttackRecord();

    TimeAttackRecord(const TimeAttackRecord&) = delete;
    TimeAttackRecord& operator=(const TimeAttackRecord&) = delete;

    void ResetMemberInfo();
    int Assemble(char* outputBuffer, int bufferSize);
    int IsMember(int userDatabaseId) const;
    int SetRewardFlag(int userDatabaseId, int flag);
    bool SetMemberFromDBField(const char16_t* names);
    bool SetDBIDFromDBField(const char16_t* list);
    bool LogForAudit() const;
    bool Create();
    bool Save();

    // The target's ReadGuard/WriteGuard calls inline these four forwards and
    // pass this+0x240 to RWLock. Keeping the forwards on the record keeps
    // that source-level locking surface for Shared/DXPacket.cpp.
    void ReadLock(const wchar_t* callerFile, int callerLine) { m_lock.ReadLock(callerFile, callerLine); }
    void ReadUnlock() { m_lock.ReadUnlock(); }
    void WriteLock(const wchar_t* callerFile, int callerLine) { m_lock.WriteLock(callerFile, callerLine); }
    void WriteUnlock() { m_lock.WriteUnlock(); }

private:
    friend class DxAddTimeAttackFee;
    friend class DxAddTimeAttackRecord;
    friend class DxSetTimeAttackRewardFlag;
    friend class TimeAttackDB;

    int m_roomNo; // +0x000
    int m_recordType; // +0x004
    int m_ssqRound; // +0x008
    int m_ssqPart; // +0x00c
    std::int64_t m_point; // +0x010
    std::int64_t m_recordTime; // +0x018
    int m_elapsedTime; // +0x020
    int m_memberCount; // +0x024
    std::int64_t m_fee; // +0x028
    char16_t m_memberNameList[9][25]; // +0x030
    int m_memberDatabaseIdList[9]; // +0x1f4
    int m_memberRewardFlags[9]; // +0x218
    RWLock m_lock; // +0x240
    std::array<std::byte, 0x08> m_targetTailPadding; // +0x278
};



using TimeAttackRecordKey = std::pair<int, int>;
using TimeAttackRecordMap = std::map<TimeAttackRecordKey, TimeAttackRecord*>;

// The lazy target singleton at 0x02a2fae0 contains only this map. Current
// tree nodes store room/type at +0x18/+0x1c and the record pointer at +0x20.
// the pair key is corroborated by the matching 2015 server PDB.
class TimeAttackDB {
public:
    TimeAttackDB();
    ~TimeAttackDB();

    TimeAttackDB(const TimeAttackDB&) = delete;
    TimeAttackDB& operator=(const TimeAttackDB&) = delete;

    // Current accessor 0x0059cc44 is an unsymbolized lazy static.
    static TimeAttackDB& Instance();

    TimeAttackRecord* GetRecord(int roomNo, int recordType) const;
    int AssembleAll(char* outputBuffer, int bufferSize);
    int Create(int round);
    bool Load(int roundNumber);
    bool InitializeTo(int round);

private:

    TimeAttackRecordMap m_records; // +0x00
};
