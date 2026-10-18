#include "Shared/src/Log.h"

#include "AllianceDb.h"

#include "Cached.h"
#include "PledgeDb.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/UtilsString.h"
#include "Shared/src/TextRuntime.h"
#include "Runtime.h"

#include <cstdint>
#include <string>

namespace {

std::u16string LowercaseAllianceName(std::u16string value) {
    Utils::CopyStrInLowerCase(value.c_str(), value.data());
    return value;
}

} // namespace

CAllianceDb g_allianceDb;

CAllianceDb::CAllianceDb() = default;
CAllianceDb::~CAllianceDb() = default;

void CAllianceDb::Init() {
    L2_GUARD_FUNCTION();

    const std::uint32_t worldId = static_cast<std::uint32_t>(runtime::process::CurrentDatabaseId());
    std::uint32_t allianceId = 0;
    DBConn connection(static_cast<int>(worldId));
    connection.Bind(&allianceId);
    constexpr const wchar_t* statement = L"{Call lin_LoadAllAllianceId}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return;

    while (connection.Fetch() && allianceId != 0) {
        GetAlliance(allianceId, worldId);
    }
}

CAllianceSP CAllianceDb::GetAlliance(std::uint32_t allianceId, std::uint32_t worldId) {
    L2_GUARD_FUNCTION();
    if (allianceId == 0)
        return {};

    const CAllianceKey requestedKey(static_cast<int>(worldId), static_cast<int>(allianceId));
    CAllianceSP alliance = m_alliances.FindObject(requestedKey);
    if (alliance)
        return alliance;

    std::uint32_t loadedAllianceId = 0;
    char16_t allianceName[25]{};
    std::uint32_t masterPledgeId = 0;
    std::uint32_t oustTime = 0;
    std::uint32_t crestId = 0;
    std::uint32_t loadedWorldId = 0;
    std::int64_t loadedWorldIdIndicator = -1;

    DBConn connection(static_cast<int>(worldId));
    connection.Bind(&loadedAllianceId);
    connection.Bind(allianceName, 0x32);
    connection.Bind(&masterPledgeId);
    connection.Bind(&oustTime);
    connection.Bind(&crestId);
    connection.BindNullable(&loadedWorldId, &loadedWorldIdIndicator);

    constexpr const wchar_t* loadStatement = L"{Call lin_LoadAlliance (?)}";
    connection.Prepare(loadStatement);
    connection.BindParameter(&allianceId);
    if (connection.ExecutePrepared(loadStatement)) {
        connection.Fetch();
    }

    if (loadedAllianceId == 0) {
        // Log.Add(LOG_ERROR, "Cannot Load Alliance[%d]", static_cast<int>(allianceId))
        // Log.Add(LOG_ERROR,
        //     "[alliance] No alliance record was loaded for ID %u from database %u. Returning no cached alliance.",
        //     static_cast<unsigned int>(allianceId), static_cast<unsigned int>(worldId))
        L2_LOG_ERROR(Log,
            "[alliance] No alliance record was loaded for ID %u from database %u; returning no cached alliance.",
            static_cast<unsigned int>(allianceId), static_cast<unsigned int>(worldId));
        return {};
    }

    CAlliance* const loadedAlliance =
        new CAlliance(loadedAllianceId, allianceName, masterPledgeId, oustTime, crestId, loadedWorldId);

    connection.UnBind();
    std::uint32_t memberPledgeId = 0;
    connection.Bind(&memberPledgeId);
    constexpr const wchar_t* memberStatement = L"{Call lin_GetAllMemberPledge (?)}";
    connection.Prepare(memberStatement);
    connection.BindParameter(&allianceId);
    if (connection.ExecutePrepared(memberStatement)) {
        while (connection.Fetch() && memberPledgeId != 0) {
            loadedAlliance->AddMemberPledge(memberPledgeId);
        }
    }

    const CAllianceKey loadedKey(static_cast<int>(worldId), static_cast<int>(loadedAllianceId));
    m_alliances.Insert(loadedKey, loadedAlliance, nullptr, -1);
    L2_RELEASE(loadedAlliance, ObjectRefType::CreateDelete);
    return m_alliances.FindObject(requestedKey);
}

