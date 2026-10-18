#include "Shared/src/Territory.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/GeoData.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"
#include "Shared/src/World.h"
#include "TextRuntime.h"

#if defined(L2_BUILD_L2SERVER)
#include "CWorld.h"
#include "Creature.h"
#include "User.h"
#include "UserSocket.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

std::int32_t WrappedArea2(const FVector2D& first, const FVector2D& second, const FVector2D& third) {
    const auto Multiply = [](double left, double right) {
        return static_cast<std::uint32_t>(runtime::Win64CvttDoubleToInt32(left)) *
            static_cast<std::uint32_t>(runtime::Win64CvttDoubleToInt32(right));
    };
    const std::uint32_t positive = Multiply(first.x, second.y) + Multiply(second.x, third.y) + Multiply(third.x, first.y);
    const std::uint32_t negative = Multiply(first.x, third.y) + Multiply(second.x, first.y) + Multiply(third.x, second.y);
    const std::uint32_t bits = positive - negative;
    return runtime::Int32FromBits(bits);
}

} // namespace

bool CTerritory::IsLeft(const FVector2D& first, const FVector2D& second, const FVector2D& third) {
    return WrappedArea2(first, second, third) > 0;
}

bool CTerritory::IsStraight(const FVector2D& first, const FVector2D& second, const FVector2D& third) {
    return WrappedArea2(first, second, third) == 0;
}

