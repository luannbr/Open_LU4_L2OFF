#include "Path.h"

#include "CConfig.h"
#include "CWorld.h"
#include "UserSocket.h"

#include "Shared/src/GeoData.h"
#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Runtime/GeoDataMapping.h"
#include "Shared/src/Runtime/ResourceDirectories.h"
#include "Shared/src/Runtime/Utf16Format.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Shared/src/Runtime/WrappingArithmetic.h"
#include "Shared/src/StandardContainers.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/ThreadManager.h"
#include "Shared/src/Utils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <limits>
#include <memory>
#include <queue>
#include <string>
#include <vector>

namespace {

std::int32_t LineName = 0;
std::int32_t PointName = 0;

double Distance(const FVector& first, const FVector& second) {
    const double x = first.x - second.x;
    const double y = first.y - second.y;
    const double z = first.z - second.z;
    return std::sqrt(x * x + y * y + z * z);
}

using PathNodeMapping = l2geo::portable::detail::MappedFile;

constexpr std::size_t kPathNodeWorldInfoBytes = sizeof(PathNodeWorldInfo);
constexpr const char* kPathNodeWorldInfoMapping = "L2World_PathNodeWorldInfo";
constexpr const char* kPathNodeDataMapping = "L2World_PathNodeData";

#if defined(_WIN32)
constexpr const char* kPathNodeIndexPath = "..\\geodata\\pathnode.idx";
constexpr const wchar_t* kPathNodeDataLoadPath = L"..\\geodata\\pathnode.bin";
#else
// The shipped tree uses this casing. Windows' target path is kept
// above. POSIX must not rely on case-insensitive lookup.
constexpr const char* kPathNodeIndexPath = "../GeoData/pathnode.idx";
constexpr const wchar_t* kPathNodeDataLoadPath = L"../GeoData/pathnode.bin";
#endif

std::size_t PathNodeDataBytes(const std::int32_t maximumIndex) {
    if (maximumIndex < 0) {
        return 0;
    }
    return (static_cast<std::size_t>(maximumIndex) + 1U) * sizeof(CPathNode);
}

PathNodeMapping* CreatePathNodeMapping(const char* name, const std::size_t bytes, std::string& error) {
    auto mapping = std::make_unique<PathNodeMapping>();
    if (!mapping->CreateNamed(name, bytes, error)) {
        return nullptr;
    }
    return mapping.release();
}

PathNodeMapping* OpenPathNodeMapping(const char* name, const std::size_t bytes, std::string& error) {
    auto mapping = std::make_unique<PathNodeMapping>();
    if (!mapping->OpenNamed(name, bytes, error)) {
        return nullptr;
    }
    return mapping.release();
}

} // namespace

CPathNodeWorld g_pnWorld;
CNodeVisitHash g_NodeVisitHash[16];

double CPathNode::Dist(const CPathNode& other) const {
    const double deltaX = static_cast<double>(x - other.x);
    const double deltaY = static_cast<double>(y - other.y);
    const double deltaZ = static_cast<double>(z - other.z);
    return std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
}

FVector CPathNode::CastFVector() const { return FVector{x, y, z}; }

CPathFindingNode::CPathFindingNode(
    CPathNode* pathNode, CPathNode* startingPathNode, const double distance, const double lowerBound, const int depth
) noexcept :
    PathNode(pathNode), StartingPathNode(startingPathNode), Depth(depth), Dist(distance), lbound(lowerBound) {}

bool CPathFindingNode::operator<(const CPathFindingNode& other) const noexcept {
    return Dist + lbound > other.Dist + other.lbound;
}

CNodeVisitHash::CNodeVisitHash() noexcept : Touch(0), Pool{}, PoolCount(0), Hash{} {}

void CNodeVisitHash::Reset() noexcept {
    PoolCount = 0;
    ++Touch;
}

