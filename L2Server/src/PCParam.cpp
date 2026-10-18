#include "PCParam.h"

#include "Shared/src/TextRuntime.h"

#include "PCParamYacc.h"

#include "Shared/src/Log.h"

#include <algorithm>
#include <cstring>

RaceType CT_to_RT[192] = {
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_ORC,
    RT_ORC,
    RT_ORC,
    RT_ORC,
    RT_ORC,
    RT_ORC,
    RT_ORC,
    RT_ORC,
    RT_ORC,
    RT_DWARF,
    RT_DWARF,
    RT_DWARF,
    RT_DWARF,
    RT_DWARF,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_HUMAN,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_ELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_DARKELF,
    RT_ORC,
    RT_ORC,
    RT_ORC,
    RT_ORC,
    RT_DWARF,
    RT_DWARF,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_KAMAEL,
    RT_KAMAEL,
    RT_KAMAEL,
    RT_KAMAEL,
    RT_KAMAEL,
    RT_KAMAEL,
    RT_KAMAEL,
    RT_KAMAEL,
    RT_KAMAEL,
    RT_KAMAEL,
    RT_KAMAEL,
    RT_KAMAEL,
    RT_KAMAEL,
    RT_KAMAEL,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_NONE,
    RT_ERTHEIA,
    RT_ERTHEIA,
    RT_ERTHEIA,
    RT_ERTHEIA,
    RT_ERTHEIA,
    RT_ERTHEIA,
    RT_ERTHEIA,
    RT_ERTHEIA,
    RT_ERTHEIA,
    RT_ERTHEIA,
};

ClassGroupType::Enum ClassToGroup[192] = {
    ClassGroupType::NONE,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::NONE,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::NONE,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::NONE,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::NONE,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::NONE,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::NONE,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::NONE,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::NONE,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::FIRST_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::FOURTH_CHANGED,
    ClassGroupType::FOURTH_CHANGED,
    ClassGroupType::FOURTH_CHANGED,
    ClassGroupType::FOURTH_CHANGED,
    ClassGroupType::FOURTH_CHANGED,
    ClassGroupType::FOURTH_CHANGED,
    ClassGroupType::FOURTH_CHANGED,
    ClassGroupType::FOURTH_CHANGED,
    ClassGroupType::FOURTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::NONE,
    ClassGroupType::NONE,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::SECOND_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::THIRD_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FIFTH_CHANGED,
    ClassGroupType::FOURTH_CHANGED,
    ClassGroupType::FOURTH_CHANGED,
};

int ClassToHennaSlotCount[193] = {
    0,
    2,
    3,
    3,
    2,
    3,
    3,
    2,
    3,
    3,
    0,
    2,
    3,
    3,
    3,
    2,
    3,
    3,
    0,
    2,
    3,
    3,
    2,
    3,
    3,
    0,
    2,
    3,
    3,
    2,
    3,
    0,
    2,
    3,
    3,
    2,
    3,
    3,
    0,
    2,
    3,
    3,
    2,
    3,
    0,
    2,
    3,
    2,
    3,
    0,
    2,
    3,
    3,
    0,
    2,
    3,
    2,
    3,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    0,
    0,
    0,
    0,
    0,
    0,
    2,
    2,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    0,
    0,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    3,
    0,
    0,
    2,
    2,
    3,
    3,
    3,
    3,
    3,
    3,
    0,
};

static_assert(sizeof(ClassToHennaSlotCount) / sizeof(ClassToHennaSlotCount[0]) == 193);

FighterMagicType CT_to_FMT[192] = {
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_MAGIC,
    FMT_FIGHTER,
    FMT_MAGIC,
};

// Current L2Server.exe SHA-256
// 37393262fb59668744981e8d1f5bb91adcc1809d38661e9ccbd1c2641bda66ae,
// .data 0x015dc4c0, 192 little-endian qwords. The exact 1536-byte image
// hashes to 6a5b813144ef8da1f6b09848d8fb681fabaad2858a72c95c6f496cf21c4344a9.
std::int64_t CT_to_CBT[192] = {
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    1,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    2,
    4,
    4,
    4,
    4,
    4,
    4,
    4,
    8,
    8,
    8,
    8,
    8,
    8,
    16,
    16,
    16,
    16,
    16,
    16,
    16,
    32,
    32,
    32,
    32,
    32,
    32,
    64,
    64,
    64,
    64,
    64,
    128,
    128,
    128,
    128,
    256,
    256,
    256,
    256,
    256,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    1,
    1,
    1,
    1,
    1,
    1,
    2,
    2,
    2,
    2,
    2,
    4,
    4,
    4,
    4,
    8,
    8,
    8,
    16,
    16,
    16,
    16,
    32,
    32,
    32,
    64,
    64,
    128,
    128,
    256,
    256,
    0,
    0,
    0,
    0,
    512,
    512,
    512,
    512,
    512,
    512,
    512,
    512,
    512,
    512,
    512,
    512,
    512,
    512,
    0,
    0,
    16384,
    32768,
    65536,
    131072,
    262144,
    524288,
    1048576,
    2097152,
    2048,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    4096,
    8192,
    4096,
    8192,
    4096,
    8192,
    4096,
    8192,
    4194304,
    8388608,
};


const wchar_t* _PCParamFieldBySRC[37] = {
    L"PC_INT_DEFAULT",
    L"PC_INT_MIN",
    L"PC_INT_MAX",
    L"PC_STR_DEFAULT",
    L"PC_STR_MIN",
    L"PC_STR_MAX",
    L"PC_CON_DEFAULT",
    L"PC_CON_MIN",
    L"PC_CON_MAX",
    L"PC_MEN_DEFAULT",
    L"PC_MEN_MIN",
    L"PC_MEN_MAX",
    L"PC_DEX_DEFAULT",
    L"PC_DEX_MIN",
    L"PC_DEX_MAX",
    L"PC_WIT_DEFAULT",
    L"PC_WIT_MIN",
    L"PC_WIT_MAX",
    L"PC_BASE_PATTACK",
    L"PC_BASE_CRITICAL",
    L"PC_BASE_ATTACK_TYPE",
    L"PC_BASE_ATTACK_SPEED",
    L"PC_BASE_PDEFEND",
    L"PC_BASE_MATTACK",
    L"PC_BASE_MDEFEND",
    L"PC_BASE_CAN_PENETRATE",
    L"PC_BASE_ATTACK_RANGE",
    L"PC_BASE_DAMAGE_RANGE",
    L"PC_BASE_RAND_DAM",
    L"PC_ORG_HP_REGEN",
    L"PC_ORG_MP_REGEN",
    L"PC_ORG_CP_REGEN",
    L"PC_MOVING_SPEED",
    L"PC_ORG_JUMP",
    L"PC_BREATH_BONUS",
    L"PC_SAFE_FALL_HEIGHT",
    L"PC_COLLISION_BOX",
};

