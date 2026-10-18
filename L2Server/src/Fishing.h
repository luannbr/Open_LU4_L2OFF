#pragma once

#include "Object.h"

#include "Shared/src/StandardContainers.h"
#include "Shared/src/Territory.h"
#include "Shared/src/ThreadSafeLibrary.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

class User;

enum FishingStateEnum : std::int32_t {
    FISHINGS_NONE = 0,
    FISHINGS_WAIT = 1,
    FISHINGS_BATTLE = 2,
    FISHINGS_MAX = 3,
};

struct DistributionStruct {
    int GroupID;
    int Probability;
};

class CFishingDistributionInfo {
public:
    CFishingDistributionInfo() : m_nId(0) {}

    int m_nId;
    std::array<std::byte, 0x04> m_padding0004;
    std::vector<DistributionStruct*> m_vDefaultDistribution;
    std::vector<DistributionStruct*> m_vEventDistribution;
};

class CFishingPlaceInfo {
public:
    CFishingPlaceInfo() :
        m_nId(0),
        m_pTerritory(nullptr),
        m_nLimitGrid(0),
        m_nDistributionId(0),
        m_nPlaceType(0),
        m_nMaintainTime(0),
        m_nFishSpecies(0),
        m_nTerritoryGroup(0) {}

    int m_nId;
    std::array<std::byte, 0x04> m_padding0004;
    CTerritory* m_pTerritory;
    int m_nLimitGrid;
    int m_nDistributionId;
    int m_nPlaceType;
    int m_nMaintainTime;
    int m_nFishSpecies;
    int m_nTerritoryGroup;
};

class CFishingLureInfo {
public:
    using DistributionList = std::vector<DistributionStruct*>;
    using PreferenceMap = std::map<int, DistributionList>;

    CFishingLureInfo() :
        m_nId(0),
        m_nItemId(0),
        m_dRevision(0.0),
        m_nLengthBonus(0),
        m_dLengthRateBonus(0.0),
        m_nLureType(0),
        m_nLureGrade(0) {}

    int m_nId;
    int m_nItemId;
    double m_dRevision;
    int m_nLengthBonus;
    std::array<std::byte, 0x04> m_padding0014;
    double m_dLengthRateBonus;
    int m_nLureType;
    int m_nLureGrade;
    PreferenceMap m_mPreference;
};

class CFishingMonsterInfo {
public:
    CFishingMonsterInfo() : m_nBegin(0), m_nEnd(0), m_dProbability(0.0) {}

    int m_nBegin;
    int m_nEnd;
    double m_dProbability;
    std::vector<int> m_vMonsterId;
};

class CFishingSkillInfo {
public:
    CFishingSkillInfo() : m_nID(0) {}

    int m_nID;
    std::array<std::byte, 0x04> m_padding0004;
    std::vector<int> m_vFishId;
};

class CFishingRodInfo {
public:
    CFishingRodInfo() : m_nId(0), m_nItemId(0), m_nLevel(0), m_dDamage(0.0), m_nFishingDurationBonus(0) {}

    ~CFishingRodInfo() = default;

    int m_nId;
    int m_nItemId;
    int m_nLevel;
    double m_dDamage;
    int m_nFishingDurationBonus;
};

class CFishInfo {
public:
    CFishInfo() :
        m_nId(0),
        m_nItemId(0),
        m_nFishGroupId(0),
        m_nFishLevel(0),
        m_dFishGuts(0.0),
        m_nHp(0),
        m_dHpRegen(0.0),
        m_nStartCombatTime(0),
        m_nCombatDuration(0),
        m_nGutsCheckTime(0),
        m_dGutsCheckProbability(0.0),
        m_dCheatingProbability(0.0),
        m_nFishGrade(0),
        m_dBiteRate(0.0),
        m_nLengthMax(0),
        m_dLengthRate(0.0),
        m_dFishIntelligence(0.0),
        m_nFishSpecies(0),
        m_nExp(0),
        m_nSp(0) {}

    ~CFishInfo() = default;

