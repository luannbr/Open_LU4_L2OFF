#include "Shared/src/Runtime/ResourceDirectories.h"
#include "Shared/src/Log.h"
#include "Shared/src/TextRuntime.h"

#include "CReadFile.h"

#include "DBConn.h"
#include "L2Core/src/inc/Guard.h"
#include "Shared/src/Exception.h"
#include "Shared/src/ScopedLockGuards.h"
#include "Portable/ProcessRuntime.h"
#include "Runtime.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

std::int64_t g_ExpTable[122]{};
CReadFile g_readFile;

namespace {

constexpr char kItemDataPath[] = "../Script/Itemdata.txt";
constexpr char kPortableItemDataPath[] = "../script/Itemdata.txt";
// Legacy diagnostic path. Live diagnostics report the resolved open attempts.
// constexpr char kTargetItemDataPath[] = "..\\Script\\Itemdata.txt"
constexpr char kExpDataPath[] = "../Script/expdata.txt";
constexpr char kPortableExpDataPath[] = "../script/expdata.txt";
// Legacy diagnostic path. Actual input may come from an overridden script directory.
// constexpr char kTargetExpDataPath[] = "..\\Script\\expdata.txt"
constexpr char kDBJobPath[] = "../Script/dbjob.txt";
constexpr char kPortableDBJobPath[] = "../script/dbjob.txt";
// Legacy diagnostic path. Live diagnostics use the resolved resource paths.
// constexpr char kTargetDBJobPath[] = "..\\Script\\dbjob.txt"

// Shared/SharedFactory.h owns the single definitions. CReadItemData.h uses
// their fixed-width opaque declarations so CacheD does not import the
// process-private L2NPC dependencies still pulled by that aggregate header.
constexpr ItemType kItemNotItem = static_cast<ItemType>(-1);
constexpr ItemType kItemWeapon = static_cast<ItemType>(0);
constexpr ItemType kItemArmor = static_cast<ItemType>(1);
constexpr ItemType kItemAccessary = static_cast<ItemType>(2);
constexpr ItemType kItemQuestItem = static_cast<ItemType>(3);
constexpr ItemType kItemAsset = static_cast<ItemType>(4);
constexpr ItemType kItemEtcItem = static_cast<ItemType>(5);

constexpr CrystalType kCrystalNone = static_cast<CrystalType>(0);
constexpr CrystalType kCrystalD = static_cast<CrystalType>(1);
constexpr CrystalType kCrystalC = static_cast<CrystalType>(2);
constexpr CrystalType kCrystalB = static_cast<CrystalType>(3);
constexpr CrystalType kCrystalA = static_cast<CrystalType>(4);
constexpr CrystalType kCrystalS = static_cast<CrystalType>(5);
constexpr CrystalType kCrystalS80 = static_cast<CrystalType>(6);
constexpr CrystalType kCrystalR = static_cast<CrystalType>(8);
constexpr CrystalType kCrystalR95 = static_cast<CrystalType>(9);
constexpr CrystalType kCrystalR99 = static_cast<CrystalType>(10);
constexpr CrystalType kCrystalEvent = static_cast<CrystalType>(11);
constexpr CrystalType kCrystalMax = static_cast<CrystalType>(12);
constexpr CrystalType kCrystalInvalid = static_cast<CrystalType>(-1);

std::wstring LowerAscii(std::wstring value) {
    for (wchar_t& character : value) {
        if (character >= L'A' && character <= L'Z') {
            character += L'a' - L'A';
        }
    }
    return value;
}

bool EqualsAsciiInsensitive(std::wstring_view left, std::wstring_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        wchar_t first = left[index];
        wchar_t second = right[index];
        if (first >= L'A' && first <= L'Z')
            first += L'a' - L'A';
        if (second >= L'A' && second <= L'Z')
            second += L'a' - L'A';
        if (first != second)
            return false;
    }
    return true;
}

CacheDServerType ParseServerType(std::wstring_view value) {
    if (EqualsAsciiInsensitive(value, L"dev"))
        return CacheDServerType::Dev;
    if (EqualsAsciiInsensitive(value, L"live"))
        return CacheDServerType::Live;
    if (EqualsAsciiInsensitive(value, L"test"))
        return CacheDServerType::Test;
    if (EqualsAsciiInsensitive(value, L"movie"))
        return CacheDServerType::Movie;
    if (EqualsAsciiInsensitive(value, L"broadcast")) {
        return CacheDServerType::Broadcast;
    }
    if (EqualsAsciiInsensitive(value, L"worldserver")) {
        return CacheDServerType::WorldServer;
    }
    // FUN_00450ba8 returns live for every unknown spelling.
    return CacheDServerType::Live;
}

int ParseCountry(std::wstring_view value) {
    struct CountryEntry {
        std::wstring_view name;
        int value;
    };
    // Exact 14-entry table at target VA 0x007246c0.
    static constexpr std::array<CountryEntry, 14> kCountries{{
        {L"k", 0},
        {L"e", 1},
        {L"j", 2},
        {L"tw", 3},
        {L"cn", 4},
        {L"th", 5},
        {L"ph", 6},
        {L"id", 7},
        {L"ru", 8},
        {L"eu", 9},
        {L"de", 10},
        {L"fr", 11},
        {L"pl", 12},
        {L"tr", 13},
    }};
    for (const CountryEntry& entry : kCountries) {
        if (EqualsAsciiInsensitive(value, entry.name))
            return entry.value;
    }
    return -1;
}

