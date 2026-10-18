#include "AccountContainer.h"
#include "Shared/src/ScopedLockGuards.h"

#include "Platform/Odbc.h"
#include "Shared/src/Runtime/Time32.h"
#include "AccountDB.h"
#include "Config.h"
#include "DBConn.h"
#include "Log.h"

#include <cstring>

namespace {

constexpr authd::platform::OdbcReturn kSqlSuccess = 0;
constexpr authd::platform::OdbcReturn kSqlError = -1;
constexpr authd::platform::OdbcInteger kSqlNts = -3;
constexpr authd::platform::OdbcSmallInt kStatementHandle = 3;

constexpr authd::platform::OdbcSmallInt kInput = 1;
constexpr authd::platform::OdbcSmallInt kInputOutput = 2;
constexpr authd::platform::OdbcSmallInt kOutput = 4;
constexpr authd::platform::OdbcSmallInt kCInt = -18;
constexpr authd::platform::OdbcSmallInt kSqlInteger = 4;

void BindInteger(
    CDBConn& connection,
    authd::platform::OdbcUSmallInt parameter,
    authd::platform::OdbcSmallInt direction,
    int* value,
    authd::platform::OdbcLen& indicator) {
    indicator = 0;
    authd::platform::SQLBindParameter(
        connection.m_stmt,
        parameter,
        direction,
        kCInt,
        kSqlInteger,
        0,
        0,
        value,
        0,
        &indicator);
}

} // namespace

AccountContainer* AccountContainer::sInstance = nullptr;

// L2Authd.exe 0x00408820, Src/accountcontainer.cpp:15-17.
AccountContainer::AccountContainer()
    : mLock(eCriticalSection, 0) {
    sInstance = this;
}

// L2Authd.exe 0x00408890, Src/accountcontainer.cpp:21-22. The target does not
// clear sInstance. Its compiler-generated member teardown destroys mLock and
// then mCustomerData.
AccountContainer::~AccountContainer() = default;

// L2Authd.exe 0x004088F0, Src/accountcontainer.cpp:39-56.
bool AccountContainer::IsOverLimit(
    int fcid,
    [[maybe_unused]] int age,
    [[maybe_unused]] char authFlag) {
    std::size_t accountCount = 0;
    {
        L2_GUARD(mLock);
        const auto customer = mCustomerData.find(fcid);
        if (customer != mCustomerData.end()) {
            accountCount = customer->second.accountData.size();
        }
    }

    return config.fatigueConcurrentPlayLimit <=
           static_cast<int>(accountCount);
}

// L2Authd.exe 0x00408980, Src/accountcontainer.cpp:26-35.
bool AccountContainer::IsFatigueCustomer(
    int fcid,
    int age,
    char authFlag) {
    if (fcid == 0 ||
        (config.fatigueAffectedAge < age && authFlag != 0)) {
        return false;
    }
    return true;
}

// L2Authd.exe 0x004089B0, Src/accountcontainer.cpp:60-79.
void AccountContainer::InsertFatigueAccount(int fcid, int uid) {
    {
        L2_GUARD(mLock);
        const auto customer = mCustomerData.find(fcid);
        if (customer != mCustomerData.end()) {
            customer->second.accountData.insert(uid);
        } else {
            CustomerTimeData data;
            data.fcid = fcid;
            data.accountData.insert(uid);
            mCustomerData.insert({fcid, data});
        }
    }
}

// L2Authd.exe 0x00408AD0, Src/accountcontainer.cpp:83-98.
void AccountContainer::RemoveFatigueAccount(int fcid, int uid) {
    {
        L2_GUARD(mLock);
        const auto customer = mCustomerData.find(fcid);
        if (customer != mCustomerData.end()) {
            customer->second.accountData.erase(uid);
            if (customer->second.accountData.empty()) {
                mCustomerData.erase(customer);
            }
        }
    }
}

