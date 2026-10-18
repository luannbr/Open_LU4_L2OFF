#pragma once

#include "Protocol/src/L2Constants.h"

#include "Shared/src/BaseAttribute.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/Systems.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

// Cross-platform carrier for Win32's 0x34-byte
// _BY_HANDLE_FILE_INFORMATION. The current Shared/BaseStructure.cpp helper
// returns this value by copy. ScriptFileUtil consumes the file-size DWORDs.
// The remaining fields stay present so the Shared API is not reduced to that
// one caller's needs.
struct ByHandleFileInformation {
    std::uint32_t dwFileAttributes = 0;
    FileTime ftCreationTime{};
    FileTime ftLastAccessTime{};
    FileTime ftLastWriteTime{};
    std::uint32_t dwVolumeSerialNumber = 0;
    std::uint32_t nFileSizeHigh = 0;
    std::uint32_t nFileSizeLow = 0;
    std::uint32_t nNumberOfLinks = 0;
    std::uint32_t nFileIndexHigh = 0;
    std::uint32_t nFileIndexLow = 0;
};


// Current L2Server.exe 0x004af678, current Cached.exe 0x00450ecc and the
// symbol-complete VS2015 Shared donor.
// On Windows this is a direct CreateFileW/GetFileInformationByHandle owner.
// other hosts translate the same metadata contract through stat(2).
ByHandleFileInformation GetFileInfo(const wchar_t* const fileName);

namespace ScriptFileUtil::CacheToServer {

// Current L2Server.exe 0x004af844. The CacheD version handshake supplies
// five little-endian file-size pairs in this exact production order.
bool CheckFileInfo(const unsigned char* packet);

} // namespace ScriptFileUtil::CacheToServer

// Current L2Server.exe 0x004af3dc/0x004af4d0 and current Cached.exe
// 0x00450dd8. L2Server emits both static methods and calls
// them from its instant-zone and other reset-date owners.
class ResetBindingDayHelper {
public:
    static int FindLatestResetBindingDay(
        int bindingDayMask, int resetMinuteOfDay, int currentWeekday, int currentMinuteOfDay
    );

    static int FindNextResetBindingDay(int bindingDayMask, int resetMinuteOfDay, int currentWeekday, int currentMinuteOfDay);
};

namespace TeleportSourceType {

enum Enum : std::int32_t {
    Unknown = 0,
    Area = 1,
    BlockUpsetNPC = 2,
    BlockUpsetPCStart = 3,
    BlockUpsetPCBanish = 4,
    BuilderCmd = 5,
    CleftStart = 6,
    CleftPCBanishWhenCleftStart = 7,
    CleftPCBanishWhenCleftEnd = 8,
    CrataeCubeResurrect = 9,
    PartyPacket = 10,
    PartyTelBookMarkPacket = 11,
    TeleportCharPacket = 12,
    PetToMaster1 = 13,
    ReplyBookMark1 = 14,
    ReplyBookMark2 = 15,
    SendHome = 16,
    ObserverHide1 = 17,
    ObserverHide2 = 18,
    ObserverHide3 = 19,
    InzoneEnterZone = 20,
    InzoneLeaveZone = 21,
    TeleportToGameArea = 22,
    PartyTeleportToGameArea = 23,
    TeleportMPCCToLocation1 = 24,
    TeleportMPCCToLocation2 = 25,
    OnNpcAbsoluteReturn = 26,
    NpcInstantTeleport = 27,
    NpcInstantTeleportInMyTerritory = 28,
    NPCTeleportToUser = 29,
    NpcTeleportTo = 30,
    CompleteToDespawnPet = 31,
    EscapeFromOlympiad = 32,
    ObserverHide4 = 33,
    ObserverHide5 = 34,
    CExpelTimerExpired = 35,
    tst_i_teleport = 36,
    tst_i_call_party = 37,
    GoPoint = 38,
    UCResurrectUser = 39,
    MasterTeleportToLocation = 40,
    UserRestart = 41,
    UserValidateLocation1 = 42,
    UserValidateLocation2 = 43,
    UserCameraModeOn = 44,
    UserCameraModeOff = 45,
    UserOnMoveToLocationPacket = 46,
    OnRequestUserCommand = 47,
    TeleportToLocationWithItem = 48,
    UserPolymorph = 49,
    UserCallPCVerified = 50,
    HtmlCmdTeleport = 51,
    AsyncVehicleKickIllegal = 52,
    AsyncVehicleKickTransformer = 53,
    GlobalObjectInstantTeleportEx = 54,
    MOVEAROUND = 55,
    ATTACK = 56,
    MOVETO_1 = 57,
    MOVETO_2 = 58,
    InstantTeleportFunction = 59,
    InstantTeleportWithItemFunction = 60,
    PetMasterIsTooFar = 61,
    AtomicTeleportParty = 62,
    AtomicTeleportTo = 63,
    TeleportRandomPos = 64,
    PositionChange = 65,
    tst_i_call_target_party = 66,
    tst_i_teleport_to_npc = 67,
    tst_i_party_teleport_to_npc = 68,
    PledgeGameStart = 69,
    MPCCTelBookMarkPacket = 70,
    NPCTeleportToTalk = 71,
    TrainingZone = 72,
    tst_i_goto_target = 73,
    InzoneFailToEnter = 74,
    InstantAgit = 75,
    Max = 76,
};

} // namespace TeleportSourceType

namespace TrainingZone {

enum EndReason : std::int32_t {
    BY_USER = 0,
    BY_TIMER = 1,
    BY_BUILDCMD = 2,
    BY_OUTSIDE = 3,
};

} // namespace TrainingZone

namespace ReuseDelaySaveReason {

enum Type : std::int32_t {
    SR_NORMAL = 0,
    SR_DUMP = 1,
    SR_MAX = 2,
};

} // namespace ReuseDelaySaveReason

namespace DBSecurity {

enum BuilderCmdType : std::int32_t {
    TurnOff = -1,
    TurnOn = 0,
    PenaltyLevel1_Logging = 1,
    PenaltyLevel2_DisconnectClient = 2,
    PenaltyLevel3_BlockAccount = 3,
};

} // namespace DBSecurity

namespace MesType {

enum Enum : std::int32_t {
    NONE = -1,
    PHYSICAL = 0,
    MENTAL = 1,
    MAX = 2,
};

} // namespace MesType

namespace AlterSkill {

enum Event : std::int32_t {
    None = -1,
    Critical = 0,
    Block = 1,
    Avoid = 2,
    Abnormal = 3,
    EventMax = 4,
};

} // namespace AlterSkill

// Shared/BaseStructure.h restoration records consumed by the generated
// skill parser and the instant restoration effects. The current L2Server
// image uses the xallocator revisions of both containers: xwstring is 0x28
// bytes and xvector is 0x20 bytes. TotalChecker supplies the source names.
// the current parser actions at 0x00cae9d0..0x00caef27 prove the layouts.
struct CRestorationUnit {
    CRestorationUnit(const char16_t* itemName, std::int64_t itemCount, int enchant) :
        xwsItemName(itemName), count(itemCount), nEnchant(enchant) {}

    std::u16string xwsItemName; // +0x00..+0x27
    std::int64_t count; // +0x28..+0x2f
    int nEnchant; // +0x30..+0x33
};

using CRestorationUnitVector = std::vector<CRestorationUnit*>;

struct CRestorationUnitList {
    explicit CRestorationUnitList(double probability) : unitList(nullptr), prob(probability) {}

    CRestorationUnitVector* unitList; // +0x00
    double prob; // +0x08
};

using CRestorationUnitListVector = std::vector<CRestorationUnitList*>;

struct CRestorationUnitGroupList {
    explicit CRestorationUnitGroupList(double probability) : groupList(nullptr), prob(probability) {}

    CRestorationUnitListVector* groupList; // +0x00
    double prob; // +0x08
};

using CRestorationUnitGroupListVector = std::vector<CRestorationUnitGroupList*>;