bool CTerritory::CheckIntersectAndGetPoint(FVector& begin, FVector& end, const bool includeZ) {
    L2_GUARD_FUNCTION();

    const FVector2D begin2D{begin.x, begin.y};
    const FVector2D end2D{end.x, end.y};
    FVector closest = end;
    bool intersected = false;

    if (!IsCollideRough(begin, end, includeZ) || (IsInside(end, includeZ) && IsInside(begin, includeZ))) {
        return false;
    }

    const int pointCount = static_cast<int>(m_TerritoryPoints.size());
    for (int index = 0; index < pointCount; ++index) {
        const FVector2D& first = runtime::CheckedVectorAt(m_TerritoryPoints, static_cast<std::size_t>(index));
        const FVector2D& second = runtime::CheckedVectorAt(m_TerritoryPoints, static_cast<std::size_t>((index + 1) % pointCount));
        if (!FVector2D::IsIntersect(begin2D, end2D, first, second)) {
            continue;
        }

        const FVector firstLineStart{first.x, first.y, 1.0};
        const FVector firstLineEnd{second.x, second.y, 1.0};
        const FVector secondLineStart{begin.x, begin.y, 1.0};
        const FVector secondLineEnd{end.x, end.y, 1.0};

        const double firstX = firstLineStart.y * firstLineEnd.z - firstLineStart.z * firstLineEnd.y;
        const double firstY = firstLineStart.z * firstLineEnd.x - firstLineStart.x * firstLineEnd.z;
        const double firstZ = firstLineStart.x * firstLineEnd.y - firstLineStart.y * firstLineEnd.x;
        const double secondX = secondLineStart.y * secondLineEnd.z - secondLineStart.z * secondLineEnd.y;
        const double secondY = secondLineStart.z * secondLineEnd.x - secondLineStart.x * secondLineEnd.z;
        const double secondZ = secondLineStart.x * secondLineEnd.y - secondLineStart.y * secondLineEnd.x;

        const double crossX = firstY * secondZ - firstZ * secondY;
        const double crossY = firstZ * secondX - firstX * secondZ;
        const double crossZ = firstX * secondY - firstY * secondX;
        FVector point{
            crossX / crossZ,
            crossY / crossZ,
            0.0,
        };
        point.z = static_cast<double>(
#if defined(L2_BUILD_L2SERVER)
            ::CWorld::Inst().GetGeo()->FindGround(
#else
            l2geo::CWorld::Inst().GetGeo()->FindGround(
#endif
                static_cast<int>(point.x), static_cast<int>(point.y), static_cast<int>(end.z)
            )
        );
        intersected = true;
        if (begin.DistCube(point) < begin.DistCube(closest)) {
            closest = point;
        }
    }

    end = closest;
    if (intersected) {
        FVector direction{
            begin.x - end.x,
            begin.y - end.y,
            begin.z - end.z,
        };
        direction.NormalizeCube();
        end = FVector{
            end.x + direction.x * 10.0,
            end.y + direction.y * 10.0,
            end.z + direction.z * 10.0,
        };
    }
    return intersected;
}

void CTerritory::MakeRectangle(FVector begin, FVector end, FVector direction, double halfWidth) {
    L2_GUARD_FUNCTION();

    const FVector leftOffset{-direction.y * halfWidth, direction.x * halfWidth, direction.z * halfWidth};
    const FVector rightOffset{direction.y * halfWidth, -direction.x * halfWidth, direction.z * halfWidth};

    FVector first{begin.x + rightOffset.x, begin.y + rightOffset.y, begin.z + rightOffset.z};
    FVector second{begin.x + leftOffset.x, begin.y + leftOffset.y, begin.z + leftOffset.z};
    FVector third{end.x + leftOffset.x, end.y + leftOffset.y, end.z + leftOffset.z};
    FVector fourth{end.x + rightOffset.x, end.y + rightOffset.y, end.z + rightOffset.z};

    AddTerritoryPoint(&first);
    AddTerritoryPoint(&second);
    AddTerritoryPoint(&third);
    AddTerritoryPoint(&fourth);
}

void CTerritory::AddTerritoryPoint(FVector* point) {
    L2_GUARD_FUNCTION();
    AddTerritoryPoint(runtime::Win64CvttDoubleToInt32(point->x), runtime::Win64CvttDoubleToInt32(point->y));
}

FVector CTerritory::GetTerritoryPoint3(int index) const {
    const FVector2D& point = runtime::CheckedVectorAt(m_TerritoryPoints, static_cast<std::size_t>(index));
    return FVector{static_cast<double>(runtime::Win64CvttDoubleToInt32(point.x)),
        static_cast<double>(runtime::Win64CvttDoubleToInt32(point.y)),
        static_cast<double>(runtime::AddInt32Wrapping(m_Bottom, m_Top) / 2)};
}

FVector CTerritory::GetCenterPos() {
    if (m_TerritoryPoints.empty()) {
        const std::wstring name = Utf16ToWide(sName.data());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[CTerritory] name [%s] GetCenterPos() n == 0",
        //     name.c_str())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[territory] Cannot calculate the center of \"%ls\": the polygon has no points. Returning (0, 0, 0).",
        //     name.empty() ? L"(unnamed)" : name.c_str())
        L2_LOG_ERROR(
            Log, u"[territory] Cannot calculate the center of \"%ls\": the polygon has no points. Returning (0, 0, 0).",
            name.empty() ? L"(unnamed)" : name.c_str()
        );
        return {};
    }

    double sumX = 0.0;
    double sumY = 0.0;
    for (const FVector2D& point : m_TerritoryPoints) {
        sumX += point.x;
        sumY += point.y;
    }
    const double inverseCount = 1.0 / static_cast<double>(m_TerritoryPoints.size());
    return FVector{
        sumX * inverseCount, sumY * inverseCount, static_cast<double>(runtime::AddInt32Wrapping(m_Bottom, m_Top)) * 0.5};
}

int CTerritory::GetMaxDistanceBy2D(const FVector& position) {
    int maximum = 0;
    for (const FVector2D& point : m_TerritoryPoints) {
        const double deltaX = point.x - position.x;
        const double deltaY = point.y - position.y;
        const int distance = runtime::Win64CvttDoubleToInt32(std::sqrt(deltaX * deltaX + deltaY * deltaY));
        if (maximum < distance) {
            maximum = distance;
        }
    }
    return maximum;
}

int CTerritory::GetMaxDistanceFromCenterBy2D() {
    const FVector center = GetCenterPos();
    return GetMaxDistanceBy2D(center);
}

bool CTerritory::IsOddNodes(const FVector& position) {
    L2_GUARD_FUNCTION();
    bool oddNodes = false;
    std::size_t next = 1;
    const std::size_t count = m_TerritoryPoints.size();
    for (std::size_t current = 0; current < count; ++current) {
        if (next == count) {
            next = 0;
        }
        const FVector2D& first = runtime::CheckedVectorAt(m_TerritoryPoints, current);
        const FVector2D& second = runtime::CheckedVectorAt(m_TerritoryPoints, next);
        if (((first.y < position.y && position.y <= second.y) || (second.y < position.y && position.y <= first.y)) &&
            ((second.x - first.x) * (position.y - first.y)) / (second.y - first.y) < position.x - first.x) {
            oddNodes = !oddNodes;
        }
        ++next;
    }
    return oddNodes;
}

bool CTerritory::IsInsideRough(const FVector& position, bool includeZ) {
    L2_GUARD_FUNCTION();

    const int x = runtime::Win64CvttDoubleToInt32(position.x);
    const int y = runtime::Win64CvttDoubleToInt32(position.y);
    const int z = runtime::Win64CvttDoubleToInt32(position.z);
    if (includeZ && (z < m_Bottom || m_Top < z)) {
        return false;
    }
    return m_West <= x && x <= m_East && m_North <= y && y <= m_South;
}

bool CTerritory::IsInside(CTerritory& territory, bool includeZ) {
    L2_GUARD_FUNCTION();

    const int pointCount = territory.GetTerritoryPointSize();
    for (int index = 0; index < pointCount; ++index) {
        const FVector2D* const point = territory.GetTerritoryPoint(index);
        FVector position{point->x, point->y, static_cast<double>(territory.m_Bottom)};
        if (!IsInside(position, includeZ)) {
            return false;
        }
        position.z = static_cast<double>(territory.m_Top);
        if (!IsInside(position, includeZ)) {
            return false;
        }
    }
    return true;
}

bool CTerritory::IsInside(const FVector& position, bool includeZ) {
    L2_GUARD_FUNCTION();
    if (m_TerritoryPoints.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "GetTerritoryPointSize() <= 0 "
        //     "at file[%s], line[%d]",
        //     "..\\Shared\\Territory.cpp",
        //     0xe4)
        // Log.Add(
        //     LOG_ERROR,
        //     "[territory] Cannot check position (%.17g, %.17g, %.17g) in \"%s\": "
        //     "the polygon has no points. Treating the position as outside.",
        //     position.x, position.y, position.z,
        //     sName.empty() ? "(unnamed)" : Utf16ToUtf8(sName.data()).c_str())
        L2_LOG_ERROR(
            Log,
            "[territory] Cannot check position (%.17g, %.17g, %.17g) in \"%s\": "
            "the polygon has no points. Treating the position as outside.",
            position.x, position.y, position.z, sName.empty() ? "(unnamed)" : Utf16ToUtf8(sName.data()).c_str()
        );
        return false;
    }
    const int z = runtime::Win64CvttDoubleToInt32(position.z);
    if (includeZ && (z < m_Bottom || m_Top < z)) {
        return false;
    }
    return IsOddNodes(position);
}

bool CTerritory::IsInsideWithLogVisitor(const FVector& position, ZLogVisitor& visitor, bool includeZ, int& reason) {
    L2_GUARD_FUNCTION();
    reason = 0;
    if (m_TerritoryPoints.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "GetTerritoryPointSize()<=0 "
        //     "at file[%s], line[%d]",
        //     "..\\Shared\\Territory.cpp",
        //     0x11b)
        // Log.Add(
        //     LOG_ERROR,
        //     "[territory] Cannot check position (%.17g, %.17g, %.17g) in \"%s\": "
        //     "the polygon has no points. Treating the position as outside (reason 1).",
        //     position.x, position.y, position.z,
        //     sName.empty() ? "(unnamed)" : Utf16ToUtf8(sName.data()).c_str())
        L2_LOG_ERROR(
            Log,
            "[territory] Cannot check position (%.17g, %.17g, %.17g) in \"%s\": "
            "the polygon has no points. Treating the position as outside (reason 1).",
            position.x, position.y, position.z, sName.empty() ? "(unnamed)" : Utf16ToUtf8(sName.data()).c_str()
        );
        reason = 1;
        return false;
    }

    if (includeZ) {
        const int z = runtime::Win64CvttDoubleToInt32(position.z);
        if (z < m_Bottom) {
            visitor.SetMinZ(std::min(visitor.GetMinZ(), m_Bottom));
            reason = 2;
            return false;
        }
        if (m_Top < z) {
            visitor.SetMaxZ(std::max(visitor.GetMaxZ(), m_Top));
            reason = 3;
            return false;
        }
    }

    if (!IsOddNodes(position)) {
        reason = 4;
        return false;
    }
    return true;
}

bool CTerritory::IsCollideRough(const FVector& begin, const FVector& end, bool includeZ) {
    L2_GUARD_FUNCTION();

    const int beginX = runtime::Win64CvttDoubleToInt32(begin.x);
    const int beginY = runtime::Win64CvttDoubleToInt32(begin.y);
    const int beginZ = runtime::Win64CvttDoubleToInt32(begin.z);
    const int endX = runtime::Win64CvttDoubleToInt32(end.x);
    const int endY = runtime::Win64CvttDoubleToInt32(end.y);
    const int endZ = runtime::Win64CvttDoubleToInt32(end.z);

    const bool beginOutsideZ = beginZ < m_Bottom || m_Top < beginZ;
    const bool endOutsideZ = endZ < m_Bottom || m_Top < endZ;
    if (includeZ && beginOutsideZ && endOutsideZ) {
        return false;
    }
    if ((beginX < m_West && endX < m_West) || (m_East < beginX && m_East < endX) || (beginY < m_North && endY < m_North) ||
        (m_South < beginY && m_South < endY)) {
        return false;
    }
    return true;
}

CTerritory::CollisionResult CTerritory::IsCollide(const FVector& begin, const FVector& end, bool includeZ) {
    L2_GUARD_FUNCTION();

    if (!IsCollideRough(begin, end, includeZ)) {
        return CR_NOT_PASS;
    }

    if (IsInside(end, includeZ)) {
        return IsInside(begin, includeZ) ? CR_STAY_IN : CR_STEP_IN;
    }
    if (IsInside(begin, includeZ)) {
        return CR_STEP_OUT;
    }

    const FVector2D begin2D{begin.x, begin.y};
    const FVector2D end2D{end.x, end.y};
    const int pointCount = GetTerritoryPointSize();
    for (int index = 0; index < pointCount; ++index) {
        if (FVector2D::IsIntersect(
                begin2D, end2D, runtime::CheckedVectorAt(m_TerritoryPoints, static_cast<std::size_t>(index)),
                runtime::CheckedVectorAt(m_TerritoryPoints, static_cast<std::size_t>((index + 1) % pointCount))
            )) {
            return CR_PASS;
        }
    }
    return CR_NOT_PASS;
}

void CTerritory::AssembleTerritoryInfo(char* buffer, unsigned int& used, unsigned int& capacity) {
    L2_GUARD_FUNCTION();

    int written =
        ::Assemble(buffer + used, static_cast<int>(capacity - used), "ddd", GetTerritoryPointSize(), m_Top, m_Bottom);
    if (written > 0) {
        used += static_cast<unsigned int>(written);
    }
    for (const FVector2D& point : m_TerritoryPoints) {
        written = ::Assemble(
            buffer + used, static_cast<int>(capacity - used), "dd", runtime::Win64CvttDoubleToInt32(point.x),
            runtime::Win64CvttDoubleToInt32(point.y)
        );
        if (written > 0) {
            used += static_cast<unsigned int>(written);
        }
    }
}

FVector CTerritory::PickRandomPos(bool sky) {
    L2_GUARD_FUNCTION();
    if (m_TerritoryPoints.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     "GetTerritoryPointSize() <= 0 "
        //     "at file[%s], line[%d]",
        //     "..\\Shared\\Territory.cpp",
        //     0x1a8)
        // Log.Add(
        //     LOG_ERROR,
        //     "[territory] Cannot pick a random position in \"%s\": the polygon has no points. Returning (0, 0, 0).",
        //     sName.empty() ? "(unnamed)" : Utf16ToUtf8(sName.data()).c_str())
        L2_LOG_ERROR(
            Log, "[territory] Cannot pick a random position in \"%s\": the polygon has no points. Returning (0, 0, 0).",
            sName.empty() ? "(unnamed)" : Utf16ToUtf8(sName.data()).c_str()
        );
        return {};
    }

    FVector position{};
    bool inside = false;
    int attempts = 0;
    do {
        double weightedX = 0.0;
        double weightedY = 0.0;
        double weightSum = 0.0;
        for (const FVector2D& point : m_TerritoryPoints) {
            const double weight = genrand64_real1();
            weightedX += point.x * weight;
            weightedY += point.y * weight;
            weightSum += weight;
        }
        position.x = weightedX / weightSum;
        position.y = weightedY / weightSum;
        position.z = 0.0;
        ++attempts;
        inside = IsOddNodes(position);
    } while (!inside && attempts < 50);

    if (attempts >= 50) {
        const std::wstring name = Utf16ToWide(sName.data());
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] random pos found out of "
        //     L"territory [%s]",
        //     L"..\\Shared\\Territory.cpp",
        //     0x1c7,
        //     name.c_str())
        // Log.Add(
        //     LOG_WARN,
        //     L"[territory] Random position selection reached %d attempts in \"%ls\". "
        //     L"using a randomly selected polygon vertex for X/Y. Last candidate (%.17g, %.17g) was %ls the polygon.",
        //     attempts,
        //     name.empty() ? L"(unnamed)" : name.c_str(),
        //     position.x, position.y,
        //     inside ? L"inside" : L"outside")
        L2_LOG_WARN(
            Log,
            u"[territory] Random position selection reached %d attempts in \"%ls\"; "
            u"using a randomly selected polygon vertex for X/Y. Last candidate (%.17g, %.17g) was %ls the polygon.",
            attempts, name.empty() ? L"(unnamed)" : name.c_str(), position.x, position.y, inside ? L"inside" : L"outside"
        );
        const std::size_t index =
            static_cast<std::size_t>(genrand64_real2() * static_cast<double>(m_TerritoryPoints.size()));
        position.x = runtime::CheckedVectorAt(m_TerritoryPoints, index).x;
        position.y = runtime::CheckedVectorAt(m_TerritoryPoints, index).y;
    }

    if (sky) {
        position.z = static_cast<double>(static_cast<int>(
            genrand64_real2() *
                static_cast<double>(static_cast<std::int64_t>(m_Top) + 1 - static_cast<std::int64_t>(m_Bottom)) +
            static_cast<double>(m_Bottom)
        ));
    } else {
        position.z = static_cast<double>(m_Top);
        position.z = static_cast<double>(
#if defined(L2_BUILD_L2SERVER)
            ::CWorld::Inst()
                .
#else
            l2geo::CWorld::Inst()
                .
#endif
            GetGeo()
                ->FindGround(position)
        );
    }
    return position;
}