bool ParseInteger(std::wstring_view text, int& result) {
    if (text.empty()) {
        result = 0;
        return true;
    }
    std::wstring copy(text);
    wchar_t* end = nullptr;
    errno = 0;
    const long parsed = std::wcstol(copy.c_str(), &end, 10);
    if (end == copy.c_str()) {
        // _wtol, used by the target helper, maps a non-number to zero.
        result = 0;
        return true;
    }
    if (errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX)
        return false;
    result = static_cast<int>(parsed);
    return true;
}

bool ParseIntegerSet(std::wstring_view value, std::set<int>& output) {
    if (value.size() < 2 || value.front() != L'{' || value.back() != L'}') {
        return false;
    }
    value.remove_prefix(1);
    value.remove_suffix(1);
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(L';', start);
        const std::wstring_view token =
            value.substr(start, end == std::wstring_view::npos ? std::wstring_view::npos : end - start);
        if (!token.empty()) {
            int parsed = 0;
            if (!ParseInteger(token, parsed))
                return false;
            output.insert(parsed);
        }
        if (end == std::wstring_view::npos)
            break;
        start = end + 1;
    }
    return true;
}

bool Unquote(std::wstring_view value, std::wstring_view& result) {
    if (value.size() < 2 || value.front() != L'"' || value.back() != L'"') {
        return false;
    }
    result = value.substr(1, value.size() - 2);
    return true;
}

bool ParseTime32(std::wstring_view value, std::int32_t& result) {
    std::wstring copy(value);
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    unsigned hour = 0;
    unsigned minute = 0;
    unsigned second = 0;
    if (std::swscanf(copy.c_str(), L"%d-%u-%uT%u:%u:%u", &year, &month, &day, &hour, &minute, &second) != 6) {
        return false;
    }

    // Exact explicit bounds from TimePeriodInfo::ParseTime at 0x00450c50.
    // Month/day zero pass those checks and are normalized by mktime.
    if (year < 1900 || year > 3950 || month > 12 || day > 31 || hour > 23 || minute > 59 || second > 59) {
        return false;
    }

    std::tm local{};
    local.tm_year = year - 1900;
    local.tm_mon = static_cast<int>(month) - 1;
    local.tm_mday = static_cast<int>(day);
    local.tm_hour = static_cast<int>(hour);
    local.tm_min = static_cast<int>(minute);
    local.tm_sec = static_cast<int>(second);
    local.tm_isdst = -1;
    const std::time_t converted = std::mktime(&local);
    if (converted < 0 || static_cast<std::uintmax_t>(converted) > static_cast<std::uintmax_t>(INT32_MAX)) {
        return false;
    }
    result = static_cast<std::int32_t>(converted);
    return true;
}

// Read a UTF-16LE data file into a wide stream. The BOM stays in the text,
// and the callers skip it as before. std::codecvt_utf16 did the same
// decoding but is deprecated since C++17.
bool OpenUtf16LE(std::wistringstream& input, const char* primaryPath, const char* portableCaseFallback) {
    std::ifstream file(runtime::ResolveResourcePath(primaryPath), std::ios::binary);
#if !defined(_WIN32)
    if (!file && portableCaseFallback != nullptr) {
        file.clear();
        file.open(runtime::ResolveResourcePath(portableCaseFallback), std::ios::binary);
    }
#else
#endif
    if (!file)
        return false;

    const std::string bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    std::u16string text(bytes.size() / 2, u'\0');
    for (std::size_t index = 0; index < text.size(); ++index) {
        text[index] = target_utf16::LoadLittleEndian(bytes.data() + index * 2);
    }
    input.str(Utf16ToWide(text, Utf16WidePolicy::PreserveInvalidOnWindows));
    return true;
}

bool EqualsTargetWideInsensitive(const char16_t* left, std::u16string_view right) {
    if (left == nullptr)
        return false;
    std::size_t index = 0;
    for (; index < right.size() && left[index] != u'\0'; ++index) {
        char16_t first = left[index];
        char16_t second = right[index];
        if (first >= u'A' && first <= u'Z')
            first += u'a' - u'A';
        if (second >= u'A' && second <= u'Z')
            second += u'a' - u'A';
        if (first != second)
            return false;
    }
    return index == right.size() && left[index] == u'\0';
}

int ParseWtol(std::wstring_view value) {
    int result = 0;
    ParseInteger(value, result);
    return result;
}

std::int64_t ParseWtoi64(std::wstring_view value) {
    if (value.empty())
        return 0;
    const std::wstring copy(value);
    wchar_t* end = nullptr;
    errno = 0;
    const long long parsed = std::wcstoll(copy.c_str(), &end, 10);
    if (end == copy.c_str())
        return 0;
    if (errno == ERANGE) {
        return parsed < 0 ? INT64_MIN : INT64_MAX;
    }
    return static_cast<std::int64_t>(parsed);
}

// void LogExpDataParseError() {
void LogExpDataParseError(const char* reason) {
    // Log.Add(LOG_ERROR, "Parsing error in file %s", kTargetExpDataPath)
    L2_LOG_ERROR(Log, "[experience data] %s", reason);
    // Noise: the second generic parse-error message duplicates the contextual diagnostic above.
    // Log.Add(LOG_ERROR, "CReadFile::ParseExpData() Error %s", kTargetExpDataPath)
}

[[noreturn]] void CrashForReadFileParseError(
    CrashInfo::ReasonType reason, const std::source_location location = std::source_location::current()
) {
    // Current bodies publish the crash location and reason immediately before
    // the intentional null write at 0x004c95a0.
    ForceCrash(reason, location);
    std::abort();
}

