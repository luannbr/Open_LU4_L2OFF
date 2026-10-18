#include "Shared/src/Territory.h"

#include "L2Core/src/inc/Guard.h"

#include <algorithm>


CTerritory::CTerritory() {
    L2_GUARD_FUNCTION();
    Clear();
}

void CTerritory::Clear() {
    m_TerritoryPoints.clear();
    m_Bottom = 0x8000;
    m_Top = -0x7fff;
    m_East = -0x50000;
    m_West = 0x37fff;
    m_South = -0x40000;
    m_North = 0x47fff;
}

void CTerritory::AddTerritoryPoint(int x, int y) {
    L2_GUARD_FUNCTION();
    m_TerritoryPoints.push_back(FVector2D{static_cast<double>(x), static_cast<double>(y)});
    m_West = std::min(m_West, x);
    m_East = std::max(m_East, x);
    m_North = std::min(m_North, y);
    m_South = std::max(m_South, y);
}
