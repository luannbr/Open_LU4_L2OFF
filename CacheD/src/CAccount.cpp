#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"

#include "CAccount.h"

#include "Cached.h"
#include "DBConn.h"
#include "Item.h"
#include "User.h"
#include "UserDb.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/GIP.h"
#include "Shared/src/Runtime/TargetFormatting.h"
#include "Shared/src/Runtime/Time32.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/StandardContainers.h"
#include "Runtime.h"
#include "TextRuntime.h"

#include <algorithm>
#include <array>
#include <ctime>
#include <cwchar>
#include <iterator>
#include <optional>
#include <string>

namespace {


constexpr int kDeletedAccountCharacterLogId = 0x342;
constexpr int kLogBoth = 6;

// Current Account.obj constructor 0x00407f54 and destructor 0x00407e60.
// The damaged current PDB keeps neither the type nor global identifier, so
// this internal semantic name is not presented as original.
// No current instruction reads the object after its CRT construction. It is
// nevertheless kept because its 66 allocations are shipping startup
// side effects instead of test scaffolding.
struct AccountStaticVectorGrid {
    AccountStaticVectorGrid() :
        m_unknownScalarA{},
        m_unknownScalarB{},
        m_unknownScalarCount(0),
        m_padding{},
        m_unknownVectorsA{},
        m_unknownVectorsB{},
        m_unknownVectorsC{} {
        // Current Cached.exe 0x00407f54, 402 bytes, exact SHA-256
        // 99efe53797aff93aca8ea3803a76d793fe74fbee193d77bb6544980e64618ff7.
        for (std::size_t index = 0; index < m_unknownVectorsA.size(); ++index) {
            m_unknownScalarA[index] = 0;
            m_unknownScalarB[index] = 0;

            auto& first = m_unknownVectorsA[index];
            auto& second = m_unknownVectorsB[index];
            auto& third = m_unknownVectorsC[index];
            first.reserve(1000);
            second.reserve(1000);
            third.reserve(1000);
            for (int element = 0; element < 1000; ++element) {
                first.push_back(0);
                second.push_back(0);
                third.push_back(0);
            }
            for (int element = 0; element < 1000; ++element) {
                first.data()[element] = 0;
                second.data()[element] = 0;
                third.data()[element] = 0;
            }
        }
    }

    ~AccountStaticVectorGrid() = default;

    std::array<std::uint64_t, 22> m_unknownScalarA; // +0x000
    std::array<std::uint64_t, 22> m_unknownScalarB; // +0x0b0
    std::int32_t m_unknownScalarCount; // +0x160
    std::array<std::byte, 4> m_padding; // +0x164
    std::array<std::vector<std::uint64_t>, 22> m_unknownVectorsA; // +0x168
    std::array<std::vector<std::uint64_t>, 22> m_unknownVectorsB; // +0x428
    std::array<std::vector<std::uint64_t>, 22> m_unknownVectorsC; // +0x6e8
};


// Current process object 0x007e8b60, initialized by CRT entry 0x0069b96c.
alignas(16) AccountStaticVectorGrid g_accountStaticVectorGrid;

void WriteDeletedAccountCharacterLog(
    int characterId,
    int accountId,
    int requestDate,
    int deletionDate,
    int deletionId,
    int sequence,
    const char16_t* characterName,
    const char16_t* accountName
) {
    // Current helper 0x004081cc is an unnamed Win64 vararg ABI adapter for
    // CLog::Add. The call frame at 0x0040abb0 fixes these nine populated
    // fields and the shipping literal fixes their positions in the 26-field
    // audit record. Build the same UTF-16 payload before the portable CLog
    // transport boundary.
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(kDeletedAccountCharacterLogId);
    fields[1] = runtime::DecimalUtf16(characterId);
    fields[2] = runtime::DecimalUtf16(accountId);
    fields[11] = runtime::DecimalUtf16(requestDate);
    fields[12] = runtime::DecimalUtf16(deletionDate);
    fields[13] = runtime::DecimalUtf16(deletionId);
    fields[14] = runtime::DecimalUtf16(sequence);
    fields[21] = characterName != nullptr ? characterName : u"";
    fields[22] = accountName != nullptr ? accountName : u"";

    std::u16string record;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0)
            record.push_back(u',');
        record.append(fields[index]);
    }
    runtime::process::WriteLogFormatterLine(kLogBoth, record);
}

std::u16string TrimAccountName(const char16_t* value) {
    // Current global Trim 0x005c9cb0 removes every U+0020, not merely a
    // leading/trailing run. CAccount::Load calls it immediately before the
    // fixed 32-WCHAR copy.
    std::u16string result;
    if (!value)
        return result;
    while (*value != u'\0') {
        if (*value != u' ')
            result.push_back(*value);
        ++value;
    }
    return result;
}