// Original name is absent from the current symbols. Cached.exe 0x0052636c
// fixes this exact five-parameter binding order and SQL types.
bool InsertItemData(
    DBConn& connection,
    const int& itemType,
    const int& canMove,
    const bool& isStackable,
    const bool& isPeriod,
    const bool& isQuestItem
) {
    constexpr const wchar_t* statement = L"{Call lin_InsertItemData (?, ?, ?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&itemType);
    connection.BindParameter(&canMove);
    connection.BindParameter(&isStackable);
    connection.BindParameter(&isPeriod);
    connection.BindParameter(&isQuestItem);
    return connection.ExecutePrepared(statement);
}

// Portability improvement, not original CacheD behavior: report the actual
// database decision. Migration 200 keeps lin_ExecuteDBJob's void ABI for
// existing callers and exposes the same atomic operation with a result here.
enum class DBJobOutcome { Executed, AlreadyExecuted, Failed, Unknown };

DBJobOutcome ExecuteDBJobCall(DBConn& connection, int bookingTime, int jobType, int jobValue) {
    constexpr const wchar_t* statement = L"{Call cached_portable_execute_dbjob(?,?,?)}";
    connection.UnBind();
    if (!connection.Prepare(statement)) {
        return DBJobOutcome::Failed;
    }
    int result = -1;
    connection.Bind(&result);
    connection.BindParameter(&bookingTime);
    connection.BindParameter(&jobType);
    connection.BindParameter(&jobValue);
    const bool executed = connection.ExecutePrepared(statement);
    const bool fetched = executed && connection.Fetch();
    // UnBind also clears the output pointer before its local value expires.
    connection.UnBind();
    if (!executed) return DBJobOutcome::Failed;
    if (!fetched) return DBJobOutcome::Unknown;
    if (result == 1) return DBJobOutcome::Executed;
    if (result == 0) return DBJobOutcome::AlreadyExecuted;
    return DBJobOutcome::Unknown;
}

const char* DBJobServerTypeName(CacheDServerType type) {
    switch (type) {
        case CacheDServerType::Dev: return "dev";
        case CacheDServerType::Live: return "live";
        case CacheDServerType::Test: return "test";
        case CacheDServerType::Movie: return "movie";
        case CacheDServerType::Broadcast: return "broadcast";
        case CacheDServerType::WorldServer: return "worldserver";
    }
    return "unknown";
}

std::string DBJobScheduledTime(int bookingTime) {
    const std::time_t time = bookingTime;
    std::tm utc{};
#if defined(_WIN32)
    const bool valid = ::gmtime_s(&utc, &time) == 0;
#else
    const bool valid = ::gmtime_r(&time, &utc) != nullptr;
#endif
    char formatted[32]{};
    if (valid && std::strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S UTC", &utc)) {
        return formatted;
    }
    return "unrepresentable time (Unix " + std::to_string(bookingTime) + ")";
}

void LogDBJobOutcome(const char* serverType, const std::string& scheduled,
                     const char* operation, const char* valuesLabel,
                     DBJobOutcome outcome, const std::vector<int>& values) {
    const char* status = "outcome unknown";
    switch (outcome) {
        case DBJobOutcome::Executed: status = "executed"; break;
        case DBJobOutcome::AlreadyExecuted: status = "already executed"; break;
        case DBJobOutcome::Failed: status = "failed"; break;
        case DBJobOutcome::Unknown: break;
    }
    // Bound each line without dropping IDs from large event-cleanup lists.
    constexpr std::size_t valuesPerLine = 32;
    for (std::size_t begin = 0; begin < values.size(); begin += valuesPerLine) {
        const std::size_t end = std::min(begin + valuesPerLine, values.size());
        std::string list;
        for (std::size_t index = begin; index < end; ++index) {
            if (!list.empty()) list += ", ";
            list += std::to_string(values[index]);
        }
        // Original: L2_LOG(Log, outcome == DBJobOutcome::Failed || outcome == DBJobOutcome::Unknown ? LOG_ERROR : LOG_NORMAL,
        //     "[database jobs] dbjob.txt. Server=%s. Scheduled=%s. %s: %s. %s (%zu-%zu of %zu)=[%s].",
        //     serverType, scheduled.c_str(), operation, status, valuesLabel,
        //     begin + 1, end, values.size(), list.c_str())
        L2_LOG(Log, outcome == DBJobOutcome::Failed || outcome == DBJobOutcome::Unknown ? LOG_ERROR : LOG_NORMAL,
            "[database jobs] %s: %s. Scheduled for %s on %s server (dbjob.txt). %s %zu-%zu of %zu: %s.",
            operation, status, scheduled.c_str(), serverType, valuesLabel,
            begin + 1, end, values.size(), list.c_str());
    }
}

} // namespace

CReadFile::CReadFile() {
    L2_GUARD_FUNCTION();
    // Current 0x005282ac..0x005282b9. The old C1 source independently
    // confirms the raw CRITICAL_SECTION member and the 4000 spin count.
    l2portable_detail::InitializeCriticalSectionAndSpinCount(m_lock, 4000);
}

CReadFile::~CReadFile() {
    L2_GUARD_FUNCTION();
    {
        L2_GUARD(m_lock);
        for (const Items::value_type& entry : m_items) {
            delete entry.second;
        }
        m_items.clear();
    }
    l2portable_detail::DeleteCriticalSection(m_lock);
}

