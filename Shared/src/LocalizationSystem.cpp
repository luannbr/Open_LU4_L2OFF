#include "Shared/src/LocalizationSystem.h"
#include "Shared/src/Runtime/Utf8.h"

#include "Shared/src/Log.h"
#include "Shared/src/Utils.h"
#include "TextRuntime.h"

#if defined(L2_BUILD_L2NPC)
#include "L2NPC/src/NpcMakerDB.h"
#include "L2NPC/src/ParserSpawn.h"
// resolve the actual file path for missing-file diagnostics.
#include "Shared/src/Runtime/ResourceDirectories.h"
#elif defined(L2_BUILD_L2SERVER)
#include "L2Server/src/MultiSellParser.h"
#include "L2Server/src/ParserColorEx.h"
#include "L2Server/src/ParserFaceEx.h"
#include "L2Server/src/ParserHairEx.h"
#include "L2Server/src/TotalChecker.h"
#endif

#include <algorithm>
#include <array>
#include <cwchar>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>

namespace {

// Original source location used by the replaced diagnostics.
/*
constexpr const wchar_t* kSourceFile =
    L"..\\Shared\\LocalizationSystem.cpp".
*/

std::mutex g_localizationAdapterMutex;
std::filesystem::path g_localizationNpcPosBasePath;

std::filesystem::path GetLocalizationNpcPosBasePath() {
    std::lock_guard<std::mutex> guard(g_localizationAdapterMutex);
    return g_localizationNpcPosBasePath;
}

} // namespace

LocalizationSystem g_Localization;

#if defined(L2_BUILD_L2SERVER)

void LocalizationSystem::LoadMultiSell(int /*countryCodeMaximum*/) {
    L2_GUARD_FUNCTION();
    constexpr wchar_t kBaseName[] = L"..\\Script\\Localization\\multisell.txt";
    std::array<wchar_t, 1024> localizedName{};
    ChangeFileNameForLocalization(kBaseName, localizedName.data(), -1);
    if (std::wcscmp(kBaseName, localizedName.data()) == 0) {
        return;
    }

    CSecureStream stream(localizedName.data(), true);
    if (stream.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from MultiSell file failed",
        //     kSourceFile,
        //     0x49)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Cannot read trade list data from \"%ls\". Loading skipped.",
        //     localizedName.data())
        L2_LOG_ERROR(
            Log, u"[localization] Cannot read trade list data from \"%ls\"; loading skipped.", localizedName.data()
        );
        return;
    }

    CParserForMultiSell parser;
    if (!parser.Create(stream.GetRawString() + 1, stream.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] MultiSellParser.create failed",
        //     kSourceFile,
        //     0x51)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Cannot initialize the parser for trade list data from \"%ls\". Loading skipped.",
        //     localizedName.data())
        L2_LOG_ERROR(
            Log, u"[localization] Cannot initialize the parser for trade list data from \"%ls\"; loading skipped.",
            localizedName.data()
        );
        return;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing MultiSell file failed after line[%d]",
        //     kSourceFile,
        //     0x5c,
        //     parser.m_lexer.yylineno)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Parsing trade list data from \"%ls\" failed near line %d.",
        //     localizedName.data(), parser.m_lexer.yylineno)
        L2_LOG_ERROR(
            Log, u"[localization] Parsing trade list data from \"%ls\" failed near line %d.", localizedName.data(),
            parser.m_lexer.yylineno
        );
    }
}

