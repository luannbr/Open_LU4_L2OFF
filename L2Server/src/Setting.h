#pragma once

#include "BeautyManager.h"

#include "L2Core/src/inc/XStd.h"
#include "Shared/src/BaseStructure.h"
#include "Shared/src/Config.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TemplateHelper.h"
#include "Shared/src/Territory.h"

#include <cstddef>
#include <cstdint>
#include <utility>

struct AlterSkillInfo;
class CParserForSetting;
class CDBSocket;
class CUserSocket;
template <std::size_t Capacity>
class CPacketHelperEx;

bool LoadSelectedCharacterPacket(CDBSocket* socket, const std::uint8_t* packet);

// Setting.cpp/current PDB value stored by the three beauty maps. The current
// machine image constructs 0x20-byte map owners, while this mapped value keeps
// the exact 0x20-byte record carried by their nodes.
struct StrAdenaCoin {
    std::int64_t Adena = 0;
    std::int64_t Coin = 0;
    std::int64_t ResetAdena = 0;
    std::int32_t Limit = 0;
    bool bEnable = false;
    std::byte padding01d[3]{};
};


// Current Setting.obj owner. The target constructor at 0x00a485a8 and
// Singleton<CSetting>::inst at 0x0047b7a8 fix the 0x7190
// physical extent and every carrier offset below. The adjacent PDB supplies
// the original field/type names. Current xvector/xwstring extents account for
// the shift from the older donor layout.
class CSetting : public Singleton<CSetting> {
    friend class CDB;
    friend class User;
    friend class CCreature;
    friend class CParserForSetting;
    friend class CUserSocket;
    friend bool LoadSelectedCharacterPacket(CDBSocket* socket, const std::uint8_t* packet);

public:
    struct ClassifiedPosition {
        FVector m_Point;
        std::int64_t m_Bit = 0;
        bool m_bForChao = false;
        std::byte padding021[7]{};
    };

    class StatValueType {
    public:
        std::int32_t Int = 0;
        std::int32_t Str = 0;
        std::int32_t Con = 0;
        std::int32_t Men = 0;
        std::int32_t Dex = 0;
        std::int32_t Wit = 0;
        std::int32_t Luc = 0;
        std::int32_t Cha = 0;
    };

    class AbnormalResistType {
    public:
        std::int32_t m_PAbnormalResist = 0;
        std::int32_t m_MAbnormalResist = 0;
    };

    class RestartPointGroup {
    public:
        FVector GetNormalRestartPoint() const;
        FVector GetChaoRestartPoint() const;

        // Current parser action 12 copies 0x18-byte FVector records into
        // both carriers, and the restart-point selectors index them with
        // the same 0x18 stride. ClassifiedPosition is used only by the
        // process-wide m_StartPoint list below.
        std::vector<FVector> m_PointsForNormal;
        std::vector<FVector> m_PointsForChao;
        std::u16string m_sName;
        std::int32_t m_nBBS = 0;
        std::int32_t m_nLocName = 0;
        bool m_bNoviceZone = false;
        bool m_bBanned[RT_MAX]{};
        std::byte padding08f{};
        RestartPointGroup* m_pBannedAlternative[RT_MAX]{};
        std::u16string m_sBannedAlternativeName[RT_MAX];
    };

    class RestartTerritory {
    public:
        CTerritory m_Territory;
        bool m_bRace[RT_MAX]{};
        std::byte padding096[2]{};
        RestartPointGroup* m_pRestartPointGroup[RT_MAX]{};
        std::u16string m_sRestartPointGroupName[RT_MAX];
    };

    using BeautyKey = std::CDualKey<std::int32_t, std::int32_t>;
    using BeautyMap = std::map<BeautyKey, StrAdenaCoin>;
    using InitialEquipment = std::vector<std::pair<std::int32_t, std::int64_t>>;
    using TypeToSettingFileName = std::map<L2ServerType::Value, const char16_t*>;

    CSetting();
    ~CSetting() override;

    const char16_t* GetSettingFileName(L2ServerType::Value serverType);
    bool LoadSetting();
    bool LoadMinigame();
    bool LoadData(char16_t* fileBuffer, int fileSize);
    FVector GetRandomStartPoint(std::int64_t classBit);
    FVector GetRandomStartPoint(ClassType classType);

    void AddRestartPointGroup(RestartPointGroup* group);
    void AddRestartTerritory(RestartTerritory* territory);
    void MapRestartPointGroup(RestartPointGroup* group, int mapX, int mapY);
    RestartPointGroup* FindRestartPointGroupByName(const char16_t* name);
    void PostProcessRestartPointGroup();

    int GetRestartPointSys(RaceType race, FVector position, int type);
    RestartPointGroup* GetRestartPointGroup(RaceType race, FVector position);
    FVector GetRestartPoint(RaceType race, FVector position, bool chao, int pkCount);

    void LoadBeautyData();
    bool LoadColorData();
    bool LoadHairData();
    bool LoadFaceData();
    static bool LoadExpData();

    bool IsExistKey(Beauty::BeautyState state, int first, int second);
    void SetBeautyEx(
        Beauty::BeautyState state,
        int first,
        int second,
        std::int64_t adena,
        std::int64_t resetAdena,
        std::int64_t coin,
        int limit,
        short enable
    );
    void DeleteBeautyEx(Beauty::BeautyState state, int first, int second);
    void SendBeautyList(Beauty::BeautyState state, CPacketHelperEx<20480>& packet, int& count);

    BeautyMap m_mapBeauty[3];

private:
    InitialEquipment m_InitialEquipment[192];
    std::vector<ClassifiedPosition> m_StartPoint;
    std::vector<RestartPointGroup*> m_RestartPointGroup;
    RestartPointGroup* m_RestartPointGroupForMap[20][17];
    std::vector<RestartTerritory*> m_RestartTerritory;
    RestartPointGroup* m_pIsolatedRestartPointGroup;
    StatValueType m_minimum_stat[192];
    StatValueType m_maximum_stat[192];
    StatValueType m_recommended_stat[192];
    AbnormalResistType m_ResistType[192];
    TypeToSettingFileName fileNameMap;
};