    int m_nId;
    int m_nItemId;
    int m_nFishGroupId;
    int m_nFishLevel;
    double m_dFishGuts;
    int m_nHp;
    double m_dHpRegen;
    int m_nStartCombatTime;
    int m_nCombatDuration;
    int m_nGutsCheckTime;
    double m_dGutsCheckProbability;
    double m_dCheatingProbability;
    int m_nFishGrade;
    double m_dBiteRate;
    int m_nLengthMax;
    double m_dLengthRate;
    double m_dFishIntelligence;
    int m_nFishSpecies;
    std::int64_t m_nExp;
    std::int64_t m_nSp;
};

class CFishingDB {
public:
    using FishMap = std::map<int, CFishInfo*>;
    using PlaceMap = std::map<int, CFishingPlaceInfo*>;
    using DistributionMap = std::map<int, CFishingDistributionInfo*>;
    using LureMap = std::map<int, CFishingLureInfo*>;
    using RodMap = std::map<int, CFishingRodInfo*>;
    using MonsterList = std::vector<CFishingMonsterInfo*>;
    using SkillMap = std::map<int, CFishingSkillInfo*>;
    using FishList = std::vector<CFishInfo*>;

    CFishingDB();
    ~CFishingDB();

    void Add(CFishInfo* info);
    void Add(CFishingPlaceInfo* info);
    void Add(CFishingDistributionInfo* info);
    void Add(CFishingLureInfo* info);
    void Add(CFishingRodInfo* info);
    void Add(CFishingMonsterInfo* info);
    void Add(CFishingSkillInfo* info);

    CFishInfo* GetRandomFishByGroup(int group) const;
    CFishingPlaceInfo* FindPlace(const FVector& position, int placeType) const;
    CFishingRodInfo* FindFishingRod(int itemId) const;
    CFishingLureInfo* FindLure(int itemId) const;
    bool PostLoad();
    void Load();

    FishMap m_mapFishInfo;
    PlaceMap m_mapFishingPlaceInfo;
    DistributionMap m_mapFishingDistributionInfo;
    LureMap m_mapFishingLureInfo;
    RodMap m_mapFishingRodInfo;
    MonsterList m_vFishingMonsterInfo;
    SkillMap m_mapFishingSkillInfo;
    double m_dFishingProbablityProb;
    FishList m_vFishInfo[500];
    int m_nWaitNextFishing;
    std::array<std::byte, 0x04> m_tailPadding3f6c;
};

extern CFishingDB g_FishingDB;

// Current L2Server.exe constructs this exact 0x17b8-byte owner at 0x00733cec
// and reads its fishing state at +0x1660 and auto-fishing flag at +0x17b1.
// TotalChecker2015 supplies the source identifiers and access specifiers only.
// every published extent below is used by the current image.
class CFishingController : public CIOObject {
public:
    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_DoAutoFishing, bool);

    L2_ASYNC_DECLARE_FIELDS_1(__async_arg_AsyncTimerExpired, int);

    CFishingController();
    ~CFishingController() override;

    L2_DECLARE_OBJECT_TYPE(CFishingController)

    void SetActor(User* actor);
    User* GetActor() const { return reinterpret_cast<User*>(m_spActor.GetCreature()); }

    void SetNoEndingAction();
    void GetFishingInfoType();
    void DoAutoFishing(bool start);
    void TimerExpired(int id) override;

protected:
    void __async_DoAutoFishing(CExecutionArgumentPad* argument);
    void __async_AsyncTimerExpired(CExecutionArgumentPad* argument);
    bool StartFishing();
    bool EndFishing();
    bool InitFishingData();
    bool CheckFishingCondition(int& messageId);
    bool SetFishingInfo();
    bool IsWaterPlace(FVector& position, User* actor);
    bool SendFishingStartPacket();
    bool SendFishingEndPacket();
    bool SendFishingStartCombatPacket();

    CSmartID m_spActor; // +0x0018
    std::array<std::byte, 0x04> m_alignment001cTo0020;
    CExecutionSerializer<CFishingController, 64> Serializer; // +0x0020

