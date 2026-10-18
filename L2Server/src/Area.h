#pragma once

#include "FlyMove.h"
#include "InstantZone.h"

#include "Shared/src/GeoData.h"
#include "Shared/src/IOC.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Territory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>


class CCreature;
class CCreatureAreaMod;
class CEventController;
class CParserForArea;
class CSkillInfo;
class CUserSocket;
class User;
enum AreaTypeEnum : std::int32_t;
enum DuelStartErrorType : std::int32_t;
enum RangeTargetType : std::int32_t;
enum SkillTargetTypeEnum : std::int32_t;

// Current Area.obj 0x020f4fbc..0x020f4fcc. The matching PDB supplies the
// original source spellings. CAreaDB::AddArea and ListenThread_common close
// every producer/consumer edge in the current image.
extern std::uint32_t totalAddAreaTime;
extern std::uint32_t totalAddAreaTime1;
extern std::uint32_t totalAddAreaTime2;
extern std::uint32_t totalAddAreaTime3;
extern int totalADDAreaCallCount;

bool BuilderCmd_setarea(CUserSocket*, User*, char16_t*);
bool BuilderCmd_setarea_all(CUserSocket*, User*, char16_t*);

namespace AreaBasedRule {

namespace details {
bool IsInPeaceField(std::uint32_t inZoneId, FVector* position);
double GetExpPenaltyRatio(std::uint32_t inZoneId, FVector* position, AreaTypeEnum areaType);
} // namespace details

bool BotReport(const CCreature* creature);
int Attackable(CCreature* relatedCreature, CCreature* attacker, CCreature* target);
bool CastBadSkill(CCreature* creature);
bool CancelPost(CCreature* creature);
bool KilledByPC(std::uint32_t inZoneId, FVector* position);
bool PetKilledByPC(const CCreature* creature);
bool ExpDownPanelty(CCreature* creature, std::int64_t defaultPenalty, std::int64_t* adjustedPenalty);
bool CompassPeace(const CGeoSector* sector, std::uint32_t worldId, FVector* position);
bool RechargeVitality(const CCreatureAreaMod& areaMod);
bool SpendPlaytime(const CCreatureAreaMod& areaMod);
bool UpdateTimeChangePeaceZone(const CCreature* creature);
DuelStartErrorType DuelStart(CCreature* creature);
bool KarmaPenalty(CCreature* creature);
bool PrivateRecipeShop(CCreature* creature);
bool PrivateStore(CCreature* creature);
bool StartIn(const CGeoSector* sector, const FVector& position);
bool TakeBadSkill(CCreature* creature, CCreature* target);
RangeTargetType RangeTargetOfSkill(CCreature* creature, CCreature* target, SkillTargetTypeEnum targetType);

} // namespace AreaBasedRule

enum AreaTypeEnum : std::int32_t {
    AREAT_BATTLE = 0,
    AREAT_FREEZE = 1,
    AREAT_PEACE = 2,
    AREAT_WATER = 3,
    AREAT_ONLY_PVE = 4,
    AREAT_SWAMP = 5,
    AREAT_MOTHER_TREE = 6,
    AREAT_NO_RESTART = 7,
    AREAT_DAMAGE = 8,
    AREAT_POISON = 9,
    AREAT_TELEPORT = 10,
    AREAT_INSTANT_SKILL = 11,
    AREAT_SSQ = 12,
    AREAT_AIRPORT = 13,
    AREAT_ENTER_INZONE = 14,
    AREAT_LEAVE_INZONE = 15,
    AREAT_FLYMOVE = 16,
    AREAT_SHUTTLE = 17,
    AREAT_DRAGON_LAIR = 18,
    AREAT_NO_SKILL = 19,
    AREAT_SIEGE = 20,
    AREAT_FISHING = 21,
    AREAT_TRAINING_ZONE = 22,
    AREAT_MAX = 23,
};

enum AreaTargetTypeEnum : std::int32_t {
    AREATT_ALL = 0,
    AREATT_PC = 1,
    AREATT_NPC = 2,
    AREATT_ONLY_PC = 3,
};

enum AreaSkillActionType : std::int32_t {
    AST_RANDOM1 = 0,
    AST_SERIAL = 1,
    AST_MAX = 2,
};


class CArea : public CIOObject {
public:
    L2_DECLARE_OBJECT_REGISTRATION(CArea)

    CArea();
    ~CArea() override;

    L2_DECLARE_OBJECT_TYPE_API_OUT_OF_LINE()
    void TimerExpired(int id) override;

    bool IsOn(int worldId);
    void MakeEventController(int eventId);
    bool SetBannedTerritoryOnOff(int index, bool on);
    bool IsBannedTerritoryOn(int index);
    bool IsBannedPoint(const FVector& position, bool includeZ);
    void SetShowDangerZone(bool enabled);
    void SetOnOff(bool on);
    void Process(CCreature* creature);
    bool CheckAndProcess(CCreature& creature, const FVector& position);
    void SetAreaType(AreaTypeEnum type);
    void SetAreaTargetType(AreaTargetTypeEnum type);
    void SetSkillActionType(AreaSkillActionType type);
    void SetUnitTick(int unitTick);
    void SetSkillProb(int probability);
    void AddSkillInfo(CSkillInfo* skillInfo);
    void AddSkillInfo(std::vector<std::int64_t>& skillKeys);
    bool HPMPRegenOnLevelUp() const { return mHPMPRegenOnLevelUp; }
    void HPMPRegenOnLevelUp(const bool enabled) { mHPMPRegenOnLevelUp = enabled; }
    void SetRestartAllowedTime(const int value) { mRestartAllowedTime = value; }
    int GetRestartAllowedTime() const { return mRestartAllowedTime; }
    void SetRestartTime(const int value) { m_nRestartTime = value; }
    int GetRestartTime() const { return m_nRestartTime; }
    FVector GetRandomRestartPoint(RaceType race, FVector position, bool chao, int pkCount);