bool CNodeVisitHash::Visit(const std::uint32_t pathNodeIndex, const double distance, const bool update) {
    const std::uint32_t hashIndex = pathNodeIndex % HASH_SPACE;
    HashDelegate& delegate = Hash[hashIndex];
    int poolIndex;

    if (delegate.Touch == Touch) {
        poolIndex = delegate.PoolIndex;
        for (int index = poolIndex; index != -1; index = Pool[index].Next) {
            HashNode& node = Pool[index];
            if (static_cast<std::uint32_t>(node.PathNodeIndex) != pathNodeIndex) {
                continue;
            }
            if (node.Dist <= distance) {
                return false;
            }
            if (update) {
                node.Dist = distance;
            }
            return true;
        }
        if (!update) {
            return true;
        }
    } else {
        if (!update) {
            return true;
        }
        delegate.Touch = Touch;
        poolIndex = -1;
    }

    const int newIndex = PoolCount++;
    HashNode& node = Pool[newIndex];
    node.PathNodeIndex = static_cast<std::int32_t>(pathNodeIndex);
    node.Dist = distance;
    node.Next = poolIndex;
    delegate.PoolIndex = newIndex;
    return true;
}

CPathNodeWorld::CPathNodeWorld() noexcept :
    m_Data(nullptr), m_PathNodeDataHandle(nullptr), m_PathNodeWorldInfoHandle(nullptr), m_WorldInfo(nullptr) {}

void CPathNodeWorld::OpenData() {
    L2_GUARD_FUNCTION();

    std::string error;
    PathNodeMapping* const infoMapping = OpenPathNodeMapping(kPathNodeWorldInfoMapping, kPathNodeWorldInfoBytes, error);
    if (infoMapping == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] cannot open path-node world-info mapping: %S",
        //     L"Path.cpp",
        //     0x25,
        //     error.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] cannot open path-node world-info mapping: %S",
        //     L"Path.cpp",
        //     0x25,
        //     error.c_str())
        L2_LOG_ERROR(
            Log, "Cannot open path-node world mapping '%s' (%zu bytes): %s; mapping setup stops.", kPathNodeWorldInfoMapping,
            kPathNodeWorldInfoBytes, error.c_str()
        );
        return;
    }
    m_PathNodeWorldInfoHandle = infoMapping;
    m_WorldInfo = reinterpret_cast<PathNodeWorldInfo*>(infoMapping->MutableData());

    const std::size_t dataBytes = PathNodeDataBytes(m_WorldInfo->maxIndex);
    PathNodeMapping* const dataMapping = OpenPathNodeMapping(kPathNodeDataMapping, dataBytes, error);
    if (dataMapping == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] cannot open path-node data mapping: %S",
        //     L"Path.cpp",
        //     0x2b,
        //     error.c_str())
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] cannot open path-node data mapping: %S",
        //     L"Path.cpp",
        //     0x2b,
        //     error.c_str())
        L2_LOG_ERROR(
            Log, "Cannot open path-node data mapping '%s' (%zu bytes): %s; node data remains unavailable.",
            kPathNodeDataMapping, dataBytes, error.c_str()
        );
        m_Data = nullptr;
        return;
    }
    m_PathNodeDataHandle = dataMapping;
    m_Data = reinterpret_cast<CPathNode*>(dataMapping->MutableData());

    // Log.Add(
    //     LOG_ERROR,
    //     L"[NO_ERROR] MaxPathNodeIndex = %d",
    //     m_WorldInfo->maxIndex)
    // L2_LOG_ERROR(Log,
    //     L"[NO_ERROR] MaxPathNodeIndex = %d",
    //     m_WorldInfo->maxIndex)
    L2_LOG_INFO(Log, "Opened path-node mappings: maximum index %d, data size %zu bytes.", m_WorldInfo->maxIndex, dataBytes);
}