struct GetMaxLevelFromAccount {
    bool Do(CUser* user) {
        if (user)
            maximumLevel = std::max(maximumLevel, user->GetLevel());
        return true;
    }

    int maximumLevel = 0;
};

struct CheckNewbieAccountLevel {
    explicit CheckNewbieAccountLevel(int levelLimit) : levelLimit(levelLimit) {}

    bool Do(CUser* user) {
        if (user && levelLimit < user->GetLevel())
            isNewbie = false;
        return true;
    }

    int levelLimit;
    bool isNewbie = true;
};

struct SelectNewbieAccountUser {
    explicit SelectNewbieAccountUser(int levelLimit) : levelLimit(levelLimit) {}

    bool Do(CUser* user) {
        if (!user)
            return true;
        const int level = user->GetLevel();
        if (level < levelLimit && bestLevel < level) {
            bestUser = user;
            bestLevel = level;
        }
        return true;
    }

    int levelLimit;
    CUser* bestUser = nullptr;
    int bestLevel = 0;
};

std::uint16_t GregorianDayOfWeek(int year, int month, int day) {
    // Sunday == 0, matching SYSTEMTIME. For the valid dates returned by the
    // procedure this is the field populated by the target's
    // SystemTimeToFileTime/FileTimeToSystemTime round trip.
    static constexpr int offsets[] = {
        0,
        3,
        2,
        5,
        0,
        3,
        5,
        1,
        4,
        6,
        2,
        4,
    };
    if (month < 3)
        --year;
    const int weekday = (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + day) % 7;
    return static_cast<std::uint16_t>(weekday);
}

SYSTEMTIME ParseDatabaseSystemTime(const wchar_t* value) {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    std::swscanf(value, L"%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second);

    SYSTEMTIME result{};
    result.wYear = static_cast<std::uint16_t>(year);
    result.wMonth = static_cast<std::uint16_t>(month);
    result.wDay = static_cast<std::uint16_t>(day);
    result.wHour = static_cast<std::uint16_t>(hour);
    result.wMinute = static_cast<std::uint16_t>(minute);
    result.wSecond = static_cast<std::uint16_t>(second);
    if (month >= 1 && month <= 12 && day >= 1) {
        result.wDayOfWeek = GregorianDayOfWeek(year, month, day);
    }
    return result;
}

} // namespace

// Current unnamed leaf 0x00408178 returns this exact process-wide map. The
// portable request owners refer to the global directly, preserving the same
// object instead of adding a second accessor or registry.
CAccountMap g_accounts;

L2_DEFINE_OBJECT_CLASS_ID(CAccount)

L2_DEFINE_OBJECT_CLASS_NAME(CAccount, m_pClassName_CAccount)

L2_DEFINE_OBJECT_NEW(CAccount)

// Native-container size differs from the shipping class-size metadata.
void* CAccount::operator new(std::size_t requestedSize, bool objectAllocation) {
    // Current allocator 0x004080e8 publishes target size 0x118. The complete
    // CRT table begins with four Shared initializers. 0x0069b9cc is therefore
    // the fifth MemoryObject class initializer and assigns class ID 5.
    return runtime::process::AllocateMemoryObject(
        requestedSize, objectAllocation, L2_MEMORY_CLASS_ID(CAccount), m_pClassName_CAccount, "CAccount", 0x118
    );
}

L2_DEFINE_OBJECT_RELEASE(CAccount)

CAccount::CAccount(int accountId) : m_id(accountId) {
    // Current constructor 0x0040c07c additionally constructs the same
    // character map and both xmaps represented by the members above.
}

// Current destructor 0x0040c30c destroys the +0xf8 map, the +0xd8 event-time
// map and the +0x18 character map in reverse member order.
CAccount::~CAccount() = default;

CAccountSP CAccount::Find(CAccountKey accountKey) {
    // Current CAccount::Find 0x005a5548 forwards the eight-byte key by value
    // directly to this main CObjectMap instantiation.
    return g_accounts.FindObject(accountKey);
}