namespace ShapeShiftAutoMatchingType {

enum type : std::int32_t {
    SSAMT_ONE_HAND_SWORD = 0,
    SSAMT_ONE_HAND_MAGIC_SWORD = 1,
    SSAMT_DAGGER = 2,
    SSAMT_TWO_HAND_SWORD = 3,
    SSAMT_DUAL_FIST = 4,
    SSAMT_POLE = 5,
    SSAMT_BOW = 6,
    SSAMT_CROSSBOW = 7,
    SSAMT_ONE_HAND_BLUNT = 8,
    SSAMT_TWO_HAND_BLUNT = 9,
    SSAMT_ONE_HAND_MAGIC_BLUNT = 10,
    SSAMT_TWO_HAND_MAGIC_BLUNT = 11,
    SSAMT_DUAL_SWORD = 12,
    SSAMT_DUAL_DAGGER = 13,
    SSAMT_DUAL_BLUNT = 14,
    SSAMT_SHIELD = 15,
    SSAMT_SIGIL = 16,
    SSAMT_HEAVY_CHEST = 17,
    SSAMT_HEAVY_GLOVES = 18,
    SSAMT_HEAVY_LEGS = 19,
    SSAMT_HEAVY_FEET = 20,
    SSAMT_HEAVY_ONEPIECE = 21,
    SSAMT_HEAVY_ALLDRESS = 22,
    SSAMT_LIGHT_CHEST = 23,
    SSAMT_LIGHT_GLOVES = 24,
    SSAMT_LIGHT_LEGS = 25,
    SSAMT_LIGHT_FEET = 26,
    SSAMT_LIGHT_ONEPIECE = 27,
    SSAMT_LIGHT_ALLDRESS = 28,
    SSAMT_MAGIC_CHEST = 29,
    SSAMT_MAGIC_GLOVES = 30,
    SSAMT_MAGIC_LEGS = 31,
    SSAMT_MAGIC_FEET = 32,
    SSAMT_MAGIC_ONEPIECE = 33,
    SSAMT_MAGIC_ALLDRESS = 34,
    SSAMT_MAX = 35,
};

} // namespace ShapeShiftAutoMatchingType

namespace ManipulateType {

enum Enum : std::int32_t {
    MANI_QUEST = 0,
    MANI_AUCTION = 1,
    MANI_CARD_COLLECT = 2,
    MANI_ITEM_ACTION_PEEL = 3,
    MANI_CAMPAIGN_ITEM = 4,
    MANI_ATTRIBUTE_ITEM = 5,
    MANI_MPCC_TELEPORT = 6,
    MANI_GIVE_NPC_ITEM = 7,
    MANI_GIVE_CASTLE_ITEM = 8,
    MANI_GIVE_OLYMPIAD_ITEM = 9,
    MANI_SKILL_ENCHANT = 10,
    MANI_SKILL_EFFECT_INSTANT = 11,
    MANI_PCCAFE_COUPON = 12,
    MANI_GIVE_EVENT_ITEM = 13,
    MANI_ATTRIBUTE_ITEM_CANCEL = 14,
    MANI_UNDEFINED_ITEM = 15,
    MANI_TELEPORT_ITEM = 16,
    MANI_TELEPORT_KEEPER = 17,
    MANI_CALL_PC = 18,
    MANI_PLEDGE_SKILL = 19,
    MANI_TAX_BANK = 20,
    MANI_PET_ITEM = 21,
    MANI_MPCC_DIVIDE = 22,
    MANI_PARTY_DIVIDE = 23,
    MANI_GIVE_ITEM_ON_ACCOUNT = 24,
    MANI_COMBINATION_ITEM = 25,
    MANI_ALCHEMY_ITEM = 26,
    MANI_VIP_ATTENDANCE = 27,
    MANI_ENSOUL = 28,
    MANI_MONSTER_BOOK_REWARD = 29,
    MANI_CARD_UPDOWN_GAME = 30,
    MANI_WITH_LAMBDA_JOB = 31,
    MANI_PLEDGE_MISSION_REWARD = 32,
    MANI_USE_COUNT = 33,
    MANI_ENCHANT_ARTIFACT = 34,
    MANI_MAX = 35,
};

} // namespace ManipulateType


// Shared/BaseStructure.h value created by the current L2Server NPC
// parser and passed through CNPC's typed GoodStruct-vector virtual slot.
// The parser allocates exactly 0x10 bytes, writes the class ID at +0x00 and
// the sign-extended 64-bit price change at +0x08.
struct GoodStruct {
    int ItemClassId;
    std::int64_t PriceChange;
};


// Exact value layouts recovered from Shared/BaseStructure.h.
struct FAngle {
    enum Enum : std::int32_t {
        Front = 0,
        Side = 1,
        Back = 2,
    };

    bool IsBetween(FAngle begin, FAngle end);
    Enum GetAngleDiff(FAngle other);

    std::int32_t Yaw = 0;
};

struct FVector {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    FVector() = default;
    // Current L2NPC.exe copies at 0x004193a8 (constructor) and 0x004192e0
    // (assignment) copy the three components, so the defaults match them and
    // keep FVector trivially copyable.
    FVector(const FVector& vector) = default;
    FVector(double xValue, double yValue, double zValue) : x(xValue), y(yValue), z(zValue) {}
    FVector(int xValue, int yValue, int zValue) :
        x(static_cast<double>(xValue)), y(static_cast<double>(yValue)), z(static_cast<double>(zValue)) {}

    FVector& operator=(const FVector& vector) = default;
    // Const so C++20 reversed-operator candidates are not ambiguous.
    bool operator==(const FVector& vector) const;
    FVector operator+(const FVector& other) const {
        return FVector{
            x + other.x,
            y + other.y,
            z + other.z,
        };
    }

    bool IsEqual(const FVector& vector) const {
        constexpr double epsilon = static_cast<double>(1.0e-5F);
        return std::fabs(x - vector.x) < epsilon && std::fabs(y - vector.y) < epsilon && std::fabs(z - vector.z) < epsilon;
    }

    double DistCube(const FVector& other) const {
        return (x - other.x) * (x - other.x) + (y - other.y) * (y - other.y) + (z - other.z) * (z - other.z);
    }

    double Size() const { return std::sqrt(x * x + y * y + z * z); }

    // TotalChecker2015's Shared/BaseStructure.h type record supplies the
    // original inline name and const qualification. Current L2Server.exe
    // 0x0046030c fixes the strict component-wise float-origin tolerance.
    bool IsZero() const {
        constexpr double tolerance = static_cast<double>(1.0e-5F);
        return std::fabs(x) < tolerance && std::fabs(y) < tolerance && std::fabs(z) < tolerance;
    }

    // The donor PDB supplies the original inline name. Current L2NPC.exe
    // 0x00488f34 fixes the 1e-8 squared-length threshold and, importantly,
    // takes the zero-vector branch for unordered (NaN) input.
    FVector SafeNormal() const {
        const double lengthSquared = x * x + y * y + z * z;
        if (lengthSquared >= 1e-8) {
            const double inverseLength = 1.0 / std::sqrt(lengthSquared);
            return FVector{
                inverseLength * x,
                inverseLength * y,
                inverseLength * z,
            };
        }
        static const FVector nullVector{};
        return nullVector;
    }

    // Current L2NPC.exe 0x0046a5a0 and current L2Server.exe 0x00460380
    // emit the same normalized body. This is an inline
    // static member in Shared/BaseStructure.h, not a caller-local Z-axis
    // shortcut.
    static FVector RotateAngleAxis(const FVector* vector, const int angle, const FVector* axis) {
        const double radians = static_cast<double>(angle) * 6.283185307 / 65535.0;
        const double sine = std::sin(radians);
        const double cosine = std::cos(radians);

        const double axisZ = axis->z;
        const double axisX = axis->x;
        const double axisY = axis->y;
        const double oneMinusCosine = 1.0 - cosine;
        const double axisXY = oneMinusCosine * axisX * axisY;
        const double axisZX = oneMinusCosine * axisZ * axisX;
        const double axisZY = oneMinusCosine * axisZ * axisY;

        FVector result;
        result.x = (axisX * axisX * oneMinusCosine + cosine) * vector->x + (axisXY - axisZ * sine) * vector->y +
            (axisZX + axisY * sine) * vector->z;
        result.z = (axisZY + axisX * sine) * vector->y + (axisZX - axisY * sine) * vector->x +
            (axisZ * axisZ * oneMinusCosine + cosine) * vector->z;
        result.y = (axisY * axisY * oneMinusCosine + cosine) * vector->y + (axisXY + axisZ * sine) * vector->x +
            (axisZY - axisX * sine) * vector->z;
        return result;
    }

    // Inline definitions from Shared/BaseStructure.h. Module 84 emits
    // these two COMDAT bodies as FVector::NormalizeSquare/NormalizeCube.
    FVector& NormalizeSquare() {
        static const FVector nullVector{};
        const double length = std::sqrt((x - nullVector.x) * (x - nullVector.x) + (y - nullVector.y) * (y - nullVector.y));
        if (length > 0.0) {
            x /= length;
            y /= length;
            z /= length;
        }
        return *this;
    }

    FVector& NormalizeCube() {
        static const FVector nullVector{};
        const double length = std::sqrt(
            (x - nullVector.x) * (x - nullVector.x) + (y - nullVector.y) * (y - nullVector.y) +
            (z - nullVector.z) * (z - nullVector.z)
        );
        if (length > 0.0) {
            x /= length;
            y /= length;
            z /= length;
        }
        return *this;
    }
};

