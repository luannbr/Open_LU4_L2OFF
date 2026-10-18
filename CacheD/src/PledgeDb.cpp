#include "PledgeDb.h"
#include "Shared/src/TemplateHelper.h"

#include "Alliance.h"
#include "AllianceDb.h"
#include "CacheSocket.h"
#include "Cached.h"
#include "IOCPMessageCached.h"
#include "Pledge.h"
#include "PledgeCrestDb.h"
#include "User.h"
#include "UserDb.h"

#include "L2Core/src/inc/Guard.h"
#include "Shared/src/DBConn.h"
#include "Shared/src/GIP.h"
#include "Shared/src/LogFormatter.h"
#include "Shared/src/PledgeRanking.h"
#include "Shared/src/Runtime/Time32.h"
#include "Portable/ProcessRuntime.h"
#include "Shared/src/UtilsString.h"
#include "Shared/src/Log.h"
#include "Shared/src/PacketHelper.h"
#include "Shared/src/TextRuntime.h"
#include "Runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct CPledgeProperty {
    std::uint32_t pledgeId = 0;
    std::array<char16_t, 24> pledgeName{};
    std::uint32_t rulerId = 0;
    std::array<char16_t, 24> rulerName{};
    std::uint32_t allianceId = 0;
    std::uint32_t challengeTime = 0;
    std::uint32_t nowWarId = 0;
    std::uint32_t nameValue = 0;
    std::uint32_t oustTime = 0;
    std::uint32_t skillLevel = 0;
    std::uint32_t privateFlag = 0;
    int status = 0;
    int rank = 0;
    std::uint32_t castleId = 0;
    std::uint32_t agitId = 0;
    std::uint32_t fortressId = 0;
    int rootNameValue = 0;
    std::uint32_t crestId = 0;
    std::uint32_t isGuilty = 0;
    std::uint32_t dismissReservedTime = 0;
    std::uint32_t allianceOustedTime = 0;
    std::uint32_t allianceWithdrawTime = 0;
    std::uint32_t allianceDismissTime = 0;
    std::uint32_t newEmblemId = 0;
    std::uint32_t castleSiegeDefenceCount = 0;
    int worldId = 0;
    std::uint32_t masteryResetTime = 0;

    // BindProperty in the current object shares one SQLLEN initialized to
    // SQL_NULL_DATA across all nullable result columns.
    std::int64_t nullableIndicator = -1;
};

std::wstring LowercasePledgeName(const wchar_t* value) {
    std::wstring result(value);
    Utils::CopyStrInLowerCase(result.c_str(), result.data());
    return result;
}


std::wstring LowercasePledgeName(const char16_t* value) {
    std::u16string target(value);
    Utils::CopyStrInLowerCase(target.c_str(), target.data());
    return Utf16ToWide(target);
}

std::string PledgeNameForLog(const CPledge* pledge) {
    const char16_t* const name = pledge->GetName();
    return Utf16ToUtf8(std::u16string_view(name));
}

void LogPledgeLoadFailure(int sourceLine, const char* operation, const CPledge* pledge) {
    static_cast<void>(sourceLine); // Original source line kept in the caller comments.
    const std::string name = PledgeNameForLog(pledge);
    // Log.Add(
    //     LOG_ERROR, "[PledgeDb.cpp][%d] fail to %s[%s:%u]", sourceLine, operation, name.c_str(), pledge->GetId()
    // )
    // Log.Add(LOG_ERROR,
    //     "[clan] Cannot load %s for clan '%s' (ID %u, world %d). Continuing with incomplete clan data.",
    //     operation, name.c_str(), pledge->GetId(), pledge->GetWorldId())
    L2_LOG_ERROR(Log,
        "[clan] Cannot load %s for clan '%s' (ID %u, world %d); continuing with incomplete clan data.",
        operation, name.c_str(), pledge->GetId(), pledge->GetWorldId());
}

} // namespace

namespace PledgeDBUtil {

bool WritePledgeWaitingUser(
    int worldId,
    int characterId,
    const wchar_t* characterName,
    int classId,
    int level,
    int pledgeId,
    const wchar_t* selfIntroduction
) {
    // Current Cached.exe 0x00515bac. Unlike older donor procedures, the
    // shipping callable returns one integer row. Only result == 1 succeeds.
    L2_GUARD_FUNCTION();

    int result = 0;
    // _time32_t is a four-byte Win64 long. The current DBConn helper routes
    // that source type through SQL_C_SLONG/SQL_DOUBLE, not SQL_INTEGER.
    const long registerDate = static_cast<long>(runtime::crt::Time32Now());
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_WritePledgeWaitingUser (?,?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&characterId);
    connection.BindParameter(characterName);
    connection.BindParameter(&classId);
    connection.BindParameter(&level);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&registerDate);
    connection.BindParameter(selfIntroduction);
    if (connection.ExecutePrepared(statement)) {
        connection.Fetch();
    }
    return result == 1;
}

} // namespace PledgeDBUtil

// The target helper is a 0x520-byte DBConn-derived stack object. Its host
// form keeps the same SQL/result ownership while DBConn itself remains the
// documented PostgreSQL pimpl boundary.
class CPledgeDbHelper final : public DBConn {
public:
    explicit CPledgeDbHelper(int worldId) : DBConn(worldId) {
        // Current Cached.exe unnamed constructor body 0x00510c28 calls the
        // DBConn base with the request-world selector and zeroes the complete
        // 0xc8-byte result carrier. The host carrier uses value-initialized
        // fields instead of pretending to share its Win64 physical layout.
    }

    void BindProperty() {
        L2_GUARD_FUNCTION();
        UnBind();
        Bind(&m_property.pledgeId);
        Bind(m_property.pledgeName.data(), 0x30);
        Bind(&m_property.rulerId);
        Bind(m_property.rulerName.data(), 0x30);
        BindNullable(&m_property.allianceId, &m_property.nullableIndicator);
        BindNullable(&m_property.challengeTime, &m_property.nullableIndicator);
        BindNullable(&m_property.nowWarId, &m_property.nullableIndicator);
        BindNullable(&m_property.nameValue, &m_property.nullableIndicator);
        BindNullable(&m_property.oustTime, &m_property.nullableIndicator);
        BindNullable(&m_property.skillLevel, &m_property.nullableIndicator);
        BindNullable(&m_property.privateFlag, &m_property.nullableIndicator);
        BindNullable(&m_property.status, &m_property.nullableIndicator);
        BindNullable(&m_property.rank, &m_property.nullableIndicator);
        BindNullable(&m_property.castleId, &m_property.nullableIndicator);
        BindNullable(&m_property.agitId, &m_property.nullableIndicator);
        BindNullable(&m_property.fortressId, &m_property.nullableIndicator);
        BindNullable(&m_property.rootNameValue, &m_property.nullableIndicator);
        BindNullable(&m_property.crestId, &m_property.nullableIndicator);
        BindNullable(&m_property.isGuilty, &m_property.nullableIndicator);
        BindNullable(&m_property.dismissReservedTime, &m_property.nullableIndicator);
        BindNullable(&m_property.allianceOustedTime, &m_property.nullableIndicator);
        BindNullable(&m_property.allianceWithdrawTime, &m_property.nullableIndicator);
        BindNullable(&m_property.allianceDismissTime, &m_property.nullableIndicator);
        BindNullable(&m_property.newEmblemId, &m_property.nullableIndicator);
        BindNullable(&m_property.castleSiegeDefenceCount, &m_property.nullableIndicator);
        // Current SQL column order is world, then mastery reset. The CPledge
        // constructor receives those final two values reversed.
        BindNullable(&m_property.worldId, &m_property.nullableIndicator);
        BindNullable(&m_property.masteryResetTime, &m_property.nullableIndicator);
    }

    CPledgeSP MakeNewPledgeObject() {
        L2_GUARD_FUNCTION();
        CPledge* const pledge = new CPledge(
            m_property.pledgeId, m_property.pledgeName.data(), m_property.rulerId, m_property.rulerName.data(),
            m_property.allianceId, m_property.challengeTime, m_property.nowWarId, m_property.nameValue, m_property.oustTime,
            m_property.skillLevel, m_property.privateFlag, m_property.status, m_property.rank, m_property.castleId,
            m_property.agitId, m_property.fortressId, m_property.rootNameValue, 0, m_property.crestId, m_property.isGuilty,
            m_property.dismissReservedTime, m_property.allianceOustedTime, m_property.allianceWithdrawTime,
            m_property.allianceDismissTime, m_property.newEmblemId, m_property.castleSiegeDefenceCount,
            m_property.masteryResetTime, m_property.worldId
        );
        pledge->LoadMergedInfo();
        pledge->LoadPledgeBonus();
        return CPledgeSP(pledge, ObjectRefType::FunctionChain);
    }

    int GetPledgeCountById(std::uint32_t pledgeId) {
        L2_GUARD_FUNCTION();
        int count = 0;
        Bind(&count);
        constexpr const wchar_t* statement = L"{Call lin_GetPledgeCountById (?)}";
        Prepare(statement);
        BindParameter(&pledgeId);
        if (!ExecutePrepared(statement) || !Fetch())
            return -1;
        return count;
    }

    int GetPledgeCountByName(wchar_t* pledgeName) {
        L2_GUARD_FUNCTION();
        int count = 0;
        Bind(&count);
        constexpr const wchar_t* statement = L"{Call lin_GetPledgeCountByName (?)}";
        Prepare(statement);
        BindParameter(pledgeName);
        if (!ExecutePrepared(statement) || !Fetch())
            return -1;
        return count;
    }

    CPledgeSP Load(std::uint32_t pledgeId) {
        L2_GUARD_FUNCTION();
        if (GetPledgeCountById(pledgeId) <= 0)
            return {};
        BindProperty();
        constexpr const wchar_t* statement = L"{Call lin_LoadPledgeByIdWithNewEmblem (?)}";
        Prepare(statement);
        BindParameter(&pledgeId);
        if (!ExecutePrepared(statement) || !Fetch())
            return {};
        return MakeNewPledgeObject();
    }

    CPledgeSP Load(wchar_t* pledgeName) {
        L2_GUARD_FUNCTION();
        if (GetPledgeCountByName(pledgeName) <= 0)
            return {};
        BindProperty();
        constexpr const wchar_t* statement = L"{Call lin_LoadPledgeByName (?)}";
        Prepare(statement);
        BindParameter(pledgeName);
        if (!ExecutePrepared(statement) || !Fetch())
            return {};
        return MakeNewPledgeObject();
    }

private:
    CPledgeProperty m_property;
};

CPledgeDb g_pledgeDb;

CPledgeDb::CPledgeDb() = default;

CPledgeDb::~CPledgeDb() = default;

