#include "SuperPoint.h"
#include "Shared/src/Runtime/ResourceDirectories.h"
#include "Shared/src/Runtime/Utf8.h"

#include "L2NPC/src/NPCServer.h"
#include "L2NPC/src/ParserSuperPointInfo.h"
#include "L2NPC/src/Portable/TargetByteWideInput.h"
#include "L2NPC/src/User.h"
#include "Shared/src/Runtime/LittleEndian.h"
#include "Shared/src/Runtime/Win64Floating.h"
#include "Runtime.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/Utils.h"
#include "TextRuntime.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace l2geo {

CSuperPointManager g_SuperPointManager;

namespace {

int PathKey(int startIndex, int endIndex) {
    return static_cast<int>((static_cast<std::uint32_t>(startIndex) << 16) | static_cast<std::uint32_t>(endIndex));
}

class SuperPointBinaryCursor {
public:
    SuperPointBinaryCursor(const void* data, std::size_t bytes) noexcept :
        cursor_(static_cast<const std::uint8_t*>(data)), remaining_(bytes), total_(bytes) {}

    bool ReadInt(int& value) noexcept {
        if (remaining_ < sizeof(std::int32_t)) {
            return false;
        }
        value = runtime::LoadLittleEndianInt32(cursor_);
        cursor_ += sizeof(std::int32_t);
        remaining_ -= sizeof(std::int32_t);
        return true;
    }

    bool ReadUtf16(int codeUnitCount, std::u16string& value) {
        constexpr int maximumTargetNameLength = 9999;
        if (codeUnitCount < 0 || codeUnitCount > maximumTargetNameLength) {
            return false;
        }
        const std::size_t count = static_cast<std::size_t>(codeUnitCount);
        if (count > remaining_ / sizeof(char16_t)) {
            return false;
        }

        std::u16string decoded;
        decoded.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            decoded.push_back(static_cast<char16_t>(runtime::LoadLittleEndian16(cursor_)));
            cursor_ += sizeof(char16_t);
            remaining_ -= sizeof(char16_t);
        }
        value.assign(decoded);
        return true;
    }

    std::size_t Remaining() const noexcept { return remaining_; }

    std::size_t Offset() const noexcept { return total_ - remaining_; }

    bool AtEnd() const noexcept { return remaining_ == 0; }

private:
    const std::uint8_t* cursor_ = nullptr;
    std::size_t remaining_ = 0;
    std::size_t total_ = 0;
};

struct ParsedSuperPointGraph {
    std::unique_ptr<CSuperPoint> graph;
    std::vector<std::unique_ptr<CSuperPoint::SuperPointNodes>> paths;
    std::vector<std::unique_ptr<CSuperPoint::SuperPointNode>> nodes;

    void ReleaseToTargetOwnership() noexcept {
        for (auto& node : nodes) {
            node.release();
        }
        for (auto& path : paths) {
            path.release();
        }
        graph.release();
    }
};

std::string TransitionLabelUtf8(const std::u16string& name, int startIndex, int endIndex) {
    return Utf16ToUtf8(runtime::StringView(name)) + "(" + std::to_string(startIndex) + "->" + std::to_string(endIndex) + ")";
}

std::wstring TransitionLabel(const std::u16string& name, int startIndex, int endIndex) {
    return Utf16ToWide(name) + L"(" + std::to_wstring(startIndex) + L"->" + std::to_wstring(endIndex) + L")";
}

std::wstring IndexedTransitionLabel(const std::u16string& name, int startIndex, int endIndex, int pathIndex) {
    return TransitionLabel(name, startIndex, endIndex) + L"[" + std::to_wstring(pathIndex) + L"]";
}

std::wstring IndexedNodeLabel(const std::u16string& name, int nodeIndex) {
    return Utf16ToWide(name) + L"[" + std::to_wstring(nodeIndex) + L"]";
}

// File-static helpers recovered from SuperPoint.cpp.
int AssembleServerPrimitiveLine(
    IPacketHelper& helper,
    const wchar_t* name,
    int red,
    int green,
    int blue,
    int alpha,
    const FVector& first,
    const FVector& second
) {
    return helper.Assemble(
        "cSdddddddddd", 2, name, red, green, blue, alpha, runtime::Win64CvttDoubleToInt32(first.x),
        runtime::Win64CvttDoubleToInt32(first.y), runtime::Win64CvttDoubleToInt32(first.z),
        runtime::Win64CvttDoubleToInt32(second.x), runtime::Win64CvttDoubleToInt32(second.y),
        runtime::Win64CvttDoubleToInt32(second.z)
    );
}

int AssembleServerPrimitivePoint(
    IPacketHelper& helper, const wchar_t* name, int red, int green, int blue, int alpha, const FVector& position
) {
    return helper.Assemble(
        "cSddddddd", 1, name, red, green, blue, alpha, runtime::Win64CvttDoubleToInt32(position.x),
        runtime::Win64CvttDoubleToInt32(position.y), runtime::Win64CvttDoubleToInt32(position.z)
    );
}

} // namespace

CSuperPoint::CSuperPoint() = default;

CSuperPoint* CSuperPointManager::Find(const char16_t* name) {
    L2_GUARD_FUNCTION();
    if (!name) {
        return nullptr;
    }
    const auto found = m_SuperPointNameMap.find(name);
    if (found == m_SuperPointNameMap.end()) {
        // Noise: Movement and builder callers report the missing graph with NPC/user context.
        // const std::wstring nameWide = Utf16ToNativeWide(std::u16string_view(name))
        // Log.Add(LOG_ERROR, L"%s : cannot find SuperPoint[%s]", L"CSuperPointManager::Find", nameWide.c_str())
        return nullptr;
    }
    return found->second;
}