// Shared/BaseStructure.h value used by CTerritory's polygon vector. The
// shipping code addresses consecutive entries with a 0x10-byte stride.
struct FVector2D {
    double x = 0.0;
    double y = 0.0;

    FVector2D() = default;
    // Current L2NPC.exe copies at 0x004193c0 (constructor) and 0x005073c4
    // (assignment) copy both components, so the defaults match them.
    FVector2D(const FVector2D& vector) = default;
    FVector2D(double xValue, double yValue) : x(xValue), y(yValue) {}
    FVector2D(int xValue, int yValue) : x(static_cast<double>(xValue)), y(static_cast<double>(yValue)) {}

    FVector2D& operator=(const FVector2D& vector) = default;

    // Donor-PDB name and current L2NPC.exe 0x00435a34. The target evaluates
    // the sine/cosine pair separately for each result component.
    FVector2D Rotate(int angle) const {
        const double radians = static_cast<double>(angle) * 3.141592653589793 / 180.0;
        FVector2D result;
        result.y = std::sin(radians) * x + std::cos(radians) * y;
        result.x = std::cos(radians) * x - std::sin(radians) * y;
        return result;
    }

    // Inline Shared/BaseStructure.h helpers used by CTerritory::IsCollide.
    // The adjacent PDB exposes both as public static members. The recovered
    // NASC body confirms that CCW returns only -1 or 1.
    static int CCW(const FVector2D& first, const FVector2D& second, const FVector2D& third) {
        return (second.x - first.x) * (third.y - first.y) <= (second.y - first.y) * (third.x - first.x) ? -1 : 1;
    }

    static bool IsIntersect(
        const FVector2D& firstStart, const FVector2D& firstEnd, const FVector2D& secondStart, const FVector2D& secondEnd
    ) {
        return CCW(firstStart, firstEnd, secondStart) * CCW(firstStart, firstEnd, secondEnd) < 1 &&
            CCW(secondStart, secondEnd, firstStart) * CCW(secondStart, secondEnd, firstEnd) < 1;
    }
};

// Shared/BaseStructure.h. TotalChecker2015 records the complete historical
// public surface and member spellings. Current L2Server.exe independently
// creates the three static parser bodies in BaseStructure.cpp and copies
// the value as one eight-byte TimePeriodInfo element. Current
// CEvent_CommonDropSystem::LoadDrop also proves that default initialization
// leaves both lanes untouched. The two-argument constructor and IsInTime
// remain donor-scoped inline surface until current L2Server bodies close them.
struct TimePeriodInfo {
    TimePeriodInfo() = default;
    TimePeriodInfo(int startTime, int endTime) : m_nStartTime(startTime), m_nEndTime(endTime) {}

    bool IsInTime(const int currentTime) const { return m_nStartTime < currentTime && currentTime < m_nEndTime; }

    static bool ParseTime(const char16_t* time, int& value);

    static bool ParseWeek(const char16_t* week, bool* days, int count);

    static bool ParseHourMin(const char16_t* time, int& value);

    int m_nStartTime;
    int m_nEndTime;
};


enum QuestType : std::int32_t {
    QUEST_NORMAL = 0,
    QUEST_NONREGIST = 1,
    QUEST_MAX = 2,
};

enum UserNPCLogType : std::int32_t {
    USER_NPC_LOG_QUEST_HUNT = 0,
    USER_NPC_LOG_MAX = 1,
};