#if defined(L2_BUILD_L2SERVER)

bool CTerritory::GoPoint(CCreature& creature, const int index) {
    // Current L2Server 0x00ae5828.
    L2_GUARD_FUNCTION();

    const int pointCount = GetTerritoryPointSize();
    if (pointCount <= 0) {
        return false;
    }

    const FVector2D& point = runtime::CheckedVectorAt(m_TerritoryPoints, static_cast<std::size_t>(index % pointCount));
    const std::int32_t x = runtime::Win64CvttDoubleToInt32(point.x);
    const std::int32_t y = runtime::Win64CvttDoubleToInt32(point.y);
    const std::int32_t zoneX = (runtime::AddInt32Wrapping(x, 0x50000) >> 15) + 10;
    const std::int32_t zoneY = (runtime::AddInt32Wrapping(y, 0x40000) >> 15) + 10;
    if (::CWorld::Inst().GetGeo()->GetZone(zoneX, zoneY) == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     "Try to teleport to the invalid Position "
        //     "at file[%s], line[%d]",
        //     "..\\Shared\\Territory.cpp",
        //     0x2a8)
        // Log.Add(
        //     LOG_ERROR,
        //     "[territory] Teleport rejected for point index %d in \"%s\": "
        //     "position (%d, %d) has no geodata zone (%d, %d).",
        //     index,
        //     sName.empty() ? "(unnamed)" : Utf16ToUtf8(sName.data()).c_str(),
        //     x, y, zoneX, zoneY)
        L2_LOG_ERROR(
            Log,
            "[territory] Teleport rejected for point index %d in \"%s\": "
            "position (%d, %d) has no geodata zone (%d, %d).",
            index, sName.empty() ? "(unnamed)" : Utf16ToUtf8(sName.data()).c_str(), x, y, zoneX, zoneY
        );
        if (creature.IsPC()) {
            creature.CastUser()->SendSystemMessage(u"SYS", u"error to teleport");
        }
        return false;
    }

    creature.StopMove();
    const std::int32_t middleZ = runtime::AddInt32Wrapping(m_Top, m_Bottom) / 2;
    const std::int32_t z = ::CWorld::Inst().GetGeo()->FindGround(x, y, middleZ);
    return creature.TeleportToLocation(x, y, z, creature.GetInZoneID(), 0x10000, TeleportSourceType::GoPoint, nullptr, 0);
}