bool CSuperPointManager::Add(const char16_t* name, CSuperPoint* superPoint) {
    L2_GUARD_FUNCTION();
    return m_SuperPointNameMap.emplace(std::u16string(name), superPoint).second;
}

bool CSuperPointManager::Remove(const char16_t* name) {
    L2_GUARD_FUNCTION();
    const auto found = m_SuperPointNameMap.find(name);
    if (found == m_SuperPointNameMap.end()) {
        return false;
    }
    delete found->second;
    m_SuperPointNameMap.erase(found);
    return true;
}

void CSuperPointManager::RemoveAll() {
    L2_GUARD_FUNCTION();
    for (const auto& entry : m_SuperPointNameMap) {
        delete entry.second;
    }
    m_SuperPointNameMap.clear();
}

int CSuperPointManager::Count() const { return static_cast<int>(m_SuperPointNameMap.size()); }

void CSuperPointManager::Load() {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    constexpr const wchar_t* kHostPath = L"..\\geodata\\SuperPoint.bin";
#else
    // Linux file names are case sensitive. The directory is GeoData.
    constexpr const wchar_t* kHostPath = L"../GeoData/SuperPoint.bin";
#endif

    int byteCount = 0;
    char* const image = LoadBinaryAbsolute(kHostPath, byteCount, false);
    if (!image) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] loading "
        //     L"..\\geodata\\SuperPoint.bin failed!",
        //     L"SuperPoint.cpp", 0x1e
        // )
        // Log.Add(LOG_ERROR,
        //     "[NPC route] Cannot load SuperPoint image '%s'. Loading stopped.",
        //     runtime::ResolveResourcePath(kHostPath).u8string().c_str())
        L2_LOG_ERROR(
            Log, "[NPC route] Cannot load SuperPoint image '%s'; loading stopped.",
            runtime::Utf8Bytes(runtime::ResolveResourcePath(kHostPath).u8string()).c_str()
        );
        return;
    }

    // SHIPPING BUG SP-PARSE-001: current L2NPC.exe 0x0053d204 walks the
    // file with an unchecked pointer, copies a file-controlled UTF-16 name
    // into a fixed stack buffer with room for 9999 code units plus its
    // terminator and continues after allocator failure with null
    // graph/path/node pointers. Keep the complete recovered parse loop
    // disabled as the audit record of that behavior.
    /*
    void* cursor = image,
    int superPointCount = 0,
    cursor = ReadIntMem(
        cursor,
        superPointCount),
    for (int superPointIndex = 0,
         superPointIndex < superPointCount,
         ++superPointIndex) {
        auto* const superPoint =
            new (std::nothrow) CSuperPoint,

        int nameLength = 0,
        cursor = ReadIntMem(
            cursor,
            nameLength),
        cursor = ReadUtf16Mem(
            cursor,
            nameLength,
            superPoint->m_Name),

        int moveType = 0,
        cursor = ReadIntMem(
            cursor,
            moveType),
        superPoint->m_MoveType =
            static_cast<CSuperPoint::SPMoveType>(
                moveType),

        int nodeCount = 0,
        cursor = ReadIntMem(
            cursor,
            nodeCount),
        for (int nodeIndex = 0,
             nodeIndex < nodeCount,
             ++nodeIndex) {
            int storedIndex = 0,
            int x = 0,
            int y = 0,
            int z = 0,
            int delay = 0,
            cursor = ReadIntMem(cursor, storedIndex),
            cursor = ReadIntMem(cursor, x),
            cursor = ReadIntMem(cursor, y),
            cursor = ReadIntMem(cursor, z),
            cursor = ReadIntMem(cursor, delay),
            superPoint->PushPath(
                delay,
                FVector{
                    static_cast<double>(x),
                    static_cast<double>(y),
                    static_cast<double>(z)})
        }

        for (int startIndex = 0,
             startIndex < superPoint->m_SPNodeCount,
             ++startIndex) {
            for (int endIndex = 0,
                 endIndex < superPoint->m_SPNodeCount,
                 ++endIndex) {
                if (startIndex == endIndex) continue,

                auto* const path =
                    new (std::nothrow)
                        CSuperPoint::SuperPointNodes,
                int pathNodeCount = 0,
                cursor = ReadIntMem(
                    cursor,
                    pathNodeCount),
                while (pathNodeCount != 0) {
                    --pathNodeCount,
                    auto* const node =
                        new (std::nothrow)
                            CSuperPoint::SuperPointNode,
                    int x = 0,
                    int y = 0,
                    int z = 0,
                    cursor = ReadIntMem(
                        cursor,
                        node->Index),
                    cursor = ReadIntMem(cursor, x),
                    cursor = ReadIntMem(cursor, y),
                    cursor = ReadIntMem(cursor, z),
                    node->Pos = FVector{
                        static_cast<double>(x),
                        static_cast<double>(y),
                        static_cast<double>(z)},
                    path->push_back(node)
                }
                superPoint->m_Paths[
                    PathKey(
                        startIndex,
                        endIndex)] = path
            }
        }

        // Current 0x0053d204 uses map::operator[] here: a duplicate name
        // replaces the published pointer and intentionally leaves the older
        // allocation unowned.
        m_SuperPointNameMap[
            superPoint->m_Name] = superPoint
    }

    std::free(image),
    */

    std::unique_ptr<char, decltype(&std::free)> imageOwner(image, &std::free);
    if (byteCount <= 0) {
        // Log.Add(LOG_ERROR, L"[%s][%d] invalid SuperPoint.bin length[%d]", L"SuperPoint.cpp", 0x1e, byteCount)
        // Log.Add(LOG_ERROR,
        //     "[NPC route] SuperPoint image '%s' has invalid length %d bytes. Loading stopped.",
        //     runtime::ResolveResourcePath(kHostPath).u8string().c_str(), byteCount)
        L2_LOG_ERROR(
            Log, "[NPC route] SuperPoint image '%s' has invalid length %d bytes; loading stopped.",
            runtime::Utf8Bytes(runtime::ResolveResourcePath(kHostPath).u8string()).c_str(), byteCount
        );
        return;
    }
    SuperPointBinaryCursor cursor(image, static_cast<std::size_t>(byteCount));
    const auto reportInvalidImage = [&](const wchar_t* reason) {
        // Log.Add(
        //     LOG_ERROR, L"[%s][%d] invalid SuperPoint.bin at byte[%llu]: %s", L"SuperPoint.cpp", 0x1e,
        //     static_cast<unsigned long long>(cursor.Offset()), reason
        // )
        // Log.Add(LOG_ERROR,
        //     "[NPC route] Loading SuperPoint image '%s' stopped at byte %llu: %s.",
        //     runtime::ResolveResourcePath(kHostPath).u8string().c_str(), static_cast<unsigned long
        //     long>(cursor.Offset()), WideToUtf8(reason).c_str())
        L2_LOG_ERROR(
            Log, "[NPC route] Loading SuperPoint image '%s' stopped at byte %llu: %s.",
            runtime::Utf8Bytes(runtime::ResolveResourcePath(kHostPath).u8string()).c_str(),
            static_cast<unsigned long long>(cursor.Offset()), WideToUtf8(reason).c_str()
        );
    };

    std::vector<ParsedSuperPointGraph> parsedGraphs;
    try {
        int superPointCount = 0;
        if (!cursor.ReadInt(superPointCount) || superPointCount < 0 ||
            static_cast<std::size_t>(superPointCount) > cursor.Remaining() / 12U) {
            reportInvalidImage(L"invalid graph count");
            return;
        }

        for (int superPointIndex = 0; superPointIndex < superPointCount; ++superPointIndex) {
            ParsedSuperPointGraph parsed;
            parsed.graph.reset(new (std::nothrow) CSuperPoint);
            if (!parsed.graph) {
                reportInvalidImage(L"graph allocation failed");
                return;
            }

            int nameLength = 0;
            if (!cursor.ReadInt(nameLength) || !cursor.ReadUtf16(nameLength, parsed.graph->m_Name)) {
                reportInvalidImage(L"invalid graph name");
                return;
            }

            int moveType = 0;
            if (!cursor.ReadInt(moveType)) {
                reportInvalidImage(L"truncated move type");
                return;
            }
            parsed.graph->m_MoveType = static_cast<CSuperPoint::SPMoveType>(moveType);

            int nodeCount = 0;
            if (!cursor.ReadInt(nodeCount) || nodeCount < 0 ||
                static_cast<std::size_t>(nodeCount) > cursor.Remaining() / 20U) {
                reportInvalidImage(L"invalid graph node count");
                return;
            }

            for (int nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
                int storedIndex = 0;
                int x = 0;
                int y = 0;
                int z = 0;
                int delay = 0;
                if (!cursor.ReadInt(storedIndex) || !cursor.ReadInt(x) || !cursor.ReadInt(y) || !cursor.ReadInt(z) ||
                    !cursor.ReadInt(delay)) {
                    reportInvalidImage(L"truncated graph node");
                    return;
                }
                parsed.graph->PushPath(
                    delay, FVector{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)}
                );
            }

            const std::uint64_t routeCount =
                nodeCount > 0 ? static_cast<std::uint64_t>(nodeCount) * static_cast<std::uint64_t>(nodeCount - 1) : 0;
            if (routeCount > cursor.Remaining() / 4U) {
                reportInvalidImage(L"truncated route table");
                return;
            }

            for (int startIndex = 0; startIndex < nodeCount; ++startIndex) {
                for (int endIndex = 0; endIndex < nodeCount; ++endIndex) {
                    if (startIndex == endIndex) {
                        continue;
                    }

                    auto path =
                        std::unique_ptr<CSuperPoint::SuperPointNodes>(new (std::nothrow) CSuperPoint::SuperPointNodes);
                    if (!path) {
                        reportInvalidImage(L"route allocation failed");
                        return;
                    }

                    int pathNodeCount = 0;
                    if (!cursor.ReadInt(pathNodeCount) || pathNodeCount < 0 ||
                        static_cast<std::size_t>(pathNodeCount) > cursor.Remaining() / 16U) {
                        reportInvalidImage(L"invalid route node count");
                        return;
                    }

                    for (int pathNodeIndex = 0; pathNodeIndex < pathNodeCount; ++pathNodeIndex) {
                        auto node =
                            std::unique_ptr<CSuperPoint::SuperPointNode>(new (std::nothrow) CSuperPoint::SuperPointNode);
                        if (!node) {
                            reportInvalidImage(L"route-node allocation failed");
                            return;
                        }

                        int x = 0;
                        int y = 0;
                        int z = 0;
                        if (!cursor.ReadInt(node->Index) || !cursor.ReadInt(x) || !cursor.ReadInt(y) || !cursor.ReadInt(z)) {
                            reportInvalidImage(L"truncated route node");
                            return;
                        }
                        node->Pos = FVector{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
                        path->push_back(node.get());
                        parsed.nodes.push_back(std::move(node));
                    }

                    parsed.graph->m_Paths[PathKey(startIndex, endIndex)] = path.get();
                    parsed.paths.push_back(std::move(path));
                }
            }
            parsedGraphs.push_back(std::move(parsed));
        }

        if (!cursor.AtEnd()) {
            reportInvalidImage(L"unexpected trailing data");
            return;
        }

        for (ParsedSuperPointGraph& parsed : parsedGraphs) {
            CSuperPoint* const graph = parsed.graph.get();
            // Preserve target map::operator[] duplicate-name replacement.
            m_SuperPointNameMap[graph->m_Name] = graph;
            parsed.ReleaseToTargetOwnership();
        }
    } catch (const std::bad_alloc&) {
        reportInvalidImage(L"container allocation failed");
        return;
    }

    imageOwner.reset();

    for (const auto& entry : m_SuperPointNameMap) {
        CSuperPoint* const superPoint = entry.second;
        if (!superPoint || superPoint->m_Name.empty()) {
            continue;
        }

        const std::string name = Utf16ToUtf8(runtime::StringView(superPoint->m_Name));
        const int nodeCount = superPoint->m_SPNodeCount;
        if (nodeCount < 2) {
            // Log.Add(LOG_ERROR, L"super point[%s] node count [%d] < 2", name.c_str(), nodeCount)
            // Log.Add(LOG_WARN,
            //     "[NPC route] SuperPoint graph '%s' has %d nodes. Movement requires at least 2. Graph retained in the
            //     registry.", WideToUtf8(name).c_str(), nodeCount)
            L2_LOG_WARN(
                Log,
                "[NPC route] SuperPoint graph '%s' has %d nodes; movement requires at least 2. Graph retained in the "
                "registry.",
                name.c_str(), nodeCount
            );
            continue;
        }

        for (int nodeIndex = 0; nodeIndex < nodeCount - 1; ++nodeIndex) {
            const CSuperPoint::SuperPointNodes* const path = superPoint->GetPath(nodeIndex, nodeIndex + 1);
            if (!path || path->empty()) {
                // Log.Add(LOG_ERROR, L"super point[%s] node invalid [%d]->[%d]", name.c_str(), nodeIndex, nodeIndex + 1)
                // Log.Add(LOG_WARN,
                //     "[NPC route] SuperPoint graph '%s' has a %s forward path %d -> %d. Ending this graph's validation,
                //     retaining it in the registry.", WideToUtf8(name).c_str(), path ? "zero-point" : "missing", nodeIndex,
                //     nodeIndex + 1)
                L2_LOG_WARN(
                    Log,
                    "[NPC route] SuperPoint graph '%s' has a %s forward path %d -> %d; ending this graph's validation, "
                    "retaining it in the registry.",
                    name.c_str(), path ? "zero-point" : "missing", nodeIndex, nodeIndex + 1
                );
                break;
            }
        }
    }
}

