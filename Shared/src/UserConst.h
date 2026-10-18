#pragma once

#include <cstdint>

namespace BlueRedTeam {

enum Enum : std::int32_t {
    BLUE = 0,
    RED = 1,
    MAX = 2,
};

} // namespace BlueRedTeam

namespace PVPMatchState {

enum Enum : std::int32_t {
    INVALID = 0,
    REGISTER = 1,
    COMBAT = 2,
    REWARD = 3,
};

} // namespace PVPMatchState


// Original Shared/UserConst.h vocabulary: TotalChecker2015 TPI 0x3D999
// and S_CONSTANT records. Corroborated by NASC64_2015 PDB/BSC.
// PCT_* are bit indices. User::ParamChanged consumes the PCB_* masks.
enum PARAM_CHANGED_TYPE : std::int32_t {
    PCT_NICKNAME = 0,
    PCT_PLEDGE = 1,
    PCT_TRANSFORM = 2,
    PCT_POLYMORPH = 3,
    PCT_USER = 4,
    PCT_CHAR = 5,
    PCT_CHAR_MARK = 6,
    PCT_CHAR_AIRSHIP = 7,
    PCT_CHAR_BYNPC = 8,
    PCT_CHAR_BUILDERCMD = 9,
    PCT_CHAR_CLEFT = 10,
    PCT_CHAR_KILL = 11,
    PCT_CHAR_DIE = 12,
    PCT_CHAR_EQUIPSLOT = 13,
    PCT_CHAR_MOVETYPE = 14,
    PCT_CHAR_ABNORMALVISUALEFFECT = 15,
    PCT_CHAR_ABNORMALSTATUS = 16,
    PCT_CHAR_CUBIC = 17,
    PCT_CHAR_ENV = 18,
    PCT_CHAR_SKILL = 19,
    PCT_CHAR_NICKNAME = 20,
    PCT_CHAR_SHAPESHIFT = 21,
    PCT_CHAR_BEAUTYSHOP = 22,
    PCT_CHAR_UPDATECHAR = 23,
    PCT_CHAR_PRIVATESTORE = 24,
    PCT_CHAR_NOBLESS = 25,
    PCT_CHAR_OLYMPIAD = 26,
    PCT_CHAR_PARTYMATCHING = 27,
    PCT_CHAR_PLEDGESKILLLEVEL = 28,
    PCT_CHAR_PLEDGEGAME = 29,
    PCT_CHAR_DOMINION = 30,
    PCT_CHAR_PARTYSUBSTITUTE = 31,
    PCT_CHAR_TRANSFORM = 32,
    PCT_CHAR_POLYMORPH = 33,
    PCT_CHAR_CARRYWEIGHT = 34,
    PCT_CHAR_SETTARGETABLE = 35,
    PCT_CHAR_SHOWNAMETAG = 36,
    PCT_CHAR_LECTUREMARK = 37,
    PCT_CHAR_DUEL = 38,
    PCT_CHAR_EVENTMATCH = 39,
    PCT_CHAR_KARMAUPDATE = 40,
    PCT_CHAR_SUBJOB = 41,
    PCT_CHAR_VITALITY = 42,
    PCT_CHAR_TELEPORT = 43,
    PCT_CHAR_PET = 44,
    PCT_CHAR_AGATHION = 45,
    PCT_CHAR_INSIGHT = 46,
    PCT_CHAR_FOOTEFFECT = 47,
    PCT_MAX = 48,
};

