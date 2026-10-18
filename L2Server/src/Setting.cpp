#include "Setting.h"

#include "FieldCycle.h"

#include "PCParam.h"
#include "ParserColorEx.h"
#include "ParserExpData.h"
#include "ParserFaceEx.h"
#include "ParserHairEx.h"
#include "ParserSetting.h"

#include "Shared/src/Exception.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Shared/src/Utils.h"

#include "Shared/src/Runtime/Win64Floating.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

} // namespace

CSetting::CSetting() {
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    constexpr const char16_t* kSetting = u"..\\Script\\setting.txt";
    constexpr const char16_t* kSettingTest = u"..\\Script\\setting_test.txt";
    constexpr const char16_t* kSettingBroadcast = u"..\\Script\\setting_broadcast.txt";
#else
    constexpr const char16_t* kSetting = u"../script/setting.txt";
    constexpr const char16_t* kSettingTest = u"../script/setting_test.txt";
    constexpr const char16_t* kSettingBroadcast = u"../script/setting_broadcast.txt";
#endif

    fileNameMap[L2ServerType::LIVE] = kSetting;
    fileNameMap[L2ServerType::TEST] = kSettingTest;
    fileNameMap[L2ServerType::DEV] = kSetting;
    fileNameMap[L2ServerType::MOVIE] = kSetting;
    fileNameMap[L2ServerType::BROADCAST] = kSettingBroadcast;
    fileNameMap[L2ServerType::WORLDSERVER] = kSetting;
    std::memset(m_RestartPointGroupForMap, 0, sizeof(m_RestartPointGroupForMap));
}

CSetting::~CSetting() {
    // Current 0x00a48940 does not own/delete the parsed restart
    // records. It releases only the carrier containers and clears this
    // non-owning pointer before the compiler-generated member teardown.
    m_pIsolatedRestartPointGroup = nullptr;
}

const char16_t* CSetting::GetSettingFileName(const L2ServerType::Value serverType) {
    // Current L2Server 0x00a47cf4. The lookup is non-inserting.
    // unknown server types fall back to the normal setting file.
    const auto found = fileNameMap.find(serverType);
    if (found != fileNameMap.end()) {
        return found->second;
    }
#if defined(_WIN32)
    return u"..\\Script\\setting.txt";
#else
    return u"../script/setting.txt";
#endif
}

bool CSetting::LoadSetting() {
    // Current L2Server 0x00a48c04.
    L2_GUARD_FUNCTION();

    const char16_t* const targetPath = GetSettingFileName(g_L2ServerType);
    const std::wstring hostPath = Utf16ToWide(std::u16string_view(targetPath));
    CSecureStream input(hostPath.c_str(), true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Setting file failed",
        //     L"Setting.cpp",
        //     0x4f)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from Setting file failed",
        //     L"Setting.cpp",
        //     0x4f)
        L2_LOG_ERROR(
            Log, u"Cannot read the selected settings resource '%ls' for server type %d; loading failed.", hostPath.c_str(),
            static_cast<int>(g_L2ServerType)
        );
        return false;
    }
    return LoadData(input.GetRawString() + 1, input.GetRawStringLen() - 1);
}

bool CSetting::LoadMinigame() {
    // Current L2Server 0x00a49850.
    L2_GUARD_FUNCTION();
#if defined(_WIN32)
    constexpr const wchar_t* kMinigamePath = L"..\\Script\\minigame.txt";
#else
    constexpr const wchar_t* kMinigamePath = L"../script/minigame.txt";
#endif

    CSecureStream input(kMinigamePath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from Setting file failed",
        //     L"Setting.cpp",
        //     0x10e)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from Setting file failed",
        //     L"Setting.cpp",
        //     0x10e)
        L2_LOG_ERROR(
            Log, u"Cannot read minigame.txt from the configured scripts directory; minigame settings were not loaded."
        );
        return false;
    }
    return LoadData(input.GetRawString() + 1, input.GetRawStringLen() - 1);
}