int CPledgeDb::CreatePledge(wchar_t* pledgeName, int rulerId) {
    // Current Cached.exe 0x00519790.
    L2_GUARD_FUNCTION();

    CPledgeSP existing = GetPledge(pledgeName, true);
    if (existing)
        return -1;

    const int worldId = runtime::process::CurrentDatabaseId();
    int pledgeId = 0;
    std::array<char16_t, 24> rulerName{};
    DBConn connection(worldId);
    connection.Bind(&pledgeId);
    connection.Bind(rulerName.data(), 0x30);
    constexpr const wchar_t* statement = L"{Call lin_CreatePledge (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(pledgeName);
    connection.BindParameter(&rulerId);
    connection.BindParameter(&worldId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch() || pledgeId == 0) {
        return -1;
    }

    CPledge* const pledge = new CPledge(
        static_cast<std::uint32_t>(pledgeId), pledgeName != nullptr ? WideToUtf16(pledgeName).c_str() : nullptr, static_cast<std::uint32_t>(rulerId), rulerName.data(), worldId
    );
    pledge->AddMember(static_cast<std::uint32_t>(rulerId), MASTER_PLEDGE);
    pledge->AddSubPledge(SUB_1, 0, L"elite", 0);
    InsertIntoMap(static_cast<std::uint32_t>(pledgeId), pledgeName, pledge);
    L2_RELEASE(pledge, ObjectRefType::CreateDelete);
    return pledgeId;
}

void CPledgeDb::ItemCheckOnAllLoadedPledge() {
    L2_GUARD_FUNCTION();
    // if (!g_bUseDBSecurity)
    //     return.
    if (!g_bUseDBSecurity) {
        // Log.Add(LOG_NORMAL,
        //     "[disconnect cleanup] Skipping clan item checks: database security is disabled.")
        L2_LOG_INFO(Log,
            "[disconnect cleanup] Skipping clan item checks: database security is disabled.");
        return;
    }

    const std::int32_t loadedPledges = m_pledgesByWorldAndId.GetSize();
    std::int32_t expected = -1;
    const bool firstInvocation = g_nPendingPledgeItemChecks.compare_exchange_strong(
        expected, loadedPledges, std::memory_order_seq_cst, std::memory_order_seq_cst
    );
    if (!firstInvocation)
        return;

    // Log.Add(LOG_NORMAL,
    //     "[disconnect cleanup] Queuing item-check callbacks for %d loaded clans.",
    //     static_cast<int>(loadedPledges))
    L2_LOG_INFO(Log,
        "[disconnect cleanup] Queuing item-check callbacks for %d loaded clans.",
        static_cast<int>(loadedPledges));
    struct PostItemCheck {
        bool Do(CPledge* pledge) {
            (new CItemCheckOnPledgeLoadedMessage(pledge))->Post(0);
            return true;
        }
    } post;
    m_pledgesByWorldAndId.FOR_ALL(&post, L"PledgeDb.cpp", 0x6c7);
}

bool CPledgeDb::SetUserPledgeInfo(std::uint32_t characterId, std::uint32_t fieldSelector, std::uint32_t newValue) {
    L2_GUARD_FUNCTION();

    const wchar_t* fieldName = nullptr;
    switch (fieldSelector) {
    case 0:
        fieldName = L"pledge_dismiss_time";
        break;
    case 1:
        fieldName = L"pledge_ousted_time";
        break;
    case 2:
        fieldName = L"pledge_withdraw_time";
        break;
    case 3:
        fieldName = L"surrender_war_id";
        break;
    default:
        return false;
    }

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_UpdatePledgeInfo (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(fieldName);
    connection.BindParameter(&newValue);
    connection.BindParameter(&characterId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Error in Updating Field user_data[%u]", characterId)
        // Log.Add(LOG_ERROR,
        //     "[clan] Cannot update character %u field '%s' to %u in database %d. Cached character data not updated.",
        //     characterId, WideToUtf8(fieldName).c_str(), newValue, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[clan] Cannot update character %u field '%s' to %u in database %d; cached character data not updated.",
            characterId, WideToUtf8(fieldName).c_str(), newValue, runtime::process::CurrentDatabaseId());
        return false;
    }

    CUserSP user = g_userDb.GetUser(static_cast<int>(characterId), true, false, runtime::process::CurrentDatabaseId());
    if (!user)
        return true;

    switch (fieldSelector) {
    case 0:
        user->m_pledgeDismissTime = newValue;
        break;
    case 1:
        user->m_pledgeOustedTime = newValue;
        break;
    case 2:
        user->m_pledgeWithdrawTime = newValue;
        break;
    default:
        // Current 0x00514364 persists surrender_war_id but has no matching
        // in-memory CUser write in this generation.
        break;
    }
    return true;
}

bool CPledgeDb::SetPledgeInfo(std::uint32_t pledgeId, std::uint32_t fieldSelector, int newValue) {
    L2_GUARD_FUNCTION();

    static constexpr std::array<const wchar_t*, 22> fieldNames = {L"alliance_id", L"challenge_time", L"root_name_value",
        L"now_war_id", L"oust_time", L"skill_level", L"castle_id", L"agit_id", L"fortress_id", L"rank", L"name_value",
        L"status", L"private_flag", L"crest_id", L"is_guilty", L"dismiss_reserved_time", L"alliance_ousted_time",
        L"alliance_withdraw_time", L"alliance_dismiss_time", L"ruler_id", L"new_emblem_id", L"mastery_reset_time"};
    if (fieldSelector >= fieldNames.size())
        return false;
    const wchar_t* const fieldName = fieldNames[fieldSelector];

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_SetPledgeInfo (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(fieldName);
    connection.BindParameter(&newValue);
    connection.BindParameter(&pledgeId);
    if (!connection.ExecutePrepared(statement)) {
        const std::string fieldForLog = WideToUtf8(fieldName);
        // Log.Add(
        //     LOG_ERROR, "Error in Updating Field[%s] Pledge[%u], UpdateValue[%d]", fieldForLog.c_str(), pledgeId,
        //     newValue
        // )
        // Log.Add(LOG_ERROR,
        //     "[clan] Cannot update clan %u field '%s' to %d in database %d. Cached clan data not updated.",
        //     pledgeId, fieldForLog.c_str(), newValue, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[clan] Cannot update clan %u field '%s' to %d in database %d; cached clan data not updated.",
            pledgeId, fieldForLog.c_str(), newValue, runtime::process::CurrentDatabaseId());
        return false;
    }

    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = GetPledge(static_cast<int>(pledgeId), false, worldId);
    if (pledge) {
        switch (fieldSelector) {
        case 0:
            pledge->m_allianceId = newValue;
            break;
        case 1:
            pledge->m_challengeTime = newValue;
            break;
        case 2:
            pledge->m_rootNameValue = newValue;
            break;
        case 3:
            pledge->m_nowWarId = newValue;
            break;
        case 4:
            pledge->m_oustTime = newValue;
            break;
        case 5: {
            pledge->m_skillLevel = newValue;
            const bool ranked = CPledgeRanking::IsInRanking(pledgeId);
            if (!ranked && pledge->m_skillLevel >= 5U) {
                CPledgeRanking::Insert(pledgeId, pledge->m_rootNameValue);
            } else if (pledge->m_skillLevel < 5U) {
                CPledgeRanking::Remove(pledgeId);
            }
            break;
        }
        case 6:
            pledge->m_castleId = newValue;
            break;
        case 7:
            pledge->m_agitId = newValue;
            break;
        case 8:
            pledge->m_fortressId = newValue;
            break;
        case 9:
            pledge->m_rank = newValue;
            break;
        case 10:
            pledge->m_nameValue = newValue;
            break;
        case 11:
            pledge->m_status = newValue;
            break;
        case 12:
            pledge->m_privateFlag = newValue;
            break;
        case 13:
            pledge->m_crestId = newValue;
            break;
        case 14:
            pledge->m_isGuilty = newValue;
            break;
        case 15:
            pledge->m_dismissReservedTime = newValue;
            break;
        case 16:
            pledge->m_allianceOustedTime = newValue;
            break;
        case 17:
            pledge->m_allianceWithdrawTime = newValue;
            break;
        case 18:
            pledge->m_allianceDismissTime = newValue;
            break;
        case 19:
            pledge->m_rulerId = newValue;
            break;
        case 20:
            pledge->m_newEmblemId = newValue;
            break;
        case 21:
            pledge->m_masteryResetTime = newValue;
            break;
        default:
            break;
        }
    }

    if (fieldSelector == 11U) {
        // The shipping body re-fetches from the process-global
        // database instead of reusing the first smart pointer.
        CPledgeSP statusPledge = g_pledgeDb.GetPledge(static_cast<int>(pledgeId), false, worldId);
        if (statusPledge) {
            CUserSP ruler = g_userDb.GetUser(static_cast<int>(statusPledge->m_rulerId), true, false, worldId);
            if (ruler && (newValue == 3 || newValue == 0)) {
                const std::u16string characterName = std::u16string(ruler->GetCharName());
                const std::u16string accountName = std::u16string(ruler->GetAccountName());
                CLogFormatter formatter;
                formatter.WriteLog(
                    newValue == 3 ? 0xec : 0xf7, ruler->GetId(), ruler->GetAccountID(), 0, 0, ruler->GetX(), ruler->GetY(),
                    ruler->GetZ(), statusPledge->GetName(), u"", u"", ruler->GetRace(), ruler->GetGender(),
                    ruler->GetClass(), ruler->GetLevel(), 0, pledgeId, 0, 0, 0, 0, characterName.c_str(),
                    accountName.c_str(), u"", u"", 0
                );
            }
        }
    }

    if (fieldSelector == 5U) {
        // This second lookup and GIP emission are also unconditional after a
        // successful SQL update, even when the first lookup missed.
        CPledgeSP skillPledge = g_pledgeDb.GetPledge(static_cast<int>(pledgeId), false, worldId);
        if (skillPledge) {
            CUserSP ruler = g_userDb.GetUser(static_cast<int>(skillPledge->m_rulerId), true, false, worldId);
            const std::wstring pledgeName = Utf16ToWide(std::u16string_view(skillPledge->GetName()));
            GIP._Add(
                L"CMD_ClanInfoData(%d, %d, '%s', %d, %d, %d, %d, %d, '%s'", GetCacheDConfiguration().worldId, pledgeId,
                pledgeName.c_str(), static_cast<std::int64_t>(std::time(nullptr)), skillPledge->m_skillLevel,
                skillPledge->m_nameValue, ruler ? ruler->GetAccountID() : 0, ruler ? ruler->GetId() : 0,
                ruler ? Utf16ToWide(ruler->GetCharName()).c_str() : L""
            );
        }
    }

    return true;
}