const wchar_t* _PCParamFieldByLevel[14] = {
    L"PC_LEVEL_BONUS",
    L"PC_INT_BONUS",
    L"PC_STR_BONUS",
    L"PC_DEX_BONUS",
    L"PC_WIT_BONUS",
    L"PC_CON_BONUS",
    L"PC_MEN_BONUS",
    L"PC_LUC_BONUS",
    L"PC_CHA_BONUS",
    L"PC_ORG_HP",
    L"PC_ORG_MP",
    L"PC_ORG_CP",
    L"PC_KARMA_INCREASE",
    L"PC_STANDARD_EXP_BY_LEV",
};

double g_OrgHPRegenTable[2][30][2][121];
double g_OrgMPRegenTable[2][30][2][121];
double g_OrgCPRegenTable[2][30][2][121];
double g_CollisionRadiusBySRC[2][30][2];
double g_CollisionHeightBySRC[2][30][2];
double g_OrgHPTable[192][122];
double g_OrgMPTable[192][122];
double g_OrgCPTable[192][122];
double g_OrgSpeedBySRCME[2][30][2][2][4];

CPCParamDB g_PCParamDB;

void CPCParamDB::Load(bool) {
    std::memset(m_BaseAttackTypeBySRC, 0, sizeof(m_BaseAttackTypeBySRC));
    std::memset(m_BaseCanPenetrateBySRC, 0, sizeof(m_BaseCanPenetrateBySRC));
    std::memset(m_BaseAttackRangeBySRC, 0, sizeof(m_BaseAttackRangeBySRC));
    std::memset(m_BaseRandomDamageBySRC, 0, sizeof(m_BaseRandomDamageBySRC));
    std::memset(m_BasePAttackBySRC, 0, sizeof(m_BasePAttackBySRC));
    std::memset(m_BasePDefendBySRC, 0, sizeof(m_BasePDefendBySRC));
    std::memset(m_BaseAttackSpeedBySRC, 0, sizeof(m_BaseAttackSpeedBySRC));
    std::memset(m_BaseCriticalProbBySRC, 0, sizeof(m_BaseCriticalProbBySRC));
    std::memset(m_BaseMAttackBySRC, 0, sizeof(m_BaseMAttackBySRC));
    std::memset(m_BaseMDefendBySRC, 0, sizeof(m_BaseMDefendBySRC));
    std::memset(m_BasePDefendBodyBySRC, 0, sizeof(m_BasePDefendBodyBySRC));
    std::memset(m_BaseMDefendBodyBySRC, 0, sizeof(m_BaseMDefendBodyBySRC));
    std::memset(g_OrgHPTable, 0, sizeof(g_OrgHPTable));
    std::memset(g_OrgMPTable, 0, sizeof(g_OrgMPTable));
    std::memset(g_OrgCPTable, 0, sizeof(g_OrgCPTable));
    std::memset(g_OrgSpeedBySRCME, 0, sizeof(g_OrgSpeedBySRCME));

    CParserForPCParam parser;
    if (!parser.Create()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] parser.create failed",
        //     L"pc_param.cpp",
        //     0x4c3)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] parser.create failed",
        //     L"pc_param.cpp",
        //     0x4c3)
        L2_LOG_ERROR(
            Log, "Cannot initialize the PC_parameter.txt parser or input; loading stops after the startup tables were reset."
        );
        return;
    }

    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing %s file failed after line[%d]",
        //     L"pc_param.cpp",
        //     0x4ce,
        //     L"..\\Script\\PC_parameter.txt",
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing %s file failed after line[%d]",
        //     L"pc_param.cpp",
        //     0x4ce,
        //     L"..\\Script\\PC_parameter.txt",
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "PC_parameter.txt parsing failed at script line %lld; values already assigned are not rolled back.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}

void CPCParamDB::SetValueBySRC(
    SexType sex, RaceType race, FighterMagicType fighterMagic, PCParamFieldBySRC field, int value
) {
    const int s = static_cast<int>(sex);
    const int r = static_cast<int>(race);
    const int f = static_cast<int>(fighterMagic);
    switch (field) {
        case PC_INT_DEFAULT:
        case PC_INT_MIN:
        case PC_INT_MAX:
        case PC_STR_DEFAULT:
        case PC_STR_MIN:
        case PC_STR_MAX:
        case PC_CON_DEFAULT:
        case PC_CON_MIN:
        case PC_CON_MAX:
        case PC_MEN_DEFAULT:
        case PC_MEN_MIN:
        case PC_MEN_MAX:
        case PC_DEX_DEFAULT:
        case PC_DEX_MIN:
        case PC_DEX_MAX:
        case PC_WIT_DEFAULT:
        case PC_WIT_MIN:
        case PC_WIT_MAX:
        case PC_ORG_JUMP:
            return;
        case PC_BASE_CRITICAL:
            m_BaseCriticalProbBySRC[s][r][f] = value;
            return;
        case PC_BASE_ATTACK_TYPE:
            m_BaseAttackTypeBySRC[s][r][f] = value;
            return;
        case PC_BASE_ATTACK_SPEED:
            m_BaseAttackSpeedBySRC[s][r][f] = value;
            return;
        case PC_BASE_PATTACK:
            m_BasePAttackBySRC[s][r][f] = value;
            return;
        case PC_BASE_MATTACK:
            m_BaseMAttackBySRC[s][r][f] = value;
            return;
        case PC_BASE_CAN_PENETRATE:
            m_BaseCanPenetrateBySRC[s][r][f] = value != 0;
            return;
        case PC_BASE_ATTACK_RANGE:
            m_BaseAttackRangeBySRC[s][r][f] = value;
            return;
        case PC_BASE_RAND_DAM:
            m_BaseRandomDamageBySRC[s][r][f] = value;
            return;
        case PC_BREATH_BONUS:
            m_BaseBreathBonusBySRC[s][r][f] = value;
            return;
        case PC_SAFE_FALL_HEIGHT:
            m_BaseSafeFallHeightBySRC[s][r][f] = value;
            return;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Unknown field[%s] setting",
            //     L"pc_param.cpp",
            //     0x554,
            //     _PCParamFieldBySRC[static_cast<int>(field)])
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Unknown field[%s] setting",
            //     L"pc_param.cpp",
            //     0x554,
            //     _PCParamFieldBySRC[static_cast<int>(field)])
            L2_LOG_ERROR(
                Log,
                "Cannot assign integer PC parameter field %d (%s), sex %d, race %d, fighter/mage type %d: this setter does "
                "not support the field; assignment is skipped.",
                static_cast<int>(field),
                WideToUtf8(
                    static_cast<unsigned>(field) < 37U ? _PCParamFieldBySRC[static_cast<unsigned>(field)] : L"<unknown>"
                )
                    .c_str(),
                static_cast<int>(sex), static_cast<int>(race), static_cast<int>(fighterMagic)
            );
            return;
    }
}