bool CSetting::LoadData(char16_t* const fileBuffer, const int fileSize) {
    // Current L2Server 0x00a46260. Unlike the smaller generated data
    // loaders, Setting starts in the lexer's default state. There is no
    // yybegin(4) store in this call path.
    L2_GUARD_FUNCTION();

    CParserForSetting parser;
    if (!parser.Create(this, fileBuffer, fileSize)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] settingParser.create failed",
        //     L"Setting.cpp",
        //     0x120)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] settingParser.create failed",
        //     L"Setting.cpp",
        //     0x120)
        L2_LOG_ERROR(Log, u"Cannot initialize the settings parser; loading failed.");
        return false;
    }

    const int parseResult = parser.yyparse();
    if (parseResult != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing Setting file failed after line[%d]",
        //     L"Setting.cpp",
        //     299,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing Setting file failed after line[%d]",
        //     L"Setting.cpp",
        //     299,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(Log, u"Cannot parse settings data at line %d; the load did not complete.", parser.m_lexer.line);
    }
    return parseResult == 0;
}

void CSetting::AddRestartPointGroup(RestartPointGroup* const group) {
    // Current L2Server 0x00a48ac4.
    L2_GUARD_FUNCTION();
    m_RestartPointGroup.push_back(group);
}

void CSetting::AddRestartTerritory(RestartTerritory* const territory) {
    // Current L2Server 0x00a48b64.
    L2_GUARD_FUNCTION();
    m_RestartTerritory.push_back(territory);
}

void CSetting::MapRestartPointGroup(RestartPointGroup* const group, const int mapX, const int mapY) {
    // Current L2Server 0x00a463e0 uses unsigned range checks after
    // subtracting the script's ten-based map coordinates.
    L2_GUARD_FUNCTION();
    if (static_cast<std::uint32_t>(mapX - 10) < 20U && static_cast<std::uint32_t>(mapY - 10) < 17U) {
        m_RestartPointGroupForMap[mapX - 10][mapY - 10] = group;
        return;
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"invalid map index[%d][%d]",
    //     mapX,
    //     mapY)
    // L2_LOG_ERROR(Log,
    //     L"invalid map index[%d][%d]",
    //     mapX,
    //     mapY)
    L2_LOG_ERROR(
        Log, u"Cannot map a restart-point group to map (%d, %d): expected X 10..29 and Y 10..26; mapping was skipped.", mapX,
        mapY
    );
}

CSetting::RestartPointGroup* CSetting::FindRestartPointGroupByName(const char16_t* const name) {
    // Current L2Server 0x00a46b34 performs a case-sensitive
    // comparison and logs on every miss.
    L2_GUARD_FUNCTION();
    for (RestartPointGroup* const group : m_RestartPointGroup) {
        if (group != nullptr && runtime::StringView(group->m_sName) == std::u16string_view(name)) {
            return group;
        }
    }
    // Log.Add(
    //     LOG_ERROR,
    //     L"cannot find restart point group name[%s]",
    //     name)
    // L2_LOG_ERROR(Log,
    //     L"cannot find restart point group name[%s]",
    //     name)
    L2_LOG_ERROR(Log, u"No restart-point group named '%ls' was found; lookup failed.", name);
    return nullptr;
}

namespace {

char16_t FoldSettingAsciiCase(const char16_t value) {
    return value >= u'A' && value <= u'Z' ? static_cast<char16_t>(value + (u'a' - u'A')) : value;
}

bool SettingNameEqualsAsciiCaseInsensitive(std::u16string_view left, std::u16string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (FoldSettingAsciiCase(left[index]) != FoldSettingAsciiCase(right[index])) {
            return false;
        }
    }
    return true;
}

} // namespace

