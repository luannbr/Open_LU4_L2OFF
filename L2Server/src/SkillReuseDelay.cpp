#include "SkillReuseDelay.h"

#include "Creature.h"
#include "DB.h"
#include "Skill.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/IOC.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/platform/Runtime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>


ReuseDelayData::ReuseDelayData(int subjobId, std::uint32_t remainDelay, std::uint32_t delay) :
    m_nSubjobId(subjobId), m_nRemainDelay(remainDelay), m_nDelay(delay), m_bDirtyFlag(true) {}

CResetSkillReuseDelayFilter::~CResetSkillReuseDelayFilter() = default;

bool CNullResetSkillReuseDelayFilter::CanDo(int, ReuseDelayData&) { return true; }

CAcquireSkillResetSkillReuseDelayFilter::CAcquireSkillResetSkillReuseDelayFilter(CCreature* creature) {
    if (creature != nullptr) {
        L2_GUARD(creature->m_AcquiredSkillsSect);
        m_AcquireSkills = creature->m_AcquiredSkills;
    }
}

CSkillReuseDelay::CSkillReuseDelay() {}

CSkillReuseDelay::~CSkillReuseDelay() = default;

void CSkillReuseDelay::SendCoolTime(User* user) {
    L2_GUARD_FUNCTION();

    if (user == nullptr) {
        return;
    }

    std::array<char, 0x5000> buffer{};
    int length = 0;
    int count = 0;
    L2_GUARD(m_UsedSkillsSect);
    for (const auto& entry : m_UsedSkillsLastTimeMap) {
        const ReuseDelayData& data = entry.second;
        const std::uint32_t now = runtime::GetTickCount();
        if (now < data.m_nRemainDelay) {
            const std::uint32_t remaining = data.m_nRemainDelay - now;
            if (remaining > 1000U) {
                length += ::Assemble(
                    buffer.data() + length, static_cast<int>(buffer.size()) - length, "dddd", entry.first, 0,
                    data.m_nDelay / 1000U, remaining / 1000U
                );
                ++count;
            }
        }
    }
    user->GetSocket()->Send("cdb", 0xc7, count, length, buffer.data());
}

std::uint32_t CSkillReuseDelay::_ResetSkillReuseDelay(CResetSkillReuseDelayFilter& filter) {
    L2_GUARD_FUNCTION();

    std::uint32_t maximumDelay = 0;
    for (auto& entry : m_UsedSkillsLastTimeMap) {
        ReuseDelayData& data = entry.second;
        if (filter.CanDo(entry.first, data)) {
            data.m_bDirtyFlag = true;
            data.m_nRemainDelay = 0;
            maximumDelay = std::max(maximumDelay, data.m_nDelay);
        }
    }
    return maximumDelay;
}

std::uint32_t CSkillReuseDelay::ResetSkillReuseDelay(CResetSkillReuseDelayFilter& filter) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_UsedSkillsSect);
    return _ResetSkillReuseDelay(filter);
}

std::uint32_t CSkillReuseDelay::_ResetSkillReuseDelay(int skillId, CResetSkillReuseDelayFilter& filter) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_UsedSkillsSect);

    const auto found = m_UsedSkillsLastTimeMap.find(skillId);
    if (found == m_UsedSkillsLastTimeMap.end() || !filter.CanDo(found->first, found->second)) {
        return 0;
    }

    ReuseDelayData& data = found->second;
    data.m_bDirtyFlag = true;
    data.m_nRemainDelay = 0;
    return data.m_nDelay;
}

std::uint32_t CSkillReuseDelay::ResetSkillReuseDelay(int skillId, CResetSkillReuseDelayFilter& filter) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_UsedSkillsSect);
    return _ResetSkillReuseDelay(skillId, filter);
}

std::uint32_t CSkillReuseDelay::ResetSkillReuseDelay(const std::vector<int>& skillIds, int reuseDelayLimit) {
    L2_GUARD_FUNCTION();

    class CSkillEffectResetSkillReuseDelayFilter final : public CResetSkillReuseDelayFilter {
    public:
        explicit CSkillEffectResetSkillReuseDelayFilter(std::uint32_t limit) : m_limit(limit) {}

        bool CanDo(int, ReuseDelayData& data) override { return data.m_nDelay < m_limit; }

    private:
        std::uint32_t m_limit;
    };


    CSkillEffectResetSkillReuseDelayFilter filter(static_cast<std::uint32_t>(reuseDelayLimit));
    std::uint32_t maximumDelay = 0;
    L2_GUARD(m_UsedSkillsSect);
    for (const int skillId : skillIds) {
        maximumDelay = std::max(maximumDelay, _ResetSkillReuseDelay(skillId, filter));
    }
    return maximumDelay;
}

