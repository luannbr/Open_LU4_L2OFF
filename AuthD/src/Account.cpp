#include "Account.h"

#include "Platform/Odbc.h"
#include "Shared/src/Runtime/Time32.h"
#include "Shared/src/Runtime/WindowsAnsi.h"
#include "AccountContainer.h"
#include "Config.h"
#include "DBConn.h"
#include "include/CDes.h"
#include "include/MD5.h"
#include "Log.h"
#include "ServerList.h"
#include "Util.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

extern CDes DesObject;
extern void (*EncPwd)(char* password);

namespace {

constexpr authd::platform::OdbcReturn kSqlSuccess = 0;
constexpr authd::platform::OdbcReturn kSqlError = -1;
constexpr authd::platform::OdbcLen kSqlNullData = -1;
constexpr authd::platform::OdbcInteger kSqlNts = -3;
constexpr authd::platform::OdbcSmallInt kStatementHandle = 3;

constexpr authd::platform::OdbcSmallInt kInput = 1;
constexpr authd::platform::OdbcSmallInt kOutput = 4;
constexpr authd::platform::OdbcSmallInt kCChar = 1;
constexpr authd::platform::OdbcSmallInt kCVarchar = 12;
constexpr authd::platform::OdbcSmallInt kCBinary = -2;
constexpr authd::platform::OdbcSmallInt kCShort = 5;
constexpr authd::platform::OdbcSmallInt kCIntOutput = -18;
constexpr authd::platform::OdbcSmallInt kCTinyInt = -28;
constexpr authd::platform::OdbcSmallInt kTinyInt = -6;
constexpr authd::platform::OdbcSmallInt kTimestamp = 93;
constexpr authd::platform::OdbcSmallInt kVarBinary = -3;

long LegacyTimezoneSecondsWest() noexcept {
#if defined(_WIN32)
    long seconds = 0;
    ::_get_timezone(&seconds);
    return seconds;
#else
    ::tzset();
    return ::timezone;
#endif
}

void BindAccountName(
    CDBConn& connection,
    int parameter,
    const char* accountName,
    authd::platform::OdbcLen& indicator) {
    indicator = kSqlNts;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        static_cast<authd::platform::OdbcUSmallInt>(parameter),
        kInput,
        kCChar,
        kCVarchar,
        14,
        0,
        const_cast<char*>(accountName),
        static_cast<authd::platform::OdbcLen>(
            std::strlen(accountName)),
        &indicator);
}

void BindIntOutput(
    CDBConn& connection,
    int parameter,
    int* value,
    authd::platform::OdbcLen& indicator) {
    indicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        static_cast<authd::platform::OdbcUSmallInt>(parameter),
        kOutput,
        kCIntOutput,
        4,
        0,
        0,
        value,
        0,
        &indicator);
}

bool IsRestrictedChildHour(int hour) noexcept {
    if (config.shutdownEndHour <
        config.shutdownBeginHour) {
        return config.shutdownBeginHour <= hour ||
               hour < config.shutdownEndHour;
    }
    return config.shutdownBeginHour <= hour &&
           hour < config.shutdownEndHour;
}

} // namespace

// L2Authd.exe 0x004059F0, Src/account.cpp:30-57.
CAccount::CAccount() {
    quotaTime = 0;
    remainTime = 0;
    block_flag = 0;
    block_flag2 = 0;
    gender = 0;
    lastworld = 0;
    service_flag = 0;
    card_number = 0;
    externalAuthAdditionalInfo = 0;
    std::memset(forbidden_servers, 0, sizeof(forbidden_servers));
    age = 0;
    active_flag = 0;
    fcid = 0;
    auth_flag = 0;
    std::memset(user_data, 0, sizeof(user_data));
    packageVersionInfo = 0;
    std::memset(
        byteChildPlayTimeInfo,
        0,
        sizeof(byteChildPlayTimeInfo));
    user_level = 0;
    lastLogoutTime = 0;
    activateTime = 0;
    std::memset(
        activateInformation,
        0,
        sizeof(activateInformation));

    // warn_flag, pay_stat, uid, login_flag, subscription_flag, account,
    // user_data_len and block_end_date are not initialized by
    // the selected constructor.
}