bool CPledgeDb::SigninPledge(std::uint32_t pledgeId, std::uint32_t characterId, PledgeType pledgeType) {
    // Current Cached.exe 0x00519c4c. This is a separate shipping operation
    // from JoinPledgeMember: its SQL result must be exactly one and it holds
    // the pledge lock from PledgeDb.cpp:0x45b throughout publication.
    L2_GUARD_FUNCTION();

    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = GetPledge(static_cast<int>(pledgeId), false, worldId);
    if (!pledge)
        return false;

    std::optional<CUserSP> userStorage;
    bool signedIn;
    {
        L2_WRITE_GUARD(pledge->m_pledgeLock);
        CUserSP& user = userStorage.emplace(g_userDb.GetUser(static_cast<int>(characterId), true, false, worldId));
        if (!user || user->GetPledgeID() != 0U) {
            return false;
        }

        signedIn = false;
        {
            int result = 0;
            DBConn connection(worldId);
            connection.Bind(&result);
            constexpr const wchar_t* signinStatement = L"{Call lin_SigninPledge (?, ?, ?)}";
            connection.Prepare(signinStatement);
            connection.BindParameter(&pledgeId);
            connection.BindParameter(&pledgeType);
            connection.BindParameter(&characterId);
            if (connection.ExecutePrepared(signinStatement)) {
                connection.Fetch();
            }

            if (result == 1) {
                if (pledgeType == ACADEMY) {
                    connection.UnBind();
                    const int joinLevel = user->GetLevel();
                    constexpr const wchar_t* academyStatement = L"{Call lin_JoinAcademyMember (?, ?, ?)}";
                    connection.Prepare(academyStatement);
                    connection.BindParameter(&characterId);
                    connection.BindParameter(&pledgeId);
                    connection.BindParameter(&joinLevel);
                    connection.ExecutePrepared(academyStatement);
                    pledge->AddAcademyMember(characterId, joinLevel, 0);
                }

                pledge->AddMember(characterId, pledgeType);
                user->SetPledge(static_cast<int>(pledgeId), pledgeType);

                int gradeId = 0;
                if (pledgeType == ACADEMY) {
                    gradeId = 9;
                } else if (pledgeType == MASTER_PLEDGE) {
                    gradeId = 6;
                } else if (pledgeType == SUB_1 || pledgeType == SUB_2) {
                    gradeId = 7;
                } else if (static_cast<int>(pledgeType) > 1000 &&
                    (static_cast<int>(pledgeType) < 1003 ||
                        (static_cast<int>(pledgeType) > 2000 && static_cast<int>(pledgeType) < 2003))) {
                    gradeId = 8;
                } else {
                    // Log.Add(
                    //     LOG_ERROR, "[PledgeDb.cpp][%d] invalid pledge type [%d]", 0x495, static_cast<int>(pledgeType)
                    // )
                    // Log.Add(LOG_ERROR,
                    //     "[clan] Character %u joined clan %u in world %d, but subgroup type %d has no default rank. Character rank left unchanged.",
                    //     characterId, pledgeId, worldId, static_cast<int>(pledgeType))
                    L2_LOG_ERROR(Log,
                        "[clan] Character %u joined clan %u in world %d, but subgroup type %d has no default rank; character rank left unchanged.",
                        characterId, pledgeId, worldId, static_cast<int>(pledgeType));
                }

                if (gradeId != 0) {
                    g_userDb.SetPowerGrade(static_cast<int>(characterId), gradeId);
                }
                signedIn = true;
            }
        }
    }
    return signedIn;
}

bool CPledgeDb::JoinPledgeMember(std::uint32_t pledgeId, std::uint32_t characterId, PledgeType pledgeType) {
    L2_GUARD_FUNCTION();

    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = GetPledge(static_cast<int>(pledgeId), false, worldId);
    if (!pledge)
        return false;

    // Current 0x0051a218 holds CPledge+0x120 across the user lookup, SQL and
    // both in-memory publications. This is the general pledge lock kept
    // by the older source owner, not a skill-only lock.
    std::optional<CUserSP> userStorage;
    bool joined;
    {
        L2_WRITE_GUARD(pledge->m_pledgeLock);
        CUserSP& user = userStorage.emplace(g_userDb.GetUser(static_cast<int>(characterId), true, false, worldId));
        if (!user || user->GetPledgeID() != 0U) {
            return false;
        }

        joined = false;
        {
            // The shipping stack slot is not initialized, but every
            // known procedure body returns one row. Zero is the deterministic
            // portable value for an execution/fetch failure instead of exposing
            // target stack garbage as cross-platform behaviour.
            int result = 0;
            DBConn connection(worldId);
            connection.Bind(&result);
            constexpr const wchar_t* joinStatement = L"{Call lin_JoinPledgeMember (?, ?, ?)}";
            connection.Prepare(joinStatement);
            connection.BindParameter(&pledgeId);
            connection.BindParameter(&pledgeType);
            connection.BindParameter(&characterId);
            if (connection.ExecutePrepared(joinStatement)) {
                connection.Fetch();
            }

            if (result != 0) {
                if (pledgeType == ACADEMY) {
                    connection.UnBind();
                    const int joinLevel = user->GetLevel();
                    constexpr const wchar_t* academyStatement = L"{Call lin_JoinAcademyMember (?, ?, ?)}";
                    connection.Prepare(academyStatement);
                    connection.BindParameter(&characterId);
                    connection.BindParameter(&pledgeId);
                    connection.BindParameter(&joinLevel);
                    connection.ExecutePrepared(academyStatement);
                    pledge->AddAcademyMember(characterId, joinLevel, 0);
                }

                pledge->AddMember(characterId, pledgeType);
                user->SetPledge(static_cast<int>(pledgeId), pledgeType);

                int gradeId = 0;
                if (pledgeType == ACADEMY) {
                    gradeId = 9;
                } else if (pledgeType == MASTER_PLEDGE) {
                    gradeId = 6;
                } else if (pledgeType == SUB_1 || pledgeType == SUB_2) {
                    gradeId = 7;
                } else if (static_cast<int>(pledgeType) > 1000 &&
                    (static_cast<int>(pledgeType) < 1003 ||
                        (static_cast<int>(pledgeType) > 2000 && static_cast<int>(pledgeType) < 2003))) {
                    gradeId = 8;
                } else {
                    // Log.Add(
                    //     LOG_ERROR, "[PledgeDb.cpp][%d] invalid pledge type [%d]", 0x4d9, static_cast<int>(pledgeType)
                    // )
                    // Log.Add(LOG_ERROR,
                    //     "[clan] Character %u joined clan %u in world %d, but subgroup type %d has no default rank. Character rank left unchanged.",
                    //     characterId, pledgeId, worldId, static_cast<int>(pledgeType))
                    L2_LOG_ERROR(Log,
                        "[clan] Character %u joined clan %u in world %d, but subgroup type %d has no default rank; character rank left unchanged.",
                        characterId, pledgeId, worldId, static_cast<int>(pledgeType));
                }

                if (gradeId != 0) {
                    g_userDb.SetPowerGrade(static_cast<int>(characterId), gradeId);
                }
                joined = true;
            }
        }
    }
    return joined;
}

bool CPledgeDb::WithdrawPledgeMember(std::uint32_t pledgeId, std::uint32_t characterId, PledgeType pledgeType) {
    L2_GUARD_FUNCTION();

    const int worldId = runtime::process::CurrentDatabaseId();
    int result = 0;
    {
        DBConn connection(worldId);
        connection.Bind(&result);
        constexpr const wchar_t* statement = L"{Call lin_WithdrawPledgeMember (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&pledgeId);
        connection.BindParameter(&characterId);
        connection.BindParameter(&pledgeType);
        if (connection.ExecutePrepared(statement)) {
            connection.Fetch();
        }
    }
    if (result == 0)
        return false;

    CPledgeSP pledge = GetPledge(static_cast<int>(pledgeId), false, worldId);
    if (pledge) {
        pledge->RemoveMember(characterId, pledgeType);
        if (pledgeType != ACADEMY && pledge->IsSubPledgeMaster(characterId)) {
            const PledgeType subPledgeType = pledge->GetSubMasterPledgeType(characterId);
            UpdateSubPledgeMaster(pledgeId, subPledgeType, 0);
        }
    }

    CUserSP user = g_userDb.GetUser(static_cast<int>(characterId), false, false, worldId);
    if (!user) {
        // Log.Add(
        //     LOG_ERROR,
        //     "WithdrawPledge is strange.(pledge_id=%u,char_id=%u,"
        //     "pledge_type=%d,nResult=%d)",
        //     pledgeId, characterId, static_cast<int>(pledgeType), result
        // )
        // Log.Add(LOG_ERROR,
        //     "[clan] Database accepted withdrawal of character %u from clan %u (world %d, subgroup %d, result %d), but the character could not be loaded to clear cached membership.",
        //     characterId, pledgeId, worldId, static_cast<int>(pledgeType), result)
        L2_LOG_ERROR(Log,
            "[clan] Database accepted withdrawal of character %u from clan %u (world %d, subgroup %d, result %d), but the character could not be loaded to clear cached membership.",
            characterId, pledgeId, worldId, static_cast<int>(pledgeType), result);
    } else {
        user->SetPledge(0, MASTER_PLEDGE);
        // Current 0x0051d7ec writes CUser+0x614 directly after SetPledge.
        user->m_gradeId = 0;
    }
    return true;
}

std::uint32_t CPledgeDb::OustPledgeMember(std::uint32_t pledgeId, const wchar_t* characterName) {
    // Current Cached.exe 0x0051d068.
    L2_GUARD_FUNCTION();

    CUserSP user = g_userDb.GetUser(WideToUtf16(characterName).c_str(), false);
    if (!user)
        return 0;

    int ignoredResult = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&ignoredResult);
    const PledgeType pledgeType = user->GetPledgeType();
    constexpr const wchar_t* statement = L"{Call lin_OustPledgeMember (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&pledgeType);
    connection.BindParameter(characterName);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        return 0;
    }

    // The bound procedure result is not inspected by the
    // shipping method. Execute+Fetch is the only SQL success gate.
    const std::uint32_t characterId = user->GetId();
    CPledgeSP pledge = GetPledge(static_cast<int>(pledgeId), false, runtime::process::CurrentDatabaseId());
    if (pledge) {
        pledge->RemoveMember(characterId, pledgeType);
        if (pledgeType != ACADEMY && pledge->IsSubPledgeMaster(characterId)) {
            const PledgeType subPledgeType = pledge->GetSubMasterPledgeType(characterId);
            UpdateSubPledgeMaster(pledgeId, subPledgeType, 0);
        }
    }
    return characterId;
}

