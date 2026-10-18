#pragma once

#include "Shared/src/BaseStructure.h"
#include "Shared/src/StandardContainers.h"

#include <cstddef>

struct CGeoCell;


class CMapBugDetector {

public:
    CMapBugDetector();
    ~CMapBugDetector();

    bool GetBugCellShelter(CGeoCell* cell, FVector& shelter) const;
    bool LoadBugCell(int zoneX, int zoneY);
    void RegisterMapBug(CGeoCell* cell, int shelterIndex);
    int GetBugCount() const;

private:
    std::map<CGeoCell*, int> m_mapBug; // +0x00..+0x1f
    std::vector<FVector> m_lstShelter; // +0x20..+0x37
};


extern CMapBugDetector g_MapBugDetector;