// L2Authd.exe 0x00408B70, Src/accountcontainer.cpp:102-125.
void AccountContainer::SetAccountTime(int uid, int time) {
    runtime::crt::Time32Now();

    bool customerFound = false;
    {
        L2_GUARD(mLock);
        // This lookup really uses uid in the selected EXE even though the map is
        // populated under fcid by InsertFatigueAccount.
        if (mCustomerData.find(uid) != mCustomerData.end()) {
            customerFound = true;
        }
    }

    if (!customerFound) {
        return;
    }

    CDBConn connection(g_linDB);
    char statement[256];
    std::strcpy(
        statement,
        "{CALL dbo.ap_SFatiguePlayTime (?, ?)}");

    authd::platform::OdbcLen uidIndicator = 0;
    BindInteger(connection, 1, kInput, &uid, uidIndicator);
    authd::platform::OdbcLen timeIndicator = 0;
    BindInteger(connection, 2, kInput, &time, timeIndicator);

    // if (authd::platform::SQLExecDirect(
    //         connection.m_stmt,
    //         statement,
    //         kSqlNts) != kSqlSuccess) {
    const auto executeResult = authd::platform::SQLExecDirect(
        connection.m_stmt,
        statement,
        kSqlNts);
    if (executeResult != kSqlSuccess) {
        // Log.AddLog(LOG_ERROR,
        //     "[fatigue] ap_SFatiguePlayTime returned ODBC code %d for account %d (expected 0). The play-time update is not confirmed.",
        //     static_cast<int>(executeResult), uid)
        L2_LOG_ERROR(Log,
            "[fatigue] ap_SFatiguePlayTime returned ODBC code %d for account %d (expected 0); the play-time update is not confirmed.",
            static_cast<int>(executeResult), uid);
        connection.Error(
            kStatementHandle,
            connection.m_stmt,
            statement);
    }
}

// L2Authd.exe 0x00408D20, Src/accountcontainer.cpp:155-224.
void AccountContainer::GetAccountTime(
    int fcid,
    int* totalTime,
    int* lastLogoutTime) {
    runtime::crt::Time32Now();

    UidList accounts;
    {
        L2_GUARD(mLock);
        const auto customer = mCustomerData.find(fcid);
        if (customer != mCustomerData.end()) {
            accounts = customer->second.accountData;
        }
    }

    *totalTime = 0;
    *lastLogoutTime = 0;
    int accountCount = static_cast<int>(accounts.size());

    for (const int uid : accounts) {
        LoginUser* user = nullptr;
        if (accountdb.FindAccount(uid, &user) &&
            user->um_mode == UM_IN_GAME) {
            const int now = runtime::crt::Time32Now();
            if (!config.useDualCharging) {
                *lastLogoutTime += now - user->billStartTime;
            } else {
                *lastLogoutTime += now - user->playStartTime;
            }
        }
        if (user != nullptr) {
            user->ReleaseRef();
        }
    }

    CDBConn connection(g_linDB);
    char statement[256];
    std::strcpy(
        statement,
        "{CALL dbo.ap_SFatigueRestTime (?, ?, ?, ?, ?)}");

    authd::platform::OdbcLen fcidIndicator = 0;
    BindInteger(connection, 1, kInput, &fcid, fcidIndicator);
    authd::platform::OdbcLen countIndicator = 0;
    BindInteger(connection, 2, kInput, &accountCount, countIndicator);
    authd::platform::OdbcLen recoveryIndicator = 0;
    BindInteger(
        connection,
        3,
        kInput,
        &config.fatigueRecoveryTime,
        recoveryIndicator);
    authd::platform::OdbcLen logoutIndicator = 0;
    BindInteger(
        connection,
        4,
        kInputOutput,
        lastLogoutTime,
        logoutIndicator);
    authd::platform::OdbcLen totalIndicator = 0;
    BindInteger(
        connection,
        5,
        kInputOutput,
        totalTime,
        totalIndicator);

    // if (authd::platform::SQLExecDirect(
    //         connection.m_stmt,
    //         statement,
    //         kSqlNts) == kSqlSuccess) {
    const auto executeResult = authd::platform::SQLExecDirect(
        connection.m_stmt,
        statement,
        kSqlNts);
    if (executeResult == kSqlSuccess) {
        // if (authd::platform::SQLFetch(connection.m_stmt) !=
        //     kSqlError) {
        const auto fetchResult = authd::platform::SQLFetch(connection.m_stmt);
        if (fetchResult != kSqlError) {
            // Log.AddLog(
            //     LOG_ERROR,
            //     "Unexpected SP result - (No result set expected.)")
            // Log.AddLog(
            //     LOG_ERROR,
            //     "component=database event=unexpected_procedure_result operation=ap_SFatigueRestTime expected=output_parameters_only expected_fetch_code=-1 actual_fetch_code=%d action=keep_output_parameters",
            //     static_cast<int>(fetchResult))
            // Log.AddLog(
            //     LOG_ERROR,
            //     "[database] ap_SFatigueRestTime returned fetch code %d. Expected -1 for an output-only procedure. Keeping the output parameters.",
            //     static_cast<int>(fetchResult))
            // L2_LOG_ERROR(Log,
            //     "[database] ap_SFatigueRestTime returned fetch code %d. Expected -1 for an output-only procedure. Keeping the output parameters.",
            //     static_cast<int>(fetchResult))
            L2_LOG_ERROR(Log,
                "[fatigue] ap_SFatigueRestTime returned fetch code %d for customer %d (%d tracked accounts); expected -1 for an output-only procedure. Keeping the output parameters.",
                static_cast<int>(fetchResult), fcid, accountCount);
            // Preserve the supplied SQL, including spacing and placeholders.
            // Log.AddLog(LOG_ERROR, "[database] SQL: %s", statement)
            // Log.AddLog(LOG_ERROR, "[database] SQL: \"%s\"", statement)
            L2_LOG_ERROR(Log, "[database] SQL: \"%s\"", statement);
            // Noise: SQL is already logged above.
            // Log.AddLog(
            //     LOG_ERROR,
            //     "DB Statement: %s",
            //     statement)
        }
    } else {
        // Log.AddLog(LOG_ERROR,
        //     "[fatigue] ap_SFatigueRestTime returned ODBC code %d for customer %d (%d tracked accounts. Expected 0). Keeping current time outputs.",
        //     static_cast<int>(executeResult), fcid, accountCount)
        L2_LOG_ERROR(Log,
            "[fatigue] ap_SFatigueRestTime returned ODBC code %d for customer %d (%d tracked accounts; expected 0). Keeping current time outputs.",
            static_cast<int>(executeResult), fcid, accountCount);
        connection.Error(
            kStatementHandle,
            connection.m_stmt,
            statement);
    }
}