bool CPledgeDb::GraduateAcademyMember(std::uint32_t pledgeId, std::uint32_t memberId) {
    // Current Cached.exe 0x0051a7e4.
    L2_GUARD_FUNCTION();

    const int worldId = runtime::process::CurrentDatabaseId();
    int result = 0;
    DBConn connection(worldId);
    connection.Bind(&result);
    constexpr PledgeType pledgeType = ACADEMY;
    constexpr const wchar_t* statement = L"{Call lin_GraduatePledgeMember (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&memberId);
    connection.BindParameter(&pledgeType);
    if (connection.ExecutePrepared(statement)) {
        // Current code ignores Fetch's return and tests the bound value.
        connection.Fetch();
    }
    if (result == 0)
        return false;

    CPledgeSP pledge = GetPledge(static_cast<int>(pledgeId), false, worldId);
    if (pledge && pledge->GraduateAcademyMember(memberId)) {
        LoadGraduateAcademyCount(pledge.get());
    }

    CUserSP user = g_userDb.GetUser(static_cast<int>(memberId), false, false, worldId);
    if (user) {
        user->SetPledge(0, MASTER_PLEDGE);
        user->m_gradeId = 0;
        if (pledge) {
            user->m_academyPledgeId = static_cast<int>(pledgeId);
        }
    }
    return true;
}

bool CPledgeDb::AcquirePledgeSkill(std::uint32_t pledgeId, std::uint32_t skillId, int level, std::int32_t expirationTime) {
    // Current Cached.exe 0x0051aad8. The final AddPledgeSkill flag is the
    // cleared low byte of the TLS world-id carrier, hence exactly false.
    L2_GUARD_FUNCTION();

    CPledgeSP pledge = GetPledge(static_cast<int>(pledgeId), false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        // Log.AddT(LOG_ERROR, L"[%S][%d]Cannot find pledge, id[%d]", "PledgeDb.cpp", 0x552, pledgeId)
        // Log.Add(LOG_ERROR,
        //     "[clan] Cannot acquire skill %u level %d for clan %u: clan unavailable in database %d.",
        //     skillId, level, pledgeId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[clan] Cannot acquire skill %u level %d for clan %u: clan unavailable in database %d.",
            skillId, level, pledgeId, runtime::process::CurrentDatabaseId());
        return false;
    }
    return pledge->AddPledgeSkill(skillId, level, expirationTime, false);
}

bool CPledgeDb::SetPowerGrade(std::uint32_t pledgeId, int gradeId, std::uint32_t power) {
    // Current Cached.exe 0x0051ac5c.
    L2_GUARD_FUNCTION();

    if (static_cast<std::uint32_t>(gradeId) - 1U >= 9U) {
        return false;
    }

    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = GetPledge(static_cast<int>(pledgeId), false, worldId);
    if (!pledge) {
        // Log.AddT(LOG_ERROR, L"[%S][%d]Cannot find pledge, id[%d]", "PledgeDb.cpp", 0x58b, pledgeId)
        // Log.Add(LOG_ERROR,
        //     "[clan] Cannot set permissions %u for rank %d of clan %u: clan unavailable in world %d.",
        //     power, gradeId, pledgeId, worldId)
        L2_LOG_ERROR(Log,
            "[clan] Cannot set permissions %u for rank %d of clan %u: clan unavailable in world %d.",
            power, gradeId, pledgeId, worldId);
        return false;
    }

    DBConn connection(worldId);
    constexpr const wchar_t* statement = L"{Call lin_SetPowerGrade (?, ?, ?)}";
    // Current unsigned-long helper 0x00513158 carries the 32-bit value as
    // SQL_C_ULONG/SQL_DOUBLE. Host unsigned long may be wider. DBConn's
    // recovered overload narrows its numeric value to the target width.
    const unsigned long powerParameter = static_cast<unsigned long>(power);
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&gradeId);
    connection.BindParameter(&powerParameter);
    if (!connection.ExecutePrepared(statement)) {
        return false;
    }

    const std::uint32_t previousPower = pledge->GetPowerByGradeId(gradeId);
    pledge->SetPowerGrade(gradeId, power);

    // The target's direct CLog record places the pledge name in EtcStr2 and
    // records grade, new power, then old power in EtcNum1..3.
    CLogFormatter formatter;
    formatter.WriteLog(
        0x119, pledgeId, 0, 0, 0, 0, 0, 0, u"", pledge->GetName(), u"", gradeId, power, previousPower, 0, 0, 0, 0, 0, 0, 0,
        u"", u"", u"", u"", 0
    );
    return true;
}

bool CPledgeDb::UpdateSubPledgeMaster(std::uint32_t pledgeId, PledgeType pledgeType, std::uint32_t masterId) {
    L2_GUARD_FUNCTION();
    if (pledgeType == ACADEMY || pledgeType == MASTER_PLEDGE) {
        return false;
    }

    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = GetPledge(static_cast<int>(pledgeId), false, worldId);
    if (!pledge)
        return false;

    CUserSP oldMaster = pledge->GetSubpledgeMaster(pledgeType);
    // The target changes memory before SQL and does not roll it
    // back if lin_SetSubPledgeMaster fails.
    if (!pledge->UpdateSubPledgeMaster(pledgeType, masterId)) {
        return false;
    }

    DBConn connection(worldId);
    constexpr const wchar_t* statement = L"{Call lin_SetSubPledgeMaster (?, ?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&masterId);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&pledgeType);
    if (!connection.ExecutePrepared(statement))
        return false;

    CUserSP newMaster;
    if (masterId != 0U) {
        newMaster = g_userDb.GetUser(static_cast<int>(masterId), true, false, worldId);
    }

    const std::u16string oldCharacterName = oldMaster ? std::u16string(oldMaster->GetCharName()) : std::u16string{};
    const std::u16string oldAccountName = oldMaster ? std::u16string(oldMaster->GetAccountName()) : std::u16string{};
    const std::u16string newCharacterName = newMaster ? std::u16string(newMaster->GetCharName()) : std::u16string{};
    const std::u16string newAccountName = newMaster ? std::u16string(newMaster->GetAccountName()) : std::u16string{};
    CLogFormatter formatter;
    formatter.WriteLog(
        0x114, oldMaster ? oldMaster->GetId() : 0, oldMaster ? oldMaster->GetAccountID() : 0,
        newMaster ? newMaster->GetId() : 0, newMaster ? newMaster->GetAccountID() : 0, 0, 0, 0, pledge->GetName(),
        pledge->GetSubpledgeName(pledgeType), u"", static_cast<int>(pledgeType), 0, 0, 0, 0, 0, 0, 0, 0, 0,
        oldCharacterName.c_str(), oldAccountName.c_str(), newCharacterName.c_str(), newAccountName.c_str(), 0
    );
    return true;
}

bool CPledgeDb::UpgradeSubPledgeMemberCount(
    std::uint32_t pledgeId [[maybe_unused]], PledgeType pledgeType [[maybe_unused]], int memberCount [[maybe_unused]]
) {
    L2_GUARD_FUNCTION();
    // Log.Add(LOG_ERROR, "[PledgeDb.cpp][%d]UpgradeSubPledgeMemberCount deprecated", 0x5d7)
    // Log.Add(LOG_WARN,
    //     "[clan] Subgroup capacity upgrade is unsupported. Request rejected for clan %u, subgroup %d, requested capacity %d.",
    //     pledgeId, static_cast<int>(pledgeType), memberCount)
    L2_LOG_WARN(Log,
        "[clan] Subgroup capacity upgrade is unsupported; request rejected for clan %u, subgroup %d, requested capacity %d.",
        pledgeId, static_cast<int>(pledgeType), memberCount);
    runtime::process::ReportNamedCallStack(L"UpgradeSubPledgeMemberCount", 2, false);
    return false;
}

int CPledgeDb::RequestPledgeMasterTransfer(std::uint32_t pledgeId, wchar_t* newMasterName) {
    // Current Cached.exe 0x0051b744.
    L2_GUARD_FUNCTION();

    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = GetPledge(static_cast<int>(pledgeId), false, worldId);
    if (!pledge)
        return -3;

    CUserSP newMaster = g_userDb.GetUser(WideToUtf16(newMasterName).c_str(), false);
    if (!newMaster)
        return -4;
    if (newMaster->GetPledgeID() == 0U)
        return -5;
    if (newMaster->GetPledgeID() != pledgeId)
        return -6;

    // Current 0x0051b9ed accepts precisely these two numeric values. The
    // surviving evidence does not keep a trustworthy source spelling
    // for the second value, so do not manufacture one here.
    const int pledgeType = static_cast<int>(newMaster->GetPledgeType());
    if (pledgeType != 0 && pledgeType != 100)
        return -7;

    int result = 0;
    const int newMasterId = static_cast<int>(newMaster->GetId());
    DBConn connection(worldId);
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_RequestPledgeMasterTransfer (?, ?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    connection.BindParameter(&newMasterId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch() || result < 1) {
        return -8;
    }

    CUserSP oldMaster = g_userDb.GetUser(static_cast<int>(pledge->m_rulerId), true, false, worldId);
    if (oldMaster) {
        const std::u16string oldCharacterName = std::u16string(oldMaster->GetCharName());
        const std::u16string oldAccountName = std::u16string(oldMaster->GetAccountName());
        const std::u16string newCharacterName = std::u16string(newMaster->GetCharName());
        const std::u16string newAccountName = std::u16string(newMaster->GetAccountName());
        CLogFormatter formatter;
        formatter.WriteLog(
            0x11f, oldMaster->GetId(), oldMaster->GetAccountID(), newMaster->GetId(), newMaster->GetAccountID(), 0, 0, 0,
            u"", pledge->GetName(), u"", pledgeId, 0, 0, 0, 0, 0, 0, 0, 0, 0, oldCharacterName.c_str(),
            oldAccountName.c_str(), newCharacterName.c_str(), newAccountName.c_str(), 0
        );
    }
    return 1;
}

int CPledgeDb::RequestPledgeMasterTransferCancel(std::uint32_t pledgeId) {
    // Current Cached.exe 0x0051bf6c.
    L2_GUARD_FUNCTION();

    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = GetPledge(static_cast<int>(pledgeId), false, worldId);
    if (!pledge)
        return -3;

    int result = 0;
    DBConn connection(worldId);
    connection.Bind(&result);
    constexpr const wchar_t* statement = L"{Call lin_RequestPledgeMasterTransferCancel (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch() || result < 1) {
        return -4;
    }

    CUserSP ruler = g_userDb.GetUser(static_cast<int>(pledge->m_rulerId), true, false, worldId);
    if (ruler) {
        const std::u16string characterName = std::u16string(ruler->GetCharName());
        const std::u16string accountName = std::u16string(ruler->GetAccountName());
        CLogFormatter formatter;
        formatter.WriteLog(
            0x11f, ruler->GetId(), ruler->GetAccountID(), 0, 0, 0, 0, 0, u"", pledge->GetName(), u"", pledgeId, 1, 0, 0, 0,
            0, 0, 0, 0, 0, characterName.c_str(), accountName.c_str(), u"", u"", 0
        );
    }
    return 1;
}