CAccountSP CAccount::Load(unsigned int accountId) {
    L2_GUARD_FUNCTION();

    // Current CAccount::Load 0x0040c6b4 reads the active database/world ID
    // from TLS+0x10 for the map key, DB connection and CUserDb lookups.
    const int worldId = runtime::process::CurrentDatabaseId();
    const CAccountKey key(worldId, static_cast<int>(accountId));
    CAccountSP existing = g_accounts.FindObject(key);
    if (existing) {
        if (existing->m_id == static_cast<int>(accountId))
            return existing;
        // Log.Add(
        //     LOG_ERROR,
        //     "[Account.cpp][181]Cannot load account[%u] != [%d] "
        //     "account map cause fault.",
        //     accountId, existing->m_id
        // )
        // Noise: omit shipping coordinates. Identify the requested and stored IDs.
        // L2_LOG_ERROR(Log,
        //     "[Account.cpp][181]Cannot load account[%u] != [%d] "
        //     "account map cause fault.", accountId, existing->m_id)
        L2_LOG_ERROR(Log,
            "[accounts] Account map mismatch in world %d: requested account %u, found account %d. Continuing the load attempt.",
            worldId, accountId, existing->m_id);
    }

    if (accountId == 0) {
        // Current FUN_004cb1cc(2) records the invalid account path before
        // returning an empty smart pointer. The portable process diagnostic
        // frontend is the recovered local logger.
        // Log.Add(LOG_ERROR, "CAccount::Load rejected account id 0")
        // L2_LOG_ERROR(Log, "CAccount::Load rejected account id 0")
        L2_LOG_ERROR(Log, "[accounts] Cannot load account ID 0 in world %d. Returning no account.", worldId);
        return {};
    }

    unsigned int characterId = 0;
    std::array<char16_t, 50> accountName{};
    DBConn connection(worldId);
    connection.Bind(&characterId);
    connection.Bind(accountName.data(), 100);
    constexpr const wchar_t* statement = L"{Call lin_LoadAccount (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);

    if (connection.ExecutePrepared(statement)) {
        CAccount* account = new CAccount(static_cast<int>(accountId));
        while (connection.Fetch()) {
            CUserSP user = g_userDb.GetUser(static_cast<int>(characterId), true, false, worldId);
            if (user) {
                account->m_chars.Insert(static_cast<int>(characterId), user.get());
            }
        }

        const std::u16string trimmed = TrimAccountName(accountName.data());
        account->m_name.fill(u'\0');
        if (trimmed.size() < account->m_name.size()) {
            std::copy(trimmed.begin(), trimmed.end(), account->m_name.begin());
        }

        account->LoadEventTime();
        g_accounts.Insert(key, account);
        L2_RELEASE(account, ObjectRefType::CreateDelete);
    }

    CAccountSP result = g_accounts.FindObject(key);
    if (!result)
        return {};
    if (result->m_id == static_cast<int>(accountId))
        return result;

    // Log.Add(
    //     LOG_ERROR,
    //     "[Account.cpp][235]Cannot load account[%d] != [%u] "
    //     "account map cause fault.",
    //     result->m_id, accountId
    // )
    // Noise: omit shipping coordinates. Retain the post-load mismatch and outcome.
    // L2_LOG_ERROR(Log,
    //     "[Account.cpp][235]Cannot load account[%d] != [%u] "
    //     "account map cause fault.", result->m_id, accountId)
    L2_LOG_ERROR(Log,
        "[accounts] Account map mismatch after loading in world %d: requested account %u, found account %d. Returning no account.",
        worldId, accountId, result->m_id);
    return {};
}

CAccountSP CAccount::Reload(unsigned int accountId) {
    // Current Cached.exe 0x0040cc44 removes the active-world account object,
    // if present, and then returns a normal CAccount::Load result. The source
    // identifier survives in the compatible C1 Account.cpp owner.
    const CAccountKey key(runtime::process::CurrentDatabaseId(), static_cast<int>(accountId));
    CAccountSP account = g_accounts.FindObject(key);
    if (account) {
        g_accounts.DeleteObject(key);
    }
    return Load(accountId);
}

int CAccount::GetId() const noexcept {
    // Current Cached.exe 0x00408174 is the four-byte +0x78 load. The original
    // identifier is kept by the compatible C1 owner.
    return m_id;
}

const char16_t* CAccount::GetName() const noexcept {
    // Current CreateCharacterPacket reads the embedded target buffer at
    // CAccount +0x7c. The corroborating C1 owner exposes this same source
    // accessor instead of duplicating account-name state in the handler.
    return m_name.data();
}

CObjectMap<CUser, 1, 5, RWLock, int>& CAccount::GetChars() noexcept {
    // Current callers use the member map at +0x18 directly through this
    // source-level accessor, as did the corroborating C1 owner.
    return m_chars;
}

int CAccount::GetCharCount() {
    // Current unnamed adapter 0x00408afc is exactly `add rcx, 0x18` followed
    // by a tail jump to the one-bucket CUser-map GetSize instantiation at
    // 0x004088fc. GetCharCount is the semantic source name corroborated by
    // the Account owner. No separate cached counter exists in the target.
    return m_chars.GetSize();
}

int CAccount::GetMaxLevel() {
    L2_GUARD_FUNCTION();
    std::optional<GetMaxLevelFromAccount> selector;
    {
        L2_READ_GUARD(*this);
        selector.emplace();
        m_chars.FOR_ALL(&*selector, L"Account.cpp", 0x1da);
    }
    return selector->maximumLevel;
}