void CSetting::PostProcessRestartPointGroup() {
    // Current L2Server 0x00a46c58.
    L2_GUARD_FUNCTION();
    m_pIsolatedRestartPointGroup = nullptr;

    static constexpr std::u16string_view kNoviceGroups[]{
        u"talking_island_town",
        u"elf_town",
        u"darkelf_town",
        u"orc_town",
        u"dwarf_town",
        u"Cedric's_Training_Hall",
        u"Einhovant's_School_of_Magic",
        u"Mothertree_Glade",
        u"Temple_of_Shilen",
        u"Temple_of_Paagrio",
        u"Quarry",
        u"spirit_cave",
        u"kamael_town",
    };

    for (RestartPointGroup* const group : m_RestartPointGroup) {
        if (group == nullptr) {
            continue;
        }
        for (std::size_t race = 0; race < RT_MAX; ++race) {
            if (group->m_bBanned[race]) {
                group->m_pBannedAlternative[race] =
                    FindRestartPointGroupByName(group->m_sBannedAlternativeName[race].c_str());
            }
        }

        for (const std::u16string_view noviceName : kNoviceGroups) {
            if (SettingNameEqualsAsciiCaseInsensitive(runtime::StringView(group->m_sName), noviceName)) {
                group->m_bNoviceZone = true;
                break;
            }
        }
        if (SettingNameEqualsAsciiCaseInsensitive(runtime::StringView(group->m_sName), u"floran_town")) {
            m_pIsolatedRestartPointGroup = group;
        }
    }

    for (RestartTerritory* const territory : m_RestartTerritory) {
        if (territory == nullptr) {
            continue;
        }
        for (std::size_t race = 0; race < RT_MAX; ++race) {
            if (territory->m_bRace[race]) {
                territory->m_pRestartPointGroup[race] =
                    FindRestartPointGroupByName(territory->m_sRestartPointGroupName[race].c_str());
            }
        }
    }
}

bool CSetting::IsExistKey(Beauty::BeautyState state, int first, int second) {
    L2_GUARD_FUNCTION();
    const BeautyMap& beauty = m_mapBeauty[static_cast<std::size_t>(state)];
    return beauty.find(BeautyKey(first, second)) != beauty.end();
}

void CSetting::SetBeautyEx(
    Beauty::BeautyState state,
    int first,
    int second,
    std::int64_t adena,
    std::int64_t resetAdena,
    std::int64_t coin,
    int limit,
    short enable
) {
    L2_GUARD_FUNCTION();

    StrAdenaCoin value;
    value.Adena = adena;
    value.Coin = coin;
    value.ResetAdena = resetAdena;
    value.Limit = limit;
    value.bEnable = enable != 0;

    m_mapBeauty[static_cast<std::size_t>(state)].insert(std::make_pair(BeautyKey(first, second), value));
}

void CSetting::DeleteBeautyEx(Beauty::BeautyState state, int first, int second) {
    L2_GUARD_FUNCTION();

    BeautyMap& beauty = m_mapBeauty[static_cast<std::size_t>(state)];
    const BeautyKey key(first, second);
    const auto position = beauty.find(key);
    if (position != beauty.end()) {
        beauty.erase(key);
    }
}

void CSetting::SendBeautyList(Beauty::BeautyState state, CPacketHelperEx<20480>& packet, int& count) {
    // Current L2Server 0x00a49840. Iteration order is the map's native
    // dual-key order and every visited row increments the caller's count.
    L2_GUARD_FUNCTION();
    const BeautyMap& beauty = m_mapBeauty[static_cast<std::size_t>(state)];
    for (const auto& entry : beauty) {
        const BeautyKey& key = entry.first;
        const StrAdenaCoin& value = entry.second;
        ++count;
        packet.Assemble(
            "ddQQQdd", key.m_PrimaryKey, key.m_SecondaryKey, value.Adena, value.ResetAdena, value.Coin, value.Limit,
            static_cast<int>(value.bEnable)
        );
    }
}

FVector CSetting::GetRandomStartPoint(const std::int64_t classBit) {
    // Current L2Server 0x00a482e4. A missing class-bit match deliberately
    // falls through to the first parsed start point without an empty check.
    L2_GUARD_FUNCTION();

    std::vector<int, std::allocator<int>> candidates;
    int startPointIndex = 0;
    while (startPointIndex < static_cast<int>(m_StartPoint.size())) {
        const ClassifiedPosition& startPoint = m_StartPoint.begin()[startPointIndex];
        if ((startPoint.m_Bit & classBit) != 0) {
            candidates.push_back(startPointIndex);
        }
        ++startPointIndex;
    }

    if (candidates.empty()) {
        return m_StartPoint.begin()->m_Point;
    }

    const int maximumIndex = static_cast<int>(candidates.size()) - 1;
    const int upper = maximumIndex < 0 ? 0 : maximumIndex;
    const int lower = maximumIndex < 0 ? maximumIndex : 0;
    const int candidateIndex =
        static_cast<int>(::genrand64_real2() * static_cast<double>(upper - lower + 1) + static_cast<double>(lower));
    return m_StartPoint.begin()[candidates[static_cast<std::size_t>(candidateIndex)]].m_Point;
}