bool LocalizationSystem::LoadHairData(int /*countryCodeMaximum*/) {
    L2_GUARD_FUNCTION();
    constexpr wchar_t kBaseName[] = L"..\\Script\\Localization\\hairexdata.txt";
    std::array<wchar_t, 1024> localizedName{};
    ChangeFileNameForLocalization(kBaseName, localizedName.data(), -1);
    if (std::wcscmp(kBaseName, localizedName.data()) == 0) {
        return false;
    }

    CSecureStream stream(localizedName.data(), true);
    if (stream.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from [%s] file failed",
        //     kSourceFile,
        //     0x76,
        //     kBaseName)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Cannot read hair data from \"%ls\". Loading skipped.",
        //     localizedName.data())
        L2_LOG_ERROR(Log, u"[localization] Cannot read hair data from \"%ls\"; loading skipped.", localizedName.data());
        return false;
    }

    ParserForHairEx parser;
    if (!parser.Create(stream.GetRawString() + 1, stream.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] HairExData.create failed",
        //     kSourceFile,
        //     0x7f)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Cannot initialize the parser for hair data from \"%ls\". Loading skipped.",
        //     localizedName.data())
        L2_LOG_ERROR(
            Log, u"[localization] Cannot initialize the parser for hair data from \"%ls\"; loading skipped.",
            localizedName.data()
        );
        return false;
    }

    parser.m_lexer.yybegin(4);
    const int result = parser.yyparse();
    if (result != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing HairExData file failed after line[%d]",
        //     kSourceFile,
        //     0x8a,
        //     parser.m_lexer.yylineno)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Parsing hair data from \"%ls\" failed near line %d.",
        //     localizedName.data(), parser.m_lexer.yylineno)
        L2_LOG_ERROR(
            Log, u"[localization] Parsing hair data from \"%ls\" failed near line %d.", localizedName.data(),
            parser.m_lexer.yylineno
        );
    }
    return result == 0;
}

bool LocalizationSystem::LoadFaceData(int /*countryCodeMaximum*/) {
    L2_GUARD_FUNCTION();
    constexpr wchar_t kBaseName[] = L"..\\Script\\Localization\\faceexdata.txt";
    std::array<wchar_t, 1024> localizedName{};
    ChangeFileNameForLocalization(kBaseName, localizedName.data(), -1);
    if (std::wcscmp(kBaseName, localizedName.data()) == 0) {
        return false;
    }

    CSecureStream stream(localizedName.data(), true);
    if (stream.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from [%s] file failed",
        //     kSourceFile,
        //     0xa6,
        //     kBaseName)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Cannot read face data from \"%ls\". Loading skipped.",
        //     localizedName.data())
        L2_LOG_ERROR(Log, u"[localization] Cannot read face data from \"%ls\"; loading skipped.", localizedName.data());
        return false;
    }

    ParserForFaceEx parser;
    if (!parser.Create(stream.GetRawString() + 1, stream.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] FaceExData.create failed",
        //     kSourceFile,
        //     0xaf)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Cannot initialize the parser for face data from \"%ls\". Loading skipped.",
        //     localizedName.data())
        L2_LOG_ERROR(
            Log, u"[localization] Cannot initialize the parser for face data from \"%ls\"; loading skipped.",
            localizedName.data()
        );
        return false;
    }

    parser.m_lexer.yybegin(4);
    const int result = parser.yyparse();
    if (result != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing FaceExData file failed after line[%d]",
        //     kSourceFile,
        //     0xba,
        //     parser.m_lexer.yylineno)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Parsing face data from \"%ls\" failed near line %d.",
        //     localizedName.data(), parser.m_lexer.yylineno)
        L2_LOG_ERROR(
            Log, u"[localization] Parsing face data from \"%ls\" failed near line %d.", localizedName.data(),
            parser.m_lexer.yylineno
        );
    }
    return result == 0;
}

bool LocalizationSystem::LoadColorData(int /*countryCodeMaximum*/) {
    L2_GUARD_FUNCTION();
    constexpr wchar_t kBaseName[] = L"..\\Script\\Localization\\colorexdata.txt";
    std::array<wchar_t, 1024> localizedName{};
    ChangeFileNameForLocalization(kBaseName, localizedName.data(), -1);
    if (std::wcscmp(kBaseName, localizedName.data()) == 0) {
        return false;
    }

    CSecureStream stream(localizedName.data(), true);
    if (stream.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from [%s] file failed",
        //     kSourceFile,
        //     0xd6,
        //     kBaseName)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Cannot read color data from \"%ls\". Loading skipped.",
        //     localizedName.data())
        L2_LOG_ERROR(Log, u"[localization] Cannot read color data from \"%ls\"; loading skipped.", localizedName.data());
        return false;
    }

    ParserForColorEx parser;
    if (!parser.Create(stream.GetRawString() + 1, stream.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] HairExData.create failed",
        //     kSourceFile,
        //     0xdf)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Cannot initialize the parser for color data from \"%ls\". Loading skipped.",
        //     localizedName.data())
        L2_LOG_ERROR(
            Log, u"[localization] Cannot initialize the parser for color data from \"%ls\"; loading skipped.",
            localizedName.data()
        );
        return false;
    }

    parser.m_lexer.yybegin(4);
    const int result = parser.yyparse();
    if (result != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing ColorExData file failed after line[%d]",
        //     kSourceFile,
        //     0xea,
        //     parser.m_lexer.yylineno)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Parsing color data from \"%ls\" failed near line %d.",
        //     localizedName.data(), parser.m_lexer.yylineno)
        L2_LOG_ERROR(
            Log, u"[localization] Parsing color data from \"%ls\" failed near line %d.", localizedName.data(),
            parser.m_lexer.yylineno
        );
    }
    return result == 0;
}