enum AbnormalStatusSlotType : std::int32_t {
    ABNS_INVALID = -1,
    ABNS_AB_HAWK_EYE = 0,
    ABNS_ALL_ATTACK_DOWN = 1,
    ABNS_ALL_ATTACK_UP = 2,
    ABNS_ALL_SPEED_DOWN = 3,
    ABNS_ALL_SPEED_UP = 4,
    ABNS_ANTARAS_DEBUFF = 5,
    ABNS_ARMOR_EARTH = 6,
    ABNS_ARMOR_FIRE = 7,
    ABNS_ARMOR_HOLY = 8,
    ABNS_ARMOR_UNHOLY = 9,
    ABNS_ARMOR_WATER = 10,
    ABNS_ARMOR_WIND = 11,
    ABNS_ATTACK_SPEED_UP_BOW = 12,
    ABNS_ATTACK_TIME_DOWN = 13,
    ABNS_ATTACK_TIME_UP = 14,
    ABNS_AVOID_DOWN = 15,
    ABNS_AVOID_UP = 16,
    ABNS_AVOID_UP_SPECIAL = 17,
    ABNS_BERSERKER = 18,
    ABNS_BIG_BODY = 19,
    ABNS_BIG_HEAD = 20,
    ABNS_BLEEDING = 21,
    ABNS_BOW_RANGE_UP = 22,
    ABNS_BUFF_QUEEN_OF_CAT = 23,
    ABNS_BUFF_UNICORN_SERAPHIM = 24,
    ABNS_CANCEL_PROB_DOWN = 25,
    ABNS_CASTING_TIME_DOWN = 26,
    ABNS_CASTING_TIME_UP = 27,
    ABNS_CHEAP_MAGIC = 28,
    ABNS_CRITICAL_DMG_DOWN = 29,
    ABNS_CRITICAL_DMG_UP = 30,
    ABNS_CRITICAL_PROB_DOWN = 31,
    ABNS_CRITICAL_PROB_UP = 32,
    ABNS_DANCE_OF_AQUA_GUARD = 33,
    ABNS_DANCE_OF_CONCENTRATION = 34,
    ABNS_DANCE_OF_EARTH_GUARD = 35,
    ABNS_DANCE_OF_FIRE = 36,
    ABNS_DANCE_OF_FURY = 37,
    ABNS_DANCE_OF_INSPIRATION = 38,
    ABNS_DANCE_OF_LIGHT = 39,
    ABNS_DANCE_OF_MYSTIC = 40,
    ABNS_DANCE_OF_PROTECTION = 41,
    ABNS_DANCE_OF_SHADOW = 42,
    ABNS_DANCE_OF_SIREN = 43,
    ABNS_DANCE_OF_VAMPIRE = 44,
    ABNS_DANCE_OF_WARRIOR = 45,
    ABNS_DEBUFF_NIGHTSHADE = 46,
    ABNS_DEBUFF_SHIELD = 47,
    ABNS_DECREASE_WEIGHT_PENALTY = 48,
    ABNS_DERANGEMENT = 49,
    ABNS_DETECT_WEAKNESS = 50,
    ABNS_DMG_SHIELD = 51,
    ABNS_DOT_ATTR = 52,
    ABNS_DOT_MP = 53,
    ABNS_DRAGON_BREATH = 54,
    ABNS_DUELIST_SPIRIT = 55,
    ABNS_FATAL_POISON = 56,
    ABNS_FISHING_MASTERY_DOWN = 57,
    ABNS_FLY_AWAY = 58,
    ABNS_FOCUS_DAGGER = 59,
    ABNS_HEAL_EFFECT_DOWN = 60,
    ABNS_HEAL_EFFECT_UP = 61,
    ABNS_HERO_BUFF = 62,
    ABNS_HERO_DEBUFF = 63,
    ABNS_HIT_DOWN = 64,
    ABNS_HIT_UP = 65,
    ABNS_HOLY_ATTACK = 66,
    ABNS_HP_RECOVER = 67,
    ABNS_HP_REGEN_DOWN = 68,
    ABNS_HP_REGEN_UP = 69,
    ABNS_LIFE_FORCE_ORC = 70,
    ABNS_LIFE_FORCE_OTHERS = 71,
    ABNS_MAGIC_CRITICAL_UP = 72,
    ABNS_MAJESTY = 73,
    ABNS_MAX_BREATH_UP = 74,
    ABNS_MAX_HP_DOWN = 75,
    ABNS_MAX_HP_UP = 76,
    ABNS_MAX_MP_UP = 77,
    ABNS_MA_DOWN = 78,
    ABNS_MA_UP = 79,
    ABNS_MA_UP_HERB = 80,
    ABNS_MD_DOWN = 81,
    ABNS_MD_UP = 82,
    ABNS_MD_UP_ATTR = 83,
    ABNS_MIGHT_MORTAL = 84,
    ABNS_MP_COST_DOWN = 85,
    ABNS_MP_COST_UP = 86,
    ABNS_MP_RECOVER = 87,
    ABNS_MP_REGEN_UP = 88,
    ABNS_MULTI_BUFF = 89,
    ABNS_MULTI_DEBUFF = 90,
    ABNS_PARALYZE = 91,
    ABNS_PA_DOWN = 92,
    ABNS_PA_PD_UP = 93,
    ABNS_PA_UP = 94,
    ABNS_PA_UP_HERB = 95,
    ABNS_PA_UP_SPECIAL = 96,
    ABNS_PD_DOWN = 97,
    ABNS_PD_UP = 98,
    ABNS_PD_UP_BOW = 99,
    ABNS_PD_UP_SPECIAL = 100,
    ABNS_PINCH = 101,
    ABNS_POISON = 102,
    ABNS_POLEARM_ATTACK = 103,
    ABNS_POSSESSION = 104,
    ABNS_PRESERVE_ABNORMAL = 105,
    ABNS_PUBLIC_SLOT = 106,
    ABNS_RAGE_MIGHT = 107,
    ABNS_REDUCE_DROP_PENALTY = 108,
    ABNS_REFLECT_ABNORMAL = 109,
    ABNS_RESIST_BLEEDING = 110,
    ABNS_RESIST_DEBUFF_DISPEL = 111,
    ABNS_RESIST_DERANGEMENT = 112,
    ABNS_RESIST_HOLY_UNHOLY = 113,
    ABNS_RESIST_POISON = 114,
    ABNS_RESIST_SHOCK = 115,
    ABNS_RESIST_SPIRITLESS = 116,
    ABNS_REUSE_DELAY_DOWN = 117,
    ABNS_REUSE_DELAY_UP = 118,
    ABNS_ROOT_PHYSICALLY = 119,
    ABNS_ROOT_MAGICALLY = 120,
    ABNS_SHIELD_DEFENCE_UP = 121,
    ABNS_SHIELD_PROB_UP = 122,
    ABNS_SILENCE = 123,
    ABNS_SILENCE_ALL = 124,
    ABNS_SILENCE_PHYSICAL = 125,
    ABNS_SLEEP = 126,
    ABNS_SNIPE = 127,
    ABNS_SONG_OF_CHAMPION = 128,
    ABNS_SONG_OF_EARTH = 129,
    ABNS_SONG_OF_FLAME_GUARD = 130,
    ABNS_SONG_OF_HUNTER = 131,
    ABNS_SONG_OF_INVOCATION = 132,
    ABNS_SONG_OF_LIFE = 133,
    ABNS_SONG_OF_MEDITATION = 134,
    ABNS_SONG_OF_RENEWAL = 135,
    ABNS_SONG_OF_STORM_GUARD = 136,
    ABNS_SONG_OF_VENGEANCE = 137,
    ABNS_SONG_OF_VITALITY = 138,
    ABNS_SONG_OF_WARDING = 139,
    ABNS_SONG_OF_WATER = 140,
    ABNS_SONG_OF_WIND = 141,
    ABNS_SPA_DISEASE_A = 142,
    ABNS_SPA_DISEASE_B = 143,
    ABNS_SPA_DISEASE_C = 144,
    ABNS_SPA_DISEASE_D = 145,
    ABNS_SPEED_DOWN = 146,
    ABNS_SPEED_UP = 147,
    ABNS_SPEED_UP_SPECIAL = 148,
    ABNS_SSQ_TOWN_BLESSING = 149,
    ABNS_SSQ_TOWN_CURSE = 150,
    ABNS_STEALTH = 151,
    ABNS_STUN = 152,
    ABNS_THRILL_FIGHT = 153,
    ABNS_TOUCH_OF_DEATH = 154,
    ABNS_TOUCH_OF_LIFE = 155,
    ABNS_TURN_FLEE = 156,
    ABNS_TURN_PASSIVE = 157,
    ABNS_TURN_STONE = 158,
    ABNS_ULTIMATE_BUFF = 159,
    ABNS_ULTIMATE_DEBUFF = 160,
    ABNS_VALAKAS_ITEM = 161,
    ABNS_VAMPIRIC_ATTACK = 162,
    ABNS_WATCHER_GAZE = 163,
    ABNS_RESURRECTION_SPECIAL = 164,
    ABNS_COUNTER_SKILL = 165,
    ABNS_AVOID_SKILL = 166,
    ABNS_CP_UP = 167,
    ABNS_CP_DOWN = 168,
    ABNS_CP_REGEN_UP = 169,
    ABNS_CP_REGEN_DOWN = 170,
    ABNS_INVINCIBILITY = 171,
    ABNS_ABNORMAL_INVINCIBILITY = 172,
    ABNS_PHYSICAL_STANCE = 173,
    ABNS_MAGICAL_STANCE = 174,
    ABNS_COMBINATION = 175,
    ABNS_ANESTHESIA = 176,
    ABNS_CRITICAL_POISON = 177,
    ABNS_SEIZURE_PENALTY = 178,
    ABNS_ABNORMAL_ITEM = 179,
    ABNS_SEIZURE_A = 180,
    ABNS_SEIZURE_B = 181,
    ABNS_SEIZURE_C = 182,
    ABNS_FORCE_MEDITATION = 183,
    ABNS_MIRAGE = 184,
    ABNS_POTION_OF_GENESIS = 185,
    ABNS_PVP_DMG_UP = 186,
    ABNS_PVP_DMG_DOWN = 187,
    ABNS_IRON_SHIELD = 188,
    ABNS_TRANSFER_DAMAGE = 189,
    ABNS_SONG_OF_ELEMENTAL = 190,
    ABNS_DANCE_OF_ALIGNMENT = 191,
    ABNS_ARCHER_SPECIAL = 192,
    ABNS_SPOIL_BOMB = 193,
    ABNS_FIRE_DOT = 194,
    ABNS_WATER_DOT = 195,
    ABNS_WIND_DOT = 196,
    ABNS_EARTH_DOT = 197,
    ABNS_HEAL_POWER_UP = 198,
    ABNS_RECHARGE_UP = 199,
    ABNS_NORMAL_ATTACK_BLOCK = 200,
    ABNS_DISARM = 201,
    ABNS_DEATH_MARK = 202,
    ABNS_KAMAEL_SPECIAL = 203,
    ABNS_TRANSFORM = 204,
    ABNS_DARK_SEED = 205,
    ABNS_REAL_TARGET = 206,
    ABNS_FREEZING = 207,
    ABNS_TIME_CHECK = 208,
    ABNS_MA_MD_UP = 209,
    ABNS_DEATH_CLACK = 210,
    ABNS_HOT_GROUND = 211,
    ABNS_EVIL_BLOOD = 212,
    ABNS_ALL_REGEN_UP = 213,
    ABNS_ALL_REGEN_DOWN = 214,
    ABNS_IRON_SHIELD_I = 215,
    ABNS_ARCHER_SPECIAL_I = 216,
    ABNS_T_CRT_RATE_UP = 217,
    ABNS_T_CRT_RATE_DOWN = 218,
    ABNS_T_CRT_DMG_UP = 219,
    ABNS_T_CRT_DMG_DOWN = 220,
    ABNS_INSTINCT = 221,
    ABNS_OBLIVION = 222,
    ABNS_WEAK_CONSTITUTION = 223,
    ABNS_THIN_SKIN = 224,
    ABNS_ENERVATION = 225,
    ABNS_SPITE = 226,
    ABNS_MENTAL_IMPOVERISH = 227,
    ABNS_ATTRIBUTE_POTION = 228,
    ABNS_TALISMAN = 229,
    ABNS_MULTI_DEBUFF_FIRE = 230,
    ABNS_MULTI_DEBUFF_WATER = 231,
    ABNS_MULTI_DEBUFF_WIND = 232,
    ABNS_MULTI_DEBUFF_EARTH = 233,
    ABNS_MULTI_DEBUFF_HOLY = 234,
    ABNS_MULTI_DEBUFF_UNHOLY = 235,
    ABNS_LIFE_FORCE_KAMAEL = 236,
    ABNS_MA_UP_SPECIAL = 237,
    ABNS_PK_PROTECT = 238,
    ABNS_MAXIMUM_ABILITY = 239,
    ABNS_TARGET_LOCK = 240,
    ABNS_PROTECTION = 241,
    ABNS_WILL = 242,
    ABNS_SEED_OF_KNIGHT = 243,
    ABNS_EXPOSE_WEAK_POINT = 244,
    ABNS_FORCE_OF_DESTRUCTION = 245,
    ABNS_ELEMENTAL_ARMOR = 246,
    ABNS_SUMMON_CONDITION = 247,
    ABNS_IMPROVE_PA_PD_UP = 248,
    ABNS_IMPROVE_MA_MD_UP = 249,
    ABNS_IMPROVE_HP_MP_UP = 250,
    ABNS_IMPROVE_CRT_RATE_DMG_UP = 251,
    ABNS_IMPROVE_SHIELD_RATE_DEFENCE_UP = 252,
    ABNS_IMPROVE_SPEED_AVOID_UP = 253,
    ABNS_LIMIT = 254,
    ABNS_MULTI_DEBUFF_SOUL = 255,
    ABNS_CURSE_LIFE_FLOW = 256,
    ABNS_BETRAYAL_MARK = 257,
    ABNS_TRANSFORM_HANGOVER = 258,
    ABNS_TRANSFORM_SCRIFICE = 259,
    ABNS_SONG_OF_WINDSTORM = 260,
    ABNS_DANCE_OF_BLADESTORM = 261,
    ABNS_IMPROVE_VAMPIRIC_HASTE = 262,
    ABNS_WEAPON_MASTERY = 263,
    ABNS_APELLA = 264,
    ABNS_TRANSFORM_SCRIFICE_P = 265,
    ABNS_SUB_TRIGGER_HASTE = 266,
    ABNS_SUB_TRIGGER_DEFENCE = 267,
    ABNS_SUB_TRIGGER_CRT_RATE_UP = 268,
    ABNS_SUB_TRIGGER_SPIRIT = 269,
    ABNS_MIRAGE_TRAP = 270,
    ABNS_DEATH_PENALTY = 271,
    ABNS_FOR_GAME = 272,
    ABNS_BLOOD_CONSTRACT = 273,
    ABNS_DWARF_ATTACK_BUFF = 274,
    ABNS_DWARF_DEFENCE_BUFF = 275,
    ABNS_EVASION_BUFF = 276,
    ABNS_BLESS_THE_BLOOD = 277,
    ABNS_PVP_WEAPON_BUFF = 278,
    ABNS_PVP_WEAPON_DEBUFF = 279,
    ABNS_SEED_OF_CRITICAL = 280,
    ABNS_VP_UP = 281,
    ABNS_BOT_PENALTY = 282,
    ABNS_HIDE = 283,
    ABNS_DD_RESIST = 284,
    ABNS_SONG_OF_PURIFICATION = 285,
    ABNS_DANCE_OF_BERSERKER = 286,
    ABNS_REFLECT_MAGIC_DD = 287,
    ABNS_FINAL_SECRET = 288,
    ABNS_STIGMA_OF_SILEN = 289,
    ABNS_SEED_DEBUFF_A = 290,
    ABNS_SEED_DEBUFF_B = 291,
    ABNS_SEED_DEBUFF_C = 292,
    ABNS_SEED_DEBUFF_D = 293,
    ABNS_SEED_DEBUFF_E = 294,
    ABNS_SEED_DEBUFF_F = 295,
    ABNS_SEED_DEBUFF_G = 296,
    ABNS_SEED_DEBUFF_H = 297,
    ABNS_SEED_DEBUFF_I = 298,
    ABNS_SEED_BUFF_A = 299,
    ABNS_AGATHION_BUFF = 300,
    ABNS_COUNTER_CRITICAL = 301,
    ABNS_COUNTER_CRITICAL_TRIGGER = 302,
    ABNS_ATTACK_TIME_DOWN_SPECIAL = 303,
    ABNS_BLOCK_SPEED_UP = 304,
    ABNS_BLOCK_SHIELD_UP = 305,
    ABNS_DEATHWORM = 306,
    ABNS_MULTI_DEBUFF_A = 307,
    ABNS_MULTI_DEBUFF_B = 308,
    ABNS_MULTI_DEBUFF_C = 309,
    ABNS_MULTI_DEBUFF_D = 310,
    ABNS_MULTI_DEBUFF_E = 311,
    ABNS_MULTI_DEBUFF_F = 312,
    ABNS_MULTI_DEBUFF_G = 313,
    ABNS_STIGMA_A = 314,
    ABNS_MULTI_BUFF_A = 315,
    ABNS_VAMPIRIC_ATTACK_SPECIAL = 316,
    ABNS_BLOCK_RESURRECTION = 317,
    ABNS_IMPROVE_HIT_DEFENCE_CRT_RATE_UP = 318,
    ABNS_IMPROVE_MAGIC_SPEED_CRT_RATE_UP = 319,
    ABNS_EVENT_GAWI = 320,
    ABNS_EVENT_BAWI = 321,
    ABNS_EVENT_BO = 322,
    ABNS_EVENT_WIN = 323,
    ABNS_EVENT_TERRITORY = 324,
    ABNS_EVENT_SANTA_REWARD = 325,
    ABNS_VP_KEEP = 326,
    ABNS_WP_CHANGE_WEAPON = 327,
    ABNS_SIGNAL_A = 328,
    ABNS_SIGNAL_B = 329,
    ABNS_SIGNAL_C = 330,
    ABNS_SIGNAL_D = 331,
    ABNS_SIGNAL_E = 332,
    ABNS_HALLO_EVENT_BUF1 = 333,
    ABNS_HALLO_EVENT_BUF2 = 334,
    ABNS_HALLO_EVENT_BUF3 = 335,
    ABNS_HALLO_EVENT_BUF4 = 336,
    ABNS_HALLO_EVENT_BUF5 = 337,
    ABNS_HALLO_EVENT_BUF6 = 338,
    ABNS_HALLO_EVENT_BUF7 = 339,
    ABNS_HALLO_EVENT_BUF8 = 340,
    ABNS_HALLO_EVENT_BUF9 = 341,
    ABNS_HALLO_EVENT_BUF10 = 342,
    ABNS_CHANGE_ATTR_W = 343,
    ABNS_CHANGE_ATTR_A = 344,
    ABNS_SOA_BUFF1 = 345,
    ABNS_SOA_BUFF2 = 346,
    ABNS_SOA_BUFF3 = 347,
    ABNS_DAMAGE_AMPLIFY = 348,
    ABNS_VIBRATION = 349,
    ABNS_BLOCK_TRANSFORM = 350,
    ABNS_SKILL_IGNORE = 351,
    ABNS_SIGNAL_G = 352,
    ABNS_KNIGHT_AURA = 353,
    ABNS_PATIENCE = 354,
    ABNS_VOTE = 355,
    ABNS_MP_SHIELD = 356,
    ABNS_VP_CHANGE = 357,
    ABNS_ABILITY_CHANGE = 358,
    ABNS_WISPERING_OF_BATTLE = 359,
    ABNS_MOTION_OF_DEFENCE = 360,
    ABNS_ARMOR_ELEMENT_ALL = 361,
    ABNS_MORALE_UP = 362,
    ABNS_TIME_BOMB = 363,
    ABNS_KNIGHT_SHIELD = 364,
    ABNS_AIRBIND = 365,
    ABNS_CHANGEBODY = 366,
    ABNS_KNOCKDOWN = 367,
    ABNS_MAX_HP_UP_K = 368,
    ABNS_BR_EVENT_BUF1 = 369,
    ABNS_BR_EVENT_BUF2 = 370,
    ABNS_BR_EVENT_BUF3 = 371,
    ABNS_BR_EVENT_BUF4 = 372,
    ABNS_BR_EVENT_BUF5 = 373,
    ABNS_BR_EVENT_BUF6 = 374,
    ABNS_BR_EVENT_BUF7 = 375,
    ABNS_BR_EVENT_BUF8 = 376,
    ABNS_BR_EVENT_BUF9 = 377,
    ABNS_BR_EVENT_BUF10 = 378,
    ABNS_POSSESSION_SPECIAL = 379,
    ABNS_DEPORT = 380,
    ABNS_FORCE_HP_HP = 381,
    ABNS_SPECIAL_BERSERKER = 382,
    ABNS_BATTLE_CRY = 383,
    ABNS_AVOID_SKILL_SPECIAL = 384,
    ABNS_SUPER_MOVE = 385,
    ABNS_CONFUSION = 386,
    ABNS_SUPER_BUFF = 387,
    ABNS_SUPER_AVOID = 388,
    ABNS_MOVEMENT = 389,
    ABNS_POISON_SELF = 390,
    ABNS_T_CRY_ALL = 391,
    ABNS_CRITICAL_SPECIAL = 392,
    ABNS_WEAPON_MASTER_SPECIAL = 393,
    ABNS_CLASS_CHANGE = 394,
    ABNS_DC_MOD = 395,
    ABNS_ABSORB = 396,
    ABNS_ENCHANTER_MOD = 397,
    ABNS_FINAL_BUFF = 398,
    ABNS_FINAL_DEBUFF = 399,
    ABNS_SHIELD_ATTACK = 400,
    ABNS_SACRIFICE = 401,
    ABNS_AURA = 402,
    ABNS_LIFE_FORCE_HEALER = 403,
    ABNS_LIFE_FORCE_HEALER_SELF = 404,
    ABNS_TURN_CRYSTAL = 405,
    ABNS_MARK_OF_LUMI = 406,
    ABNS_ENERGY_OF_TOTEM_1 = 407,
    ABNS_ENERGY_OF_TOTEM_2 = 408,
    ABNS_ENERGY_OF_TOTEM_3 = 409,
    ABNS_ENERGY_OF_TOTEM_4 = 410,
    ABNS_DEATH_PENALTY_GD = 411,
    ABNS_ALL = 412,
    ABNS_DEBUFF_SPECIAL_ATTACK = 413,
    ABNS_DEBUFF_SPECIAL_DEFENCE = 414,
    ABNS_BUFF_SPECIAL_ATTACK = 415,
    ABNS_BUFF_SPECIAL_DEFENCE = 416,
    ABNS_BUFF_SPECIAL_CONDITION = 417,
    ABNS_BUFF_SPECIAL_CRITICAL = 418,
    ABNS_BUFF_SPECIAL_HITAVOID = 419,
    ABNS_BUFF_SPECIAL_MOVE = 420,
    ABNS_BUFF_SPECIAL_CLASS = 421,
    ABNS_BUFF_SPECIAL_AURA = 422,
    ABNS_BUFF_SPECIAL_MULTI = 423,
    ABNS_EARTHWORM_DEBUFF = 424,
    ABNS_SPECIAL_RIDE = 425,
    ABNS_SPIRIT_SHARING = 426,
    ABNS_SUBSTITUTE_BUFF = 427,
    ABNS_ANTI_SUMMON = 428,
    ABNS_NPC_FURY = 429,
    ABNS_PET_FURY = 430,
    ABNS_CLAN_ADVENT = 431,
    ABNS_DEATH_PENALTY_BLOCK = 432,
    ABNS_SYNERGY_SIGEL = 433,
    ABNS_SYNERGY_TIR = 434,
    ABNS_SYNERGY_OTHEL = 435,
    ABNS_SYNERGY_YR = 436,
    ABNS_SYNERGY_FEOH = 437,
    ABNS_SYNERGY_IS = 438,
    ABNS_SYNERGY_WYNN = 439,
    ABNS_SYNERGY_EOLH = 440,
    ABNS_MARK_DEBUF_A = 441,
    ABNS_MARK_DEBUF_B = 442,
    ABNS_MARK_DEBUF_C = 443,
    ABNS_MARK_DEBUF_D = 444,
    ABNS_ACADEMY_UP = 445,
    ABNS_CLAN_FRIEND = 446,
    ABNS_CLAN_BOUNDARY = 447,
    ABNS_CLAN_PRISON = 448,
    ABNS_RACE_HUMAN1 = 449,
    ABNS_RACE_ELF1 = 450,
    ABNS_RACE_DARKELF1 = 451,
    ABNS_RACE_ORC1 = 452,
    ABNS_RACE_ORC2 = 453,
    ABNS_RACE_DWAF1 = 454,
    ABNS_RACE_DWAF2 = 455,
    ABNS_RACE_KAMAEL1 = 456,
    ABNS_NPC_ATTACK1 = 457,
    ABNS_NPC_ATTACK2 = 458,
    ABNS_NPC_ATTACK3 = 459,
    ABNS_WEAKENED_DEATH_PENALTY = 460,
    ABNS_VP_MENTOR_RUNE = 461,
    ABNS_INVINCIBILITY_SPECIAL = 462,
    ABNS_BUFF_MENTEE1 = 463,
    ABNS_BUFF_PCCAFE_EXP1 = 464,
    ABNS_SYNERGY_PARTY_BUF = 465,
    ABNS_NOTICE_PORTAL = 466,
    ABNS_EXP_BOTTLE = 467,
    ABNS_RESIST_DEATH = 468,
    ABNS_BUFF_RHAPSODY = 469,
    ABNS_NO_CHAT = 470,
    ABNS_FLAG_BUF = 471,
    ABNS_FLAG_DEBUF = 472,
    ABNS_BUFF_TREE = 473,
    ABNS_INSTANT_EVENT_BUFF1 = 474,
    ABNS_INSTANT_EVENT_BUFF2 = 475,
    ABNS_INSTANT_EVENT_BUFF3 = 476,
    ABNS_INSTANT_EVENT_BUFF4 = 477,
    ABNS_INSTANT_EVENT_BUFF5 = 478,
    ABNS_INSTANT_EVENT_BUFF6 = 479,
    ABNS_SEED_TALISMAN1 = 480,
    ABNS_SEED_TALISMAN2 = 481,
    ABNS_SEED_TALISMAN3 = 482,
    ABNS_SEED_TALISMAN4 = 483,
    ABNS_SEED_TALISMAN5 = 484,
    ABNS_SEED_TALISMAN6 = 485,
    ABNS_LUMIERE_BUFF = 486,
    ABNS_BRIGHTNESS_BLESS = 487,
    ABNS_CURIOUS_HOUSE = 488,
    ABNS_SPECIAL_MOVE_UP = 489,
    ABNS_STAR_AGATHION_EXP_SP_BUFF1 = 490,
    ABNS_NPC_MULTI_BUFF1 = 491,
    ABNS_DUAL_MAX_BODY = 492,
    ABNS_DUAL_ATTACK_UP = 493,
    ABNS_DUAL_SKILL_UP = 494,
    ABNS_DUAL_DEFENCE_UP = 495,
    ABNS_DUAL_DMG_SHIELD = 496,
    ABNS_KALIE_BUFF = 497,
    ABNS_BUFF_SPECIAL_EXP = 498,
    ABNS_DISPLAY_DEBUFF1 = 499,
    ABNS_NPC_BUFF1 = 500,
    ABNS_SOUL_SHIELD = 501,
    ABNS_UTHANKA_BUFF = 502,
    ABNS_KNIGHT_BUFF1 = 503,
    ABNS_KNIGHT_BUFF2 = 504,
    ABNS_KNIGHT_BUFF3 = 505,
    ABNS_KNIGHT_BUFF4 = 506,
    ABNS_PROVOKE = 507,
    ABNS_ARCHER_STANCE = 508,
    ABNS_RAGE_BUFF = 509,
    ABNS_ROGUE_MIGHT_MORTAL = 510,
    ABNS_WIZARD_BUFF1 = 511,
    ABNS_WIZARD_BUFF2 = 512,
    ABNS_WIZARD_BUFF3 = 513,
    ABNS_WIZARD_BUFF4 = 514,
    ABNS_ARCHER_BUFF1 = 515,
    ABNS_ARCHER_BUFF2 = 516,
    ABNS_ARCHER_BUFF3 = 517,
    ABNS_ARCHER_BUFF4 = 518,
    ABNS_SUMMON_BUFF1 = 519,
    ABNS_SUMMON_BUFF2 = 520,
    ABNS_SUMMON_BUFF3 = 521,
    ABNS_SUMMON_BUFF4 = 522,
    ABNS_PREMIUM_BUFF = 523,
    ABNS_WP_CHANGE_EVENT1 = 524,
    ABNS_LIND_ABNORMAL1 = 525,
    ABNS_LIND_ABNORMAL2 = 526,
    ABNS_LIND_ABNORMAL3 = 527,
    ABNS_FIELD_RAID_BUFF1 = 528,
    ABNS_UP_DMAGIC = 529,
    ABNS_LEGEND_CART_EVENT = 530,
    ABNS_SYSTEM_EVENT_BUF1 = 531,
    ABNS_SYSTEM_EVENT_BUF2 = 532,
    ABNS_SYSTEM_EVENT_BUF3 = 533,
    ABNS_SYSTEM_EVENT_BUF4 = 534,
    ABNS_SYSTEM_EVENT_BUF5 = 535,
    ABNS_ROGUE_DEBUFF1 = 536,
    ABNS_POWER_OF_ADVEN = 537,
    ABNS_POWER_OF_GHOST = 538,
    ABNS_CHANCE_OF_WIND = 539,
    ABNS_KNIGHT_DEBUFF1 = 540,
    ABNS_KNIGHT_DEBUFF2 = 541,
    ABNS_WARRIOR_BUFF1 = 542,
    ABNS_WARRIOR_BUFF2 = 543,
    ABNS_WARRIOR_DEBUFF1 = 544,
    ABNS_WARRIOR_DEBUFF2 = 545,
    ABNS_THIRST_DESERT = 546,
    ABNS_STAR_GAMBLE = 547,
    ABNS_DUAL_BUFF = 548,
    ABNS_DUAL_DEBUFF = 549,
    ABNS_CUBIC_BUFF1 = 550,
    ABNS_STAR_GAMBLE2 = 551,
    ABNS_SPECIAL_POTION1 = 552,
    ABNS_SPECIAL_POTION2 = 553,
    ABNS_SPECIAL_POTION3 = 554,
    ABNS_SYSTEM_EVENT_BUF6 = 555,
    ABNS_FISHING_USE = 556,
    ABNS_FISHING_BLESS = 557,
    ABNS_FISHING_FOOD = 558,
    ABNS_RAID_BLESSING = 559,
    ABNS_EX_RAID_BLESSING = 560,
    ABNS_BLUFF = 561,
    ABNS_TIR_BUFFING1 = 562,
    ABNS_TIR_BUFFING2 = 563,
    ABNS_SIGEL_BUFFING1 = 564,
    ABNS_SIGEL_BUFFING2 = 565,
    ABNS_OTHER_BUFFING1 = 566,
    ABNS_OTHER_BUFFING2 = 567,
    ABNS_YIR_BUFFING1 = 568,
    ABNS_YIR_BUFFING2 = 569,
    ABNS_FEO_BUFFING1 = 570,
    ABNS_FEO_BUFFING2 = 571,
    ABNS_WYNN_BUFFING1 = 572,
    ABNS_WYNN_BUFFING2 = 573,
    ABNS_WYNN_BUFFING3 = 574,
    ABNS_EOLH_BUFFING1 = 575,
    ABNS_EOLH_BUFFING2 = 576,
    ABNS_EOLH_BUFFING3 = 577,
    ABNS_IS_BUFFING1 = 578,
    ABNS_PSYCHIC_POWER = 579,
    ABNS_SIGH_OF_STORM = 580,
    ABNS_WIND_HIDE = 581,
    ABNS_STORM_EYE = 582,
    ABNS_ULTIMATE_SQUALL = 583,
    ABNS_SAYHA_BUFF = 584,
    ABNS_SAYHA_PARTY_BUFF = 585,
    ABNS_MENACE_MULTI_DEBUFF = 586,
    ABNS_PSYCHIC_DEBUFF = 587,
    ABNS_WIND_STUN = 588,
    ABNS_SYNERGY_LENKER = 589,
    ABNS_SYNERGY_SEER = 590,
    ABNS_GRAVITY_POWER = 591,
    ABNS_DISTORTION = 592,
    ABNS_GRAVITY_DEBUFF = 593,
    ABNS_GRAVITY_MULTI_DEBUFF = 594,
    ABNS_SPEED_DOWN_SPECIAL = 595,
    ABNS_SPEICAL_SPIRIT = 596,
    ABNS_GRAVITY_SPACE = 597,
    ABNS_BUFF_SPECIAL_EXP_VITAL = 598,
    ABNS_ARMOR_VISUAL_EFFECT = 599,
    ABNS_WEAPON_VISUAL_EFFECT = 600,
    ABNS_INVITE_COOKIE_BUFF1 = 601,
    ABNS_VP_RECOVER_BOOST = 602,
    ABNS_ADD_VITAL_BONUS = 603,
    ABNS_VP_CONSUME_REDUCE = 604,
    ABNS_BLOCKED_INVINCIBILITY = 605,
    ABNS_BATTLE_TOLERANCE = 606,
    ABNS_MYSTERIOUS_DYE = 607,
    ABNS_RICHNESS_TALISMAN = 608,
    ABNS_SAYHA_DEBUFF = 609,
    ABNS_ENCHANTER_MIGHT = 610,
    ABNS_ENCHANTER_SHIELD = 611,
    ABNS_ENCHANTER_TRAIT_BUFF1 = 612,
    ABNS_ENCHANTER_TRAIT_BUFF2 = 613,
    ABNS_ENCHANTER_DEBUFF1 = 614,
    ABNS_ENCHANTER_DEBUFF2 = 615,
    ABNS_HEALER_PROTECTION = 616,
    ABNS_HEALER_BUFFING = 617,
    ABNS_HEALER_BUFF1 = 618,
    ABNS_HEALER_DEBUFF1 = 619,
    ABNS_ABILITY_CHANGE_ITEMSKILL = 620,
    ABNS_PREVENTION_DEATH = 621,
    ABNS_ABSORB_SHIELD = 622,
    ABNS_DISROBE = 623,
    ABNS_DUAL_BUFF2 = 624,
    ABNS_BR_PCCAFE_BUFF_J = 625,
    ABNS_DMG_SHIELD1 = 626,
    ABNS_ASSIMILATE_PENALTY = 627,
    ABNS_LOOKCHANGE_BUFF1 = 628,
    ABNS_SUMMONER_BIGBODY1 = 629,
    ABNS_SUMMON_MASTER_BUFF1 = 630,
    ABNS_COMMON_BIGBODY1 = 631,
    ABNS_WIZARD_TRIGGER_BUFF1 = 632,
    ABNS_T_BLOW_RATE_UP = 633,
    ABNS_ALL_DEFENCE_DOWN = 634,
    ABNS_N20_FREE_BUFF01 = 635,
    ABNS_N20_FREE_BUFF02 = 636,
    ABNS_N20_FREE_BUFF03 = 637,
    ABNS_N20_BUFF_VITAL = 638,
    ABNS_N20_BUFF_BATTLE01 = 639,
    ABNS_N20_BUFF_BATTLE02 = 640,
    ABNS_N20_BUFF_BATTLE03 = 641,
    ABNS_N20_BUFF_WORLD_CHAT_POINT = 642,
    ABNS_CLAN_BONUS_BUFF1 = 643,
    ABNS_BUFF_PCCAFE_EXP2 = 644,
    ABNS_KNIGHT_DEBUFF_AURA1 = 645,
    ABNS_KNIGHT_DEBUFF_AURA2 = 646,
    ABNS_MAX = 647,
};


