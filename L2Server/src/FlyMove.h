#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

class CParserForFlyMove;
class User;

struct FlyMovePair {
    int path_id;
    int node_id;
};

static_assert(std::is_aggregate_v<FlyMovePair>);
static_assert(std::is_trivially_default_constructible_v<FlyMovePair>);
static_assert(std::is_trivially_copyable_v<FlyMovePair>);
static_assert(std::is_trivially_destructible_v<FlyMovePair>);

class CFlyMovePathNode {
public:
    explicit CFlyMovePathNode(int index);
    ~CFlyMovePathNode() = default;

    void SetPos(const FVector& position) { m_Pos = position; }
    const FVector& GetPos() const { return m_Pos; }
    void SetLandingPos(const FVector& position) { m_LandingPos = position; }
    const FVector& GetLandingPos() const { return m_LandingPos; }
    void SetNonLanding() { m_bNonLanding = true; }
    bool IsNonLanding() const { return m_bNonLanding; }
    const std::vector<int>& GetRelatedNodeList() const { return m_RelatedNodeList; }
    void InsertRelatedNode(int nodeId);
    bool IsRelatedNode(int nodeId) const;
    int GetFlyingHeight() const { return m_nFlyingHeight; }
    void SetFlyingHeight(int height) { m_nFlyingHeight = height; }

private:
    friend class CParserForFlyMove;
    friend class CFlyMoveManager;

    std::int32_t m_nIndex; // +0x00
    std::array<std::byte, 0x04> m_alignment04To08; // +0x04
    FVector m_Pos; // +0x08
    FVector m_LandingPos; // +0x20
    bool m_bNonLanding; // +0x38
    std::array<std::byte, 0x07> m_alignment39To40; // +0x39
    std::vector<int> m_RelatedNodeList; // +0x40
    std::int32_t m_nFlyingHeight; // +0x60
    std::array<std::byte, 0x04> m_alignment64To68; // +0x64
};

class CFlyMovePath {
public:
    explicit CFlyMovePath(int id);
    ~CFlyMovePath() = default;

    void InsertNode(CFlyMovePathNode* node);
    CFlyMovePathNode* GetNode(int nodeId) const;

private:
    friend class CParserForFlyMove;
    friend class CFlyMoveManager;

    std::int32_t m_nId; // +0x00
    std::array<std::byte, 0x04> m_alignment04To08; // +0x04
    std::vector<CFlyMovePathNode*> m_NodeList; // +0x08
};

class CFlyMoveManager {
public:
    CFlyMoveManager() = default;
    ~CFlyMoveManager() = default;

    static CFlyMoveManager& Inst();

    void InsertPath(CFlyMovePath* path);
    CFlyMovePathNode* GetPathNode(int pathId, int nodeId) const;
    void FlyMove(User* user, int pathId, int nodeId, bool continuation);
    void Land(User* user, int pathId, int nodeId);
    bool IsRelatedNode(int pathId, int nodeId, int relatedNodeId) const;
    void Load();

private:
    std::vector<CFlyMovePath*> m_PathList; // +0x00
};