int CAllianceDb::CreateAlliance(const wchar_t* allianceName, std::uint32_t masterPledgeId, int worldId) {
    // Current Cached.exe 0x0044df7c.
    L2_GUARD_FUNCTION();

    const int databaseId = runtime::process::CurrentDatabaseId();
    int allianceId = 0;
    DBConn connection(databaseId);
    connection.Bind(&allianceId);

    constexpr const wchar_t* statement = L"{Call lin_CreateAlliance (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(allianceName);
    connection.BindParameter(&masterPledgeId);
    connection.BindParameter(&worldId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        // Log.Add(LOG_ERROR, "Cannot Create Alliance[%ls] pledge[%u]", allianceName, masterPledgeId)
        // Log.Add(LOG_ERROR,
        //     "[alliance] No creation result was returned for alliance '%s', master clan %u, world %d (database %d).",
        //     allianceName ? WideToUtf8(allianceName).c_str() : "<missing>", static_cast<unsigned int>(masterPledgeId), worldId, databaseId)
        L2_LOG_ERROR(Log,
            "[alliance] No creation result was returned for alliance '%s', master clan %u, world %d (database %d).",
            allianceName ? WideToUtf8(allianceName).c_str() : "<missing>", static_cast<unsigned int>(masterPledgeId), worldId, databaseId);
        return 0;
    }
    if (allianceId == 0)
        return 0;

    CAllianceSP alliance = GetAlliance(static_cast<std::uint32_t>(allianceId), static_cast<std::uint32_t>(databaseId));
    if (alliance) {
        CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(masterPledgeId), false, databaseId);
        if (pledge) {
            pledge->setAllianceId(static_cast<std::uint32_t>(allianceId));
            return allianceId;
        }
    }

    // Log.Add(LOG_ERROR, "failed in loading alliance[%d] restart cached -_-,,", allianceId)
    // Log.Add(LOG_ERROR,
    //     "[alliance] Creation returned alliance ID %d, but the alliance or master clan %u could not be loaded into cache (database %d). Returning failure.",
    //     allianceId, static_cast<unsigned int>(masterPledgeId), databaseId)
    L2_LOG_ERROR(Log,
        "[alliance] Creation returned alliance ID %d, but the alliance or master clan %u could not be loaded into cache (database %d); returning failure.",
        allianceId, static_cast<unsigned int>(masterPledgeId), databaseId);
    return 0;
}

bool CAllianceDb::JoinAlliance(std::uint32_t allianceId, std::uint32_t memberPledgeId) {
    // Current Cached.exe 0x0044e224.
    L2_GUARD_FUNCTION();

    const int databaseId = runtime::process::CurrentDatabaseId();
    int result = 0;
    DBConn connection(databaseId);
    connection.Bind(&result);

    constexpr const wchar_t* statement = L"{Call lin_JoinAlliance (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&allianceId);
    connection.BindParameter(&memberPledgeId);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    if (result == 0) {
        // Log.Add(LOG_ERROR, "Cannot Join Alliance Member[%u] -> %u", memberPledgeId, allianceId)
        // Log.Add(LOG_ERROR,
        //     "[alliance] No success result for clan %u joining alliance %u in database %d. Cached membership not updated.",
        //     static_cast<unsigned int>(memberPledgeId), static_cast<unsigned int>(allianceId), databaseId)
        L2_LOG_ERROR(Log,
            "[alliance] No success result for clan %u joining alliance %u in database %d; cached membership not updated.",
            static_cast<unsigned int>(memberPledgeId), static_cast<unsigned int>(allianceId), databaseId);
        return false;
    }

    CAllianceSP alliance = GetAlliance(allianceId, static_cast<std::uint32_t>(databaseId));
    if (alliance)
        alliance->AddMemberPledge(memberPledgeId);

    CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(memberPledgeId), false, databaseId);
    if (pledge)
        pledge->setAllianceId(allianceId);
    return true;
}

