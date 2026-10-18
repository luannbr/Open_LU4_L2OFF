#include "FlyMove.h"

#include "CWorld.h"
#include "ParserFlyMove.h"
#include "ThreadLocalWorldMap.h"
#include "User.h"
#include "UserSocket.h"

#include "Shared/src/IOCPacket.h"
#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <array>
#include <cstdint>

CFlyMovePathNode::CFlyMovePathNode(int index) :
    m_nIndex(index), m_bNonLanding(false), m_nFlyingHeight(0) {}

void CFlyMovePathNode::InsertRelatedNode(int nodeId) {
    L2_GUARD_FUNCTION();
    m_RelatedNodeList.push_back(nodeId);
}

bool CFlyMovePathNode::IsRelatedNode(int nodeId) const {
    L2_GUARD_FUNCTION();
    for (const int relatedNodeId : m_RelatedNodeList) {
        if (relatedNodeId == nodeId) {
            return true;
        }
    }
    return false;
}

CFlyMovePath::CFlyMovePath(int id) : m_nId(id) {}

void CFlyMovePath::InsertNode(CFlyMovePathNode* node) {
    L2_GUARD_FUNCTION();
    m_NodeList.push_back(node);
}

CFlyMovePathNode* CFlyMovePath::GetNode(int nodeId) const {
    L2_GUARD_FUNCTION();
    const std::uint64_t index = static_cast<std::uint64_t>(static_cast<std::int64_t>(nodeId));
    if (index >= m_NodeList.size()) {
        return nullptr;
    }
    return m_NodeList[static_cast<std::size_t>(index)];
}

CFlyMoveManager& CFlyMoveManager::Inst() {
    static CFlyMoveManager instance;
    return instance;
}

void CFlyMoveManager::InsertPath(CFlyMovePath* path) {
    L2_GUARD_FUNCTION();
    m_PathList.push_back(path);
}

CFlyMovePathNode* CFlyMoveManager::GetPathNode(int pathId, int nodeId) const {
    L2_GUARD_FUNCTION();
    const std::uint64_t index = static_cast<std::uint64_t>(static_cast<std::int64_t>(pathId));
    if (index >= m_PathList.size()) {
        return nullptr;
    }
    CFlyMovePath* const path = m_PathList[static_cast<std::size_t>(index)];
    return path == nullptr ? nullptr : path->GetNode(nodeId);
}

bool CFlyMoveManager::IsRelatedNode(int pathId, int nodeId, int relatedNodeId) const {
    L2_GUARD_FUNCTION();
    CFlyMovePathNode* const node = Inst().GetPathNode(pathId, nodeId);
    return node != nullptr && node->IsRelatedNode(relatedNodeId);
}

void CFlyMoveManager::Land(User* user, int pathId, int nodeId) {
    L2_GUARD_FUNCTION();
    CFlyMovePathNode* const node = Inst().GetPathNode(pathId, nodeId);
    if (node == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"no landing pos for flymovemanager [%d][%d]",
        //     pathId,
        //     nodeId)
        // L2_LOG_ERROR(Log,
        //     L"no landing pos for flymovemanager [%d][%d]",
        //     pathId,
        //     nodeId)
        L2_LOG_ERROR(
            Log, "Cannot land on fly-move path %d at node %d: path node is missing; landing is skipped.", pathId, nodeId
        );
        return;
    }
    if (node->IsNonLanding()) {
        return;
    }

    user->m_bFlyMoving = false;
    const int mode = node->GetRelatedNodeList().empty() ? 2 : 1;
    const FVector& nodePosition = node->GetPos();
    const FVector& landingPosition = node->GetLandingPos();
    g_ptlUserMap->BroadcastToNeighborExceptSelf(
        user, 0x80000, user->GetPos(), "chdddddddddd", 0xfe, 0x108, static_cast<int>(user->GetServerId()), mode,
        node->GetFlyingHeight(), static_cast<int>(nodePosition.x), static_cast<int>(nodePosition.y),
        static_cast<int>(nodePosition.z), 0, static_cast<int>(landingPosition.x), static_cast<int>(landingPosition.y),
        static_cast<int>(landingPosition.z)
    );
    CWorld::Inst().MoveTo(user, landingPosition, true);
}

