#include "L2NPC/src/Freeway.h"
#include "Shared/src/Runtime/ResourceDirectories.h"
#include "Shared/src/Runtime/Utf8.h"

#include "L2Core/src/inc/Guard.h"
#include "L2NPC/src/ParserFreeway.h"
#include "L2NPC/src/Portable/TargetByteWideInput.h"
#include "Shared/src/Log.h"

#include <cstddef>
#include <fstream>


CFreewayManager g_FreewayManager;

int CFreeway::GetClosestIndex(int x, int y, int z) {
    L2_GUARD_FUNCTION();

    const FVector nowPos{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)};
    int index = -1;
    double minDistance = -1.0;
    for (NodeListType::iterator it = nodeList.begin(); it != nodeList.end(); ++it) {
        CFreewayNode* const node = *it;
        const double distance = node->position.DistCube(nowPos);
        if (minDistance == -1.0 || distance < minDistance) {
            index = node->nodeID;
            minDistance = distance;
        }
    }
    return index;
}

CFreewayNode* CFreeway::GetNode(int index) {
    L2_GUARD_FUNCTION();

    if (index < 0 || static_cast<std::size_t>(index) >= nodeList.size()) {
        return nullptr;
    }
    return runtime::CheckedVectorAt(nodeList, static_cast<std::size_t>(index));
}

CFreeway* CFreewayManager::Find(const int freewayId) {
    L2_GUARD_FUNCTION();

    const FreewayMapType::iterator found = freewayMap.find(freewayId);
    if (found == freewayMap.end()) {
        // Noise: Both movement callers report the missing freeway with NPC/formation context.
        // Log.Add(
        //     LOG_ERROR,
        //     L"%s : cannot find freeway[%d]",
        //     L"CFreewayManager::Find",
        //     freewayId)
        return nullptr;
    }
    return found->second;
}

void CFreewayManager::Load() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const char* nativePath = "..\\Script\\freewayInfo.txt";
#else
    // Linux file names are case sensitive. The data file is freewayinfo.txt.
    constexpr const char* nativePath = "../Script/freewayinfo.txt";
#endif
    // constexpr const wchar_t* targetPath =
    //     L"..\\Script\\freewayInfo.txt".
    [[maybe_unused]] constexpr const wchar_t* targetPath = L"..\\Script\\freewayInfo.txt";

    // std::wifstream file(
    //     runtime::ResolveResourcePath(nativePath))
    const auto resolvedPath = runtime::ResolveResourcePath(nativePath);
#if defined(_WIN32)
    std::wifstream file(resolvedPath);
    const bool fileOpened = static_cast<bool>(file);
#else
    // The lexer reads raw bytes, as from the target MSVC wide stream.
    std::ifstream byteFile(resolvedPath, std::ios::binary);
    l2npc::portable::TargetByteWideInputBuffer fileBuffer(byteFile);
    std::wistream file(&fileBuffer);
    const bool fileOpened = static_cast<bool>(byteFile);
#endif
    if (!fileOpened) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] Read from %s file failed",
        //     L"Freeway.cpp",
        //     0x4a,
        //     targetPath)
        // Log.Add(LOG_ERROR,
        //     "[NPC route] Cannot open freeway definitions at '%s'. Loading skipped.",
        //     resolvedPath.u8string().c_str())
        L2_LOG_ERROR(
            Log, "[NPC route] Cannot open freeway definitions at '%s'; loading skipped.",
            runtime::Utf8Bytes(resolvedPath.u8string()).c_str()
        );
        return;
    }

    CParserForFreeway settingParser;
    if (settingParser.create() == 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] settingParser.create failed",
        //     L"Freeway.cpp",
        //     0x51)
        // Log.Add(LOG_ERROR,
        //     "[NPC route] Cannot initialize the freeway parser for '%s'. Loading skipped.",
        //     resolvedPath.u8string().c_str())
        L2_LOG_ERROR(
            Log, "[NPC route] Cannot initialize the freeway parser for '%s'; loading skipped.",
            runtime::Utf8Bytes(resolvedPath.u8string()).c_str()
        );
        return;
    }

    settingParser.m_lexer.yyin = &file;
    if (settingParser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing %s file failed "
        //     L"after line[%d]",
        //     L"Freeway.cpp",
        //     0x5b,
        //     targetPath,
        //     settingParser.m_lexer.line)
        // Log.Add(LOG_ERROR,
        //     "[NPC route] Cannot parse freeway definitions at '%s' near line %d.",
        //     resolvedPath.u8string().c_str(), settingParser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     "[NPC route] Cannot parse freeway definitions at '%s' near line %d.",
        //     resolvedPath.u8string().c_str(), settingParser.m_lexer.line)
        L2_LOG_ERROR(
            Log, "[NPC route] Cannot parse freeway definitions at '%s' near line %lld.",
            runtime::Utf8Bytes(resolvedPath.u8string()).c_str(), static_cast<long long>(settingParser.m_lexer.line) + 1
        );
    } else if (settingParser.yyerrorcount != 0) {
        L2_LOG_WARN(
            Log,
            "[NPC route] Parser accepted freeway definitions from '%s' after reporting %d errors. Check the preceding "
            "parser diagnostics.",
            runtime::Utf8Bytes(resolvedPath.u8string()).c_str(), settingParser.yyerrorcount
        );
    }
}