constexpr std::int64_t PCB_NICKNAME = std::int64_t{1} << PCT_NICKNAME;
constexpr std::int64_t PCB_PLEDGE = std::int64_t{1} << PCT_PLEDGE;
constexpr std::int64_t PCB_TRANSFORM = std::int64_t{1} << PCT_TRANSFORM;
constexpr std::int64_t PCB_POLYMORPH = std::int64_t{1} << PCT_POLYMORPH;
constexpr std::int64_t PCB_USER = std::int64_t{1} << PCT_USER;
constexpr std::int64_t PCB_CHAR = std::int64_t{1} << PCT_CHAR;
constexpr std::int64_t PCB_CHAR_MARK = std::int64_t{1} << PCT_CHAR_MARK;
constexpr std::int64_t PCB_CHAR_AIRSHIP = std::int64_t{1} << PCT_CHAR_AIRSHIP;
constexpr std::int64_t PCB_CHAR_BYNPC = std::int64_t{1} << PCT_CHAR_BYNPC;
constexpr std::int64_t PCB_CHAR_BUILDERCMD = std::int64_t{1} << PCT_CHAR_BUILDERCMD;
constexpr std::int64_t PCB_CHAR_CLEFT = std::int64_t{1} << PCT_CHAR_CLEFT;
constexpr std::int64_t PCB_CHAR_KILL = std::int64_t{1} << PCT_CHAR_KILL;
constexpr std::int64_t PCB_CHAR_DIE = std::int64_t{1} << PCT_CHAR_DIE;
constexpr std::int64_t PCB_CHAR_EQUIPSLOT = std::int64_t{1} << PCT_CHAR_EQUIPSLOT;
constexpr std::int64_t PCB_CHAR_MOVETYPE = std::int64_t{1} << PCT_CHAR_MOVETYPE;
constexpr std::int64_t PCB_CHAR_ABNORMALVISUALEFFECT = std::int64_t{1} << PCT_CHAR_ABNORMALVISUALEFFECT;
constexpr std::int64_t PCB_CHAR_ABNORMALSTATUS = std::int64_t{1} << PCT_CHAR_ABNORMALSTATUS;
constexpr std::int64_t PCB_CHAR_CUBIC = std::int64_t{1} << PCT_CHAR_CUBIC;
constexpr std::int64_t PCB_CHAR_ENV = std::int64_t{1} << PCT_CHAR_ENV;
constexpr std::int64_t PCB_CHAR_SKILL = std::int64_t{1} << PCT_CHAR_SKILL;
constexpr std::int64_t PCB_CHAR_NICKNAME = std::int64_t{1} << PCT_CHAR_NICKNAME;
constexpr std::int64_t PCB_CHAR_SHAPESHIFT = std::int64_t{1} << PCT_CHAR_SHAPESHIFT;
constexpr std::int64_t PCB_CHAR_BEAUTYSHOP = std::int64_t{1} << PCT_CHAR_BEAUTYSHOP;
constexpr std::int64_t PCB_CHAR_UPDATECHAR = std::int64_t{1} << PCT_CHAR_UPDATECHAR;
constexpr std::int64_t PCB_CHAR_PRIVATESTORE = std::int64_t{1} << PCT_CHAR_PRIVATESTORE;
constexpr std::int64_t PCB_CHAR_NOBLESS = std::int64_t{1} << PCT_CHAR_NOBLESS;
constexpr std::int64_t PCB_CHAR_OLYMPIAD = std::int64_t{1} << PCT_CHAR_OLYMPIAD;
constexpr std::int64_t PCB_CHAR_PARTYMATCHING = std::int64_t{1} << PCT_CHAR_PARTYMATCHING;
constexpr std::int64_t PCB_CHAR_PLEDGESKILLLEVEL = std::int64_t{1} << PCT_CHAR_PLEDGESKILLLEVEL;
constexpr std::int64_t PCB_CHAR_PLEDGEGAME = std::int64_t{1} << PCT_CHAR_PLEDGEGAME;
constexpr std::int64_t PCB_CHAR_DOMINION = std::int64_t{1} << PCT_CHAR_DOMINION;
constexpr std::int64_t PCB_CHAR_PARTYSUBSTITUTE = std::int64_t{1} << PCT_CHAR_PARTYSUBSTITUTE;
constexpr std::int64_t PCB_CHAR_TRANSFORM = std::int64_t{1} << PCT_CHAR_TRANSFORM;
constexpr std::int64_t PCB_CHAR_POLYMORPH = std::int64_t{1} << PCT_CHAR_POLYMORPH;
constexpr std::int64_t PCB_CHAR_CARRYWEIGHT = std::int64_t{1} << PCT_CHAR_CARRYWEIGHT;
constexpr std::int64_t PCB_CHAR_SETTARGETABLE = std::int64_t{1} << PCT_CHAR_SETTARGETABLE;
constexpr std::int64_t PCB_CHAR_SHOWNAMETAG = std::int64_t{1} << PCT_CHAR_SHOWNAMETAG;
constexpr std::int64_t PCB_CHAR_LECTUREMARK = std::int64_t{1} << PCT_CHAR_LECTUREMARK;
constexpr std::int64_t PCB_CHAR_DUEL = std::int64_t{1} << PCT_CHAR_DUEL;
constexpr std::int64_t PCB_CHAR_EVENTMATCH = std::int64_t{1} << PCT_CHAR_EVENTMATCH;
constexpr std::int64_t PCB_CHAR_KARMAUPDATE = std::int64_t{1} << PCT_CHAR_KARMAUPDATE;
constexpr std::int64_t PCB_CHAR_SUBJOB = std::int64_t{1} << PCT_CHAR_SUBJOB;
constexpr std::int64_t PCB_CHAR_VITALITY = std::int64_t{1} << PCT_CHAR_VITALITY;
constexpr std::int64_t PCB_CHAR_TELEPORT = std::int64_t{1} << PCT_CHAR_TELEPORT;
constexpr std::int64_t PCB_CHAR_PET = std::int64_t{1} << PCT_CHAR_PET;
constexpr std::int64_t PCB_CHAR_AGATHION = std::int64_t{1} << PCT_CHAR_AGATHION;
constexpr std::int64_t PCB_CHAR_INSIGHT = std::int64_t{1} << PCT_CHAR_INSIGHT;
constexpr std::int64_t PCB_CHAR_FOOTEFFECT = std::int64_t{1} << PCT_CHAR_FOOTEFFECT;
constexpr std::int64_t PCB_MAX = std::int64_t{1} << PCT_MAX;