const CReadItemData* CReadFile::FindItem(int itemType) const {
    const Items::const_iterator found = m_items.find(itemType);
    return found == m_items.end() ? nullptr : found->second;
}

std::int64_t CReadFile::GetDefaultPrice(int itemType) const {
    const CReadItemData* item = FindItem(itemType);
    return item == nullptr ? 0 : item->m_defaultPrice;
}

bool CReadFile::IsPremiumItem(int itemType) const { return m_premiumItems.find(itemType) != m_premiumItems.end(); }

bool CReadFile::IsPetCollar(int itemType) const { return m_petCollars.find(itemType) != m_petCollars.end(); }

int CReadFile::GetPeriodItemPeriod(int itemType) const {
    L2_GUARD_FUNCTION();
    const auto found = m_periodItems.find(itemType);
    return found == m_periodItems.end() ? 0 : found->second * 60;
}

int CReadFile::GetDurationItemDuration(int itemType) const {
    L2_GUARD_FUNCTION();
    const auto found = m_durationItems.find(itemType);
    return found == m_durationItems.end() ? -1 : found->second;
}

int CReadFile::GetConsumeType(int itemType) {
    // Current Cached.exe 0x00524ca0, 349 bytes, SHA-256
    // 290c1cc62befe1b37b539c4ad3aa6852387a64c01b116a360c9b98ee79f0eeed.
    L2_GUARD_FUNCTION();
    const CReadItemData* item = FindItem(itemType);
    if (item == nullptr)
        return 0;
    if (EqualsTargetWideInsensitive(item->m_consumeType, u"consume_type_normal")) {
        return 1;
    }
    if (EqualsTargetWideInsensitive(item->m_consumeType, u"consume_type_asset")) {
        return 2;
    }
    if (EqualsTargetWideInsensitive(item->m_consumeType, u"consume_type_charge")) {
        return 3;
    }
    if (EqualsTargetWideInsensitive(item->m_consumeType, u"consume_type_stackable")) {
        return 4;
    }
    return 0;
}

ItemConsumeType CReadFile::GetItemConsumeType(int itemType) {
    // Current Cached.exe 0x00524e00, 346 bytes, SHA-256
    // e15a78491f33c6741a6aa34b03987f936ae23cbe578c14d0dbe89749b1b15cee.
    // The shipping body has the same C++ identity as 0x00524ca0 but ignores
    // its receiver and always reads g_readFile. The semantic method name is
    // required only to represent both ODR-conflicting bodies in one program.
    L2_GUARD_FUNCTION();
    const CReadItemData* item = g_readFile.FindItem(itemType);
    if (item == nullptr)
        return ICT_UNKNOWN;
    if (EqualsTargetWideInsensitive(item->m_consumeType, u"consume_type_normal")) {
        return ICT_NORMAL;
    }
    if (EqualsTargetWideInsensitive(item->m_consumeType, u"consume_type_asset")) {
        return ICT_ASSET;
    }
    if (EqualsTargetWideInsensitive(item->m_consumeType, u"consume_type_charge")) {
        return ICT_CHARGE;
    }
    if (EqualsTargetWideInsensitive(item->m_consumeType, u"consume_type_stackable")) {
        return ICT_STACKABLE;
    }
    return ICT_NORMAL;
}

CrystalType CReadFile::GetCrystalType(const wchar_t* value) {
    L2_GUARD_FUNCTION();
    if (std::wcscmp(value, L"a") == 0)
        return kCrystalA;
    if (std::wcscmp(value, L"b") == 0)
        return kCrystalB;
    if (std::wcscmp(value, L"c") == 0)
        return kCrystalC;
    if (std::wcscmp(value, L"d") == 0)
        return kCrystalD;
    if (std::wcscmp(value, L"s") == 0)
        return kCrystalS;
    if (std::wcscmp(value, L"s80") == 0)
        return kCrystalS80;
    if (std::wcscmp(value, L"none") == 0)
        return kCrystalNone;
    if (std::wcscmp(value, L"crystal_free") == 0)
        return kCrystalMax;
    if (std::wcscmp(value, L"event") == 0)
        return kCrystalEvent;
    if (std::wcscmp(value, L"r") == 0)
        return kCrystalR;
    if (std::wcscmp(value, L"r95") == 0)
        return kCrystalR95;
    if (std::wcscmp(value, L"r99") == 0)
        return kCrystalR99;
    return kCrystalInvalid;
}