CSuperPointInfoOfNPC::CSuperPointInfoOfNPC(int npcClassId) : npc_class_id(npcClassId) {}

void CSuperPointInfoOfNPC::Enter(const wchar_t* file, int line) { m_lock.Enter(file, line); }

void CSuperPointInfoOfNPC::Leave(const wchar_t* file, int line) { m_lock.Leave(file, line); }

SuperPointNodeInfo* CSuperPointInfoOfNPC::GetNodeInfo(int nodeIndex, bool readOnly) {
    L2_GUARD_FUNCTION();
    SuperPointNodeInfo* result = nullptr;
    {
        L2_GUARD(*this);

        const auto found = NodeList.find(nodeIndex);
        if (found != NodeList.end()) {
            result = found->second;
        } else if (!readOnly) {
            result = new (std::nothrow) SuperPointNodeInfo{};
            // The shipping xmap receives the key even when its non-throwing
            // allocation returned null.
            NodeList.emplace(nodeIndex, result);
        }
    }
    return result;
}

int CSuperPointInfoOfNPC::GetMoveType() { return nMoveType; }

void CSuperPointInfoOfNPC::SetNPCClassID(int npcClassId) { npc_class_id = npcClassId; }

void CSuperPointInfoOfNPC::SetMoveType(int moveType) { nMoveType = moveType; }