bool CAccount::IsNewbieAccount(int levelLimit) {
    L2_GUARD_FUNCTION();
    std::optional<CheckNewbieAccountLevel> selector;
    {
        L2_READ_GUARD(*this);
        selector.emplace(levelLimit);
        m_chars.FOR_ALL(&*selector, L"Account.cpp", 0x1f7);
    }
    return selector->isNewbie;
}

CSPointer<CUser> CAccount::GetNewbieUser(int levelLimit) {
    L2_GUARD_FUNCTION();
    std::optional<SelectNewbieAccountUser> selector;
    {
        L2_READ_GUARD(*this);
        selector.emplace(levelLimit);
        m_chars.FOR_ALL(&*selector, L"Account.cpp", 0x219);
    }
    return selector->bestUser ? CSPointer<CUser>(selector->bestUser, ObjectRefType::FunctionChain) : CSPointer<CUser>{};
}

void CAccount::SetCharacter(CUser* user) {
    L2_GUARD_FUNCTION();
    if (user) {
        m_chars.Insert(static_cast<int>(user->GetId()), user);
    }
}

int CAccount::DeleteCharacter(unsigned int characterId) {
    // Current Cached.exe 0x0040a4fc, 428 bytes, SHA-256
    // 4c6f2e6485ff5a693c51cd49d0d8429a021fc816fe0b69150de3962181d111bb.
    L2_GUARD_FUNCTION();
    CUserSP user = m_chars.FindObject(static_cast<int>(characterId));
    if (!user)
        return 0;

    m_chars.DeleteObject(static_cast<int>(characterId));
    const int level = user->GetLevel();
    if (!g_userDb.DeleteChar(user.get(), true))
        return 0;

    GIP._Add(
        L"CMD_DeleteCharacter(%d, %d, %d, %d", m_id, GetCacheDConfiguration().worldId, characterId,
        static_cast<std::int64_t>(std::time(nullptr))
    );
    return level;
}

void CAccount::ReadLock(const wchar_t* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    s_rwLock.ReadLock(callerFile, callerLine);
}

void CAccount::ReadUnlock() {
    L2_GUARD_FUNCTION();
    s_rwLock.ReadUnlock();
}

void CAccount::WriteLock(const wchar_t* callerFile, int callerLine) {
    L2_GUARD_FUNCTION();
    s_rwLock.WriteLock(callerFile, callerLine);
}

void CAccount::WriteUnlock() {
    L2_GUARD_FUNCTION();
    s_rwLock.WriteUnlock();
}

bool CAccount::LoadUserStamp(int type, std::map<int, int>& stamps) {
    L2_GUARD_FUNCTION();
    // Current CAccount::LoadUserStamp 0x0040b4d8 selects every record for
    // (account,type) by passing the final selector pair as (0,0).
    int record = 0;
    int stamp = 0;
    const int selectOne = 0;
    const int selectedRecord = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&record);
    connection.Bind(&stamp);
    constexpr const wchar_t* query = L"{Call lin_LoadAccountUserStamp(?,?,?,?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_id);
    connection.BindParameter(&type);
    connection.BindParameter(&selectOne);
    connection.BindParameter(&selectedRecord);
    if (!connection.ExecutePrepared(query)) {
        // Log.Add(
        //     LOG_ERROR, "File[%s] Line[%d] query has problem id[%d] type[%d]", "Account.cpp", 0x2c5, m_id, type
        // )
        L2_LOG_ERROR(Log,
            "[account stamps] Cannot load stamps for account %d, type %d: database execution failed. No rows added to the destination map.",
            m_id, type);
        return false;
    }
    while (connection.Fetch()) {
        stamps.emplace(record, stamp);
    }
    return true;
}

bool CAccount::LoadUserStamp(int type, int selectedRecord, std::map<int, int>& stamps) {
    L2_GUARD_FUNCTION();
    // Current overload 0x0040b69c passes (selectedRecord,1). This order is
    // fixed by the two stack arguments to helper 0x0040a708 and matches the
    // database procedure's (stamp,useStamp) tail.
    int record = 0;
    int stamp = 0;
    const int selectOne = 1;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&record);
    connection.Bind(&stamp);
    constexpr const wchar_t* query = L"{Call lin_LoadAccountUserStamp(?,?,?,?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_id);
    connection.BindParameter(&type);
    connection.BindParameter(&selectedRecord);
    connection.BindParameter(&selectOne);
    if (!connection.ExecutePrepared(query)) {
        // Log.Add(
        //     LOG_ERROR, "File[%s] Line[%d] query has problem id[%d] type[%d]", "Account.cpp", 0x2e3, m_id, type
        // )
        L2_LOG_ERROR(Log,
            "[account stamps] Cannot load selected record %d for account %d, type %d: database execution failed. "
            "No rows added to the destination map.",
            selectedRecord, m_id, type);
        return false;
    }
    while (connection.Fetch()) {
        stamps.emplace(record, stamp);
    }
    return true;
}