LocalAccountCreateResult CAccount::CreateLocalAccount(
    std::string_view accountName,
    std::string_view password) {
    if (accountName.empty() || accountName.size() > 14 ||
        !std::all_of(accountName.begin(), accountName.end(), [](char value) {
            return (value >= 'a' && value <= 'z') ||
                   (value >= '0' && value <= '9') || value == '_';
        })) {
        return LocalAccountCreateResult::InvalidName;
    }
    if (password.empty() || password.size() > 16 ||
        !std::all_of(password.begin(), password.end(), [](unsigned char value) {
            return value >= 0x20 && value <= 0x7e;
        })) {
        return LocalAccountCreateResult::InvalidPassword;
    }
#if !defined(AUTHD_USE_POSTGRESQL)
    return LocalAccountCreateResult::Unsupported;
#else
    const std::string name(accountName);
    const std::string plainPassword(password);
    std::array<char, 16> encryptedPassword{};
    CAccount account;
    account.Encrypt(2, name.c_str(), plainPassword.c_str(), encryptedPassword.data());

    CDBConn connection(g_linDB);
    if (!connection.IsConnected()) {
        return LocalAccountCreateResult::DatabaseError;
    }
    authd::platform::OdbcLen accountIndicator = 0;
    BindAccountName(connection, 1, name.c_str(), accountIndicator);
    authd::platform::OdbcLen passwordIndicator = encryptedPassword.size();
    if (authd::platform::SQLBindParameter(
            connection.m_stmt, 2, kInput, kCBinary, kCBinary, 16, 0,
            encryptedPassword.data(), encryptedPassword.size(),
            &passwordIndicator) != kSqlSuccess) {
        return LocalAccountCreateResult::DatabaseError;
    }
    int createdUid = 0;
    connection.Bind(&createdUid);
    bool noMoreRows = false;
    if (!connection.Execute("{CALL dbo.ap_CreateAccount(?,?)}") ||
        !connection.Fetch(noMoreRows) || createdUid < 0) {
        return LocalAccountCreateResult::DatabaseError;
    }
    return createdUid == 0 ? LocalAccountCreateResult::AlreadyExists
                           : LocalAccountCreateResult::Created;
#endif
}