FVector CSetting::GetRandomStartPoint(const ClassType classType) {
    // Current L2Server 0x00a484e8 performs no class-range check.
    L2_GUARD_FUNCTION();
    return GetRandomStartPoint(CT_to_CBT[static_cast<int>(classType)]);
}

namespace {

FVector RandomRestartPoint(const std::vector<FVector>& points) {
    const int maximumIndex = static_cast<int>(points.size()) - 1;
    const int upper = maximumIndex < 0 ? 0 : maximumIndex;
    const int lower = maximumIndex < 0 ? maximumIndex : 0;
    const int index =
        static_cast<int>(::genrand64_real2() * static_cast<double>(upper - lower + 1) + static_cast<double>(lower));
    return runtime::CheckedVectorAt(points, static_cast<std::size_t>(index));
}

} // namespace

FVector CSetting::RestartPointGroup::GetNormalRestartPoint() const { return RandomRestartPoint(m_PointsForNormal); }

FVector CSetting::RestartPointGroup::GetChaoRestartPoint() const {
    if (m_PointsForChao.empty()) {
        return GetNormalRestartPoint();
    }
    return RandomRestartPoint(m_PointsForChao);
}

CSetting::RestartPointGroup* CSetting::GetRestartPointGroup(RaceType race, FVector position) {
    L2_GUARD_FUNCTION();

    const std::size_t raceIndex = static_cast<std::size_t>(race);
    for (RestartTerritory* const territory : m_RestartTerritory) {
        if (territory != nullptr && territory->m_bRace[raceIndex] && territory->m_Territory.IsInside(position, true)) {
            RestartPointGroup* const group = territory->m_pRestartPointGroup[raceIndex];
            if (group != nullptr) {
                return group;
            }
        }
    }

    const std::uint32_t mapX =
        (static_cast<std::uint32_t>(runtime::Win64CvttDoubleToInt32(position.x)) + 0x50000U) >> 15U;
    const std::uint32_t mapY =
        (static_cast<std::uint32_t>(runtime::Win64CvttDoubleToInt32(position.y)) + 0x40000U) >> 15U;
    if (mapX < 20U && mapY < 17U) {
        RestartPointGroup* group = m_RestartPointGroupForMap[mapX][mapY];
        if (group != nullptr && group->m_bBanned[raceIndex]) {
            group = group->m_pBannedAlternative[raceIndex];
        }
        if (group != nullptr) {
            return group;
        }
    }

    int nearestIndex = -1;
    int currentIndex = 0;
    double nearestDistance = 0.0;
    for (RestartPointGroup* const group : m_RestartPointGroup) {
        if (group != nullptr && !group->m_bBanned[raceIndex]) {
            const FVector point = runtime::CheckedVectorAt(group->m_PointsForNormal, 0);
            const double dx = point.x - position.x;
            const double dy = point.y - position.y;
            const double distance = dx * dx + dy * dy;
            if (nearestIndex < 0 || distance < nearestDistance) {
                nearestIndex = currentIndex;
                nearestDistance = distance;
            }
        }
        ++currentIndex;
    }
    if (nearestIndex < 0) {
        // Log.Add(LOG_ERROR, L"cannot find restart point!")
        // L2_LOG_ERROR(Log, L"cannot find restart point!")
        L2_LOG_ERROR(
            Log, u"No eligible restart-point group for race %d at (%.17g, %.17g, %.17g); falling back to group index 0.",
            static_cast<int>(race), position.x, position.y, position.z
        );
        nearestIndex = 0;
    }
    return runtime::CheckedVectorAt(m_RestartPointGroup, static_cast<std::size_t>(nearestIndex));
}