bool CTerritory::GoFirstPoint(CCreature& creature) {
    // Current L2Server 0x00ae6304.
    L2_GUARD_FUNCTION();
    return GoPoint(creature, 0);
}

bool CTerritory::GoPointRandom(CCreature& creature) {
    // Current L2Server 0x00ae63b0.
    L2_GUARD_FUNCTION();

    const FVector position = PickRandomPos(false);
    creature.StopMove();
    static_cast<void>(creature.TeleportToLocation(
        runtime::Win64CvttDoubleToInt32(position.x), runtime::Win64CvttDoubleToInt32(position.y),
        runtime::Win64CvttDoubleToInt32(position.z), creature.GetInZoneID(), 0x10000, TeleportSourceType::BuilderCmd,
        nullptr, 0
    ));
    return true;
}

void CTerritory::ShowLineByAdena(CCreature& creature, const FVector& begin, const FVector& end) {
    // Current L2Server 0x00ae6a9c.
    L2_GUARD_FUNCTION();

    const FVector direction =
        FVector{
            end.x - begin.x,
            end.y - begin.y,
            end.z - begin.z,
        }
            .SafeNormal();
    const double deltaX = begin.x - end.x;
    const double deltaY = begin.y - end.y;
    const double deltaZ = begin.z - end.z;
    const double distance = std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);

    std::vector<FVector> points;
    points.push_back(begin);
    for (double offset = 200.0; offset < distance; offset += 200.0) {
        points.push_back(
            FVector{
                begin.x + direction.x * offset,
                begin.y + direction.y * offset,
                begin.z + direction.z * offset,
            }
        );
    }
    points.push_back(end);
    creature.ShowBlocks(points);
}