// The declarations that remain forward-only below still lack a closed
// complete current declaration boundary.
enum ActionType : std::int32_t;
// enum AdditionalArmorType : std::int32_t {
//     AAMT_NONE = 0,
//     AAMT_LIGHT = 1,
//     AAMT_HEAVY = 2,
//     AAMT_MAGIC = 3,
//     AAMT_SIGIL = 4,
//     AAMT_MAX = 5,
// }
enum ArmorType : std::int32_t {
    AMT_NONE = 0,
    AMT_LIGHT = 1,
    AMT_HEAVY = 2,
    AMT_MAGIC = 3,
    AMT_SIGIL = 4,
    AMT_MAX = 5,
};
// enum AttackType : std::int32_t {
//     AT_NONE = 0,
//     AT_SWORD = 1,
//     AT_TWOHANDSWORD = 2,
//     AT_BUSTER = 3,
//     AT_BLUNT = 4,
//     AT_TWOHANDBLUNT = 5,
//     AT_STAFF = 6,
//     AT_TWOHANDSTAFF = 7,
//     AT_DAGGER = 8,
//     AT_POLE = 9,
//     AT_FIST = 10,
//     AT_BOW = 11,
//     AT_ETC = 12,
//     AT_DUAL = 13,
//     AT_DUALFIST = 14,
//     AT_FISHINGROD = 15,
//     AT_RAPIER = 16,
//     AT_CROSSBOW = 17,
//     AT_ANCIENTSWORD = 18,
//     AT_FLAG = 19,
//     AT_DUALDAGGER = 20,
//     AT_OWNTHING = 21,
//     AT_TWOHANDCROSSBOW = 22,
//     AT_DUALBLUNT = 23,
//     AT_PISTOL = 24,
//     AT_MAX = 25,
// }
enum ClassType : std::int32_t;
enum CreatureMoveType : std::int32_t;
// Original Shared/BaseStructure.h: TotalChecker2015 IPI file 290, line 2094.
enum EnvType : std::int32_t {
    ENV_GROUND = 0,
    ENV_UNDERWATER = 1,
    ENV_AIR = 2,
    ENV_HOVER = 3,
    ENV_MAX = 4,
};