public:
    FishingStateEnum m_FishingState; // +0x1660
    std::array<std::byte, 0x04> m_alignment1664To1668;
    FVector m_vFishingPos; // +0x1668
    std::int32_t m_nLureId; // +0x1680
    std::array<std::byte, 0x04> m_alignment1684To1688;
    double m_dFishingRodDamage; // +0x1688
    std::int32_t m_nExperienceLevel; // +0x1690
    std::int32_t m_nReelingLevel; // +0x1694
    std::int32_t m_nPumpingLevel; // +0x1698
    std::array<std::byte, 0x04> m_alignment169cTo16a0;
    double m_dFishingShotBonus; // +0x16a0
    double m_dExperienceBonus; // +0x16a8
    double m_dReelingBonus; // +0x16b0
    double m_dPumpingBonus; // +0x16b8
    std::int32_t m_FishingStartTime; // +0x16c0
    std::int32_t m_FishingEndTime; // +0x16c4
    std::int32_t m_nFishingCount; // +0x16c8
    std::int32_t m_nFishGroupId; // +0x16cc
    std::int32_t m_nFishId; // +0x16d0
    bool m_bFishAlive; // +0x16d4
    std::array<std::byte, 0x03> m_alignment16d5To16d8;
    std::int32_t m_nFishHP; // +0x16d8
    std::int32_t m_nFullFishHP; // +0x16dc
    double m_dFishHPRegen; // +0x16e0
    double m_dGutsProb; // +0x16e8
    double m_dCheatingProb; // +0x16f0
    std::int32_t m_nGutsCheckTime; // +0x16f8
    std::int32_t m_nGuts; // +0x16fc
    std::int32_t m_nCheating; // +0x1700
    std::int32_t m_nCheatingCount; // +0x1704
    std::int32_t m_nGutsCount; // +0x1708
    std::int32_t m_nTimeCount; // +0x170c
    std::int32_t m_nSuccessFishing; // +0x1710
    std::int32_t m_nItemId; // +0x1714
    std::int32_t m_nFishGrade; // +0x1718
    std::uint32_t m_lFishingInfo; // +0x171c
    bool m_bInstantKillFlg; // +0x1720
    std::array<std::byte, 0x03> m_alignment1721To1724;
    std::int32_t m_nFishingCountSamePlace; // +0x1724
    std::int32_t m_nEffect; // +0x1728
    std::int32_t m_nMonsterID; // +0x172c
    double m_dMonsterProb; // +0x1730
    bool m_bEquipPenalty; // +0x1738
    std::array<std::byte, 0x03> m_alignment1739To173c;
    std::int32_t m_nPenalty; // +0x173c
    std::int32_t m_nFishLengthMax; // +0x1740
    std::array<std::byte, 0x04> m_alignment1744To1748;
    double m_dFishLengthRate; // +0x1748
    std::int32_t m_nLureLengthBonus; // +0x1750
    std::array<std::byte, 0x04> m_alignment1754To1758;
    double m_dLureLengthRateBonus; // +0x1758
    std::int32_t m_nLureType; // +0x1760
    char16_t m_szIsNight[10]; // +0x1764
    CFishingPlaceInfo* m_pFishingPlaceInfo; // +0x1778
    CFishingDistributionInfo* m_pFishingDistributionInfo; // +0x1780
    CFishInfo* m_pFishInfo; // +0x1788
    CFishingLureInfo* m_pFishingLureInfo; // +0x1790
    CFishingRodInfo* m_pFishingRodInfo; // +0x1798
    CFishingMonsterInfo* m_pFishingMonsterInfo; // +0x17a0
    CFishingSkillInfo* m_pFishingSkillInfo; // +0x17a8
    bool m_bIsWaitNextAutoFishing; // +0x17b0
    bool m_bIsAutoFishing; // +0x17b1
    std::array<std::byte, 0x06> m_tailPadding17b2To17b8;
};


static_assert(std::is_trivially_copyable_v<DistributionStruct>);
static_assert(std::is_trivially_destructible_v<DistributionStruct>);

static_assert(std::is_trivially_copyable_v<CFishingRodInfo>);
static_assert(std::is_trivially_destructible_v<CFishingRodInfo>);

static_assert(std::is_trivially_copyable_v<CFishInfo>);
static_assert(std::is_trivially_destructible_v<CFishInfo>);