// Independent literal values from the donor's S_CONSTANT records.
static_assert(PCT_NICKNAME == 0 && PCB_NICKNAME == INT64_C(0x1));
static_assert(PCT_PLEDGE == 1 && PCB_PLEDGE == INT64_C(0x2));
static_assert(PCT_TRANSFORM == 2 && PCB_TRANSFORM == INT64_C(0x4));
static_assert(PCT_POLYMORPH == 3 && PCB_POLYMORPH == INT64_C(0x8));
static_assert(PCT_USER == 4 && PCB_USER == INT64_C(0x10));
static_assert(PCT_CHAR == 5 && PCB_CHAR == INT64_C(0x20));
static_assert(PCT_CHAR_MARK == 6 && PCB_CHAR_MARK == INT64_C(0x40));
static_assert(PCT_CHAR_AIRSHIP == 7 && PCB_CHAR_AIRSHIP == INT64_C(0x80));
static_assert(PCT_CHAR_BYNPC == 8 && PCB_CHAR_BYNPC == INT64_C(0x100));
static_assert(PCT_CHAR_BUILDERCMD == 9 && PCB_CHAR_BUILDERCMD == INT64_C(0x200));
static_assert(PCT_CHAR_CLEFT == 10 && PCB_CHAR_CLEFT == INT64_C(0x400));
static_assert(PCT_CHAR_KILL == 11 && PCB_CHAR_KILL == INT64_C(0x800));
static_assert(PCT_CHAR_DIE == 12 && PCB_CHAR_DIE == INT64_C(0x1000));
static_assert(PCT_CHAR_EQUIPSLOT == 13 && PCB_CHAR_EQUIPSLOT == INT64_C(0x2000));
static_assert(PCT_CHAR_MOVETYPE == 14 && PCB_CHAR_MOVETYPE == INT64_C(0x4000));
static_assert(PCT_CHAR_ABNORMALVISUALEFFECT == 15 && PCB_CHAR_ABNORMALVISUALEFFECT == INT64_C(0x8000));
static_assert(PCT_CHAR_ABNORMALSTATUS == 16 && PCB_CHAR_ABNORMALSTATUS == INT64_C(0x10000));
static_assert(PCT_CHAR_CUBIC == 17 && PCB_CHAR_CUBIC == INT64_C(0x20000));
static_assert(PCT_CHAR_ENV == 18 && PCB_CHAR_ENV == INT64_C(0x40000));
static_assert(PCT_CHAR_SKILL == 19 && PCB_CHAR_SKILL == INT64_C(0x80000));
static_assert(PCT_CHAR_NICKNAME == 20 && PCB_CHAR_NICKNAME == INT64_C(0x100000));
static_assert(PCT_CHAR_SHAPESHIFT == 21 && PCB_CHAR_SHAPESHIFT == INT64_C(0x200000));
static_assert(PCT_CHAR_BEAUTYSHOP == 22 && PCB_CHAR_BEAUTYSHOP == INT64_C(0x400000));
static_assert(PCT_CHAR_UPDATECHAR == 23 && PCB_CHAR_UPDATECHAR == INT64_C(0x800000));
static_assert(PCT_CHAR_PRIVATESTORE == 24 && PCB_CHAR_PRIVATESTORE == INT64_C(0x1000000));
static_assert(PCT_CHAR_NOBLESS == 25 && PCB_CHAR_NOBLESS == INT64_C(0x2000000));
static_assert(PCT_CHAR_OLYMPIAD == 26 && PCB_CHAR_OLYMPIAD == INT64_C(0x4000000));
static_assert(PCT_CHAR_PARTYMATCHING == 27 && PCB_CHAR_PARTYMATCHING == INT64_C(0x8000000));
static_assert(PCT_CHAR_PLEDGESKILLLEVEL == 28 && PCB_CHAR_PLEDGESKILLLEVEL == INT64_C(0x10000000));
static_assert(PCT_CHAR_PLEDGEGAME == 29 && PCB_CHAR_PLEDGEGAME == INT64_C(0x20000000));
static_assert(PCT_CHAR_DOMINION == 30 && PCB_CHAR_DOMINION == INT64_C(0x40000000));
static_assert(PCT_CHAR_PARTYSUBSTITUTE == 31 && PCB_CHAR_PARTYSUBSTITUTE == INT64_C(0x80000000));
static_assert(PCT_CHAR_TRANSFORM == 32 && PCB_CHAR_TRANSFORM == INT64_C(0x100000000));
static_assert(PCT_CHAR_POLYMORPH == 33 && PCB_CHAR_POLYMORPH == INT64_C(0x200000000));
static_assert(PCT_CHAR_CARRYWEIGHT == 34 && PCB_CHAR_CARRYWEIGHT == INT64_C(0x400000000));
static_assert(PCT_CHAR_SETTARGETABLE == 35 && PCB_CHAR_SETTARGETABLE == INT64_C(0x800000000));
static_assert(PCT_CHAR_SHOWNAMETAG == 36 && PCB_CHAR_SHOWNAMETAG == INT64_C(0x1000000000));
static_assert(PCT_CHAR_LECTUREMARK == 37 && PCB_CHAR_LECTUREMARK == INT64_C(0x2000000000));
static_assert(PCT_CHAR_DUEL == 38 && PCB_CHAR_DUEL == INT64_C(0x4000000000));
static_assert(PCT_CHAR_EVENTMATCH == 39 && PCB_CHAR_EVENTMATCH == INT64_C(0x8000000000));
static_assert(PCT_CHAR_KARMAUPDATE == 40 && PCB_CHAR_KARMAUPDATE == INT64_C(0x10000000000));
static_assert(PCT_CHAR_SUBJOB == 41 && PCB_CHAR_SUBJOB == INT64_C(0x20000000000));
static_assert(PCT_CHAR_VITALITY == 42 && PCB_CHAR_VITALITY == INT64_C(0x40000000000));
static_assert(PCT_CHAR_TELEPORT == 43 && PCB_CHAR_TELEPORT == INT64_C(0x80000000000));
static_assert(PCT_CHAR_PET == 44 && PCB_CHAR_PET == INT64_C(0x100000000000));
static_assert(PCT_CHAR_AGATHION == 45 && PCB_CHAR_AGATHION == INT64_C(0x200000000000));
static_assert(PCT_CHAR_INSIGHT == 46 && PCB_CHAR_INSIGHT == INT64_C(0x400000000000));
static_assert(PCT_CHAR_FOOTEFFECT == 47 && PCB_CHAR_FOOTEFFECT == INT64_C(0x800000000000));
static_assert(PCT_MAX == 48 && PCB_MAX == INT64_C(0x1000000000000));