    GeoFieldTypeEnum m_FieldType;
    std::array<bool, 30> m_vAffectRace;
    std::int32_t m_nMoveBonus;
    std::int32_t m_nDamageOnHP;
    std::int32_t m_nDamageOnMP;
    std::int32_t m_nHpRegenBonus;
    std::int32_t m_nMpRegenBonus;
    bool m_bIsOn;
    bool m_bSSQArea;
    bool m_bShowDangerZone;
    std::byte m_padding053;
    std::int32_t m_nMessageNO;
    std::int32_t m_nEnteringMessageNO;
    std::int32_t m_nLeavingMessageNO;
    CTerritory m_FieldTerritory;
    CTerritory m_TeleportTerritory;
    std::vector<bool> m_bIsBannedTerritoryOn;
    std::vector<CTerritory*> m_BannedFieldTerritories;
    std::array<std::byte, 0x08> m_padding190;
    CEventController* m_pEventController;
    std::int32_t m_nInitialDelay;
    std::int32_t m_nOnTime;
    std::int32_t m_nOffTime;
    std::int32_t m_nRandomTime;
    bool m_bMPCCAsParty;
    std::array<std::byte, 0x03> m_padding1b1;
    std::int32_t m_nMapX;
    std::int32_t m_nMapY;

protected:
    std::array<std::byte, 0x04> m_padding1bc;
    std::vector<FVector> m_vecRestartPoints;
    std::vector<FVector> m_vecTeleportPoints;
    AreaTypeEnum m_AreaType;
    AreaTargetTypeEnum m_AreaTargetType;
    std::vector<CSkillInfo*> mSkillInfoList;
    AreaSkillActionType mSkillActionType;
    std::int32_t m_nSkillProb;
    std::int32_t m_nUnitTick;
    std::int32_t m_nRestartTime;
    std::array<std::int32_t, 2> m_BlockedActionTable;
    double mExpPenaltyPer;
    bool mIsItemDrop;
    bool mHPMPRegenOnLevelUp;
    std::array<std::byte, 0x02> m_padding24a;
    std::int32_t mRestartAllowedTime;
    InstantZoneKey m_InstantZoneKey;
    InstantZoneKey m_EnterInzoneKey;
    FlyMovePair m_FlyMoveInfo;

private:
    friend class CAreaDB;
    friend class CEvent_DynamicPvPZone;
    friend class CParserForArea;
    friend class User;
    friend bool BuilderCmd_setarea(CUserSocket*, User*, char16_t*);
    friend bool BuilderCmd_setarea_all(CUserSocket*, User*, char16_t*);
    friend double AreaBasedRule::details::GetExpPenaltyRatio(
        std::uint32_t inZoneId, FVector* position, AreaTypeEnum areaType
    );
    friend bool AreaBasedRule::PrivateStore(CCreature* creature);
    friend bool AreaBasedRule::PrivateRecipeShop(CCreature* creature);

    L2_DECLARE_OBJECT_CLASS_NAME_STORAGE(CArea)
};


class CAreaDB : public Singleton<CAreaDB> {
public:
    using AreaList = std::vector<CArea*>;
    using AreaMap = std::map<std::u16string, CArea*, runtime::CStringLess<std::u16string>>;

    static constexpr std::size_t kMapXCount = 20;
    static constexpr std::size_t kMapYCount = 17;
    static constexpr std::size_t kAreaTypeCount = 23;

    CAreaDB();
    ~CAreaDB() override;

    virtual bool IsUnderWater(const FVector& position, int& waterSurface, int worldId);
    virtual const AreaList& GetAreaListByType(int mapX, int mapY, AreaTypeEnum type) const;
    virtual const AreaList& GetSortedInstantAreaList(int mapX, int mapY) const;

    CArea* FindAreaRough(int worldId, const FVector& position, AreaTypeEnum type);
    CArea* FindAllArea(const FVector& position, AreaTypeEnum type);
    AreaList FindAllAreaList(const FVector& position);
    AreaList FindAllAreaList(const FVector& position, AreaTypeEnum type);
    CArea* FindArea(int worldId, const FVector& position, AreaTypeEnum type, bool includeZ);
    CArea* FindArea(const char16_t* name);
    bool IsWaterZone(const FVector& position, int& waterSurface, int worldId);
    bool IsFishingZone(const FVector& position, int worldId);
    bool IsFlyMoveZone(const FVector& position, int worldId);
    void ProcessInstantAreas(CCreature* creature, const FVector& position);
    void OnSunrise(bool isNight);
    void StartAreaTimer();

    void GatherAllAreas(int mapX, int mapY, AreaTypeEnum type, AreaList& areas);
    void SortInstantArea();
    void PostLoad();
    void LoadArea();
    void LoadAreaForChecker();
    void AddArea(CArea* area);

private:
    AreaMap m_AreaMap;
    AreaList m_vArea[kMapXCount][kMapYCount][kAreaTypeCount];
    AreaList m_vSortedInstantArea[kMapXCount][kMapYCount];
    bool m_bLoadForChecker;
    [[maybe_unused]] std::array<std::byte, 0x07> m_padding3fc29;
};