bool CReadFile::ParseItemData(std::wistream& input) {
    L2_GUARD_FUNCTION();
    std::optional<std::wstring> tokenStorage;
    {
        L2_GUARD(m_lock);

        auto& rawToken = tokenStorage.emplace();
        while (input >> rawToken) {
            if (LowerAscii(rawToken) != L"item_begin")
                continue;

            int tokenIndex = 0;
            int itemId = 0;
            int defaultPrice = 0;
            std::wstring itemName;
            std::wstring consumeType;
            int canMove = 0;
            bool isPremium = false;
            bool isPeriod = false;
            int period = 0;
            bool isDuration = false;
            int duration = -1;
            bool isPetCollar = false;
            bool isQuestItem = false;
            ItemType type = kItemNotItem;
            CrystalType crystalType = kCrystalInvalid;
            bool ended = false; // Diagnostic only. Incomplete records are still processed.

            while (input >> rawToken) {
                const std::wstring lowered = LowerAscii(rawToken);
                if (lowered == L"item_end") {
                    ended = true;
                    // The shipping parser performs two unformatted reads here.
                    // Itemdata.txt uses CRLF and those calls consume it.
                    input.get();
                    input.get();
                    break;
                }

                ++tokenIndex;
                if (tokenIndex == 1) {
                    if (rawToken == L"weapon")
                        type = kItemWeapon;
                    else if (rawToken == L"armor")
                        type = kItemArmor;
                    else if (rawToken == L"accessary")
                        type = kItemAccessary;
                    else if (rawToken == L"questitem")
                        type = kItemQuestItem;
                    else if (rawToken == L"asset")
                        type = kItemAsset;
                    else if (rawToken == L"etcitem")
                        type = kItemEtcItem;
                } else if (tokenIndex == 2) {
                    itemId = ParseWtol(rawToken);
                    continue;
                } else if (tokenIndex == 3) {
                    itemName = rawToken;
                    continue;
                }

                const std::size_t equals = rawToken.find(L'=');
                if (equals == std::wstring::npos)
                    continue;
                const std::wstring key = LowerAscii(rawToken.substr(0, equals));
                const std::wstring_view value(rawToken.data() + equals + 1, rawToken.size() - equals - 1);

                if (key == L"default_price") {
                    defaultPrice = ParseWtol(value);
                }
                if (key == L"consume_type") {
                    consumeType.assign(value);
                }
                if (key == L"can_move") {
                    canMove = ParseWtol(value);
                    if (canMove < 0 || canMove > 2)
                        canMove = 0;
                }
                if (key == L"is_premium") {
                    isPremium = ParseWtol(value) == 1;
                }
                if (key == L"period") {
                    period = ParseWtol(value);
                    isPeriod = period != 0;
                }
                if (key == L"duration") {
                    duration = ParseWtol(value);
                    isDuration = duration > 0;
                }
                if (key == L"etcitem_type") {
                    isPetCollar = value == L"pet_collar";
                }
                if (key == L"item_type") {
                    isQuestItem = value == L"questitem";
                }
                if (key == L"crystal_type") {
                    const std::wstring copy(value);
                    crystalType = GetCrystalType(copy.c_str());
                }
            }

            if (!ended) {
                L2_LOG_WARN(Log,
                    "[item data] Input ended before item_end for parsed item ID %d. The incomplete record will still be processed using the values read so far.",
                    itemId);
            }
            CReadItemData* item = new CReadItemData(
                itemId, itemName.data(), defaultPrice, consumeType.data(), canMove, isPremium, isPeriod, isDuration, isQuestItem,
                type, crystalType
            );
            // std::map::insert is exact here: the shipping parser retains the
            // first duplicate ID and does not replace its pointer.
            m_items.insert(Items::value_type(itemId, item));
            if (isPremium)
                m_premiumItems.insert(itemId);
            if (isPeriod)
                m_periodItems.insert({itemId, period});
            if (isDuration)
                m_durationItems.insert({itemId, duration});
            if (isPetCollar)
                m_petCollars.insert(itemId);
        }
    }
    if (input.bad()) {
        L2_LOG_ERROR(Log,
            "[item data] Input stream reported a read error. Items already processed remain in memory; the parser still returns success.");
    }
    return true;
}

bool CReadFile::ParseItemData() {
    L2_GUARD_FUNCTION();
    std::wistringstream input;
    if (!OpenUtf16LE(input, kItemDataPath, kPortableItemDataPath)) {
        // Log.Add(LOG_ERROR, "CReadFile::ParseItemData() Error %s", kTargetItemDataPath)
#if defined(_WIN32)
        L2_LOG_ERROR(Log, "[item data] Cannot open '%s'. Item-data loading stopped.",
            runtime::ResolveResourcePath(kItemDataPath).string().c_str());
#else
        L2_LOG_ERROR(Log, "[item data] Cannot open '%s' or fallback '%s'. Item-data loading stopped.",
            runtime::ResolveResourcePath(kItemDataPath).string().c_str(),
            runtime::ResolveResourcePath(kPortableItemDataPath).string().c_str());
#endif
        return false;
    }
    // Current 0x00526efc consumes the UTF-16 BOM before delegating to the
    // stream overload.
    input.get();
    return ParseItemData(input);
}