bool CAccount::SaveUserStamp(int type, int record, int stamp, bool enabled) {
    L2_GUARD_FUNCTION();
    // Current CAccount::SaveUserStamp 0x0040b870.
    const int enabledValue = enabled ? 1 : 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* query = L"{Call lin_UpdateAccountUserStamp(?,?,?,?,?)}";
    connection.Prepare(query);
    connection.BindParameter(&m_id);
    connection.BindParameter(&type);
    connection.BindParameter(&record);
    connection.BindParameter(&stamp);
    connection.BindParameter(&enabledValue);
    const bool saved = connection.ExecutePrepared(query);
    if (!saved) {
        // Log.Add(
        //     LOG_ERROR, "FILE[%s], LINE[%d] account id[%d] type[%d] record[%d]", "Account.cpp", 0x2f3, m_id, type,
        //     record
        // )
        L2_LOG_ERROR(Log,
            "[account stamps] Cannot save record %d for account %d, type %d (stamp %d, enabled %d): database execution failed.",
            record, m_id, type, stamp, enabledValue);
    }
    return saved;
}

bool CAccount::MakeUserStampLogPacket(int type, CPacketHelperEx<20480>& packet) {
    // Current Cached.exe 0x0040b9e4. The message and date columns are bound
    // because they are part of the shipping result set, although only param1
    // is serialized into the packet helper for the sole supported type.
    L2_GUARD_FUNCTION();

    int parameter = 0;
    std::array<char16_t, 256> message{};
    int logDate = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&parameter);
    connection.Bind(message.data(), 0x100);
    connection.Bind(&logDate);
    constexpr const wchar_t* statement = L"{Call lin_LoadAccountUserStampLog(?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_id);
    connection.BindParameter(&type);
    if (connection.ExecutePrepared(statement)) {
        while (connection.Fetch()) {
            if (type != 0) {
                // Log.Add(LOG_ERROR, "FILE[%s], LINE[%d] unknown type!!! error!!", "Account.cpp", 0x312)
                L2_LOG_ERROR(Log,
                    "[account stamps] Cannot assemble stamp history for account %d: unsupported type %d; only type 0 is supported. "
                    "Stopped reading rows without adding stamp data to the packet.",
                    m_id, type);
                break;
            }
            packet.Assemble("c", static_cast<int>(static_cast<std::int8_t>(parameter)));
        }
    }

    // The shipping epilogue clears AL after the DBConn destructor. All
    // callers consume only the assembled packet and ignore this return.
    return false;
}

int CAccount::GetWeekPlayTimeFromDB() {
    L2_GUARD_FUNCTION();
    int usedSeconds = 0;
    std::array<char16_t, 20> changedAt{};
    constexpr char16_t defaultChangedAt[] = u"2004-01-01 0:0:0";
    std::copy(std::begin(defaultChangedAt), std::end(defaultChangedAt), changedAt.begin());

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&usedSeconds);
    connection.Bind(changedAt.data(), 0x28);
    constexpr const wchar_t* statement = L"{Call lin_LoadDayUsedTime (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_id);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        // Log.Add(
        //     LOG_WARN,
        //     "[Account.cpp][394] Cannot load day uesed time for "
        //     "account [%d]",
        //     m_id
        // )
        // Noise: omit shipping coordinates. Retain the failed operation and account.
        // L2_LOG_WARN(Log,
        //     "[Account.cpp][394] Cannot load day uesed time for "
        //     "account [%d]", m_id)
        L2_LOG_WARN(Log,
            "[accounts] Could not execute or fetch lin_LoadDayUsedTime for account %d. Continuing with the time values left by the query.",
            m_id);
    }

    // Current 0x0040ae19 performs these assignments even on Execute/Fetch
    // failure, retaining the zero/default locals initialized above.
    m_weekPlayTime = usedSeconds;
    m_lastSaveTime = ParseDatabaseSystemTime(Utf16ToWide(changedAt.data()).c_str());
    return m_weekPlayTime;
}

void CAccount::LoadEventTime() {
    L2_GUARD_FUNCTION();
    int eventTime = 0;
    int eventType = 0;
    int eventVariable = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&eventTime);
    connection.Bind(&eventType);
    connection.Bind(&eventVariable);
    constexpr const wchar_t* statement = L"{Call lin_LoadEventTime (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_id);
    if (!connection.ExecutePrepared(statement))
        return;

    while (connection.Fetch()) {
        const std::uint32_t key = static_cast<std::uint16_t>(eventType) |
            (static_cast<std::uint32_t>(static_cast<std::uint16_t>(eventVariable)) << 16U);
        m_eventTimes[static_cast<int>(key)] = eventTime;
    }
}

