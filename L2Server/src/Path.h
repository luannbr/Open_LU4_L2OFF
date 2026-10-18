#pragma once

#include "Shared/src/BaseStructure.h"

#include <array>
#include <cstddef>
#include <cstdint>

class CCreature;
class CUserSocket;

// The spelling is donor-selected source vocabulary. The current path solver
// returns only these three distinct outcomes.
enum PathFindResult : std::int32_t {
    PFR_CAN_GO = 0,
    PFR_PATH_FOUND = 1,
    PFR_ERROR = 2,
};


class CPathNode {
public:
    double Dist(const CPathNode& other) const;
    FVector CastFVector() const;

    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
    std::uint32_t Neighbor[8];
};


class CPathFindingNode {
public:
    CPathFindingNode(
        CPathNode* pathNode, CPathNode* startingPathNode, double distance, double lowerBound, int depth
    ) noexcept;

    bool operator<(const CPathFindingNode& other) const noexcept;

    CPathNode* PathNode;
    CPathNode* StartingPathNode;
    std::int32_t Depth;
    double Dist;
    double lbound;
};


struct PathNodeWorldInfo {
    std::uint32_t SectionOffsetCount[20][17][128][128];
    std::int32_t maxIndex;
};


class CNodeVisitHash {
public:
    enum : std::int32_t {
        HASH_SPACE = 262143,
        MAX_POOL = 4200,
    };

    struct HashNode {
        std::int32_t PathNodeIndex;
        double Dist;
        std::int32_t Next;
    };

    struct HashDelegate {
        std::int32_t PoolIndex;
        std::int32_t Touch;
    };

    CNodeVisitHash() noexcept;

    void Reset() noexcept;
    bool Visit(std::uint32_t pathNodeIndex, double distance, bool update);

private:
    std::int32_t Touch;
    HashNode Pool[MAX_POOL];
    std::int32_t PoolCount;
    HashDelegate Hash[HASH_SPACE];
};


extern CNodeVisitHash g_NodeVisitHash[16];

class CPathNodeWorld {
public:
    CPathNodeWorld() noexcept;

    unsigned int GetMaxIndex() const noexcept;
    FVector GetPathNodePos(int index) const;

    // Load/OpenData/LoadData keep the original shared-mapping lifecycle in
    // Path.cpp.
    void Load();
    bool FindPathNodeTest(const FVector& position);
    PathFindResult PathFind(
        FVector source, FVector destination, FVector& output, int maximumVisitCount, CCreature* creature
    );
    void ViewPathNode(FVector position, CUserSocket* socket);

private:
    CPathNode* GetPathNodeArray(int x, int y, int& count);
    bool FindClosestPathNode(const FVector& position, CPathNode** nodes, int& count, bool reverse);
    CPathNode* GetPathNode(std::uint32_t index) const;
    std::uint32_t IndexFromPathNode(const CPathNode* node) const;
    void LoadData();
    void OpenData();

    CPathNode* m_Data;
    void* m_PathNodeDataHandle;
    void* m_PathNodeWorldInfoHandle;
    PathNodeWorldInfo* m_WorldInfo;
};

extern CPathNodeWorld g_pnWorld;