static_assert(ENV_GROUND == 0);
static_assert(ENV_UNDERWATER == 1);
static_assert(ENV_AIR == 2);
static_assert(ENV_HOVER == 3);
static_assert(ENV_MAX == 4);
enum MoveType : std::int32_t {
    MT_SLOW = 0,
    MT_FAST = 1,
    MT_END = 2,
};
enum MovingStatusEnum : std::int32_t {
    MSTE_MOVING = 0,
    MSTE_NOT_MOVING = 1,
};
// enum NpcSummonType : std::int32_t {
//     NST_NOTHING = 0,
//     NST_SUMMON = 1,
//     NST_PET = 2,
//     NST_PCTRAP = 3,
//     NST_DOPPELGANGER = 4,
//     NST_UNIQUE = 5,
//     NST_NOT_EVENT = 6,
// }

enum NpcEventFlag : std::int32_t {
    NEF_SEE_CREATURE = 0,
    NEF_MAX = 1,
};
enum RaceType : std::int32_t {
    RT_NONE = -1,
    RT_HUMAN = 0,
    RT_ELF = 1,
    RT_DARKELF = 2,
    RT_ORC = 3,
    RT_DWARF = 4,
    RT_KAMAEL = 5,
    RT_ERTHEIA = 6,
    RT_HUMANOID = 7,
    RT_ANIMAL = 8,
    RT_MONSTER = 9,
    RT_CREATURE = 10,
    RT_UNDEAD = 11,
    RT_ELEMENTAL = 12,
    RT_DEVIL = 13,
    RT_DRAGON = 14,
    RT_ANGEL = 15,
    RT_CONSTRUCT = 16,
    RT_BEAST = 17,
    RT_PLANT = 18,
    RT_DIVINE = 19,
    RT_DEMONIC = 20,
    RT_GIANT = 21,
    RT_BUG = 22,
    RT_FAIRY = 23,
    RT_SIEGE_WEAPON = 24,
    RT_MERCENARY = 25,
    RT_CASTLE_GUARD = 26,
    RT_MONRACE = 27,
    RT_ET_CETERA = 28,
    RT_FRIEND = 29,
    RT_MAX = 30,
};
enum RideType : std::int32_t {
    RIDE_NONE = 0,
    RIDE_STRIDER = 1,
    RIDE_WYVERN = 2,
    RIDE_WOLF = 3,
};
enum SexType : std::int32_t {
    ST_MALE = 0,
    ST_FEMALE = 1,
    ST_ETC = 2,
};
enum StopType : std::int32_t {
    STP_SIT = 0,
    STP_STAND = 1,
    STP_STORE = 2,
    STP_MAX = 3,
};