void CFlyMoveManager::FlyMove(User* user, int pathId, int nodeId, bool continuation) {
    L2_GUARD_FUNCTION();
    CFlyMovePathNode* const node = Inst().GetPathNode(pathId, nodeId);
    if (node == nullptr) {
        user->m_bFlyMoving = false;
        return;
    }

    const std::vector<int> relatedNodes(node->GetRelatedNodeList());
    std::array<char, 1024> buffer{};
    int bufferLength = 0;
    unsigned int nodeCount = 0;

    if (!node->IsNonLanding()) {
        const FVector& landingPosition = node->GetLandingPos();
        bufferLength = ::Assemble(
            buffer.data(), static_cast<int>(buffer.size()), "ddddd", -1, 0, static_cast<int>(landingPosition.x),
            static_cast<int>(landingPosition.y), static_cast<int>(landingPosition.z)
        );
        nodeCount = 1;
    }

    for (const int relatedNodeId : relatedNodes) {
        CFlyMovePathNode* const relatedNode = Inst().GetPathNode(pathId, relatedNodeId);
        if (relatedNode == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"Invalid Node in FlyMoveManager! path[%d]node[%d]",
            //     pathId,
            //     relatedNodeId)
            // L2_LOG_ERROR(Log,
            //     L"Invalid Node in FlyMoveManager! path[%d]node[%d]",
            //     pathId,
            //     relatedNodeId)
            L2_LOG_ERROR(
                Log, "Fly-move path %d node %d references missing node %d; this destination is omitted.", pathId, nodeId,
                relatedNodeId
            );
            continue;
        }

        const FVector& position = relatedNode->GetPos();
        bufferLength += ::Assemble(
            buffer.data() + bufferLength, static_cast<int>(buffer.size()) - bufferLength, "ddddd", relatedNodeId,
            relatedNode->GetFlyingHeight(), static_cast<int>(position.x), static_cast<int>(position.y),
            static_cast<int>(position.z)
        );
        ++nodeCount;
    }

    int mode = continuation ? 0 : 1;
    if (nodeCount == 1) {
        mode = 2;
    }
    user->GetSocket()->Send(
        "chdddddb", 0xfe, 0xe8, static_cast<int>(user->GetServerId()), mode, node->GetFlyingHeight(), pathId,
        static_cast<int>(nodeCount), bufferLength, buffer.data()
    );
    user->ReadyToFlyMove();
}

void CFlyMoveManager::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFileName = L"..\\Script\\flymovedata.txt";
#else
    constexpr const wchar_t* kFileName = L"../script/flymovedata.txt";
#endif

    CSecureStream input(kFileName, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from %s file failed",
        //     L"FlyMove.cpp",
        //     0xe4,
        //     kFileName)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from %s file failed",
        //     L"FlyMove.cpp",
        //     0xe4,
        //     kFileName)
        L2_LOG_ERROR(Log, "Cannot read flymovedata.txt; fly-move loading stops.");
        return;
    }

    CParserForFlyMove parser;
    if (!parser.create()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] CParserForFlyMove::create failed",
        //     L"FlyMove.cpp",
        //     0xec)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] CParserForFlyMove::create failed",
        //     L"FlyMove.cpp",
        //     0xec)
        L2_LOG_ERROR(Log, "Cannot initialize the flymovedata.txt parser; fly-move loading stops.");
        return;
    }

    parser.m_lexer.yyin = input.GetStream();
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing FlyMove file failed after line[%d]",
        //     L"FlyMove.cpp",
        //     0xf7,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing FlyMove file failed after line[%d]",
        //     L"FlyMove.cpp",
        //     0xf7,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "Cannot parse flymovedata.txt at script line %lld; already inserted paths are retained.",
            static_cast<long long>(parser.m_lexer.line) + 1
        );
    }
}