void CTerritory::ShowTerritoryByAdena(CCreature& creature) {
    // Current L2Server 0x00ae6fa0.
    L2_GUARD_FUNCTION();

    const int pointCount = GetTerritoryPointSize();
    if (pointCount <= 1) {
        return;
    }

    const double middleZ = static_cast<double>(runtime::AddInt32Wrapping(m_Bottom, m_Top) / 2);
    const auto MakePoint = [this, middleZ](const int index) {
        const FVector2D& point = runtime::CheckedVectorAt(m_TerritoryPoints, static_cast<std::size_t>(index));
        return FVector{
            runtime::Win64CvttDoubleToInt32(point.x),
            runtime::Win64CvttDoubleToInt32(point.y),
            runtime::Win64CvttDoubleToInt32(middleZ),
        };
    };

    if (pointCount == 2) {
        const FVector begin = MakePoint(0);
        const FVector end = MakePoint(1);
        ShowLineByAdena(creature, begin, end);
        return;
    }

    for (int index = 0; index + 1 < pointCount; ++index) {
        const FVector begin = MakePoint(index);
        const FVector end = MakePoint(index + 1);
        ShowLineByAdena(creature, begin, end);
    }
    const FVector begin = MakePoint(pointCount - 1);
    const FVector end = MakePoint(0);
    ShowLineByAdena(creature, begin, end);

    if (!creature.IsPC()) {
        return;
    }

    char buffer[0x5000]{};
    int packetLength = 0;
    for (const FVector2D& point : m_TerritoryPoints) {
        const int written = ::Assemble(
            buffer + packetLength, static_cast<int>(sizeof(buffer)) - packetLength, "dd",
            runtime::Win64CvttDoubleToInt32(point.x), runtime::Win64CvttDoubleToInt32(point.y)
        );
        if (written > 0) {
            packetLength += written;
        }
    }
    creature.CastUser()->GetSocket()->Send("chdddb", 0xfe, 0x8d, pointCount, m_Bottom, m_Top, packetLength, buffer);
}