FVector CSetting::GetRestartPoint(RaceType race, FVector position, bool chao, int pkCount) {
    L2_GUARD_FUNCTION();

    const FVector fieldCyclePoint = g_FieldCycleSystem.GetRestartPoint(position, chao);
    if (!fieldCyclePoint.IsZero()) {
        return fieldCyclePoint;
    }

    RestartPointGroup* const group = GetRestartPointGroup(race, position);
    if (group == nullptr) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%S][%d] restart group is null",
        //     "Setting.cpp",
        //     0x286)
        // L2_LOG_ERROR(Log,
        //     L"[%S][%d] restart group is null",
        //     "Setting.cpp",
        //     0x286)
        L2_LOG_ERROR(
            Log, u"Restart-point group is null for race %d at (%.17g, %.17g, %.17g); returning the zero position.",
            static_cast<int>(race), position.x, position.y, position.z
        );
        return FVector{};
    }

    GuardHelper selectRestartPoint(L"GetRestartPoint-1", CThreadManager::GetThreadID());
    if (!chao) {
        GuardHelper normalRestartPoint(L"GetRestartPoint-4", CThreadManager::GetThreadID());
        return group->GetNormalRestartPoint();
    }
    if (pkCount < 5 || !group->m_bNoviceZone || m_pIsolatedRestartPointGroup == nullptr) {
        GuardHelper chaoRestartPoint(L"GetRestartPoint-3", CThreadManager::GetThreadID());
        return group->GetChaoRestartPoint();
    }
    GuardHelper isolatedRestartPoint(L"GetRestartPoint-2", CThreadManager::GetThreadID());
    return m_pIsolatedRestartPointGroup->GetChaoRestartPoint();
}

int CSetting::GetRestartPointSys(RaceType race, FVector position, int type) {
    L2_GUARD_FUNCTION();

    const std::size_t raceIndex = static_cast<std::size_t>(race);

    for (RestartTerritory* territory : m_RestartTerritory) {
        if (territory == nullptr || !territory->m_bRace[raceIndex] || !territory->m_Territory.IsInside(position, true)) {
            continue;
        }

        RestartPointGroup* const group = territory->m_pRestartPointGroup[raceIndex];
        if (group == nullptr) {
            continue;
        }
        if (type == 1) {
            return group->m_nBBS;
        }
        if (type == 2) {
            return group->m_nLocName;
        }
    }

    const std::uint32_t mapX =
        (static_cast<std::uint32_t>(runtime::Win64CvttDoubleToInt32(position.x)) + 0x50000U) >> 15U;
    const std::uint32_t mapY =
        (static_cast<std::uint32_t>(runtime::Win64CvttDoubleToInt32(position.y)) + 0x40000U) >> 15U;
    if (mapX < 20U && mapY < 17U) {
        RestartPointGroup* group = m_RestartPointGroupForMap[mapX][mapY];
        if (group != nullptr && group->m_bBanned[raceIndex]) {
            group = group->m_pBannedAlternative[raceIndex];
        }
        if (group != nullptr) {
            if (type == 1) {
                return group->m_nBBS;
            }
            if (type == 2) {
                return group->m_nLocName;
            }
        }
    }

    if (m_RestartPointGroup.empty()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"respoint empty. racetype %d, pos (%d %d %d), type %d",
        //     static_cast<int>(race),
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z),
        //     type)
        // L2_LOG_ERROR(Log,
        //     L"respoint empty. racetype %d, pos (%d %d %d), type %d",
        //     static_cast<int>(race),
        //     runtime::Win64CvttDoubleToInt32(position.x),
        //     runtime::Win64CvttDoubleToInt32(position.y),
        //     runtime::Win64CvttDoubleToInt32(position.z),
        //     type)
        L2_LOG_ERROR(
            Log,
            u"No restart-point groups are loaded for system lookup type %d, race %d at (%.17g, %.17g, %.17g); returning 0.",
            type, static_cast<int>(race), position.x, position.y, position.z
        );
        return 0;
    }

    int nearestIndex = -1;
    int currentIndex = 0;
    double nearestDistance = 0.0;
    for (RestartPointGroup* group : m_RestartPointGroup) {
        if (group != nullptr && !group->m_bBanned[raceIndex]) {
            const FVector& point = runtime::CheckedVectorAt(group->m_PointsForNormal, 0);
            const double dx = point.x - position.x;
            const double dy = point.y - position.y;
            const double distance = dx * dx + dy * dy;
            if (nearestIndex < 0 || distance < nearestDistance) {
                nearestIndex = currentIndex;
                nearestDistance = distance;
            }
        }
        ++currentIndex;
    }

    if (nearestIndex < 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"cannot find restart point!")
        // L2_LOG_ERROR(Log,
        //     L"cannot find restart point!")
        L2_LOG_ERROR(
            Log,
            u"No eligible restart-point group for system lookup type %d, race %d at (%.17g, %.17g, %.17g); falling back to "
            u"group index 0.",
            type, static_cast<int>(race), position.x, position.y, position.z
        );
        nearestIndex = 0;
    }

    RestartPointGroup* const group = runtime::CheckedVectorAt(m_RestartPointGroup, static_cast<std::size_t>(nearestIndex));
    return type == 1 ? group->m_nBBS : group->m_nLocName;
}

