#pragma once

#include "Protocol/src/L2Constants.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"

#include <cstdint>
#include <vector>

enum MoveModeEnum : std::int32_t;

class CParserForPCParam;

using PCParamDoubleVector = std::vector<double, std::allocator<double>>;

enum FighterMagicType : std::int32_t {
    FMT_FIGHTER = 0,
    FMT_MAGIC = 1,
};

enum PCParamFieldBySRC : std::int32_t {
    PC_INT_DEFAULT = 0,
    PC_INT_MIN = 1,
    PC_INT_MAX = 2,
    PC_STR_DEFAULT = 3,
    PC_STR_MIN = 4,
    PC_STR_MAX = 5,
    PC_CON_DEFAULT = 6,
    PC_CON_MIN = 7,
    PC_CON_MAX = 8,
    PC_MEN_DEFAULT = 9,
    PC_MEN_MIN = 10,
    PC_MEN_MAX = 11,
    PC_DEX_DEFAULT = 12,
    PC_DEX_MIN = 13,
    PC_DEX_MAX = 14,
    PC_WIT_DEFAULT = 15,
    PC_WIT_MIN = 16,
    PC_WIT_MAX = 17,
    PC_BASE_PATTACK = 18,
    PC_BASE_CRITICAL = 19,
    PC_BASE_ATTACK_TYPE = 20,
    PC_BASE_ATTACK_SPEED = 21,
    PC_BASE_PDEFEND = 22,
    PC_BASE_MATTACK = 23,
    PC_BASE_MDEFEND = 24,
    PC_BASE_CAN_PENETRATE = 25,
    PC_BASE_ATTACK_RANGE = 26,
    PC_BASE_DAMAGE_RANGE = 27,
    PC_BASE_RAND_DAM = 28,
    PC_ORG_HP_REGEN = 29,
    PC_ORG_MP_REGEN = 30,
    PC_ORG_CP_REGEN = 31,
    PC_MOVING_SPEED = 32,
    PC_ORG_JUMP = 33,
    PC_BREATH_BONUS = 34,
    PC_SAFE_FALL_HEIGHT = 35,
    PC_COLLISION_BOX = 36,
};

enum PCParamFieldByLevel : std::int32_t {
    PC_LEVEL_BONUS = 0,
    PC_INT_BONUS = 1,
    PC_STR_BONUS = 2,
    PC_DEX_BONUS = 3,
    PC_WIT_BONUS = 4,
    PC_CON_BONUS = 5,
    PC_MEN_BONUS = 6,
    PC_LUC_BONUS = 7,
    PC_CHA_BONUS = 8,
    PC_ORG_HP = 9,
    PC_ORG_MP = 10,
    PC_ORG_CP = 11,
    PC_KARMA_INCREASE = 12,
    PC_STANDARD_EXP_BY_LEV = 13,
};

namespace PCBODY {

enum Enum : std::int32_t {
    NONE = -1,
    JACKET = 0,
    PANTS = 1,
    HELM = 2,
    BOOT = 3,
    GLOVE = 4,
    UNDERWEAR = 5,
    CLOAK = 6,
    PDEFEND_SUM = 7,
    R_EAR = 8,
    L_EAR = 9,
    R_RING = 10,
    L_RING = 11,
    NECKLACE = 12,
    MDEFEND_SUM = 13,
    MAX = 14,
};

} // namespace PCBODY

namespace ClassGroupType {

enum Enum : std::int32_t {
    NONE = 0,
    FIRST_CHANGED = 1,
    SECOND_CHANGED = 2,
    THIRD_CHANGED = 3,
    FOURTH_CHANGED = 4,
    FIFTH_CHANGED = 5,
};

} // namespace ClassGroupType

extern ::RaceType CT_to_RT[192];
extern ClassGroupType::Enum ClassToGroup[192];
extern int ClassToHennaSlotCount[193];
extern FighterMagicType CT_to_FMT[192];
extern std::int64_t CT_to_CBT[192];
extern const wchar_t* _PCParamFieldBySRC[37];
extern const wchar_t* _PCParamFieldByLevel[14];

extern double g_OrgHPRegenTable[2][30][2][121];
extern double g_OrgMPRegenTable[2][30][2][121];
extern double g_OrgCPRegenTable[2][30][2][121];
extern double g_CollisionRadiusBySRC[2][30][2];
extern double g_CollisionHeightBySRC[2][30][2];
extern double g_OrgHPTable[192][122];
extern double g_OrgMPTable[192][122];
extern double g_OrgCPTable[192][122];
extern double g_OrgSpeedBySRCME[2][30][2][2][4];

class CPCParamDB {
private:
    friend class CParserForPCParam;