void CSuperPointInfoOfNPC::SetDelay(int nodeIndex, int delay) {
    L2_GUARD_FUNCTION();
    SuperPointNodeInfo* const info = GetNodeInfo(nodeIndex, false);
    if (info) {
        info->Delay = delay;
    }
}

void CSuperPointInfoOfNPC::SetFStringIndex(int nodeIndex, int fstringIndex) {
    L2_GUARD_FUNCTION();
    SuperPointNodeInfo* const info = GetNodeInfo(nodeIndex, false);
    if (info) {
        info->FStringIndex = fstringIndex;
    }
}

void CSuperPointInfoOfNPC::SetSocialNumber(int nodeIndex, int socialNumber) {
    L2_GUARD_FUNCTION();
    SuperPointNodeInfo* const info = GetNodeInfo(nodeIndex, false);
    if (info) {
        info->SocialNumber = socialNumber;
    }
}

int CSuperPointInfoOfNPC::GetDelay(int nodeIndex) {
    L2_GUARD_FUNCTION();
    SuperPointNodeInfo* const info = GetNodeInfo(nodeIndex, true);
    return info ? info->Delay : -1;
}

int CSuperPointInfoOfNPC::GetSocialNumber(int nodeIndex) {
    L2_GUARD_FUNCTION();
    SuperPointNodeInfo* const info = GetNodeInfo(nodeIndex, true);
    return info ? info->SocialNumber : -1;
}

int CSuperPointInfoOfNPC::GetFStringIndex(int nodeIndex) {
    L2_GUARD_FUNCTION();
    SuperPointNodeInfo* const info = GetNodeInfo(nodeIndex, true);
    return info ? info->FStringIndex : -1;
}

CSuperPointInfo::CSuperPointInfo() = default;

void CSuperPointInfo::SetSuperpointName(const char16_t* name) { SuperpointName = std::u16string(name); }

std::u16string CSuperPointInfo::GetSuperpointName() const { return SuperpointName; }

void CSuperPointInfo::Enter(const wchar_t* file, int line) { m_lock.Enter(file, line); }

void CSuperPointInfo::Leave(const wchar_t* file, int line) { m_lock.Leave(file, line); }