// L2Authd.exe 0x00405B20, Src/account.cpp:105-205.
char CAccount::Load(const char* accountName) {
    CDBConn connection(g_linDB);

    authd::platform::OdbcLen accountIndicator = 0;
    BindAccountName(
        connection,
        1,
        accountName,
        accountIndicator);

    authd::platform::OdbcLen uidIndicator = 0;
    authd::platform::OdbcLen payStatIndicator = 0;
    authd::platform::OdbcLen loginFlagIndicator = 0;
    authd::platform::OdbcLen warnFlagIndicator = 0;
    authd::platform::OdbcLen blockFlagIndicator = 0;
    authd::platform::OdbcLen blockFlag2Indicator = 0;
    authd::platform::OdbcLen subscriptionIndicator = 0;
    BindIntOutput(connection, 2, &uid, uidIndicator);
    BindIntOutput(connection, 3, &pay_stat, payStatIndicator);
    BindIntOutput(connection, 4, &login_flag, loginFlagIndicator);
    BindIntOutput(connection, 5, &warn_flag, warnFlagIndicator);
    BindIntOutput(connection, 6, &block_flag, blockFlagIndicator);
    BindIntOutput(connection, 7, &block_flag2, blockFlag2Indicator);
    BindIntOutput(
        connection,
        8,
        &subscription_flag,
        subscriptionIndicator);

    authd::platform::OdbcLen lastWorldIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        9,
        kOutput,
        kCTinyInt,
        kTinyInt,
        0,
        0,
        &lastworld,
        0,
        &lastWorldIndicator);

    block_end_date.year = -1;
    authd::platform::OdbcLen blockEndIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        10,
        kOutput,
        kTimestamp,
        kTimestamp,
        23,
        3,
        &block_end_date,
        0,
        &blockEndIndicator);

    std::array<char, 256> statement{};
    std::strcpy(
        statement.data(),
        "{CALL dbo.ap_GStat (?,?,?,?,?,?,?,?,?,?");
    int parameter = 11;
    authd::platform::OdbcLen forbiddenIndicator = 0;
    authd::platform::OdbcLen activeIndicator = 0;
    if (config.useForbiddenServerSetting) {
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            static_cast<authd::platform::OdbcUSmallInt>(parameter),
            kOutput,
            kCBinary,
            kCBinary,
            sizeof(forbidden_servers),
            0,
            forbidden_servers,
            sizeof(forbidden_servers),
            &forbiddenIndicator);
        ++parameter;
        std::strcat(statement.data(), ",?");
    }
    if (config.useDormantFlag) {
        BindIntOutput(
            connection,
            parameter,
            &active_flag,
            activeIndicator);
        std::strcat(statement.data(), ",?");
    }
    std::strcat(statement.data(), ") }");

    if (!connection.Execute(statement.data())) {
        connection.Error(
            kStatementHandle,
            connection.m_stmt,
            statement.data());
        return 1;
    }

    // This selected SQL Server deployment returns SQL_ERROR when SQLFetch is
    // attempted on the output-parameter-only procedure. The EXE treats that
    // exact result as the expected no-result-set condition.
    // if (authd::platform::SQLFetch(connection.m_stmt) !=
    //     kSqlError) {
    const auto fetchResult = authd::platform::SQLFetch(connection.m_stmt);
    if (fetchResult != kSqlError) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Unexpected SP result - (No result set expected.)")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "DB Statement: %s",
        //     statement.data())
        // Log.AddLog(LOG_ERROR,
        //     "component=database event=procedure_contract_mismatch environment=game_account procedure=dbo.ap_GStat operation=fetch expected=sql_error_for_output_only_procedure account_load_failed=true")
        // Log.AddLog(LOG_ERROR,
        //     "[database] Account load failed: dbo.ap_GStat returned an unexpected fetch result. This output-only procedure requires SQL_ERROR from fetch.")
        // L2_LOG_ERROR(Log,
        //     "[database] Account load failed: dbo.ap_GStat returned an unexpected fetch result. This output-only procedure requires SQL_ERROR from fetch.")
        L2_LOG_ERROR(Log,
            "[database] Account load failed: dbo.ap_GStat returned SQLFetch status %d; this output-only procedure requires SQL_ERROR (%d).",
            static_cast<int>(fetchResult), static_cast<int>(kSqlError));
        // Preserve the supplied SQL, including spacing and placeholders.
        // Log.AddLog(LOG_ERROR, "[database] SQL: %s", statement.data())
        // Log.AddLog(LOG_ERROR, "[database] SQL: \"%s\"", statement.data())
        L2_LOG_ERROR(Log, "[database] SQL: \"%s\"", statement.data());
        return 1;
    }

    connection.ResetHtmt();
    if (config.useNoBill) {
        pay_stat = 2000;
    }
    return 0;
}