int CPledgeDb::RenameSubpledge(std::uint32_t pledgeId, PledgeType pledgeType, wchar_t* newName) {
    // Current Cached.exe 0x0051c328.
    L2_GUARD_FUNCTION();

    if (pledgeType == MASTER_PLEDGE)
        return -3;

    CPledgeSP pledge = GetPledge(static_cast<int>(pledgeId), false, runtime::process::CurrentDatabaseId());
    if (!pledge)
        return -8;

    int result = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&result);
    if (pledgeType == ACADEMY) {
        constexpr const wchar_t* statement = L"{Call lin_RenameAcademy (?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&pledgeId);
        connection.BindParameter(newName);
        if (connection.ExecutePrepared(statement) && connection.Fetch() && result == 1) {
            CPledge::AssignFixedName(pledge->m_academyName, newName != nullptr ? WideToUtf16(newName).c_str() : nullptr, L"newName length < 24");
            return 1;
        }
    } else {
        constexpr const wchar_t* statement = L"{Call lin_RenameSubpledge (?, ?, ?)}";
        connection.Prepare(statement);
        connection.BindParameter(&pledgeId);
        connection.BindParameter(&pledgeType);
        connection.BindParameter(newName);
        if (connection.ExecutePrepared(statement) && connection.Fetch() && result == 1 &&
            pledge->SetSubpledgeName(pledgeType, newName)) {
            return 1;
        }
    }
    return -9;
}

void CPledgeDb::TransferPledgeMasterBatch() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_TransferPledgeMaster}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Pledge Master Transfer has failed")
        // Log.Add(LOG_ERROR,
        //     "[clan] Leader-transfer database batch failed in database %d.",
        //     runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[clan] Leader-transfer database batch failed in database %d.",
            runtime::process::CurrentDatabaseId());
        return;
    }
    // Log.Add(LOG_NORMAL, "Pledge Master Transfer is completed")
    // Log.Add(LOG_NORMAL,
    //     "[clan] Leader-transfer database batch executed in database %d.",
    //     runtime::process::CurrentDatabaseId())
    L2_LOG_INFO(Log,
        "[clan] Leader-transfer database batch executed in database %d.",
        runtime::process::CurrentDatabaseId());
}

void CPledgeDb::ChangeNameByMergeBatch() {
    L2_GUARD_FUNCTION();

    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_ChangeNameByMergeBatch}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Pledge Name Change is faild")
        // Log.Add(LOG_ERROR,
        //     "[clan] Merge name-change database batch failed in database %d.",
        //     runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[clan] Merge name-change database batch failed in database %d.",
            runtime::process::CurrentDatabaseId());
        return;
    }
    // Log.Add(LOG_NORMAL, "Pledge Name Change is completed")
    // Log.Add(LOG_NORMAL,
    //     "[clan] Merge name-change database batch executed in database %d.",
    //     runtime::process::CurrentDatabaseId())
    L2_LOG_INFO(Log,
        "[clan] Merge name-change database batch executed in database %d.",
        runtime::process::CurrentDatabaseId());
}

bool CPledgeDb::LoadPledgePower(CPledge* pledge) {
    L2_GUARD_FUNCTION();
    int gradeId = 0;
    std::uint32_t power = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&gradeId);
    connection.Bind(&power);
    constexpr const wchar_t* statement = L"{Call lin_GetPledgePower (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledge->m_pledgeId);
    if (!connection.ExecutePrepared(statement))
        return false;
    while (connection.Fetch())
        pledge->SetPowerGrade(gradeId, power);
    return true;
}

bool CPledgeDb::LoadAcademy(CPledge* pledge) {
    L2_GUARD_FUNCTION();
    std::array<char16_t, 25> academyName{};
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(academyName.data(), 0x32);
    constexpr const wchar_t* statement = L"{Call lin_GetAcademyName (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledge->m_pledgeId);
    if (!connection.ExecutePrepared(statement))
        return false;
    if (connection.Fetch()) {
        pledge->m_academyExists = true;
        CPledge::AssignFixedName(pledge->m_academyName, academyName.data(), L"academyName length < 24");
    }
    return true;
}

bool CPledgeDb::LoadAcademyMember(CPledge* pledge) {
    L2_GUARD_FUNCTION();
    std::uint32_t memberId = 0;
    int joinLevel = 0;
    std::uint32_t academyMasterId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&memberId);
    connection.Bind(&joinLevel);
    connection.Bind(&academyMasterId);
    constexpr const wchar_t* statement = L"{Call lin_LoadAcademyMember (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledge->m_pledgeId);
    if (!connection.ExecutePrepared(statement))
        return false;
    while (connection.Fetch()) {
        pledge->AddAcademyMember(memberId, joinLevel, academyMasterId);
    }
    return true;
}

bool CPledgeDb::LoadGraduateAcademyCount(CPledge* pledge) {
    L2_GUARD_FUNCTION();
    int count = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&count);
    constexpr const wchar_t* statement = L"{Call lin_LoadGraduateAcademyCount (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledge->m_pledgeId);
    if (!connection.ExecutePrepared(statement) || !connection.Fetch()) {
        return false;
    }
    pledge->m_graduateAcademyCount = static_cast<std::uint32_t>(count);
    return true;
}

bool CPledgeDb::LoadPledgeSkill(CPledge* pledge) {
    L2_GUARD_FUNCTION();
    std::uint32_t skillId = 0;
    int level = 0;
    int expirationTime = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&skillId);
    connection.Bind(&level);
    connection.Bind(&expirationTime);
    constexpr const wchar_t* statement = L"{Call lin_LoadPledgeSkill (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledge->m_pledgeId);
    if (!connection.ExecutePrepared(statement))
        return false;

    {
        L2_WRITE_GUARD(pledge->m_pledgeLock);
        while (connection.Fetch()) {
            pledge->_AddPledgeSkillWithoutSql(skillId, level, expirationTime, false);
        }
    }
    return true;
}

bool CPledgeDb::LoadPledgeMember(CPledge* pledge) {
    L2_GUARD_FUNCTION();
    int memberId = 0;
    int pledgeType = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&memberId);
    connection.Bind(&pledgeType);
    constexpr const wchar_t* statement = L"{Call lin_LoadPledgeMember (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledge->m_pledgeId);
    if (!connection.ExecutePrepared(statement)) {
        // Log.Add(LOG_ERROR, "Error in Loading Pledge[%u] Member", pledge->m_pledgeId)
        // Log.Add(LOG_ERROR,
        //     "[clan] Member query failed for clan %u in database %d. Clan loading continues with incomplete member data.",
        //     pledge->m_pledgeId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[clan] Member query failed for clan %u in database %d; clan loading continues with incomplete member data.",
            pledge->m_pledgeId, runtime::process::CurrentDatabaseId());
        return false;
    }
    while (connection.Fetch()) {
        pledge->AddMember(static_cast<std::uint32_t>(memberId), static_cast<PledgeType>(pledgeType));
    }
    return true;
}

template <class Function>
void CPledge::ServeForAllMember(Function& function) {
    L2_GUARD_FUNCTION();
    for (const auto& pledgeTypeMembers : m_members) {
        for (const std::uint32_t memberId : pledgeTypeMembers.second) {
            function(memberId);
        }
    }
}

bool CPledgeDb::DeletePledge(std::uint32_t pledgeId) {
    // Current Cached.exe 0x00519a74 has no guard literal. The method name is
    // kept from the C1 source/PDB donor. The body and extra current
    // ranking/grade behavior below come from the current machine code.
    DBConn connection(runtime::process::CurrentDatabaseId());
    constexpr const wchar_t* statement = L"{Call lin_DeletePledge (?)}";
    connection.Prepare(statement);
    connection.BindParameter(&pledgeId);
    if (!connection.ExecutePrepared(statement))
        return false;

    CPledgeSP pledge = GetPledge(static_cast<int>(pledgeId), false, runtime::process::CurrentDatabaseId());
    if (!pledge) {
        // Log.Add(LOG_ERROR, "cannot find pledge. dbid[%u]", pledgeId)
        // Log.Add(LOG_ERROR,
        //     "[clan] Delete procedure executed for clan %u in database %d, but the clan could not be loaded for cache cleanup. Returning failure.",
        //     pledgeId, runtime::process::CurrentDatabaseId())
        L2_LOG_ERROR(Log,
            "[clan] Delete procedure executed for clan %u in database %d, but the clan could not be loaded for cache cleanup; returning failure.",
            pledgeId, runtime::process::CurrentDatabaseId());
        return false;
    }

    DeleteFromMap(pledge.get());

    struct SetNoPledge {
        void operator()(std::uint32_t memberId) const {
            CUserSP member = g_userDb.GetUser(static_cast<int>(memberId), true, false, runtime::process::CurrentDatabaseId());
            if (!member) {
                // Log.Add(LOG_ERROR, "[%s][%d]Cannot find user, id[%u]", "PledgeDb.cpp", 0x422, memberId)
                // Log.Add(LOG_ERROR,
                //     "[clan] Cannot clear cached membership after clan deletion: character %u could not be loaded in database %d. Continuing with other members.",
                //     memberId, runtime::process::CurrentDatabaseId())
                L2_LOG_ERROR(Log,
                    "[clan] Cannot clear cached membership after clan deletion: character %u could not be loaded in database %d; continuing with other members.",
                    memberId, runtime::process::CurrentDatabaseId());
                return;
            }
            member->SetPledge(0, MASTER_PLEDGE);
            member->m_gradeId = 0;
        }
    } setNoPledge;

    pledge->ServeForAllMember(setNoPledge);
    CPledgeRanking::Remove(pledgeId);
    return true;
}

void CPledgeDb::InsertIntoMap(std::uint32_t pledgeId, wchar_t* pledgeName, CPledge* pledge) {
    L2_GUARD_FUNCTION();
    const std::wstring lowercaseName = LowercasePledgeName(pledgeName);
    const int worldId = pledge->GetWorldId();
    m_pledgesByWorldAndId.Insert(CPledgeKey(worldId, static_cast<int>(pledgeId)), pledge, nullptr, -1);
    m_pledgesByName.Insert(worldId, WideToUtf16(lowercaseName), pledge, nullptr, -1);
}

void CPledgeDb::DeleteFromMap(CPledge* pledge) {
    L2_GUARD_FUNCTION();
    const int worldId = pledge->GetWorldId();
    m_pledgesByWorldAndId.DeleteObject(CPledgeKey(worldId, static_cast<int>(pledge->GetId())), nullptr, -1);
    m_pledgesByName.DeleteObject(worldId, WideToUtf16(LowercasePledgeName(pledge->GetName())), nullptr, -1);
}