CSuperPointInfoOfNPC* CSuperPointInfo::GetNPCInfo(int npcClassId, bool readOnly) {
    L2_GUARD_FUNCTION();
    CSuperPointInfoOfNPC* result = nullptr;
    {
        L2_GUARD(*this);

        const auto found = NPCList.find(npcClassId);
        if (found != NPCList.end()) {
            result = found->second;
        } else if (!readOnly) {
            result = new (std::nothrow) CSuperPointInfoOfNPC(npcClassId);
            // This includes the target's null insertion on allocation failure.
            NPCList.emplace(npcClassId, result);
        }
    }
    return result;
}

void CSuperPointInfo::SetDelay(int npcClassId, int nodeIndex, int delay) {
    L2_GUARD_FUNCTION();
    CSuperPointInfoOfNPC* const info = GetNPCInfo(npcClassId, false);
    if (info) {
        info->SetDelay(nodeIndex, delay);
    }
}

void CSuperPointInfo::SetFStringIndex(int npcClassId, int nodeIndex, int fstringIndex) {
    L2_GUARD_FUNCTION();
    CSuperPointInfoOfNPC* const info = GetNPCInfo(npcClassId, false);
    if (info) {
        info->SetFStringIndex(nodeIndex, fstringIndex);
    }
}

void CSuperPointInfo::SetMoveType(int npcClassId, int moveType) {
    L2_GUARD_FUNCTION();
    CSuperPointInfoOfNPC* const info = GetNPCInfo(npcClassId, false);
    if (info) {
        info->SetMoveType(moveType);
    }
}

void CSuperPointInfo::SetSocialNumber(int npcClassId, int nodeIndex, int socialNumber) {
    L2_GUARD_FUNCTION();
    CSuperPointInfoOfNPC* const info = GetNPCInfo(npcClassId, false);
    if (info) {
        info->SetSocialNumber(nodeIndex, socialNumber);
    }
}

int CSuperPointInfo::GetDelay(int npcClassId, int nodeIndex) {
    L2_GUARD_FUNCTION();
    CSuperPointInfoOfNPC* const info = GetNPCInfo(npcClassId, true);
    return info ? info->GetDelay(nodeIndex) : -1;
}

int CSuperPointInfo::GetSocialNumber(int npcClassId, int nodeIndex) {
    L2_GUARD_FUNCTION();
    CSuperPointInfoOfNPC* const info = GetNPCInfo(npcClassId, true);
    return info ? info->GetSocialNumber(nodeIndex) : -1;
}

int CSuperPointInfo::GetFStringIndex(int npcClassId, int nodeIndex) {
    L2_GUARD_FUNCTION();
    CSuperPointInfoOfNPC* const info = GetNPCInfo(npcClassId, true);
    return info ? info->GetFStringIndex(nodeIndex) : -1;
}

int CSuperPointInfo::GetMoveType(int npcClassId) {
    L2_GUARD_FUNCTION();
    CSuperPointInfoOfNPC* const info = GetNPCInfo(npcClassId, true);
    return info ? info->GetMoveType() : -1;
}

CSuperPointInfoManager& CSuperPointInfoManager::Inst() {
    static CSuperPointInfoManager instance;
    return instance;
}

void CSuperPointInfoManager::Enter(const wchar_t* file, int line) { m_lock.Enter(file, line); }

void CSuperPointInfoManager::Leave(const wchar_t* file, int line) { m_lock.Leave(file, line); }

bool CSuperPointInfoManager::AddSuperPointInfo(CSuperPointInfo* info) {
    L2_GUARD_FUNCTION();
    // The target obtains the name before testing info for null.
    const std::u16string name = info->GetSuperpointName();
    if (GetSuperPointInfo(name) || !info) {
        return false;
    }

    {
        L2_GUARD(*this);
        InfoList.emplace(info->GetSuperpointName(), info);
    }
    return true;
}

CSuperPointInfo* CSuperPointInfoManager::GetSuperPointInfo(std::u16string superPointName) {
    L2_GUARD_FUNCTION();
    CSuperPointInfo* result = nullptr;
    {
        L2_GUARD(*this);
        const auto found = InfoList.find(superPointName);
        if (found != InfoList.end()) {
            result = found->second;
        }
    }
    return result;
}

int CSuperPointInfoManager::GetDelay(std::u16string superPointName, int npcClassId, int nodeIndex) {
    L2_GUARD_FUNCTION();
    CSuperPointInfo* const info = GetSuperPointInfo(superPointName);
    return info ? info->GetDelay(npcClassId, nodeIndex) : -1;
}

int CSuperPointInfoManager::GetFStringIndex(std::u16string superPointName, int npcClassId, int nodeIndex) {
    L2_GUARD_FUNCTION();
    CSuperPointInfo* const info = GetSuperPointInfo(superPointName);
    return info ? info->GetFStringIndex(npcClassId, nodeIndex) : -1;
}

int CSuperPointInfoManager::GetSocialNumber(std::u16string superPointName, int npcClassId, int nodeIndex) {
    L2_GUARD_FUNCTION();
    CSuperPointInfo* const info = GetSuperPointInfo(superPointName);
    return info ? info->GetSocialNumber(npcClassId, nodeIndex) : -1;
}

int CSuperPointInfoManager::GetMoveType(std::u16string superPointName, int npcClassId) {
    L2_GUARD_FUNCTION();
    CSuperPointInfo* const info = GetSuperPointInfo(superPointName);
    return info ? info->GetMoveType(npcClassId) : -1;
}