// L2Authd.exe 0x00405FD0, Src/account.cpp:333-377.
char CAccount::LoadPassword(
    const char* accountName,
    char* password,
    char* encryptionType,
    [[maybe_unused]] int newEncryptionType) {
    password[0] = 0;
    CDBConn connection(g_linDB);

    authd::platform::OdbcLen accountIndicator = 0;
    BindAccountName(
        connection,
        1,
        accountName,
        accountIndicator);

    authd::platform::OdbcLen passwordIndicator = kSqlNts;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        2,
        kOutput,
        kCBinary,
        kCBinary,
        16,
        0,
        password,
        16,
        &passwordIndicator);

    authd::platform::OdbcLen encryptionIndicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        3,
        kOutput,
        kTinyInt,
        kTinyInt,
        0,
        0,
        encryptionType,
        0,
        &encryptionIndicator);

    char telephoneAuthState = 0;
    authd::platform::OdbcLen tasIndicator = 0;
    std::array<char, 256> statement{};
    std::strcpy(
        statement.data(),
        "{CALL dbo.ap_GPwdWithFlag (?,?,?");
    if (config.checkTasStatusFromDb) {
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            4,
            kOutput,
            kTinyInt,
            kTinyInt,
            0,
            0,
            &telephoneAuthState,
            0,
            &tasIndicator);
        std::strcat(statement.data(), ",?");
    }
    std::strcat(statement.data(), ")}");

    if (!connection.Execute(statement.data())) {
        connection.Error(
            kStatementHandle,
            connection.m_stmt,
            statement.data());
        return 1;
    }
    // if (authd::platform::SQLFetch(connection.m_stmt) !=
    //     kSqlError) {
    const auto fetchResult = authd::platform::SQLFetch(connection.m_stmt);
    if (fetchResult != kSqlError) {
        // Log.AddLog(
        //     LOG_ERROR,
        //     "Unexpected SP result - (No result set expected.)")
        // Log.AddLog(
        //     LOG_ERROR,
        //     "DB Statement: %s",
        //     statement.data())
        // Log.AddLog(LOG_ERROR,
        //     "component=database event=procedure_contract_mismatch environment=game_account procedure=dbo.ap_GPwdWithFlag operation=fetch expected=sql_error_for_output_only_procedure password_load_failed=true")
        // Log.AddLog(LOG_ERROR,
        //     "[database] Password load failed: dbo.ap_GPwdWithFlag returned an unexpected fetch result. This output-only procedure requires SQL_ERROR from fetch.")
        // L2_LOG_ERROR(Log,
        //     "[database] Password load failed: dbo.ap_GPwdWithFlag returned an unexpected fetch result. This output-only procedure requires SQL_ERROR from fetch.")
        L2_LOG_ERROR(Log,
            "[database] Password load failed: dbo.ap_GPwdWithFlag returned SQLFetch status %d; this output-only procedure requires SQL_ERROR (%d).",
            static_cast<int>(fetchResult), static_cast<int>(kSqlError));
        // Preserve the supplied SQL, including spacing and placeholders.
        // Log.AddLog(LOG_ERROR, "[database] SQL: %s", statement.data())
        // Log.AddLog(LOG_ERROR, "[database] SQL: \"%s\"", statement.data())
        L2_LOG_ERROR(Log, "[database] SQL: \"%s\"", statement.data());
        return 1;
    }

    // Missing output is distinct from a failed query or an invalid stored hash.
    if (passwordIndicator == kSqlNullData) {
        return 3;
    }
    if (passwordIndicator != 16 || std::strlen(accountName) >= sizeof(account)) {
        return 1;
    }
    std::memset(account, 0, sizeof(account));
    std::strcpy(account, accountName);
    if (config.checkTasStatusFromDb) {
        const SecAuth::Enum type = SecAuth::TELEPHONEAUTH;
        securityFlag.Set(type, telephoneAuthState);
    }
    return 0;
}