// L2Authd.exe 0x00409100, Src/accountcontainer.cpp:128-151.
void AccountContainer::GetLastLogoutTime(
    int uid,
    int* lastLogoutTime) {
    CDBConn connection(g_linDB);
    int value = 0;

    // Both parameters share one indicator in the target call sequence.
    authd::platform::OdbcLen indicator = 0;
    BindInteger(connection, 1, kInput, &uid, indicator);
    BindInteger(connection, 2, kOutput, &value, indicator);

    char statement[256];
    std::strcpy(
        statement,
        "{CALL dbo.ap_GFatigueLastLogoutTime (?, ?) }");

    // if (authd::platform::SQLExecDirect(
    //         connection.m_stmt,
    //         statement,
    //         kSqlNts) == kSqlSuccess) {
    const auto executeResult = authd::platform::SQLExecDirect(
        connection.m_stmt,
        statement,
        kSqlNts);
    if (executeResult == kSqlSuccess) {
        *lastLogoutTime = value;
        connection.ResetHtmt();
    } else {
        *lastLogoutTime = 0;
        // Log.AddLog(LOG_ERROR,
        //     "[fatigue] ap_GFatigueLastLogoutTime returned ODBC code %d for account %d (expected 0). Returning logout time zero.",
        //     static_cast<int>(executeResult), uid)
        L2_LOG_ERROR(Log,
            "[fatigue] ap_GFatigueLastLogoutTime returned ODBC code %d for account %d (expected 0); returning logout time zero.",
            static_cast<int>(executeResult), uid);
        connection.Error(
            kStatementHandle,
            connection.m_stmt,
            statement);
        connection.ResetHtmt();
    }
}
