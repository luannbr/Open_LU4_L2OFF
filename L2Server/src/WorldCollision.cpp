#include "WorldCollision.h"

#include "Shared/src/Runtime/ProcessRuntime.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/Territory.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "User.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

int IncrementInt32WithTargetWrap(int& value) noexcept {
    const int previous = value;
    value = runtime::AddInt32Wrapping(value, 1);
    return previous;
}

} // namespace

CWorldPlaneCollision g_WorldPlaneCollision;

bool CAxisAlgnBB::Intersect(const ::FVector& start, const ::FVector& end) const {
    const double startX = start.x;
    const double startY = start.y;
    const double startZ = start.z;
    const double deltaX = end.x - startX;
    const double deltaY = end.y - startY;
    const double deltaZ = end.z - startZ;

    bool startsInside = true;
    double xDistance = -1.0;
    if (m_Min.x <= startX) {
        if (m_Max.x < startX) {
            xDistance = m_Max.x - startX;
            if (!(deltaX <= xDistance)) {
                return false;
            }
            startsInside = false;
            xDistance /= deltaX;
        }
    } else {
        xDistance = m_Min.x - startX;
        if (!(xDistance <= deltaX)) {
            return false;
        }
        startsInside = false;
        xDistance /= deltaX;
    }

    double yDistance = -1.0;
    if (m_Min.y <= startY) {
        if (m_Max.y < startY) {
            yDistance = m_Max.y - startY;
            if (!(deltaY <= yDistance)) {
                return false;
            }
            startsInside = false;
            yDistance /= deltaY;
        }
    } else {
        yDistance = m_Min.y - startY;
        if (!(yDistance <= deltaY)) {
            return false;
        }
        startsInside = false;
        yDistance /= deltaY;
    }

    double zDistance = -1.0;
    if (m_Min.z <= startZ) {
        if (m_Max.z < startZ) {
            zDistance = m_Max.z - startZ;
            if (!(deltaZ <= zDistance)) {
                return false;
            }
            zDistance /= deltaZ;
        } else if (startsInside) {
            return true;
        }
    } else {
        zDistance = m_Min.z - startZ;
        if (!(zDistance <= deltaZ)) {
            return false;
        }
        zDistance /= deltaZ;
    }

    double distance = xDistance;
    if (distance < yDistance) {
        distance = yDistance;
    }
    if (distance < zDistance) {
        distance = zDistance;
    }

    if (distance == xDistance) {
        const double y = startY + distance * deltaY;
        if (m_Min.y <= y && y <= m_Max.y) {
            const double z = startZ + distance * deltaZ;
            if (m_Min.z <= z && z <= m_Max.z) {
                return true;
            }
        }
    } else if (distance == yDistance) {
        const double x = startX + distance * deltaX;
        if (m_Min.x <= x && x <= m_Max.x) {
            const double z = startZ + distance * deltaZ;
            if (m_Min.z <= z && z <= m_Max.z) {
                return true;
            }
        }
    } else {
        const double y = startY + distance * deltaY;
        const double x = startX + distance * deltaX;
        if (m_Min.x <= x && x <= m_Max.x && m_Min.y <= y && y <= m_Max.y) {
            return true;
        }
    }

    return false;
}

void CAxisAlgnBB::Set(const ::FVector& minimum, const ::FVector& maximum) {
    m_Min = minimum;
    m_Max = maximum;
    m_Center = ::FVector{
        (m_Min.x + m_Max.x) * 0.5,
        (m_Min.y + m_Max.y) * 0.5,
        (m_Min.z + m_Max.z) * 0.5,
    };

    const double deltaX = m_Max.x - m_Min.x;
    const double deltaY = m_Max.y - m_Min.y;
    const double deltaZ = m_Max.z - m_Min.z;
    m_RadiusSquare = (deltaY * deltaY * 0.5 + deltaX * deltaX * 0.5 + deltaZ * deltaZ * 0.5) * static_cast<double>(1.1F);
}