namespace PVPMatchType {
enum Enum : std::int32_t;
}

namespace PVPMatchPlayerState {
enum Enum : std::int32_t;
}

// Value types embedded in SharedFactory records. The names, member order,
// and widths come from the current TotalChecker2015 type stream.
struct DamageRangeType {
    DamageRangeType() : target(0), direction(0), distance(0), angle(0) {}

    std::int32_t target;
    std::int32_t direction;
    std::int32_t distance;
    std::int32_t angle;
};

struct AgitDecoEffect {
    AgitDecoEffect();

    void Init();

    double dHpBonus;
    double dMpBonus;
    double dExpRestore;
};

struct CStats {
    CStats();
    CStats(const CStats& other);
    CStats& operator=(const CStats& other);

    CStats operator-(const CStats& other) const;
    CStats operator+(const CStats& other) const;
    CStats& operator+=(const CStats& other);
    bool IsSameValue(const CStats& other);

    void Fix();
    void GuaranteeMax(int maximum);
    void Init();
    bool IsValid() const;
    void Report(wchar_t* sourceFile, int sourceLine, wchar_t* creatureName) const;

    std::int32_t m_N[8];
};

class AbnormalResist {
public:
    std::int32_t m_N[2];
};

// Windows' SYSTEMTIME has an invariant 16-byte wire/shared-memory layout.
// Windows uses the SDK type. The SDK has no include guard for this struct, so a
// second definition fails after Windows.h. Other hosts get an identical copy.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
struct _SYSTEMTIME {
    std::uint16_t wYear;
    std::uint16_t wMonth;
    std::uint16_t wDayOfWeek;
    std::uint16_t wDay;
    std::uint16_t wHour;
    std::uint16_t wMinute;
    std::uint16_t wSecond;
    std::uint16_t wMilliseconds;
};