// L2Authd.exe 0x00406340, Src/account.cpp:214-328.
char CAccount::LoadEtc() {
    bool hasOutput = false;
    CDBConn connection(g_linDB);
    if (config.useSecurityCard) {
        connection.Bind(&card_number);
        hasOutput = true;
    }

    authd::platform::OdbcLen userDataIndicator = 0;
    authd::platform::OdbcLen userDataLengthIndicator = 0;
    if (config.UserData) {
        authd::platform::SQLBindCol(
            connection.m_stmt,
            static_cast<authd::platform::OdbcUSmallInt>(
                connection.m_colNum++),
            kCBinary,
            user_data,
            sizeof(user_data),
            &userDataIndicator);
        authd::platform::SQLBindCol(
            connection.m_stmt,
            static_cast<authd::platform::OdbcUSmallInt>(
                connection.m_colNum++),
            kCShort,
            &user_data_len,
            sizeof(user_data_len),
            &userDataLengthIndicator);
        hasOutput = true;
    }
    if (!hasOutput) {
        return 0;
    }

    connection.BindParam(account, 14, kInput);
    const char* const statement = config.useTestServer
        ? "{CALL dbo.ap_GStatEtcT (?)}"
        : "{CALL dbo.ap_GStatEtc (?)}";
    bool noMoreRows = true;
    if (!connection.Execute(statement)) {
        return 1;
    }
    if (!connection.Fetch(noMoreRows)) {
        return noMoreRows ? 4 : 1;
    }

    if (card_number != 0) {
        const SecAuth::Enum type = SecAuth::SECURITYCARD;
        securityFlag.Set(type, 1);
    }

    if (config.supportSleepAccountActivate) {
        connection.ResetHtmt();

        authd::platform::OdbcLen uidIndicator = 0;
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            1,
            kInput,
            kCIntOutput,
            4,
            4,
            0,
            &uid,
            0,
            &uidIndicator);

        TIMESTAMP_STRUCT lastLogout{};
        TIMESTAMP_STRUCT activate{};
        authd::platform::OdbcLen lastLogoutIndicator = 0;
        authd::platform::OdbcLen activateIndicator = 0;
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            2,
            kOutput,
            kTimestamp,
            kTimestamp,
            sizeof(TIMESTAMP_STRUCT),
            0,
            &lastLogout,
            0,
            &lastLogoutIndicator);
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            3,
            kOutput,
            kTimestamp,
            kTimestamp,
            sizeof(TIMESTAMP_STRUCT),
            0,
            &activate,
            0,
            &activateIndicator);

        std::array<unsigned char, 8> information{};
        authd::platform::OdbcLen informationIndicator = 0;
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            4,
            kOutput,
            kCBinary,
            kVarBinary,
            information.size(),
            0,
            information.data(),
            information.size(),
            &informationIndicator);

        if (!connection.Execute(
                "{CALL dbo.ap_GetAccountGameSlot(?, ?, ?, ?)}")) {
            return 1;
        }

        std::tm converted{};
        if (lastLogoutIndicator != -1) {
            lastLogoutTime = ConvertSQLToTome(
                lastLogout,
                &converted);
        }
        if (activateIndicator != -1) {
            activateTime = ConvertSQLToTome(
                activate,
                &converted);
        }
        if (informationIndicator != -1) {
            std::memcpy(
                activateInformation,
                information.data(),
                information.size());
        }
    }
    return 0;
}