void CSuperPointInfoManager::Load() {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    constexpr const char* nativePath = "..\\Script\\superpointinfo.txt";
#else
    constexpr const char* nativePath = "../Script/superpointinfo.txt";
#endif
    // constexpr const wchar_t* targetPath = L"..\\Script\\superpointinfo.txt"
    [[maybe_unused]] constexpr const wchar_t* targetPath = L"..\\Script\\superpointinfo.txt";

    // std::wifstream file(runtime::ResolveResourcePath(nativePath), std::ios::binary)
    const auto resolvedPath = runtime::ResolveResourcePath(nativePath);
#if defined(_WIN32)
    std::wifstream file(resolvedPath, std::ios::binary);
    const bool fileOpened = static_cast<bool>(file);
#else
    // The lexer reads raw bytes, as from the target MSVC wide stream.
    std::ifstream byteFile(resolvedPath, std::ios::binary);
    l2npc::portable::TargetByteWideInputBuffer fileBuffer(byteFile);
    std::wistream file(&fileBuffer);
    const bool fileOpened = static_cast<bool>(byteFile);
#endif
    if (!fileOpened) {
        // Log.Add(LOG_ERROR, L"[%S][%d] Read from %s file failed", L"SuperPoint.cpp", 0x323, targetPath)
        // Log.Add(LOG_ERROR,
        //     "[NPC route] Cannot open SuperPoint settings at '%s'. Loading skipped.",
        //     resolvedPath.u8string().c_str())
        L2_LOG_ERROR(
            Log, "[NPC route] Cannot open SuperPoint settings at '%s'; loading skipped.",
            runtime::Utf8Bytes(resolvedPath.u8string()).c_str()
        );
        return;
    }

    CParserForSuperPointInfo settingParser;
    if (settingParser.create() == 0) {
        // Log.Add(LOG_ERROR, L"[%s][%d] settingParser.create failed", L"SuperPoint.cpp", 0x32a)
        // Log.Add(LOG_ERROR,
        //     "[NPC route] Cannot initialize the SuperPoint settings parser for '%s'. Loading skipped.",
        //     resolvedPath.u8string().c_str())
        L2_LOG_ERROR(
            Log, "[NPC route] Cannot initialize the SuperPoint settings parser for '%s'; loading skipped.",
            runtime::Utf8Bytes(resolvedPath.u8string()).c_str()
        );
        return;
    }

    settingParser.m_lexer.yyin = &file;
    // Log.Add(LOG_NORMAL, L"Loading %s Started", targetPath)
    // Log.Add(LOG_NORMAL,
    //     "[NPC route] Parsing SuperPoint settings from '%s'...",
    //     resolvedPath.u8string().c_str())
    L2_LOG_INFO(
        Log, "[NPC route] Parsing SuperPoint settings from '%s'...", runtime::Utf8Bytes(resolvedPath.u8string()).c_str()
    );
    if (settingParser.yyparse() == 0) {
        // Log.Add(LOG_NORMAL, L"Loading superpointinfo Succeeded")
        // Log.Add(LOG_NORMAL,
        //     "[NPC route] Finished parsing SuperPoint settings from '%s'.",
        //     resolvedPath.u8string().c_str())
        // Original: L2_LOG_INFO(Log,
        //     "[NPC route] Finished parsing SuperPoint settings from '%s'.",
        //     resolvedPath.u8string().c_str())
        if (settingParser.yyerrorcount != 0) {
            L2_LOG_WARN(
                Log,
                "[NPC route] Parser accepted SuperPoint settings from '%s' after reporting %d errors. Check the preceding "
                "parser diagnostics.",
                runtime::Utf8Bytes(resolvedPath.u8string()).c_str(), settingParser.yyerrorcount
            );
        } else {
            L2_LOG_INFO(
                Log, "[NPC route] Finished parsing SuperPoint settings from '%s'.",
                runtime::Utf8Bytes(resolvedPath.u8string()).c_str()
            );
        }
    } else {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing %s file failed "
        //     L"after line[%d]",
        //     L"SuperPoint.cpp", 0x335, targetPath, settingParser.m_lexer.line
        // )
        // Log.Add(LOG_ERROR,
        //     "[NPC route] Cannot parse SuperPoint settings at '%s' near line %d.",
        //     resolvedPath.u8string().c_str(), settingParser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     "[NPC route] Cannot parse SuperPoint settings at '%s' near line %d.",
        //     resolvedPath.u8string().c_str(), settingParser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "[NPC route] Cannot parse SuperPoint settings at '%s' near line %lld.",
            runtime::Utf8Bytes(resolvedPath.u8string()).c_str(), static_cast<long long>(settingParser.m_lexer.line) + 1
        );
    }
}