using SYSTEMTIME = _SYSTEMTIME;
#endif


namespace base_structure_detail {

// Not named InterlockedIncrement: Windows.h defines that name as a macro.
inline std::int32_t AtomicIncrement(std::int32_t* value) noexcept {
#if defined(_MSC_VER)
    return static_cast<std::int32_t>(_InterlockedIncrement(reinterpret_cast<volatile long*>(value)));
#elif defined(__GNUC__) || defined(__clang__)
    return __atomic_add_fetch(value, std::int32_t{1}, __ATOMIC_SEQ_CST);
#else
    return ++*value;
#endif
}

inline std::int32_t LoadAfterInterlockedIncrement(const std::int32_t* value) noexcept {
#if defined(_MSC_VER)
    return *reinterpret_cast<const volatile std::int32_t*>(value);
#elif defined(__GNUC__) || defined(__clang__)
    return __atomic_load_n(value, __ATOMIC_RELAXED);
#else
    return *value;
#endif
}

inline void InterlockedStore(std::int32_t* value, std::int32_t replacement) noexcept {
#if defined(_MSC_VER)
    _InterlockedExchange(reinterpret_cast<volatile long*>(value), static_cast<long>(replacement));
#elif defined(__GNUC__) || defined(__clang__)
    __atomic_store_n(value, replacement, __ATOMIC_SEQ_CST);
#else
    *value = replacement;
#endif
}

} // namespace base_structure_detail

struct UserNPCLog {
    UserNPCLogType userLogType = USER_NPC_LOG_MAX;
    std::int32_t nNPCID = 0;
    std::int32_t nCount = 0;
};

struct MemoStruct {
    std::int32_t nQuestID = 0;
    std::int32_t nJournal = 1;
    std::int32_t nState = 0;
    std::int32_t nState2 = 0;
    std::int32_t nState3 = 0;
    std::int32_t nState4 = 0;
    bool bChanged = false;
    bool bDeleted = false;
    UserNPCLog NPCLog[10]{};

    bool DeleteNPCLog(UserNPCLogType userLogType, int npcId) noexcept {
        for (UserNPCLog& log : NPCLog) {
            if (log.userLogType != userLogType || log.nNPCID != npcId) {
                continue;
            }
            log.userLogType = USER_NPC_LOG_MAX;
            log.nNPCID = 0;
            base_structure_detail::InterlockedStore(&log.nCount, 0);
            return true;
        }
        return false;
    }

    int IncreaseNPCLog(UserNPCLogType userLogType, int npcId, int maximum) noexcept {
        for (UserNPCLog& log : NPCLog) {
            const int count = log.userLogType == userLogType && log.nNPCID == npcId ? log.nCount : 0;
            if (count > 0 && (maximum == 0 || count < maximum)) {
                base_structure_detail::AtomicIncrement(&log.nCount);
                return base_structure_detail::LoadAfterInterlockedIncrement(&log.nCount);
            }
        }

        // The shipping helper allocates a second type/id row
        // when the existing row has reached its cap.
        for (UserNPCLog& log : NPCLog) {
            if (log.nNPCID != 0) {
                continue;
            }
            log.userLogType = userLogType;
            log.nNPCID = npcId;
            base_structure_detail::InterlockedStore(&log.nCount, 1);
            return 1;
        }
        return 0;
    }

    bool UpdateNPCLog(UserNPCLogType userLogType, int npcId, int count) noexcept {
        // Current Cached.exe unnamed Shared/BaseStructure.h body 0x0059ceb4.
        UserNPCLog* destination = nullptr;
        for (UserNPCLog& log : NPCLog) {
            if (log.userLogType == userLogType && log.nNPCID == npcId) {
                destination = &log;
                break;
            }
        }
        if (!destination) {
            for (UserNPCLog& log : NPCLog) {
                if (log.nNPCID == 0) {
                    destination = &log;
                    break;
                }
            }
        }
        if (!destination) {
            return false;
        }
        destination->userLogType = userLogType;
        destination->nNPCID = npcId;
        base_structure_detail::InterlockedStore(&destination->nCount, count);
        return true;
    }
};


// Shared/BaseStructure.cpp globals. L2Server fills these arrays while parsing
// factiondata.txt. L2NPC only links the shared creature helpers that consume
// them.
extern std::int64_t* FactionByLevel[16];
extern int FactionMaxLevel[16];


// Standalone Shared/BaseStructure.cpp helpers. Their global scope is part
// of the original source contract. FVector now also keeps its PDB-confirmed
// global scope so both products consume one main layout.
bool HeroType_IsHero(int heroType);

int LeftTurn(const FVector& first, const FVector& second, const FVector& third);

void SwapFVector(FVector& first, FVector& second);

// Shared/BaseStructure.h line-segment helpers created by the current
// L2Server WorldCollision owner. The first consumes four borrowed vectors.
// the second takes copies because it replaces their z values
// with homogeneous-coordinate 1.0 before forming the intersection.
bool IsIntersectionNew(const FVector& first, const FVector& second, const FVector& third, const FVector& fourth);

FVector FindIntersectionPoint(FVector first, FVector second, FVector third, FVector fourth);

namespace ServiceVersion {

// Current L2Server.exe 0x004af5e4. The body is compiled in the L2Server
// physical Shared creation because its sole input is the L2Server
// config.cpp process global.
bool IsLiveTestVer();

} // namespace ServiceVersion