bool LocalizationSystem::LoadNpcposData(int /*countryCodeMaximum*/) {
    L2_GUARD_FUNCTION();
    constexpr wchar_t kDefaultName[] = L"..\\Script\\Localization\\npcpos.txt";
    const std::filesystem::path configuredPath = GetLocalizationNpcPosBasePath();
    const std::wstring baseName = configuredPath.empty() ? std::wstring(kDefaultName) : Utf8ToWide(configuredPath.string());
    std::array<wchar_t, 1024> localizedName{};
    ChangeFileNameForLocalization(baseName.c_str(), localizedName.data(), -1);
    if (std::wcscmp(baseName.c_str(), localizedName.data()) == 0) {
        return false;
    }

    CSecureStream stream(localizedName.data(), true);
    if (stream.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"Read from %s file failed",
        //     localizedName.data())
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Cannot read NPC positions from \"%ls\". Loading skipped.",
        //     localizedName.data())
        L2_LOG_ERROR(Log, u"[localization] Cannot read NPC positions from \"%ls\"; loading skipped.", localizedName.data());
        return false;
    }

    // Semantic diagnostics can leave yyparse successful, so identify the
    // selected file before entering the checker as well as on parse failure.
    // Log.Add(LOG_NORMAL,
    //     L"[localization] Checking NPC positions from \"%ls\".",
    //     localizedName.data())
    L2_LOG_INFO(Log, u"[localization] Checking NPC positions from \"%ls\".", localizedName.data());

    // Current 0x00419378 ignores the checker result and reports
    // success once the localized file itself was read.
    // CNpcPosChecker::Inst()->LoadData(
    //     stream.GetRawString() + 1,
    //     stream.GetRawStringLen() - 1)
    // Observe failure for diagnostics without changing the return policy.
    const bool checked = CNpcPosChecker::Inst()->LoadData(stream.GetRawString() + 1, stream.GetRawStringLen() - 1);
    if (!checked) {
        // Log.Add(LOG_ERROR,
        //     L"[localization] NPC position checks failed for \"%ls\".",
        //     localizedName.data())
        L2_LOG_ERROR(Log, u"[localization] NPC position checks failed for \"%ls\".", localizedName.data());
    }
    return true;
}

void LocalizationSystem::LoadLocalizationData(int countryCodeMaximum) {
    L2_GUARD_FUNCTION();
    LoadMultiSell(countryCodeMaximum);
    LoadHairData(countryCodeMaximum);
    LoadFaceData(countryCodeMaximum);
    LoadColorData(countryCodeMaximum);
    LoadNpcposData(countryCodeMaximum);
}

#elif defined(L2_BUILD_L2NPC)

