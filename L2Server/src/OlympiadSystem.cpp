#include "OlympiadSystem.h"

#include "L2Core/src/inc/Guard.h"

COlympiadDefaultSetting::COlympiadDefaultSetting() :
    m_GiveSkills{},
    m_ResetSkillInfo{},
    m_BonusItemData{},
    m_actionCount(0),
    m_buf{},
    m_bufLen(0),
    m_StartTime(1200),
    m_EndTime(1440),
    m_Denominator{3, 3, 3},
    m_Weight{100, 100, 100},
    m_MatchLimit{30, 30, 30, 70},
    SELECTION_MIN_COUNT{0, 20, 10} {
    L2_GUARD_FUNCTION();
}

COlympiadDefaultSetting::~COlympiadDefaultSetting() = default;

bool COlympiadDefaultSetting::IsPlayingTime(const std::int32_t time) const {
    if (m_EndTime - m_StartTime < 0) {
        if ((time < 0 || m_EndTime <= time) && (time < m_StartTime || 1440 < time)) {
            return false;
        }
    } else {
        if (time < m_StartTime || m_EndTime <= time) {
            return false;
        }
    }
    return true;
}