// L2Authd.exe 0x004067B0, Src/account.cpp:466-521.
char CAccount::CheckPassword(
    const char* accountName,
    char* password,
    int newEncryptionType,
    bool autoCreate) {
    char storedPassword[20];
    std::memset(storedPassword, 0, 17);
    char storedEncryptionType = 0;
    char result = LoadPassword(
        accountName,
        storedPassword,
        &storedEncryptionType,
        newEncryptionType);
    if (result == 3 && autoCreate) {
        const auto created = CreateLocalAccount(accountName, password);
        if (created == LocalAccountCreateResult::DatabaseError ||
            created == LocalAccountCreateResult::Unsupported) {
            return 1;
        }
        if (created != LocalAccountCreateResult::Created &&
            created != LocalAccountCreateResult::AlreadyExists) {
            return 3;
        }
        if (created == LocalAccountCreateResult::Created) {
            L2_LOG_INFO(Log, "[accounts] Created local account %s during login.", accountName);
        }
        // A concurrent request may have created the account with another password.
        result = LoadPassword(
            accountName, storedPassword, &storedEncryptionType, newEncryptionType);
    }
    if (result != 0) {
        return result;
    }

    password[16] = 0;
    char encryptedPassword[20];
    Encrypt(
        storedEncryptionType,
        accountName,
        password,
        encryptedPassword);
    if (std::memcmp(
            encryptedPassword,
            storedPassword,
            16) != 0) {
        return 3;
    }

    if (newEncryptionType != storedEncryptionType) {
        Encrypt(
            newEncryptionType,
            accountName,
            password,
            encryptedPassword);
        CDBConn connection(g_linDB);

        authd::platform::OdbcLen accountIndicator = 0;
        BindAccountName(
            connection,
            1,
            accountName,
            accountIndicator);
        authd::platform::OdbcLen passwordIndicator = 16;
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            2,
            kInput,
            kCBinary,
            kCBinary,
            16,
            0,
            encryptedPassword,
            16,
            &passwordIndicator);
        authd::platform::OdbcLen encryptionIndicator = 0;
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            3,
            kInput,
            kTinyInt,
            kTinyInt,
            0,
            0,
            &newEncryptionType,
            0,
            &encryptionIndicator);

        constexpr char statement[] =
            "{CALL dbo.ap_SNewPwd(?,?,?) }";
        if (authd::platform::SQLExecDirect(
                connection.m_stmt,
                statement,
                kSqlNts) != kSqlSuccess) {
            connection.Error(
                kStatementHandle,
                connection.m_stmt,
                statement);
            connection.ResetHtmt();
            return 1;
        }
        connection.ResetHtmt();
    }
    return 0;
}

// L2Authd.exe 0x00406A10, Src/account.cpp:413-463.
void CAccount::Encrypt(
    int encryptionType,
    const char* accountName,
    const char* password,
    char* encryptedPassword) {
    MD5 md5;
    if (encryptionType == 0) {
        std::strncpy(encryptedPassword, password, 16);
        EncPwd(encryptedPassword);
    } else if (encryptionType == 1) {
        std::strncpy(encryptedPassword, password, 16);
        for (int index = static_cast<int>(
                 std::strlen(encryptedPassword));
             index < 16;
             ++index) {
            encryptedPassword[index] = 0x10;
        }
        DesObject.DesWriteBlock(encryptedPassword, 16);
    } else if (encryptionType == 2) {
        std::array<unsigned char, 30> material{};
        material.fill('$');
        std::memcpy(
            material.data(),
            accountName,
            std::strlen(accountName));
        std::memcpy(
            material.data() + 14,
            password,
            std::strlen(password));
        unsigned char salt[] =
            "ti6&%fjgie115/*50 54_+60y6a^$@hg";
        md5.Update(salt, 0x20);
        md5.Update(material.data(), material.size());
        md5.Final(
            reinterpret_cast<unsigned char*>(
                encryptedPassword));
    }
}

