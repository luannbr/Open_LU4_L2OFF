#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>
#include <string>

class CTerritory;
#if defined(L2_BUILD_L2SERVER)
class CWorldObject;
#endif

// Current Shared GeoData.obj startup-profiler state at L2Server
// 0x04d37120..0x04d37170. SetStaticField is the sole producer and the
// listener's post-LoadData report is the sole current consumer.
extern int maxHorizonLength;
extern int maxVerticalLength;
extern CTerritory* pMaxHorizonTerritory;
extern CTerritory* pMaxVerticalTerritory;
extern int totalloopCount;
extern std::uint32_t profile1;
extern std::uint32_t profile2;
extern std::uint32_t profile3;
extern std::uint32_t profile4;
extern std::uint32_t profile5;
extern std::uint32_t profile6;
extern std::uint32_t profile7;
extern int callCount1;
extern int callCount2;
extern int callCount3;
extern int callCount4;
extern int callCount5;
extern int callCount6;
extern int callCount7;

namespace l2geo::portable {
class GeoDataSource;
}

enum GeoDirEnum {
    GEO_DIR_EAST = 0,
    GEO_DIR_WEST = 1,
    GEO_DIR_SOUTH = 2,
    GEO_DIR_NORTH = 3,
    GEO_DIR_ABOVE = 4,
    GEO_DIR_BELOW = 5,
    GEO_DIR_HORIZONTAL = 6,
    GEO_DIR_MAX = 7,
};

enum GeoFieldTypeEnum {
    GEO_FIELD_BATTLE_RESIDENCE = 0,
    GEO_FIELD_BATTLE_PVP = 1,
    GEO_FIELD_FREEZE = 2,
    GEO_FIELD_PEACE = 3,
    GEO_FIELD_WATER = 4,
    GEO_FIELD_STATUS_CHANGE = 5,
    GEO_FIELD_INSTANT = 6,
    GEO_FIELD_SSQ = 7,
    GEO_FIELD_NO_RESTART = 8,
    GEO_FIELD_SHUTTLE = 9,
    GEO_FIELD_DRAGON_LAIR = 10,
    GEO_FIELD_NO_SKILL = 11,
    GEO_FIELD_TRAINING_ZONE = 12,
    GEO_FIELD_ONLY_PVE = 13,
    GEO_FIELD_MAX = 16,
};

// Current Shared GeoData.obj field-mask table consumed directly by the
// L2Server area/creature owners. The descending bit order is observable in
// both CGeoData::IsField and CCreature::AreaCheckForEveryTick.
extern const std::uint16_t GeoFieldMask[GEO_FIELD_MAX];

// Current Shared GeoData.obj writable height mask at L2Server
// 0x015c581c. BuilderCmd_worlddebug is a direct consumer.
extern std::int16_t GeoHeightMask;

// Exact PODs used by the three L2World_GeoData* shared mappings. Their
// names and fields come from the available lineage2build PDB. The current
// L2NPC.exe confirms every offset used below.
struct CGeoCell {
    std::int16_t m_data = 0;
};

class CGeoSector {
public:
    CGeoSector() noexcept;

    int GetFirstCellIndex(int cellX, int cellY, std::int16_t* cellOffsets);
    int GetLastCellIndex(int cellX, int cellY, std::int16_t* cellOffsets);

    std::int16_t m_Field;
    CGeoCell m_DefaultCell;
    std::int16_t m_TerritoryMinZ;
    std::int16_t m_TerritoryMaxZ;
    std::uint8_t m_BooleanFlag;
    std::uint8_t m_Alignment0[3];
    std::int32_t cell_offset_index;
    std::int32_t cell_segment;
    std::int16_t num_cell;
    std::uint8_t m_Alignment1[2];
};

struct CGeoDataIndexStruct {
    std::int32_t num_cell[340];
    std::int32_t num_complex_sector[340];
    std::int16_t zone_num;
    std::int8_t idx[340];
    std::int8_t idy[340];
    std::uint8_t m_Alignment[2];
};

