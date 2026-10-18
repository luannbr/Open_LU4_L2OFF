#pragma once

#include "Shared/src/StandardContainers.h"
#include "Shared/src/YieldLock.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

class CCreature;
class User;

namespace ReuseDelaySaveReason {
enum Type : int;
}

// The target's unsigned long fields are fixed 32-bit values. Fixed-width
// types keep that image on hosts where unsigned long is 64 bits.
struct ReuseDelayData {
    ReuseDelayData(int subjobId, std::uint32_t remainDelay, std::uint32_t delay);

    int m_nSubjobId;
    std::uint32_t m_nRemainDelay;
    std::uint32_t m_nDelay;
    bool m_bDirtyFlag;
};


struct AcquiredSkillInfo {
    AcquiredSkillInfo(int id, int level, int subLevel, int reuseDelayGroup, bool saveToDB) :
        m_nId(id), m_nLevel(level), m_nSubLevel(subLevel), m_ReuseDelayGroup(reuseDelayGroup), m_bSaveToDB(saveToDB) {}

    int m_nId;
    int m_nLevel;
    int m_nSubLevel;
    int m_ReuseDelayGroup;
    bool m_bSaveToDB;
};

static_assert(std::is_trivially_copyable_v<AcquiredSkillInfo>);
static_assert(std::is_trivially_destructible_v<AcquiredSkillInfo>);

struct CResetSkillReuseDelayFilter {
    virtual ~CResetSkillReuseDelayFilter();

    virtual bool CanDo(int skillId, ReuseDelayData& data) = 0;
};

class CNullResetSkillReuseDelayFilter final : public CResetSkillReuseDelayFilter {
public:
    ~CNullResetSkillReuseDelayFilter() override = default;

    bool CanDo(int skillId, ReuseDelayData& data) override;
};

class CAcquireSkillResetSkillReuseDelayFilter : public CResetSkillReuseDelayFilter {
public:
    explicit CAcquireSkillResetSkillReuseDelayFilter(CCreature* creature);
    ~CAcquireSkillResetSkillReuseDelayFilter() override = default;

    bool CanDo(int skillId, ReuseDelayData& data) override = 0;

protected:
    ::std::map<int, AcquiredSkillInfo> m_AcquireSkills;
};


class CSkillReuseDelay {
public:
    CSkillReuseDelay();
    virtual ~CSkillReuseDelay();

    std::uint32_t ResetSkillReuseDelay(CResetSkillReuseDelayFilter& filter);
    std::uint32_t ResetSkillReuseDelay(int skillId, CResetSkillReuseDelayFilter& filter);
    std::uint32_t ResetSkillReuseDelay(const std::vector<int>& skillIds, int reuseDelayLimit);
    std::uint32_t ReduceSkillReuuseDelay(const std::deque<int>& skillIds, int reduction, int reuseDelayLimit);
    void AddOrUpdateSkillReuseDelay(int skillId, int subjobId, std::uint32_t reuseDelay, std::uint32_t remainDelay);
    bool RemoveSkillUsableTime(int skillId);

    std::uint32_t GetSkillUsedTime(int skillId, int level, int subLevel);
    bool CheckAndRemoveSkillUsableTime(int skillId, int level, int subLevel);
    bool TrySaveSkillUsedTime(User* user, ReuseDelaySaveReason::Type reason);
    bool SaveSkillUsedTime(User* user, ReuseDelaySaveReason::Type reason, bool clearAfterSave);
    void SendCoolTime(User* user);

protected:
    std::uint32_t _GetSkillUsedTime(int skillId, int level, int subLevel);
    bool _RemoveSkillUsableTime(int skillId);
    std::uint32_t _ResetSkillReuseDelay(CResetSkillReuseDelayFilter& filter);
    std::uint32_t _ResetSkillReuseDelay(int skillId, CResetSkillReuseDelayFilter& filter);
    std::uint32_t _ReduceSkillReuseDelay(int skillId, int reduction, CResetSkillReuseDelayFilter& filter);


    ::CYieldLock m_UsedSkillsSect;
    ::std::map<int, ReuseDelayData> m_UsedSkillsLastTimeMap;
};