bool LocalizationSystem::LoadNpcposData(int countryCodeMaximum) {
    L2_GUARD_FUNCTION();
    const std::filesystem::path configuredPath = GetLocalizationNpcPosBasePath();
    wchar_t localizedName[1024]{};
    const std::wstring baseName =
        configuredPath.empty() ? L"..\\Script\\Localization\\npcpos.txt" : Utf8ToWide(configuredPath.string());
    ChangeFileNameForLocalization(baseName.c_str(), localizedName, countryCodeMaximum);
    if (std::wcscmp(baseName.c_str(), localizedName) == 0) {
        return false;
    }

    int codeUnitCount = 0;
    std::uint16_t* const image = LoadBinaryAbsoluteW(localizedName, codeUnitCount);
    if (!image) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] Read from Objects file failed",
        //     kSourceFile,
        //     0x11a)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Cannot read NPC positions from \"%ls\". Loading skipped.",
        //     localizedName)
        // a missing regional supplement does not discard
        // the base spawn definitions. Only classify confirmed absence as
        // WARN. Unreadable files and failed status checks remain ERROR.
        // Keep the original load attempt and false return unchanged.
        // L2_LOG_ERROR(Log,
        //     L"[localization] Cannot read NPC positions from \"%ls\". Loading skipped.",
        //     localizedName)
        std::string diagnosticName = WideToUtf8(localizedName);
#if !defined(_WIN32)
        std::replace(diagnosticName.begin(), diagnosticName.end(), '\\', '/');
#endif
        const std::filesystem::path diagnosticPath =
            runtime::ResolveResourcePath(std::filesystem::path(runtime::Utf8CodeUnits(diagnosticName)));
        std::error_code statusError;
        const std::filesystem::file_status status = std::filesystem::status(diagnosticPath, statusError);
        const bool missing = status.type() == std::filesystem::file_type::not_found &&
            (!statusError || statusError == std::errc::no_such_file_or_directory);
        if (missing) {
            L2_LOG_WARN(
                Log,
                "[localization] Regional NPC spawn file '%s' is absent; regional additions are skipped. Previously loaded "
                "base spawn definitions are retained.",
                runtime::Utf8Bytes(diagnosticPath.u8string()).c_str()
            );
        } else {
            L2_LOG_ERROR(Log, u"[localization] Cannot read NPC positions from \"%ls\"; loading skipped.", localizedName);
        }
        return false;
    }

    l2npc::CParserForNPCSpawn npcspawnParser;
    if (!npcspawnParser.Create(&l2npc::g_NpcMakerDB, image + 1, codeUnitCount - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] npcspawnParser.create failed",
        //     kSourceFile,
        //     0x122)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Cannot initialize the NPC spawn parser for \"%ls\". Loading skipped.",
        //     localizedName)
        L2_LOG_ERROR(
            Log, u"[localization] Cannot initialize the NPC spawn parser for \"%ls\"; loading skipped.", localizedName
        );
        return false;
    }
    if (npcspawnParser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing NPC Spawn file failed, at line[%d]",
        //     kSourceFile,
        //     0x12d,
        //     npcspawnParser.m_lexer.yylineno)
        // Log.Add(
        //     LOG_ERROR,
        //     L"[localization] Parsing NPC positions from \"%ls\" failed near line %d.",
        //     localizedName, npcspawnParser.m_lexer.yylineno)
        L2_LOG_ERROR(
            Log, u"[localization] Parsing NPC positions from \"%ls\" failed near line %d.", localizedName,
            npcspawnParser.m_lexer.yylineno
        );
        return false;
    }

    // Log.Add(
    //     LOG_NORMAL,
    //     L"loading file '%s' finished",
    //     localizedName)
    // Log.Add(
    //     LOG_NORMAL,
    //     L"[localization] Loaded NPC positions from \"%ls\".",
    //     localizedName)
    L2_LOG_INFO(Log, u"[localization] Loaded NPC positions from \"%ls\".", localizedName);
    return true;
}

void LocalizationSystem::LoadLocalizationData(int countryCodeMaximum) {
    L2_GUARD_FUNCTION();
    LoadNpcposData(countryCodeMaximum);
}

#endif

namespace portable_runtime {

void ConfigureLocalizationNpcPos(std::filesystem::path baseNpcPosPath) {
    std::lock_guard<std::mutex> guard(g_localizationAdapterMutex);
    g_localizationNpcPosBasePath = std::move(baseNpcPosPath);
}

void ResetLocalizationNpcPos() {
    std::lock_guard<std::mutex> guard(g_localizationAdapterMutex);
    g_localizationNpcPosBasePath.clear();
}

} // namespace portable_runtime