#endif

CTerritory* CTerritory::DisassembleTerritoryInfo(const unsigned char** packet) {
    L2_GUARD_FUNCTION();

    CTerritory* const territory = new CTerritory();
    int pointCount = 0;
    int top = 0;
    int bottom = 0;
    *packet = ::Disassemble(*packet, "ddd", &pointCount, &top, &bottom);
    territory->m_Top = top;
    territory->m_Bottom = bottom;

    for (int index = 0; index < pointCount; ++index) {
        int x = 0;
        int y = 0;
        *packet = ::Disassemble(*packet, "dd", &x, &y);
        territory->AddTerritoryPoint(x, y);
    }
    return territory;
}

bool CTerritory::IsConvex() {
    L2_GUARD_FUNCTION();

    const int pointCount = GetTerritoryPointSize();
    if (pointCount < 3) {
        return false;
    }
    if (pointCount == 3) {
        return true;
    }

    bool left = IsLeft(
        runtime::CheckedVectorAt(m_TerritoryPoints, static_cast<std::size_t>(pointCount - 2)),
        runtime::CheckedVectorAt(m_TerritoryPoints, static_cast<std::size_t>(pointCount - 1)),
        runtime::CheckedVectorAt(m_TerritoryPoints, 0)
    );
    const FVector2D* previousPrevious = &runtime::CheckedVectorAt(m_TerritoryPoints, static_cast<std::size_t>(pointCount - 1));
    const FVector2D* previous = &runtime::CheckedVectorAt(m_TerritoryPoints, 0);
    for (int index = 1; index < pointCount; ++index) {
        const FVector2D* const current = &runtime::CheckedVectorAt(m_TerritoryPoints, static_cast<std::size_t>(index));
        if (!IsStraight(*previousPrevious, *previous, *current) && left != IsLeft(*previousPrevious, *previous, *current)) {
            return false;
        }
        previousPrevious = previous;
        previous = current;
    }
    return true;
}

bool CTerritory::DeleteTerritory() {
    L2_RELEASE(this, ObjectRefType::CreateDelete);
    return true;
}