int CAccount::GetEventTime(int eventType, int eventVariable) {
    // Current Cached.exe 0x004099e0 truncates both components to 16 bits,
    // uses them as one ordered-map key and returns zero for a missing entry.
    L2_GUARD_FUNCTION();
    const std::uint32_t key = static_cast<std::uint16_t>(eventType) |
        (static_cast<std::uint32_t>(static_cast<std::uint16_t>(eventVariable)) << 16U);
    const auto found = m_eventTimes.find(static_cast<int>(key));
    return found == m_eventTimes.end() ? 0 : found->second;
}

void CAccount::SetEventTime(int eventTime, int eventType, int eventVariable, bool saveToDatabase) {
    // Current Cached.exe 0x0040b25c persists first when requested, ignores
    // that SQL result, and always publishes the new in-memory value.
    L2_GUARD_FUNCTION();
    if (saveToDatabase) {
        DBConn connection(runtime::process::CurrentDatabaseId());
        constexpr const wchar_t* statement = L"{Call lin_SaveEventTime (?, ?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&m_id);
        connection.BindParameter(&eventTime);
        connection.BindParameter(&eventType);
        connection.BindParameter(&eventVariable);
        connection.ExecutePrepared(statement);
    }

    const std::uint32_t key = static_cast<std::uint16_t>(eventType) |
        (static_cast<std::uint32_t>(static_cast<std::uint16_t>(eventVariable)) << 16U);
    m_eventTimes[static_cast<int>(key)] = eventTime;
}

void CAccount::ClearEventTime() {
    // Current Cached.exe 0x0040b394 clears the in-memory map only after the
    // database procedure succeeds.
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ClearEventTime (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_id);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "EventTime Clear Fail [%d]", m_id)
        L2_LOG_ERROR(Log,
            "[account events] Cannot clear event times for account %d: database execution failed. In-memory event times are retained.",
            m_id);
        return;
    }
    m_eventTimes.clear();
}

int CAccount::GetWeekPlayTime() {
    L2_GUARD_FUNCTION();
    const bool enabled = GetCacheDConfiguration().timeLimit > 0;
    if (m_weekPlayTime == -1 && enabled) {
        GetWeekPlayTimeFromDB();
    }
    return m_weekPlayTime == -1 && enabled ? 0 : m_weekPlayTime;
}

SYSTEMTIME CAccount::GetWeekPlayTimeLastChanged() {
    L2_GUARD_FUNCTION();
    const bool enabled = GetCacheDConfiguration().timeLimit > 0;
    if (m_weekPlayTime == -1 && enabled) {
        GetWeekPlayTimeFromDB();
        if (m_weekPlayTime == -1) {
            // Log.Add(
            //     LOG_ERROR,
            //     "[Account.cpp][370]Error. Cannot load Week play time, "
            //     "account id [%d]",
            //     m_id
            // )
            // Noise: omit shipping coordinates. Describe the checked sentinel explicitly.
            // L2_LOG_ERROR(Log,
            //     "[Account.cpp][370]Error. Cannot load Week play time, "
            //     "account id [%d]", m_id)
            L2_LOG_ERROR(Log,
                "[accounts] Weekly play time remains -1 after loading account %d. Returning the stored last-change time.",
                m_id);
        }
    }
    return m_lastSaveTime;
}

bool CAccount::SaveWeekPlayTime(int weekPlayTime, SYSTEMTIME* lastSaveTime) {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    // Current 0x0040aee4 uses a 20-WCHAR buffer and the unpadded
    // year-month-day hour:minute:second representation below.
    std::array<wchar_t, 20> lastSaveText{};
    std::swprintf(
        lastSaveText.data(), lastSaveText.size(), L"%d-%d-%d %d:%d:%d", static_cast<int>(lastSaveTime->wYear),
        static_cast<int>(lastSaveTime->wMonth), static_cast<int>(lastSaveTime->wDay), static_cast<int>(lastSaveTime->wHour),
        static_cast<int>(lastSaveTime->wMinute), static_cast<int>(lastSaveTime->wSecond)
    );

    constexpr const wchar_t* statement = L"{Call lin_SaveTimeData (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_id);
    connection.BindParameter(&weekPlayTime);
    connection.BindParameter(lastSaveText.data());
    // The current method ignores the execution result, updates
    // both cached members and returns true unconditionally.
    connection.ExecutePrepared(statement);
    m_weekPlayTime = weekPlayTime;
    m_lastSaveTime = *lastSaveTime;
    return true;
}