::FVector CAxisAlgnBB::GetCorner(const int corner) const {
    return ::FVector{
        (corner & 1) == 0 ? m_Min.x : m_Max.x,
        (corner & 2) == 0 ? m_Min.y : m_Max.y,
        (corner & 4) == 0 ? m_Min.z : m_Max.z,
    };
}

void CAxisAlgnBB::GetTerritory(::CTerritory& territory) const {
    const ::FVector minimum(m_Min);
    const ::FVector maximum(m_Max);
    territory.m_Bottom = static_cast<int>(minimum.z);
    territory.m_Top = static_cast<int>(maximum.z);

    ::FVector corner = GetCorner(0);
    territory.AddTerritoryPoint(static_cast<int>(corner.x), static_cast<int>(corner.y));
    corner = GetCorner(1);
    territory.AddTerritoryPoint(static_cast<int>(corner.x), static_cast<int>(corner.y));
    corner = GetCorner(3);
    territory.AddTerritoryPoint(static_cast<int>(corner.x), static_cast<int>(corner.y));
    corner = GetCorner(2);
    territory.AddTerritoryPoint(static_cast<int>(corner.x), static_cast<int>(corner.y));
}

CWorldPlaneCollision::CWorldPlaneCollision() {
    L2_GUARD_FUNCTION();

    m_nTotalCount = 0;
    std::memset(m_arrPlaneCollisionLink, 0, sizeof(m_arrPlaneCollisionLink));
    std::memset(m_CheckKey, 0, sizeof(m_CheckKey));
    std::memset(m_CheckField, 0, sizeof(m_CheckField));
    std::memset(m_arrServerBlockLink, 0, sizeof(m_arrServerBlockLink));
}

void CWorldPlaneCollision::Load() {
    L2_GUARD_FUNCTION();

    for (int thread = 0; thread < ::CThreadManager::GetThreadCount(); ++thread) {
        m_CheckField[thread] = static_cast<int*>(std::malloc(static_cast<std::size_t>(m_nTotalCount) * sizeof(int)));
    }
}

bool CWorldPlaneCollision::IsInPlaneCollisionZone(const ::FVector& position) {
    L2_GUARD_FUNCTION();

    const int worldX = static_cast<int>(position.x) + 0x50000;
    const int worldY = static_cast<int>(position.y) + 0x44000;
    const int x = (worldX + ((worldX >> 31) & 0x1ff)) >> 9;
    const int y = (worldY + ((worldY >> 31) & 0x1ff)) >> 9;
    if (static_cast<unsigned int>(x) < COLLISION_X_SIZE && static_cast<unsigned int>(y) < COLLISION_Y_SIZE) {
        return m_arrPlaneCollisionLink[x][y] != nullptr;
    }
    return false;
}

void CWorldPlaneCollision::AddServerBlock(CAxisAlgnBB* serverBlock) {
    L2_GUARD_FUNCTION();

    int minimumX;
    int maximumX;
    int minimumY;
    int maximumY = 0;
    ConvertWorldToPlaneSection(
        static_cast<int>(serverBlock->m_Min.x), static_cast<int>(serverBlock->m_Max.x),
        static_cast<int>(serverBlock->m_Min.y), static_cast<int>(serverBlock->m_Max.y), minimumX, maximumX, minimumY,
        maximumY
    );

    for (int x = minimumX; x <= maximumX; ++x) {
        for (int y = minimumY; y <= maximumY; ++y) {
            m_arrServerBlockLink[x][y] = new CServerBlockLink(serverBlock, m_arrServerBlockLink[x][y]);
        }
    }
}