void CPathNodeWorld::LoadData() {
    L2_GUARD_FUNCTION();

    std::FILE* const indexFile = std::fopen(runtime::ResolveResourcePath(kPathNodeIndexPath).string().c_str(), "r");
    if (indexFile == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] pathnode.idx does't exist",
        //     L"Path.cpp",
        //     0x33)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] pathnode.idx does't exist",
        //     L"Path.cpp",
        //     0x33)
        L2_LOG_ERROR(Log, "Cannot open pathnode.idx; path-node loading stops.");
        return;
    }

    std::int32_t maximumIndex;
    std::fscanf(indexFile, "Total = %d\r\n", &maximumIndex);

    std::string error;
    PathNodeMapping* const infoMapping = CreatePathNodeMapping(kPathNodeWorldInfoMapping, kPathNodeWorldInfoBytes, error);
    m_PathNodeWorldInfoHandle = infoMapping;
    m_WorldInfo = reinterpret_cast<PathNodeWorldInfo*>(infoMapping->MutableData());
    std::memset(m_WorldInfo->SectionOffsetCount, 0, sizeof(m_WorldInfo->SectionOffsetCount));
    m_WorldInfo->maxIndex = maximumIndex;

    const std::int32_t signedDataBytes = runtime::MultiplyInt32Wrapping(
        runtime::AddInt32Wrapping(maximumIndex, 1), static_cast<std::int32_t>(sizeof(CPathNode))
    );
    const std::size_t dataBytes = static_cast<std::uint32_t>(signedDataBytes);
    PathNodeMapping* const dataMapping = CreatePathNodeMapping(kPathNodeDataMapping, dataBytes, error);
    m_PathNodeDataHandle = dataMapping;
    m_Data = reinterpret_cast<CPathNode*>(dataMapping->MutableData());

    int binaryByteCount;
    char* const binary = LoadBinaryAbsolute(kPathNodeDataLoadPath, binaryByteCount, false);
    if (binary == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] loading ..\\geodata\\pathnode.bin failed!",
        //     L"Path.cpp",
        //     0x51)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] loading ..\\geodata\\pathnode.bin failed!",
        //     L"Path.cpp",
        //     0x51)
        L2_LOG_ERROR(Log, "Cannot read pathnode.bin; path-node loading stops.");
        return;
    }
    std::memcpy(m_Data, binary, dataBytes);
    std::free(binary);
    if (m_Data == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] loading ..\\geodata\\pathnode.bin failed!",
        //     L"Path.cpp",
        //     0x61)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] loading ..\\geodata\\pathnode.bin failed!",
        //     L"Path.cpp",
        //     0x61)
        L2_LOG_ERROR(Log, "Path-node data mapping is null after the binary copy; path-node loading stops.");
        return;
    }

    std::uint32_t accumulatedCount = 0;
    for (std::int32_t mapX = 10; mapX < 30; ++mapX) {
        const std::int32_t worldX = -0x50000 + (mapX - 10) * 0x8000;
        for (std::int32_t mapY = 10; mapY < 27; ++mapY) {
            const std::int32_t worldY = -0x40000 + (mapY - 10) * 0x8000;
            std::int32_t fileMapX;
            std::int32_t fileMapY;
            std::int32_t zoneEntryCount;
            // std::fscanf(
            //     indexFile,
            //     "--- Zone(%d,%d)[%d] ---\r\n",
            //     &fileMapX,
            //     &fileMapY,
            //     &zoneEntryCount)
            const int zoneFieldsRead =
                std::fscanf(indexFile, "--- Zone(%d,%d)[%d] ---\r\n", &fileMapX, &fileMapY, &zoneEntryCount);
            if (fileMapX != mapX || fileMapY != mapY) {
                // Log.Add(
                //     LOG_ERROR,
                //     "[%s][%d] Invalid pathnode.bin!!!",
                //     L"Path.cpp",
                //     0x70)
                // L2_LOG_ERROR(Log,
                //     "[%s][%d] Invalid pathnode.bin!!!",
                //     L"Path.cpp",
                //     0x70)
                L2_LOG_ERROR(
                    Log,
                    "Invalid pathnode.idx zone header: read (%s, %s), expected (%d, %d), scan result %d (expected 3 "
                    "fields); loading stops with partial mapping state retained.",
                    zoneFieldsRead >= 1 ? std::to_string(fileMapX).c_str() : "<unread>",
                    zoneFieldsRead >= 2 ? std::to_string(fileMapY).c_str() : "<unread>", mapX, mapY, zoneFieldsRead
                );
                return;
            }

            auto& sections = m_WorldInfo->SectionOffsetCount[mapX - 10][mapY - 10];
            if (zoneEntryCount == 0) {
                const std::uint32_t value = accumulatedCount << 8U;
                for (auto& row : sections) {
                    std::fill(std::begin(row), std::end(row), value);
                }
                continue;
            }

            for (std::int32_t x = worldX; x < worldX + 0x8000; x += 0x100) {
                for (std::int32_t y = worldY; y < worldY + 0x8000; y += 0x100) {
                    std::int32_t fileX;
                    std::int32_t fileY;
                    std::int32_t signedCount;
                    // std::fscanf(
                    //     indexFile,
                    //     "%d %d %d\r\n",
                    //     &fileX,
                    //     &fileY,
                    //     &signedCount)
                    const int sectionFieldsRead = std::fscanf(indexFile, "%d %d %d\r\n", &fileX, &fileY, &signedCount);
                    if (fileX != x || fileY != y) {
                        // Log.Add(
                        //     LOG_ERROR,
                        //     L"[%s][%d] Invalid pathnode.bin ",
                        //     L"Path.cpp",
                        //     0x92)
                        // L2_LOG_ERROR(Log,
                        //     L"[%s][%d] Invalid pathnode.bin ",
                        //     L"Path.cpp",
                        //     0x92)
                        L2_LOG_ERROR(
                            Log,
                            "Invalid pathnode.idx section coordinates: read (%s, %s), expected (%d, %d) in zone (%d, %d), "
                            "scan result %d (expected 3 fields); loading stops with partial mapping state retained.",
                            sectionFieldsRead >= 1 ? std::to_string(fileX).c_str() : "<unread>",
                            sectionFieldsRead >= 2 ? std::to_string(fileY).c_str() : "<unread>", x, y, mapX, mapY,
                            sectionFieldsRead
                        );
                        return;
                    }
                    const std::uint32_t count = static_cast<std::uint32_t>(signedCount);
                    sections[(static_cast<std::uint32_t>(x) >> 8U) & 0x7fU][(static_cast<std::uint32_t>(y) >> 8U) & 0x7fU] =
                        ((accumulatedCount + 1U) << 8U) | count;
                    accumulatedCount += count;
                }
            }
        }
    }

    std::fclose(indexFile);
}