bool CAccount::SetLastSaveTIme(const SYSTEMTIME* lastSaveTime) {
    L2_GUARD_FUNCTION();
    m_lastSaveTime = *lastSaveTime;
    return true;
}

void CAccount::SetLastLogoutTime(const SYSTEMTIME& lastLogoutTime) {
    L2_GUARD_FUNCTION();
    // Current helper 0x00408278 zeroes tm (including tm_isdst) before
    // calling _mktime32.
    const int converted =
        runtime::crt::SystemTimeToTime32(
            lastLogoutTime);
    if (m_lastLogoutTime < converted) {
        m_lastLogoutTime = converted;
    }
}

void CAccount::ClearUpFromAddedService3ChangeName() {
    L2_GUARD_FUNCTION();
    // Exact current 0x00408424 call surface. The body of the stored procedure
    // is external to Cached.exe and is ported separately from pinned modern
    // database exports.
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.ExecuteNoError(L"{Call lin_ClearUpCharFromAddedService3ChangeName}");
}

void CAccount::DeleteChars(int requestDate, int deletionDate, int deletionId, int accountId, int sequence) {
    L2_GUARD_FUNCTION();
    if (accountId <= 0)
        return;

    // Current 0x0040c4cc and 0x0040c64a bind TLS +0x10 to both
    // lin_GetDelAccountId and lin_SetDelId.
    const int worldId = runtime::process::CurrentDatabaseId();
    int characterId = 0;
    int resultAccountId = 0;
    std::array<char16_t, 50> characterName{};
    std::array<char16_t, 50> accountName{};

    DBConn accountCharacters(worldId);
    accountCharacters.Bind(&characterId);
    accountCharacters.Bind(&resultAccountId);
    accountCharacters.Bind(characterName.data(), 100);
    accountCharacters.Bind(accountName.data(), 100);
    constexpr const wchar_t* getCharacters = L"{Call lin_GetCharByAccountId (? )}";
    accountCharacters.Prepare(getCharacters);
    accountCharacters.BindParameter(&accountId);
    if (!accountCharacters.ExecutePrepared(getCharacters))
        return;

    // Noise: query success alone provides no deletion result. DBConn reports failures.
    // Log.Add(LOG_ERROR, "[NO_ERROR]Get Delete Characters by Account Success!!")
    characterId = 0;
    resultAccountId = 0;
    characterName.fill(u'\0');
    accountName.fill(u'\0');

    unsigned int itemId = 0;
    DBConn deleteCharacter(worldId);
    deleteCharacter.Bind(&itemId);

    while (accountCharacters.Fetch()) {
        constexpr const wchar_t* clearPledge = L"{Call lin_DeleteCharClearPledge (? )}";
        deleteCharacter.Prepare(clearPledge);
        deleteCharacter.BindParameter(&characterId);
        if (!deleteCharacter.ExecutePrepared(clearPledge)) {
            const std::string name = Utf16ToUtf8(characterName.data());
            // Log.Add(
            //     LOG_ERROR,
            //     "Cannot delete character name from lin2world db. "
            //     "[%d],[%s]",
            //     characterId, name.c_str()
            // )
            // Log.Add(LOG_ERROR, "component=account_cleanup event=clear_pledge_failed account_id=%d character_id=%d character_name=\"%s\", character skipped", accountId, characterId, name.c_str())
            // Log.Add(LOG_ERROR, "[account cleanup] Could not clear the pledge for account %d, character %d (\"%s\"). Skipping this character.", accountId, characterId, name.c_str())
            L2_LOG_ERROR(Log, "[account cleanup] Could not clear the pledge for account %d, character %d (\"%s\"). Skipping this character.", accountId, characterId, name.c_str());
            deleteCharacter.ResetParam();
            continue;
        }

        deleteCharacter.ResetParam();
        constexpr const wchar_t* deleteCharacterStatement = L"{Call lin_DeleteChar (?, 1)}";
        deleteCharacter.Prepare(deleteCharacterStatement);
        deleteCharacter.BindParameter(&characterId);
        if (!deleteCharacter.ExecutePrepared(deleteCharacterStatement)) {
            const std::string name = Utf16ToUtf8(characterName.data());
            // Log.Add(
            //     LOG_ERROR,
            //     "Cannot delete character name from lin2world db. "
            //     "failed to clear pledge. [%d],[%s]",
            //     characterId, name.c_str()
            // )
            // Log.Add(LOG_ERROR, "component=account_cleanup event=delete_character_failed account_id=%d character_id=%d character_name=\"%s\", character skipped", accountId, characterId, name.c_str())
            // Log.Add(LOG_ERROR, "[account cleanup] Could not delete character for account %d, character %d (\"%s\"). Skipping this character.", accountId, characterId, name.c_str())
            L2_LOG_ERROR(Log, "[account cleanup] Could not delete character for account %d, character %d (\"%s\"). Skipping this character.", accountId, characterId, name.c_str());
            deleteCharacter.ResetParam();
            continue;
        }

        // Log.Add(LOG_ERROR, "[NO_ERROR]Call Delete Character Success!! : CharID[%d]", characterId)
        // Log.Add(LOG_NORMAL, "component=account_cleanup event=delete_character_executed account_id=%d character_id=%d", accountId, characterId)
        // Log.Add(LOG_NORMAL, "[account cleanup] Delete-character procedure executed for account %d, character %d. Processing returned items.", accountId, characterId)
        L2_LOG_INFO(Log, "[account cleanup] Delete-character procedure executed for account %d, character %d. Processing returned items.", accountId, characterId);
        while (deleteCharacter.Fetch()) {
            CItemSP item = CItem::Load(itemId);
            if (item && item->Delete(true, false)) {
                item->CopyByDeleteChar();
            }
        }

        WriteDeletedAccountCharacterLog(
            characterId, resultAccountId, requestDate, deletionDate, deletionId, sequence, characterName.data(),
            accountName.data()
        );
        deleteCharacter.ResetParam();
    }
}