struct CSharedGeoZone {
    CGeoSector sectors[256][256];
    std::int8_t zone_x;
    std::int8_t zone_y;
    std::uint8_t m_Alignment[2];
    std::int32_t num_cell;
};

// Exact shared representation opened by CGeoData::OpenMMF in the current
// L2NPC.exe. "Offest" is the spelling used by the shipping mapping name.
namespace shared_geo_layout {

inline constexpr std::size_t indexBytes = 0xd4c;
inline constexpr std::size_t sectorBytes = 0x18;
inline constexpr std::size_t sectorCount = 256 * 256;
inline constexpr std::size_t zoneBytes = 0x180008;


} // namespace shared_geo_layout

struct GeoDataSharedMemoryNames {
    std::string index = "L2World_GeoDataIndex";
    std::string zones = "L2World_GeoDataZones";
    std::string cells = "L2World_GeoDataCells";
    std::string cellOffsets = "L2World_GeoDataCellOffest";
};

// Aggregate values read from the first (zone-file) section of geo_index.txt.
// Exact CGeoData::LoadByStream reads only this section. Its Windows shared
// representation then creates L2World_GeoDataZones, L2World_GeoDataCells and
// L2World_GeoDataCellOffset mappings from each zone header's num_cell and
// num_complex_sector values. Those mappings are not the later index lists.
struct GeoDataLoadStats {
    std::size_t zones = 0;
    std::uint64_t mappedBytes = 0;
    std::uint64_t cells = 0;
    std::uint64_t flatSectors = 0;
    std::uint64_t simpleSectors = 0;
    std::uint64_t complexSectors = 0;
};

namespace HelperFunction {

bool IsInArea(const std::vector<CTerritory*>& areas, const FVector& position);

} // namespace HelperFunction

// CGeoZone is embedded 340 times in CGeoData. The current executable's
// constructor at 0x004355e8 installs a seven-slot vtable, keeps the linked
// CSharedGeoZone pointer at +0x08, castleId at +0x10, and the seven MSVC
// xvector objects at +0x18..+0xd8. The shipping size is 0xf8 because that
// build's xvector occupies 0x20 bytes. The portable STL layout is different.
class CGeoZone {
public:
    CGeoZone() = default;
    ~CGeoZone() = default;

    CGeoZone(const CGeoZone&) = delete;
    CGeoZone& operator=(const CGeoZone&) = delete;

    // Vtable order at 0x005b31f8 in the shipping image.
    virtual bool IsNoFlyArea(const FVector& position);
    virtual bool IsInNoCallPCArea(const FVector& position);
    virtual bool IsInNoChangePCArea(const FVector& position);
    virtual bool IsInNoDropItemArea(const FVector& position);
    virtual bool IsInNoSaveBookmarkArea(const FVector& position);
    virtual bool IsInNoUseBookmarkArea(const FVector& position);
    virtual bool IsInTransformableArea(const FVector& position);

    CGeoSector* GetSector(int sectorX, int sectorY);
    int PosToMap_X(int position);
    int PosToMap_Y(int position);
    bool Load(const wchar_t* fileName, CGeoCell* cells, std::int16_t* cellOffsets);
    int ClearData();
    char GetX();
    char GetY();
    CTerritory* GetNoDropItemTerritory(const FVector& position);

    void AddNoFlyArea(CTerritory* territory);
    void AddNoCallPCArea(CTerritory* territory);
    void AddNoChangePCArea(CTerritory* territory);
    void AddNoDropItemArea(CTerritory* territory);
    void AddNoSaveBookmarkArea(CTerritory* territory);
    void AddNoUseBookmarkArea(CTerritory* territory);
    void AddTransformableArea(CTerritory* territory);

    using TerritoryList = std::vector<CTerritory*>;

    CSharedGeoZone* m_data = nullptr;
    int castleId = 0;
    TerritoryList m_NoFlyAreas;
    TerritoryList m_NoCallPCAreas;
    TerritoryList m_NoChangePCAreas;
    TerritoryList m_NoDropItemAreas;
    TerritoryList m_NoSaveBookmarkAreas;
    TerritoryList m_NoUseBookmarkAreas;
    TerritoryList m_TransformableAreas;
};

