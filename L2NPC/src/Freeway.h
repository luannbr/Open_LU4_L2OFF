#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>


struct CFreewayNode {
    CFreewayNode(int nodeId, int x, int y, int z, int nodeDelay, int nodeSocial, int nodeState) :
        nodeID(nodeId),
        position{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)},
        delay(nodeDelay),
        social(nodeSocial),
        state(nodeState) {}

    int nodeID;
    FVector position;
    int delay;
    int social;
    int state;
};


class CFreeway {
public:
    using NodeListType = std::vector<CFreewayNode*>;

    enum {
        OneWay = 0,
        Loop = 1,
        LoopTeleport = 2,
    };

    CFreeway(int nFreewayID, const char16_t* name) : freewayID(nFreewayID), freewayName(name) {}

    void AddNode(CFreewayNode* node) { nodeList.push_back(node); }
    int GetClosestIndex(int x, int y, int z);
    CFreewayNode* GetNode(int index);
    int GetNodeCount() { return static_cast<int>(nodeList.size()); }

private:
    [[maybe_unused]] int freewayID;
    std::u16string freewayName;
    NodeListType nodeList;
};

class CFreewayManager {
public:
    using FreewayMapType = std::map<int, CFreeway*>;

    CFreewayManager() = default;

    CFreeway* Find(const int freewayId);
    void Load();
    void AddFreeway(int freewayId, CFreeway* freeway) {
        if (freewayId >= 0) {
            freewayMap[freewayId] = freeway;
        }
    }

private:
    FreewayMapType freewayMap;
};


extern CFreewayManager g_FreewayManager;