void CPCParamDB::SetValueBySRC(
    SexType sex,
    RaceType race,
    FighterMagicType fighterMagic,
    PCParamFieldBySRC field,
    int target,
    int direction,
    int distance,
    int angle
) {
    if (field == PC_BASE_DAMAGE_RANGE) {
        DamageRangeType& value =
            m_OrgBaseDamageRangeTable[static_cast<int>(sex)][static_cast<int>(race)][static_cast<int>(fighterMagic)];
        value.target = target;
        value.direction = direction;
        value.distance = distance;
        value.angle = angle;
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Unknown field[%s] setting",
    //     L"pc_param.cpp",
    //     0x56d,
    //     _PCParamFieldBySRC[static_cast<int>(field)])
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Unknown field[%s] setting",
    //     L"pc_param.cpp",
    //     0x56d,
    //     _PCParamFieldBySRC[static_cast<int>(field)])
    L2_LOG_ERROR(
        Log,
        "Cannot assign damage-range PC parameter field %d (%s), sex %d, race %d, fighter/mage type %d: this setter does not "
        "support the field; assignment is skipped.",
        static_cast<int>(field),
        WideToUtf8(static_cast<unsigned>(field) < 37U ? _PCParamFieldBySRC[static_cast<unsigned>(field)] : L"<unknown>")
            .c_str(),
        static_cast<int>(sex), static_cast<int>(race), static_cast<int>(fighterMagic)
    );
}

void CPCParamDB::SetValueBySRC(
    SexType sex,
    RaceType race,
    FighterMagicType fighterMagic,
    PCParamFieldBySRC field,
    int slowGround,
    int fastGround,
    int slowWater,
    int fastWater,
    int slowAir,
    int fastAir,
    int slowZeroG,
    int fastZeroG
) {
    if (field != PC_MOVING_SPEED) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Unknown field[%s] setting",
        //     L"pc_param.cpp",
        //     0x5e9,
        //     _PCParamFieldBySRC[static_cast<int>(field)])
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Unknown field[%s] setting",
        //     L"pc_param.cpp",
        //     0x5e9,
        //     _PCParamFieldBySRC[static_cast<int>(field)])
        L2_LOG_ERROR(
            Log,
            "Cannot assign movement-speed PC parameter field %d (%s), sex %d, race %d, fighter/mage type %d: this setter "
            "does not support the field; assignment is skipped.",
            static_cast<int>(field),
            WideToUtf8(static_cast<unsigned>(field) < 37U ? _PCParamFieldBySRC[static_cast<unsigned>(field)] : L"<unknown>")
                .c_str(),
            static_cast<int>(sex), static_cast<int>(race), static_cast<int>(fighterMagic)
        );
        return;
    }
    double (&speed)[2][4] = g_OrgSpeedBySRCME[static_cast<int>(sex)][static_cast<int>(race)][static_cast<int>(fighterMagic)];
    speed[0][0] = slowGround;
    speed[1][0] = fastGround;
    speed[0][1] = slowWater;
    speed[1][1] = fastWater;
    speed[0][2] = slowAir;
    speed[1][2] = fastAir;
    speed[0][3] = slowZeroG;
    speed[1][3] = fastZeroG;
}

void CPCParamDB::SetValueBySRC(
    SexType sex, RaceType race, FighterMagicType fighterMagic, PCParamFieldBySRC field, double radius, double height
) {
    if (field == PC_COLLISION_BOX) {
        g_CollisionRadiusBySRC[static_cast<int>(sex)][static_cast<int>(race)][static_cast<int>(fighterMagic)] = radius;
        g_CollisionHeightBySRC[static_cast<int>(sex)][static_cast<int>(race)][static_cast<int>(fighterMagic)] = height;
        return;
    }
    if (static_cast<std::uint32_t>(field) < 37U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Unknown field[%s] setting",
        //     L"pc_param.cpp",
        //     0x612,
        //     _PCParamFieldBySRC[static_cast<int>(field)])
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Unknown field[%s] setting",
        //     L"pc_param.cpp",
        //     0x612,
        //     _PCParamFieldBySRC[static_cast<int>(field)])
        L2_LOG_ERROR(
            Log,
            "Cannot assign collision-box PC parameter field %d (%s), sex %d, race %d, fighter/mage type %d: this setter "
            "does not support the field; assignment is skipped.",
            static_cast<int>(field),
            WideToUtf8(static_cast<unsigned>(field) < 37U ? _PCParamFieldBySRC[static_cast<unsigned>(field)] : L"<unknown>")
                .c_str(),
            static_cast<int>(sex), static_cast<int>(race), static_cast<int>(fighterMagic)
        );
    }
}