// CGeoData keeps the original consumer-side field model: mapping handles,
// mapped views, 340 embedded CGeoZone objects and the bad-map table. Only
// the OS implementation behind the opaque handles changes on non-Windows
// systems. Packed cell bits 4..15 contain twice the signed height and bits
// 0..3 are E/W/S/N.
class CGeoData {
public:
    enum JumpType {
        NoJump = 0,
        Jumpable = 1,
    };

    explicit CGeoData(bool bUseTdd);
    ~CGeoData();

    CGeoData(const CGeoData&) = delete;
    CGeoData& operator=(const CGeoData&) = delete;

    // Names, parameter qualifiers and return types are from the nearby
    // module 84 PDB records. The current executable confirms
    // these bodies and its public-name encoding confirms non-const `this`.
    bool Load();
    bool LoadByStream(std::wistream& stream);

    bool IsValidGeo(int x, int y);
    CGeoZone* GetZone(int zoneX, int zoneY);
    CGeoCell* GetCellArray(int zoneX, int zoneY);
    std::int16_t* GetCellOffsetArray(int zoneX, int zoneY);
    bool GetCellIndexes(int x, int y, int& firstIndex, int& lastIndex);
    CGeoCell* GetBaseCell(int z, CGeoCell* cells, int firstIndex, int lastIndex);
    CGeoCell* GetBaseCell(int x, int y, int z);
    CGeoCell* GetBaseCell(const FVector& position);
    CGeoCell* GetClosestUpperCell(const FVector& position);
    CGeoCell* GetBaseCellFromSector(CGeoSector* sector, int x, int y, int z);
    CGeoCell* GetUpperCell(int z, CGeoCell* cells, int firstIndex, int lastIndex);

    int FindNearGround(int x, int y, int z);
    int FindNearGround(FVector& position);
    int FindGround(int x, int y, int z);
    int FindGround(FVector& position);
    int FindGround(const FVector& position);
    int GetMovingDist(FVector& source, FVector& destination);
    bool IsValidPosition(int x, int y);
    bool IsOpen(FVector position);
#if defined(L2_BUILD_L2SERVER)
    bool CanSee(const FVector& source, const FVector& destination, int inZoneId, bool checkWorldCollision);
    bool CanSee(CWorldObject* source, CWorldObject* destination, bool checkWorldCollision);
    bool CanSeeOldStyle(const FVector& source, const FVector& destination, int inZoneId, bool checkWorldCollision);
    bool CanSeeOldStyle(CWorldObject* source, CWorldObject* destination, bool checkWorldCollision);
#endif
    bool SingleLineCheckForProjectile(const FVector& source, const FVector& destination);
    bool SingleLineCheckWithFlyTopDown(const FVector& source, const FVector& destination);
    bool SingleLineCheckWithFlyOld(FVector source, FVector destination, bool checkBothDiagonalPaths);

    bool MoveStraightTest(
        const FVector& source, const FVector& destination, double distance, bool floating, int verticalLimit
    );

    bool MoveStraightTest(
        const FVector& source,
        const FVector& destination,
        FVector& outputPosition,
        double& travelledDistance,
        bool& distanceExhausted,
        double distance,
        bool floating,
        int verticalLimit
    );

    bool MoveStraightTestBoth(
        const FVector& source,
        const FVector& destination,
        double distance,
        bool bothDirections,
        bool floating,
        int verticalLimit
    );

    CGeoCell* GetNextCell(
        CGeoCell* current, CGeoSector* sector, GeoDirEnum direction, int x, int y, int z, JumpType jumpType
    );
    CGeoCell* GetNextCell(CGeoCell* current, GeoDirEnum direction, int x, int y, int z, JumpType jumpType);
    CGeoCell* GetNextCellOnFloating(CGeoCell* current, GeoDirEnum direction, int x, int y, int z, JumpType jumpType);
    CGeoCell* GetNextCellOnFly(CGeoCell* current, GeoDirEnum direction, int x, int y, int z);
    CGeoCell* GetNextHorizontalCellOnSight(CGeoCell* current, GeoDirEnum direction, int x, int y, int z);
    CGeoCell* GetNextHorizontalCellOnSightOld(CGeoCell* current, GeoDirEnum direction, int x, int y, int z);
    GeoDirEnum AntiGeoDirEnum(GeoDirEnum direction);