CPledgeSP CPledgeDb::LoadPledgeInternal(CPledgeSP pledge) {
    L2_GUARD_FUNCTION();
    if (!pledge)
        return {};

    CPledge* const object = pledge.get();
    if (!LoadAcademy(object)) {
        // LogPledgeLoadFailure(0xfb, "LoadAcademy", object)
        LogPledgeLoadFailure(0xfb, "academy information", object);
    }
    object->AddSubPledge(SUB_1, 0, L"elite", 0);
    if (!LoadPledgeMember(object)) {
        // Noise: LoadPledgeMember already reports the failed query and incomplete clan load with clan/database context.
        // LogPledgeLoadFailure(0x109, "LoadPledgeMember", object)
    }
    if (!LoadAcademyMember(object)) {
        // LogPledgeLoadFailure(0x10d, "LoadAcademyMember", object)
        LogPledgeLoadFailure(0x10d, "academy members", object);
    }
    if (!LoadPledgePower(object)) {
        // LogPledgeLoadFailure(0x112, "LoadPledgePower", object)
        LogPledgeLoadFailure(0x112, "rank permissions", object);
    }
    if (!LoadPledgeSkill(object)) {
        // LogPledgeLoadFailure(0x116, "LoadPledgeSkill", object)
        LogPledgeLoadFailure(0x116, "clan skills", object);
    }
    if (!LoadGraduateAcademyCount(object)) {
        // LogPledgeLoadFailure(0x120, "LoadGraduateAcademyCount", object)
        LogPledgeLoadFailure(0x120, "academy graduation count", object);
    }
    if (!object->m_mastery->LoadFromDB()) {
        const std::string name = PledgeNameForLog(object);
        // Log.Add(
        //     LOG_ERROR,
        //     "[PLEDGE][PledgeDb.cpp][294]fail to Load Pledge "
        //     "Mastery[%s:%u]",
        //     name.c_str(), object->GetId()
        // )
        // Log.Add(LOG_ERROR,
        //     "[clan] Cannot load mastery points for clan '%s' (ID %u, world %d). Continuing with incomplete clan data.",
        //     name.c_str(), object->GetId(), object->GetWorldId())
        L2_LOG_ERROR(Log,
            "[clan] Cannot load mastery points for clan '%s' (ID %u, world %d); continuing with incomplete clan data.",
            name.c_str(), object->GetId(), object->GetWorldId());
    }
    if (!object->m_items->LoadFromDB()) {
        const std::string name = PledgeNameForLog(object);
        // Log.Add(
        //     LOG_ERROR,
        //     "[PLEDGE][PledgeDb.cpp][299]fail to Load Pledge "
        //     "Item [%s:%u]",
        //     name.c_str(), object->GetId()
        // )
        // Log.Add(LOG_ERROR,
        //     "[clan] Cannot load items for clan '%s' (ID %u, world %d). Continuing with incomplete clan data.",
        //     name.c_str(), object->GetId(), object->GetWorldId())
        L2_LOG_ERROR(Log,
            "[clan] Cannot load items for clan '%s' (ID %u, world %d); continuing with incomplete clan data.",
            name.c_str(), object->GetId(), object->GetWorldId());
    }

    std::wstring pledgeName = Utf16ToWide(std::u16string_view(object->GetName()));
    InsertIntoMap(object->GetId(), pledgeName.data(), object);
    L2_RELEASE(object, ObjectRefType::CreateDelete);
    return m_pledgesByWorldAndId.FindObject(CPledgeKey(object->GetWorldId(), static_cast<int>(object->GetId())));
}

CPledgeSP CPledgeDb::LoadPledge(wchar_t* pledgeName) {
    L2_GUARD_FUNCTION();
    CPledgeDbHelper helper(runtime::process::CurrentDatabaseId());
    return LoadPledgeInternal(helper.Load(pledgeName));
}

CPledgeSP CPledgeDb::LoadPledge(int pledgeId) {
    L2_GUARD_FUNCTION();
    return LoadPledgeForWorld(pledgeId, runtime::process::CurrentDatabaseId());
}

CPledgeSP CPledgeDb::LoadPledgeForWorld(int pledgeId, int worldId) {
    // The explicit overload carries its selected world into the same helper.
    // the no-world overloads obtain that selector from request TLS +0x10.
    CPledgeDbHelper helper(worldId);
    return LoadPledgeInternal(helper.Load(static_cast<std::uint32_t>(pledgeId)));
}

CPledgeSP CPledgeDb::GetPledge(wchar_t* pledgeName, bool suppressLoadError) {
    L2_GUARD_FUNCTION();
    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = m_pledgesByName.FindObject(worldId, WideToUtf16(LowercasePledgeName(pledgeName)));
    if (pledge)
        return pledge;

    pledge = LoadPledge(pledgeName);
    if (!pledge && !suppressLoadError) {
        const std::string name = WideToUtf8(pledgeName);
        // Log.Add(LOG_ERROR, "[PledgeDb.cpp][540] fail to LoadPledge[%d][%s]", worldId, name.c_str())
        // Log.Add(LOG_ERROR,
        //     "[clan] Clan '%s' could not be loaded in world %d. No cached clan returned.",
        //     name.c_str(), worldId)
        L2_LOG_ERROR(Log,
            "[clan] Clan '%s' could not be loaded in world %d; no cached clan returned.",
            name.c_str(), worldId);
        runtime::process::ReportNamedCallStack(L"GetPledge", 2, false);
    }
    return pledge;
}

CPledgeJoinDB::CPledgeJoinDB() = default;

// Current Cached.exe 0x00458288. The target destroys the seven lock/map
// pairs in reverse member order, which the source-level default destructor
// reproduces without a second teardown path.
CPledgeJoinDB::~CPledgeJoinDB() = default;

CPledgeJoinDB& CPledgeJoinDB::Instance() {
    static CPledgeJoinDB instance;
    return instance;
}

void CPledgeJoinDB::LoadPledge() {
    // Current Cached.exe 0x005116d0. The shipping owner publishes each
    // registered pledge through the process-wide active L2Server socket.
    L2_GUARD_FUNCTION();

    int pledgeId = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeId);
    constexpr const wchar_t* statement = L"{Call lin_GetPledgeOnPledgeRecruitBoard}";
    connection.Prepare(statement);
    if (!connection.ExecutePrepared(statement))
        return;

    while (connection.Fetch()) {
        CSocket* const socket = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (!socket)
            return;
        socket->Send("chd", 0xc6, 0xdc, pledgeId);
    }
}

void CPledgeJoinDB::InitPledgeNotify() {
    // Current Cached.exe 0x00514e74. This notification scan is armed only
    // once after Init resets m_initPledgeNotifyTime to zero.
    L2_GUARD_FUNCTION();
    if (m_initPledgeNotifyTime != 0)
        return;

    m_initPledgeNotifyTime = runtime::crt::Time32Now();
    int pledgeId = 0;
    int registerDate = 0;
    const int worldId = runtime::process::CurrentDatabaseId();
    DBConn connection(worldId);
    connection.Bind(&pledgeId);
    connection.Bind(&registerDate);
    constexpr const wchar_t* statement = L"{Call lin_GetLatestAppliedUserOnPledge(?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    if (!connection.ExecutePrepared(statement))
        return;

    while (connection.Fetch()) {
        CSocket* const socket = g_pActiveServerSocket.load(std::memory_order_acquire);
        if (!socket)
            return;
        socket->Send("chdd", 0xc6, 0xdd, pledgeId, registerDate);
    }
}

int CPledgeJoinDB::SearchPledgeRecruitBoard(
    CPacketHelperEx<20480>& pledgeIds,
    CPacketHelperEx<20480>& pledgeRows,
    CUserSP& user,
    wchar_t* pledgeName,
    int pledgeLevel,
    int trend,
    wchar_t* pledgeMasterName,
    int sortCondition,
    int orderCondition,
    int recruitType,
    int& totalPages,
    int& page
) {
    // Current Cached.exe 0x0051c684. Both SQL input sequences and the two
    // packet projections below follow the raw call-site/bind order.
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] referencing NULL object.",
        //     "c:\\work\\lineage2build\\server\\main\\shared\\"
        //     "ThreadSafeLibrary.h",
        //     0x75
        // )
        // Log.Add(LOG_ERROR,
        //     "[clan recruitment] Cannot search the recruitment board: requesting character is unavailable.")
        L2_LOG_ERROR(Log,
            "[clan recruitment] Cannot search the recruitment board: requesting character is unavailable.");
        return 0;
    }

    const int worldId = user->GetWorld();
    int matchingCount = 0;
    DBConn countConnection(runtime::process::CurrentDatabaseId());
    countConnection.Bind(&matchingCount);
    constexpr const wchar_t* countStatement = L"{Call lin_CountSearchPledgeRecruitBoard(?,?,?,?,?)}";
    countConnection.Prepare(countStatement);
    countConnection.BindParameter(pledgeName);
    countConnection.BindParameter(&pledgeLevel);
    countConnection.BindParameter(&trend);
    countConnection.BindParameter(pledgeMasterName);
    countConnection.BindParameter(&worldId);
    countConnection.ExecutePrepared(countStatement);
    countConnection.Fetch();

    totalPages = static_cast<int>(std::ceil(static_cast<double>(matchingCount) / static_cast<double>(m_pageSize)));
    page = std::min(totalPages, page);

    int pledgeId = 0;
    std::array<char16_t, 25> resultPledgeName{};
    std::array<char16_t, 25> resultPledgeMasterName{};
    int resultPledgeLevel = 0;
    int pledgeNameValue = 0;
    int resultTrend = 0;
    std::array<char16_t, 17> titleMessage{};
    int resultRecruitType = 0;
    int resultPledgeType = 0;

    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&pledgeId);
    connection.Bind(resultPledgeName.data(), 0x32);
    connection.Bind(resultPledgeMasterName.data(), 0x32);
    connection.Bind(&resultPledgeLevel);
    connection.Bind(&pledgeNameValue);
    connection.Bind(&resultTrend);
    connection.Bind(titleMessage.data(), 0x22);
    connection.Bind(&resultRecruitType);
    connection.Bind(&resultPledgeType);
    constexpr const wchar_t* statement = L"{Call lin_SearchPledgeRecruitBoard (?,?,?,?,?,?,?,?,?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&m_pageSize);
    connection.BindParameter(pledgeName);
    connection.BindParameter(&pledgeLevel);
    connection.BindParameter(&trend);
    connection.BindParameter(pledgeMasterName);
    connection.BindParameter(&page);
    connection.BindParameter(&worldId);
    connection.BindParameter(&sortCondition);
    connection.BindParameter(&orderCondition);
    connection.BindParameter(&recruitType);
    if (!connection.ExecutePrepared(statement))
        return 0;

    int resultCount = 0;
    while (connection.Fetch()) {
        CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, worldId);
        if (!pledge)
            continue;

        CAllianceSP alliance = g_allianceDb.GetAlliance(pledge->allianceId(), static_cast<std::uint32_t>(worldId));
        std::uint32_t allianceCrestId = 0;
        if (alliance && alliance->crestId() != 0U) {
            CPledgeCrestSP crest = g_pledgeCrestDb.GetPledgeCrest(alliance->crestId());
            if (crest)
                allianceCrestId = crest->id();
        }

        pledgeIds.Assemble("d", pledgeId);
        pledgeRows.Assemble(
            "ddSSdddSdd", static_cast<int>(pledge->crestId()), static_cast<int>(allianceCrestId), resultPledgeName.data(),
            resultPledgeMasterName.data(), resultPledgeLevel, pledge->GetMemberCount(), pledgeNameValue,
            titleMessage.data(), resultRecruitType, resultPledgeType
        );
        ++resultCount;
    }
    return resultCount;
}