// L2Authd.exe 0x00406B60, Src/account.cpp:579-769.
bool CAccount::VerifyUnderAgeTimeLimit(int portalUid) {
    std::memset(
        byteChildPlayTimeInfo,
        0xff,
        sizeof(byteChildPlayTimeInfo));

    if (config.useUnderAgeTimeLimit &&
        (static_cast<unsigned int>(login_flag) & 0x40U) == 0 &&
        (static_cast<unsigned int>(login_flag) & 0x10U) == 0) {
        const int now = runtime::crt::Time32Now();
        const std::div_t localDay = std::div(
            now - static_cast<int>(
                      LegacyTimezoneSecondsWest()),
            86400);
        const int from =
            config.underAgeTimeLimitFrom * 3600;
        int to = config.underAgeTimeLimitTo * 3600;
        if (config.underAgeTimeLimitTo <
            config.underAgeTimeLimitFrom) {
            to += 86400;
        }
        if (from < localDay.rem && localDay.rem < to) {
            return false;
        }
    }

    if (config.useChildUsageCheck) {
        const std::time_t now = static_cast<std::time_t>(
            runtime::crt::Time32Now());
        const std::tm* const local = std::localtime(&now);
        if (age < config.shutdownAge) {
            if (IsRestrictedChildHour(local->tm_hour)) {
                return false;
            }
            for (int dayOffset = 0;
                 dayOffset < 21;
                 dayOffset += 3) {
                for (int hour = 0; hour < 24; ++hour) {
                    if (IsRestrictedChildHour(hour)) {
                        byteChildPlayTimeInfo[
                            dayOffset + hour / 8] &=
                            static_cast<unsigned char>(
                                ~(1U << (7 - hour % 8)));
                    }
                }
            }
        }
    }

    if (config.useUserPlayTimeLimit) {
        CDBConn connection(g_GloActNcsDB);
        int result = 0;
        std::array<char, 48> schedule{};
        std::array<unsigned char, 24> scheduleBytes{};
        scheduleBytes.fill(0xff);

        authd::platform::OdbcLen uidIndicator = 0;
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            1,
            kInput,
            kCIntOutput,
            4,
            0,
            0,
            &portalUid,
            0,
            &uidIndicator);
        short gameId = 0x1b;
        authd::platform::OdbcLen gameIndicator = 0;
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            2,
            kInput,
            kCShort,
            kCShort,
            0,
            0,
            &gameId,
            0,
            &gameIndicator);
        authd::platform::OdbcLen resultIndicator = 0;
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            3,
            kOutput,
            4,
            4,
            0,
            0,
            &result,
            0,
            &resultIndicator);
        authd::platform::OdbcLen scheduleIndicator = 0;
        authd::platform::SQLBindParameter(
            connection.m_stmt,
            4,
            kOutput,
            kCChar,
            kCVarchar,
            schedule.size(),
            0,
            schedule.data(),
            schedule.size(),
            &scheduleIndicator);

        constexpr char statement[] =
            "{CALL dbo.ap_GetServiceTimeSchedule (?,?,?,?)}";
        // Noise: schedule bitmap snapshots add per-login payload dumps without an operational outcome.
        // WritePacketLog(
        //     "1.Auth<->GlobalAct",
        //     static_cast<unsigned int>(portalUid),
        //     reinterpret_cast<const unsigned char*>(
        //         schedule.data()),
        //     schedule.size(),
        //     0x400)
        if (authd::platform::SQLExecDirect(
                connection.m_stmt,
                statement,
                kSqlNts) != kSqlSuccess) {
            connection.Error(
                kStatementHandle,
                connection.m_stmt,
                statement);
            connection.ResetHtmt();
            return false;
        }
        if (result == 0) {
            StrToBytes(
                scheduleBytes.data(),
                scheduleBytes.size(),
                schedule.data());
            for (std::size_t index = 0;
                 index < scheduleBytes.size();
                 ++index) {
                byteChildPlayTimeInfo[index] &=
                    scheduleBytes[index];
            }
        }
        // Noise: schedule bitmap snapshots add per-login payload dumps without an operational outcome.
        // WritePacketLog(
        //     "2.Auth<->GlobalAct",
        //     static_cast<unsigned int>(portalUid),
        //     byteChildPlayTimeInfo,
        //     sizeof(byteChildPlayTimeInfo),
        //     0x400)
        connection.ResetHtmt();
    }

    const std::time_t now = static_cast<std::time_t>(
        runtime::crt::Time32Now());
    const std::tm* const local = std::localtime(&now);
    const int day = local->tm_wday == 0
        ? 6
        : local->tm_wday - 1;
    const int hour = local->tm_hour;
    return (byteChildPlayTimeInfo[
                day * 3 + hour / 8] &
            static_cast<unsigned char>(
                1U << (7 - hour % 8))) != 0;
}

// L2Authd.exe 0x00407260, Src/account.cpp:773-792.
char CAccount::CheckBlockStatus() {
    if (block_flag2 != 0) {
        return 2;
    }
    return block_flag != 0 ? 1 : 0;
}