bool CWorldPlaneCollision::CheckCollision(
    const ::FVector& start,
    const ::FVector& end,
    const double radius,
    ::FVector& collisionPosition,
    const int instantZoneId,
    const bool mode
) {
    L2_GUARD_FUNCTION();

    const int thread = ::CThreadManager::GetThreadID();
    const int checkKey = IncrementInt32WithTargetWrap(m_CheckKey[thread]);

    int minimumX;
    int maximumX;
    int minimumY;
    int maximumY = 0;
    ConvertWorldToPlaneSection(
        static_cast<int>(start.x), static_cast<int>(end.x), static_cast<int>(start.y), static_cast<int>(end.y), minimumX,
        maximumX, minimumY, maximumY
    );

    for (int x = minimumX; x <= maximumX; ++x) {
        for (int y = minimumY; y <= maximumY; ++y) {
            for (CServerBlockLink* link = m_arrServerBlockLink[x][y]; link; link = link->m_pNext) {
                if (link->m_pServerBlock->Intersect(start, end)) {
                    collisionPosition = start;
                    return true;
                }
            }
        }
    }

    double nearestCollision = 1.0;
    for (int x = minimumX; x <= maximumX; ++x) {
        for (int y = minimumY; y <= maximumY; ++y) {
            for (CPlaneCollisionLink* link = m_arrPlaneCollisionLink[x][y]; link; link = link->m_pNext) {
                CPlaneCollision* const collision = link->m_pPlaneCollision;

                bool enabled = false;
                if (instantZoneId == 0) {
                    if (collision->m_InstantZoneKey.typeId == 0) {
                        enabled = collision->m_bEnabled;
                    }
                } else {
                    CInstantZone* zone = nullptr;
                    CGeneralObject* const object = CGeneralObject::GetObjectW(static_cast<std::uint32_t>(instantZoneId));
                    if (object && object->IsInZone()) {
                        zone = object->CastInZone();
                    }
                    if (zone) {
                        enabled = zone->GetCollisionCheck(collision);
                    }
                }

                if (!enabled || m_CheckField[thread][collision->m_nIndex] == checkKey) {
                    continue;
                }
                m_CheckField[thread][collision->m_nIndex] = checkKey;

                const double candidate = collision->CheckCollision(start, end, radius, mode);
                if (candidate < nearestCollision || candidate == nearestCollision) {
                    nearestCollision = collision->CheckCollision(start, end, radius, mode);
                }
            }
        }
    }

    if (nearestCollision == 1.0) {
        return false;
    }

    collisionPosition = ::FVector{
        start.x + (end.x - start.x) * nearestCollision,
        start.y + (end.y - start.y) * nearestCollision,
        start.z + (end.z - start.z) * nearestCollision,
    };
    return true;
}

void CWorldPlaneCollision::SetEnabled(::std::u16string name, const bool enabled) {
    // Current L2Server 0x00c6d5f8.
    L2_GUARD_FUNCTION();

    const auto position = m_mapNameToCollisionVector.find(name);
    if (position == m_mapNameToCollisionVector.end()) {
        const std::string hostName = Utf16ToUtf8(runtime::StringView(name));
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] SetEnableCollision failed. "
        //     L"undefined collision[%s]",
        //     L"WorldCollision.cpp",
        //     0x26d,
        //     hostName.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] SetEnableCollision failed. "
        //     L"undefined collision[%s]",
        //     L"WorldCollision.cpp",
        //     0x26d,
        //     hostName.c_str())
        L2_LOG_ERROR(
            Log, "Cannot %s world collision '%s': no collision group with that name is registered.",
            enabled ? "enable" : "disable", hostName.c_str()
        );
        runtime::process::ReportCallStack(LOG_ERROR, false);
        return;
    }

    for (int index = 0; static_cast<std::size_t>(index) < position->second.size(); ++index) {
        position->second[static_cast<std::size_t>(index)]->m_bEnabled = enabled;
    }
}