void CPledgeJoinDB::RegisterObject(int worldId, int objectId, RegistrationMap& objects, RWLock& lock) {
    L2_GUARD_FUNCTION();

    const RegistrationKey key(worldId, objectId);
    auto found = objects.find(key);

    // Current 0x005128e2 compares the iterator with the sentinel at this+0x48
    // (the pledge registry), not with the passed map's sentinel. So
    // a missing draft/waiting entry remains absent. The target only writes the
    // unused mapped byte in that map's sentinel. Avoid host end-iterator UB
    // while retaining the same observable registry state.
    const bool targetRecognizesMissingEntry = &objects == &m_registeredPledges && found == objects.end();
    if (targetRecognizesMissingEntry) {
        {
            L2_WRITE_GUARD(lock);
            objects.insert({key, true});
        }
    } else if (found != objects.end()) {
        found->second = true;
    }
}

void CPledgeJoinDB::UnRegisterObject(int worldId, int objectId, RegistrationMap& objects, RWLock& lock) {
    L2_GUARD_FUNCTION();

    const RegistrationKey key(worldId, objectId);
    auto found = objects.find(key);
    const bool targetRecognizesMissingEntry = &objects == &m_registeredPledges && found == objects.end();
    if (targetRecognizesMissingEntry) {
        {
            L2_WRITE_GUARD(lock);
            objects.insert({key, false});
        }
    } else if (found != objects.end()) {
        found->second = false;
    }
}

int CPledgeJoinDB::GetPledgeWaitUserCount(int pledgeId) {
    L2_GUARD_FUNCTION();
    std::optional<decltype(m_pledgeWaitUserCounts.find(pledgeId))> foundStorage;
    {
        L2_WRITE_GUARD(m_pledgeWaitUserCountsLock);
        foundStorage.emplace(m_pledgeWaitUserCounts.find(pledgeId));
    }
    const auto& found = *foundStorage;
    // The current body releases before reading the node and returns zero for
    // a miss. Preserve that ordering, including its original race window.
    return found == m_pledgeWaitUserCounts.end() ? 0 : found->second;
}

void CPledgeJoinDB::DecresePledgeWaitUserCount(int pledgeId) {
    L2_GUARD_FUNCTION();
    std::optional<decltype(m_pledgeWaitUserCounts.find(pledgeId))> foundStorage;
    {
        L2_WRITE_GUARD(m_pledgeWaitUserCountsLock);
        foundStorage.emplace(m_pledgeWaitUserCounts.find(pledgeId));
    }
    const auto& found = *foundStorage;
    if (found != m_pledgeWaitUserCounts.end()) {
        --found->second;
    }
}

bool CPledgeJoinDB::CanRegistratePledge(int pledgeId) {
    L2_GUARD_FUNCTION();
    return GetPledgeWaitUserCount(pledgeId) < 200;
}

void CPledgeJoinDB::UnRegisterPledge(int worldId, int pledgeId) {
    L2_GUARD_FUNCTION();
    UnRegisterObject(worldId, pledgeId, m_registeredPledges, m_registeredPledgesLock);
    {
        L2_WRITE_GUARD(m_pledgeWaitUserCountsLock);
        m_pledgeWaitUserCounts.erase(pledgeId);
    }
}

void CPledgeJoinDB::UnRegisterDraftUser(int worldId, int characterId) {
    L2_GUARD_FUNCTION();
    UnRegisterObject(worldId, characterId, m_draftUsers, m_draftUsersLock);
}

void CPledgeJoinDB::UnRegisterWaitingUser(int worldId, int characterId) {
    L2_GUARD_FUNCTION();
    UnRegisterObject(worldId, characterId, m_waitingUsers, m_waitingUsersLock);
}

void CPledgeJoinDB::SetPledgeWaitUserCount(int pledgeId, int userCount) {
    L2_GUARD_FUNCTION();
    std::optional<decltype(m_pledgeWaitUserCounts.find(pledgeId))> foundStorage;
    {
        L2_WRITE_GUARD(m_pledgeWaitUserCountsLock);
        const auto& found = foundStorage.emplace(m_pledgeWaitUserCounts.find(pledgeId));
        if (found == m_pledgeWaitUserCounts.end()) {
            m_pledgeWaitUserCounts.insert({pledgeId, userCount});
        } else {
            found->second = userCount;
        }
    }
}

void CPledgeJoinDB::SetTimeOnPledgeJoinSystem(int objectId, TimeMap& times, RWLock& lock) {
    L2_GUARD_FUNCTION();
    std::optional<decltype(times.find(objectId))> foundStorage;
    {
        L2_WRITE_GUARD(lock);
        const std::int32_t now = runtime::crt::Time32Now();
        const auto& found = foundStorage.emplace(times.find(objectId));
        if (found == times.end()) {
            times.insert({objectId, now});
        } else {
            found->second = now;
        }
    }
}

void CPledgeJoinDB::RegisterPledge(int worldId, int pledgeId) {
    L2_GUARD_FUNCTION();
    RegisterObject(worldId, pledgeId, m_registeredPledges, m_registeredPledgesLock);
    SetTimeOnPledgeJoinSystem(pledgeId, m_pledgeRegistrationTimes, m_pledgeRegistrationTimesLock);
}

void CPledgeJoinDB::RegisterDraftUser(int worldId, int characterId) {
    L2_GUARD_FUNCTION();
    RegisterObject(worldId, characterId, m_draftUsers, m_draftUsersLock);
    SetTimeOnPledgeJoinSystem(characterId, m_draftRegistrationTimes, m_draftRegistrationTimesLock);
}

void CPledgeJoinDB::RegisterWaitingUser(int worldId, int characterId, int pledgeId) {
    L2_GUARD_FUNCTION();
    RegisterObject(worldId, characterId, m_waitingUsers, m_waitingUsersLock);

    std::optional<decltype(m_pledgeWaitUserCounts.find(pledgeId))> foundStorage;
    {
        L2_WRITE_GUARD(m_pledgeWaitUserCountsLock);
        const auto& found = foundStorage.emplace(m_pledgeWaitUserCounts.find(pledgeId));
        if (found == m_pledgeWaitUserCounts.end()) {
            int userCount = 0;
            const int databaseWorldId = runtime::process::CurrentDatabaseId();
            DBConn connection(databaseWorldId);
            connection.Bind(&userCount);
            constexpr const wchar_t* statement = L"{Call lin_CountPledgeWaitingUser (?,?)}";
            connection.Prepare(statement);
            connection.BindParameter(&databaseWorldId);
            connection.BindParameter(&pledgeId);
            if (connection.ExecutePrepared(statement) && connection.Fetch()) {
                m_pledgeWaitUserCounts.insert({pledgeId, userCount});
            }
        } else {
            ++found->second;
        }
    }

    // Current 0x0051562e keys the wait-time cache with param4 (pledgeId),
    // even though the donor SQL names the matching input char_id.
    SetTimeOnPledgeJoinSystem(pledgeId, m_waitingRegistrationTimes, m_waitingRegistrationTimesLock);
}

bool CPledgeJoinDB::IsObjectExist(
    int worldId, int objectId, const wchar_t* statement, RegistrationMap& objects, RWLock& lock
) {
    L2_GUARD_FUNCTION();
    std::optional<RegistrationKey> keyStorage;
    std::optional<RegistrationMap::iterator> foundStorage;
    bool exists;
    {
        L2_WRITE_GUARD(lock);
        const RegistrationKey& key = keyStorage.emplace(worldId, objectId);
        const auto& found = foundStorage.emplace(objects.find(key));
        exists = false;
        if (found == objects.end()) {
            int count = 0;
            DBConn connection(runtime::process::CurrentDatabaseId());
            connection.Bind(&count);
            connection.Prepare(statement);
            connection.BindParameter(&worldId);
            connection.BindParameter(&objectId);
            if (connection.ExecutePrepared(statement) && connection.Fetch()) {
                exists = count != 0;
                objects.insert({key, exists});
            }
        } else {
            exists = found->second;
        }
    }
    return exists;
}

int CPledgeJoinDB::GetTimeOnPledgeJoinSystem(int objectId, const wchar_t* statement, TimeMap& times, RWLock& lock) {
    L2_GUARD_FUNCTION();
    std::optional<decltype(times.find(objectId))> foundStorage;
    int value;
    {
        L2_WRITE_GUARD(lock);
        const auto& found = foundStorage.emplace(times.find(objectId));
        value = 0;
        if (found == times.end()) {
            const int worldId = runtime::process::CurrentDatabaseId();
            DBConn connection(worldId);
            connection.Bind(&value);
            connection.Prepare(statement);
            connection.BindParameter(&worldId);
            connection.BindParameter(&objectId);
            connection.ExecutePrepared(statement);
            connection.Fetch();
            times.insert({objectId, value});
        } else {
            value = found->second;
        }
    }
    return value;
}

bool CPledgeJoinDB::IsWaitingUserRegistered(int worldId, int characterId) {
    L2_GUARD_FUNCTION();
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (!user)
        return false;
    return IsObjectExist(
        worldId, characterId, L"{Call lin_ConfirmPledgeUserList (?,?)}", m_waitingUsers, m_waitingUsersLock
    );
}

bool CPledgeJoinDB::IsDraftUserRegistered(int worldId, int characterId) {
    L2_GUARD_FUNCTION();
    CUserSP user = g_userDb.FindResidentUser(characterId);
    if (!user)
        return false;
    return IsObjectExist(worldId, characterId, L"{Call lin_ConfirmDraftUserList (?,?)}", m_draftUsers, m_draftUsersLock);
}

bool CPledgeJoinDB::IsPledgeRegistered(int worldId, int pledgeId) {
    L2_GUARD_FUNCTION();
    CPledgeSP pledge = g_pledgeDb.GetPledge(pledgeId, false, runtime::process::CurrentDatabaseId());
    if (!pledge)
        return false;
    return IsObjectExist(
        worldId, pledgeId, L"{Call lin_ConfirmPledgeRecruitBoard (?,?)}", m_registeredPledges, m_registeredPledgesLock
    );
}

void CPledgeJoinDB::ModifyUserInfoOnPledgeJoinDB(int characterId, int level, int classId) {
    L2_GUARD_FUNCTION();
    const int worldId = runtime::process::CurrentDatabaseId();
    const wchar_t* statement = nullptr;
    if (IsWaitingUserRegistered(worldId, characterId)) {
        statement = L"{Call lin_ModifyPledgeWaitingUser (?,?,?,?)}";
    } else if (IsDraftUserRegistered(worldId, characterId)) {
        statement = L"{Call lin_ModifyPledgeDraftUser (?,?,?,?)}";
    }
    if (!statement)
        return;

    DBConn connection(worldId);
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&characterId);
    connection.BindParameter(&level);
    connection.BindParameter(&classId);
    connection.ExecutePrepared(statement);
}