void CSetting::LoadBeautyData() {
    L2_GUARD_FUNCTION();
    LoadHairData();
    LoadFaceData();
    LoadColorData();
}

bool CSetting::LoadColorData() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kColorDataPath = L"..\\Script\\colorexdata.txt";
#else
    // The shipped POSIX data tree keeps this exact casing.
    constexpr const wchar_t* kColorDataPath = L"../script/ColorExdata.txt";
#endif

    CSecureStream input(kColorDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from [%s] file failed",
        //     L"Setting.cpp",
        //     0x80,
        //     kColorDataPath)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from [%s] file failed",
        //     L"Setting.cpp",
        //     0x80,
        //     kColorDataPath)
        L2_LOG_ERROR(Log, u"Cannot read the color-data resource '%ls'; terminating the process.", kColorDataPath);
        ForceCrash(CrashInfo::PARSING_ERROR_COLOREXDATA_FILE);
        return false;
    }

    ParserForColorEx parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Current 0x00a48d30 contains this literal HairEx typo.
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] HairExData.create failed",
        //     L"Setting.cpp",
        //     0x88)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] HairExData.create failed",
        //     L"Setting.cpp",
        //     0x88)
        L2_LOG_ERROR(Log, u"Cannot initialize the color-data parser; terminating the process.");
        ForceCrash(CrashInfo::PARSING_ERROR_COLOREXDATA_FILE);
        return false;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing ColorExData file failed after line[%d]",
        //     L"Setting.cpp",
        //     0x92,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing ColorExData file failed after line[%d]",
        //     L"Setting.cpp",
        //     0x92,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(Log, u"Cannot parse color data at line %d; terminating the process.", parser.m_lexer.line);
        ForceCrash(CrashInfo::PARSING_ERROR_COLOREXDATA_FILE);
        return false;
    }
    return true;
}

bool CSetting::LoadHairData() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kHairDataPath = L"..\\Script\\hairexdata.txt";
#else
    constexpr const wchar_t* kHairDataPath = L"../script/hairexdata.txt";
#endif

    CSecureStream input(kHairDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from [%s] file failed",
        //     L"Setting.cpp",
        //     0xa3,
        //     kHairDataPath)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from [%s] file failed",
        //     L"Setting.cpp",
        //     0xa3,
        //     kHairDataPath)
        L2_LOG_ERROR(Log, u"Cannot read the hair-data resource '%ls'; terminating the process.", kHairDataPath);
        ForceCrash(CrashInfo::PARSING_ERROR_HAIREXDATA_FILE);
        return false;
    }

    ParserForHairEx parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] HairExData.create failed",
        //     L"Setting.cpp",
        //     0xab)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] HairExData.create failed",
        //     L"Setting.cpp",
        //     0xab)
        L2_LOG_ERROR(Log, u"Cannot initialize the hair-data parser; terminating the process.");
        ForceCrash(CrashInfo::PARSING_ERROR_HAIREXDATA_FILE);
        return false;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing HairExData file failed after line[%d]",
        //     L"Setting.cpp",
        //     0xb5,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing HairExData file failed after line[%d]",
        //     L"Setting.cpp",
        //     0xb5,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(Log, u"Cannot parse hair data at line %d; terminating the process.", parser.m_lexer.line);
        ForceCrash(CrashInfo::PARSING_ERROR_HAIREXDATA_FILE);
        return false;
    }
    return true;
}