void CWorldPlaneCollision::ShowBlockTerritory(User& user, int radius) {
    // Current L2Server 0x00c6db60.
    L2_GUARD_FUNCTION();

    if (radius < 0) {
        radius = 0;
    } else if (radius > 0x800) {
        radius = 0x800;
    }

    const FVector position = user.GetPos();
    const double distance = static_cast<double>(radius);
    int minimumX = 0;
    int maximumX = 0;
    int minimumY = 0;
    int maximumY = 0;
    ConvertWorldToPlaneSection(
        runtime::Win64CvttDoubleToInt32(position.x - distance), runtime::Win64CvttDoubleToInt32(position.x + distance),
        runtime::Win64CvttDoubleToInt32(position.y - distance), runtime::Win64CvttDoubleToInt32(position.y + distance),
        minimumX, maximumX, minimumY, maximumY
    );

    for (int x = minimumX; x <= maximumX; ++x) {
        for (int y = minimumY; y <= maximumY; ++y) {
            CServerBlockLink* link = m_arrServerBlockLink[x][y];
            while (link != nullptr) {
                CTerritory territory;
                link->m_pServerBlock->GetTerritory(territory);
                territory.ShowTerritoryByAdena(user);
                link = link->m_pNext;
            }
        }
    }
}

void CWorldPlaneCollision::GetPlaneCollisionVector(::std::u16string name, CollisionVector& collisions) {
    L2_GUARD_FUNCTION();

    collisions.clear();
    const auto position = m_mapNameToCollisionVector.find(name);
    if (position == m_mapNameToCollisionVector.end()) {
        return;
    }
    for (int index = 0; static_cast<std::size_t>(index) < position->second.size(); ++index) {
        collisions.push_back(position->second[static_cast<std::size_t>(index)]);
    }
}

void CWorldPlaneCollision::Add(::std::u16string name, CPlaneCollision* collision) {
    L2_GUARD_FUNCTION();

    collision->m_nIndex = m_nTotalCount;
    ++m_nTotalCount;

    const auto position = m_mapNameToCollisionVector.find(name);
    if (position == m_mapNameToCollisionVector.end()) {
        CollisionVector collisions;
        collisions.push_back(collision);
        m_mapNameToCollisionVector[name] = collisions;
    } else {
        m_mapNameToCollisionVector[name].push_back(collision);
    }

    int minimumX;
    int maximumX;
    int minimumY;
    int maximumY = 0;
    ConvertWorldToPlaneSection(
        static_cast<int>(collision->m_dMinX), static_cast<int>(collision->m_dMaxX), static_cast<int>(collision->m_dMinY),
        static_cast<int>(collision->m_dMaxY), minimumX, maximumX, minimumY, maximumY
    );

    for (int x = minimumX; x <= maximumX; ++x) {
        for (int y = minimumY; y <= maximumY; ++y) {
            CPlaneCollisionLink* link = new CPlaneCollisionLink;
            link->m_pPlaneCollision = collision;
            link->m_pNext = m_arrPlaneCollisionLink[x][y];
            m_arrPlaneCollisionLink[x][y] = link;
        }
    }
}

void CWorldPlaneCollision::ConvertWorldToPlaneSection(
    const int firstX,
    const int secondX,
    const int firstY,
    const int secondY,
    int& minimumX,
    int& maximumX,
    int& minimumY,
    int& maximumY
) {
    const int firstWorldX = firstX + 0x50000;
    const int secondWorldX = secondX + 0x50000;
    const int firstWorldY = firstY + 0x44000;
    const int secondWorldY = secondY + 0x44000;
    const int firstSectionX = (firstWorldX + ((firstWorldX >> 31) & 0x1ff)) >> 9;
    const int secondSectionX = (secondWorldX + ((secondWorldX >> 31) & 0x1ff)) >> 9;
    const int firstSectionY = (firstWorldY + ((firstWorldY >> 31) & 0x1ff)) >> 9;
    const int secondSectionY = (secondWorldY + ((secondWorldY >> 31) & 0x1ff)) >> 9;

    minimumX = std::min(firstSectionX, secondSectionX);
    maximumX = std::max(firstSectionX, secondSectionX);
    minimumY = std::min(firstSectionY, secondSectionY);
    maximumY = std::max(firstSectionY, secondSectionY);
}

