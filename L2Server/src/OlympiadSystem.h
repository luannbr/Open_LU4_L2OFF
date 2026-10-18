#pragma once

#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct AlterSkillInfo;
class CParserForSetting;

enum OlympiadTeamEnum : std::int32_t {
    OTE_TEAM1 = 0,
    OTE_TEAM2 = 1,
    OTE_MAX = 2,
};

enum LogOlympiadEnterType : std::int32_t {
    LOET_PLAYER = 0,
    LOET_OBSERVER = 1,
    LOET_MAX = 2,
};


struct OlympiadResetSkillInfo {
    std::int32_t reuseDelayLimit{}; // +0x00
    std::array<std::byte, 0x04> m_alignment04To08{}; // +0x04..+0x07
    std::vector<std::int32_t> notResetSkillIdList; // +0x08..+0x27
};

// Current L2Server.exe 0x00917268/0x009a6fa4. This is the physical
// OlympiadSystem.cpp owner embedded at CPledgeGameSystem+0x60. It is distinct
// from the newer Singleton-based WOlympiadDefaultSetting.
class COlympiadDefaultSetting {
public:
    struct BonusItemData {
        std::int32_t itemClassId{};
        std::int32_t itemCount{};
        std::int32_t itemClassIdForLoser{};
        std::int32_t itemCountForLoser{};
    };

    COlympiadDefaultSetting();
    ~COlympiadDefaultSetting();

    bool IsPlayingTime(std::int32_t time) const;

private:
    // Current Setting parser actions 58/59 write these two embedded fields
    // directly. Retain that original access contract without proxy setters.
    friend class CParserForSetting;

    std::set<std::int32_t> m_ForbiddenWeapons; // +0x0000..+0x001f
    std::set<std::int32_t> m_ForbiddenSkills; // +0x0020..+0x003f
    std::set<std::int32_t> m_ForbiddenItems; // +0x0040..+0x005f
    std::vector<AlterSkillInfo*> m_GiveSkills[108]; // +0x0060..+0x0ddf
    OlympiadResetSkillInfo m_ResetSkillInfo; // +0x0de0..+0x0e07
    BonusItemData m_BonusItemData[3]; // +0x0e08..+0x0e37
    std::int32_t m_actionCount{}; // +0x0e38
    std::uint8_t m_buf[0x4f9c]{}; // +0x0e3c..+0x5dd7
    std::uint32_t m_bufLen{}; // +0x5dd8
    std::int32_t m_StartTime{}; // +0x5ddc
    std::int32_t m_EndTime{}; // +0x5de0
    std::int32_t m_Denominator[3]{}; // +0x5de4..+0x5def
    std::int32_t m_Weight[3]{}; // +0x5df0..+0x5dfb
    std::int32_t m_MatchLimit[4]{}; // +0x5dfc..+0x5e0b
    std::int32_t SELECTION_MIN_COUNT[3]{}; // +0x5e0c..+0x5e17
    std::vector<std::int32_t> m_vMatchDayList; // +0x5e18..+0x5e37
};