bool CReadFile::ParseExpData(std::wistream& input) {
    L2_GUARD_FUNCTION();
    std::optional<std::wstring> tokenStorage;
    std::optional<std::wstring> levelStorage;
    std::optional<std::wstring> expStorage;
    bool reportedInputFailure = false; // Diagnostic only: avoid repeating a block-level read error.
    const bool parsed = [&]() {
        L2_GUARD(m_lock);

        auto& token = tokenStorage.emplace();
        for (;;) {
            bool foundTable = false;
            while (input >> token) {
                if (LowerAscii(token) == L"exp_table_begin") {
                    foundTable = true;
                    break;
                }
            }
            if (!foundTable) {
                if (input.bad() && !reportedInputFailure) {
                    L2_LOG_ERROR(Log,
                        "[experience data] Input stream reported a read error while searching for exp_table_begin. Existing table values remain in memory; the parser still returns success.");
                }
                return true;
            }

            int maximumLevel = 0;
            for (;;) {
                // Original: if (!(input >> token)) break.
                if (!(input >> token)) {
                    // Original: L2_LOG_WARN(Log,
                    //     "[experience data] Input ended before exp_table_end. Values already loaded remain in memory, the parser still returns success.")
                    L2_LOG(Log, input.bad() ? LOG_ERROR : LOG_WARN,
                        "[experience data] %s before exp_table_end. Values already loaded remain in memory; the parser still returns success.",
                        input.bad() ? "Input stream reported a read error" : "Input ended");
                    reportedInputFailure = true;
                    break;
                }
                if (token == L"exp_table_end") {
                    // Log.Add(LOG_NORMAL, "[ExpTableInfo] Max level [%d]", maximumLevel)
                    L2_LOG_INFO(Log,
                        "[experience data] Reached exp_table_end (current level value %d).", maximumLevel);
                    break;
                }

                const auto& levelToken = levelStorage.emplace(LowerAscii(token));
                if (levelToken.find(L"level=") == std::wstring::npos) {
                    // LogExpDataParseError()
                    LogExpDataParseError("Expected a token containing 'level='. Aborting through the experience-data parse-error crash handler.");
                    CrashForReadFileParseError(CrashInfo::PARSING_ERROR_EXPDATA_FILE);
                }
                // The shipping body checks find("level=") but parses from the
                // fixed six-character offset, instead of from the found offset.
                maximumLevel = ParseWtol(std::wstring_view(levelToken).substr(std::min<std::size_t>(6, levelToken.size())));

                if (!(input >> token)) {
                    // Original: L2_LOG_WARN(Log,
                    //     "[experience data] Missing experience value after level %d. This row is not applied, earlier values remain in memory and the parser still returns success.", maximumLevel)
                    L2_LOG(Log, input.bad() ? LOG_ERROR : LOG_WARN,
                        "[experience data] Cannot read experience value after level %d: %s. This row is not applied; earlier values remain in memory and the parser still returns success.",
                        maximumLevel, input.bad() ? "input stream read error" : "input ended");
                    reportedInputFailure = true;
                    levelStorage.reset();
                    break;
                }
                const auto& expToken = expStorage.emplace(LowerAscii(token));
                if (expToken.find(L"exp=") == std::wstring::npos) {
                    // LogExpDataParseError()
                    LogExpDataParseError("Expected a token containing 'exp='. Aborting through the experience-data parse-error crash handler.");
                    CrashForReadFileParseError(CrashInfo::PARSING_ERROR_EXPDATA_FILE);
                }
                const std::int64_t experience =
                    ParseWtoi64(std::wstring_view(expToken).substr(std::min<std::size_t>(4, expToken.size())));

                // Current valid data spans exactly 0..121. The target performs
                // an unchecked BSS write. Reproducing an adjacent-object overwrite
                // on malformed portable input would not be a meaningful ABI.
                if (maximumLevel < 0 || maximumLevel >= 122) {
                    return false;
                }
                g_ExpTable[maximumLevel] = experience;

                // 0x00525e00 extracts and discards the following training_rate
                // token before reading the next level row.
                input >> token;
                expStorage.reset();
                levelStorage.reset();
            }
        }
    }();
    if (!parsed) {
        // LogExpDataParseError()
        LogExpDataParseError("A parsed level is outside 0..121. Loading stopped; earlier table entries remain in memory.");
    }
    return parsed;
}

bool CReadFile::ParseExpData() {
    L2_GUARD_FUNCTION();
    std::wistringstream input;
    if (!OpenUtf16LE(input, kExpDataPath, kPortableExpDataPath)) {
        // Current 0x005270d5..0x00527152 records reason 30 and deliberately
        // faults. The apparent delegate after 0x00527157 is unreachable in
        // a normal execution. The decompiler had treated CrashImpl as if it
        // could return.
        // LogExpDataParseError()
#if defined(_WIN32)
        L2_LOG_ERROR(Log, "[experience data] Cannot open '%s'. Aborting through the experience-data parse-error crash handler.",
            runtime::ResolveResourcePath(kExpDataPath).string().c_str());
#else
        L2_LOG_ERROR(Log,
            "[experience data] Cannot open '%s' or fallback '%s'. Aborting through the experience-data parse-error crash handler.",
            runtime::ResolveResourcePath(kExpDataPath).string().c_str(),
            runtime::ResolveResourcePath(kPortableExpDataPath).string().c_str());
#endif
        CrashForReadFileParseError(CrashInfo::PARSING_ERROR_EXPDATA_FILE);
    }
    input.get();
    return ParseExpData(input);
}