CPlaneCollision::CPlaneCollision(
    const ::FVector& start, const ::FVector& end, const double height, const InstantZoneKey& instantZoneKey
) :
    m_nIndex(0), m_bEnabled(true), m_vStart(start), m_vEnd(end), m_dHeight(height), m_InstantZoneKey(instantZoneKey) {
    L2_GUARD_FUNCTION();

    m_dMinZ = std::min(start.z, end.z);
    m_vStart.z = m_dMinZ;
    m_vEnd.z = m_dMinZ;
    m_dMinX = std::min(start.x, end.x);
    m_dMinY = std::min(start.y, end.y);
    m_dMaxX = std::max(start.x, end.x);
    m_dMaxY = std::max(start.y, end.y);
}

double CPlaneCollision::CheckCollision(const ::FVector& start, const ::FVector& end, const double radius, const bool mode) {
    L2_GUARD_FUNCTION();

    if (start.z < m_vStart.z - 100.0 || m_vStart.z + m_dHeight - 40.0 < start.z) {
        return 1.0;
    }

    const ::FVector planeStart(m_vStart);
    const ::FVector planeEnd(m_vEnd);
    ::FVector offset{
        -(planeEnd.y - planeStart.y),
        planeEnd.x - planeStart.x,
        0.0,
    };
    const ::FVector normal = offset.SafeNormal();
    offset = ::FVector{
        radius * normal.x,
        radius * normal.y,
        radius * normal.z,
    };

    ::FVector shiftedStart{};
    ::FVector shiftedEnd{};
    if (mode) {
        shiftedStart = start;
        shiftedEnd = ::FVector{
            offset.x + end.x,
            offset.y + end.y,
            offset.z + end.z,
        };
    } else {
        shiftedStart = ::FVector{
            offset.x + start.x,
            offset.y + start.y,
            offset.z + start.z,
        };
        shiftedEnd = ::FVector{
            offset.x + end.x,
            offset.y + end.y,
            offset.z + end.z,
        };

        if (IsIntersectionNew(planeStart, planeEnd, start, shiftedStart)) {
            if (IsIntersectionNew(planeStart, planeEnd, shiftedStart, shiftedEnd)) {
                if (!IsIntersectionNew(planeStart, planeEnd, start, shiftedEnd)) {
                    return 1.0;
                }
            } else {
                const ::FVector intersection = FindIntersectionPoint(planeStart, planeEnd, start, shiftedStart);
                if (intersection.z != 0.0) {
                    return 0.0;
                }
            }
        }
    }

    if (!IsIntersectionNew(planeStart, planeEnd, shiftedStart, shiftedEnd)) {
        if (!IsIntersectionNew(planeStart, planeEnd, start, end)) {
            return 1.0;
        }

        const ::FVector intersection = FindIntersectionPoint(planeStart, planeEnd, start, end);
        if (intersection.z != 0.0) {
            return 0.0;
        }
    }

    const ::FVector intersection = FindIntersectionPoint(planeStart, planeEnd, shiftedStart, shiftedEnd);
    if (intersection.z == 0.0) {
        return 1.0;
    }

    const double intersectionY = intersection.y / intersection.z - offset.y;
    const double intersectionX = intersection.x / intersection.z - offset.x;
    const double deltaY = start.y - intersectionY;
    const double deltaX = start.x - intersectionX;
    double correctedDistanceSquared = deltaY * deltaY + deltaX * deltaX - 5.0;
    if (!(correctedDistanceSquared > 0.0)) {
        correctedDistanceSquared = 0.0;
    }

    const double distance = std::sqrt(correctedDistanceSquared);
    const double pathDeltaX = start.x - end.x;
    const double pathDeltaY = start.y - end.y;
    const double pathDistance = std::sqrt(pathDeltaX * pathDeltaX + pathDeltaY * pathDeltaY);
    const double ratio = distance / pathDistance;

    if (!IsIntersectionNew(planeStart, planeEnd, start, end)) {
        return ratio;
    }

    const ::FVector point{
        start.x + (end.x - start.x) * ratio,
        start.y + (end.y - start.y) * ratio,
        start.z + (end.z - start.z) * ratio,
    };
    if (!IsIntersectionNew(planeStart, planeEnd, start, point)) {
        return ratio;
    }
    return 0.0;
}