void CPCParamDB::SetValueBySRC(
    SexType sex,
    RaceType race,
    FighterMagicType fighterMagic,
    PCParamFieldBySRC field,
    double value0,
    double value1,
    double value2,
    double value3,
    double value4,
    double value5,
    double value6,
    double
) {
    const int s = static_cast<int>(sex);
    const int r = static_cast<int>(race);
    const int f = static_cast<int>(fighterMagic);
    if (field == PC_BASE_PDEFEND) {
        double* const body = m_BasePDefendBodyBySRC[s][r][f];
        body[0] = value0;
        body[1] = value1;
        body[2] = value2;
        body[3] = value3;
        body[4] = value4;
        body[5] = value5;
        body[6] = value6;
        body[PCBODY::PDEFEND_SUM] = value0 + value1 + value2 + value3 + value4 + value5 + value6;
        return;
    }
    if (field == PC_BASE_MDEFEND) {
        double* const body = m_BaseMDefendBodyBySRC[s][r][f];
        body[PCBODY::R_EAR] = value0;
        body[PCBODY::L_EAR] = value1;
        body[PCBODY::R_RING] = value2;
        body[PCBODY::L_RING] = value3;
        body[PCBODY::NECKLACE] = value4;
        body[PCBODY::MDEFEND_SUM] = value0 + value1 + value2 + value3 + value4;
        return;
    }
    // Current 0x00950c0c silently accepts the two regen fields here.
    if (field == PC_ORG_HP_REGEN || field == PC_ORG_MP_REGEN) {
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Unknown field[%s] setting",
    //     L"pc_param.cpp",
    //     0x5d2,
    //     _PCParamFieldBySRC[static_cast<int>(field)])
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Unknown field[%s] setting",
    //     L"pc_param.cpp",
    //     0x5d2,
    //     _PCParamFieldBySRC[static_cast<int>(field)])
    L2_LOG_ERROR(
        Log,
        "Cannot assign defence PC parameter field %d (%s), sex %d, race %d, fighter/mage type %d: this setter does not "
        "support the field; assignment is skipped.",
        static_cast<int>(field),
        WideToUtf8(static_cast<unsigned>(field) < 37U ? _PCParamFieldBySRC[static_cast<unsigned>(field)] : L"<unknown>")
            .c_str(),
        static_cast<int>(sex), static_cast<int>(race), static_cast<int>(fighterMagic)
    );
}

void CPCParamDB::SetValueBySRC(
    SexType sex, RaceType race, FighterMagicType fighterMagic, PCParamFieldBySRC field, PCParamDoubleVector* values
) {
    if (values == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Invalid parameter. pDoubleVector=NULL",
        //     "pc_param.cpp",
        //     0x591)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] Invalid parameter. pDoubleVector=NULL",
        //     "pc_param.cpp",
        //     0x591)
        L2_LOG_ERROR(
            Log,
            "Cannot assign PC regeneration field %d (%s), sex %d, race %d, fighter/mage type %d: the value list is null; "
            "assignment is skipped.",
            static_cast<int>(field),
            WideToUtf8(static_cast<unsigned>(field) < 37U ? _PCParamFieldBySRC[static_cast<unsigned>(field)] : L"<unknown>")
                .c_str(),
            static_cast<int>(sex), static_cast<int>(race), static_cast<int>(fighterMagic)
        );
        return;
    }
    if (values->size() > 120U) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[%s][%d] Invalid parameter. pDoubleVector size() > %d",
        //     "pc_param.cpp",
        //     0x597,
        //     120)
        // L2_LOG_ERROR(Log,
        //     "[%s][%d] Invalid parameter. pDoubleVector size() > %d",
        //     "pc_param.cpp",
        //     0x597,
        //     120)
        L2_LOG_ERROR(
            Log,
            "Cannot assign PC regeneration field %d (%s): %zu values exceed the setter limit of 120; assignment is skipped.",
            static_cast<int>(field),
            WideToUtf8(static_cast<unsigned>(field) < 37U ? _PCParamFieldBySRC[static_cast<unsigned>(field)] : L"<unknown>")
                .c_str(),
            static_cast<std::size_t>(values->size())
        );
        return;
    }
    double* destination = nullptr;
    switch (field) {
        case PC_ORG_HP_REGEN:
            destination = g_OrgHPRegenTable[static_cast<int>(sex)][static_cast<int>(race)][static_cast<int>(fighterMagic)];
            break;
        case PC_ORG_MP_REGEN:
            destination = g_OrgMPRegenTable[static_cast<int>(sex)][static_cast<int>(race)][static_cast<int>(fighterMagic)];
            break;
        case PC_ORG_CP_REGEN:
            destination = g_OrgCPRegenTable[static_cast<int>(sex)][static_cast<int>(race)][static_cast<int>(fighterMagic)];
            break;
        default:
            return;
    }
    std::copy(values->begin(), values->end(), destination);
}

void CPCParamDB::SetValueByLevel(int level, PCParamFieldByLevel field, int value) {
    if (static_cast<std::uint32_t>(level) >= 201U) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Invalid level on field[%s] setting",
        //     L"pc_param.cpp",
        //     0x6f3,
        //     level,
        //     _PCParamFieldByLevel[static_cast<int>(field)])
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Invalid level on field[%s] setting",
        //     L"pc_param.cpp",
        //     0x6f3,
        //     level,
        //     _PCParamFieldByLevel[static_cast<int>(field)])
        L2_LOG_ERROR(
            Log,
            "Cannot assign PC parameter field %d (%s) at level %d: expected level 0 through 200; assignment is skipped.",
            static_cast<int>(field),
            WideToUtf8(
                static_cast<unsigned>(field) < 14U ? _PCParamFieldByLevel[static_cast<unsigned>(field)] : L"<unknown>"
            )
                .c_str(),
            level
        );
        return;
    }
    double* destination = nullptr;
    switch (field) {
        case PC_INT_BONUS:
            destination = m_dIntBonus;
            break;
        case PC_STR_BONUS:
            destination = m_dStrBonus;
            break;
        case PC_DEX_BONUS:
            destination = m_dDexBonus;
            break;
        case PC_WIT_BONUS:
            destination = m_dWitBonus;
            break;
        case PC_CON_BONUS:
            destination = m_dConBonus;
            break;
        case PC_MEN_BONUS:
            destination = m_dMenBonus;
            break;
        case PC_LUC_BONUS:
            destination = m_dLucBonus;
            break;
        case PC_CHA_BONUS:
            destination = m_dChaBonus;
            break;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Unknown field[%s] setting",
            //     L"pc_param.cpp",
            //     0x720,
            //     _PCParamFieldByLevel[static_cast<int>(field)])
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Unknown field[%s] setting",
            //     L"pc_param.cpp",
            //     0x720,
            //     _PCParamFieldByLevel[static_cast<int>(field)])
            L2_LOG_ERROR(
                Log,
                "Cannot assign integer level-bonus PC parameter field %d (%s) at level %d: this setter does not support the "
                "field; assignment is skipped.",
                static_cast<int>(field),
                WideToUtf8(
                    static_cast<unsigned>(field) < 14U ? _PCParamFieldByLevel[static_cast<unsigned>(field)] : L"<unknown>"
                )
                    .c_str(),
                level
            );
            return;
    }
    destination[level] = (static_cast<double>(value) + 1000.0) / 1000.0;
}

void CPCParamDB::SetValueByLevel(int level, PCParamFieldByLevel field, double value) {
    if (field == PC_LEVEL_BONUS) {
        m_dLevelBonus[level] = value;
        return;
    }
    if (field == PC_KARMA_INCREASE) {
        m_KarmaIncreaseTable[level] = value;
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Unknown field[%s] setting",
    //     L"pc_param.cpp",
    //     0x736,
    //     _PCParamFieldByLevel[static_cast<int>(field)])
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Unknown field[%s] setting",
    //     L"pc_param.cpp",
    //     0x736,
    //     _PCParamFieldByLevel[static_cast<int>(field)])
    L2_LOG_ERROR(
        Log,
        "Cannot assign floating-point level PC parameter field %d (%s) at level %d: this setter does not support the field; "
        "assignment is skipped.",
        static_cast<int>(field),
        WideToUtf8(static_cast<unsigned>(field) < 14U ? _PCParamFieldByLevel[static_cast<unsigned>(field)] : L"<unknown>")
            .c_str(),
        level
    );
}

void CPCParamDB::SetValueByLevelClass(int level, ClassType classType, PCParamFieldByLevel field, double value) {
    switch (field) {
        case PC_ORG_HP:
            g_OrgHPTable[static_cast<int>(classType)][level] = value;
            return;
        case PC_ORG_MP:
            g_OrgMPTable[static_cast<int>(classType)][level] = value;
            return;
        case PC_ORG_CP:
            g_OrgCPTable[static_cast<int>(classType)][level] = value;
            return;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Unknown field[%s] setting",
            //     L"pc_param.cpp",
            //     0x750,
            //     _PCParamFieldByLevel[static_cast<int>(field)])
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] Unknown field[%s] setting",
            //     L"pc_param.cpp",
            //     0x750,
            //     _PCParamFieldByLevel[static_cast<int>(field)])
            L2_LOG_ERROR(
                Log,
                "Cannot assign class-level PC parameter field %d (%s) at level %d: this setter does not support the field; "
                "assignment is skipped.",
                static_cast<int>(field),
                WideToUtf8(
                    static_cast<unsigned>(field) < 14U ? _PCParamFieldByLevel[static_cast<unsigned>(field)] : L"<unknown>"
                )
                    .c_str(),
                level
            );
            return;
    }
}