// Original Shared/UserConst.h: TotalChecker2015 and NASC64_2015
// IPI source-line records bind both validation indices and masks here.

enum VALIDATE_TYPE : std::int32_t {
    VT_BASEDATA = 0,
    VT_BASEATTR = 1,
    VT_LEVEL = 2,
    VT_ENV = 3,
    VT_MOTION = 4,
    VT_WEAPONARMOR = 5,
    VT_ITEMSKILL = 6,
    VT_SKILLLIST = 7,
    VT_SKILLMOD = 8,
    VT_HPMPCP = 9,
    VT_SIM = 10,
    VT_SPEED = 11,
    VT_LEVELUP = 12,
    VT_MAX = 13,
};

static_assert(VT_BASEDATA == 0);
static_assert(VT_BASEATTR == 1);
static_assert(VT_LEVEL == 2);
static_assert(VT_ENV == 3);
static_assert(VT_MOTION == 4);
static_assert(VT_WEAPONARMOR == 5);
static_assert(VT_ITEMSKILL == 6);
static_assert(VT_SKILLLIST == 7);
static_assert(VT_SKILLMOD == 8);
static_assert(VT_HPMPCP == 9);
static_assert(VT_SIM == 10);
static_assert(VT_SPEED == 11);
static_assert(VT_LEVELUP == 12);
static_assert(VT_MAX == 13);