bool CAllianceDb::Withdraw(std::uint32_t allianceId, std::uint32_t memberPledgeId, std::uint32_t withdrawTime) {
    // Current Cached.exe 0x0044e458.
    L2_GUARD_FUNCTION();

    const int databaseId = runtime::process::CurrentDatabaseId();
    int result = 0;
    DBConn connection(databaseId);
    connection.Bind(&result);

    constexpr const wchar_t* statement = L"{Call lin_WithdrawAlliance (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&allianceId);
    connection.BindParameter(&memberPledgeId);
    connection.BindParameter(&withdrawTime);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    if (result == 0) {
        // Log.Add(LOG_ERROR, "Cannot Withdraw Alliance[%u] %u", allianceId, memberPledgeId)
        // Log.Add(LOG_ERROR,
        //     "[alliance] No success result for clan %u withdrawing from alliance %u in database %d. Cached membership not updated.",
        //     static_cast<unsigned int>(memberPledgeId), static_cast<unsigned int>(allianceId), databaseId)
        L2_LOG_ERROR(Log,
            "[alliance] No success result for clan %u withdrawing from alliance %u in database %d; cached membership not updated.",
            static_cast<unsigned int>(memberPledgeId), static_cast<unsigned int>(allianceId), databaseId);
        return false;
    }

    CAllianceSP alliance = GetAlliance(allianceId, static_cast<std::uint32_t>(databaseId));
    if (alliance)
        alliance->RemoveMemberPledge(memberPledgeId);

    CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(memberPledgeId), false, databaseId);
    if (pledge) {
        pledge->setAllianceId(0);
        pledge->setAllianceWithdrawTime(withdrawTime);
    }
    return true;
}

bool CAllianceDb::OustMemberPledge(std::uint32_t allianceId, std::uint32_t memberPledgeId, std::uint32_t oustTime) {
    // Current Cached.exe 0x0044e6e0.
    L2_GUARD_FUNCTION();

    const int databaseId = runtime::process::CurrentDatabaseId();
    int result = 0;
    DBConn connection(databaseId);
    connection.Bind(&result);

    constexpr const wchar_t* statement = L"{Call lin_OustMemberPledge (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&allianceId);
    connection.BindParameter(&memberPledgeId);
    connection.BindParameter(&oustTime);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    if (result == 0) {
        // This Withdraw wording is the current binary's kept diagnostic.
        // Log.Add(LOG_ERROR, "Cannot Withdraw Alliance[%u] %u", allianceId, memberPledgeId)
        // Log.Add(LOG_ERROR,
        //     "[alliance] No success result for expelling clan %u from alliance %u in database %d. Cached membership not updated.",
        //     static_cast<unsigned int>(memberPledgeId), static_cast<unsigned int>(allianceId), databaseId)
        L2_LOG_ERROR(Log,
            "[alliance] No success result for expelling clan %u from alliance %u in database %d; cached membership not updated.",
            static_cast<unsigned int>(memberPledgeId), static_cast<unsigned int>(allianceId), databaseId);
        return false;
    }

    CAllianceSP alliance = GetAlliance(allianceId, static_cast<std::uint32_t>(databaseId));
    if (alliance) {
        alliance->RemoveMemberPledge(memberPledgeId);
        alliance->m_oustTime = oustTime;
    }

    CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(memberPledgeId), false, databaseId);
    if (pledge) {
        pledge->setAllianceId(0);
        pledge->setAllianceOustedTime(oustTime);
    }
    return true;
}