void CPathNodeWorld::Load() {
    L2_GUARD_FUNCTION();
    if (g_bUseResourceLoader) {
        OpenData();
    } else {
        LoadData();
    }
}

unsigned int CPathNodeWorld::GetMaxIndex() const noexcept { return static_cast<unsigned int>(m_WorldInfo->maxIndex); }

FVector CPathNodeWorld::GetPathNodePos(const int index) const { return m_Data[index].CastFVector(); }

CPathNode* CPathNodeWorld::GetPathNode(const std::uint32_t index) const {
    if (m_Data == nullptr || m_WorldInfo == nullptr || static_cast<std::uint32_t>(m_WorldInfo->maxIndex) < index) {
        return nullptr;
    }
    return m_Data + index;
}

std::uint32_t CPathNodeWorld::IndexFromPathNode(const CPathNode* node) const {
    return static_cast<std::uint32_t>(node - m_Data);
}

CPathNode* CPathNodeWorld::GetPathNodeArray(int x, int y, int& count) {
    // Current L2Server 0x0094dfec.
    L2_GUARD_FUNCTION();

    int i;
    unsigned int offset;
    int zone_x = ((x + 0x50000) >> 15) + 10;
    int zone_y = ((y + 0x40000) >> 15) + 10;
    if (zone_x - 10 < 0 || zone_y - 10 < 0 || zone_x - 10 >= 20 || zone_y - 10 >= 17) {
        static int err_count = 0;
        if (err_count++ < 100) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] invalid pos(%d, %d)  zone[%d][%d] ",
            //     L"Path.cpp",
            //     0xe3,
            //     x,
            //     y,
            //     zone_x,
            //     zone_y)
            // L2_LOG_ERROR(Log,
            //     L"[%s][%d] invalid pos(%d, %d)  zone[%d][%d] ",
            //     L"Path.cpp",
            //     0xe3,
            //     x,
            //     y,
            //     zone_x,
            //     zone_y)
            L2_LOG_ERROR(
                Log,
                "No path-node section for position (%d, %d): zone (%d, %d) is outside X 10..29, Y 10..26; returning no "
                "nodes.",
                x, y, zone_x, zone_y
            );
        }
    } else {
        i = static_cast<int>(
            m_WorldInfo->SectionOffsetCount[zone_x - 10][zone_y - 10][static_cast<std::uint32_t>(x) >> 8U & 0x7fU]
                                           [static_cast<std::uint32_t>(y) >> 8U & 0x7fU]
        );
        count = i & 0xff;
        offset = static_cast<unsigned int>(i) >> 8U;
        if (offset != 0 && offset <= static_cast<unsigned int>(m_WorldInfo->maxIndex)) {
            return m_Data + offset;
        }

        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] i[%d] Invalid offset[%d] count[%d] maxIndex[%d] "
        //     L"zone[%d,%d] pathnodeSection[%d, %d]",
        //     L"Path.cpp",
        //     0xf4,
        //     i,
        //     offset,
        //     count,
        //     m_WorldInfo->maxIndex,
        //     zone_x,
        //     zone_y,
        //     static_cast<std::uint32_t>(x) >> 8U & 0x7fU,
        //     static_cast<std::uint32_t>(y) >> 8U & 0x7fU)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] i[%d] Invalid offset[%d] count[%d] maxIndex[%d] "
        //     L"zone[%d,%d] pathnodeSection[%d, %d]",
        //     L"Path.cpp",
        //     0xf4,
        //     i,
        //     offset,
        //     count,
        //     m_WorldInfo->maxIndex,
        //     zone_x,
        //     zone_y,
        //     static_cast<std::uint32_t>(x) >> 8U & 0x7fU,
        //     static_cast<std::uint32_t>(y) >> 8U & 0x7fU)
        // Noise: Zero offset and count denote an empty section, whose lookup already returns no nodes.
        if (offset != 0U || count != 0) {
            L2_LOG_ERROR(
                Log,
                "Invalid path-node section offset %u, count %d, maximum index %d (packed value 0x%08x), zone (%d, %d), "
                "section (%u, %u); returning no nodes.",
                static_cast<unsigned>(offset), count, m_WorldInfo->maxIndex, static_cast<unsigned>(i), zone_x, zone_y,
                static_cast<unsigned>((static_cast<std::uint32_t>(x) >> 8U) & 0x7fU),
                static_cast<unsigned>((static_cast<std::uint32_t>(y) >> 8U) & 0x7fU)
            );
        }
    }
    count = 0;
    return nullptr;
}