std::uint32_t CSkillReuseDelay::_ReduceSkillReuseDelay(int skillId, int reduction, CResetSkillReuseDelayFilter& filter) {
    L2_GUARD_FUNCTION();

    const auto found = m_UsedSkillsLastTimeMap.find(skillId);
    if (found == m_UsedSkillsLastTimeMap.end() || !filter.CanDo(found->first, found->second)) {
        return 0;
    }

    ReuseDelayData& data = found->second;
    data.m_bDirtyFlag = true;
    const std::uint32_t amount = static_cast<std::uint32_t>(reduction);
    if (amount < data.m_nRemainDelay) {
        data.m_nRemainDelay -= amount;
    } else {
        data.m_nRemainDelay = 0;
    }
    if (data.m_nRemainDelay <= runtime::GetTickCount()) {
        data.m_nRemainDelay = 0;
    }
    return data.m_nDelay;
}

std::uint32_t CSkillReuseDelay::ReduceSkillReuuseDelay(const std::deque<int>& skillIds, int reduction, int reuseDelayLimit) {
    L2_GUARD_FUNCTION();

    class CSkillEffectReduceSkillReuseDelayFilter final : public CResetSkillReuseDelayFilter {
    public:
        explicit CSkillEffectReduceSkillReuseDelayFilter(std::uint32_t limit) : m_limit(limit) {}

        bool CanDo(int, ReuseDelayData& data) override { return data.m_nDelay < m_limit; }

    private:
        std::uint32_t m_limit;
    };


    CSkillEffectReduceSkillReuseDelayFilter filter(static_cast<std::uint32_t>(reuseDelayLimit));
    std::uint32_t maximumDelay = 0;
    L2_GUARD(m_UsedSkillsSect);
    for (const int skillId : skillIds) {
        maximumDelay = std::max(maximumDelay, _ReduceSkillReuseDelay(skillId, reduction, filter));
    }
    return maximumDelay;
}

bool CSkillReuseDelay::_RemoveSkillUsableTime(int skillId) {
    L2_GUARD_FUNCTION();

    const auto found = m_UsedSkillsLastTimeMap.find(skillId);
    if (found == m_UsedSkillsLastTimeMap.end()) {
        return false;
    }
    m_UsedSkillsLastTimeMap.erase(found);
    return true;
}

std::uint32_t CSkillReuseDelay::_GetSkillUsedTime(int skillId, int level, int subLevel) {
    L2_GUARD_FUNCTION();

    if (!g_SkillDB.GetSkillInfo(CSkillKey(skillId, level, subLevel), nullptr)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] no SkillInfo exist! skillId[%d], level[%d]",
        //     L"SkillReuseDelay.cpp",
        //     0x19c,
        //     skillId,
        //     1)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] no SkillInfo exist! skillId[%d], level[%d]",
        //     L"SkillReuseDelay.cpp",
        //     0x19c,
        //     skillId,
        //     1)
        L2_LOG_ERROR(
            Log,
            "Reuse-time lookup cannot resolve requested skill %d level %d sublevel %d; checking recorded times continues.",
            skillId, level, subLevel
        );
    }

    const auto found = m_UsedSkillsLastTimeMap.find(skillId);
    if (found != m_UsedSkillsLastTimeMap.end()) {
        return found->second.m_nRemainDelay;
    }

    const CSkillInfo* const requestedSkill = g_SkillDB.GetSkillInfo(CSkillKey(skillId, level, subLevel), nullptr);
    if (!requestedSkill || requestedSkill->m_nReuseDelayGroup <= 0) {
        return 0;
    }
    for (const auto& entry : m_UsedSkillsLastTimeMap) {
        const CSkillInfo* const usedSkill = g_SkillDB.GetSkillInfo(CSkillKey(entry.first, 1), nullptr);
        if (usedSkill && usedSkill->m_nReuseDelayGroup == requestedSkill->m_nReuseDelayGroup) {
            return entry.second.m_nRemainDelay;
        }
    }
    return 0;
}

bool CSkillReuseDelay::RemoveSkillUsableTime(int skillId) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_UsedSkillsSect);
    return _RemoveSkillUsableTime(skillId);
}

std::uint32_t CSkillReuseDelay::GetSkillUsedTime(int skillId, int level, int subLevel) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_UsedSkillsSect);
    return _GetSkillUsedTime(skillId, level, subLevel);
}