void CReadFile::MakeItemDataTable() {
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_ERROR, "[NO_ERROR] Creating ItemData")
    // Log.Add(LOG_NORMAL, "component=item_data event=table_refresh_started items=%d", GetItemCount())
    // Log.Add(LOG_NORMAL, "[item data] Refreshing the database table from %d loaded items.", GetItemCount())
    L2_LOG_INFO(Log, "[item data] Refreshing the database table from %d loaded items.", GetItemCount());

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* initialize = L"{Call lin_InitItemData}";
    connection.Prepare(initialize);
    if (!connection.ExecutePrepared(initialize)) {
        // Log.Add(LOG_ERROR, "Fail to initiate ItemData")
        // Log.Add(LOG_ERROR, "component=item_data event=table_init_failed procedure=lin_InitItemData, refresh aborted")
        // Log.Add(LOG_ERROR, "[item data] lin_InitItemData failed. Table refresh aborted.")
        L2_LOG_ERROR(Log, "[item data] lin_InitItemData failed. Table refresh aborted.");
        return;
    }

    // Original: unsigned int failedItems = 0.
    std::size_t failedItems = 0;
    std::size_t attemptedItems = 0;
    for (const Items::value_type& entry : m_items) {
        connection.ResetParam();
        const CReadItemData& item = *entry.second;
        const bool isStackable = EqualsTargetWideInsensitive(item.m_consumeType, u"consume_type_asset") ||
            EqualsTargetWideInsensitive(item.m_consumeType, u"consume_type_stackable");
        ++attemptedItems;
        if (!InsertItemData(connection, item.m_itemType, item.m_canMove, isStackable, item.m_isPeriod, item.m_isQuestItem)) {
            ++failedItems;
            // Original: const std::string itemName = TargetWideToUtf8(item.m_itemName)
            // Log.Add(
            //     LOG_ERROR, "Fail to insert ItemData for L2Added Service[%d][%s]", item.m_itemType, itemName.c_str()
            // )
            // Log.Add(LOG_ERROR, "component=item_data event=insert_failed item_type=%d item_name=\"%s\"", item.m_itemType, itemName.c_str())
            // Log.Add(LOG_ERROR, "[item data] Could not insert item type %d (\"%s\"). Continuing with the remaining items.", item.m_itemType, itemName.c_str())
            // Original: L2_LOG_ERROR(Log, "[item data] Could not insert item type %d (\"%s\"). Continuing with the remaining items.", item.m_itemType, itemName.c_str())
            // Noise: one caller-level example plus the final failure count avoids
            // repeating the same refresh context for every failed database write.
            if (failedItems == 1) {
                const std::string itemName = Utf16ToUtf8(item.m_itemName);
                L2_LOG_ERROR(Log,
                    "[item data] First failed insert: item type %d (\"%s\"). Refresh continues; total failures follow in the summary.",
                    item.m_itemType, itemName.c_str());
            }
        }
    }
    // Log.Add(LOG_ERROR, "[NO_ERROR] ItemData is made")
    // Log.Add(failedItems ? LOG_WARN : LOG_NORMAL, "component=item_data event=table_refresh_finished attempted=%d failed=%u", GetItemCount(), failedItems)
    // Log.Add(failedItems ? LOG_WARN : LOG_NORMAL, "[item data] Table refresh finished: %d items attempted, %u failed.", GetItemCount(), failedItems)
    // Original: L2_LOG(Log, failedItems ? LOG_WARN : LOG_NORMAL, "[item data] Table refresh finished: %d items attempted, %u failed.", GetItemCount(), failedItems)
    L2_LOG(Log, failedItems ? LOG_WARN : LOG_NORMAL,
        "[item data] Table refresh finished: %zu insert attempts, %zu succeeded, %zu failed.",
        attemptedItems, attemptedItems - failedItems, failedItems);
}

bool CReadFile::ParseDBJobData(std::wistream& input) {
    std::optional<std::wstring> rawStorage;
    std::optional<std::wstring> tokenStorage;
    std::optional<DBJobEntry> entryStorage;
    {
        L2_GUARD(m_lock);

        auto& rawToken = rawStorage.emplace();
        while (input >> rawToken) {
            const auto& token = tokenStorage.emplace(LowerAscii(rawToken));
            // Comment lines are not special in the target tokenizer. A commented
            // marker becomes //dbjob_begin and therefore cannot open a record.
            if (token != L"dbjob_begin") {
                tokenStorage.reset();
                continue;
            }

            auto& entry = entryStorage.emplace();
            int country = 0;
            bool invalid = false;
            bool ended = false;
            std::wstring invalidField; // Diagnostic only: keep the first rejected assignment key.

            while (input >> rawToken) {
                const std::wstring lowered = LowerAscii(rawToken);
                if (lowered == L"dbjob_end") {
                    ended = true;
                    break;
                }

                if (invalid)
                    continue;
                const std::size_t equals = rawToken.find(L'=');
                // 0x00527288 ignores non-assignment tokens inside a record.
                if (equals == std::wstring::npos)
                    continue;
                const std::wstring key = LowerAscii(rawToken.substr(0, equals));
                const std::wstring_view value(rawToken.data() + equals + 1, rawToken.size() - equals - 1);

                if (key == L"period") {
                    std::wstring_view unquoted;
                    if (!Unquote(value, unquoted) || !ParseTime32(unquoted, entry.bookingTime)) {
                        invalid = true;
                    }
                } else if (key == L"servertype") {
                    entry.serverType = ParseServerType(value);
                } else if (key == L"item_list") {
                    if (!ParseIntegerSet(value, entry.itemList))
                        invalid = true;
                } else if (key == L"buff_list") {
                    if (!ParseIntegerSet(value, entry.buffList))
                        invalid = true;
                } else if (key == L"quest_list") {
                    if (!ParseIntegerSet(value, entry.questList))
                        invalid = true;
                } else if (key == L"delete_ui_date") {
                    std::wstring_view unquoted;
                    if (!Unquote(value, unquoted) || !ParseInteger(unquoted, entry.deleteUiDateValue)) {
                        invalid = true;
                    } else {
                        entry.deleteUiDate = true;
                    }
                } else if (key == L"country") {
                    country = ParseCountry(value);
                } else {
                    invalid = true;
                }
                if (invalid)
                    invalidField = key;
            }

            if (invalid) {
                // Log.Add(LOG_ERROR, "CReadFile::ParseDBJobData() Error! Invalid Script[Line : %d]", 1)
                L2_LOG_ERROR(
                    Log,
                    u"[DBJob] Invalid assignment for field '%ls': unknown field or malformed value. "
                    u"Aborting through the DBJob parse-error crash handler; this parser does not track line numbers.",
                    invalidField.c_str()
                );
                CrashForReadFileParseError(CrashInfo::PARSING_ERROR_DBJOB_FILE);
            }
            // At EOF the target silently discards an unterminated, otherwise-valid
            // record and returns success.
            if (!ended) {
                // Original: L2_LOG_WARN(Log,
                //     "[DBJob] Input ended before dbjob_end. The unfinished job is discarded, earlier jobs remain loaded and the parser returns success.")
                L2_LOG(Log, input.bad() ? LOG_ERROR : LOG_WARN,
                    "[DBJob] %s before dbjob_end. The unfinished job is discarded; earlier jobs remain loaded and the parser returns success.",
                    input.bad() ? "Input stream reported a read error" : "Input ended");
                return true;
            }
            if (country == GetCacheDConfiguration().country) {
                // Current 0x00527a48..0x00527ac1 copy-constructs all three xset
                // owners into the new 0x80-byte xlist node.
                m_dbJobs.push_back(entry);
            }
            entryStorage.reset();
            tokenStorage.reset();
        }
    }
    if (input.bad()) {
        L2_LOG_ERROR(Log,
            "[DBJob] Input stream reported a read error between job records. Earlier jobs remain loaded; the parser still returns success.");
    }
    return true;
}