bool CPathNodeWorld::FindClosestPathNode(const FVector& position, CPathNode** nodes, int& count, const bool reverse) {
    L2_GUARD_FUNCTION();

    std::fill(nodes, nodes + 16, nullptr);
    double distances[16];
    std::fill(distances, distances + 16, static_cast<double>(std::numeric_limits<float>::max()));

    const int centerX = runtime::Win64CvttDoubleToInt32(position.x);
    const int centerY = runtime::Win64CvttDoubleToInt32(position.y);
    bool found = false;
    for (int x = centerX - 0x100; x <= centerX + 0x100; x += 0x100) {
        for (int y = centerY - 0x100; y <= centerY + 0x100; y += 0x100) {
            int nodeCount = 0;
            CPathNode* node = GetPathNodeArray(x, y, nodeCount);
            for (int index = 0; index < nodeCount; ++index, ++node) {
                const double angle =
                    std::atan2(static_cast<double>(node->y - centerY), static_cast<double>(node->x - centerX));
                const int sector = std::clamp(8 - runtime::Win64CvttDoubleToInt32(angle / -3.141592 * 8.0), 0, 15);
                const FVector nodePosition = node->CastFVector();
                const double distanceSquared = position.DistCube(nodePosition);
                if (distanceSquared <= 0.010000000000000002 ||
                    (nodes[sector] != nullptr && distances[sector] * distances[sector] <= distanceSquared)) {
                    continue;
                }

                CGeoData* const geo = CWorld::Inst().GetGeo();
                const bool reachable = reverse ? geo->MoveStraightTest(position, nodePosition, 8192.0, false, 0)
                                               : geo->MoveStraightTest(nodePosition, position, 8192.0, false, 0);
                if (reachable) {
                    nodes[sector] = node;
                    distances[sector] = std::sqrt(distanceSquared);
                    found = true;
                }
            }
        }
    }

    if (!found) {
        return false;
    }

    count = 0;
    for (int index = 0; index < 16; ++index) {
        if (nodes[index] != nullptr) {
            nodes[count++] = nodes[index];
        }
    }
    return true;
}