int CPledgeJoinDB::GetTimeOnPledge(int pledgeId) {
    L2_GUARD_FUNCTION();
    return GetTimeOnPledgeJoinSystem(
        pledgeId, L"{Call lin_GetTimeOnPledgeBoard (?,?)}", m_pledgeRegistrationTimes, m_pledgeRegistrationTimesLock
    );
}

int CPledgeJoinDB::GetTimeOnWait(int characterOrPledgeId) {
    L2_GUARD_FUNCTION();
    return GetTimeOnPledgeJoinSystem(
        characterOrPledgeId, L"{Call lin_GetTimeOnPledgeWaitList (?,?)}", m_waitingRegistrationTimes,
        m_waitingRegistrationTimesLock
    );
}

int CPledgeJoinDB::GetTimeOnDraft(int characterId) {
    L2_GUARD_FUNCTION();
    return GetTimeOnPledgeJoinSystem(
        characterId, L"{Call lin_GetTimeOnPledgeDraftList (?,?)}", m_draftRegistrationTimes, m_draftRegistrationTimesLock
    );
}

int CPledgeJoinDB::RequestPledgeWaitingUserList(CPacketHelperEx<20480>& packet, CUserSP user, int pledgeId) {
    L2_GUARD_FUNCTION();
    if (!user) {
        // Log.Add(
        //     LOG_ERROR, "[%s][%d] referencing NULL object.",
        //     "c:\\work\\lineage2build\\server\\main\\shared\\"
        //     "ThreadSafeLibrary.h",
        //     0x75
        // )
        // Log.Add(LOG_ERROR,
        //     "[clan recruitment] Cannot list applicants for clan %d: requesting character is unavailable.",
        //     pledgeId)
        L2_LOG_ERROR(Log,
            "[clan recruitment] Cannot list applicants for clan %d: requesting character is unavailable.",
            pledgeId);
        return 0;
    }

    int characterId = 0;
    std::array<char16_t, 25> characterName{};
    int classId = 0;
    int level = 0;
    DBConn connection(runtime::process::CurrentDatabaseId());
    connection.Bind(&characterId);
    connection.Bind(characterName.data(), 0x32);
    connection.Bind(&classId);
    connection.Bind(&level);
    const int worldId = user->GetWorld();
    constexpr const wchar_t* statement = L"{Call lin_SearchPledgeWaitingList (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&pledgeId);
    if (!connection.ExecutePrepared(statement))
        return 0;

    int resultCount = 0;
    while (connection.Fetch()) {
        packet.Assemble("dSdd", characterId, characterName.data(), classId, level);
        ++resultCount;
        if (resultCount >= 0xc9)
            break;
    }
    return resultCount;
}

void CPledgeJoinDB::RequestPledgeDelete(int pledgeId) {
    L2_GUARD_FUNCTION();
    const int worldId = runtime::process::CurrentDatabaseId();
    DBConn connection(worldId);
    constexpr const wchar_t* statement = L"{Call lin_DeletePledgeRecruitBoard (?,?)}";
    connection.Prepare(statement);
    connection.BindParameter(&worldId);
    connection.BindParameter(&pledgeId);
    connection.ExecutePrepared(statement);

    UnRegisterPledge(worldId, pledgeId);
    {
        L2_WRITE_GUARD(m_waitingUsersLock);
        m_waitingUsers.clear();
    }
}

void CPledgeJoinDB::InitPledgeJoinSystemTables(CSocket* socket, int initializationPeriod) {
    L2_GUARD_FUNCTION();
    const int worldId = runtime::process::CurrentDatabaseId();

    int characterId = 0;
    DBConn waitingConnection(worldId);
    waitingConnection.Bind(&characterId);
    int now = runtime::crt::Time32Now();
    constexpr const wchar_t* waitingStatement = L"{Call lin_InitializePledgeWaitList (?, ?, ?)}";
    waitingConnection.Prepare(waitingStatement);
    waitingConnection.BindParameter(&now);
    waitingConnection.BindParameter(&initializationPeriod);
    waitingConnection.BindParameter(&worldId);
    if (!waitingConnection.ExecutePrepared(waitingStatement)) {
        // Log.Add(LOG_ERROR, "Cannot Initialize Pledge wait list")
        // Log.Add(LOG_ERROR,
        //     "[clan recruitment] Database applicant-list cleanup failed for world %d (expiration period %d seconds). Corresponding cache entries not updated.",
        //     worldId, initializationPeriod)
        L2_LOG_ERROR(Log,
            "[clan recruitment] Database applicant-list cleanup failed for world %d (expiration period %d seconds); corresponding cache entries not updated.",
            worldId, initializationPeriod);
    } else {
        while (waitingConnection.Fetch()) {
            UnRegisterWaitingUser(worldId, characterId);
            if (socket) {
                socket->Send("chhd", 0xc6, 0xef, 1, characterId);
            }
        }
    }

    int pledgeId = 0;
    DBConn boardConnection(worldId);
    boardConnection.Bind(&pledgeId);
    now = runtime::crt::Time32Now();
    constexpr const wchar_t* boardStatement = L"{Call lin_InitializePledgeRecruitBoard (?, ?, ?)}";
    boardConnection.Prepare(boardStatement);
    boardConnection.BindParameter(&now);
    boardConnection.BindParameter(&initializationPeriod);
    boardConnection.BindParameter(&worldId);
    if (!boardConnection.ExecutePrepared(boardStatement)) {
        // Log.Add(LOG_ERROR, "Cannot Initialize Pledge Join System")
        // Log.Add(LOG_ERROR,
        //     "[clan recruitment] Database recruitment-board maintenance failed for world %d (expiration period %d seconds). Corresponding cache entries not updated.",
        //     worldId, initializationPeriod)
        L2_LOG_ERROR(Log,
            "[clan recruitment] Database recruitment-board maintenance failed for world %d (expiration period %d seconds); corresponding cache entries not updated.",
            worldId, initializationPeriod);
    } else {
        while (boardConnection.Fetch()) {
            UnRegisterPledge(worldId, pledgeId);
            if (socket) {
                socket->Send("chhd", 0xc6, 0xef, 0, pledgeId);
            }
        }
    }

    DBConn draftConnection(worldId);
    draftConnection.Bind(&characterId);
    now = runtime::crt::Time32Now();
    constexpr const wchar_t* draftStatement = L"{Call lin_InitializePledgeDraftList (?, ?, ?)}";
    draftConnection.Prepare(draftStatement);
    draftConnection.BindParameter(&now);
    draftConnection.BindParameter(&initializationPeriod);
    draftConnection.BindParameter(&worldId);
    if (!draftConnection.ExecutePrepared(draftStatement)) {
        // Log.Add(LOG_ERROR, "Cannot Initialize Pledge draft list")
        // Log.Add(LOG_ERROR,
        //     "[clan recruitment] Database recruit-list cleanup failed for world %d (expiration period %d seconds). Corresponding cache entries not updated.",
        //     worldId, initializationPeriod)
        L2_LOG_ERROR(Log,
            "[clan recruitment] Database recruit-list cleanup failed for world %d (expiration period %d seconds); corresponding cache entries not updated.",
            worldId, initializationPeriod);
    } else {
        while (draftConnection.Fetch()) {
            UnRegisterDraftUser(worldId, characterId);
            if (socket) {
                socket->Send("chhd", 0xc6, 0xef, 2, characterId);
            }
        }
    }
}

void CPledgeJoinDB::Init() {
    L2_GUARD_FUNCTION();
    m_waitingUsers.clear();
    m_draftUsers.clear();
    m_registeredPledges.clear();
    m_pledgeWaitUserCounts.clear();
    InitPledgeJoinSystemTables(nullptr, 0x278d00);
    m_initPledgeNotifyTime = 0;
}

CPledgeSP CPledgeDb::GetPledge(int pledgeId, bool suppressLoadError, int worldId) {
    L2_GUARD_FUNCTION();
    CPledgeSP pledge = m_pledgesByWorldAndId.FindObject(CPledgeKey(worldId, pledgeId));
    if (pledge)
        return pledge;

    pledge = LoadPledgeForWorld(pledgeId, worldId);
    if (!pledge && !suppressLoadError && pledgeId > 0) {
        // Log.Add(
        //     LOG_ERROR,
        //     "[PledgeDb.cpp][580] fail to LoadPledge"
        //     "[WorldId:%d][PledgeId:%d]",
        //     worldId, pledgeId
        // )
        // Log.Add(LOG_ERROR,
        //     "[clan] Clan %d could not be loaded in world %d. No cached clan returned.",
        //     pledgeId, worldId)
        L2_LOG_ERROR(Log,
            "[clan] Clan %d could not be loaded in world %d; no cached clan returned.",
            pledgeId, worldId);
        runtime::process::ReportNamedCallStack(L"GetPledge", 2, false);
    }
    return pledge;
}

bool CPledgeDb::ModifyPledgeName(std::uint32_t pledgeId, wchar_t* newName) {
    // Current Cached.exe 0x005193cc. This method keeps the
    // shipping stale-name-index defect: it computes the lowercase new name,
    // but removes and reinserts the object under the lowercase old name.
    L2_GUARD_FUNCTION();

    const int worldId = runtime::process::CurrentDatabaseId();
    CPledgeSP pledge = GetPledge(static_cast<int>(pledgeId), false, worldId);
    if (!pledge)
        return false;

    const std::wstring oldName = Utf16ToWide(std::u16string_view(pledge->GetName()));
    const std::wstring oldLower = LowercasePledgeName(oldName.c_str());
    const std::wstring unusedNewLower = LowercasePledgeName(newName);

    DBConn connection(worldId);
    constexpr const wchar_t* statement = L"{Call lin_ModifyPledgeName (?, ? )}";
    connection.Prepare(statement);
    // The current image binds the original input, not unusedNewLower.
    connection.BindParameter(newName);
    connection.BindParameter(&pledgeId);
    if (!connection.ExecutePrepared(statement))
        return false;

    CPledge::AssignFixedName(pledge->m_name, newName != nullptr ? WideToUtf16(newName).c_str() : nullptr, L"newName length < 24");
    m_pledgesByWorldAndId.DeleteObject(CPledgeKey(worldId, static_cast<int>(pledgeId)), nullptr, -1);
    m_pledgesByName.DeleteObject(worldId, WideToUtf16(oldLower), nullptr, -1);
    m_pledgesByWorldAndId.Insert(CPledgeKey(worldId, static_cast<int>(pledgeId)), pledge.get(), nullptr, -1);
    // Shipping 0x005196cf uses oldLower here as well.
    m_pledgesByName.Insert(worldId, WideToUtf16(oldLower), pledge.get(), nullptr, -1);
    return true;
}
