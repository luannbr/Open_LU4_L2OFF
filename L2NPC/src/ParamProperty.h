#pragma once

#include "L2Core/src/inc/XStd.h"
#include "ScriptTypeInfo.h"
#include "Shared/src/RWLock.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>

template <std::size_t Capacity>
class CPacketHelperEx;

namespace l2npc {

class NpcMakerEx;

} // namespace l2npc


struct FVector;
enum PhysicalState : int;
using AiWaypoints = std::vector<FVector>;
using AiWaypointDelays = std::vector<int>;

// PDB type AIConstantData, target size 0x10. Windows wchar_t is a UTF-16
// code unit, represented by char16_t at the portable ABI boundary.
union AIConstantValue {
    std::int64_t intData;
    float floatData;
    char16_t* stringData;
    TelPosList* telPosListData;
    BuySellList* buySellListData;
    AiWaypoints* wayPointsData;
    AiWaypointDelays* wayPointDelaysData;
    void* pointerData;

    constexpr AIConstantValue() noexcept : intData(0) {}
};

struct AIConstantData {
    ScriptTypeInfo* type = nullptr;
    AIConstantValue data;

    bool IsValid() const noexcept { return type != nullptr; }
};


class CTelPosListCacheManager {
public:
    static bool IsCached(TelPosList* list);
    static bool Cache(TelPosList* list, int npcId, int userId);

private:
    static RWLock m_lock;
    static std::map<TelPosList*, bool> m_mapCached;
};

class CBuySellListCacheManager {
public:
    using map_key = std::CTriKey<int, int, BuySellList*>;

    static bool IsCached(map_key key);
    static bool Cache(map_key key, CPacketHelperEx<20480>& packet);

private:
    static RWLock m_lock;
    static std::map<map_key, bool> m_mapCached;
};

class CAIParameter {
public:
    CAIParameter();
    CAIParameter(const CAIParameter&) = default;
    CAIParameter& operator=(const CAIParameter&) = default;
    ~CAIParameter() = default;

    AIConstantData GetData(const char16_t* varName);
    float GetDesireDecayValue(PhysicalState state);
    float GetDesireBoostValue(PhysicalState state);
    int GetSize();
    void Copy(const CAIParameter& other);

private:
    friend class CParserForLoader;
    friend class l2npc::NpcMakerEx;
    friend class CNPC;

    // Original private members. The current EXE, instead of the older PDB,
    // fixes the map/cache boundaries below.
    CAIParameter* m_pSuper; // +0x000
    std::map<std::u16string, AIConstantData, runtime::CStringLess<std::u16string>> m_mapData; // +0x008
    struct {
        bool Cached;
        float Value;
    } DesireDecayCache[19], // +0x028
        DesireBoostCache[19]; // +0x0c0
};

class CAIProperty {
public:
    CAIProperty();
    CAIProperty(const CAIProperty&) = default;
    CAIProperty& operator=(const CAIProperty&) = default;
    ~CAIProperty() = default;

    AIConstantData GetData(char16_t* varName);

private:
    friend class CParserForLoader;

    CAIProperty* m_pSuper; // +0x00
    std::map<std::u16string, AIConstantData, runtime::CStringLess<std::u16string>> m_mapData; // +0x08
};
