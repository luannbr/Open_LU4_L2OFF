#pragma once

#include "Shared/src/MemorySub.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/Obj.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <limits>

class CCreature;


// Header-inline helper declared immediately before CTerritory in the
// original Shared/Territory.h. The current EXE inlines its constructor and
// accessors into the territory/spawn paths. The adjacent PDB fixes the exact
// member names, access and 12-byte layout.
class ZLogVisitor {
public:
    ZLogVisitor() : minZ(std::numeric_limits<int>::max()), maxZ(std::numeric_limits<int>::min()), isEnable(false) {}

    void SetEnable(bool enable) { isEnable = enable; }
    bool IsEnable() { return isEnable; }
    int GetMaxZ() { return maxZ; }
    int GetMinZ() { return minZ; }
    void SetMinZ(int z) { minZ = z; }
    void SetMaxZ(int z) { maxZ = z; }

private:
    int minZ;
    int maxZ;
    bool isEnable;
};


// Recovered from Shared/Territory.h and the shipping functions at
// 0x0053eb70..0x0053fbec. The Win64 object is exactly 0x78 bytes:
// MemoryObject[0x18], xwstring[0x28], xvector[0x20], and six int bounds.
class CTerritory : public MemoryObject {
public:
    enum TerritoryLogType {
        NONE = 0,
        SHOW_LOG = 1,
    };

    enum CollisionResult {
        CR_NOT_PASS = 0,
        CR_PASS = 1,
        CR_STEP_IN = 2,
        CR_STEP_OUT = 3,
        CR_STAY_IN = 4,
    };

    CTerritory();
    ~CTerritory() override = default;

    L2_DECLARE_OBJECT_TYPE(CTerritory)

    // The PDB declares this compatibility hook, but neither a body nor
    // backing storage survives in the current EXE or the 2015 donor.
    void SetSystemMsgId(int systemMessageId);

    bool IsLeft(const FVector2D& first, const FVector2D& second, const FVector2D& third);
    bool IsStraight(const FVector2D& first, const FVector2D& second, const FVector2D& third);

    void Clear();
    void MakeRectangle(FVector begin, FVector end, FVector direction, double halfWidth);
    void AddTerritoryPoint(int x, int y);
    void AddTerritoryPoint(FVector* point);

    FVector2D* GetTerritoryPoint(int index) {
        return &runtime::CheckedVectorAt(m_TerritoryPoints, static_cast<std::size_t>(index));
    }
    FVector GetTerritoryPoint3(int index) const;
    FVector GetCenterPos();
    int GetMaxDistanceBy2D(const FVector& position);
    int GetMaxDistanceFromCenterBy2D();
    int GetTerritoryPointSize() { return static_cast<int>(m_TerritoryPoints.size()); }

    void SetName(char16_t* name) { sName = std::u16string(name); }
    const char16_t* GetName() { return sName.c_str(); }
    std::u16string GetNameString() { return sName; }

    void SetBottom(int bottom) { m_Bottom = bottom; }
    int GetBottom() const { return m_Bottom; }
    void SetTop(int top) { m_Top = top; }
    int GetTop() const { return m_Top; }

    bool IsInsideRough(const FVector& position, bool includeZ);
    bool IsInside(CTerritory& territory, bool includeZ);
    bool IsInside(const FVector& position, bool includeZ);
    bool IsInsideWithLogVisitor(const FVector& position, ZLogVisitor& visitor, bool includeZ, int& reason);
    bool IsCollideRough(const FVector& begin, const FVector& end, bool includeZ);
    bool CheckIntersectAndGetPoint(FVector& begin, FVector& end, bool includeZ);
    CollisionResult IsCollide(const FVector& begin, const FVector& end, bool includeZ);

    FVector PickRandomPos(bool sky);
    bool GoPoint(CCreature& creature, int index);
    bool GoFirstPoint(CCreature& creature);
    bool GoPointRandom(CCreature& creature);
    static void ShowLineByAdena(CCreature& creature, const FVector& begin, const FVector& end);
    void ShowTerritoryByAdena(CCreature& creature);
    static CTerritory* DisassembleTerritoryInfo(const unsigned char** packet);
    void AssembleTerritoryInfo(char* buffer, unsigned int& used, unsigned int& capacity);
    bool IsConvex();
    bool DeleteTerritory();

    // Names and order are recovered directly from the target/donor layout.
    std::u16string sName;
    std::vector<FVector2D> m_TerritoryPoints;
    int m_Bottom;
    int m_Top;
    int m_East;
    int m_West;
    int m_South;
    int m_North;

private:
    bool IsOddNodes(const FVector& position);
};