    double m_dLevelBonus[122];
    std::int64_t m_nStandardExpByLevel[122];
    double m_dIntBonus[201];
    double m_dStrBonus[201];
    double m_dConBonus[201];
    double m_dMenBonus[201];
    double m_dDexBonus[201];
    double m_dWitBonus[201];
    double m_dLucBonus[201];
    double m_dChaBonus[201];
    std::int32_t m_BaseAttackTypeBySRC[2][30][2];
    bool m_BaseCanPenetrateBySRC[2][30][2];
    std::int32_t m_BaseAttackRangeBySRC[2][30][2];
    std::int32_t m_BaseRandomDamageBySRC[2][30][2];
    std::int32_t m_BasePAttackBySRC[2][30][2];
    std::int32_t m_BasePDefendBySRC[2][30][2];
    std::int32_t m_BaseAttackSpeedBySRC[2][30][2];
    std::int32_t m_BaseCriticalProbBySRC[2][30][2];
    std::int32_t m_BaseMAttackBySRC[2][30][2];
    std::int32_t m_BaseMDefendBySRC[2][30][2];
    std::int32_t m_BaseBreathBonusBySRC[2][30][2];
    std::int32_t m_BaseSafeFallHeightBySRC[2][30][2];
    double m_BasePDefendBodyBySRC[2][30][2][14];
    double m_BaseMDefendBodyBySRC[2][30][2][14];
    ::DamageRangeType m_OrgBaseDamageRangeTable[2][30][2];
    double m_BaseRegenMoveModeBonus[4];
    double m_BaseHPRegenMoveModeBonus[4][6];
    double m_BaseMPRegenMoveModeBonus[4][6];
    double m_BaseCPRegenMoveModeBonus[4][6];
    double m_KarmaIncreaseTable[122];

    void SetValueBySRC(::SexType, ::RaceType, FighterMagicType, PCParamFieldBySRC, int);
    void SetValueBySRC(::SexType, ::RaceType, FighterMagicType, PCParamFieldBySRC, int, int, int, int);
    void SetValueBySRC(::SexType, ::RaceType, FighterMagicType, PCParamFieldBySRC, int, int, int, int, int, int, int, int);
    void SetValueBySRC(::SexType, ::RaceType, FighterMagicType, PCParamFieldBySRC, double, double);
    void SetValueBySRC(
        ::SexType,
        ::RaceType,
        FighterMagicType,
        PCParamFieldBySRC,
        double,
        double,
        double,
        double,
        double,
        double,
        double,
        double
    );
    void SetValueBySRC(::SexType, ::RaceType, FighterMagicType, PCParamFieldBySRC, PCParamDoubleVector*);
    void SetValueByLevel(int, PCParamFieldByLevel, int);
    void SetValueByLevel(int, PCParamFieldByLevel, double);
    void SetValueByLevelClass(int, ClassType, PCParamFieldByLevel, double);

    void SetStandardExpByLevel(int level, PCParamFieldByLevel field, std::int64_t value);

public:
    void Load(bool);
    double GetCollisionRadius(::SexType sexType, ClassType classType, ::RaceType raceType);
    double GetCollisionHeight(::SexType sexType, ClassType classType, ::RaceType raceType);
    double GetOrgHP(ClassType classType, int level);
    double GetOrgMP(ClassType classType, int level);
    double GetOrgCP(ClassType classType, int level);
    double GetOrgHPRegen(::SexType sexType, ::RaceType raceType, FighterMagicType fighterMagicType, int level);
    double GetOrgCPRegen(::SexType sexType, ::RaceType raceType, FighterMagicType fighterMagicType, int level);
    double GetOrgMPRegen(::SexType sexType, ::RaceType raceType, FighterMagicType fighterMagicType, int level);
    static ::RaceType GetRaceByClass(ClassType classType, ::RaceType raceType);
    static ClassType GetBasicClassByRaceAndFTM(::RaceType raceType, bool fighter, bool male);
    int GetMaxHennaSlotCount(ClassType classType);
    int GetInventoryLimitToChangeSubjob(ClassType classType, ::RaceType raceType);
    int GetOriMaxPrivateBankCount(ClassType classType, ::RaceType raceType);
    int GetOriMaxInventoryCount(ClassType classType, ::RaceType raceType);
    int GetOriMaxPrivateStoreCount(ClassType classType, ::RaceType raceType);
    int GetOriMaxPrivateStoreBuyCount(ClassType classType, ::RaceType raceType);
    ::DamageRangeType GetBaseDamageRangeValue(::SexType sexType, ClassType classType, ::RaceType raceType);
    double GetOrgSpeed(
        ::SexType sexType, ClassType classType, ::MoveType moveType, ::EnvType environmentType, ::RaceType raceType
    );
    int GetIntValueByField(::SexType sexType, ClassType classType, PCParamFieldBySRC field, ::RaceType raceType);
    double GetBodyPart(
        ::SexType sexType, ClassType classType, PCBODY::Enum bodyPart, VALIDATE::Enum validateType, ::RaceType raceType
    );
    void SetRegenMoveModeBonus(MoveModeEnum, double);
    void SetRegenMoveModeHPBonus(MoveModeEnum, double, double, double, double, double, double);
    void SetRegenMoveModeMPBonus(MoveModeEnum, double, double, double, double, double, double);
    void SetRegenMoveModeCPBonus(MoveModeEnum, double, double, double, double, double, double);
    double GetParamBonus(PCParamFieldByLevel, int);
    double GetRegenMoveModeBonus(MoveModeEnum);
    double GetHPRegenMoveModeBonus(MoveModeEnum, int);
    double GetMPRegenMoveModeBonus(MoveModeEnum, int);
    double GetCPRegenMoveModeBonus(MoveModeEnum, int);
    double GetKarmaIncreaseAmount(int);
    std::int64_t GetStandardExpByLevel(int);
};

extern CPCParamDB g_PCParamDB;