bool CAllianceDb::DismissAlliance(std::uint32_t allianceId, std::uint32_t masterPledgeId, std::uint32_t dismissTime) {
    // Current Cached.exe 0x0044f3ac.
    L2_GUARD_FUNCTION();

    const int databaseId = runtime::process::CurrentDatabaseId();
    int result = 0;
    DBConn connection(databaseId);
    connection.Bind(&result);

    constexpr const wchar_t* statement = L"{Call lin_DismissAlliance (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&allianceId);
    connection.BindParameter(&masterPledgeId);
    connection.BindParameter(&dismissTime);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    if (result == 0) {
        // Log.Add(LOG_ERROR, "Cannot Dismiss Alliance[%u]", allianceId)
        // Log.Add(LOG_ERROR,
        //     "[alliance] No success result for dissolving alliance %u, master clan %u, in database %d. Cached membership not updated.",
        //     static_cast<unsigned int>(allianceId), static_cast<unsigned int>(masterPledgeId), databaseId)
        L2_LOG_ERROR(Log,
            "[alliance] No success result for dissolving alliance %u, master clan %u, in database %d; cached membership not updated.",
            static_cast<unsigned int>(allianceId), static_cast<unsigned int>(masterPledgeId), databaseId);
        return false;
    }

    CAllianceSP alliance = GetAlliance(allianceId, static_cast<std::uint32_t>(databaseId));
    if (alliance) {
        PledgeIdList memberPledgeIds;
        alliance->GetAllMemberPledgeId(memberPledgeIds);
        for (const std::uint32_t memberPledgeId : memberPledgeIds) {
            CPledgeSP pledge = g_pledgeDb.GetPledge(static_cast<int>(memberPledgeId), false, databaseId);
            if (!pledge)
                continue;
            pledge->setAllianceId(0);
            pledge->setAllianceDismissTime(dismissTime);
        }
    }

    m_alliances.DeleteObject(CAllianceKey(databaseId, static_cast<int>(allianceId)), nullptr, -1);
    return true;
}

bool CAllianceDb::SetAllianceInfo(std::uint32_t allianceId, std::uint32_t updateFieldName, std::uint32_t updateValue) {
    // Current Cached.exe 0x0044eb40, 516 bytes, SHA-256
    // 37e312a5fc70102043a3bea2a68462b5c74d77d76f7331c3912576446e13b796.
    L2_GUARD_FUNCTION();

    const wchar_t* fieldName = nullptr;
    if (updateFieldName == 0) {
        fieldName = L"oust_time";
    } else if (updateFieldName == 1) {
        fieldName = L"crest_id";
    } else {
        return false;
    }

    const int worldId = runtime::process::CurrentDatabaseId();
    DBConn connection(worldId);
    constexpr const wchar_t* statement = L"{Call lin_SetAllianceInfo (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(fieldName);
    connection.BindParameter(&updateValue);
    connection.BindParameter(&allianceId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Error in Updating Field Alliance[%d]", static_cast<int>(allianceId))
        // Log.Add(LOG_ERROR,
        //     "[alliance] Cannot update field '%s' to %u for alliance %u in database %d.",
        //     WideToUtf8(fieldName).c_str(), static_cast<unsigned int>(updateValue), static_cast<unsigned int>(allianceId), worldId)
        L2_LOG_ERROR(Log,
            "[alliance] Cannot update field '%s' to %u for alliance %u in database %d.",
            WideToUtf8(fieldName).c_str(), static_cast<unsigned int>(updateValue), static_cast<unsigned int>(allianceId), worldId);
        return false;
    }

    CAllianceSP alliance = GetAlliance(allianceId, static_cast<std::uint32_t>(worldId));
    if (alliance) {
        if (updateFieldName == 0) {
            alliance->m_oustTime = updateValue;
        } else {
            alliance->m_crestId = updateValue;
        }
    }
    return true;
}

bool CAllianceDb::ModifyAllianceName(std::uint32_t allianceId, const wchar_t* allianceName) {
    L2_GUARD_FUNCTION();

    const int worldId = runtime::process::CurrentDatabaseId();
    CAllianceSP alliance = GetAlliance(allianceId, static_cast<std::uint32_t>(worldId));
    if (!alliance)
        return false;

    // Both lowercase copies are explicit calls in current 0x0044ed44 even
    // though their results are not later consumed.
    LowercaseAllianceName(std::u16string(alliance->m_name));
    LowercaseAllianceName(WideToUtf16(allianceName));

    DBConn connection(worldId);
    constexpr const wchar_t* statement = L"{Call lin_ModifyAllianceName (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(allianceName);
    connection.BindParameter(&allianceId);
    if (!connection.ExecutePrepared(statement))
        return false;

    alliance->AssignName(allianceName != nullptr ? WideToUtf16(allianceName).c_str() : nullptr);
    return true;
}

const CAllianceMap& CAllianceDb::alliances() const noexcept {
    // The accessor spelling survives in the C1 owner. Current
    // RequestLoadAllAlliance inlines traversal of this exact +0x10 map.
    return m_alliances;
}