enum VALIDATE_BITFLAG : std::int32_t {
    VB_BASEDATA = 1,
    VB_BASEATTR = 2,
    VB_LEVEL = 4,
    VB_ENV = 8,
    VB_MOTION = 16,
    VB_WEAPONARMOR = 32,
    VB_ITEMSKILL = 64,
    VB_SKILLLIST = 128,
    VB_SKILLMOD = 256,
    VB_HPMPCP = 512,
    VB_SIM = 1024,
    VB_SPEED = 2048,
    VB_LEVELUP = 4096,
    VB_MAX = 8192,
};

static_assert(VB_BASEDATA == 1);
static_assert(VB_BASEATTR == 2);
static_assert(VB_LEVEL == 4);
static_assert(VB_ENV == 8);
static_assert(VB_MOTION == 16);
static_assert(VB_WEAPONARMOR == 32);
static_assert(VB_ITEMSKILL == 64);
static_assert(VB_SKILLLIST == 128);
static_assert(VB_SKILLMOD == 256);
static_assert(VB_HPMPCP == 512);
static_assert(VB_SIM == 1024);
static_assert(VB_SPEED == 2048);
static_assert(VB_LEVELUP == 4096);
static_assert(VB_MAX == 8192);

namespace ValidateEx {

enum VALIDATE_EX_TYPE : std::int32_t {
    VET_SET_ITEM = 0,
    VET_COPY_BUFF_TO_SUMMON = 1,
    VET_MAX = 2,
};

static_assert(VET_SET_ITEM == 0);
static_assert(VET_COPY_BUFF_TO_SUMMON == 1);
static_assert(VET_MAX == 2);

} // namespace ValidateEx

namespace ValidateEx {

enum VALIDATE_EX_BITFLAG : std::int32_t {
    VEB_SET_ITEM = 1,
    VEB_COPY_BUFF_TO_SUMMON = 2,
    VEB_MAX = 4,
};

static_assert(VEB_SET_ITEM == 1);
static_assert(VEB_COPY_BUFF_TO_SUMMON == 2);
static_assert(VEB_MAX == 4);

} // namespace ValidateEx

// Original Shared/UserConst.h: TotalChecker2015 source lines 375 and 401.
namespace PVPMatchType {

enum Enum : std::int32_t {
    NONE = 0,
    CRATAE_CUBE = 1,
    CLEFT = 2,
    BLOCK_UPSET = 3,
    OLYMPIAD = 4,
    UNDERGROUND_COLOSSEUM = 5,
    PLEDGEGAME = 6,
    MAX = 7,
};

} // namespace PVPMatchType

namespace PVPMatchPlayerState {

enum Enum : std::int32_t {
    NONE = 0,
    WAITING = 1,
    COMBAT = 2,
    REWARD = 3,
};

} // namespace PVPMatchPlayerState
