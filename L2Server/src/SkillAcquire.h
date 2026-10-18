#pragma once

#include "L2Core/src/inc/XStd.h"
#include "Protocol/src/L2Constants.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/PledgeType.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

enum EtcSkillAcquireType : std::int32_t {
    ESAT_NORMAL = 0,
    ESAT_FISHING = 1,
    ESAT_PLEDGE = 2,
    ESAT_SUB_PLEDGE = 3,
    ESAT_TRANSFORM = 4,
    ESAT_SUBJOB = 5,
    ESAT_COLLECT = 6,
    ESAT_BISHOP_SHARING = 7,
    ESAT_ELDER_SHARING = 8,
    ESAT_SILEN_ELDER_SHARING = 9,
    ESAT_NONDWARF_FISHING = 10,
    ESAT_DUALJOB = 47,
    ESAT_DUALJOB_SHARING = 48,
    ESAT_SIGEL_SHARING = 49,
    ESAT_TIR_SHARING = 50,
    ESAT_OTHEL_SHARING = 51,
    ESAT_YR_SHARING = 52,
    ESAT_FEOH_SHARING = 53,
    ESAT_IS_SHARING = 54,
    ESAT_WYNN_SHARING = 55,
    ESAT_EOLH_SHARING = 56,
    ESAT_TRIBE_SHARING = 57,
    ESAT_REWARD_NOREWARD = 58,
    ESAT_TRIBE2_SHARING = 59,
    ESAT_REWARD_DUELIST = 88,
    ESAT_REWARD_DREADNOUGHT = 89,
    ESAT_REWARD_PHOENIX_KNIGHT = 90,
    ESAT_REWARD_HELL_KNIGHT = 91,
    ESAT_REWARD_SAGITTARIUS = 92,
    ESAT_REWARD_ADVENTURER = 93,
    ESAT_REWARD_ARCHMAGE = 94,
    ESAT_REWARD_SOULTAKER = 95,
    ESAT_REWARD_ARCANA_LORD = 96,
    ESAT_REWARD_CARDINAL = 97,
    ESAT_REWARD_HIEROPHANT = 98,
    ESAT_REWARD_EVAS_TEMPLAR = 99,
    ESAT_REWARD_SWORD_MUSE = 100,
    ESAT_REWARD_WIND_RIDER = 101,
    ESAT_REWARD_MOONLIGHT_SENTINEL = 102,
    ESAT_REWARD_MYSTIC_MUSE = 103,
    ESAT_REWARD_ELEMENTAL_MASTER = 104,
    ESAT_REWARD_EVAS_SAINT = 105,
    ESAT_REWARD_SHILLIEN_TEMPLAR = 106,
    ESAT_REWARD_SPECTRAL_DANCER = 107,
    ESAT_REWARD_GHOST_HUNTER = 108,
    ESAT_REWARD_GHOST_SENTINEL = 109,
    ESAT_REWARD_STORM_SCREAMER = 110,
    ESAT_REWARD_SPECTRAL_MASTER = 111,
    ESAT_REWARD_SHILLIEN_SAINT = 112,
    ESAT_REWARD_TITAN = 113,
    ESAT_REWARD_GRAND_KHAVATARI = 114,
    ESAT_REWARD_DOMINATOR = 115,
    ESAT_REWARD_DOOMCRYER = 116,
    ESAT_REWARD_FORTUNE_SEEKER = 117,
    ESAT_REWARD_MAESTRO = 118,
    ESAT_REWARD_DOOMBRINGER = 131,
    ESAT_REWARD_M_SOUL_HOUND = 132,
    ESAT_REWARD_F_SOUL_HOUND = 133,
    ESAT_REWARD_TRICKSTER = 134,
    ESAT_REWARD_JUDICATOR = 136,
    ESAT_MENTOR = 137,
    ESAT_MENTEE = 138,
    ESAT_POTENTIAL = 139,
    ESAT_ALCHEMY = 140,
    ESAT_HONORS = 141,
    ESAT_MAX = 142,
};


