#include "Shared/src/Log.h"
#include "Shared/src/MemorySub.h"

#include "NewbieDb.h"

#include "Cached.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/Runtime/TargetFormatting.h"
#include "Runtime.h"

#include <array>
#include <string>

namespace {


constexpr int kLogBoth = 6;
constexpr int kCreateNewbieDataLogId = 0x355;
constexpr int kSetExpertLogId = 0x356;

void WriteNewbieAuditLog(int logId, int accountId) {
    // Both current literals have 26 CSV fields, populated at positions zero
    // and two: "%I64d,,%I64d,...".
    std::array<std::u16string, 26> fields{};
    fields[0] = runtime::DecimalUtf16(logId);
    fields[2] = runtime::DecimalUtf16(accountId);

    std::u16string record;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index != 0)
            record.push_back(u',');
        record.append(fields[index]);
    }
    runtime::process::WriteLogFormatterLine(kLogBoth, record);
}

} // namespace

CNewbieDataMap CNewbieDb::m_newbieData;
CNewbieDB g_newbieDB;

L2_DEFINE_OBJECT_CLASS_ID(CNewbieData)

L2_DEFINE_OBJECT_CLASS_NAME(CNewbieData, m_pClassName_CNewbieData)

L2_DEFINE_OBJECT_NEW(CNewbieData)

// Current allocator 0x004f8044 publishes 0x28 bytes. Its class-id CRT
// initializer 0x006a317c is the 21st MemoryObject increment in the exact
// current initializer table (CAlliance = 9 validates the
// same ordering).
L2_DEFINE_OBJECT_ALLOCATOR(CNewbieData, m_pClassName_CNewbieData)

// Current owner 0x004f80c8 is the exact MemoryObject release sequence
// specialized with CNewbieData's class ID and NewbieDb.h line 0x0f.
L2_DEFINE_OBJECT_RELEASE(CNewbieData)

CNewbieData::CNewbieData(int accountId, int characterId, NEWBIE_STAT newbieStat) :
    m_accountId(accountId), m_characterId(characterId), m_newbieStat(newbieStat) {
    // Current inlined construction at 0x004f87d7 and 0x004f89f1 writes the
    // three fields through +0x20 only. +0x24 is tail alignment supplied by
    // the allocator and is not normalized here.
}

CNewbieData::~CNewbieData() = default;

bool CNewbieData::IsNewbieChar(int characterId) const {
    L2_GUARD_FUNCTION();
    return m_newbieStat == NEWBIE_STAT::Newbie && (m_characterId == 0 || characterId == m_characterId);
}

bool CNewbieData::SetNewbieStat(NEWBIE_STAT newbieStat) {
    L2_GUARD_FUNCTION();
    if (m_newbieStat == newbieStat)
        return false;
    m_newbieStat = newbieStat;
    return true;
}

int CNewbieData::GetNewbieStat() const {
    L2_GUARD_FUNCTION();
    return static_cast<int>(m_newbieStat);
}

bool CNewbieData::IsNewbieAccount() const {
    L2_GUARD_FUNCTION();
    return m_newbieStat == NEWBIE_STAT::Newbie && m_characterId == 0;
}

bool CNewbieData::SetNewbieChar(int characterId) {
    L2_GUARD_FUNCTION();
    if (!IsNewbieChar(characterId) || m_characterId == characterId) {
        return false;
    }
    m_characterId = characterId;
    return true;
}

void CNewbieDb::LoadNewbieDataAll() {
    L2_GUARD_FUNCTION();

    std::uint32_t accountId = 0;
    std::uint32_t characterId = 0;
    std::uint32_t newbieStat = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&accountId);
    connection.Bind(&characterId);
    connection.Bind(&newbieStat);

    constexpr const wchar_t* statement = L"{Call lin_LoadNewbieData}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return;

    while (connection.Fetch()) {
        CNewbieData* const data = new CNewbieData(
            static_cast<int>(accountId), static_cast<int>(characterId), static_cast<NEWBIE_STAT>(newbieStat)
        );
        m_newbieData.Insert(static_cast<int>(accountId), data, nullptr, -1);
        L2_RELEASE(data, ObjectRefType::CreateDelete);
    }
}

CNewbieDataSP CNewbieDb::CreateNewbieData(int accountId) {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_CreateNewbieData (? )}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Cannot Create Newbie Data[%d]", accountId)
        // Log.Add(LOG_ERROR,
        //     "[newcomer] Cannot create newcomer status for account %d in database %d. No cache entry created.",
        //     accountId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[newcomer] Cannot create newcomer status for account %d in database %d; no cache entry created.",
            accountId, runtime::process::CurrentDatabaseId());
        return {};
    }

    WriteNewbieAuditLog(kCreateNewbieDataLogId, accountId);
    CNewbieData* const data = new CNewbieData(accountId, 0, NEWBIE_STAT::Newbie);
    m_newbieData.Insert(accountId, data, nullptr, -1);
    L2_RELEASE(data, ObjectRefType::CreateDelete);
    return m_newbieData.FindObject(accountId);
}

bool CNewbieDb::UpdateNewbieCharData(int accountId, int characterId) {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdateNewbieCharData (?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&characterId);
    return connection.ExecutePrepared(statement);
}

CNewbieDataSP CNewbieDb::GetNewbieData(int accountId) {
    L2_GUARD_FUNCTION();

    CNewbieDataSP data = m_newbieData.FindObject(accountId);
    if (!data)
        return CreateNewbieData(accountId);
    return data;
}

bool CNewbieDb::IsNewbieUser(int accountId, int characterId) {
    L2_GUARD_FUNCTION();
    CNewbieDataSP data = GetNewbieData(accountId);
    return data && data->IsNewbieChar(characterId);
}

bool CNewbieDb::IsNewbieAccount(int accountId) {
    L2_GUARD_FUNCTION();
    CNewbieDataSP data = GetNewbieData(accountId);
    return data && data->IsNewbieAccount();
}

void CNewbieDb::SetNewbieUser(int accountId, int characterId) {
    L2_GUARD_FUNCTION();
    CNewbieDataSP data = GetNewbieData(accountId);
    if (data && data->SetNewbieChar(characterId)) {
        UpdateNewbieCharData(accountId, characterId);
    }
}

void CNewbieDb::SetNewbieStat(int accountId, NEWBIE_STAT newbieStat) {
    L2_GUARD_FUNCTION();
    CNewbieDataSP data = GetNewbieData(accountId);
    if (!data || !data->SetNewbieStat(newbieStat))
        return;

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetNewbieStat (?, ? )}";
    connection.Prepare(statement);
    connection.BindParameter(&accountId);
    connection.BindParameter(&newbieStat);
    connection.ExecutePrepared(statement);
}

void CNewbieDB::SetExpert(int accountId) {
    L2_GUARD_FUNCTION();

    CNewbieDataSP data = GetNewbieData(accountId);
    int previousStat = static_cast<int>(NEWBIE_STAT::Expert);
    if (data)
        previousStat = data->GetNewbieStat();

    SetNewbieStat(accountId, NEWBIE_STAT::Expert);
    if (previousStat != static_cast<int>(NEWBIE_STAT::Expert)) {
        WriteNewbieAuditLog(kSetExpertLogId, accountId);
    }
}