bool CReadFile::ParseDBJobData() {
    L2_GUARD_FUNCTION();
    std::wistringstream input;
    if (!OpenUtf16LE(input, kDBJobPath, kPortableDBJobPath)) {
        // Log.Add(LOG_ERROR, "CReadFile::ParseDBJobData() Error %s", kTargetDBJobPath)
#if defined(_WIN32)
        L2_LOG_ERROR(Log, "[DBJob] Cannot open '%s'. DBJob loading stopped.",
            runtime::ResolveResourcePath(kDBJobPath).string().c_str());
#else
        L2_LOG_ERROR(Log, "[DBJob] Cannot open '%s' or fallback '%s'. DBJob loading stopped.",
            runtime::ResolveResourcePath(kDBJobPath).string().c_str(),
            runtime::ResolveResourcePath(kPortableDBJobPath).string().c_str());
#endif
        return false;
    }
    // Current 0x00527f54 uses the same BOM-consuming stream helper as the
    // other two file wrappers before calling the stream overload.
    input.get();
    return ParseDBJobData(input);
}

void CReadFile::ExecuteDBJob() {
    L2_GUARD_FUNCTION();
    const CacheDConfiguration& configuration = GetCacheDConfiguration();
    // Current 0x005267ee constructs this connection from the active TLS
    // database ID (TLS block +0x10), not from the configured world ID.
    DBConn connection(runtime::process::CurrentDatabaseId());
    const std::time_t nowValue = std::time(nullptr);
    const std::int32_t now = nowValue < 0 ? -1 : static_cast<std::int32_t>(std::min<std::time_t>(nowValue, INT32_MAX));

    for (const DBJobEntry& entry : m_dbJobs) {
        if (entry.serverType != configuration.serverType || entry.bookingTime >= now) {
            // Noise: expected filtering of jobs for another server type or a future time.
            // Log.Add(
            //     LOG_NORMAL,
            //     "[NO_ERROR] [DBJOB] Didn't execute DBJob. "
            //     "serverType[Script : %d, Server : %d] "
            //     "time[Script : %d, Server : %d]",
            //     static_cast<int>(entry.serverType), static_cast<int>(configuration.serverType), entry.bookingTime, now
            // )
            continue;
        }

        // Log.Add(
        //     LOG_NORMAL,
        //     "[NO_ERROR] [DBJOB] Execute Or Already Executed. "
        //     "serverType[Script : %d, Server : %d] "
        //     "time[Script : %d, Server : %d]",
        //     static_cast<int>(entry.serverType), static_cast<int>(configuration.serverType), entry.bookingTime, now
        // )
        // Log.Add(LOG_NORMAL, "component=db_job event=execution_started booking_time=%d server_type=%d", entry.bookingTime, static_cast<int>(configuration.serverType))
        // Log.Add(LOG_NORMAL, "[database jobs] Processing job scheduled at Unix time %d for server type %d. Execution outcome is reported by the database.", entry.bookingTime, static_cast<int>(configuration.serverType))
        const char* serverType = DBJobServerTypeName(configuration.serverType);
        const std::string scheduled = DBJobScheduledTime(entry.bookingTime);
        const auto executeGroup = [&](const auto& values, int jobType,
                                      const char* operation, const char* valuesLabel) {
            std::array<std::vector<int>, 4> outcomes;
            for (int value : values) {
                const DBJobOutcome outcome = ExecuteDBJobCall(connection, entry.bookingTime, jobType, value);
                outcomes[static_cast<std::size_t>(outcome)].push_back(value);
            }
            for (std::size_t index = 0; index < outcomes.size(); ++index) {
                LogDBJobOutcome(serverType, scheduled, operation, valuesLabel,
                                static_cast<DBJobOutcome>(index), outcomes[index]);
            }
        };
        // Keep the shipping operation/ID order and continue after individual
        // failures. Group only the diagnostics by the returned outcome.
        executeGroup(entry.itemList, 1, "Delete items", "item IDs");
        executeGroup(entry.buffList, 2, "Delete buffs", "skill IDs");
        executeGroup(entry.questList, 3, "Delete quest data", "quest IDs");
        if (entry.deleteUiDate) {
            executeGroup(std::array<int, 1>{entry.deleteUiDateValue}, 4,
                         "Delete UI event counts", "log_date cutoff (exclusive)");
        }
    }
}