bool CPathNodeWorld::FindPathNodeTest(const FVector& position) {
    // Current L2Server 0x0094e568 calls the process-wide owner.
    L2_GUARD_FUNCTION();
    CPathNode* nodes[16];
    int count = 0;
    return g_pnWorld.FindClosestPathNode(position, nodes, count, true);
}

void CPathNodeWorld::ViewPathNode(FVector position, CUserSocket* const socket) {
    // Current L2Server 0x0094e61c.
    L2_GUARD_FUNCTION();

    int pointPacketLength = 0;
    int pointCount = 0;
    int linePacketLength = 0;
    int lineCount = 0;
    std::array<char16_t, 10000> primitiveName;
    primitiveName.back() = L'\0';
    std::array<char, 0x5000> pointPacket;
    std::array<char, 0x5000> linePacket;

    position.z = -16000.0;
    const int primitiveRadius = runtime::Win64CvttDoubleToInt32(std::sqrt(2.0) * 1024.0);
    const int centerX = runtime::Win64CvttDoubleToInt32(position.x);
    const int centerY = runtime::Win64CvttDoubleToInt32(position.y);
    const int minimumX = runtime::SubtractInt32Wrapping(centerX, 0x300);
    const int maximumX = runtime::AddInt32Wrapping(centerX, 0x300);

    if (minimumX <= maximumX) {
        const int minimumY = runtime::SubtractInt32Wrapping(centerY, 0x300);
        const int maximumY = runtime::AddInt32Wrapping(centerY, 0x300);

        int x = minimumX;
        do {
            int y = minimumY;
            if (minimumY <= maximumY) {
                do {
                    int pathNodeCount = 0;
                    CPathNode* pathNode = GetPathNodeArray(x, y, pathNodeCount);
                    while (pathNodeCount != 0) {
                        --pathNodeCount;
                        pointPacketLength += ::Assemble(
                            pointPacket.data() + pointPacketLength, 0x5000 - pointPacketLength, "cddd", 1, pathNode->x,
                            pathNode->y, pathNode->z
                        );
                        ++pointCount;
                        if (pointPacketLength > 0x4f9c) {
                            const int nameIndex = PointName;
                            PointName = runtime::AddInt32Wrapping(PointName, 1);
                            FormatUtf16To(primitiveName.data(), primitiveName.size(), u"ServerPrimitive_point%d", nameIndex);
                            const std::u16string targetName = std::u16string(primitiveName.data());
                            socket->Send(
                                "chSddddddb", 0xfe, 0x11, targetName.c_str(), runtime::Win64CvttDoubleToInt32(position.x),
                                runtime::Win64CvttDoubleToInt32(position.y),
                                runtime::Win64CvttDoubleToInt32(position.z), primitiveRadius, 32000, pointCount,
                                pointPacketLength, pointPacket.data()
                            );
                            pointPacketLength = 0;
                            pointCount = 0;
                        }

                        for (int neighborSlot = 0; neighborSlot < 8; ++neighborSlot) {
                            const std::uint32_t neighborIndex = pathNode->Neighbor[neighborSlot];
                            if (neighborIndex == 0) {
                                continue;
                            }

                            const FVector neighborPosition = m_Data[neighborIndex].CastFVector();
                            linePacketLength += ::Assemble(
                                linePacket.data() + linePacketLength, 0x5000 - linePacketLength, "cdddddd", 2, pathNode->x,
                                pathNode->y, pathNode->z, runtime::Win64CvttDoubleToInt32(neighborPosition.x),
                                runtime::Win64CvttDoubleToInt32(neighborPosition.y),
                                runtime::Win64CvttDoubleToInt32(neighborPosition.z)
                            );
                            ++lineCount;
                            if (linePacketLength > 0x4f9c) {
                                const int nameIndex = LineName;
                                LineName = runtime::AddInt32Wrapping(LineName, 1);
                                FormatUtf16To(
                                    primitiveName.data(), primitiveName.size(), u"ServerPrimitive_line%d", nameIndex
                                );
                                const std::u16string targetName = std::u16string(primitiveName.data());
                                socket->Send(
                                    "chSddddddb", 0xfe, 0x11, targetName.c_str(),
                                    runtime::Win64CvttDoubleToInt32(position.x),
                                    runtime::Win64CvttDoubleToInt32(position.y),
                                    runtime::Win64CvttDoubleToInt32(position.z), primitiveRadius, 32000, lineCount,
                                    linePacketLength, linePacket.data()
                                );
                                linePacketLength = 0;
                                lineCount = 0;
                            }
                        }
                        ++pathNode;
                    }
                    y = runtime::AddInt32Wrapping(y, 0x100);
                } while (y <= maximumY);
            }
            x = runtime::AddInt32Wrapping(x, 0x100);
        } while (x <= maximumX);

        if (pointCount != 0) {
            const int nameIndex = PointName;
            PointName = runtime::AddInt32Wrapping(PointName, 1);
            FormatUtf16To(primitiveName.data(), primitiveName.size(), u"ServerPrimitive_point%d", nameIndex);
            const std::u16string targetName = std::u16string(primitiveName.data());
            socket->Send(
                "chSddddddb", 0xfe, 0x11, targetName.c_str(), runtime::Win64CvttDoubleToInt32(position.x),
                runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z),
                primitiveRadius, 32000, pointCount, pointPacketLength, pointPacket.data()
            );
        }
        if (lineCount != 0) {
            const int nameIndex = LineName;
            LineName = runtime::AddInt32Wrapping(LineName, 1);
            FormatUtf16To(primitiveName.data(), primitiveName.size(), u"ServerPrimitive_line%d", nameIndex);
            const std::u16string targetName = std::u16string(primitiveName.data());
            socket->Send(
                "chSddddddb", 0xfe, 0x11, targetName.c_str(), runtime::Win64CvttDoubleToInt32(position.x),
                runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z),
                primitiveRadius, 32000, lineCount, linePacketLength, linePacket.data()
            );
        }
    }
}