void CPCParamDB::SetStandardExpByLevel(int level, PCParamFieldByLevel field, std::int64_t value) {
    if (field == PC_STANDARD_EXP_BY_LEV) {
        m_nStandardExpByLevel[level] = value;
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Unknown field[%s] setting",
        //     L"pc_param.cpp",
        //     0x79c,
        //     _PCParamFieldByLevel[static_cast<int>(field)])
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Unknown field[%s] setting",
        //     L"pc_param.cpp",
        //     0x79c,
        //     _PCParamFieldByLevel[static_cast<int>(field)])
        L2_LOG_ERROR(
            Log,
            "Cannot assign standard-experience PC parameter field %d (%s) at level %d: this setter does not support the "
            "field; assignment is skipped.",
            static_cast<int>(field),
            WideToUtf8(
                static_cast<unsigned>(field) < 14U ? _PCParamFieldByLevel[static_cast<unsigned>(field)] : L"<unknown>"
            )
                .c_str(),
            level
        );
    }
}

double CPCParamDB::GetCollisionRadius(SexType sexType, ClassType classType, RaceType raceType) {
    const std::uint32_t sex = static_cast<std::uint32_t>(sexType);
    const std::uint32_t classIndex = static_cast<std::uint32_t>(classType);
    const std::uint32_t race = static_cast<std::uint32_t>(raceType);
    if (sex < 2U && classIndex < 193U && race < 30U) {
        return g_CollisionRadiusBySRC[sex][race][CT_to_FMT[static_cast<int>(classType)]];
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Some parameter is invalid sex[%d] race [%d] clstype[%d]",
    //     L"pc_param.cpp",
    //     0x622,
    //     static_cast<int>(sexType),
    //     static_cast<int>(raceType),
    //     static_cast<int>(classType))
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Some parameter is invalid sex[%d] race [%d] clstype[%d]",
    //     L"pc_param.cpp",
    //     0x622,
    //     static_cast<int>(sexType),
    //     static_cast<int>(raceType),
    //     static_cast<int>(classType))
    L2_LOG_ERROR(
        Log,
        "Cannot read PC collision radius for sex %d, race %d, class %d: expected sex 0..1, race 0..29 and class 0..192; "
        "returning 0.",
        static_cast<int>(sexType), static_cast<int>(raceType), static_cast<int>(classType)
    );
    return 0.0;
}

double CPCParamDB::GetCollisionHeight(SexType sexType, ClassType classType, RaceType raceType) {
    const std::uint32_t sex = static_cast<std::uint32_t>(sexType);
    const std::uint32_t classIndex = static_cast<std::uint32_t>(classType);
    const std::uint32_t race = static_cast<std::uint32_t>(raceType);
    if (sex < 2U && classIndex < 193U && race < 30U) {
        return g_CollisionHeightBySRC[sex][race][CT_to_FMT[static_cast<int>(classType)]];
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Some parameter is invalid sex[%d] race[%d] clstype[%d]",
    //     L"pc_param.cpp",
    //     0x632,
    //     static_cast<int>(sexType),
    //     static_cast<int>(raceType),
    //     static_cast<int>(classType))
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Some parameter is invalid sex[%d] race[%d] clstype[%d]",
    //     L"pc_param.cpp",
    //     0x632,
    //     static_cast<int>(sexType),
    //     static_cast<int>(raceType),
    //     static_cast<int>(classType))
    L2_LOG_ERROR(
        Log,
        "Cannot read PC collision height for sex %d, race %d, class %d: expected sex 0..1, race 0..29 and class 0..192; "
        "returning 0.",
        static_cast<int>(sexType), static_cast<int>(raceType), static_cast<int>(classType)
    );
    return 0.0;
}

double CPCParamDB::GetOrgHP(ClassType classType, int level) {
    if (level > 121) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] nLevel[%d] is bigger than MAX_LEVEL_RESERVED[%d]",
        //     L"CPCParamDB::GetOrgHP",
        //     0x7aa,
        //     level,
        //     0x78)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] nLevel[%d] is bigger than MAX_LEVEL_RESERVED[%d]",
        //     L"CPCParamDB::GetOrgHP",
        //     0x7aa,
        //     level,
        //     0x78)
        L2_LOG_ERROR(
            Log,
            "PC base HP requested for class %d at level %d, above the reserved maximum 121; the original table lookup "
            "continues without clamping.",
            static_cast<int>(classType), level
        );
    }
    return g_OrgHPTable[static_cast<int>(classType)][level];
}

double CPCParamDB::GetOrgMP(ClassType classType, int level) {
    if (level > 121) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] nLevel[%d] is bigger than MAX_LEVEL_RESERVED[%d]",
        //     L"CPCParamDB::GetOrgMP",
        //     0x7b4,
        //     level,
        //     0x78)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] nLevel[%d] is bigger than MAX_LEVEL_RESERVED[%d]",
        //     L"CPCParamDB::GetOrgMP",
        //     0x7b4,
        //     level,
        //     0x78)
        L2_LOG_ERROR(
            Log,
            "PC base MP requested for class %d at level %d, above the reserved maximum 121; the original table lookup "
            "continues without clamping.",
            static_cast<int>(classType), level
        );
    }
    return g_OrgMPTable[static_cast<int>(classType)][level];
}

double CPCParamDB::GetOrgCP(ClassType classType, int level) {
    if (level > 121) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] nLevel[%d] is bigger than MAX_LEVEL_RESERVED[%d]",
        //     L"CPCParamDB::GetOrgCP",
        //     0x7be,
        //     level,
        //     0x78)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] nLevel[%d] is bigger than MAX_LEVEL_RESERVED[%d]",
        //     L"CPCParamDB::GetOrgCP",
        //     0x7be,
        //     level,
        //     0x78)
        L2_LOG_ERROR(
            Log,
            "PC base CP requested for class %d at level %d, above the reserved maximum 121; the original table lookup "
            "continues without clamping.",
            static_cast<int>(classType), level
        );
    }
    return g_OrgCPTable[static_cast<int>(classType)][level];
}