class CSkillInfo;

struct SkillAcquireInfo {
    SkillAcquireInfo();

    int nSkillID; // +0x000
    int nSkillLevel; // +0x004
    ClassType cls; // +0x008
    EtcSkillAcquireType etcType; // +0x00c
    RaceType raceType; // +0x010
    int nGetLevel; // +0x014
    int m_GetMainDualClassLevel; // +0x018
    std::vector<int> vNeededItem; // +0x020
    std::vector<std::int64_t> vNeededItemCount; // +0x040
    std::int64_t nLevelUP_SP; // +0x060
    bool bNeedQuest; // +0x068
    bool bAutoGet; // +0x069
    int m_SocialClass; // +0x06c
    PledgeType m_MaximumPledgeType; // +0x070
    ::std::set<RaceType> m_RaceConditions; // +0x078
    std::vector<const CSkillInfo*> m_PrerequisiteSkills; // +0x098
    std::vector<const CSkillInfo*> m_ReplaceableSkills; // +0x0b8
    bool m_bResidenceSkill; // +0x0d8
    bool m_bMentorMenteeSkill; // +0x0d9
    bool m_bExcludedFromListOfSkillsToLearn; // +0x0da
    std::vector<int> vNeededQuest; // +0x0e0
};


class CSkillAcquireDB {
public:
    using ClassSkillKey = std::CTriKey<ClassType, int, int>;
    using ClassMinimumLevelKey = std::CDualKey<ClassType, int>;
    using EtcSkillKey = std::CTriKey<EtcSkillAcquireType, int, int>;
    using EtcMinimumLevelKey = std::CDualKey<EtcSkillAcquireType, int>;
    using SkillAcquireInfoVector = std::vector<SkillAcquireInfo*>;

    CSkillAcquireDB();
    ~CSkillAcquireDB();

    void Load();
    void Add(SkillAcquireInfo* info);
    void AddEtcSkill(SkillAcquireInfo* info);
    void AddRaceSkill(SkillAcquireInfo* info);

    const SkillAcquireInfoVector& GetAcquirableSkills(ClassType classType) const;
    const SkillAcquireInfoVector& GetAcquirableEtcSkills(EtcSkillAcquireType type) const;
    const SkillAcquireInfoVector& GetAcquirableRaceSkills(RaceType raceType) const;
    const SkillAcquireInfoVector& GetAcquirableSkills(EtcSkillAcquireType type, ClassType classType) const;
    SkillAcquireInfo* GetSkillAcquireInfo(ClassType classType, int skillId, int level);
    SkillAcquireInfo* GetSkillAcquireInfo(EtcSkillAcquireType type, ClassType classType, int skillId, int level);
    SkillAcquireInfo* GetEtcSkillAcquireInfo(EtcSkillAcquireType type, int skillId, int level);
    int GetMinimumLevel(ClassType classType, int skillId) const;
    int GetEtcMinimumLevel(EtcSkillAcquireType type, int skillId) const;
    bool IsMatchSkillAcquireType(EtcSkillAcquireType type, int skillId, int level);

    // These owners are public in the original source: User/Creature acquire
    // paths index the vectors directly.
    ::std::map<ClassSkillKey, SkillAcquireInfo*> m_mapClsSkill_to_info; // +0x0000
    ::std::map<ClassMinimumLevelKey, int> m_mapClsSkill_to_minLevel; // +0x0020
    std::array<SkillAcquireInfoVector, CT_MAX> m_vectorClsSkill; // +0x0040
    ::std::map<EtcSkillKey, SkillAcquireInfo*> m_mapEtcSkill_to_info; // +0x1840
    ::std::map<EtcMinimumLevelKey, int> m_mapEtcSkill_to_minLevel; // +0x1860
    std::array<SkillAcquireInfoVector, ESAT_MAX> m_vectorEsatSkill; // +0x1880
    std::array<SkillAcquireInfoVector, RT_MAX> m_vectorRaceSkill; // +0x2a40
};


extern CSkillAcquireDB g_SkillAcquireDB;