bool CSetting::LoadFaceData() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kFaceDataPath = L"..\\Script\\faceexdata.txt";
#else
    constexpr const wchar_t* kFaceDataPath = L"../script/faceexdata.txt";
#endif

    CSecureStream input(kFaceDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from [%s] file failed",
        //     L"Setting.cpp",
        //     0xc6,
        //     kFaceDataPath)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from [%s] file failed",
        //     L"Setting.cpp",
        //     0xc6,
        //     kFaceDataPath)
        L2_LOG_ERROR(Log, u"Cannot read the face-data resource '%ls'; terminating the process.", kFaceDataPath);
        ForceCrash(CrashInfo::PARSING_ERROR_FACEEXDATA_FILE);
        return false;
    }

    ParserForFaceEx parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] FaceExData.create failed",
        //     L"Setting.cpp",
        //     0xce)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] FaceExData.create failed",
        //     L"Setting.cpp",
        //     0xce)
        L2_LOG_ERROR(Log, u"Cannot initialize the face-data parser; terminating the process.");
        ForceCrash(CrashInfo::PARSING_ERROR_FACEEXDATA_FILE);
        return false;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing FaceExData file failed after line[%d]",
        //     L"Setting.cpp",
        //     0xd8,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing FaceExData file failed after line[%d]",
        //     L"Setting.cpp",
        //     0xd8,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(Log, u"Cannot parse face data at line %d; terminating the process.", parser.m_lexer.line);
        ForceCrash(CrashInfo::PARSING_ERROR_FACEEXDATA_FILE);
        return false;
    }
    return true;
}

bool CSetting::LoadExpData() {
    L2_GUARD_FUNCTION();

#if defined(_WIN32)
    constexpr const wchar_t* kExpDataPath = L"..\\Script\\expdata.txt";
#else
    constexpr const wchar_t* kExpDataPath = L"../script/expdata.txt";
#endif

    CSecureStream input(kExpDataPath, true);
    if (input.fail()) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Read from [%s] file failed",
        //     L"Setting.cpp",
        //     0xea,
        //     kExpDataPath)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Read from [%s] file failed",
        //     L"Setting.cpp",
        //     0xea,
        //     kExpDataPath)
        L2_LOG_ERROR(Log, u"Cannot read the experience-data resource '%ls'; terminating the process.", kExpDataPath);
        ForceCrash(CrashInfo::PARSING_ERROR_EXPDATA_FILE);
        return false;
    }

    ParserForExp parser;
    if (!parser.Create(input.GetRawString() + 1, input.GetRawStringLen() - 1)) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] ExpData.create failed",
        //     L"Setting.cpp",
        //     0xf2)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] ExpData.create failed",
        //     L"Setting.cpp",
        //     0xf2)
        L2_LOG_ERROR(Log, u"Cannot initialize the experience-data parser; terminating the process.");
        ForceCrash(CrashInfo::PARSING_ERROR_EXPDATA_FILE);
        return false;
    }

    parser.m_lexer.yybegin(4);
    if (parser.yyparse() != 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     L"[%s][%d] Parsing ExpData file failed after line[%d]",
        //     L"Setting.cpp",
        //     0xfc,
        //     parser.m_lexer.line)
        // L2_LOG_ERROR(Log,
        //     L"[%s][%d] Parsing ExpData file failed after line[%d]",
        //     L"Setting.cpp",
        //     0xfc,
        //     parser.m_lexer.line)
        L2_LOG_ERROR(Log, u"Cannot parse experience data at line %d; terminating the process.", parser.m_lexer.line);
        ForceCrash(CrashInfo::PARSING_ERROR_EXPDATA_FILE);
        return false;
    }

    return true;
}