bool CSkillReuseDelay::CheckAndRemoveSkillUsableTime(int skillId, int level, int subLevel) {
    L2_GUARD_FUNCTION();
    L2_GUARD(m_UsedSkillsSect);
    if (_GetSkillUsedTime(skillId, level, subLevel) == 0) {
        return false;
    }
    return _RemoveSkillUsableTime(skillId);
}

bool CSkillReuseDelay::SaveSkillUsedTime(User* user, ReuseDelaySaveReason::Type reason, bool clearAfterSave) {
    L2_GUARD_FUNCTION();

    if (user == nullptr) {
        return false;
    }

    std::array<char, 0x400> normalBuffer{};
    std::array<char, 0x400> transformedBuffer{};
    int normalLength = 0;
    int transformedLength = 0;
    int normalCount = 0;
    int transformedCount = 0;

    L2_GUARD(m_UsedSkillsSect);
    const std::int32_t nowTime = runtime::crt::Time32Now();
    const std::uint32_t nowTick = runtime::GetTickCount();

    for (auto& entry : m_UsedSkillsLastTimeMap) {
        ReuseDelayData& data = entry.second;
        if (data.m_bDirtyFlag == true) {
            std::uint32_t expiry = 0;
            if (data.m_nRemainDelay != 0U) {
                const std::int64_t remainingMilliseconds =
                    static_cast<std::int64_t>(data.m_nRemainDelay) - static_cast<std::int64_t>(nowTick);
                const std::int64_t remainingSeconds = remainingMilliseconds / 1000;
                expiry = static_cast<std::uint32_t>(static_cast<std::int64_t>(nowTime) + remainingSeconds);
            }

            if (data.m_nSubjobId == 4) {
                transformedLength += ::Assemble(
                    transformedBuffer.data() + transformedLength,
                    static_cast<int>(transformedBuffer.size()) - transformedLength, "ddd", entry.first, expiry, data.m_nDelay
                );
                ++transformedCount;
            } else {
                normalLength += ::Assemble(
                    normalBuffer.data() + normalLength, static_cast<int>(normalBuffer.size()) - normalLength, "ddd",
                    entry.first, expiry, data.m_nDelay
                );
                ++normalCount;
            }
            data.m_bDirtyFlag = false;
        }
    }

    if (clearAfterSave) {
        m_UsedSkillsLastTimeMap.clear();
    }
    if (normalCount > 0 || transformedCount > 0) {
        db.SaveSkillUse(
            *user, reason, normalCount, transformedCount, normalLength, normalBuffer.data(), transformedLength,
            transformedBuffer.data()
        );
    }
    return true;
}

bool CSkillReuseDelay::TrySaveSkillUsedTime(User* user, ReuseDelaySaveReason::Type reason) {
    L2_GUARD_FUNCTION();
    if (m_UsedSkillsSect.IsEntered()) {
        return false;
    }
    return SaveSkillUsedTime(user, reason, false);
}

void CSkillReuseDelay::AddOrUpdateSkillReuseDelay(
    int skillId, int subjobId, std::uint32_t reuseDelay, std::uint32_t remainDelay
) {
    L2_GUARD_FUNCTION();

    if (reuseDelay == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Don't add reuse delay 0 or minus. reuseDelay = %d",
        //     static_cast<int>(reuseDelay))
        // L2_LOG_ERROR(Log,
        //     L"Don't add reuse delay 0 or minus. reuseDelay = %d",
        //     static_cast<int>(reuseDelay))
        L2_LOG_ERROR(
            Log,
            "Cannot add or update reuse time for skill %d, subjob %d: reuse delay is 0; the existing record is left "
            "unchanged.",
            skillId, subjobId
        );
        return;
    }
    if (remainDelay == 0) {
        remainDelay = reuseDelay;
    }

    L2_GUARD(m_UsedSkillsSect);
    std::uint32_t deadline = 0;
    ::CIOObject::SetShortTimeStamp(&deadline, remainDelay);

    const auto found = m_UsedSkillsLastTimeMap.find(skillId);
    if (found == m_UsedSkillsLastTimeMap.end()) {
        m_UsedSkillsLastTimeMap.emplace(skillId, ReuseDelayData(subjobId, deadline, reuseDelay));
        return;
    }

    ReuseDelayData& data = found->second;
    data.m_nSubjobId = subjobId;
    data.m_nRemainDelay = deadline;
    data.m_nDelay = reuseDelay;
    data.m_bDirtyFlag = true;
}
