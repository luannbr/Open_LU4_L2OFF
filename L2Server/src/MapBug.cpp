#include "MapBug.h"

#include "CWorld.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/Runtime/ResourceDirectories.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

CMapBugDetector g_MapBugDetector;

CMapBugDetector::CMapBugDetector() = default;

CMapBugDetector::~CMapBugDetector() = default;

bool CMapBugDetector::GetBugCellShelter(CGeoCell* const cell, FVector& shelter) const {
    const auto found = m_mapBug.find(cell);
    if (found == m_mapBug.end()) {
        return false;
    }

    shelter = m_lstShelter[static_cast<std::size_t>(found->second)];
    return true;
}

bool CMapBugDetector::LoadBugCell(const int zoneX, const int zoneY) {
    char fileName[0x400]{};
    std::snprintf(fileName, sizeof(fileName), "%02d_%02d_bug.txt", zoneX, zoneY);
    std::ifstream stream(runtime::ResolveResourcePath(std::filesystem::path{".."} / "GeoData" / fileName));
    if (!stream) {
        return false;
    }

    std::string line;
    while (std::getline(stream, line)) {
        const std::size_t firstSeparator = line.find(';');
        if (firstSeparator == std::string::npos) {
            return false;
        }

        const bool shelterLine = line.compare(0, 2, "//") == 0;
        const std::size_t firstValueStart = shelterLine ? 2U : 0U;
        const std::string firstValue = line.substr(firstValueStart, firstSeparator - firstValueStart);
        const std::size_t secondSeparator = line.find(';', firstSeparator + 1U);
        if (secondSeparator == std::string::npos) {
            return false;
        }

        const std::string secondValue = line.substr(firstSeparator + 1U, secondSeparator - firstSeparator - 1U);
        const std::string thirdValue = line.substr(secondSeparator + 1U);
        const int x = std::atoi(firstValue.c_str());
        const int y = std::atoi(secondValue.c_str());
        const int z = std::atoi(thirdValue.c_str());

        if (shelterLine) {
            m_lstShelter.push_back(FVector{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)});
            continue;
        }

        CGeoCell* const cell = CWorld::Inst().GetGeo()->GetBaseCell(x, y, z);
        const int shelterIndex = static_cast<int>(m_lstShelter.size()) - 1;
        if (cell != nullptr && shelterIndex >= 0) {
            RegisterMapBug(cell, shelterIndex);
        }
    }

    return true;
}

void CMapBugDetector::RegisterMapBug(CGeoCell* const cell, const int shelterIndex) { m_mapBug[cell] = shelterIndex; }

int CMapBugDetector::GetBugCount() const { return static_cast<int>(m_mapBug.size()); }