PathFindResult CPathNodeWorld::PathFind(
    const FVector source, const FVector destination, FVector& output, const int maximumVisitCount, CCreature* const creature
) {
    (void)creature;

    CGeoData* const geo = CWorld::Inst().GetGeo();
    FVector straightDestination{};
    bool distanceExhausted = false;
    double travelledDistance = 0.0;
    if (geo->MoveStraight(source, destination, 8192.0, straightDestination, distanceExhausted, travelledDistance)) {
        output = straightDestination;
        return PFR_CAN_GO;
    }

    CPathNode* startNodes[16];
    CPathNode* destinationNodes[16];
    int startCount = 0;
    int destinationCount = 0;
    if (!FindClosestPathNode(source, startNodes, startCount, true) ||
        !FindClosestPathNode(destination, destinationNodes, destinationCount, false)) {
        return PFR_ERROR;
    }

    const int threadId = CThreadManager::GetThreadID();
    CNodeVisitHash& visitHash = g_NodeVisitHash[threadId];
    visitHash.Reset();

    using PathQueue = std::priority_queue<CPathFindingNode, std::vector<CPathFindingNode, std::allocator<CPathFindingNode>>>;
    PathQueue queue;
    for (int index = 0; index < startCount; ++index) {
        CPathNode* const node = startNodes[index];
        const FVector nodePosition = node->CastFVector();
        const double distance = Distance(source, nodePosition);
        queue.emplace(node, node, distance, Distance(destination, nodePosition), 1);
        visitHash.Visit(IndexFromPathNode(node), distance, true);
    }

    std::uint32_t visitCount = 0;
    while (!queue.empty()) {
        if (visitCount++ > static_cast<std::uint32_t>(maximumVisitCount)) {
            break;
        }

        const CPathFindingNode current = queue.top();
        queue.pop();
        for (int index = 0; index < destinationCount; ++index) {
            if (destinationNodes[index] == current.PathNode) {
                output = current.StartingPathNode->CastFVector();
                return PFR_PATH_FOUND;
            }
        }

        for (int neighborIndex = 0; neighborIndex < 8; ++neighborIndex) {
            const std::uint32_t index = current.PathNode->Neighbor[neighborIndex];
            if (index == 0) {
                break;
            }
            CPathNode* const node = GetPathNode(index);
            if (node == nullptr) {
                // Log.Add(
                //     LOG_ERROR,
                //     L"[%s][%d] invalid index[%d]. nMaxIndex[%d]",
                //     L"c:\\work\\lineage2build\\server\\main\\l2server\\Path.h",
                //     0x7f,
                //     index,
                //     GetMaxIndex())
                // L2_LOG_ERROR(Log,
                //     L"[%s][%d] invalid index[%d]. nMaxIndex[%d]",
                //     L"c:\\work\\lineage2build\\server\\main\\l2server\\Path.h",
                //     0x7f,
                //     index,
                //     GetMaxIndex())
                L2_LOG_ERROR(
                    Log, "Path search cannot resolve neighbor index %u (maximum %u); this neighbor is skipped.",
                    static_cast<unsigned>(index), static_cast<unsigned>(GetMaxIndex())
                );
                continue;
            }

            const FVector nodePosition = node->CastFVector();
            double distance;
            int depth;
            CPathNode* startingNode;
            if (current.Depth == 1) {
                distance = Distance(source, nodePosition);
                if (!visitHash.Visit(index, distance, false)) {
                    continue;
                }

                if (distance > 0.1) {
                    FVector moved{};
                    bool exhausted = false;
                    double travelled = 0.0;
                    if (geo->MoveStraight(source, nodePosition, 8192.0, moved, exhausted, travelled)) {
                        visitHash.Visit(index, distance, true);
                        depth = 1;
                        startingNode = node;
                        queue.emplace(node, startingNode, distance, Distance(destination, nodePosition), depth);
                        continue;
                    }
                }
            }

            distance = current.Dist + current.PathNode->Dist(*node);
            if (!visitHash.Visit(index, distance, true)) {
                continue;
            }
            depth = current.Depth + 1;
            startingNode = current.StartingPathNode;
            queue.emplace(node, startingNode, distance, Distance(destination, nodePosition), depth);
        }
    }
    return PFR_ERROR;
}