double CPCParamDB::GetOrgHPRegen(SexType sexType, RaceType raceType, FighterMagicType fighterMagicType, int level) {
    if (level > 120) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] nLevel[%d] is bigger than MAX_LEVEL[%d]",
        //     L"CPCParamDB::GetOrgHPRegen",
        //     0x7cd,
        //     level,
        //     0x78)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] nLevel[%d] is bigger than MAX_LEVEL[%d]",
        //     L"CPCParamDB::GetOrgHPRegen",
        //     0x7cd,
        //     level,
        //     0x78)
        L2_LOG_ERROR(
            Log,
            "PC HP regeneration requested at level %d, above maximum 120 (sex %d, race %d, fighter/mage type %d); the "
            "original table lookup continues without clamping.",
            level, static_cast<int>(sexType), static_cast<int>(raceType), static_cast<int>(fighterMagicType)
        );
    }
    return g_OrgHPRegenTable[sexType][raceType][fighterMagicType][level - 1];
}

double CPCParamDB::GetOrgCPRegen(SexType sexType, RaceType raceType, FighterMagicType fighterMagicType, int level) {
    if (level > 120) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] nLevel[%d] is bigger than MAX_LEVEL[%d]",
        //     L"CPCParamDB::GetOrgCPRegen",
        //     0x7de,
        //     level,
        //     0x78)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] nLevel[%d] is bigger than MAX_LEVEL[%d]",
        //     L"CPCParamDB::GetOrgCPRegen",
        //     0x7de,
        //     level,
        //     0x78)
        L2_LOG_ERROR(
            Log,
            "PC CP regeneration requested at level %d, above maximum 120 (sex %d, race %d, fighter/mage type %d); the "
            "original table lookup continues without clamping.",
            level, static_cast<int>(sexType), static_cast<int>(raceType), static_cast<int>(fighterMagicType)
        );
    }
    return g_OrgCPRegenTable[sexType][raceType][fighterMagicType][level - 1];
}

double CPCParamDB::GetOrgMPRegen(SexType sexType, RaceType raceType, FighterMagicType fighterMagicType, int level) {
    if (level > 120) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] nLevel[%d] is bigger than MAX_LEVEL[%d]",
        //     L"CPCParamDB::GetOrgMPRegen",
        //     0x815,
        //     level,
        //     0x78)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] nLevel[%d] is bigger than MAX_LEVEL[%d]",
        //     L"CPCParamDB::GetOrgMPRegen",
        //     0x815,
        //     level,
        //     0x78)
        L2_LOG_ERROR(
            Log,
            "PC MP regeneration requested at level %d, above maximum 120 (sex %d, race %d, fighter/mage type %d); the "
            "original table lookup continues without clamping.",
            level, static_cast<int>(sexType), static_cast<int>(raceType), static_cast<int>(fighterMagicType)
        );
    }
    return g_OrgMPRegenTable[sexType][raceType][fighterMagicType][level - 1];
}

RaceType CPCParamDB::GetRaceByClass(ClassType classType, RaceType raceType) {
    L2_GUARD_FUNCTION();
    const int index = static_cast<int>(classType);
    if (ClassToGroup[index] != ClassGroupType::FOURTH_CHANGED && ClassToGroup[index] != ClassGroupType::FIFTH_CHANGED) {
        raceType = CT_to_RT[index];
    }
    return raceType;
}

ClassType CPCParamDB::GetBasicClassByRaceAndFTM(RaceType raceType, bool fighter, bool male) {
    L2_GUARD_FUNCTION();
    switch (raceType) {
        case RT_HUMAN:
            return fighter ? CT_FIGHTER : CT_MAGE;
        case RT_ELF:
            return fighter ? CT_ELVEN_FIGHTER : CT_ELVEN_MAGE;
        case RT_DARKELF:
            return fighter ? CT_DARK_FIGHTER : CT_DARK_MAGE;
        case RT_ORC:
            return fighter ? CT_ORC_FIGHTER : CT_ORC_MAGE;
        case RT_DWARF:
            return CT_DWARVEN_FIGHTER;
        case RT_KAMAEL:
            return male ? CT_KAMAEL_M_SOLDIER : CT_KAMAEL_F_SOLDIER;
        case RT_ERTHEIA:
            return fighter ? CT_ERTHEIA_FIGHTER : CT_ERTHEIA_WIZARD;
        default:
            // Log.Add(
            //     LOG_ERROR,
            //     L"abnormal race %d",
            //     static_cast<int>(raceType))
            // L2_LOG_ERROR(Log,
            //     L"abnormal race %d",
            //     static_cast<int>(raceType))
            L2_LOG_ERROR(
                Log, "Cannot select a basic class for race %d; using the Fighter class.", static_cast<int>(raceType)
            );
            return CT_FIGHTER;
    }
}

int CPCParamDB::GetInventoryLimitToChangeSubjob(ClassType classType, RaceType raceType) {
    L2_GUARD_FUNCTION();
    const int index = static_cast<int>(classType);
    const ClassGroupType::Enum group = ClassToGroup[index];
    if (group == ClassGroupType::FOURTH_CHANGED || group == ClassGroupType::FIFTH_CHANGED) {
        const RaceType classRace = GetRaceByClass(classType, raceType);
        return classRace == RT_DWARF ? 115 : 95;
    }
    const RaceType classRace = GetRaceByClass(classType, raceType);
    return classRace == RT_DWARF ? 100 : 80;
}

int CPCParamDB::GetOriMaxPrivateBankCount(const ClassType classType, const RaceType raceType) {
    L2_GUARD_FUNCTION();
    return GetRaceByClass(classType, raceType) == RT_DWARF ? 120 : 100;
}

int CPCParamDB::GetOriMaxInventoryCount(const ClassType classType, const RaceType raceType) {
    L2_GUARD_FUNCTION();
    return GetRaceByClass(classType, raceType) == RT_DWARF ? 100 : 80;
}

int CPCParamDB::GetOriMaxPrivateStoreCount(const ClassType classType, const RaceType raceType) {
    L2_GUARD_FUNCTION();
    return GetRaceByClass(classType, raceType) == RT_DWARF ? 4 : 3;
}

int CPCParamDB::GetOriMaxPrivateStoreBuyCount(const ClassType classType, const RaceType raceType) {
    L2_GUARD_FUNCTION();
    return GetRaceByClass(classType, raceType) == RT_DWARF ? 5 : 4;
}

