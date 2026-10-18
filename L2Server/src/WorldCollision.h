#pragma once

#include "InstantZone.h"

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

class CTerritory;
class User;

class CAxisAlgnBB {
public:
    CAxisAlgnBB() {}

    bool Intersect(const ::FVector& start, const ::FVector& end) const;
    void Set(const ::FVector& minimum, const ::FVector& maximum);
    ::FVector GetCorner(int corner) const;
    void GetTerritory(::CTerritory& territory) const;

    ::FVector m_Min;
    ::FVector m_Max;
    ::FVector m_Center;
    double m_RadiusSquare;
};


class CPlaneCollision {
public:
    CPlaneCollision(const ::FVector& start, const ::FVector& end, double height, const InstantZoneKey& instantZoneKey);

    bool IsEnabled() { return m_bEnabled; }

    void SetEnabled(bool enabled) { m_bEnabled = enabled; }

    double CheckCollision(const ::FVector& start, const ::FVector& end, double radius, bool mode);

    std::int32_t m_nIndex;
    bool m_bEnabled;
    ::FVector m_vStart;
    ::FVector m_vEnd;
    double m_dHeight;
    InstantZoneKey m_InstantZoneKey;
    double m_dMinX;
    double m_dMaxX;
    double m_dMinY;
    double m_dMaxY;
    double m_dMinZ;
};


struct CPlaneCollisionLink {
    CPlaneCollision* m_pPlaneCollision;
    CPlaneCollisionLink* m_pNext;
};


class CServerBlockLink {
public:
    CServerBlockLink(CAxisAlgnBB* serverBlock, CServerBlockLink* next) : m_pServerBlock(serverBlock), m_pNext(next) {}

    virtual ~CServerBlockLink() {
        m_pServerBlock = nullptr;
        m_pNext = nullptr;
    }

    CAxisAlgnBB* m_pServerBlock;
    CServerBlockLink* m_pNext;
};


class CWorldPlaneCollision {
public:
    static constexpr int COLLISION_X_SIZE = 1280;
    static constexpr int COLLISION_Y_SIZE = 1088;
    static constexpr int WORLD_PLANE_SECTION_SIZE = 512;

    using CollisionVector = std::vector<CPlaneCollision*, std::allocator<CPlaneCollision*>>;
    using CollisionMap = ::std::map<::std::u16string, CollisionVector>;

    CWorldPlaneCollision();

    void Load();
    bool IsInPlaneCollisionZone(const ::FVector& position);
    void AddServerBlock(CAxisAlgnBB* serverBlock);

    bool CheckCollision(
        const ::FVector& start,
        const ::FVector& end,
        double radius,
        ::FVector& collisionPosition,
        int instantZoneId,
        bool mode
    );

    void SetEnabled(::std::u16string name, bool enabled);

    void ShowBlockTerritory(User& user, int radius);

    void GetPlaneCollisionVector(::std::u16string name, CollisionVector& collisions);
    void Add(::std::u16string name, CPlaneCollision* collision);

    static void ConvertWorldToPlaneSection(
        int firstX, int secondX, int firstY, int secondY, int& minimumX, int& maximumX, int& minimumY, int& maximumY
    );

    CPlaneCollisionLink* m_arrPlaneCollisionLink[COLLISION_X_SIZE][COLLISION_Y_SIZE];
    CServerBlockLink* m_arrServerBlockLink[COLLISION_X_SIZE][COLLISION_Y_SIZE];
    CollisionMap m_mapNameToCollisionVector;
    int* m_CheckField[16];
    int m_CheckKey[16];
    int m_nTotalCount;
};


extern CWorldPlaneCollision g_WorldPlaneCollision;
