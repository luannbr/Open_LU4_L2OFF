#include "RestrictArea.h"
#include "Shared/src/Runtime/ResourceDirectories.h"

#include "ParserRestrictArea.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"

#include <filesystem>

#if defined(_WIN32)
#include <io.h>
#endif

CRestrictArea g_RestrictArea;
CNpcPosDB g_NpcPosDB;
CNpcPosDB g_NpcAnnounceDB;
CNpcPosDB g_RestrictedAreaDB;

void CRestrictArea::Load() {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    constexpr const wchar_t* restrictAreaFile = L"..\\Script\\RestrictAreaData.txt";
    constexpr const wchar_t* npcPosFile = L"..\\Script\\npcpos_for_server.txt";
#else
    constexpr const wchar_t* restrictAreaFile = L"../script/RestrictAreaData.txt";
    constexpr const wchar_t* npcPosFile = L"../script/npcpos_for_server.txt";
#endif

    LoadScript(restrictAreaFile, true);
#if defined(_WIN32)
    if (_waccess(runtime::ResolveResourcePath(npcPosFile).wstring().c_str(), 0) == 0) {
#else
    if (std::filesystem::exists(runtime::ResolveResourcePath("../script/npcpos_for_server.txt"))) {
#endif
        LoadScript(npcPosFile, true);
    }
}

bool CRestrictArea::LoadData(char16_t* const fileBuffer, const int fileSize) {
    L2_GUARD_FUNCTION();
    CParserForRestrictArea parser;
    if (!parser.Create(fileBuffer, fileSize)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] RestrictAreaParser.create failed",
        //     L"RestrictArea.cpp",
        //     0x53)
        // Log.Add(LOG_ERROR,
        //     L"[area data] Cannot initialize the restricted-area/NPC-position parser.")
        L2_LOG_ERROR(Log, u"[area data] Cannot initialize the restricted-area/NPC-position parser.");
        return false;
    }

    if (parser.yyparse() == 0) {
        return true;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Parsing RestrictArea file failed after line[%d]",
    //     L"RestrictArea.cpp",
    //     0x5e,
    //     parser.m_lexer.line)
    // Log.Add(LOG_ERROR,
    //     L"[area data] Parsing restricted-area/NPC-position data failed near line %d.",
    //     parser.m_lexer.line)
    L2_LOG_ERROR(Log, u"[area data] Parsing restricted-area/NPC-position data failed near line %d.", parser.m_lexer.line);
    return false;
}

bool CRestrictArea::LoadScript(const wchar_t* const fileName, const bool decrypt) {
    L2_GUARD_FUNCTION();
    if (!decrypt) {
        int fileSize = 0;
        std::uint16_t* const image = LoadBinaryAbsoluteW(fileName, fileSize);
        if (image == nullptr) {
            // Log.Add(
            //     LOG_ERROR,
            //     L"[%s][%d] Read from [%d] file failed",
            //     L"RestrictArea.cpp",
            //     0x40,
            //     fileName)
            // Log.Add(LOG_ERROR,
            //     L"[area data] Cannot read requested file \"%ls\".",
            //     fileName ? fileName : L"<unspecified>")
            L2_LOG_ERROR(Log, u"[area data] Cannot read requested file \"%ls\".", fileName ? fileName : L"<unspecified>");
            return false;
        }
        // return LoadData(
        //     reinterpret_cast<char16_t*>(image) + 1,
        //     fileSize - 1)
        const bool loaded = LoadData(reinterpret_cast<char16_t*>(image) + 1, fileSize - 1);
        if (!loaded) {
            // Log.Add(LOG_ERROR,
            //     L"[area data] Parsing failed for requested file \"%ls\".",
            //     fileName ? fileName : L"<unspecified>")
            L2_LOG_ERROR(
                Log, u"[area data] Parsing failed for requested file \"%ls\".", fileName ? fileName : L"<unspecified>"
            );
        }
        return loaded;
    }

    CSecureStream input(fileName, true);
    if (!input.fail()) {
        // return LoadData(
        //     input.GetRawString() + 1,
        //     input.GetRawStringLen() - 1)
        const bool loaded = LoadData(input.GetRawString() + 1, input.GetRawStringLen() - 1);
        if (!loaded) {
            // Log.Add(LOG_ERROR,
            //     L"[area data] Parsing failed for requested file \"%ls\".",
            //     fileName ? fileName : L"<unspecified>")
            L2_LOG_ERROR(
                Log, u"[area data] Parsing failed for requested file \"%ls\".", fileName ? fileName : L"<unspecified>"
            );
        }
        return loaded;
    }

    // Log.Add(
    //     LOG_ERROR,
    //     L"[%s][%d] Read from [%d] file failed",
    //     L"RestrictArea.cpp",
    //     0x31,
    //     fileName)
    // Log.Add(LOG_ERROR,
    //     L"[area data] Cannot read requested file \"%ls\".",
    //     fileName ? fileName : L"<unspecified>")
    L2_LOG_ERROR(Log, u"[area data] Cannot read requested file \"%ls\".", fileName ? fileName : L"<unspecified>");
    return false;
}

CTerritory* CNpcPosDB::GetNpcPos(char16_t* name) {
    const std::u16string key(name);
    const auto position = m_NpcPosDB.find(key);
    if (position == m_NpcPosDB.end()) {
        return nullptr;
    }
    return position->second;
}

void CNpcPosDB::AddNpcPos(CTerritory* territory) { m_NpcPosDB.emplace(territory->sName, territory); }