// L2Authd.exe 0x00407290, Src/account.cpp:796-813.
bool CAccount::CheckAge() {
    return g_ServerList.GetAgeLimit() <= age;
}

// L2Authd.exe 0x004072C0, Src/account.cpp:817-825.
bool CAccount::CheckGMUser(in_addr address) {
    return (static_cast<unsigned int>(login_flag) & 0x10U) == 0 ||
           !config.RestrictGMIP ||
           address.s_addr == config.GMIP.s_addr;
}

// L2Authd.exe 0x00407300, Src/account.cpp:829-858.
void CAccount::SetPremiumLevel(int premiumLevel) {
    if (config.useGradeServer) {
        if (config.Country == 2 && config.gameId == 8) {
            user_level = static_cast<std::uint16_t>(
                (user_level & 0x00ffU) |
                ((premiumLevel & 0xffU) << 8));
        }
    } else if (config.useLevelCheck) {
        user_level = premiumLevel == 1 ? 1 : 0;
    } else if (premiumLevel == 1) {
        login_flag |= 0x400;
    }
}

// L2Authd.exe 0x004073C0, Src/account.cpp:862-879.
bool CAccount::CheckFatigueData() {
    if (config.useFatigueSystem &&
        AccountContainer::Inst()->IsFatigueCustomer(
            fcid,
            age,
            auth_flag)) {
        if (AccountContainer::Inst()->IsOverLimit(
                fcid,
                age,
                auth_flag)) {
            return false;
        }
        login_flag |= 0x200;
    }
    return true;
}

// L2Authd.exe 0x00407450, Src/account.cpp:884-887.
void CAccount::SetFreeServer() {
    if (config.FreeServer == 2) {
        pay_stat = 1002;
    }
}

// L2Authd.exe 0x00407470, Src/account.cpp:525-574.
char CAccount::LoadAccount(const char* accountName) {
    char result = Load(accountName);
    if (result != 0) {
        return result;
    }

    std::strncpy(account, accountName, sizeof(account));
    account[sizeof(account) - 1] = 0;
    if (!config.GMCheckMode ||
        (static_cast<unsigned int>(login_flag) & 0x10U) != 0 ||
        (static_cast<unsigned int>(login_flag) & 0x20U) != 0) {
        if ((static_cast<unsigned int>(login_flag) & 3U) == 0) {
            return active_flag == 1 ? 0x24 : LoadEtc();
        }
        return 0x11;
    }
    return 0x10;
}

// L2Authd.exe 0x00407520, Src/account.cpp:62-98.
int CAccount::MakeBlockInfo(char* output) {
    int totalLength = 1;
    int restrictionId = 0;
    unsigned char count = 0;
    char* cursor = output + 1;

    CDBConn connection(g_linDB);
    connection.BindParam(&uid, kInput);
    connection.Bind(&restrictionId);
    std::array<char, 256> restrictionText{};
    connection.Bind(
        restrictionText.data(),
        restrictionText.size());

    bool noMoreRows = false;
    if (connection.Execute(
            "{call ap_GetRestriction (?)}")) {
        while (connection.Fetch(noMoreRows)) {
            std::array<char16_t, 128> wideText{};
            const int converted =
                portable::WindowsAnsiToWideChar(
                    restrictionText.data(),
                    -1,
                    wideText.data(),
                    wideText.size());
            const int characterCount = converted > 0
                ? converted - 1
                : -1;
            const int wideByteCount =
                characterCount * 2 + 2;
            if (totalLength + 4 + wideByteCount >= 0x1000) {
                break;
            }

            std::memcpy(cursor, &restrictionId, 4);
            cursor += 4;
            std::memcpy(
                cursor,
                wideText.data(),
                wideByteCount);
            cursor += wideByteCount;
            ++count;
            totalLength += 4 + wideByteCount;
        }
    }

    std::memcpy(output, &count, 1);
    return static_cast<int>(cursor - output);
}