CSuperPoint::SuperPointNodes* CSuperPoint::GetPath(int startIndex, int endIndex) const {
    L2_GUARD_FUNCTION();
    if (startIndex < 0 || startIndex >= m_SPNodeCount) {
        const std::string name = Utf16ToUtf8(runtime::StringView(m_Name));
        // Log.Add(
        //     LOG_ERROR,
        //     L"CSuperPoint::GetPath(nStartIndex, nEndIndex) - "
        //     L"name(%s), Invalid nStartIndex(%d), size(%d)",
        //     name.c_str(), startIndex, m_SPNodeCount
        // )
        // Log.Add(LOG_ERROR,
        //     "[NPC route] SuperPoint path %d -> %d is unavailable: start index must be non-negative and below node count
        //     %d. Graph '%s'.", startIndex, endIndex, m_SPNodeCount, WideToUtf8(name).c_str())
        L2_LOG_ERROR(
            Log,
            "[NPC route] SuperPoint path %d -> %d is unavailable: start index must be non-negative and below node count %d. "
            "Graph '%s'.",
            startIndex, endIndex, m_SPNodeCount, name.c_str()
        );
        return nullptr;
    }
    if (endIndex >= m_SPNodeCount) {
        const std::string name = Utf16ToUtf8(runtime::StringView(m_Name));
        // Log.Add(
        //     LOG_ERROR,
        //     L"CSuperPoint::GetPath(nStartIndex, nEndIndex) - "
        //     L"name(%s), Invalid nEndIndex(%d), size(%d)",
        //     name.c_str(), endIndex, m_SPNodeCount
        // )
        // Log.Add(LOG_ERROR,
        //     "[NPC route] SuperPoint path %d -> %d is unavailable: end index must be below node count %d. Graph '%s'.",
        //     startIndex, endIndex, m_SPNodeCount, WideToUtf8(name).c_str())
        L2_LOG_ERROR(
            Log, "[NPC route] SuperPoint path %d -> %d is unavailable: end index must be below node count %d. Graph '%s'.",
            startIndex, endIndex, m_SPNodeCount, name.c_str()
        );
        return nullptr;
    }

    const auto found = m_Paths.find(PathKey(startIndex, endIndex));
    if (found == m_Paths.end()) {
        // Noise: Random-route selection and builder graph enumeration probe absent paths normally. Selected-route failures
        // are reported by their callers. const std::wstring name = Utf16ToNativeWide(m_Name.view()), Log.Add(LOG_ERROR,
        // L"SuperPoint(%s) - No path %d -> %d", name.c_str(), startIndex, endIndex)
        return nullptr;
    }
    return found->second;
}

int CSuperPoint::PushPath(int delay, const FVector& location) {
    m_Delays.push_back(delay);
    m_Locs.push_back(location);
    ++m_SPNodeCount;
    return m_SPNodeCount;
}

int CSuperPoint::GetNodeCount() const { return m_SPNodeCount; }

int CSuperPoint::GetDelay(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= m_Delays.size()) {
        // Log.Add(LOG_ERROR, L"SuperPoint::GetDelay : invalid index[%d]", index)
        // Log.Add(LOG_ERROR,
        //     "[NPC route] Cannot read delay index %d from %zu entries in SuperPoint graph '%s'. Returning 0.",
        //     index, m_Delays.size(), Utf16ToUtf8(m_Name.view()).c_str())
        L2_LOG_ERROR(
            Log, "[NPC route] Cannot read delay index %d from %zu entries in SuperPoint graph '%s'; returning 0.", index,
            m_Delays.size(), Utf16ToUtf8(runtime::StringView(m_Name)).c_str()
        );
        return 0;
    }
    return runtime::CheckedVectorAt(m_Delays, static_cast<std::size_t>(index));
}

FVector CSuperPoint::GetLoc(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= m_Locs.size()) {
        // Log.Add(LOG_ERROR, L"SuperPoint::GetLoc : invalid index[%d]", index)
        // Log.Add(LOG_ERROR,
        //     "[NPC route] Cannot read position index %d from %zu entries in SuperPoint graph '%s'. Returning the zero
        //     vector.", index, m_Locs.size(), Utf16ToUtf8(m_Name.view()).c_str())
        L2_LOG_ERROR(
            Log,
            "[NPC route] Cannot read position index %d from %zu entries in SuperPoint graph '%s'; returning the zero "
            "vector.",
            index, m_Locs.size(), Utf16ToUtf8(runtime::StringView(m_Name)).c_str()
        );
        return {};
    }
    return runtime::CheckedVectorAt(m_Locs, static_cast<std::size_t>(index));
}

int CSuperPoint::GetClosestIndex(int x, int y, int z) const {
    L2_GUARD_FUNCTION();
    int closestIndex = -1;
    double closestDistance = -1.0;
    for (int index = 0; static_cast<std::size_t>(index) < m_Locs.size(); ++index) {
        const FVector position = GetLoc(index);
        const double deltaX = position.x - static_cast<double>(x);
        const double deltaY = position.y - static_cast<double>(y);
        const double deltaZ = position.z - static_cast<double>(z);
        const double distance = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
        if (closestDistance == -1.0 || distance < closestDistance) {
            closestDistance = distance;
            closestIndex = index;
        }
    }
    return closestIndex;
}