void CAccount::Init() {
    L2_GUARD_FUNCTION();
    // Noise: function-entry message is logged even when account cleanup is disabled.
    // Log.Add(LOG_ERROR, "[NO_ERROR]CAccount::Init Called!")
    if (!GetCacheDConfiguration().deleteAccount)
        return;

    // Log.Add(LOG_ERROR, "Clearing chars owned by deleted account")
    // Log.Add(LOG_NORMAL, "component=account_cleanup event=starting")
    // Log.Add(LOG_NORMAL, "[account cleanup] Starting character cleanup for deleted accounts.")
    L2_LOG_INFO(Log, "[account cleanup] Starting character cleanup for deleted accounts.");

    int requestDate = 0;
    int deletionDate = 0;
    int deletionId = 0;
    int accountId = 0;
    int sequence = 0;
    std::int64_t nullableIndicator = -1;

    DBConn deletedAccounts(-1);
    deletedAccounts.BindNullable(&requestDate, &nullableIndicator);
    deletedAccounts.BindNullable(&deletionDate, &nullableIndicator);
    deletedAccounts.BindNullable(&deletionId, &nullableIndicator);
    deletedAccounts.BindNullable(&accountId, &nullableIndicator);
    deletedAccounts.BindNullable(&sequence, &nullableIndicator);

    // Current 0x0040c4cc and 0x0040c64a bind TLS +0x10 to both
    // lin_GetDelAccountId and lin_SetDelId.
    const int worldId = runtime::process::CurrentDatabaseId();
    constexpr const wchar_t* getDeletedAccounts = L"{Call lin2user.dbo.lin_GetDelAccountId (? )}";
    deletedAccounts.Prepare(getDeletedAccounts);
    deletedAccounts.BindParameter(&worldId);
    if (!deletedAccounts.ExecutePrepared(getDeletedAccounts))
        return;

    // Noise: successful query submission duplicates the account-cleanup progress below.
    // Log.Add(LOG_ERROR, "[NO_ERROR]Get Delete Account Success!!")
    requestDate = 0;
    deletionDate = 0;
    deletionId = 0;
    accountId = 0;
    sequence = 0;

    int lastDeletionId = 0;
    while (deletedAccounts.Fetch()) {
        // Log.Add(LOG_ERROR, "[NO_ERROR]Clearing chars by deleted account : AccountID[%d]", accountId)
        // Log.Add(LOG_NORMAL, "component=account_cleanup event=account_processing account_id=%d deletion_id=%d", accountId, deletionId)
        // Log.Add(LOG_NORMAL, "[account cleanup] Processing account %d (deletion request %d).", accountId, deletionId)
        L2_LOG_INFO(Log, "[account cleanup] Processing account %d (deletion request %d).", accountId, deletionId);
        DeleteChars(requestDate, deletionDate, deletionId, accountId, sequence);
        lastDeletionId = deletionId;
        requestDate = 0;
        deletionDate = 0;
        deletionId = 0;
        accountId = 0;
        sequence = 0;
    }

    if (lastDeletionId > 0) {
        deletedAccounts.UnBind();
        constexpr const wchar_t* setDeletionId = L"{Call lin2user.dbo.lin_SetDelId (?, ? )}";
        deletedAccounts.Prepare(setDeletionId);
        deletedAccounts.BindParameter(&worldId);
        deletedAccounts.BindParameter(&lastDeletionId);
        deletedAccounts.ExecutePrepared(setDeletionId);
    }
}
