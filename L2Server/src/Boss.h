#pragma once

#include "NPC.h"
#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <functional>

class User;

// Current L2Server CBoss physical envelope. The target allocates 0x3d80
// bytes (0x004bf5ac). Parser_npc constructs CNPC first and initializes the
// two tail values at +0x3d78/+0x3d7c. Keep the full type available to
// consumers without emitting the still-separate Boss.cpp vtable owner.
class CBoss : public CNPC {
public:

    CBoss();
    CBoss(const CBoss& other);
    ~CBoss() override;

    static void* operator new(std::size_t requestedSize);

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void ReportOrt(const char* callerFile, int callerLine) override;
    CObject* Copy() override;
    bool IsBoss() override;
    CBoss* CastBoss() override;

    int GetPledgeId() const noexcept { return m_nPledgeID; }
    int GetNeighborPledgeMember();

    std::int32_t m_nPledgeID; // +0x3d78
    bool m_bIsFieldBoss; // +0x3d7c
    std::array<std::byte, 3> m_alignment3d7dTo3d80;
};


class CBossRecord : public MemoryObject {
public:
    CBossRecord(CBossRecord&&) noexcept = default;
    CBossRecord(const CBossRecord&) = default;
    CBossRecord(int bossId, int accumulatedPoint, int point);
    explicit CBossRecord(int bossId);
    CBossRecord();
    ~CBossRecord() override = default;

    CBossRecord& operator=(CBossRecord&&) noexcept = default;
    CBossRecord& operator=(const CBossRecord&) = default;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    void Init();
    bool AddPoint(User& user, int point, int accumulatedPoint);
    bool SubtractPoint(User& user, int point, int accumulatedPoint);
    bool _AddAccumulatedPoint(int point);
    bool SubtractAccumulatedPoint(int point);
    void SetBossID(int bossId);

    int m_nBossID; // +0x18
    int m_nAccumulatedPoint; // +0x1c
    int m_nPoint; // +0x20

private:
    L2_DECLARE_OBJECT_REGISTRATION(CBossRecord)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CBossRecord)

    ::CYieldLock m_lock; // +0x24
};


class CBossRecordList : public MemoryObject {
public:
    using BossRecordMap = ::std::map<int, CBossRecord*>;

    CBossRecordList(const CBossRecordList&) = default;
    CBossRecordList();
    ~CBossRecordList() override;

    CBossRecordList& operator=(const CBossRecordList&) = default;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()

    void DeleteAllBossRecord();
    void Enter(const wchar_t* callerFile, int callerLine);
    void Leave(const wchar_t* callerFile, int callerLine);
    int GetCertainBossPoint(int bossId);
    int GetTotalBossPoint();
    int GetTotalAccumulatedBossPoint();
    void AddBossRecord(CBossRecord* record);
    bool AddBossPoint(User& user, int bossId, int point);
    bool SubtractBossPoint(User& user, int bossId, int point);
    int AssembleBossRecord(char* buffer, int bufferSize, int* recordCount);
    void SetRound(int round);
    int GetRound();
    void Initialize();

    BossRecordMap m_mapBossRecord; // +0x18

private:
    L2_DECLARE_OBJECT_REGISTRATION(CBossRecordList)
    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CBossRecordList)

    int m_nRound; // +0x38
    ::CYieldLock m_lock; // +0x3c
};


class CBossRecordRanking : public MemoryObject {
public:
    using BossRecordMemberMap = ::std::map<int, int>;
    using BossRecordPointMap = ::std::multimap<int, int, std::greater<int>>;

    CBossRecordRanking();
    ~CBossRecordRanking() override;

    static CBossRecordRanking& inst();

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()

    void AddBossRecord(int characterId, int point);
    void Initialize();

private:
    L2_DECLARE_OBJECT_REGISTRATION(CBossRecordRanking)

    BossRecordMemberMap m_mapBossRecordMember; // +0x18
    BossRecordPointMap m_multimapBossRecordPoint; // +0x38
    ::CYieldLock m_lock; // +0x58
};


class CBossRecordRound : public CIOObject {
public:
    CBossRecordRound();
    ~CBossRecordRound() override = default;

    static CBossRecordRound& inst();

    L2_DECLARE_OBJECT_RELEASE_OUT_OF_LINE()
    void TimerExpired(int id) override;

    void Set(int round, int startTime, int endTime);
    void MoveToNextRound();

private:
    L2_DECLARE_OBJECT_REGISTRATION(CBossRecordRound)

public:
    std::int32_t m_nRound; // +0x18
    std::int32_t m_nStartTime; // +0x1c
    std::int32_t m_nEndTime; // +0x20
};