void CSuperPoint::ShowSuperPointNodeToBuilder(
    int builderId, int startIndex, int endIndex, IPacketHelper& helper, int& count
) const {
    L2_GUARD_FUNCTION();
    SuperPointNodes* const nodes = GetPath(startIndex, endIndex);
    if (!nodes) {
        // Keep the recovered dereference below. This reports the missing required route before it.
        // Log.Add(LOG_ERROR,
        //     "[builder] Required SuperPoint path %d -> %d is missing in graph '%s' while drawing for builder %d.",
        //     startIndex, endIndex, Utf16ToUtf8(m_Name.view()).c_str(), builderId)
        L2_LOG_ERROR(
            Log, "[builder] Required SuperPoint path %d -> %d is missing in graph '%s' while drawing for builder %d.",
            startIndex, endIndex, Utf16ToUtf8(runtime::StringView(m_Name)).c_str(), builderId
        );
    }
    if (nodes->empty()) {
        const std::wstring label = TransitionLabel(m_Name, startIndex, endIndex);
        if (AssembleServerPrimitiveLine(helper, label.c_str(), 255, 0, 0, 255, GetLoc(startIndex), GetLoc(endIndex)) != 0) {
            ++count;
        }

        const std::string error = "No SuperPoint node [" + TransitionLabelUtf8(m_Name, startIndex, endIndex) + "]";
        // Noise: The host-wide copy served only the old console format. The builder reply keeps its original text below.
        // const std::wstring errorHostWide = Utf8ToWide(error)
        // Log.Add(LOG_ERROR, L"%s", errorHostWide.c_str())
        // Log.Add(LOG_WARN,
        //     "[builder] SuperPoint path %d -> %d in graph '%s' contains no path points. Endpoint line drawing was attempted
        //     for builder %d.", startIndex, endIndex, Utf16ToUtf8(m_Name.view()).c_str(), builderId)
        L2_LOG_WARN(
            Log,
            "[builder] SuperPoint path %d -> %d in graph '%s' contains no path points; endpoint line drawing was attempted "
            "for builder %d.",
            startIndex, endIndex, Utf16ToUtf8(runtime::StringView(m_Name)).c_str(), builderId
        );
        if (builderId != 0) {
            const std::u16string errorWide = Utf8ToUtf16(error);
            g_ns.Send("cdS", 0x54, builderId, errorWide.c_str());
        }
        return;
    }

    if (nodes->size() == 1) {
        return;
    }

    const std::wstring name = Utf16ToWide(runtime::StringView(m_Name), Utf16WidePolicy::PreserveInvalid);
    for (std::size_t index = 0; index + 1 < nodes->size(); ++index) {
        // The target formats this transition label before each line even
        // though the primitive itself is labelled only with m_Name.
        const std::wstring unusedTransition = TransitionLabel(m_Name, startIndex, endIndex);

        if (AssembleServerPrimitiveLine(
                helper, name.c_str(), 0, 0, 255, 128, runtime::CheckedVectorAt((*nodes), index)->Pos,
                runtime::CheckedVectorAt((*nodes), index + 1)->Pos
            ) != 0) {
            ++count;
        }
        if (index + 2 < nodes->size()) {
            const std::wstring pointName = IndexedTransitionLabel(m_Name, startIndex, endIndex, static_cast<int>(index + 1));
            if (AssembleServerPrimitivePoint(
                    helper, pointName.c_str(), 0, 0, 255, 128, runtime::CheckedVectorAt((*nodes), index + 1)->Pos
                ) != 0) {
                ++count;
            }
        }
    }
}

bool CSuperPoint::ShowSuperPointToBuilder(User& builder) const {
    L2_GUARD_FUNCTION();
    if (m_SPNodeCount == 0) {
        const std::string name = Utf16ToUtf8(runtime::StringView(m_Name));
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] superpoint[%s] "
        //     L"nNodeCount = 0",
        //     L"SuperPoint.cpp", 0x134, name.c_str()
        // )
        // Log.Add(LOG_WARN,
        //     "[builder] Cannot draw SuperPoint graph '%s': it has no nodes. Nothing sent to builder %u.",
        //     WideToUtf8(name).c_str(), static_cast<unsigned int>(builder.Id()))
        L2_LOG_WARN(
            Log, "[builder] Cannot draw SuperPoint graph '%s': it has no nodes; nothing sent to builder %u.",
            name.c_str(), static_cast<unsigned int>(builder.Id())
        );
        return false;
    }

    CPacketHelperEx<20480> packet;
    const FVector center = GetLoc(0);
    const std::wstring name = Utf16ToWide(runtime::StringView(m_Name), Utf16WidePolicy::PreserveInvalid);

    if (m_MoveType == SPMoveType::FollowRail) {
        CPacketHelperEx<20480> primitives;
        int primitiveCount = 0;
        for (int index = 0; index < m_SPNodeCount; ++index) {
            const std::wstring pointName = IndexedNodeLabel(m_Name, index);
            if (AssembleServerPrimitivePoint(primitives, pointName.c_str(), 0, 0, 255, 255, GetLoc(index)) != 0) {
                ++primitiveCount;
            }
        }
        for (int index = 0; index + 1 < m_SPNodeCount; ++index) {
            ShowSuperPointNodeToBuilder(static_cast<int>(builder.Id()), index, index + 1, primitives, primitiveCount);
        }
        for (int index = m_SPNodeCount - 1; index > 0; --index) {
            ShowSuperPointNodeToBuilder(static_cast<int>(builder.Id()), index, index - 1, primitives, primitiveCount);
        }

        packet.Assemble(
            "chSddddddb", 0xfe, 0x11, name.c_str(), runtime::Win64CvttDoubleToInt32(center.x),
            runtime::Win64CvttDoubleToInt32(center.y), runtime::Win64CvttDoubleToInt32(center.z), 0x8000, 0x8000,
            primitiveCount, primitives.GetLength(), primitives.GetPacket()
        );
    } else if (m_MoveType == SPMoveType::Random) {
        CPacketHelperEx<20480> primitives;
        int primitiveCount = 0;
        for (int startIndex = 0; startIndex < m_SPNodeCount; ++startIndex) {
            for (int endIndex = 0; endIndex < m_SPNodeCount; ++endIndex) {
                if (startIndex == endIndex) {
                    continue;
                }
                SuperPointNodes* const nodes = GetPath(startIndex, endIndex);
                if (!nodes || nodes->size() < 2) {
                    continue;
                }
                for (std::size_t index = 0; index + 1 < nodes->size(); ++index) {
                    if (AssembleServerPrimitiveLine(
                            primitives, name.c_str(), 0, 0, 255, 128, runtime::CheckedVectorAt((*nodes), index)->Pos,
                            runtime::CheckedVectorAt((*nodes), index + 1)->Pos
                        ) != 0) {
                        ++primitiveCount;
                    }
                }
            }
        }

        packet.Assemble(
            "chSddddddb", 0xfe, 0x11, name.c_str(), runtime::Win64CvttDoubleToInt32(center.x),
            runtime::Win64CvttDoubleToInt32(center.y), runtime::Win64CvttDoubleToInt32(center.z), 0x8000, 0x8000,
            primitiveCount, primitives.GetLength(), primitives.GetPacket()
        );
    }

    g_ns.Send(
        "cddb", 0x7a, static_cast<int>(builder.Id()), packet.GetLength(), packet.GetLength(),
        static_cast<const void*>(packet.GetPacket())
    );
    return false;
}

} // namespace l2geo