    bool MoveStraight(
        const FVector& source,
        const FVector& destination,
        double distance,
        FVector& outputPosition,
        bool& distanceExhausted,
        double& travelledDistance
    );
    bool MoveStraightFloating(
        const FVector& source,
        const FVector& destination,
        double distance,
        FVector& outputPosition,
        bool& distanceExhausted,
        int verticalLimit
    );
    bool MoveStraightFly(
        const FVector& source,
        const FVector& destination,
        double distance,
        FVector& outputPosition,
        bool& distanceExhausted,
        double collisionHeight,
        bool legacyCollisionCheck,
        double& travelledDistance
    );

    bool SetStaticField(CTerritory* territory, GeoFieldTypeEnum fieldType);
    void SetField(CTerritory* territory, GeoFieldTypeEnum fieldType, bool enabled);
    bool IsField(int x, int y, int z, GeoFieldTypeEnum fieldType);
    bool IsField(CGeoZone* zone, int x, int y, int z, GeoFieldTypeEnum fieldType);
    bool IsBattleField(FVector position);

    void AddNoFlyArea(CTerritory* territory);
    void AddNoCallPCArea(CTerritory* territory);
    void AddNoChangePCArea(CTerritory* territory);
    void AddNoDropItemArea(CTerritory* territory);
    void AddNoSaveBookmarkArea(CTerritory* territory);
    void AddNoUseBookmarkArea(CTerritory* territory);
    void AddTransformableArea(CTerritory* territory);
    void ExpandWaterSector();

protected:
    bool AddZone(char zoneX, char zoneY, int cellCount, int nonComplexSectorCount);
    bool ReadInfo(const wchar_t* fileName, char& zoneX, char& zoneY, int& cellCount, int& nonComplexSectorCount);
    bool CheckZoneBound(int zoneX, int zoneY);
    void DecideDirection(const FVector& vector, GeoDirEnum* directionX, GeoDirEnum* directionY, GeoDirEnum* directionZ);
    CGeoSector* GetSectorByCoord(int x, int y);
    CGeoCell* GetClosestUpperCell(int z, CGeoCell* cells, int firstIndex, int lastIndex, bool requireAtOrAbove);
    CGeoCell* GetClosestUpperCellFromSector(CGeoSector* sector, int x, int y, int z, bool requireAtOrAbove);
    bool CollideWithTopOrBottom(GeoDirEnum direction, const FVector& position, double distance);
    bool CollideWithTopOrBottom(GeoDirEnum direction, int x, int y, int z, double distance, FVector& adjustedPosition);

    void* m_hfm_index = nullptr;
    CGeoDataIndexStruct* m_zone_index = nullptr;
    void* m_hfm_zones[20][17]{};
    CSharedGeoZone* m_SharedZone[20][17]{};
    CGeoZone m_Zone[20][17];
    void* m_hfm_cells[20][17]{};
    CGeoCell* m_cells[20][17]{};
    void* m_hfm_cell_offset[20][17]{};
    std::int16_t* m_cell_offset[20][17]{};
    char m_bBadMap[27][27]{};

private:
    friend class l2geo::portable::GeoDataSource;

    static constexpr int kZoneXMinimum = 10;
    static constexpr int kZoneXMaximum = 29;
    static constexpr int kZoneYMinimum = 10;
    static constexpr int kZoneYMaximum = 26;
    static constexpr std::size_t kZoneYCount = 17;

    void OpenMMF();
    void LinkWithSharedGeoData();
    void MakeMMF(bool testWorld);
    double GetVerticalLimit(const FVector& position);

public:
    std::vector<FVector> m_lstWaterPos;
};