::DamageRangeType CPCParamDB::GetBaseDamageRangeValue(SexType sexType, ClassType classType, RaceType raceType) {
    const std::uint32_t sex = static_cast<std::uint32_t>(sexType);
    const std::uint32_t classIndex = static_cast<std::uint32_t>(classType);
    if (sex < 2U && classIndex < 193U) {
        raceType = GetRaceByClass(classType, raceType);
        const FighterMagicType fighterMagicType = CT_to_FMT[static_cast<int>(classType)];
        return m_OrgBaseDamageRangeTable[sex][raceType][fighterMagicType];
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Some parameter is invalid sex[%d] clstype[%d]",
    //     L"pc_param.cpp",
    //     0x57d,
    //     static_cast<int>(sexType),
    //     static_cast<int>(classType))
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Some parameter is invalid sex[%d] clstype[%d]",
    //     L"pc_param.cpp",
    //     0x57d,
    //     static_cast<int>(sexType),
    //     static_cast<int>(classType))
    L2_LOG_ERROR(
        Log,
        "Cannot read PC damage range for sex %d and class %d: expected sex 0..1 and class 0..192; using the table entry "
        "[0][0][0].",
        static_cast<int>(sexType), static_cast<int>(classType)
    );
    return m_OrgBaseDamageRangeTable[0][0][0];
}

double CPCParamDB::GetOrgSpeed(
    SexType sexType, ClassType classType, ::MoveType moveType, ::EnvType environmentType, RaceType raceType
) {
    const std::uint32_t sex = static_cast<std::uint32_t>(sexType);
    const std::uint32_t classIndex = static_cast<std::uint32_t>(classType);
    const std::uint32_t move = static_cast<std::uint32_t>(moveType);
    const std::uint32_t environment = static_cast<std::uint32_t>(environmentType);
    if (sex < 2U && classIndex < 193U && move < 2U && environment < 4U) {
        raceType = GetRaceByClass(classType, raceType);
        const FighterMagicType fighterMagicType = CT_to_FMT[static_cast<int>(classType)];
        return g_OrgSpeedBySRCME[sex][raceType][fighterMagicType][move][environment];
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Some parameter is invalid sex[%d] clstype[%d] movetype[%d] envtype[%d]",
    //     L"pc_param.cpp",
    //     0x5f8,
    //     static_cast<int>(sexType),
    //     static_cast<int>(classType),
    //     static_cast<int>(moveType),
    //     static_cast<int>(environmentType))
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Some parameter is invalid sex[%d] clstype[%d] movetype[%d] envtype[%d]",
    //     L"pc_param.cpp",
    //     0x5f8,
    //     static_cast<int>(sexType),
    //     static_cast<int>(classType),
    //     static_cast<int>(moveType),
    //     static_cast<int>(environmentType))
    L2_LOG_ERROR(
        Log,
        "Cannot read PC movement speed for sex %d, class %d, move type %d and environment %d: expected ranges 0..1, 0..192, "
        "0..1 and 0..3; returning 0.",
        static_cast<int>(sexType), static_cast<int>(classType), static_cast<int>(moveType), static_cast<int>(environmentType)
    );
    return 0.0;
}

int CPCParamDB::GetIntValueByField(SexType sexType, ClassType classType, PCParamFieldBySRC field, RaceType raceType) {
    const std::uint32_t sex = static_cast<std::uint32_t>(sexType);
    const std::uint32_t classIndex = static_cast<std::uint32_t>(classType);
    if (sex < 2U && classIndex < 193U) {
        raceType = GetRaceByClass(classType, raceType);
        const FighterMagicType fighterMagicType = CT_to_FMT[static_cast<int>(classType)];
        switch (field) {
            case PC_BASE_CRITICAL:
                return m_BaseCriticalProbBySRC[sex][raceType][fighterMagicType];
            case PC_BASE_ATTACK_TYPE:
                return m_BaseAttackTypeBySRC[sex][raceType][fighterMagicType];
            case PC_BASE_ATTACK_SPEED:
                return m_BaseAttackSpeedBySRC[sex][raceType][fighterMagicType];
            case PC_BASE_PATTACK:
                return m_BasePAttackBySRC[sex][raceType][fighterMagicType];
            case PC_BASE_MATTACK:
                return m_BaseMAttackBySRC[sex][raceType][fighterMagicType];
            case PC_BASE_CAN_PENETRATE:
                return m_BaseCanPenetrateBySRC[sex][raceType][fighterMagicType];
            case PC_BASE_ATTACK_RANGE:
                return m_BaseAttackRangeBySRC[sex][raceType][fighterMagicType];
            case PC_BASE_RAND_DAM:
                return m_BaseRandomDamageBySRC[sex][raceType][fighterMagicType];
            case PC_BREATH_BONUS:
                return m_BaseBreathBonusBySRC[sex][raceType][fighterMagicType];
            case PC_SAFE_FALL_HEIGHT:
                return m_BaseSafeFallHeightBySRC[sex][raceType][fighterMagicType];
            default:
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] Unknown field[%s] GetIntValueByField",
                //     L"pc_param.cpp",
                //     0x6d6,
                //     _PCParamFieldBySRC[static_cast<int>(field)])
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] Unknown field[%s] GetIntValueByField",
                //     L"pc_param.cpp",
                //     0x6d6,
                //     _PCParamFieldBySRC[static_cast<int>(field)])
                L2_LOG_ERROR(
                    Log,
                    "Cannot read integer PC parameter field %d (%s): this getter does not support the field; returning 0.",
                    static_cast<int>(field),
                    WideToUtf8(
                        static_cast<unsigned>(field) < 37U ? _PCParamFieldBySRC[static_cast<unsigned>(field)] : L"<unknown>"
                    )
                        .c_str()
                );
                return 0;
        }
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Some parameter is invalid sex[%d] clstype[%d]",
    //     L"pc_param.cpp",
    //     0x651,
    //     static_cast<int>(sexType),
    //     static_cast<int>(classType))
    // L2_LOG_ERROR(Log,
    //     L"[%s][%d] Some parameter is invalid sex[%d] clstype[%d]",
    //     L"pc_param.cpp",
    //     0x651,
    //     static_cast<int>(sexType),
    //     static_cast<int>(classType))
    L2_LOG_ERROR(
        Log,
        "Cannot read integer PC parameter field %d for sex %d and class %d: expected sex 0..1 and class 0..192; returning "
        "0.",
        static_cast<int>(field), static_cast<int>(sexType), static_cast<int>(classType)
    );
    return 0;
}

double CPCParamDB::GetBodyPart(
    SexType sexType, ClassType classType, PCBODY::Enum bodyPart, VALIDATE::Enum validateType, RaceType raceType
) {
    L2_GUARD_FUNCTION();
    const std::uint32_t sex = static_cast<std::uint32_t>(sexType);
    const std::uint32_t classIndex = static_cast<std::uint32_t>(classType);
    if (sex < 2U && classIndex < 193U) {
        raceType = GetRaceByClass(classType, raceType);
        if (validateType == VALIDATE::PDEFEND) {
            const FighterMagicType fighterMagicType = CT_to_FMT[static_cast<int>(classType)];
            return m_BasePDefendBodyBySRC[sex][raceType][fighterMagicType][bodyPart];
        }
        if (validateType == VALIDATE::MDEFEND) {
            const FighterMagicType fighterMagicType = CT_to_FMT[static_cast<int>(classType)];
            return m_BaseMDefendBodyBySRC[sex][raceType][fighterMagicType][bodyPart];
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"error. class type is invalid.")
        // L2_LOG_ERROR(Log,
        //     L"error. class type is invalid.")
        L2_LOG_ERROR(
            Log,
            "Cannot read PC body-part defence for sex %d and class %d: expected sex 0..1 and class 0..192; returning 0.",
            static_cast<int>(sexType), static_cast<int>(classType)
        );
    }
    return 0.0;
}

void CPCParamDB::SetRegenMoveModeBonus(MoveModeEnum moveMode, double value) {
    L2_GUARD_FUNCTION();
    m_BaseRegenMoveModeBonus[moveMode] = value;
}

void CPCParamDB::SetRegenMoveModeHPBonus(
    MoveModeEnum moveMode, double value0, double value1, double value2, double value3, double value4, double value5
) {
    L2_GUARD_FUNCTION();
    m_BaseHPRegenMoveModeBonus[moveMode][2] = value2;
    m_BaseHPRegenMoveModeBonus[moveMode][3] = value3;
    m_BaseHPRegenMoveModeBonus[moveMode][0] = value0;
    m_BaseHPRegenMoveModeBonus[moveMode][4] = value4;
    m_BaseHPRegenMoveModeBonus[moveMode][1] = value1;
    m_BaseHPRegenMoveModeBonus[moveMode][5] = value5;
}

void CPCParamDB::SetRegenMoveModeMPBonus(
    MoveModeEnum moveMode, double value0, double value1, double value2, double value3, double value4, double value5
) {
    L2_GUARD_FUNCTION();
    m_BaseMPRegenMoveModeBonus[moveMode][2] = value2;
    m_BaseMPRegenMoveModeBonus[moveMode][3] = value3;
    m_BaseMPRegenMoveModeBonus[moveMode][0] = value0;
    m_BaseMPRegenMoveModeBonus[moveMode][4] = value4;
    m_BaseMPRegenMoveModeBonus[moveMode][1] = value1;
    m_BaseMPRegenMoveModeBonus[moveMode][5] = value5;
}

void CPCParamDB::SetRegenMoveModeCPBonus(
    MoveModeEnum moveMode, double value0, double value1, double value2, double value3, double value4, double value5
) {
    L2_GUARD_FUNCTION();
    m_BaseCPRegenMoveModeBonus[moveMode][2] = value2;
    m_BaseCPRegenMoveModeBonus[moveMode][3] = value3;
    m_BaseCPRegenMoveModeBonus[moveMode][0] = value0;
    m_BaseCPRegenMoveModeBonus[moveMode][4] = value4;
    m_BaseCPRegenMoveModeBonus[moveMode][1] = value1;
    m_BaseCPRegenMoveModeBonus[moveMode][5] = value5;
}

double CPCParamDB::GetParamBonus(PCParamFieldByLevel field, int value) {
    switch (field) {
        case PC_LEVEL_BONUS:
            return m_dLevelBonus[value];
        case PC_INT_BONUS:
            return m_dIntBonus[value];
        case PC_STR_BONUS:
            return m_dStrBonus[value];
        case PC_DEX_BONUS:
            return m_dDexBonus[value];
        case PC_WIT_BONUS:
            return m_dWitBonus[value];
        case PC_CON_BONUS:
            return m_dConBonus[value];
        case PC_MEN_BONUS:
            return m_dMenBonus[value];
        case PC_LUC_BONUS:
            return m_dLucBonus[value];
        case PC_CHA_BONUS:
            return m_dChaBonus[value];
        default:
            return 1.0;
    }
}

double CPCParamDB::GetRegenMoveModeBonus(MoveModeEnum moveMode) {
    L2_GUARD_FUNCTION();
    return m_BaseRegenMoveModeBonus[moveMode];
}

double CPCParamDB::GetHPRegenMoveModeBonus(MoveModeEnum moveMode, int level) {
    L2_GUARD_FUNCTION();
    const double levelValue = static_cast<double>(level);
    if (levelValue < m_BaseHPRegenMoveModeBonus[moveMode][0] || m_BaseHPRegenMoveModeBonus[moveMode][1] <= levelValue) {
        return m_BaseHPRegenMoveModeBonus[moveMode][2];
    }
    const double delta = levelValue - m_BaseHPRegenMoveModeBonus[moveMode][4];
    return m_BaseHPRegenMoveModeBonus[moveMode][3] * m_BaseHPRegenMoveModeBonus[moveMode][2] * delta * delta +
        m_BaseHPRegenMoveModeBonus[moveMode][5];
}

double CPCParamDB::GetMPRegenMoveModeBonus(MoveModeEnum moveMode, int level) {
    L2_GUARD_FUNCTION();
    const double levelValue = static_cast<double>(level);
    if (levelValue < m_BaseMPRegenMoveModeBonus[moveMode][0] || m_BaseMPRegenMoveModeBonus[moveMode][1] <= levelValue) {
        return m_BaseMPRegenMoveModeBonus[moveMode][2];
    }
    const double delta = levelValue - m_BaseMPRegenMoveModeBonus[moveMode][4];
    return m_BaseMPRegenMoveModeBonus[moveMode][3] * m_BaseMPRegenMoveModeBonus[moveMode][2] * delta * delta +
        m_BaseMPRegenMoveModeBonus[moveMode][5];
}

double CPCParamDB::GetCPRegenMoveModeBonus(MoveModeEnum moveMode, int level) {
    L2_GUARD_FUNCTION();
    const double levelValue = static_cast<double>(level);
    if (levelValue < m_BaseCPRegenMoveModeBonus[moveMode][0] || m_BaseCPRegenMoveModeBonus[moveMode][1] <= levelValue) {
        return m_BaseCPRegenMoveModeBonus[moveMode][2];
    }
    const double delta = levelValue - m_BaseCPRegenMoveModeBonus[moveMode][4];
    return m_BaseCPRegenMoveModeBonus[moveMode][3] * m_BaseCPRegenMoveModeBonus[moveMode][2] * delta * delta +
        m_BaseCPRegenMoveModeBonus[moveMode][5];
}

double CPCParamDB::GetKarmaIncreaseAmount(int level) {
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(level) - 1U < 120U) {
        return m_KarmaIncreaseTable[level];
    }
    return 0.0;
}

std::int64_t CPCParamDB::GetStandardExpByLevel(int level) {
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(level) - 1U < 120U) {
        return m_nStandardExpByLevel[level];
    }
    return 0;
}

int CPCParamDB::GetMaxHennaSlotCount(const ClassType classType) {
    const std::uint32_t index = static_cast<std::uint32_t>(classType);
    if (index < 193U) {
        return ClassToHennaSlotCount[index];
    }
    return 0;
}
